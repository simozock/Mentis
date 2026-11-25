#include "mobileoutline.h"

#include "document.h"
#include "formulafielddocument.h"
#include "formulatext.h"
#include "imageframe.h"
#include "markdownshortcuts.h"
#include "mindmapmodel.h"
#include "mobilemap.h"
#include "nodegeometry.h"
#include "nodestyle.h"
#include "outlineformats.h"

#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>
#include <memory>

using namespace OutlineFormats;

namespace {

int descendantCount(const MindNode *node) {
    int count = 0;
    for (const auto &child : node->children) count += 1 + descendantCount(child.get());
    return count;
}

// True when `node` is `branch` or one of its descendants.
bool holds(const MindNode *branch, const MindNode *node) {
    for (const MindNode *up = node; up; up = up->parent)
        if (up == branch) return true;
    return false;
}

}  // namespace

MobileOutline::MobileOutline(QObject *parent) : QAbstractListModel(parent) {}

void MobileOutline::setMap(MobileMap *map) {
    if (m_map == map) return;
    if (m_map) disconnect(m_map, nullptr, this, nullptr);
    m_map = map;
    m_folded.clear();
    m_expanded.clear();
    m_focusPath.clear();
    if (m_map) {
        // Another sheet or map: every branch open again (the desktop's expandAll on reset), the whole
        // sheet shown, the rows compact again (FEAT-121 D).
        for (auto signal : {&MobileMap::snapshotChanged, &MobileMap::opened})
            connect(m_map, signal, this, [this] {
                m_folded.clear();
                m_expanded.clear();
                m_focusPath.clear();
                refresh();
            });
        connect(m_map, &MobileMap::contentChanged, this, &MobileOutline::refresh);
        connect(m_map, &MobileMap::selectionChanged, this, &MobileOutline::openAroundSelection);
    }
    emit mapChanged();
    refresh();
}

MindNode *MobileOutline::node(const QString &id) const {
    MindMapModel *model = m_map && m_map->document() ? m_map->document()->model() : nullptr;
    return model && !id.isEmpty() ? model->nodeById(id) : nullptr;
}

bool MobileOutline::heavy(const MindNode *node) const {
    return node && (!node->parent || !node->parent->parent);
}

namespace {
const char *const kLoadedHeavy = "mentisOutlineHeavy";  // the row weights a field was loaded with
}

bool MobileOutline::heavy(const QTextDocument *field, const MindNode *node) const {
    const QVariant loaded = field ? field->property(kLoadedHeavy) : QVariant();
    return loaded.isValid() ? loaded.toBool() : heavy(node);
}

void MobileOutline::loadTitle(QQuickTextDocument *field, const QString &id) const {
    const MindNode *topic = node(id);
    QTextDocument *text = field ? field->textDocument() : nullptr;
    if (!topic || !text) return;
    // As loadRootTitle: the plain text in the default format, then the row's formats over it.
    text->setPlainText(topic->title);
    text->setProperty(kLoadedHeavy, heavy(topic));
    QTextCursor cursor(text);
    const BoldWeights row = heavy(topic) ? kHeavyBold : kNormalBold;
    for (const QTextLayout::FormatRange &range : titleInlineRanges(topic->title, topic->titleHtml, storedBoldWeights(topic).bold, row.bold)) {
        cursor.setPosition(range.start);
        cursor.setPosition(range.start + range.length, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(range.format);
    }
    text->clearUndoRedoStacks();  // the loaded formats are no edit
    text->setModified(false);
}

QString MobileOutline::mathTitle(const QString &id) const {
    const MindNode *topic = node(id);
    return topic && FormulaText::containsMath(topic->title) ? topic->title : QString();
}

bool MobileOutline::applyTitle(QQuickTextDocument *field, const QString &id, const QString &preedit, int cursor) {
    MindNode *topic = node(id);
    QTextDocument *text = field ? field->textDocument() : nullptr;
    if (!topic || !text || (!text->isModified() && preedit.isEmpty())) return false;
    if (const QString source = FormulaFieldDocument::sourceOf(text, preedit, cursor); FormulaText::containsMath(source)) {
        if (preedit.isEmpty()) text->setModified(false);
        if (!topic->parent && source.trimmed().isEmpty()) return false;
        const QString nodeId = topic->id;
        if (!m_map->document()->editTitle(topic, source, QString())) return false;
        m_map->finishEdit(nodeId);
        return true;
    }
    // Android keeps the word being composed out of the document until it is committed: the row
    // shows it already, typed in a copy at the cursor (MobileMap::renameFrom).
    std::unique_ptr<QTextDocument> typed;
    const QTextDocument *source = text;
    if (!preedit.isEmpty()) {
        typed.reset(text->clone());
        QTextCursor at(typed.get());
        at.setPosition(std::clamp(cursor, 0, typed->characterCount() - 1));
        at.insertText(preedit);
        source = typed.get();
    }
    const QString title = source->toPlainText();
    // The desktop keeps the central topic's title when its field is left empty.
    if (!topic->parent && title.trimmed().isEmpty()) return false;
    const BoldWeights row = heavy(text, topic) ? kHeavyBold : kNormalBold;
    const QString html = mergeOutlineFormats(topic->title, topic->titleHtml, *source, NodeGeometry::titleFont(topic), row.bold, storedBoldWeights(topic));
    const QString nodeId = topic->id;
    const bool changed = m_map->document()->editTitle(topic, title, html);
    // The field's text is in the topic now: a later call (a queued one) writes it only when typed again.
    if (preedit.isEmpty()) text->setModified(false);
    if (!changed) return false;
    m_map->finishEdit(nodeId);
    return true;
}

int MobileOutline::boldWeight(QQuickTextDocument *field, const QString &id) const {
    const MindNode *topic = node(id);
    return (heavy(field ? field->textDocument() : nullptr, topic) ? kHeavyBold : kNormalBold).bold;
}

int MobileOutline::formatState(QQuickTextDocument *field, const QString &id, int start, int end) const {
    const MindNode *topic = node(id);
    QTextDocument *text = field ? field->textDocument() : nullptr;
    if (!topic || !text) return 0;
    QTextCursor cursor(text);
    cursor.setPosition(std::clamp(start, 0, text->characterCount() - 1));
    cursor.setPosition(std::clamp(end, 0, text->characterCount() - 1), QTextCursor::KeepAnchor);
    return inlineFlags(cursor.charFormat(), (heavy(text, topic) ? kHeavyBold : kNormalBold).bold);
}

void MobileOutline::applyFormat(QQuickTextDocument *field, const QString &id, int start, int end, int flag, bool on) {
    const MindNode *topic = node(id);
    QTextDocument *text = field ? field->textDocument() : nullptr;
    if (!topic || !text || start == end) return;
    QTextCursor cursor(text);
    cursor.setPosition(std::clamp(std::min(start, end), 0, text->characterCount() - 1));
    cursor.setPosition(std::clamp(std::max(start, end), 0, text->characterCount() - 1), QTextCursor::KeepAnchor);
    if (flag == kInlineHighlight) {
        MarkdownShortcuts::setHighlight(cursor, on);
        return;
    }
    // As OutlineFormatBar's buttons: taking bold off gives back the row's weight, not 400.
    const BoldWeights row = heavy(text, topic) ? kHeavyBold : kNormalBold;
    QTextCharFormat format;
    switch (flag) {
    case kInlineBold: format.setFontWeight(on ? row.bold : row.regular); break;
    case kInlineItalic: format.setFontItalic(on); break;
    case kInlineStrike: format.setFontStrikeOut(on); break;
    case kInlineUnderline: format.setFontUnderline(on); break;
    default: return;
    }
    cursor.mergeCharFormat(format);
}

void MobileOutline::loadNote(QQuickTextDocument *field, const QString &id, const QColor &color) const {
    const MindNode *topic = node(id);
    QTextDocument *text = field ? field->textDocument() : nullptr;
    if (!topic || !text) return;
    if (topic->notesHtml.isEmpty()) text->setPlainText(topic->notes);
    else text->setHtml(topic->notesHtml);
    FormulaText::renderInDocument(text, text->defaultFont(), color);
    text->clearUndoRedoStacks();
    text->setModified(false);
}

void MobileOutline::collect(const MindNode *node, int depth, QList<Row> &rows) const {
    const MindNode *previous = nullptr;
    for (const auto &owned : node->children) {
        const MindNode *child = owned.get();
        if (child->summaryTopic) continue;  // FEAT-052: not one of its parent's rows
        Row row;
        row.id = child->id;
        row.title = child->title;
        row.html = child->titleHtml;
        row.depth = depth;
        row.heavy = heavy(child);
        row.expanded = m_expanded.contains(child->id);
        row.hasChildren = !child->children.empty();
        row.folded = row.hasChildren && m_folded.contains(child->id);
        row.hidden = row.folded ? descendantCount(child) : 0;
        // The desktop draws the line over the first floating topic that follows a branch.
        row.floatingStart = depth == 0 && child->floating && previous && !previous->floating;
        row.canIndent = previous != nullptr;  // MindMapModel::indentNode: under the sibling above
        previous = child;
        row.canOutdent = depth > 0 && !child->parent->summaryTopic;
        row.canMoveUp = MobileMap::outlineSibling(child, -1);
        row.canMoveDown = MobileMap::outlineSibling(child, 1);
        row.markers = child->markers;
        row.labels = child->labels;
        row.task = child->task == MindNode::Task::Done ? 2 : child->task == MindNode::Task::Open ? 1 : 0;
        row.branch = child->branchColor;
        row.tick = NodeStyle::readableTextOn(child->branchColor);
        row.topicLink = !child->topicLink.isEmpty();
        row.webLink = !child->webLink.isEmpty();
        row.files = int(child->files.size());
        if (!child->imagePath.isEmpty() || !child->formula.isEmpty()) {
            row.picture = child->imageSize;
            // What MapScene::topicPicture draws from: the file, or the formula with its font and colours.
            row.pictureKey = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8").arg(child->imagePath, child->formula,
                                 child->formulaBackground.name(QColor::HexArgb), child->text.name(QColor::HexArgb),
                                 NodeGeometry::titleFont(child).key())
                                 .arg(child->imageStyle.rotation).arg(child->imageStyle.outline).arg(child->imageSticker)
                             + QLatin1Char('|') + ImageFrame::effectKey(child->imageStyle.effect);  // FEAT-131
            row.pictureOpacity = child->imageStyle.opacity / qreal(ImageStyle::kMaxOpacity);
        }
        // The desktop's hasNoteText: a note of blanks only is no box.
        if (!child->notes.trimmed().isEmpty()) row.noteKey = child->notes + QChar(0x1f) + child->notesHtml;
        rows.append(row);
        if (!row.folded) collect(child, depth + 1, rows);
    }
}

void MobileOutline::setZoomPercent(int percent) {
    percent = std::clamp(percent, kMinZoomPercent, kMaxZoomPercent);
    if (percent == s_zoomPercent) return;
    s_zoomPercent = percent;
    emit zoomPercentChanged();
}

void MobileOutline::setShowContents(bool on) {
    if (on == s_showContents) return;
    s_showContents = on;
    emit showContentsChanged();
}

void MobileOutline::setFocusPath(const MindNode *node) {
    m_focusPath.clear();
    for (const MindNode *up = node; up && up->parent; up = up->parent) m_focusPath.prepend(up->id);
}

const MindNode *MobileOutline::focusNode() {
    for (qsizetype i = m_focusPath.size() - 1; i >= 0; --i) {
        const MindNode *found = node(m_focusPath[i]);
        if (!found) continue;
        if (!found->parent) break;  // the central topic: the whole sheet
        setFocusPath(found);  // it may have moved since
        return found;
    }
    m_focusPath.clear();
    return nullptr;
}

void MobileOutline::focusOn(const QString &id) {
    const MindNode *topic = node(id);
    if (topic && topic->parent) setFocusPath(topic);
    else m_focusPath.clear();
    refresh();
}

bool MobileOutline::focusUp() {
    if (m_focusPath.isEmpty()) return false;
    const MindNode *entered = focusNode();
    focusOn(entered && entered->parent ? entered->parent->id : QString());
    return true;
}

void MobileOutline::toggleContents(const QString &id) {
    if (rowOf(id) < 0) return;
    if (!m_expanded.remove(id)) m_expanded.insert(id);
    refresh();
}

void MobileOutline::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    emit activeChanged();
    if (m_active) refresh();
}

void MobileOutline::refresh() {
    if (!m_active) return;
    const MindMapModel *model = m_map && m_map->document() ? m_map->document()->model() : nullptr;
    const MindNode *root = model ? model->root() : nullptr;
    const MindNode *entered = root ? focusNode() : nullptr;
    const MindNode *head = entered ? entered : root;
    QList<Row> rows;
    if (head) collect(head, 0, rows);
    const QString rootId = root ? root->id : QString();
    const QString rootTitle = root ? root->title : QString();
    const QString rootHtml = root ? root->titleHtml : QString();
    const QString headId = head ? head->id : QString();
    const QString headTitle = head ? head->title : QString();
    const QString headHtml = head ? head->titleHtml : QString();
    const bool headHeavy = heavy(head);
    QVariantList crumbs;
    for (const MindNode *up = entered ? entered->parent : nullptr; up; up = up->parent)
        crumbs.prepend(QVariantMap{{QStringLiteral("id"), up->id}, {QStringLiteral("title"), up->title}});
    const bool dark = model && NodeStyle::isDarkBackground(model->backdrop().color);
    if (rootId != m_rootId || rootTitle != m_rootTitle || rootHtml != m_rootHtml || dark != m_dark || headId != m_headId
        || headTitle != m_headTitle || headHtml != m_headHtml || headHeavy != m_headHeavy || crumbs != m_crumbs) {
        m_rootId = rootId;
        m_rootTitle = rootTitle;
        m_rootHtml = rootHtml;
        m_headId = headId;
        m_headTitle = headTitle;
        m_headHtml = headHtml;
        m_headHeavy = headHeavy;
        m_crumbs = crumbs;
        m_dark = dark;
        emit rootChanged();
    }

    bool sameTopics = rows.size() == m_rows.size();
    for (qsizetype i = 0; sameTopics && i < rows.size(); ++i) sameTopics = rows[i].id == m_rows[i].id;
    if (!sameTopics) {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
        return;
    }
    for (qsizetype i = 0; i < rows.size(); ++i) {
        if (rows[i] == m_rows[i]) continue;
        m_rows[i] = rows[i];
        emit dataChanged(index(int(i)), index(int(i)));
    }
}

void MobileOutline::openAroundSelection() {
    const MindMapModel *model = m_map && m_map->document() ? m_map->document()->model() : nullptr;
    const MindNode *node = model && !m_map->selectedId().isEmpty() ? model->nodeById(m_map->selectedId()) : nullptr;
    bool opened = false;
    for (const MindNode *up = node ? node->parent : nullptr; up; up = up->parent) opened |= m_folded.remove(up->id);
    // FEAT-121 D: the entered branch shows the selected topic, or gives way to the nearest one that does.
    if (const MindNode *entered = node ? focusNode() : nullptr; entered && !holds(entered, node)) {
        const MindNode *holder = entered->parent;
        while (holder && !holds(holder, node)) holder = holder->parent;
        if (holder && holder->parent) setFocusPath(holder);
        else m_focusPath.clear();
        opened = true;
    }
    if (opened) refresh();
}

int MobileOutline::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant MobileOutline::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Row &row = m_rows[index.row()];
    switch (role) {
    case IdRole: return row.id;
    case DepthRole: return row.depth;
    case Qt::DisplayRole:
    case TitleRole: return row.title;
    case HtmlRole: return row.html;
    case HasChildrenRole: return row.hasChildren;
    case FoldedRole: return row.folded;
    case HiddenCountRole: return row.hidden;
    case FloatingStartRole: return row.floatingStart;
    case CanIndentRole: return row.canIndent;
    case CanOutdentRole: return row.canOutdent;
    case CanMoveUpRole: return row.canMoveUp;
    case CanMoveDownRole: return row.canMoveDown;
    case MarkersRole: return row.markers;
    case TaskRole: return row.task;
    case BranchColorRole: return row.branch;
    case TickColorRole: return row.tick;
    case TopicLinkRole: return row.topicLink;
    case WebLinkRole: return row.webLink;
    case FilesRole: return row.files;
    case PictureWidthRole: return row.picture.width();
    case PictureHeightRole: return row.picture.height();
    case PictureKeyRole: return row.pictureKey;
    case PictureOpacityRole: return row.pictureOpacity;
    case NoteKeyRole: return row.noteKey;
    case LabelsRole: return row.labels;
    case HeavyRole: return row.heavy;
    case ExpandedRole: return row.expanded;
    default: return {};
    }
}

QHash<int, QByteArray> MobileOutline::roleNames() const {
    return {{IdRole, "nodeId"}, {DepthRole, "depth"}, {TitleRole, "title"}, {HtmlRole, "html"},
            {HasChildrenRole, "hasChildren"}, {FoldedRole, "folded"}, {HiddenCountRole, "hiddenCount"},
            {FloatingStartRole, "floatingStart"}, {CanIndentRole, "canIndent"}, {CanOutdentRole, "canOutdent"},
            {CanMoveUpRole, "canMoveUp"}, {CanMoveDownRole, "canMoveDown"}, {MarkersRole, "markers"},
            {TaskRole, "task"}, {BranchColorRole, "branchColor"}, {TickColorRole, "tickColor"}, {TopicLinkRole, "topicLink"},
            {WebLinkRole, "webLink"}, {FilesRole, "files"}, {PictureWidthRole, "pictureWidth"},
            {PictureHeightRole, "pictureHeight"}, {PictureKeyRole, "pictureKey"}, {PictureOpacityRole, "pictureOpacity"},
            {NoteKeyRole, "noteKey"}, {LabelsRole, "labels"}, {HeavyRole, "heavy"},
            {ExpandedRole, "expanded"}};
}

int MobileOutline::rowOf(const QString &id) const {
    for (qsizetype i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].id == id) return int(i);
    return -1;
}

void MobileOutline::toggleFolded(const QString &id) {
    const int row = rowOf(id);
    if (row < 0 || !m_rows[row].hasChildren) return;
    if (!m_folded.remove(id)) m_folded.insert(id);
    refresh();
}
