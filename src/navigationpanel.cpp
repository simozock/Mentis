#include "navigationpanel.h"

#include "uitheme.h"

#include <QAction>
#include <QApplication>
#include <QActionGroup>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QSet>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {
constexpr int kSheetRole = Qt::UserRole;
constexpr int kNodeRole = Qt::UserRole + 1;
constexpr int kRangesRole = Qt::UserRole + 2;  // QList<QPoint>: start and length to highlight
constexpr int kRowHeightPx = 28;               // like the topic tree in the modals (AGENTS.md "Modali")
constexpr int kSearchDelayMs = 120;            // results follow typing without rebuilding per key
constexpr int kSnippetChars = 40;              // text shown around a match in a note, label or link
// Found text: the text selection colours of the map and outliner (AGENTS.md "Colori").
const QColor kHighlight("#b3d4fc");
const QColor kHighlightText("#202630");
const QString kBullet = QStringLiteral("•  ");

// Text with highlighted ranges, elided on the right; rows like the other dark lists.
class HighlightDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        return {QStyledItemDelegate::sizeHint(option, index).width(), kRowHeightPx};
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF row = QRectF(option.rect).adjusted(0, 1, 0, -1);
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(UiTheme::color(option.state & QStyle::State_Selected ? UiTheme::Token::SurfaceRaised : UiTheme::Token::Divider));
            painter->drawRoundedRect(row, 4, 4);
        }
        QFont font = option.font;
        if (const QVariant bold = index.data(Qt::FontRole); bold.isValid()) font = bold.value<QFont>();
        painter->setFont(font);
        const QFontMetricsF metrics(font);
        const QString text = index.data(Qt::DisplayRole).toString();
        const qreal available = option.rect.width() - 16;
        const QString shown = metrics.elidedText(text, Qt::ElideRight, available);
        const qreal baseline = option.rect.center().y() + (metrics.ascent() - metrics.descent()) / 2;
        const qreal left = option.rect.left() + 8;
        for (const QVariant &value : index.data(kRangesRole).toList()) {
            const QPoint range = value.toPoint();
            if (range.x() >= shown.size()) continue;
            const qreal x0 = metrics.horizontalAdvance(shown.left(range.x()));
            const qreal x1 = metrics.horizontalAdvance(shown.left(std::min<int>(range.x() + range.y(), shown.size())));
            painter->setPen(Qt::NoPen);
            painter->setBrush(kHighlight);
            painter->drawRoundedRect(QRectF(left + x0 - 1, baseline - metrics.ascent() - 1, x1 - x0 + 2, metrics.height() + 2), 2, 2);
        }
        // Text drawn in pieces so the highlighted ones get the dark text.
        QList<std::pair<int, bool>> cuts{{0, false}};
        for (const QVariant &value : index.data(kRangesRole).toList()) {
            const QPoint range = value.toPoint();
            cuts.append({range.x(), true});
            cuts.append({range.x() + range.y(), false});
        }
        std::sort(cuts.begin(), cuts.end());
        for (int i = 0; i < cuts.size(); ++i) {
            const int start = std::min<int>(cuts[i].first, shown.size());
            const int end = std::min<int>(i + 1 < cuts.size() ? cuts[i + 1].first : shown.size(), shown.size());
            if (end <= start) continue;
            painter->setPen(cuts[i].second ? kHighlightText : UiTheme::color(UiTheme::Token::Text));
            painter->drawText(QPointF(left + metrics.horizontalAdvance(shown.left(start)), baseline), shown.mid(start, end - start));
        }
        painter->restore();
    }
};

// Hugeicons are dark in the files: light on the dark panel (AGENTS.md "Icone").
QIcon lightIcon(const QString &path) {
    QPixmap pixmap = QIcon(path).pixmap(QSize(16, 16), qApp->devicePixelRatio());
    QPainter tint(&pixmap);
    tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tint.fillRect(pixmap.rect(), UiTheme::color(UiTheme::Token::Text));
    tint.end();
    return QIcon(pixmap);
}

// The panel's menus: the options (with check marks) and the tag menu (without, so less room on the left).
void styleMenu(QMenu *menu, bool checkable = true) {
    menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground);
    menu->setStyleSheet(UiTheme::qss(QStringLiteral(
        "QMenu { background:@surface; border:1px solid @divider; border-radius:8px; padding:4px; }"
        "QMenu::item { color:@text; padding:8px 24px 8px %1px; border-radius:4px; }"
        "QMenu::item:selected { background:@surfaceRaised; }"
        "QMenu::indicator { left:10px; width:12px; height:12px; }"
        "QMenu::separator { height:1px; background:@divider; margin:4px 8px; }").arg(checkable ? 32 : 12)));
}

QTreeWidget *makeList(QWidget *parent) {
    auto *list = new QTreeWidget(parent);
    list->setHeaderHidden(true);
    list->setColumnCount(1);
    list->setRootIsDecorated(false);
    list->setIndentation(16);
    list->setMouseTracking(true);
    list->setFrameShape(QFrame::NoFrame);
    list->setFocusPolicy(Qt::StrongFocus);
    list->setUniformRowHeights(true);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setItemDelegate(new HighlightDelegate(list));
    list->header()->setStretchLastSection(true);
    return list;
}

QList<QVariant> rangesFrom(const QList<std::pair<int, int>> &found, int offset) {
    QList<QVariant> ranges;
    for (const auto &[start, length] : found) ranges.append(QPoint(start + offset, length));
    return ranges;
}

// Words around the first occurrence, so a match in a long note is visible in one row.
std::pair<QString, QList<QVariant>> snippet(const QString &text, const QString &query, const NodeSearch::Options &options) {
    const QString flat = QString(text).replace(QLatin1Char('\n'), QLatin1Char(' '));
    const auto found = NodeSearch::occurrences(flat, query, options);
    if (found.isEmpty()) return {flat.left(kSnippetChars), {}};
    const int start = std::max(0, found.front().first - kSnippetChars / 2);
    const QString prefix = start > 0 ? QStringLiteral("…") : QString();
    const QString piece = prefix + flat.mid(start, kSnippetChars + found.front().second);
    QList<QVariant> ranges;
    for (const auto &[at, length] : found)
        if (at >= start && at + length <= start + kSnippetChars + found.front().second) ranges.append(QPoint(at - start + int(prefix.size()), length));
    return {piece, ranges};
}
}  // namespace

NavigationPanel::NavigationPanel(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("NavigationPanel"));
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(UiTheme::qss(QStringLiteral(
        "QWidget#NavigationPanel { background:@surfaceSunken; }"
        "QToolButton#NavigationTab { color:@textMuted; background:transparent; border:0; border-radius:4px; padding:2px 6px; font-size:12px; font-weight:600; }"
        "QToolButton#NavigationTab:hover { color:@text; }"
        "QToolButton#NavigationTab:checked { color:@text; background:@surfaceRaised; }"
        "QToolButton#NavigationClose, QToolButton#NavigationOptions { background:transparent; border:0; border-radius:4px; }"
        "QToolButton#NavigationClose:hover, QToolButton#NavigationOptions:hover { background:@divider; }"
        "QToolButton#NavigationOptions::menu-indicator { image:none; }"
        "QLineEdit { color:@text; background:@surface; border:1px solid @divider; border-radius:4px; min-height:24px; max-height:24px;"
        " padding:0 6px; font-size:12px; selection-background-color:@surfaceRaised; }"
        "QLineEdit:focus { border-color:@accent; }"
        "QPushButton#NavigationReplace { color:@text; background:@surface; border:1px solid @divider; border-radius:4px;"
        " min-height:24px; max-height:24px; padding:0 10px; font-size:12px; }"
        "QPushButton#NavigationReplace:hover { background:@surfaceRaised; }"
        "QPushButton#NavigationReplace:disabled { color:@textMuted; background:transparent; }"
        "QTreeWidget, QListWidget { background:transparent; color:@text; border:0; outline:0; font-size:13px; }"
        "QListWidget::item { height:28px; padding:0 8px; border-radius:4px; }"
        "QListWidget::item:hover { background:@divider; } QListWidget::item:selected { background:@surfaceRaised; color:@text; }"
        "QScrollBar:vertical { background:transparent; width:8px; margin:0; }"
        "QScrollBar::handle:vertical { background:@surfaceRaised; border-radius:3px; min-height:24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }")));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto *header = new QHBoxLayout;
    header->setSpacing(4);
    const QStringList names{tr("Outline"), tr("Notes"), tr("Tags")};
    for (int i = 0; i < names.size(); ++i) {
        auto *tab = new QToolButton(this);
        tab->setObjectName(QStringLiteral("NavigationTab"));
        tab->setText(names[i]);
        tab->setCheckable(true);
        tab->setAutoExclusive(true);
        tab->setCursor(Qt::PointingHandCursor);
        connect(tab, &QToolButton::clicked, this, [this, i] { showTab(static_cast<Tab>(i)); });
        m_tabs.append(tab);
        header->addWidget(tab);
    }
    header->addStretch();
    auto *close = new QToolButton(this);
    close->setObjectName(QStringLiteral("NavigationClose"));
    close->setIcon(lightIcon(QStringLiteral(":/assets/hugeicons/window-close-light.svg")));
    close->setIconSize(QSize(16, 16));
    close->setFixedSize(24, 24);
    close->setToolTip(tr("Close"));
    close->setAccessibleName(tr("Close the navigation panel"));
    connect(close, &QToolButton::clicked, this, &NavigationPanel::closeRequested);
    header->addWidget(close);
    layout->addLayout(header);

    m_searchDelay = new QTimer(this);
    m_searchDelay->setSingleShot(true);
    m_searchDelay->setInterval(kSearchDelayMs);
    connect(m_searchDelay, &QTimer::timeout, this, &NavigationPanel::refresh);

    // Options in the gear menu (picture 06, Find and replace checked live 2026-10-22).
    m_optionsButton = new QToolButton(this);
    m_optionsButton->setObjectName(QStringLiteral("NavigationOptions"));
    m_optionsButton->setIcon(lightIcon(QStringLiteral(":/assets/hugeicons/settings-general.svg")));
    m_optionsButton->setIconSize(QSize(16, 16));
    m_optionsButton->setFixedSize(24, 24);
    m_optionsButton->setToolTip(tr("Search options"));
    m_optionsButton->setPopupMode(QToolButton::InstantPopup);
    // Not a child of the window: its actions ("Note", "Label"...) must not be taken for the window's
    // commands by text; deleted with the panel.
    auto *menu = new QMenu;
    connect(this, &QObject::destroyed, menu, &QObject::deleteLater);
    styleMenu(menu);
    auto *mode = new QActionGroup(menu);
    QAction *findOnly = menu->addAction(tr("Find"));
    m_findAndReplace = menu->addAction(tr("Find and replace"));
    for (QAction *action : {findOnly, m_findAndReplace}) { action->setCheckable(true); mode->addAction(action); }
    findOnly->setChecked(true);
    connect(m_findAndReplace, &QAction::toggled, this, [this](bool on) { setReplaceMode(on); });
    menu->addSeparator();
    auto *scope = new QActionGroup(menu);
    QAction *currentMap = menu->addAction(tr("Current map"));
    QAction *currentFile = menu->addAction(tr("Current file"));
    for (QAction *action : {currentMap, currentFile}) { action->setCheckable(true); scope->addAction(action); }
    currentMap->setChecked(true);
    connect(currentFile, &QAction::toggled, this, [this](bool on) { m_allSheets = on; refresh(); });
    menu->addSeparator();
    const auto option = [this, menu](const QString &text, bool checked, const std::function<void(bool)> &set) {
        QAction *action = menu->addAction(text);
        action->setCheckable(true);
        action->setChecked(checked);
        connect(action, &QAction::toggled, this, [this, set](bool on) { set(on); refresh(); });
    };
    option(tr("Whole words"), false, [this](bool on) { m_options.wholeWords = on; });
    option(tr("Match case"), false, [this](bool on) { m_options.matchCase = on; });
    menu->addSeparator();
    const QList<std::pair<QString, int>> fields{{tr("Topic"), NodeSearch::Title}, {tr("Note"), NodeSearch::Note},
                                                {tr("Tag"), NodeSearch::Label}, {tr("Link"), NodeSearch::Link}};
    for (const auto &[text, field] : fields)
        option(text, true, [this, field](bool on) { m_options.fields = on ? (m_options.fields | field) : (m_options.fields & ~field); });
    m_optionsButton->setMenu(menu);

    m_pages = new QStackedWidget(this);
    // Outline.
    auto *outlinePage = new QWidget(m_pages);
    auto *outlineLayout = new QVBoxLayout(outlinePage);
    outlineLayout->setContentsMargins(0, 0, 0, 0);
    outlineLayout->setSpacing(8);
    m_find = new QLineEdit(outlinePage);
    m_find->setObjectName(QStringLiteral("NavigationFind"));
    m_find->setPlaceholderText(tr("Find"));
    outlineLayout->addWidget(searchRow(m_find, m_optionsButton));
    // Find and replace: field with a pen, then Replace and Replace all on the right.
    m_replaceRow = new QWidget(outlinePage);
    auto *replaceLayout = new QVBoxLayout(m_replaceRow);
    replaceLayout->setContentsMargins(0, 0, 32, 0);  // as wide as the Find field (gear 24 + 8)
    replaceLayout->setSpacing(8);
    m_replace = new QLineEdit(m_replaceRow);
    m_replace->setObjectName(QStringLiteral("NavigationReplaceField"));
    m_replace->setPlaceholderText(tr("Replace"));
    m_replace->setClearButtonEnabled(true);
    m_replace->addAction(lightIcon(QStringLiteral(":/assets/hugeicons/pen.svg")), QLineEdit::LeadingPosition);
    replaceLayout->addWidget(m_replace);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch();
    m_replaceOne = new QPushButton(tr("Replace"), m_replaceRow);
    m_replaceAll = new QPushButton(tr("Replace all"), m_replaceRow);
    for (QPushButton *button : {m_replaceOne, m_replaceAll}) {
        button->setObjectName(QStringLiteral("NavigationReplace"));
        button->setCursor(Qt::PointingHandCursor);
        buttons->addWidget(button);
    }
    connect(m_replaceOne, &QPushButton::clicked, this, [this] { replace(false); });
    connect(m_replaceAll, &QPushButton::clicked, this, [this] { replace(true); });
    replaceLayout->addLayout(buttons);
    m_replaceRow->hide();
    outlineLayout->addWidget(m_replaceRow);
    m_outline = makeList(outlinePage);
    m_outline->setObjectName(QStringLiteral("NavigationOutline"));
    outlineLayout->addWidget(m_outline, 1);
    m_pages->addWidget(outlinePage);
    // Notes.
    auto *notesPage = new QWidget(m_pages);
    auto *notesLayout = new QVBoxLayout(notesPage);
    notesLayout->setContentsMargins(0, 0, 0, 0);
    notesLayout->setSpacing(8);
    m_notesFind = new QLineEdit(notesPage);
    m_notesFind->setObjectName(QStringLiteral("NavigationNotesFind"));
    m_notesFind->setPlaceholderText(tr("Search notes"));
    notesLayout->addWidget(searchRow(m_notesFind, nullptr));
    m_notes = makeList(notesPage);
    m_notes->setObjectName(QStringLiteral("NavigationNotes"));
    notesLayout->addWidget(m_notes, 1);
    m_pages->addWidget(notesPage);
    // Tags.
    m_tags = new QListWidget(m_pages);
    m_tags->setObjectName(QStringLiteral("NavigationTags"));
    m_tags->setFrameShape(QFrame::NoFrame);
    m_tags->setMouseTracking(true);
    connect(m_tags, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (activateLabel) activateLabel(item->data(Qt::UserRole).toString());
    });
    // Rename and delete on every sheet (FEAT-060): right-click menu, F2 and Delete on the selected tag.
    // Renaming edits the name in the row: Enter or a click elsewhere applies it, Esc cancels.
    m_tags->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tags->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tags->installEventFilter(this);
    connect(m_tags, &QListWidget::customContextMenuRequested, this, &NavigationPanel::showTagMenu);
    connect(m_tags->itemDelegate(), &QAbstractItemDelegate::closeEditor, this,
            [this](QWidget *editor, QAbstractItemDelegate::EndEditHint hint) {
        const QString from = std::exchange(m_renamingTag, QString());
        if (from.isEmpty()) return;
        const auto *field = qobject_cast<QLineEdit *>(editor);
        const QString to = field && hint != QAbstractItemDelegate::RevertModelCache ? field->text().simplified() : QString();
        // After the view has closed the editor, since the list is rebuilt.
        QTimer::singleShot(0, this, [this, from, to] {
            if (!to.isEmpty() && to != from && renameLabel) renameLabel(from, to);
            rebuildTags();
        });
    });
    m_pages->addWidget(m_tags);
    layout->addWidget(m_pages, 1);

    for (QLineEdit *field : {m_find, m_notesFind}) {
        connect(field, &QLineEdit::textChanged, m_searchDelay, qOverload<>(&QTimer::start));
    }
    connect(m_outline, &QTreeWidget::currentItemChanged, this, &NavigationPanel::updateReplaceButtons);
    for (QTreeWidget *list : {m_outline, m_notes}) {
        connect(list, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) { activate(item); });
        connect(list, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) { activate(item); });
    }
    connect(m_find, &QLineEdit::returnPressed, this, [this] {
        m_searchDelay->stop();
        rebuildOutline();
        // Enter: the first match (the whole tree has the central topic first).
        if (QTreeWidgetItem *first = m_outline->topLevelItem(0)) { m_outline->setCurrentItem(first); activate(first); }
    });
    showTab(Tab::Outline);
}

QWidget *NavigationPanel::searchRow(QLineEdit *field, QToolButton *options) {
    auto *row = new QWidget(field->parentWidget());
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);
    field->setParent(row);
    field->setClearButtonEnabled(true);
    field->addAction(lightIcon(QStringLiteral(":/assets/hugeicons/search.svg")), QLineEdit::LeadingPosition);
    rowLayout->addWidget(field, 1);
    if (options) { options->setParent(row); rowLayout->addWidget(options); }
    return row;
}

void NavigationPanel::showTab(Tab tab) {
    const int index = static_cast<int>(tab);
    m_tabs[index]->setChecked(true);
    m_pages->setCurrentIndex(index);
    refresh();
    if (tab == Tab::Outline) { m_find->setFocus(); m_find->selectAll(); }
    else if (tab == Tab::Notes) m_notesFind->setFocus();
    else m_tags->setFocus();
}

void NavigationPanel::refresh() {
    switch (static_cast<Tab>(m_pages->currentIndex())) {
    case Tab::Outline: rebuildOutline(); break;
    case Tab::Notes: rebuildNotes(); break;
    case Tab::Tags: rebuildTags(); break;
    }
}

void NavigationPanel::rebuildOutline() {
    m_outline->clear();
    if (!sheets) return;
    const QString query = m_find->text().trimmed();
    if (query.isEmpty()) {
        // The current map as a tree: "[Sheet]Central topic" in bold, then the topics.
        for (const SheetData &sheet : sheets(false)) {
            if (!sheet.current) continue;
            QList<QTreeWidgetItem *> parents;
            for (const NodeSearch::Entry &entry : NodeSearch::entries(sheet.id, sheet.content)) {
                auto *item = entry.depth == 0 ? new QTreeWidgetItem(m_outline) : new QTreeWidgetItem(parents.value(entry.depth - 1));
                const QString shown = NodeSearch::shownTitle(entry);
                const QString title = shown.isEmpty() ? QStringLiteral(" ") : QString(shown).replace(QLatin1Char('\n'), QLatin1Char(' '));
                item->setText(0, entry.depth == 0 ? QStringLiteral("[%1]%2").arg(sheet.name, title) : kBullet + title);
                if (entry.depth <= 1) {
                    QFont font = m_outline->font();
                    font.setBold(true);
                    if (entry.depth == 0) font.setPixelSize(UiTheme::fontPx(16));
                    item->setData(0, Qt::FontRole, font);
                }
                item->setData(0, kSheetRole, entry.sheetId);
                item->setData(0, kNodeRole, entry.nodeId);
                parents = parents.mid(0, entry.depth);
                parents.append(item);
            }
        }
        m_outline->expandAll();
        updateReplaceButtons();
        return;
    }
    // Matches only, in tree order; with "Current file" grouped by sheet.
    const QList<SheetData> data = sheets(m_allSheets);
    for (const SheetData &sheet : data) {
        QTreeWidgetItem *group = nullptr;
        for (const NodeSearch::Entry &entry : NodeSearch::entries(sheet.id, sheet.content)) {
            const int fields = NodeSearch::matchingFields(entry, query, m_options);
            if (!fields) continue;
            if (m_allSheets && !group) {
                group = new QTreeWidgetItem(m_outline);
                group->setText(0, sheet.name);
                group->setFlags(Qt::ItemIsEnabled);
                QFont font = m_outline->font();
                font.setBold(true);
                group->setData(0, Qt::FontRole, font);
            }
            auto *item = group ? new QTreeWidgetItem(group) : new QTreeWidgetItem(m_outline);
            QString text = NodeSearch::shownTitle(entry).replace(QLatin1Char('\n'), QLatin1Char(' '));
            QList<QVariant> ranges = rangesFrom(NodeSearch::occurrences(text, query, m_options), 0);
            if (!(fields & NodeSearch::Title)) {
                // Found elsewhere: a piece of the note, label or link after the title.
                const QString source = (fields & NodeSearch::Note) ? entry.notes
                                       : (fields & NodeSearch::Label) ? entry.labels.join(QStringLiteral(", ")) : entry.webLink;
                const auto [piece, pieceRanges] = snippet(source, query, m_options);
                const int offset = int(text.size()) + 3;
                text += QStringLiteral(" · ") + piece;
                for (const QVariant &range : pieceRanges) ranges.append(range.toPoint() + QPoint(offset, 0));
            }
            item->setText(0, text);
            item->setData(0, kRangesRole, ranges);
            item->setData(0, kSheetRole, entry.sheetId);
            item->setData(0, kNodeRole, entry.nodeId);
        }
    }
    m_outline->expandAll();
    updateReplaceButtons();
}

void NavigationPanel::setReplaceMode(bool on) {
    if (m_findAndReplace->isChecked() != on) m_findAndReplace->setChecked(on);  // comes back here
    m_replaceRow->setVisible(on);
    updateReplaceButtons();
}

// Replace needs a selected result, Replace all a query with results.
void NavigationPanel::updateReplaceButtons() {
    const bool query = !m_find->text().trimmed().isEmpty();
    QTreeWidgetItem *current = m_outline->currentItem();
    m_replaceOne->setEnabled(query && current && !current->data(0, kNodeRole).toString().isEmpty());
    bool results = false;
    for (QTreeWidgetItemIterator it(m_outline); *it && !results; ++it) results = !(*it)->data(0, kNodeRole).toString().isEmpty();
    m_replaceAll->setEnabled(query && results);
}

// Only topic titles change (a match in a note, label or link is left as it is).
void NavigationPanel::replace(bool all) {
    const QString query = m_find->text().trimmed();
    if (query.isEmpty() || !sheets || !replaceTitles) return;
    QSet<QString> wanted;  // sheet + node of the selected result
    if (!all) {
        QTreeWidgetItem *current = m_outline->currentItem();
        if (!current) return;
        wanted.insert(current->data(0, kSheetRole).toString() + QLatin1Char('/') + current->data(0, kNodeRole).toString());
    }
    TitleChanges changes;
    for (const SheetData &sheet : sheets(m_allSheets)) {
        for (const NodeSearch::Entry &entry : NodeSearch::entries(sheet.id, sheet.content)) {
            if (!all && !wanted.contains(entry.sheetId + QLatin1Char('/') + entry.nodeId)) continue;
            if (!(m_options.fields & NodeSearch::Title)) continue;
            const QString title = NodeSearch::replaced(entry.title, query, m_replace->text(), m_options);
            if (title != entry.title) changes[entry.sheetId].insert(entry.nodeId, title);
        }
    }
    if (changes.isEmpty()) return;
    replaceTitles(changes);
    m_outline->setCurrentItem(nullptr);
    rebuildOutline();
}

void NavigationPanel::rebuildNotes() {
    m_notes->clear();
    if (!sheets) return;
    const QString query = m_notesFind->text().trimmed();
    NodeSearch::Options noteOptions = m_options;
    noteOptions.fields = NodeSearch::Note;
    for (const SheetData &sheet : sheets(false)) {
        if (!sheet.current) continue;
        for (const NodeSearch::Entry &entry : NodeSearch::entries(sheet.id, sheet.content)) {
            if (entry.notes.trimmed().isEmpty()) continue;
            if (!query.isEmpty() && !NodeSearch::matchingFields(entry, query, noteOptions)) continue;
            // Title, then the note (or the piece with the match) on the same row.
            const QString title = NodeSearch::shownTitle(entry).replace(QLatin1Char('\n'), QLatin1Char(' '));
            const auto [piece, ranges] = snippet(entry.notes, query, noteOptions);
            auto *item = new QTreeWidgetItem(m_notes);
            const int offset = int(title.size()) + 3;
            item->setText(0, title + QStringLiteral(" · ") + piece);
            QList<QVariant> shifted;
            for (const QVariant &range : ranges) shifted.append(range.toPoint() + QPoint(offset, 0));
            item->setData(0, kRangesRole, shifted);
            item->setData(0, kSheetRole, entry.sheetId);
            item->setData(0, kNodeRole, entry.nodeId);
        }
    }
}

void NavigationPanel::rebuildTags() {
    if (!m_renamingTag.isEmpty()) return;  // a map change while renaming: rebuilt when the editor closes
    m_tags->clear();
    if (!sheets) return;
    // Labels of the current map with how many topics have them, alphabetical (FEAT-060).
    // "Esame" and "esame" are one label (Labels::same): one row, with the first spelling found.
    QMap<QString, std::pair<QString, int>> counts;
    for (const SheetData &sheet : sheets(false)) {
        if (!sheet.current) continue;
        for (const NodeSearch::Entry &entry : NodeSearch::entries(sheet.id, sheet.content))
            for (const QString &label : entry.labels) {
                auto &[shown, count] = counts[label.trimmed().toLower()];
                if (shown.isEmpty()) shown = label;
                ++count;
            }
    }
    for (const auto &[shown, count] : std::as_const(counts)) {
        auto *item = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(shown).arg(count), m_tags);
        item->setData(Qt::UserRole, shown);
    }
}

void NavigationPanel::showTagMenu(const QPoint &pos) {
    QListWidgetItem *item = m_tags->itemAt(pos);
    if (!item || !renameLabel) return;
    m_tags->setCurrentItem(item);
    const QString label = item->data(Qt::UserRole).toString();
    QMenu menu(this);
    styleMenu(&menu, false);
    QAction *rename = menu.addAction(tr("Rename"));
    QAction *remove = menu.addAction(tr("Delete"));
    // By label: the list may be rebuilt while the menu is open (the map changed).
    QAction *chosen = menu.exec(m_tags->viewport()->mapToGlobal(pos));
    if (chosen == rename) startRenamingTag(label);
    else if (chosen == remove) deleteTag(label);
}

void NavigationPanel::startRenamingTag(const QString &label) {
    if (!renameLabel || !m_renamingTag.isEmpty()) return;
    for (int row = 0; row < m_tags->count(); ++row) {
        QListWidgetItem *item = m_tags->item(row);
        if (item->data(Qt::UserRole).toString() != label) continue;
        m_renamingTag = label;
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setText(label);  // without the count
        m_tags->setCurrentItem(item);
        m_tags->editItem(item);
        return;
    }
}

void NavigationPanel::deleteTag(const QString &label) {
    if (!renameLabel || label.isEmpty()) return;
    renameLabel(label, QString());
    rebuildTags();
}

bool NavigationPanel::eventFilter(QObject *watched, QEvent *event) {
    // F2 and Delete on the selected tag. ShortcutOverride is accepted so the window's Delete (remove the
    // selected topic) does not take the key while the list has the focus.
    if (watched == m_tags && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        const auto *key = static_cast<QKeyEvent *>(event);
        QListWidgetItem *current = m_tags->currentItem();
        const bool ours = (key->key() == Qt::Key_F2 || key->key() == Qt::Key_Delete) && key->modifiers() == Qt::NoModifier
                          && current && m_renamingTag.isEmpty() && renameLabel;
        if (ours && event->type() == QEvent::ShortcutOverride) { event->accept(); return true; }
        if (ours) {
            const QString label = current->data(Qt::UserRole).toString();
            if (key->key() == Qt::Key_F2) startRenamingTag(label);
            else deleteTag(label);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void NavigationPanel::activate(QTreeWidgetItem *item) {
    if (!item || !activateTopic) return;
    const QString nodeId = item->data(0, kNodeRole).toString();
    if (!nodeId.isEmpty()) activateTopic(item->data(0, kSheetRole).toString(), nodeId);
}

void NavigationPanel::keyPressEvent(QKeyEvent *event) {
    // Esc closes the panel and gives the focus back to the map (FEAT-061).
    if (event->key() == Qt::Key_Escape) { emit closeRequested(); return; }
    QWidget::keyPressEvent(event);
}
