#include "iconpanel.h"

#include "markers.h"
#include "modaldialog.h"
#include "stickereditor.h"
#include "stickers.h"
#include "uitheme.h"

#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const QString kItemMime = QStringLiteral("application/x-mentis-icon-item");
const QString kCollectionMime = QStringLiteral("application/x-mentis-icon-collection");

QString settingsKey(IconLibrary::Kind kind) {
    return kind == IconLibrary::Kind::Sticker ? QStringLiteral("stickers/collapsed") : QStringLiteral("markers/collapsed");
}

bool drawnMarker(IconLibrary::Kind kind, const QString &path) {
    return kind == IconLibrary::Kind::Marker && !path.contains(QLatin1Char('/'));
}

QIcon drawItemIcon(IconLibrary::Kind kind, const QString &path, int px);

// Le celle si ricreano a ogni modifica delle raccolte: le icone (con il contorno degli adesivi,
// qualche millisecondo ciascuna) si tengono per file, misura e data di modifica.
QIcon itemIcon(IconLibrary::Kind kind, const QString &path, int px) {
    static QHash<QString, QIcon> cache;
    const QString key = path + QLatin1Char('@') + QString::number(px) + QLatin1Char('@')
                        + QString::number(QFileInfo(path).lastModified().toMSecsSinceEpoch());
    if (const auto found = cache.constFind(key); found != cache.cend()) return *found;
    if (cache.size() > 2000) cache.clear();
    const QIcon icon = drawItemIcon(kind, path, px);
    cache.insert(key, icon);
    return icon;
}

QIcon drawItemIcon(IconLibrary::Kind kind, const QString &path, int px) {
    QPixmap pixmap(px * 2, px * 2);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    if (drawnMarker(kind, path)) {
        QPainter painter(&pixmap);
        Markers::paint(&painter, QRectF(0, 0, px, px), path);
        return QIcon(pixmap);
    }
    QImageReader reader(path);
    const QSize size = reader.size();
    if (size.isValid()) reader.setScaledSize(size.scaled(px * 2, px * 2, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull()) return QIcon(pixmap);
    // Adesivi con il contorno, come sulla mappa: si staccano anche dal fondo scuro del pannello.
    if (kind == IconLibrary::Kind::Sticker) image = Stickers::outlined(image);
    image.setDevicePixelRatio(2.0);
    QPainter painter(&pixmap);
    const QSizeF logical = QSizeF(image.size()) / 2.0;
    painter.drawImage(QPointF((px - logical.width()) / 2, (px - logical.height()) / 2), image);
    return QIcon(pixmap);
}

// Avvio di un trascinamento oltre la soglia del sistema, con il dato del tipo indicato.
class DragStarter {
public:
    void press(QMouseEvent *event) { if (event->button() == Qt::LeftButton) m_start = event->position().toPoint(); }
    // true se il trascinamento e' partito (il clic non vale piu').
    bool move(QWidget *source, QMouseEvent *event, const QString &mime, const QString &value, const QPixmap &pixmap) {
        if (!(event->buttons() & Qt::LeftButton) || m_start.isNull()) return false;
        if ((event->position().toPoint() - m_start).manhattanLength() < QApplication::startDragDistance()) return false;
        m_start = {};
        auto *drag = new QDrag(source);
        auto *data = new QMimeData;
        data->setData(mime, value.toUtf8());
        drag->setMimeData(data);
        if (!pixmap.isNull()) drag->setPixmap(pixmap);
        drag->exec(Qt::MoveAction);
        return true;
    }
    void release() { m_start = {}; }
private:
    QPoint m_start;
};

class Cell final : public QToolButton {
public:
    Cell(const QString &path, QWidget *parent) : QToolButton(parent), m_path(path) {}
    QString path() const { return m_path; }
protected:
    void mousePressEvent(QMouseEvent *event) override { m_drag.press(event); QToolButton::mousePressEvent(event); }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (m_drag.move(this, event, kItemMime, m_path, icon().pixmap(iconSize()))) { setDown(false); return; }
        QToolButton::mouseMoveEvent(event);
    }
private:
    QString m_path;
    DragStarter m_drag;
};

class Header final : public QToolButton {
public:
    Header(const QString &id, QWidget *parent) : QToolButton(parent), m_id(id) {}
protected:
    void mousePressEvent(QMouseEvent *event) override { m_drag.press(event); QToolButton::mousePressEvent(event); }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (m_drag.move(this, event, kCollectionMime, m_id, grab())) { setDown(false); return; }
        QToolButton::mouseMoveEvent(event);
    }
private:
    QString m_id;
    DragStarter m_drag;
};

// Una raccolta: intestazione e griglia; riceve i rilasci di elementi e di raccolte.
class Section final : public QWidget {
public:
    Section(IconPage *page, const QString &id) : QWidget(page), m_page(page), m_id(id) { setAcceptDrops(true); }
    QString id() const { return m_id; }
    QList<Cell *> cells;
protected:
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (event->mimeData()->hasFormat(kItemMime) || event->mimeData()->hasFormat(kCollectionMime)) event->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *event) override { event->acceptProposedAction(); }
    void dropEvent(QDropEvent *event) override {
        const QMimeData *data = event->mimeData();
        if (data->hasFormat(kCollectionMime)) {
            const QString moved = QString::fromUtf8(data->data(kCollectionMime));
            QMetaObject::invokeMethod(m_page, [page = m_page, moved, before = m_id] { page->dropCollection(moved, before); }, Qt::QueuedConnection);
        } else {
            const QPoint at = event->position().toPoint();
            int index = -1;
            for (int i = 0; i < cells.size(); ++i) {
                const QRect rect(cells[i]->mapTo(this, QPoint(0, 0)), cells[i]->size());
                if (rect.contains(at)) { index = at.x() > rect.center().x() ? i + 1 : i; break; }
            }
            const QString path = QString::fromUtf8(data->data(kItemMime));
            // Dopo il rilascio: la pagina si ricostruisce e questa sezione sparisce.
            QMetaObject::invokeMethod(m_page, [page = m_page, path, id = m_id, index] { page->dropItem(path, id, index); }, Qt::QueuedConnection);
        }
        event->acceptProposedAction();
    }
private:
    IconPage *m_page;
    QString m_id;
};
}  // namespace

IconPage::IconPage(IconLibrary::Kind kind, QWidget *parent) : QWidget(parent), m_kind(kind) {
    setObjectName(kind == IconLibrary::Kind::Sticker ? QStringLiteral("StickerPage") : QStringLiteral("MarkerPage"));
    setStyleSheet(UiTheme::qss(QStringLiteral(
        "QToolButton#IconCell { background:transparent; border:0; border-radius:6px; }"
        "QToolButton#IconCell:hover { background:@surfaceRaised; }"
        "QToolButton#IconCell[active=\"true\"] { background:@divider; }"
        "QToolButton#IconHeader { color:@text; background:transparent; border:0; font-size:13px; font-weight:600; padding:4px 0; text-align:left; }"
        "QToolButton#IconHeader:hover { color:@accent; }"
        "QLabel { color:@textMuted; font-size:12px; background:transparent; }"
        "QPushButton { color:@text; background:@surfaceRaised; border:0; border-radius:4px; min-height:30px; padding:0 12px; font-size:13px; }"
        "QPushButton:hover { background:@divider; }"
        "QToolButton#IconMore { color:@text; background:@surfaceRaised; border:0; border-radius:4px; min-height:30px; min-width:30px; font-size:15px; }"
        "QToolButton#IconMore::menu-indicator { image:none; }"
        "QComboBox { color:@text; background:@surfaceSunken; border:1px solid @divider; border-radius:4px; min-height:28px; padding:0 8px; font-size:13px; }"
        "QLineEdit#IconSearch { color:@text; background:@surfaceSunken; border:1px solid @divider; border-radius:4px; min-height:28px; padding:0 8px; font-size:13px; }"
        "QLineEdit#IconSearch:focus { border-color:@accent; }")));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    // The group and collection commands come first, above the list (user request 2026-11-04).
    auto *actions = new QHBoxLayout;
    m_newCollection = new QPushButton(kind == IconLibrary::Kind::Sticker ? tr("New collection") : tr("New group"), this);
    connect(m_newCollection, &QPushButton::clicked, this, &IconPage::newCollection);
    m_more = new QToolButton(this);
    m_more->setObjectName(QStringLiteral("IconMore"));
    m_more->setText(QStringLiteral("…"));
    m_more->setToolTip(tr("More"));
    m_more->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(m_more);
    menu->addAction(tr("Add from file…"), this, [this] { addFiles({}); });
    menu->addAction(tr("Import a folder…"), this, &IconPage::importFolder);
    menu->addSeparator();
    menu->addAction(tr("Restore built-in collections"), this, &IconPage::restoreBuiltIns);
    m_more->setMenu(menu);
    actions->addWidget(m_newCollection, 1);
    actions->addWidget(m_more);
    layout->addLayout(actions);
    // Tappa 3 (user request 2026-11-05): filters by the item's and the collection's name as you type.
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("IconSearch"));
    m_search->setPlaceholderText(kind == IconLibrary::Kind::Sticker ? tr("Search stickers") : tr("Search markers"));
    m_search->setClearButtonEnabled(true);
    // Rebuilding the whole grid takes about 25 ms here (all 256 stickers), more on old PCs: the
    // grid follows a pause in the typing, and at once when the field is cleared.
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(kSearchDelayMs);
    connect(m_searchTimer, &QTimer::timeout, this, &IconPage::reload);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (text.isEmpty()) { m_searchTimer->stop(); reload(); }
        else m_searchTimer->start();
    });
    layout->addWidget(m_search);
    if (kind == IconLibrary::Kind::Sticker) {
        // "Categoria" mostra tutte le raccolte o una sola.
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Category")));
        m_category = new QComboBox(this);
        m_category->setObjectName(QStringLiteral("StickerCategory"));
        connect(m_category, &QComboBox::activated, this, [this] { reload(); });
        row->addWidget(m_category, 1);
        layout->addLayout(row);
    }
    m_content = new QWidget(this);
    m_content->setAcceptDrops(true);
    m_sections = new QVBoxLayout(m_content);
    m_sections->setContentsMargins(0, 0, 0, 0);
    m_sections->setSpacing(2);
    layout->addWidget(m_content);
    layout->addStretch();
    // Minimo esplicito: il layout non deve alzarlo alla larghezza delle colonne di adesso.
    setMinimumWidth(minimumPageWidth());
    reload();
}

int IconPage::columnsFor(int width) const {
    const bool sticker = m_kind == IconLibrary::Kind::Sticker;
    const int cell = (sticker ? kStickerCellPx : kMarkerCellPx) + kCellSpacingPx;
    return std::max(sticker ? kStickerColumns : kMarkerColumns, (width + kCellSpacingPx) / cell);
}

int IconPage::minimumPageWidth() const {
    const auto grid = [](int cell, int columns) { return columns * cell + (columns - 1) * kCellSpacingPx; };
    return std::max(grid(kStickerCellPx, kStickerColumns), grid(kMarkerCellPx, kMarkerColumns));
}

// La griglia occupa quanto il pannello le da': il minimo resta quello delle colonne di base, cosi'
// il pannello si puo' di nuovo stringere dopo averlo allargato.
QSize IconPage::minimumSizeHint() const {
    return {minimumPageWidth(), QWidget::minimumSizeHint().height()};
}

void IconPage::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (const int columns = columnsFor(width()); columns != m_columns) {
        m_columns = columns;
        reload();  // icone in cache: rifare le celle costa poco
    }
}

QString IconPage::customGroup(const QString &collectionId) {
    QString group = collectionId;
    return group.replace(QLatin1Char(':'), QLatin1Char('-'));
}

QString IconPage::markerFor(const IconLibrary::Item &item, const QString &collectionId, QByteArray *data) {
    if (!item.path.contains(QLatin1Char('/'))) return item.path;
    QFile file(item.path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.readAll();
    const QString sha = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    Markers::registerImage(sha, bytes);
    Markers::setCustomLabel(sha, item.name);
    if (data) *data = bytes;
    return Markers::customMarker(customGroup(collectionId), sha);
}

void IconPage::reload() {
    while (QLayoutItem *item = m_sections->takeAt(0)) {
        if (QWidget *widget = item->widget()) { widget->hide(); widget->deleteLater(); }
        delete item;
    }
    m_shown.clear();
    m_cells.clear();
    m_collections = IconLibrary::collections(m_kind);
    const QStringList collapsed = QSettings().value(settingsKey(m_kind)).toStringList();
    QString only;
    if (m_category) {
        const QString current = m_category->currentData().toString();
        m_category->clear();
        m_category->addItem(tr("All"), QString());
        for (const IconLibrary::Collection &c : std::as_const(m_collections))
            if (!c.hidden) m_category->addItem(c.name, c.id);
        const int index = m_category->findData(current);
        m_category->setCurrentIndex(std::max(0, index));
        only = m_category->currentData().toString();
    }
    const bool sticker = m_kind == IconLibrary::Kind::Sticker;
    const int cellPx = sticker ? kStickerCellPx : kMarkerCellPx;
    if (m_columns <= 0) m_columns = sticker ? kStickerColumns : kMarkerColumns;
    const int columns = m_columns;
    const int iconPx = sticker ? cellPx - 12 : cellPx - 10;
    const QString query = m_search ? m_search->text().simplified() : QString();
    for (const IconLibrary::Collection &all : std::as_const(m_collections)) {
        if (all.hidden || (!only.isEmpty() && all.id != only)) continue;
        IconLibrary::Collection collection = all;
        if (!query.isEmpty() && !collectionMatches(collection, query)) {
            collection.items.removeIf([&](const IconLibrary::Item &item) { return !itemMatches(item, query); });
            if (collection.items.isEmpty()) continue;
        }
        auto *section = new Section(this, collection.id);
        section->setObjectName(QStringLiteral("IconSection"));
        auto *sectionLayout = new QVBoxLayout(section);
        sectionLayout->setContentsMargins(0, 0, 0, 6);
        sectionLayout->setSpacing(2);
        const bool folded = query.isEmpty() && collapsed.contains(collection.id);  // a search opens them
        auto *header = new Header(collection.id, section);
        header->setObjectName(QStringLiteral("IconHeader"));
        header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        header->setArrowType(folded ? Qt::RightArrow : Qt::DownArrow);
        header->setText(collection.name);
        header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        header->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(header, &QToolButton::clicked, this, [this, id = collection.id, folded] { setCollapsed(id, !folded); });
        connect(header, &QToolButton::customContextMenuRequested, this, [this, header, id = collection.id](const QPoint &pos) {
            showCollectionMenu(id, header->mapToGlobal(pos));
        });
        sectionLayout->addWidget(header);
        if (!folded) {
            auto *grid = new QGridLayout;
            grid->setSpacing(kCellSpacingPx);
            grid->setContentsMargins(0, 0, 0, 0);
            for (int i = 0; i < collection.items.size(); ++i) {
                const IconLibrary::Item item = collection.items[i];
                auto *cell = new Cell(item.path, section);
                cell->setObjectName(QStringLiteral("IconCell"));
                cell->setFixedSize(cellPx, cellPx);
                cell->setIconSize(QSize(iconPx, iconPx));
                cell->setIcon(itemIcon(m_kind, item.path, iconPx));
                cell->setToolTip(item.name);
                cell->setAccessibleName(item.name);
                cell->setFocusPolicy(Qt::NoFocus);
                cell->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(cell, &QToolButton::clicked, this, [this, item, id = collection.id] { if (chosen) chosen(item, id); });
                connect(cell, &QToolButton::customContextMenuRequested, this, [this, cell, path = item.path, id = collection.id](const QPoint &pos) {
                    showItemMenu(path, id, cell->mapToGlobal(pos));
                });
                grid->addWidget(cell, i / columns, i % columns);
                section->cells.append(cell);
                m_cells.insert(item.path, cell);
                m_shown.append(item.path);
            }
            grid->setColumnStretch(columns, 1);
            sectionLayout->addLayout(grid);
            if (collection.items.isEmpty()) {
                auto *empty = new QLabel(tr("Empty: drag items here or add them from a file."), section);
                empty->setWordWrap(true);
                sectionLayout->addWidget(empty);
            }
        }
        m_sections->addWidget(section);
    }
    if (!query.isEmpty() && m_sections->isEmpty()) {
        auto *none = new QLabel(sticker ? tr("No stickers found.") : tr("No markers found."), m_content);
        none->setObjectName(QStringLiteral("IconNoResult"));
        none->setWordWrap(true);
        m_sections->addWidget(none);
    }
    setActiveMarkers(m_active);
}

void IconPage::setActiveMarkers(const QStringList &markers) {
    m_active = markers;
    if (m_kind != IconLibrary::Kind::Marker) return;
    QStringList customShas;
    for (const QString &marker : markers)
        if (Markers::isCustom(marker)) customShas.append(Markers::customImage(marker));
    for (const IconLibrary::Collection &collection : std::as_const(m_collections))
        for (const IconLibrary::Item &item : collection.items) {
            QAbstractButton *cell = m_cells.value(item.path);
            if (!cell) continue;
            bool on = markers.contains(item.path);
            if (!on && !customShas.isEmpty() && item.path.contains(QLatin1Char('/'))) {
                const QString marker = markerFor(item, collection.id);
                on = markers.contains(marker);
            }
            if (cell->property("active").toBool() == on) continue;
            cell->setProperty("active", on);
            cell->style()->unpolish(cell);
            cell->style()->polish(cell);
        }
}

QStringList IconPage::shownCollections() const {
    QStringList ids;
    for (int i = 0; i < m_sections->count(); ++i)
        if (auto *section = dynamic_cast<Section *>(m_sections->itemAt(i)->widget())) ids.append(section->id());
    return ids;
}

QAbstractButton *IconPage::cell(const QString &path) const { return m_cells.value(path); }

void IconPage::setCollapsed(const QString &id, bool collapsed) {
    QSettings settings;
    QStringList list = settings.value(settingsKey(m_kind)).toStringList();
    list.removeAll(id);
    if (collapsed) list.append(id);
    settings.setValue(settingsKey(m_kind), list);
    QMetaObject::invokeMethod(this, &IconPage::reload, Qt::QueuedConnection);  // il clic arriva dall'intestazione
}

QString IconPage::ensureOwnCollection() {
    for (const IconLibrary::Collection &c : IconLibrary::collections(m_kind))
        if (!c.builtIn) return c.id;
    return IconLibrary::addCollection(m_kind, m_kind == IconLibrary::Kind::Sticker ? tr("My stickers") : tr("My markers"));
}

// Controlli dei formati (IconLibrary::checkFile): i file rifiutati si spiegano, quelli piccoli si
// chiedono.
QStringList IconPage::checkedFiles(const QStringList &paths) {
    QStringList accepted;
    for (const QString &path : paths) {
        const IconLibrary::Check check = IconLibrary::checkFile(m_kind, path);
        const QString name = QFileInfo(path).fileName();
        if (!check.ok) {
            ModalDialog::inform(window(), tr("File not added"), tr("\"%1\": %2").arg(name, check.reason));
            continue;
        }
        if (check.small && ModalDialog::ask(window(), tr("Small image"), tr("\"%1\": %2 Add it anyway?").arg(name, check.smallReason), tr("Add"))
                               != ModalDialog::Choice::Primary)
            continue;
        accepted.append(path);
    }
    return accepted;
}

void IconPage::addFiles(const QString &collectionId) {
    QStringList paths;
    if (pickFiles) paths = pickFiles();
    else paths = QFileDialog::getOpenFileNames(window(), m_kind == IconLibrary::Kind::Sticker ? tr("Add stickers") : tr("Add markers"), {},
                                               IconLibrary::fileFilter());
    paths = checkedFiles(paths);
    if (paths.isEmpty()) return;
    // Tappa 3: every added file gets a name, its own prefilled; Cancel skips that file.
    QStringList named, names;
    for (const QString &path : std::as_const(paths)) {
        const QString current = IconLibrary::displayName(path);
        const QString title = tr("Name of \"%1\"").arg(QFileInfo(path).fileName());
        QString name;
        if (askName) name = askName(title, current);
        else {
            bool ok = false;
            name = ModalDialog::askText(window(), title, tr("Name"), tr("Add"), &ok, current);
            if (!ok) continue;
            if (name.trimmed().isEmpty()) name = current;  // confirmed empty: the file's own name
        }
        if (name.trimmed().isEmpty()) continue;
        named.append(path);
        names.append(name);
    }
    if (named.isEmpty()) return;
    const QString target = collectionId.isEmpty() ? ensureOwnCollection() : collectionId;
    QStringList added;
    for (int i = 0; i < named.size(); ++i)
        if (const QString copy = IconLibrary::addFile(m_kind, target, named[i], names[i]); !copy.isEmpty()) added.append(copy);
    reload();
    if (!added.isEmpty() && filesAdded) filesAdded(added, target);
}

void IconPage::importFolder() {
    const QString folder = pickFolder ? pickFolder() : QFileDialog::getExistingDirectory(window(), tr("Import a folder"));
    if (folder.isEmpty()) return;
    int imported = 0;
    const QString id = IconLibrary::importFolder(m_kind, folder, &imported);
    if (id.isEmpty()) {
        ModalDialog::inform(window(), tr("Nothing imported"), tr("The folder has no SVG or PNG images up to 2 MB."));
        return;
    }
    reload();
}

void IconPage::newCollection() {
    const QString title = m_kind == IconLibrary::Kind::Sticker ? tr("New collection") : tr("New group");
    QString name;
    if (askName) name = askName(title, {});
    else {
        bool ok = false;
        name = ModalDialog::askText(window(), title, tr("Name"), tr("Create"), &ok);
        if (!ok) return;
    }
    if (name.trimmed().isEmpty()) return;
    IconLibrary::addCollection(m_kind, name);
    reload();
}

void IconPage::renameCollection(const QString &id) {
    const IconLibrary::Collection collection = IconLibrary::collection(m_kind, id);
    QString name;
    if (askName) name = askName(tr("Rename"), collection.name);
    else {
        bool ok = false;
        name = ModalDialog::askText(window(), tr("Rename \"%1\"").arg(collection.name), tr("Name"), tr("Rename"), &ok);
        if (!ok) return;
    }
    if (IconLibrary::renameCollection(m_kind, id, name)) reload();
}

void IconPage::removeCollection(const QString &id) {
    const IconLibrary::Collection collection = IconLibrary::collection(m_kind, id);
    // Si chiede solo se si perdono file aggiunti (gli elementi di base tornano nella loro raccolta).
    // Solo i file aggiunti si perdono (bug 2026-10-20: il messaggio contava anche quelli di base).
    const int ownFiles = int(std::count_if(collection.items.cbegin(), collection.items.cend(), [this](const IconLibrary::Item &item) {
        return item.path.startsWith(IconLibrary::folder(m_kind));
    }));
    if (!collection.builtIn && ownFiles > 0
        && ModalDialog::ask(window(), tr("Delete collection"),
                            tr("Delete \"%1\" and its %n added files? Maps that already use them keep their copy.", nullptr, ownFiles)
                                .arg(collection.name),
                            tr("Delete")) != ModalDialog::Choice::Primary)
        return;
    if (IconLibrary::removeCollection(m_kind, id)) reload();
}

void IconPage::restoreBuiltIns() {
    IconLibrary::restoreBuiltIns(m_kind);
    reload();
}

// The search is in English only, the same in every interface language (user's decision
// 2026-11-05): built-in collections and markers by their English names, own ones by the name
// the user gave them.
bool IconPage::collectionMatches(const IconLibrary::Collection &collection, const QString &query) const {
    const QString name = collection.builtIn ? IconLibrary::englishName(m_kind, collection.id) : collection.name;
    return name.contains(query, Qt::CaseInsensitive);
}

bool IconPage::itemMatches(const IconLibrary::Item &item, const QString &query) const {
    return IconLibrary::englishName(m_kind, item).contains(query, Qt::CaseInsensitive);
}

void IconPage::dropItem(const QString &path, const QString &collectionId, int index) {
    const QString query = m_search ? m_search->text().simplified() : QString();
    for (const IconLibrary::Collection &c : IconLibrary::collections(m_kind)) {
        if (c.id != collectionId) continue;
        // While searching, index counts the shown cells only: before the shown item at index, or
        // after the last shown one.
        if (!query.isEmpty() && index >= 0 && !collectionMatches(c, query)) {
            QList<int> shown;
            for (int i = 0; i < c.items.size(); ++i)
                if (itemMatches(c.items[i], query)) shown.append(i);
            if (index < shown.size()) index = shown[index];
            else if (!shown.isEmpty()) index = shown.last() + 1;
        }
        // Nella stessa raccolta, piu' avanti: la posizione conta senza l'elemento tolto.
        for (int i = 0; i < c.items.size(); ++i)
            if (c.items[i].path == path && index > i) --index;
    }
    if (IconLibrary::moveItem(m_kind, path, collectionId, index)) reload();
}

void IconPage::dropCollection(const QString &id, const QString &beforeId) {
    if (id == beforeId) return;
    const QList<IconLibrary::Collection> list = IconLibrary::collections(m_kind);
    int from = -1, to = int(list.size()) - 1;
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].id == id) from = i;
        if (list[i].id == beforeId) to = i;
    }
    if (from < 0) return;
    if (from < to && !beforeId.isEmpty()) --to;
    if (IconLibrary::moveCollection(m_kind, id, to)) reload();
}

void IconPage::showItemMenu(const QString &path, const QString &collectionId, const QPoint &globalPos) {
    QMenu menu(this);
    QMenu *move = menu.addMenu(tr("Move to"));
    for (const IconLibrary::Collection &c : std::as_const(m_collections)) {
        if (c.hidden || c.id == collectionId) continue;
        move->addAction(c.name, this, [this, path, id = c.id] { dropItem(path, id, -1); });
    }
    move->setEnabled(!move->isEmpty());
    if (m_kind == IconLibrary::Kind::Sticker && path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive))
        menu.addAction(tr("Edit…"), this, [this, path, collectionId] { editSticker(path, collectionId); });
    // Solo i file aggiunti si tolgono: quelli di base restano nella loro raccolta d'origine.
    if (path.startsWith(IconLibrary::folder(m_kind))) {
        menu.addAction(tr("Rename…"), this, [this, path] { renameItem(path); });
        menu.addAction(tr("Remove from the collection"), this, [this, path, collectionId] {
            if (IconLibrary::removeItem(m_kind, collectionId, path)) reload();
        });
    }
    menu.exec(globalPos);
}

void IconPage::renameItem(const QString &path) {
    const QString current = IconLibrary::displayName(path);
    QString name;
    if (askName) name = askName(tr("Rename"), current);
    else {
        bool ok = false;
        name = ModalDialog::askText(window(), tr("Rename \"%1\"").arg(current), tr("Name"), tr("Rename"), &ok, current);
        if (!ok) return;
    }
    if (name.trimmed().isEmpty() || name.simplified() == current) return;
    if (!IconLibrary::renameItem(m_kind, path, name).isEmpty()) reload();
}

void IconPage::editSticker(const QString &path, const QString &collectionId) {
    StickerEditorDialog dialog(window(), path, IconLibrary::collections(m_kind), collectionId);
    if (!dialog.isValid()) {
        ModalDialog::inform(window(), tr("Edit sticker"), tr("This SVG cannot be edited: the app cannot draw it."));
        return;
    }
    if (!(runEditor ? runEditor(dialog) : dialog.exec() == QDialog::Accepted)) return;
    QString target = dialog.collectionId();
    if (target.isEmpty()) target = IconLibrary::addCollection(m_kind, dialog.newCollectionName());
    const QString saved = target.isEmpty() ? QString() : IconLibrary::addData(m_kind, target, dialog.stickerName() + QStringLiteral(".svg"), dialog.svg());
    reload();
    if (saved.isEmpty()) {
        ModalDialog::inform(window(), tr("Edit sticker"), tr("The sticker could not be saved."));
        return;
    }
    // Only added stickers can be deleted: the built-in ones stay in their collection.
    if (!path.startsWith(IconLibrary::folder(m_kind))) return;
    const QString name = IconLibrary::displayName(path);
    const bool remove = askDeleteOriginal ? askDeleteOriginal(name)
                                          : ModalDialog::ask(window(), tr("Delete the original?"),
                                                             tr("The new sticker is saved. Delete the original \"%1\" from its collection?").arg(name),
                                                             tr("Delete"), QString(), tr("Keep")) == ModalDialog::Choice::Primary;
    if (remove && IconLibrary::removeItem(m_kind, collectionId, path)) reload();
}

void IconPage::showCollectionMenu(const QString &id, const QPoint &globalPos) {
    const IconLibrary::Collection collection = IconLibrary::collection(m_kind, id);
    QMenu menu(this);
    menu.addAction(tr("Add from file…"), this, [this, id] { addFiles(id); });
    menu.addAction(tr("Rename…"), this, [this, id] { renameCollection(id); });
    menu.addAction(collection.builtIn ? tr("Hide") : tr("Delete…"), this, [this, id] { removeCollection(id); });
    menu.exec(globalPos);
}
