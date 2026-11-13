#include "mobilemap.h"

#include "bmapstorage.h"
#include "document.h"
#include "filevalidation.h"
#include "formulatext.h"
#include "iconlibrary.h"
#include "maplayout.h"
#include "mapvariants.h"
#include "markdownimport.h"
#include "mapnavigation.h"
#include "mapstructure.h"
#include "markers.h"
#include "mindmapmodel.h"
#include "nodegeometry.h"
#include "nodelinks.h"
#include "nodesearch.h"
#include "performance.h"
#include "richtitle.h"
#include "stickers.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImageReader>
#include <QLocale>
#include <QMimeDatabase>
#include <QMimeData>
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QLoggingCategory>

#include <algorithm>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>

namespace {

// The file through the FileProvider of Qt's manifest template ("<package>.qtprovider", the app's
// files folder among its paths) on Android's clipboard, as ClipData.newUri: the pasting app gets
// the right to read it. https://developer.android.com/develop/ui/views/touch-and-input/copy-paste
QJniObject providerUri(const QJniObject &context, const QString &file) {
    const QString package = context.callObjectMethod("getPackageName", "()Ljava/lang/String;").toString();
    const QJniObject javaFile("java/io/File", "(Ljava/lang/String;)V", QJniObject::fromString(file).object<jstring>());
    QJniObject uri = QJniObject::callStaticObjectMethod(
        "androidx/core/content/FileProvider", "getUriForFile",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/io/File;)Landroid/net/Uri;", context.object(),
        QJniObject::fromString(package + QStringLiteral(".qtprovider")).object<jstring>(), javaFile.object());
    QJniEnvironment().checkAndClearExceptions();  // a file outside the provider's paths throws
    return uri;
}

bool putOnAndroidClipboard(const QString &file) {
    const QJniObject context(QNativeInterface::QAndroidApplication::context().object());
    const QJniObject uri = providerUri(context, file);
    if (!uri.isValid()) return false;
    // The ClipboardManager wants the main thread (it is made with its Looper).
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([context, uri] {
        const QJniObject resolver = context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
        const QJniObject clip = QJniObject::callStaticObjectMethod(
            "android/content/ClipData", "newUri",
            "(Landroid/content/ContentResolver;Ljava/lang/CharSequence;Landroid/net/Uri;)Landroid/content/ClipData;",
            resolver.object(), QJniObject::fromString(QStringLiteral("Mentis")).object<jstring>(), uri.object());
        const QJniObject manager = context.callObjectMethod("getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                                                            QJniObject::fromString(QStringLiteral("clipboard")).object<jstring>());
        if (clip.isValid() && manager.isValid())
            manager.callMethod<void>("setPrimaryClip", "(Landroid/content/ClipData;)V", clip.object());
    });
    return true;
}

// FEAT-114: the file through the same FileProvider in an ACTION_VIEW (open with) or ACTION_SEND
// (share) intent, read permission granted, in the system's chooser.
// https://developer.android.com/training/secure-file-sharing/share-file
bool startAndroidFileIntent(const QString &file, bool share, const QString &chooserTitle) {
    const QJniObject context(QNativeInterface::QAndroidApplication::context().object());
    const QJniObject uri = providerUri(context, file);
    if (!uri.isValid()) return false;
    QString mime = QMimeDatabase().mimeTypeForFile(file, QMimeDatabase::MatchExtension).name();
    if (mime.isEmpty() || mime == QLatin1String("application/octet-stream")) mime = QStringLiteral("*/*");
    const QJniObject action = QJniObject::fromString(share ? QStringLiteral("android.intent.action.SEND") : QStringLiteral("android.intent.action.VIEW"));
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", action.object<jstring>());
    const QJniObject type = QJniObject::fromString(mime);
    if (share) {
        intent.callObjectMethod("setType", "(Ljava/lang/String;)Landroid/content/Intent;", type.object<jstring>());
        intent.callObjectMethod("putExtra", "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;",
                                QJniObject::fromString(QStringLiteral("android.intent.extra.STREAM")).object<jstring>(), uri.object());
    } else {
        intent.callObjectMethod("setDataAndType", "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;", uri.object(),
                                type.object<jstring>());
    }
    constexpr jint kGrantReadUriPermission = 0x00000001;  // Intent.FLAG_GRANT_READ_URI_PERMISSION
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", kGrantReadUriPermission);
    const QJniObject chooser = QJniObject::callStaticObjectMethod(
        "android/content/Intent", "createChooser", "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;",
        intent.object(), QJniObject::fromString(chooserTitle).object<jstring>());
    if (!chooser.isValid()) return false;
    context.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", chooser.object());
    return !QJniEnvironment().checkAndClearExceptions();
}

}  // namespace
#endif

Q_LOGGING_CATEGORY(lcMobileMap, "mentis.mobile.map")

MobileMap::MobileMap(QObject *parent) : QObject(parent), m_document(std::make_unique<Document>()) {
    m_document->setAutoSaveDelay(Performance::autoSaveDelayMs());
    connect(m_document.get(), &Document::historyChanged, this, &MobileMap::historyChanged);
    connect(m_document.get(), &Document::autoSaveFailed, this, [this](const QString &error) {
        setError(tr("Autosave failed: %1").arg(error));
    });
    m_document->setInteractionInProgress([this] { return m_editingTitle; });
    FormulaText::installRendering();  // FEAT-116: the core measures and draws formulas through it
}

MobileMap::~MobileMap() {
    // As close(), without signals: QML may be tearing down what listens to them.
    m_snapshot.reset();
    disconnect(m_document.get(), nullptr, this, nullptr);
    if (!isOpen()) return;
    m_editingTitle = false;  // the closing save takes the open step too
    if (m_document->isDirty())
        if (const Document::Result result = m_document->save(m_path, BmapStorage::SaveReason::Closing); !result.ok)
            qCWarning(lcMobileMap, "FEAT-097 closing save failed: %s", qPrintable(result.error));
    m_document->close();
}

QString MobileMap::title() const { return m_path.isEmpty() ? QString() : QFileInfo(m_path).completeBaseName(); }

QStringList MobileMap::sheets() const {
    QStringList names;
    if (isOpen())
        for (const Document::Sheet &sheet : m_document->sheets()) names.append(sheet.name);
    return names;
}

int MobileMap::currentSheet() const { return isOpen() ? m_document->currentSheet() : -1; }

MindNode *MobileMap::selectedNode() const {
    return isOpen() && !m_selectedId.isEmpty() ? m_document->model()->nodeById(m_selectedId) : nullptr;
}

QList<MindNode *> MobileMap::targetNodes() const {
    if (!m_multiSelect) {
        MindNode *node = selectedNode();
        return node ? QList<MindNode *>{node} : QList<MindNode *>{};
    }
    QList<MindNode *> nodes;
    for (const QString &id : m_multiSelection)
        if (MindNode *node = m_document->model()->nodeById(id)) nodes.append(node);
    return nodes;
}

QStringList MobileMap::selectedIds() const {
    if (m_multiSelect) return m_multiSelection;
    return m_selectedId.isEmpty() ? QStringList() : QStringList{m_selectedId};
}

bool MobileMap::canRemoveSelection() const {
    const QList<MindNode *> nodes = targetNodes();
    return std::any_of(nodes.cbegin(), nodes.cend(), [](const MindNode *node) { return node->parent; });
}

void MobileMap::startMultiSelect() {
    // With nothing selected the mode starts empty (user's request 2026-11-12): the bar at the bottom
    // offers it then too. A selected relationship or boundary has no place in the selection.
    if (!isOpen() || m_multiSelect || !m_selectedRelationId.isEmpty() || !m_selectedBoundaryId.isEmpty()) return;
    const MindNode *node = selectedNode();
    stopFormatPainter();  // as the two tools end each other
    stopRelation();
    m_multiSelect = true;
    m_multiSelection = node ? QStringList{node->id} : QStringList();
    m_pictureSelected = false;
    emit selectionChanged();
}

void MobileMap::stopMultiSelect() {
    if (!m_multiSelect) return;
    m_multiSelect = false;
    m_multiSelection.clear();
    emit selectionChanged();  // only selectedId stays selected
}

void MobileMap::toggleInSelection(const QString &id) {
    if (!m_multiSelect || !isOpen() || !m_document->model()->nodeById(id)) return;
    if (m_multiSelection.removeOne(id)) {
        if (m_multiSelection.isEmpty()) {
            m_selectedId.clear();
            stopMultiSelect();
            return;
        }
        if (id == m_selectedId) m_selectedId = m_multiSelection.constLast();
    } else {
        m_multiSelection.append(id);
        m_selectedId = id;
    }
    emit selectionChanged();
}

void MobileMap::multiSelectAt(const QPointF &mapPoint) {
    const MapSnapshot::Topic *topic = m_snapshot ? m_snapshot->topicAt(mapPoint) : nullptr;
    if (topic) toggleInSelection(topic->node->id);
    else stopMultiSelect();
}

void MobileMap::setCopiedStyle(const MindMapModel::TopicStyle &style) {
    m_copiedStyle = style;
    emit copiedStyleChanged();
}

QString MobileMap::selectedTitle() const {
    const MindNode *node = selectedNode();
    return node ? node->title : QString();
}

bool MobileMap::selectedIsCentral() const {
    const MindNode *node = selectedNode();
    return node && !node->parent;
}

bool MobileMap::canCollapse() const {
    // Document::setCollapsed: not the central topic.
    const MindNode *node = selectedNode();
    return node && node->parent && !node->children.empty();
}

bool MobileMap::selectedCollapsed() const {
    const MindNode *node = selectedNode();
    return node && MapLayout::childrenHidden(node);
}

bool MobileMap::canUndo() const { return isOpen() && m_document->canUndo(); }
bool MobileMap::canRedo() const { return isOpen() && m_document->canRedo(); }
bool MobileMap::isDirty() const { return isOpen() && m_document->isDirty(); }

const MapSnapshot::Topic *MobileMap::selectedTopic() const {
    const MindNode *node = selectedNode();
    return node && m_snapshot ? m_snapshot->topicOf(node) : nullptr;
}

bool MobileMap::open(const QString &path) {
    setError(QString());
    setMessage(QString());
    QElapsedTimer timer;
    timer.start();
    // As MainWindow: the integrity check only when SQLite left a journal (an interrupted write).
    const bool integrityCheck = QFileInfo::exists(path + QStringLiteral("-journal")) || QFileInfo::exists(path + QStringLiteral("-wal"));
    // The snapshot points at the model's topics, which the open replaces.
    m_snapshot.reset();
    setSelected(QString());
    emit snapshotChanged();
    const Document::Result result = m_document->open(path, integrityCheck);
    if (!result.ok) {
        setError(result.fileLocked ? tr("The file is already open in another Mentis window.") : result.error);
        if (isOpen()) rebuildSnapshot(true);  // the map open before, still in the model
        return false;
    }
    m_path = path;
    QDir(openedFilesFolder()).removeRecursively();  // FEAT-114: copies of attached files opened before
    rebuildSnapshot(true);
    m_openMs = int(timer.elapsed());
    qCInfo(lcMobileMap, "FEAT-097 open: %d ms, %d topics, %s", m_openMs, topicCount(), qPrintable(QFileInfo(path).fileName()));
    emit opened();
    emit sheetsChanged();
    emit historyChanged();
    return true;
}

bool MobileMap::openUrl(const QUrl &url, MapLibrary *library) {
    if (!library) return false;
    bool created = false;
    const QString path = library->localCopy(url, &created);
    if (path.isEmpty()) { setError(library->lastError()); return false; }
    if (open(path)) return true;
    if (created) library->discard(path);
    return false;
}

bool MobileMap::switchSheet(int index) {
    if (!isOpen()) return false;
    if (index == m_document->currentSheet()) return true;
    stopFormatPainter();  // as MainWindow on sheetsChanged
    stopRelation();
    stopMultiSelect();
    m_snapshot.reset();  // the switch replaces the model's topics
    const Document::Result result = m_document->switchSheet(index);
    setSelected(QString());
    rebuildSnapshot(true);
    if (!result.ok) { setError(result.error); return false; }
    setError(QString());
    emit sheetsChanged();
    return true;
}

void MobileMap::endSheetTools() {
    stopFormatPainter();
    stopRelation();
    stopMultiSelect();
}

void MobileMap::showOtherSheet() {
    setSelected(QString());
    if (!m_snapshot) rebuildSnapshot(true);
    else {
        emit snapshotChanged();
        emit contentChanged();
    }
    emit sheetsChanged();
}

QVariantList MobileMap::search(const QString &query, bool allSheets, bool wholeWords, bool matchCase, int fields) {
    QVariantList rows;
    if (!isOpen() || query.trimmed().isEmpty()) return rows;
    NodeSearch::Options options;
    options.wholeWords = wholeWords;
    options.matchCase = matchCase;
    options.fields = fields & NodeSearch::AllFields;
    const auto ranges = [](const QList<std::pair<int, int>> &found) {
        QVariantList list;
        for (const auto &[start, length] : found) list.append(QVariant(QVariantList{start, length}));  // not appended as two items
        return list;
    };
    // The desktop's found text (NavigationPanel: the text selection colours, ui-foundations.md).
    const auto html = [](const NodeSearch::Shown &line) {
        QString out;
        int at = 0;
        for (const auto &[start, length] : line.ranges) {
            if (start < at) continue;
            out += line.text.mid(at, start - at).toHtmlEscaped();
            out += QStringLiteral("<span style=\"background-color:#b3d4fc;color:#202630\">%1</span>")
                       .arg(line.text.mid(start, length).toHtmlEscaped());
            at = start + length;
        }
        return out + line.text.mid(at).toHtmlEscaped();
    };
    const QVector<Document::Sheet> &sheets = m_document->sheets();
    for (int i = 0; i < sheets.size(); ++i) {
        const bool current = i == m_document->currentSheet();
        if (!allSheets && !current) continue;
        if (!current && !m_document->ensureSheetLoaded(i).ok) continue;  // unreadable: skipped, as the desktop
        bool named = false;
        for (const NodeSearch::Entry &entry : NodeSearch::entries(sheets[i].id, m_document->sheetContent(i))) {
            const int found = NodeSearch::matchingFields(entry, query, options);
            if (!found) continue;
            if (allSheets && !named) {
                rows.append(QVariantMap{{QStringLiteral("header"), true}, {QStringLiteral("text"), sheets[i].name},
                                        {QStringLiteral("ranges"), QVariantList()}, {QStringLiteral("html"), QString()},
                                        {QStringLiteral("sheet"), i},
                                        {QStringLiteral("id"), QString()}});
                named = true;
            }
            const NodeSearch::Shown line = NodeSearch::resultLine(entry, query, options, found);
            rows.append(QVariantMap{{QStringLiteral("header"), false}, {QStringLiteral("text"), line.text},
                                    {QStringLiteral("ranges"), ranges(line.ranges)}, {QStringLiteral("html"), html(line)},
                                    {QStringLiteral("sheet"), i},
                                    {QStringLiteral("id"), entry.nodeId}});
        }
    }
    return rows;
}

bool MobileMap::reveal(int sheet, const QString &id) {
    if (!isOpen() || sheet < 0 || sheet >= m_document->sheets().size()) return false;
    endSheetTools();
    if (!switchSheet(sheet)) return false;
    MindNode *node = m_document->model()->nodeById(id);
    if (!node) return false;
    bool opened = false;
    for (MindNode *above = node->parent; above; above = above->parent)
        if (MapLayout::childrenHidden(above)) opened = m_document->setCollapsed(above, false) || opened;
    if (opened) finishEdit(node->id);  // the branches and their layout: one Undo step
    else setSelected(node->id);
    return true;
}

void MobileMap::addSheet() {
    if (!isOpen()) return;
    endSheetTools();
    m_snapshot.reset();  // the new sheet replaces the model's topics
    // As MainWindow::addSheet: a Mind Map at once (FEAT-072 Q1), laid out before it enters the
    // history, so its positions are the sheet's starting state.
    const QString variant = MapVariants::all().first().id;
    m_document->addSheet([&variant](MindMapModel &sheet) { MapVariants::apply(sheet, variant, true); },
                         [this] { m_snapshot = std::make_unique<MapSnapshot>(m_document->model(), true); });
    setError(QString());
    showOtherSheet();
}

bool MobileMap::renameSheet(const QString &name) {
    if (!isOpen()) return false;
    endSheetTools();
    if (!m_document->renameSheet(m_document->currentSheet(), name)) return false;
    emit sheetsChanged();
    return true;
}

bool MobileMap::duplicateSheet() {
    if (!isOpen()) return false;
    endSheetTools();
    const QString name = m_document->sheets().value(m_document->currentSheet()).name;
    m_snapshot.reset();  // the copy replaces the model's topics
    const Document::Result result = m_document->duplicateSheet(m_document->currentSheet());
    showOtherSheet();
    if (!result.ok) { setError(result.error); return false; }
    setError(QString());
    setMessage(tr("Sheet \"%1\" duplicated").arg(name));
    return true;
}

bool MobileMap::deleteSheet() {
    if (!isOpen() || m_document->sheets().size() < 2) return false;
    endSheetTools();
    const QString name = m_document->sheets().value(m_document->currentSheet()).name;
    m_snapshot.reset();  // another sheet replaces the model's topics
    const Document::Result result = m_document->removeSheet(m_document->currentSheet());
    showOtherSheet();
    if (!result.ok) { setError(result.error); return false; }
    setError(QString());
    setMessage(tr("Sheet \"%1\" deleted").arg(name));
    return true;
}

bool MobileMap::moveSheet(int from, int to) {
    if (!isOpen()) return false;
    endSheetTools();
    if (!m_document->moveSheet(from, to)) return false;
    emit sheetsChanged();
    return true;
}

bool MobileMap::save() {
    if (!isOpen() || !m_document->isDirty()) return true;
    const Document::Result result = m_document->save(m_path, BmapStorage::SaveReason::Automatic);
    if (!result.ok) { setError(tr("The changes could not be saved: %1").arg(result.error)); return false; }
    return true;
}

bool MobileMap::close() {
    if (!isOpen()) return true;
    // As the desktop's closing save (MainWindow::closeEvent); an unchanged map is left as it is.
    if (m_document->isDirty()) {
        const Document::Result result = m_document->save(m_path, BmapStorage::SaveReason::Closing);
        if (!result.ok) { setError(tr("The changes could not be saved: %1").arg(result.error)); return false; }
    }
    closeDocument();
    return true;
}

void MobileMap::closeWithoutSaving() {
    if (isOpen()) closeDocument();
}

void MobileMap::closeDocument() {
    m_editingTitle = false;
    stopFormatPainter();
    stopRelation();
    m_snapshot.reset();
    m_document->close();
    // The picked images were embedded by the closing save (or are dropped with the changes).
    QDir(pickedImagesFolder()).removeRecursively();
    m_path.clear();
    m_openMs = -1;
    setSelected(QString());
    setError(QString());
    setMessage(QString());
    emit snapshotChanged();
    emit contentChanged();
    emit opened();
    emit sheetsChanged();
    emit historyChanged();
}

void MobileMap::setEditingTitle(bool editing) {
    if (editing == m_editingTitle) return;
    m_editingTitle = editing;
    if (!editing && isOpen()) m_document->flushPendingStep();  // the edit and its title, one step
    emit editingTitleChanged();
}

void MobileMap::selectAt(const QPointF &mapPoint, qreal relationTolerance) {
    const MapSnapshot::Topic *topic = m_snapshot ? m_snapshot->topicAt(mapPoint) : nullptr;
    if (!topic && m_snapshot) {
        const MapSnapshot::Relation *relation = m_snapshot->relationAt(mapPoint, relationTolerance);
        if (relation) { setSelectedRelation(relation->relationship.id); return; }
        if (const MapSnapshot::BoundaryShape *boundary = m_snapshot->boundaryAt(mapPoint, relationTolerance)) {
            setSelectedBoundary(boundary->boundary.id);  // FEAT-115
            return;
        }
    }
    setSelected(topic ? topic->node->id : QString());
    const bool onPicture = topic && topic->geometry->imageRect().translated(topic->position).contains(mapPoint);
    if (onPicture == m_pictureSelected) return;
    m_pictureSelected = onPicture;
    emit selectionChanged();
}

bool MobileMap::pictureSelected() const {
    const MapSnapshot::Topic *topic = m_pictureSelected ? selectedTopic() : nullptr;
    return topic && !topic->geometry->imageRect().isEmpty();  // not after an Undo took it off
}

bool MobileMap::selectedHasNote() const {
    const MindNode *node = selectedNode();
    return node && !node->notes.isEmpty();
}

bool MobileMap::selectedHasCheckbox() const {
    const MindNode *node = selectedNode();
    return node && node->task != MindNode::Task::None;
}

QString MobileMap::selectedWebLink() const {
    const MindNode *node = selectedNode();
    return node ? node->webLink : QString();
}

QString MobileMap::selectedFormula() const {
    const MindNode *node = selectedNode();
    return node ? node->formula : QString();
}

void MobileMap::select(const QString &id) {
    setSelected(isOpen() && m_document->model()->nodeById(id) ? id : QString());
}

bool MobileMap::addChild() {
    MindNode *parent = selectedNode();
    if (!parent) return false;
    MindNode *created = m_document->addChild(parent);
    if (!created) return false;  // under a summary topic, or at the depth limit
    // A child of a collapsed topic would be hidden: the branch opens, in the same step.
    if (MapLayout::childrenHidden(parent)) m_document->setCollapsed(parent, false);
    finishEdit(created->id);
    return true;
}

bool MobileMap::addSibling() {
    MindNode *node = selectedNode();
    if (!node) return false;
    MindNode *created = m_document->addSibling(node);
    if (!created) return false;
    finishEdit(created->id);
    return true;
}

bool MobileMap::rename(const QString &title) {
    MindNode *node = selectedNode();
    // Plain text in B1: a changed title replaces the formatted one (Document::editTitle, no HTML).
    if (!node || title == node->title || !m_document->editTitle(node, title, QString())) return false;
    finishEdit(node->id);
    return true;
}

bool MobileMap::selectedTitleRich() const {
    const MindNode *node = selectedNode();
    // As NodeItem's editor: a formula stays its plain source.
    return node && !node->titleHtml.isEmpty() && !FormulaText::containsMath(node->title);
}

void MobileMap::loadTitle(QQuickTextDocument *field) {
    const MindNode *node = selectedNode();
    QTextDocument *text = field ? field->textDocument() : nullptr;
    if (!node || !text) return;
    if (selectedTitleRich()) text->setHtml(RichTitle::withoutDocumentFont(node->titleHtml));  // FEAT-022: the topic's font holds
    else text->setPlainText(node->title);
    text->setModified(false);
}

bool MobileMap::renameFrom(QQuickTextDocument *field, const QString &preedit, int cursor) {
    MindNode *node = selectedNode();
    QTextDocument *text = field ? field->textDocument() : nullptr;
    if (!node || !text || (!text->isModified() && preedit.isEmpty())) return false;
    // Android keeps the word being composed out of the document until it is committed: the topic
    // shows it already, typed in a copy with the format at the cursor.
    std::unique_ptr<QTextDocument> typed;
    const QTextDocument *source = text;
    if (!preedit.isEmpty()) {
        typed.reset(text->clone());
        QTextCursor at(typed.get());
        at.setPosition(std::clamp(cursor, 0, typed->characterCount() - 1));
        at.insertText(preedit);
        source = typed.get();
    }
    // As NodeItem::finishEditing: the field's HTML (readers drop its document font); a title that
    // became empty or a formula keeps no formatting.
    const QString title = source->toPlainText();
    const QString html = title.trimmed().isEmpty() || FormulaText::containsMath(title) ? QString() : source->toHtml();
    if (!m_document->editTitle(node, title, html)) return false;
    finishEdit(node->id);
    return true;
}

void MobileMap::loadNote(QQuickTextDocument *field) {
    QTextDocument *text = field ? field->textDocument() : nullptr;
    if (!text) return;
    const MindNode *node = selectedNode();
    if (!node) text->clear();
    else if (!node->notesHtml.isEmpty()) text->setHtml(node->notesHtml);  // as NoteEditor
    else text->setPlainText(node->notes);
    text->setModified(false);
}

bool MobileMap::saveNote(QQuickTextDocument *field) {
    MindNode *node = selectedNode();
    QTextDocument *text = field ? field->textDocument() : nullptr;
    if (!node || !text || !text->isModified()) return false;
    // As NoteEditor::commitAndClose: the HTML without the field's font, none for an empty note.
    const QString plain = text->toPlainText();
    const QString html = plain.trimmed().isEmpty() ? QString() : RichTitle::withoutDocumentFont(text->toHtml());
    text->setModified(false);  // written: closing later without another edit makes no step
    if (!m_document->setNote(node, plain, html)) return false;
    finishEdit(node->id);  // the note's icon changes the topic's size: the layout joins the step
    return true;
}

bool MobileMap::removeSelected() {
    if (!m_selectedRelationId.isEmpty()) {
        if (!m_document->removeRelations({m_selectedRelationId})) return false;
        setSelectedRelation(QString());
        rebuildSnapshot(false);
        return true;
    }
    if (!m_selectedBoundaryId.isEmpty()) {
        // FEAT-115: its topics stay; the room it kept goes, so the layout joins the step.
        if (!m_document->removeBoundaries({m_selectedBoundaryId})) return false;
        finishEdit(QString());
        return true;
    }
    if (m_multiSelect) {
        // FEAT-111, as MainWindow::removeSelected: every selected topic with its branch but the
        // central one, one step; the topic chosen before is selected after.
        QList<MindNode *> nodes = targetNodes();
        nodes.removeIf([](const MindNode *node) { return !node->parent; });
        if (nodes.isEmpty()) return false;
        const MindNode *next = Document::nodeAfterRemoval(nodes);
        const QString nextId = next ? next->id : QString();
        if (!m_document->removeNodes(nodes)) return false;
        m_multiSelect = false;
        m_multiSelection.clear();
        finishEdit(nextId);
        return true;
    }
    MindNode *node = selectedNode();
    if (node && pictureSelected()) {
        m_pictureSelected = false;
        if (!m_document->removeVisual(node)) return false;
        finishEdit(node->id);
        return true;
    }
    if (!node || !node->parent) return false;  // the central topic is not removed
    const QString parent = node->parent->id;
    if (!m_document->removeNodes({node})) return false;
    // removeNodes deleted the topic, which the snapshot still points at: it is made again first.
    finishEdit(parent);
    return true;
}

bool MobileMap::removeNote() {
    MindNode *node = selectedNode();
    if (!node || !m_document->setNote(node, QString(), QString())) return false;
    finishEdit(node->id);  // the icon goes: the topic's size changes
    return true;
}

bool MobileMap::placePicture(const QString &id, const QPointF &mapPoint) {
    MindNode *node = isOpen() ? m_document->model()->nodeById(id) : nullptr;
    const MapSnapshot::Topic *topic = node && m_snapshot ? m_snapshot->topicOf(node) : nullptr;
    // NodeItem's drop: a topic without a title keeps its picture where it is.
    if (!topic || topic->geometry->imageRect().isEmpty() || node->title.trimmed().isEmpty()) return false;
    const int zone = topic->geometry->imageDropZoneAt(mapPoint - topic->position);
    if (zone < 0 || !m_document->setImagePlacement(node, static_cast<MindNode::ImagePlacement>(zone))) return false;
    finishEdit(node->id);
    if (!m_pictureSelected) {
        m_pictureSelected = true;
        emit selectionChanged();
    }
    return true;
}

bool MobileMap::resizePicture(const QSizeF &size) {
    MindNode *node = pictureSelected() ? selectedNode() : nullptr;
    if (!node || !m_document->setImageSize(node, size)) return false;
    // NodeItem's release: a hand width narrower than the content becomes the one needed.
    if (node->customWidth > 0) {
        const qreal needed = MapSnapshot::measure(node)->textRect().width();
        if (needed > node->customWidth) node->customWidth = std::min(std::ceil(needed), kMaxManualNodeWidthPx);
    }
    finishEdit(node->id);  // the same topic: the picture stays selected
    return true;
}

bool MobileMap::copyPicture() {
    const MindNode *node = pictureSelected() ? selectedNode() : nullptr;
    if (!node) return false;
    // The original of an embedded image (the map draws its 512 px thumbnail); a sticker's SVG at
    // twice its shown size. A formula has no image file (FEAT-116: its code is copied, copyFormulaCode).
    QString path = node->imagePath;
    if (!node->imageAssetId.isEmpty()) {
        const QString original = BmapStorage::originalAssetPath(m_document->assetSourcePath(), node->imageAssetId, node->imageOriginalName);
        if (QFileInfo(original).isFile()) path = original;
    }
    QImage image;
    if (!path.isEmpty()) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        if (reader.format() == "svg") reader.setScaledSize((node->imageSize * 2).toSize());
        image = reader.read();
    }
    bool copied = !image.isNull();
#ifdef Q_OS_ANDROID
    if (copied) {
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/clipboard");
        const QString file = folder + QStringLiteral("/picture.png");
        copied = QDir().mkpath(folder) && image.save(file, "PNG") && putOnAndroidClipboard(file);
    }
#else
    if (copied) QGuiApplication::clipboard()->setImage(image);
#endif
    setMessage(copied ? tr("Image copied") : tr("The image could not be copied."));
    return copied;
}

QColor MobileMap::selectedFormulaBackground() const {
    const MindNode *node = selectedNode();
    return node && !node->formula.isEmpty() ? node->formulaBackground : QColor(Qt::white);
}

QString MobileMap::fixedFontFamily() { return QFontDatabase::systemFont(QFontDatabase::FixedFont).family(); }

bool MobileMap::setFormula(const QString &code) {
    MindNode *node = selectedNode();
    const QString next = code.trimmed();
    if (!node || next.isEmpty()) return false;
    // MainWindow::editFormula: a new or changed formula starts from its natural size; the same keeps
    // the size chosen. The background stays (white for a new one, Document::setFormula's default).
    const QSizeF size = next != node->formula ? FormulaText::boxSize(next, NodeGeometry::titleFont(node)) : QSizeF();
    const QColor background = node->formula.isEmpty() ? QColor(Qt::white) : node->formulaBackground;
    if (!m_document->setFormula(node, next, background, size)) return false;
    finishEdit(node->id);
    return true;
}

bool MobileMap::removeFormula() {
    MindNode *node = selectedNode();
    if (!node || node->formula.isEmpty() || !m_document->setFormula(node, QString(), node->formulaBackground)) return false;
    m_pictureSelected = false;
    finishEdit(node->id);
    return true;
}

bool MobileMap::copyFormulaCode() {
    const MindNode *node = selectedNode();
    if (!node || node->formula.isEmpty()) return false;
    QGuiApplication::clipboard()->setText(node->formula);
    setMessage(tr("LaTeX code copied"));
    return true;
}

QVariantList MobileMap::markerChoices(const QString &marker) const {
    QVariantList choices;
    const QString series = Markers::seriesOf(marker);
    for (const Markers::Series &s : Markers::series()) {
        if (s.id != series) continue;
        for (const QString &value : s.values) {
            const QString option = series + QLatin1Char(':') + value;
            choices.append(QVariantMap{{QStringLiteral("marker"), option}, {QStringLiteral("label"), Markers::label(option)},
                                       {QStringLiteral("checked"), option == marker}});
        }
    }
    return choices;
}

bool MobileMap::setMarker(const QString &marker) {
    MindNode *node = selectedNode();
    if (!node || !Markers::isValid(marker) || !m_document->setMarker(node, Markers::seriesOf(marker), marker)) return false;
    finishEdit(node->id);
    return true;
}

bool MobileMap::removeMarker(const QString &marker) {
    MindNode *node = selectedNode();
    if (!node || !m_document->setMarker(node, Markers::seriesOf(marker), QString())) return false;
    finishEdit(node->id);
    return true;
}

bool MobileMap::toggleCollapsed() {
    MindNode *node = selectedNode();
    if (!canCollapse() || !m_document->setCollapsed(node, !node->collapsed)) return false;
    finishEdit(node->id);
    return true;
}

bool MobileMap::toggleCheckbox() {
    // MainWindow::toggleTaskBoxes: the topics without a checkbox get one; when every one has it, it
    // goes from all (FEAT-111: every selected topic in the mode).
    const QList<MindNode *> nodes = targetNodes();
    if (nodes.isEmpty()) return false;
    QList<MindNode *> without, with;
    for (MindNode *node : nodes) (node->task == MindNode::Task::None ? without : with).append(node);
    const bool added = !without.isEmpty();
    if (!m_document->setTask(added ? without : with, added ? MindNode::Task::Open : MindNode::Task::None)) return false;
    finishEdit(m_selectedId);  // the box takes room beside the title
    return true;
}

bool MobileMap::toggleTaskDone() {
    MindNode *node = selectedNode();
    if (!node || !m_document->toggleTask(node)) return false;
    finishEdit(node->id);
    return true;
}

QString MobileMap::webAddress(const QString &typed) { return NodeLinks::webAddress(typed); }

void MobileMap::linkEdited(const MindNode *node) {
    // setLinks does not mark the document changed (on the desktop MainWindow's linksEdited does):
    // marked here, the change, its layout and the autosave go as every other edit.
    m_document->markChanged();
    finishEdit(node->id);
}

bool MobileMap::setWebLink(const QString &typed) {
    MindNode *node = selectedNode();
    const QString address = NodeLinks::webAddress(typed);
    if (!node || address.isEmpty() || !m_document->model()->setLinks({{node, {}, address}})) return false;
    linkEdited(node);
    return true;
}

bool MobileMap::removeWebLink() {
    MindNode *node = selectedNode();
    if (!node || !m_document->model()->setLinks({{node, {}, QString()}})) return false;
    linkEdited(node);
    return true;
}

bool MobileMap::openWebLink() {
    const MindNode *node = selectedNode();
    if (!node || node->webLink.isEmpty()) return false;
    const QUrl url(node->webLink);
    // BUG-043: a file from someone else may hold any address; only http(s) and mailto are opened.
    if (NodeLinks::canOpen(url) && QDesktopServices::openUrl(url)) return true;
    setMessage(tr("Cannot open %1").arg(node->webLink));
    return false;
}

QStringList MobileMap::selectedLabels() const {
    const MindNode *node = selectedNode();
    return node ? node->labels : QStringList();
}

bool MobileMap::labelsAutoSort() const { return Labels::autoSort(); }

void MobileMap::setLabelsAutoSort(bool on) {
    if (on == Labels::autoSort()) return;
    Labels::setAutoSort(on);
    emit labelsAutoSortChanged();
}

QStringList MobileMap::documentLabels() const { return isOpen() ? m_document->documentLabels() : QStringList(); }

QStringList MobileMap::labelSuggestions(const QString &field, const QStringList &all) { return Labels::suggestions(field, all); }

QString MobileMap::labelsWithAdded(const QString &field, const QString &label) { return Labels::withAdded(field, label); }

bool MobileMap::setLabels(const QString &field) {
    MindNode *node = selectedNode();
    if (!node || !m_document->setLabels(node, Labels::typed(field))) return false;
    finishEdit(node->id);  // the pills change the topic's room in the layout
    return true;
}

QVariantList MobileMap::selectedFiles() const {
    QVariantList files;
    if (const MindNode *node = selectedNode())
        for (const NodeFile &file : node->files)
            files.append(QVariantMap{{QStringLiteral("name"), file.name}, {QStringLiteral("size"), QLocale().formattedDataSize(file.size)}});
    return files;
}

QVariantMap MobileMap::pickFile(const QUrl &url) {
    QVariantMap result{{QStringLiteral("ok"), false}};
    QString name, error;
    const QString target = copyPicked(url, &name, &error);
    const auto fail = [&](const QString &reason) {
        if (!target.isEmpty()) QDir(QFileInfo(target).absolutePath()).removeRecursively();
        result.insert(QStringLiteral("reason"), tr("\"%1\" was not inserted. %2").arg(name, reason));
        return result;
    };
    if (target.isEmpty()) return fail(error);
    const FileValidation::Check check = FileValidation::anyFile(target);  // FEAT-039, as the desktop
    if (!check.ok) return fail(check.reason);
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("path"), target);
    result.insert(QStringLiteral("large"), check.large);
    result.insert(QStringLiteral("largeReason"), tr("\"%1\": %2 Insert it anyway?").arg(name, check.largeReason));
    return result;
}

bool MobileMap::attachFiles(const QStringList &paths) {
    MindNode *node = selectedNode();
    if (!node || paths.isEmpty() || !m_document->addFiles(node, paths)) return false;
    finishEdit(node->id);  // the paper clip changes the topic's size
    return true;
}

bool MobileMap::removeFile(int index) {
    MindNode *node = selectedNode();
    if (!node || index < 0 || index >= node->files.size() || !m_document->removeFile(node, index)) return false;
    finishEdit(node->id);
    return true;
}

bool MobileMap::fileIsRisky(int index) const {
    const MindNode *node = selectedNode();
    return node && index >= 0 && index < node->files.size()
           && FileValidation::isRisky(FileValidation::diskFileName(node->files[index].name, index + 1));
}

QString MobileMap::openedFilesFolder() const {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("opened-files"));
}

QString MobileMap::openedCopy(int index) {
    MindNode *node = selectedNode();
    if (!node || index < 0 || index >= node->files.size()) return {};
    const NodeFile file = node->files[index];
    const QString source = m_document->fileSource(node, index);
    if (source.isEmpty()) {
        setMessage(tr("\"%1\" cannot be read from the document.").arg(file.name));
        return {};
    }
    // As MainWindow::openNodeFile: a folder of its own, the copy named attach01.pdf... (BUG-043);
    // the name in the map comes from the document.
    const QString folder = QDir(openedFilesFolder()).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString copy = QDir(folder).filePath(FileValidation::diskFileName(file.name, index + 1));
    if (!QDir().mkpath(folder) || !QFile::copy(source, copy)) {
        setMessage(tr("\"%1\" cannot be copied to open it.").arg(file.name));
        return {};
    }
    return copy;
}

bool MobileMap::openFile(int index) {
    const QString copy = openedCopy(index);
    if (copy.isEmpty()) return false;
    const QString name = selectedNode()->files[index].name;
#ifdef Q_OS_ANDROID
    const bool opened = startAndroidFileIntent(copy, false, tr("Open \"%1\" with").arg(name));
#else
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(copy));
#endif
    if (!opened) setMessage(tr("No application opens \"%1\".").arg(name));
    return opened;
}

bool MobileMap::shareFile(int index) {
    const QString copy = openedCopy(index);
    if (copy.isEmpty()) return false;
    const QString name = selectedNode()->files[index].name;
#ifdef Q_OS_ANDROID
    const bool shared = startAndroidFileIntent(copy, true, tr("Share \"%1\"").arg(name));
#else
    const bool shared = QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(copy).absolutePath()));
#endif
    if (!shared) setMessage(tr("\"%1\" could not be shared.").arg(name));
    return shared;
}

QString MobileMap::selectedTopicLink() const {
    const MindNode *node = selectedNode();
    return node ? node->topicLink : QString();
}

int MobileMap::sheetOfTopic(const QString &id) {
    if (!isOpen() || id.isEmpty()) return -1;
    if (m_document->model()->nodeById(id)) return m_document->currentSheet();
    for (int i = 0; i < m_document->sheets().size(); ++i) {
        if (i == m_document->currentSheet() || !m_document->ensureSheetLoaded(i).ok) continue;
        if (m_document->sheetContent(i).contains(id)) return i;
    }
    return -1;
}

bool MobileMap::topicHasLink(int sheet, const QString &id) {
    if (!isOpen() || sheet < 0 || sheet >= m_document->sheets().size()) return false;
    if (sheet == m_document->currentSheet()) {
        const MindNode *node = m_document->model()->nodeById(id);
        return node && !node->topicLink.isEmpty();
    }
    if (!m_document->ensureSheetLoaded(sheet).ok) return false;
    return !m_document->sheetContent(sheet).value(id).value(QStringLiteral("topicLink")).toString().isEmpty();
}

bool MobileMap::setTopicLink(int sheet, const QString &id, bool alsoBack) {
    MindNode *node = selectedNode();
    if (!node || id.isEmpty() || id == node->id || sheet < 0 || sheet >= m_document->sheets().size()) return false;
    const QString sourceId = node->id;
    m_document->flushPendingStep();
    QList<MindMapModel::LinkChange> changes{{node, id, {}}};
    std::optional<Document::LinkSheetChange> other;
    // At most one topic link per topic: the back link does not replace one already there.
    if (alsoBack && !topicHasLink(sheet, id)) {
        if (sheet == m_document->currentSheet()) {
            if (MindNode *target = m_document->model()->nodeById(id)) changes.append({target, sourceId, {}});
        } else {
            // In another sheet its new state goes into the same step (Document::commitLinkStep, as
            // MainWindow::editTopicLink: one Undo takes both links back).
            const QString sheetId = m_document->sheets()[sheet].id;
            MapHistory::Snapshot content = m_document->historySnapshot(sheetId);
            QJsonObject target = content.value(id);
            if (!target.isEmpty()) {
                target.insert(QStringLiteral("topicLink"), sourceId);
                content.insert(id, target);
                other = Document::LinkSheetChange{sheetId, content};
            }
        }
    }
    const bool changed = m_document->model()->setLinks(changes);
    if (!changed && !other) return false;
    setMessage(QString());
    m_document->markChanged();
    rebuildSnapshot(false, true);  // the chain button changes the topic's size
    m_document->commitLinkStep(other);
    setSelected(sourceId);
    emit selectionChanged();
    setMessage(tr("Link added"));
    return true;
}

bool MobileMap::removeTopicLink() {
    MindNode *node = selectedNode();
    if (!node || !m_document->model()->setLinks({{node, QString(), {}}})) return false;
    linkEdited(node);
    return true;
}

bool MobileMap::revealFollowed(int sheet, const QString &id) {
    m_following = true;
    const bool shown = reveal(sheet, id);
    m_following = false;
    return shown;
}

void MobileMap::clearLinkHistory() {
    if (m_linkBack.isEmpty()) return;
    m_linkBack.clear();
    emit linkHistoryChanged();
}

bool MobileMap::followTopicLink() {
    const MindNode *node = selectedNode();
    if (!node || node->topicLink.isEmpty()) return false;
    const std::pair<QString, QString> from{m_document->sheets()[m_document->currentSheet()].id, node->id};
    const QString target = node->topicLink;
    const int sheet = sheetOfTopic(target);
    if (sheet < 0 || !revealFollowed(sheet, target)) {
        markBrokenLinks();  // the sheets read now may show it gone
        emit contentChanged();
        setMessage(tr("The linked topic no longer exists"));
        return false;
    }
    m_linkBack.append(from);
    emit linkHistoryChanged();
    return true;
}

bool MobileMap::followBack() {
    // A place deleted since is skipped.
    while (!m_linkBack.isEmpty()) {
        const auto [sheetId, topicId] = m_linkBack.takeLast();
        int sheet = -1;
        for (int i = 0; i < m_document->sheets().size(); ++i)
            if (m_document->sheets()[i].id == sheetId) sheet = i;
        if (sheet >= 0 && (sheet == m_document->currentSheet() || m_document->ensureSheetLoaded(sheet).ok)
            && (sheet == m_document->currentSheet() ? m_document->model()->nodeById(topicId) != nullptr
                                                     : m_document->sheetContent(sheet).contains(topicId))
            && revealFollowed(sheet, topicId)) {
            emit linkHistoryChanged();
            return true;
        }
    }
    emit linkHistoryChanged();
    return false;
}

void MobileMap::markBrokenLinks() {
    if (!m_snapshot) return;
    QSet<const MindNode *> broken;
    std::optional<QSet<QString>> elsewhere;  // the ids of the other sheets read so far, made once
    bool unread = false;
    for (const MapSnapshot::Topic &topic : m_snapshot->topics()) {
        const QString &target = topic.node->topicLink;
        if (target.isEmpty() || m_document->model()->nodeById(target)) continue;
        if (!elsewhere) {
            elsewhere.emplace();
            for (int i = 0; i < m_document->sheets().size(); ++i) {
                if (i == m_document->currentSheet()) continue;
                if (!m_document->sheets()[i].loaded) { unread = true; continue; }
                const MapHistory::Snapshot content = m_document->sheetContent(i);
                for (auto it = content.cbegin(); it != content.cend(); ++it) elsewhere->insert(it.key());
            }
        }
        if (!elsewhere->contains(target) && !unread) broken.insert(topic.node);
    }
    m_snapshot->setBrokenLinks(broken);
}

bool MobileMap::insertSticker(const QString &path) {
    const QList<MindNode *> nodes = targetNodes();  // FEAT-111: MainWindow::insertSticker on a list
    if (nodes.isEmpty() || path.isEmpty()) return false;
    const FileValidation::Check check = FileValidation::image(path);  // FEAT-039, as the desktop
    if (!check.ok) {
        setError(check.reason);
        return false;
    }
    const QSizeF size = Stickers::insertionSize(path);
    bool changed = false;
    for (MindNode *node : nodes) changed = m_document->setSticker(node, path, size) || changed;
    if (!changed) return false;
    finishEdit(m_selectedId);  // stickers and layout, one step
    return true;
}

ReorderTarget MobileMap::moveTarget(const QString &id, const QPointF &mapPoint) const {
    const MindNode *node = isOpen() && m_snapshot ? m_document->model()->nodeById(id) : nullptr;
    if (!node) return {};
    ReorderTarget::Scene scene;
    scene.model = m_document->model();
    scene.layout = m_snapshot->layout();
    // The topic drawn on top first, as topicAt.
    QHash<const MindNode *, const MapSnapshot::Topic *> drawn;
    const QList<MapSnapshot::Topic> &topics = m_snapshot->topics();
    for (auto it = topics.crbegin(); it != topics.crend(); ++it) {
        // FEAT-115 step 2: a summary topic's branch takes no topic from outside it
        // (MindMapModel::moveNode), so it offers no place.
        if (summaryTopicOf(it->node)) continue;
        scene.nodes.append(const_cast<MindNode *>(it->node));
        drawn.insert(it->node, &*it);
    }
    scene.box = [&drawn](const MindNode *topic) {
        const MapSnapshot::Topic *at = drawn.value(topic);
        return at ? at->geometry->shapeRect().translated(at->position) : QRectF();
    };
    scene.shown = [&drawn](const MindNode *topic) { return drawn.contains(topic); };
    scene.branchStart = [&drawn](const MindNode *topic, bool right) {
        const MapSnapshot::Topic *at = drawn.value(topic);
        return at ? at->position + at->geometry->branchAnchor(right) : QPointF();
    };
    return ReorderTarget::at(scene, node, mapPoint);
}

bool MobileMap::moveTopic(const QString &id, const QPointF &mapPoint) {
    const ReorderTarget target = moveTarget(id, mapPoint);
    if (target.kind != ReorderTarget::Kind::Child && target.kind != ReorderTarget::Kind::Insert) return false;
    MindNode *node = m_document->model()->nodeById(id);
    if (!m_document->moveNode(node, target.parent, target.row, target.side)) return false;
    finishEdit(id);  // as MainWindow::reorderNode: the move and the layout, one step
    return true;
}

QString MobileMap::selectedRelationLabel() const {
    const MapSnapshot::Relation *relation = selectedRelation();
    return relation ? relation->relationship.label : QString();
}

const MapSnapshot::Relation *MobileMap::selectedRelation() const {
    return m_snapshot && !m_selectedRelationId.isEmpty() ? m_snapshot->relationOf(m_selectedRelationId) : nullptr;
}

bool MobileMap::renameRelation(const QString &label) {
    const MapSnapshot::Relation *relation = selectedRelation();
    if (!relation) return false;
    Relationship changed = relation->relationship;
    changed.label = label.simplified();
    if (changed.label == relation->relationship.label || !m_document->updateRelations({changed})) return false;  // one step
    setMessage(QString());
    rebuildSnapshot(false);
    emit selectionChanged();  // the label is the selection's
    return true;
}

bool MobileMap::bendRelation(bool fromHandle, const QPointF &control) {
    const MapSnapshot::Relation *relation = selectedRelation();
    if (!relation) return false;
    Relationship changed = relation->relationship;
    if (fromHandle) changed.fromControl = control - relation->geometry.fromCentre;
    else changed.toControl = control - relation->geometry.toCentre;
    if (changed == relation->relationship || !m_document->updateRelations({changed})) return false;  // one step
    setMessage(QString());
    rebuildSnapshot(false);
    emit selectionChanged();  // the selected line moved
    return true;
}

QString MobileMap::selectedBoundaryTitle() const {
    const MapSnapshot::BoundaryShape *boundary = selectedBoundary();
    return boundary ? boundary->boundary.title : QString();
}

const MapSnapshot::BoundaryShape *MobileMap::selectedBoundary() const {
    return m_snapshot && !m_selectedBoundaryId.isEmpty() ? m_snapshot->boundaryOf(m_selectedBoundaryId) : nullptr;
}

bool MobileMap::addBoundary() {
    const QList<MindNode *> nodes = targetNodes();
    if (nodes.isEmpty() || !m_snapshot) return false;
    stopFormatPainter();
    stopRelation();
    const MapLayout *layout = m_snapshot->layout();
    const QStringList created = m_document->addBoundaries(nodes, [layout](const MindNode *node) { return layout->sideOf(node); });
    if (created.isEmpty()) {
        // MainWindow::addBoundary's reasons.
        const bool onlyCentral = std::all_of(nodes.cbegin(), nodes.cend(), [](const MindNode *node) { return !node->parent; });
        const bool table = std::any_of(nodes.cbegin(), nodes.cend(), [](const MindNode *node) { return MapStructure::insideTreeTable(node); });
        setMessage(onlyCentral ? tr("The central topic cannot have a boundary")
                   : table     ? tr("Tree tables cannot have boundaries")
                               : tr("These topics already have a boundary"));
        return false;
    }
    finishEdit(QString());  // the boundary's room and the layout, one step
    setSelectedBoundary(created.front());
    return true;
}

bool MobileMap::renameBoundary(const QString &title) {
    const MapSnapshot::BoundaryShape *boundary = selectedBoundary();
    if (!boundary) return false;
    Boundary changed = boundary->boundary;
    changed.title = title.simplified();
    if (changed.title == boundary->boundary.title || !m_document->updateBoundaries({changed})) return false;
    const QString id = changed.id;
    finishEdit(QString());  // the tab's room changes the layout: one step with it
    setSelectedBoundary(id);
    return true;
}

bool MobileMap::addSummary() {
    const QList<MindNode *> nodes = targetNodes();
    if (nodes.isEmpty() || !m_snapshot) return false;
    stopFormatPainter();
    stopRelation();
    const MapLayout *layout = m_snapshot->layout();
    const QList<MindNode *> created = m_document->addSummaries(nodes, [layout](const MindNode *node) { return layout->sideOf(node); });
    if (created.isEmpty()) {
        // MainWindow::addSummary's reasons.
        const bool onlyCentral = std::all_of(nodes.cbegin(), nodes.cend(), [](const MindNode *node) { return !node->parent; });
        const bool table = std::any_of(nodes.cbegin(), nodes.cend(), [](const MindNode *node) {
            return node->parent && (MapStructure::typeOf(node->parent) == MapStructure::Type::TreeTable || MapStructure::insideTreeTable(node));
        });
        setMessage(onlyCentral ? tr("The central topic cannot have a summary")
                   : table     ? tr("Tree tables cannot have summaries")
                               : tr("These topics already have a summary"));
        return false;
    }
    stopMultiSelect();  // the new summary topic alone is selected, for its title
    // The summary's room and the layout in one step, which stays open for the title while it is
    // asked (editingTitle), as addChild.
    finishEdit(created.front()->id);
    return true;
}

void MobileMap::startRelation(bool keep) {
    const MindNode *node = selectedNode();
    if (!node) return;
    stopFormatPainter();
    stopMultiSelect();
    if (m_relationSource == node->id && m_relationKept == keep) return;
    m_relationSource = node->id;
    m_relationKept = keep;
    emit relationToolChanged();
}

void MobileMap::stopRelation() {
    if (m_relationSource.isEmpty()) return;
    m_relationSource.clear();
    m_relationKept = false;
    emit relationToolChanged();
}

void MobileMap::relateAt(const QPointF &mapPoint, qreal tolerance) {
    const MapSnapshot::Topic *topic = m_snapshot ? m_snapshot->topicAt(mapPoint) : nullptr;
    const MapSnapshot::BoundaryShape *boundary = !topic && m_snapshot ? m_snapshot->boundaryAt(mapPoint, tolerance) : nullptr;
    if (!topic && !boundary) { stopRelation(); return; }
    const QString source = m_relationSource;
    const QString target = topic ? topic->node->id : boundary->boundary.id;
    if (target == source) return;  // a self-relationship is refused (FEAT-053)
    if (!m_relationKept) stopRelation();
    if (m_document->addRelation(source, target).isEmpty()) return;  // one step
    setMessage(QString());
    rebuildSnapshot(false);  // the layout does not change: only the new line is drawn
}

void MobileMap::startFormatPainter(bool sticky) {
    const MindNode *node = selectedNode();
    if (!node) return;
    stopRelation();
    stopMultiSelect();
    setCopiedStyle(m_document->model()->topicStyle(node));  // MainWindow::copyStyleOf
    if (m_formatPainter && m_formatPainterSticky == sticky) return;
    m_formatPainter = true;
    m_formatPainterSticky = sticky;
    emit formatPainterChanged();
}

void MobileMap::stopFormatPainter() {
    if (!m_formatPainter) return;
    m_formatPainter = false;
    m_formatPainterSticky = false;
    emit formatPainterChanged();
}

void MobileMap::paintAt(const QPointF &mapPoint) {
    const MapSnapshot::Topic *topic = m_snapshot ? m_snapshot->topicAt(mapPoint) : nullptr;
    if (!topic || !m_copiedStyle) { stopFormatPainter(); return; }
    MindNode *node = m_document->model()->nodeById(topic->node->id);
    const bool sticky = m_formatPainterSticky;
    if (!sticky) stopFormatPainter();
    // MainWindow::pasteStyleOn and its styleEdited listener: the change opens the step, finishEdit
    // lays out and closes it.
    if (!node || !m_document->model()->pasteStyle(*m_copiedStyle, {node})) return;
    m_document->markChanged();
    finishEdit(m_selectedId);
}

bool MobileMap::selectNeighbour(int key) {
    if (!isOpen() || !m_snapshot) return false;
    // As MainWindow::eventFilter: with none selected the arrow starts from the central topic.
    MindNode *selected = selectedNode();
    MindNode *from = selected ? selected : m_document->model()->root();
    MapNavigation::Scene scene;
    scene.layout = m_snapshot->layout();
    QHash<const MindNode *, const MapSnapshot::Topic *> drawn;
    for (const MapSnapshot::Topic &topic : m_snapshot->topics()) {
        scene.nodes.append(const_cast<MindNode *>(topic.node));
        drawn.insert(topic.node, &topic);
    }
    scene.shown = [&drawn](const MindNode *topic) { return drawn.contains(topic); };
    scene.centre = [&drawn](const MindNode *topic) {
        const MapSnapshot::Topic *at = drawn.value(topic);
        return at ? at->position : QPointF();
    };
    scene.box = [&drawn](const MindNode *topic) {
        const MapSnapshot::Topic *at = drawn.value(topic);
        return at ? at->geometry->shapeRect().translated(at->position) : QRectF();
    };
    const MindNode *to = MapNavigation::neighbour(scene, from, Qt::Key(key));
    if (!to && selected) return false;
    setSelected(to ? to->id : from->id);
    return true;
}

void MobileMap::selectCentral() {
    if (isOpen()) setSelected(m_document->model()->root()->id);
}

bool MobileMap::moveAmongSiblings(int direction) {
    MindNode *node = selectedNode();
    if (!node || !m_snapshot) return false;
    const MapLayout *layout = m_snapshot->layout();
    if (!m_document->moveAmongSiblings(node, direction, [layout](const MindNode *at) { return layout->sideOf(at); }))
        return false;
    finishEdit(node->id);
    return true;
}

bool MobileMap::addTopicBefore() {
    MindNode *node = selectedNode();
    MindNode *created = node ? m_document->addSiblingBefore(node) : nullptr;
    if (!created) return false;
    finishEdit(created->id);
    return true;
}

bool MobileMap::addParentTopic() {
    MindNode *node = selectedNode();
    MindNode *created = node ? m_document->insertParent(node) : nullptr;
    if (!created) return false;
    finishEdit(created->id);
    return true;
}

void MobileMap::copyStyle() {
    if (const MindNode *node = selectedNode()) setCopiedStyle(m_document->model()->topicStyle(node));
}

bool MobileMap::pasteStyle() {
    const QList<MindNode *> nodes = targetNodes();  // FEAT-111: every selected topic in the mode
    if (nodes.isEmpty() || !m_copiedStyle || !m_document->model()->pasteStyle(*m_copiedStyle, nodes)) return false;
    m_document->markChanged();
    finishEdit(m_selectedId);
    return true;
}

bool MobileMap::copyBranches() {
    const QList<MindNode *> nodes = targetNodes();
    if (nodes.isEmpty()) return false;
    m_copiedBranches = m_document->branchesData(nodes);
    m_copiedBranchesText = m_document->branchesText(nodes);
    auto *mime = new QMimeData;
    mime->setData(Document::branchesMimeType(), m_copiedBranches);  // dropped by Android's clipboard
    mime->setText(m_copiedBranchesText);
    QGuiApplication::clipboard()->setMimeData(mime);  // the clipboard owns mime
    setMessage(nodes.size() == 1 ? tr("Topic copied") : tr("Topics copied"));
    return true;
}

bool MobileMap::pasteBranches() {
    MindNode *parent = selectedNode();
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!parent) return false;
    const int row = static_cast<int>(parent->children.size());
    MindNode *created = nullptr;
    // The clipboard's branches, or this app's copy while the clipboard still holds its text (on
    // Android the branches' format does not reach the clipboard); other text is read as Markdown.
    QByteArray branches = mime && mime->hasFormat(Document::branchesMimeType()) ? mime->data(Document::branchesMimeType()) : QByteArray();
    if (branches.isEmpty() && mime && mime->hasText() && !m_copiedBranches.isEmpty()
        && mime->text().trimmed() == m_copiedBranchesText.trimmed())
        branches = m_copiedBranches;
    if (!branches.isEmpty()) {
        created = m_document->insertBranches(parent, row, Document::branchesFromData(branches));
    } else if (mime && mime->hasText() && !mime->text().trimmed().isEmpty()) {
        const Document::PasteResult result = m_document->pasteMarkdown(parent, row, mime->text());
        if (result.tooLarge) {
            setMessage(tr("Text too long: you can paste at most %1 items at a time.").arg(MarkdownImport::kMaxPastedItems));
            return false;
        }
        created = result.first;
    }
    if (!created) {
        setMessage(tr("Nothing to paste"));
        return false;
    }
    // As addChild: under a collapsed topic the branch opens, in the same step.
    if (MapLayout::childrenHidden(parent)) m_document->setCollapsed(parent, false);
    finishEdit(created->id);  // a topic out of the selection: the mode ends (FEAT-111)
    return true;
}

QStringList MobileMap::imageFilters() const { return {FileValidation::imageFilter()}; }

QString MobileMap::pickedImagesFolder() const {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("picked-images"));
}

QString MobileMap::copyPicked(const QUrl &url, QString *name, QString *error) const {
    // A content:// URI is read by its whole text (MapLibrary::localCopy); Android's content file
    // engine gives its display name.
    const QString source = url.isLocalFile() ? url.toLocalFile() : url.toString();
    *name = QFileInfo(source).fileName();
    if (name->isEmpty()) *name = url.fileName().isEmpty() ? tr("Image") : url.fileName();
    QFile input(source);
    if (source.isEmpty() || !input.open(QIODevice::ReadOnly)) { *error = input.errorString(); return {}; }
    // A folder of its own for each copy: the copy keeps the picked file's name, which the map keeps
    // as the image's original name and the library shows.
    const QString folder = QDir(pickedImagesFolder()).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(folder)) { *error = tr("The copy cannot be written."); return {}; }
    const QString target = QDir(folder).filePath(*name);
    QFile output(target);
    if (!output.open(QIODevice::WriteOnly)) { *error = output.errorString(); return {}; }
    constexpr qint64 kChunkBytes = 1 << 20;
    bool written = true;
    while (written && !input.atEnd()) {
        const QByteArray chunk = input.read(kChunkBytes);
        written = !chunk.isEmpty() && output.write(chunk) == chunk.size();
    }
    written = written && input.error() == QFileDevice::NoError && output.flush();
    output.close();
    if (!written) {
        QDir(folder).removeRecursively();
        *error = tr("The copy cannot be written.");
        return {};
    }
    return target;
}

QVariantMap MobileMap::pickImage(const QUrl &url) {
    QVariantMap result{{QStringLiteral("ok"), false}};
    QString name, error;
    const QString target = copyPicked(url, &name, &error);
    const auto fail = [&](const QString &reason) {
        if (!target.isEmpty()) QDir(QFileInfo(target).absolutePath()).removeRecursively();
        result.insert(QStringLiteral("reason"), tr("\"%1\" was not inserted. %2").arg(name, reason));
        return result;
    };
    if (target.isEmpty()) return fail(error);
    const FileValidation::Check check = FileValidation::image(target);  // FEAT-039, as the desktop
    if (!check.ok) return fail(check.reason);
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("path"), target);
    result.insert(QStringLiteral("large"), check.large);
    result.insert(QStringLiteral("largeReason"), tr("\"%1\": %2 Insert it anyway?").arg(name, check.largeReason));
    return result;
}

QVariantMap MobileMap::pickLibraryFile(const QUrl &url, bool marker) {
    QVariantMap result{{QStringLiteral("ok"), false}};
    QString name, error;
    const QString target = copyPicked(url, &name, &error);
    const auto fail = [&](const QString &reason) {
        if (!target.isEmpty()) QDir(QFileInfo(target).absolutePath()).removeRecursively();
        result.insert(QStringLiteral("reason"), tr("\"%1\": %2").arg(name, reason));  // IconPage::checkedFiles
        return result;
    };
    if (target.isEmpty()) return fail(error);
    const IconLibrary::Check check = IconLibrary::checkFile(marker ? IconLibrary::Kind::Marker : IconLibrary::Kind::Sticker, target);
    if (!check.ok) return fail(check.reason);
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("path"), target);
    result.insert(QStringLiteral("name"), IconLibrary::displayName(target));
    result.insert(QStringLiteral("title"), tr("Name of \"%1\"").arg(name));
    result.insert(QStringLiteral("small"), check.small);
    result.insert(QStringLiteral("smallReason"), tr("\"%1\": %2 Add it anyway?").arg(name, check.smallReason));
    return result;
}

bool MobileMap::addToLibrary(const QString &path, const QString &name, bool marker) {
    const IconLibrary::Kind kind = marker ? IconLibrary::Kind::Marker : IconLibrary::Kind::Sticker;
    // IconPage::ensureOwnCollection: the first own collection, made when there is none.
    QString collection;
    for (const IconLibrary::Collection &c : IconLibrary::collections(kind))
        if (!c.builtIn) { collection = c.id; break; }
    if (collection.isEmpty()) collection = IconLibrary::addCollection(kind, marker ? tr("My markers") : tr("My stickers"));
    const QString shown = name.trimmed().isEmpty() ? IconLibrary::displayName(path) : name;
    const QString copy = collection.isEmpty() ? QString() : IconLibrary::addFile(kind, collection, path, shown);
    if (copy.isEmpty()) {
        setError(tr("The file could not be added to the library."));
        return false;
    }
    // As MainWindow's filesAdded: one file added goes into the selected topic too.
    if (selectedNode()) marker ? toggleMarker(copy, IconLibrary::displayName(copy), collection) : insertSticker(copy);
    return true;
}

QStringList MobileMap::selectedMarkers() const {
    // FEAT-111: in the mode the markers every selected topic has, which a tap takes off from all.
    const QList<MindNode *> nodes = targetNodes();
    if (nodes.isEmpty()) return {};
    QStringList common = nodes.first()->markers;
    for (const MindNode *node : nodes)
        common.removeIf([node](const QString &marker) { return !node->markers.contains(marker); });
    return common;
}

bool MobileMap::toggleMarker(const QString &path, const QString &name, const QString &collectionId) {
    // MainWindow::chooseIcon: the marker of its series on every target, or off from all of them when
    // every one has it already (FEAT-111: all the selected topics in the mode).
    const QList<MindNode *> nodes = targetNodes();
    if (nodes.isEmpty()) return false;
    QByteArray data;
    const QString marker = IconLibrary::markerFor({path, name}, collectionId, &data);
    if (marker.isEmpty() || !Markers::isValid(marker)) return false;
    if (Markers::isCustom(marker)) m_document->model()->addMarkerImage(Markers::customImage(marker), data);
    const bool everyHasIt = std::all_of(nodes.cbegin(), nodes.cend(), [&marker](const MindNode *node) { return node->markers.contains(marker); });
    bool changed = false;
    for (MindNode *node : nodes) changed = m_document->setMarker(node, Markers::seriesOf(marker), everyHasIt ? QString() : marker) || changed;
    if (!changed) return false;
    finishEdit(m_selectedId);  // markers and layout, one step
    return true;
}

bool MobileMap::insertImage(const QString &path) {
    if (!m_snapshot || path.isEmpty()) return false;
    bool changed = false;
    for (MindNode *node : targetNodes()) {  // FEAT-111: the same picture in every selected topic
        const MapSnapshot::Topic *topic = m_snapshot->topicOf(node);
        if (!topic || !m_document->setImage(node, path, NodeGeometry::fittedImageSize(path, topic->geometry->imageRoomWidth())))
            continue;
        changed = true;
        // MainWindow::setNodeImage: a hand width narrower than the new content becomes the one needed.
        if (node->customWidth > 0) {
            const qreal needed = MapSnapshot::measure(node)->textRect().width();
            if (needed > node->customWidth) node->customWidth = std::min(std::ceil(needed), kMaxManualNodeWidthPx);
        }
    }
    if (!changed) return false;
    finishEdit(m_selectedId);  // pictures and layout, one step
    return true;
}

bool MobileMap::undo() { return applyStep(false); }
bool MobileMap::redo() { return applyStep(true); }

bool MobileMap::applyStep(bool redo) {
    if (!isOpen()) return false;
    stopMultiSelect();  // FEAT-111: only the last topic tapped stays selected
    const QString selected = m_selectedId;
    const QString selectedRelation = m_selectedRelationId;
    const QString selectedBoundary = m_selectedBoundaryId;
    m_snapshot.reset();  // the step may replace the model's topics
    const Document::StepResult result = redo ? m_document->redo() : m_document->undo();
    rebuildSnapshot(false);
    setSelected(m_document->model()->nodeById(selected) ? selected : QString());
    if (!selectedRelation.isEmpty() && m_snapshot->relationOf(selectedRelation)) setSelectedRelation(selectedRelation);
    if (!selectedBoundary.isEmpty() && m_snapshot->boundaryOf(selectedBoundary)) setSelectedBoundary(selectedBoundary);
    emit selectionChanged();  // the title or the collapsed state may be the step's
    switch (result.outcome) {
    case Document::StepOutcome::Applied:
        setMessage(QString());
        return true;
    case Document::StepOutcome::OtherSheet: {
        // As MainWindow::pointToSheet: the step stays where it is and the user is told where.
        const int index = m_document->sheetIndex(result.sheetId);
        const QString name = index >= 0 ? m_document->sheets().at(index).name : QString();
        setMessage(redo ? tr("The change to redo is in the sheet \"%1\"").arg(name)
                        : tr("The change to undo is in the sheet \"%1\"").arg(name));
        return false;
    }
    case Document::StepOutcome::Failed:
        setError(result.error);
        return false;
    case Document::StepOutcome::Nothing:
        return false;
    }
    return false;
}

void MobileMap::finishEdit(const QString &selected) {
    setMessage(QString());
    rebuildSnapshot(false, true);  // lays the sheet out and writes the positions into the model
    // The edit and its layout are one Undo step; while a title is asked, the step stays open.
    if (!m_editingTitle) m_document->flushPendingStep();
    setSelected(selected);
    emit selectionChanged();  // the selected title may have changed
}

void MobileMap::setSelectedRelation(const QString &id) {
    stopMultiSelect();
    if (id == m_selectedRelationId && m_selectedId.isEmpty() && m_selectedBoundaryId.isEmpty()) return;
    m_selectedRelationId = id;
    m_selectedBoundaryId.clear();
    m_selectedId.clear();
    m_pictureSelected = false;
    emit selectionChanged();
}

void MobileMap::setSelectedBoundary(const QString &id) {
    stopMultiSelect();
    if (id == m_selectedBoundaryId && m_selectedId.isEmpty() && m_selectedRelationId.isEmpty()) return;
    m_selectedBoundaryId = id;
    m_selectedRelationId.clear();
    m_selectedId.clear();
    m_pictureSelected = false;
    emit selectionChanged();
}

void MobileMap::setSelected(const QString &id) {
    // FEAT-111: selecting a topic out of the selection (a key, an edit's new topic) ends the mode.
    const bool leftMode = m_multiSelect && !m_multiSelection.contains(id);
    if (leftMode) {
        m_multiSelect = false;
        m_multiSelection.clear();
    }
    if (!m_following && id != m_selectedId) clearLinkHistory();  // FEAT-114
    if (!leftMode && id == m_selectedId && m_selectedRelationId.isEmpty() && m_selectedBoundaryId.isEmpty()) return;
    m_selectedRelationId.clear();
    m_selectedBoundaryId.clear();
    m_selectedId = id;
    m_pictureSelected = false;  // another topic: its picture is chosen only by a tap on it
    emit selectionChanged();
}

void MobileMap::setError(const QString &error) {
    if (error == m_lastError) return;
    m_lastError = error;
    emit lastErrorChanged();
}

void MobileMap::setMessage(const QString &message) {
    if (message == m_message) return;
    m_message = message;
    emit messageChanged();
}

void MobileMap::rebuildSnapshot(bool newSheet, bool writePositions) {
    m_snapshot.reset();  // before the new one is measured: it points at the model's topics
    m_snapshot = std::make_unique<MapSnapshot>(m_document->model(), writePositions);
    markBrokenLinks();  // FEAT-114
    // FEAT-110: the tool's start removed (deleted, or by an Undo step) turns it off.
    if (relationTool() && !m_document->model()->nodeById(m_relationSource)) stopRelation();
    // FEAT-111: a selected topic removed (with a deleted branch) leaves the selection.
    if (m_multiSelect) {
        const qsizetype before = m_multiSelection.size();
        m_multiSelection.removeIf([this](const QString &id) { return !m_document->model()->nodeById(id); });
        if (m_multiSelection.size() != before) {
            if (m_multiSelection.isEmpty()) {
                m_selectedId.clear();
                m_multiSelect = false;
            } else if (!m_multiSelection.contains(m_selectedId)) {
                m_selectedId = m_multiSelection.constLast();
            }
            emit selectionChanged();
        }
    }
    if (newSheet) emit snapshotChanged();
    emit contentChanged();
}
