#include "linkdialogs.h"
#include "help.h"
#include "uitheme.h"

#include "nodelinks.h"
#include "segmentedcontrol.h"

#include <QCheckBox>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {
enum Role { IdRole = Qt::UserRole, SheetRole, HasTopicLinkRole, SearchTextRole };

// The title on one line for the tree (line breaks become spaces).
QString oneLine(const QString &title) {
    QString text = title;
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text.trimmed().isEmpty() ? LinkDialog::tr("(untitled)") : text;
}

QWidget *page(QStackedWidget *pages, int spacing, QVBoxLayout **layout) {
    auto *widget = new QWidget(pages);
    *layout = new QVBoxLayout(widget);
    (*layout)->setContentsMargins(0, 0, 0, 0);
    (*layout)->setSpacing(spacing);
    pages->addWidget(widget);
    return widget;
}
}  // namespace

LinkDialog::LinkDialog(QWidget *parent, Tab tab, const QString &currentAddress, std::function<QList<SheetContent>()> sheets,
                       const QString &sourceId, const QString &currentTargetId)
    : ModalDialog(parent, tr("Insert a link"), tr("Insert"), 480), m_sheets(std::move(sheets)), m_sourceId(sourceId),
      m_currentTargetId(currentTargetId) {
    Help::setPage(this, QStringLiteral("topic-contents/links.md"));  // FEAT-092: F1 and "?" open it
    m_tabs = new SegmentedControl({tr("Web page"), tr("Topic")}, this);
    contentLayout()->addWidget(m_tabs);
    // A stack is as tall as its tallest page (Topic): the buttons stay put when the tab changes.
    m_pages = new QStackedWidget(this);
    contentLayout()->addWidget(m_pages);

    QVBoxLayout *layout = nullptr;
    QWidget *web = page(m_pages, contentLayout()->spacing(), &layout);
    m_address = new QLineEdit(currentAddress, web);
    m_address->setPlaceholderText(QStringLiteral("https://"));
    layout->addWidget(m_address);
    m_error = new QLabel(tr("Only http, https and mailto addresses are allowed."), web);
    m_error->setObjectName(QStringLiteral("ModalError"));
    layout->addWidget(m_error);
    layout->addStretch();

    QWidget *topic = page(m_pages, contentLayout()->spacing(), &layout);
    m_search = new QLineEdit(topic);
    m_search->setPlaceholderText(tr("Search"));
    m_search->addAction(QIcon(QStringLiteral(":/assets/hugeicons/search.svg")), QLineEdit::LeadingPosition);
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);
    m_tree = new QTreeWidget(topic);
    m_tree->setHeaderHidden(true);
    m_tree->setFixedHeight(320);
    m_tree->setUniformRowHeights(true);  // row heights without asking every item (10,000 topics)
    // FEAT-035: a dark tree, as the modal.
    m_tree->setStyleSheet(UiTheme::qss(QStringLiteral(
        "QTreeWidget { background:@surfaceSunken; color:@text; border:0; border-radius:4px; font-size:14px;"
        " outline:0; padding:4px; }"
        "QTreeWidget::item { min-height:28px; border-radius:4px; }"
        "QTreeWidget::item:hover { background:@divider; }"
        "QTreeWidget::item:selected { background:@surfaceRaised; color:@text; }"
        "QTreeWidget::item:disabled { color:@textMuted; }")));
    layout->addWidget(m_tree);
    m_back = new QCheckBox(tr("Also add the link to the chosen topic"), topic);
    layout->addWidget(m_back);

    connect(m_address, &QLineEdit::textChanged, this, &LinkDialog::updateButtons);
    connect(m_search, &QLineEdit::textChanged, this, &LinkDialog::filter);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &LinkDialog::updateButtons);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (item->flags() & Qt::ItemIsSelectable) accept();
    });
    connect(m_tabs, &SegmentedControl::segmentActivated, this, [this](int index) { setTab(Tab(index)); });
    m_address->selectAll();
    setTab(tab);
}

LinkDialog::Tab LinkDialog::tab() const { return Tab(m_pages->currentIndex()); }

void LinkDialog::setTab(Tab tab) {
    if (tab == Tab::Topic) buildTree();
    m_tabs->setCurrentIndex(int(tab));
    m_pages->setCurrentIndex(int(tab));
    updateButtons();
    // The tab's field takes the focus, unless the arrow keys are moving along the tabs.
    if (!m_tabs->hasFocus()) (tab == Tab::Web ? m_address : m_search)->setFocus();
}

void LinkDialog::buildTree() {
    if (!m_sheets) return;
    const QList<SheetContent> sheets = std::exchange(m_sheets, nullptr)();
    // Each sheet's tree from the flat form: children grouped by parent, sorted by row.
    QFont sheetFont = m_tree->font();
    sheetFont.setWeight(QFont::DemiBold);
    QTreeWidgetItem *current = nullptr;
    for (const SheetContent &sheet : sheets) {
        auto *sheetItem = new QTreeWidgetItem(m_tree, {sheet.name});
        sheetItem->setFont(0, sheetFont);
        sheetItem->setFlags(Qt::ItemIsEnabled);  // a sheet cannot be chosen
        QHash<QString, QList<std::pair<int, QString>>> children;
        QString rootId;
        for (auto it = sheet.content.cbegin(); it != sheet.content.cend(); ++it) {
            if (it.key() == QStringLiteral("@map")) { rootId = it.value().value(QStringLiteral("rootId")).toString(); continue; }
            children[it.value().value(QStringLiteral("parent")).toString()].append({it.value().value(QStringLiteral("row")).toInt(), it.key()});
        }
        std::function<void(QTreeWidgetItem *, const QString &)> add = [&](QTreeWidgetItem *parentItem, const QString &id) {
            const QJsonObject node = sheet.content.value(id);
            const QString title = oneLine(node.value(QStringLiteral("title")).toString());
            auto *item = new QTreeWidgetItem(parentItem, {title});
            item->setData(0, IdRole, id);
            item->setData(0, SheetRole, sheet.id);
            item->setData(0, HasTopicLinkRole, !node.value(QStringLiteral("topicLink")).toString().isEmpty());
            item->setData(0, SearchTextRole, title.toCaseFolded());
            // A link to the topic itself is not allowed.
            item->setFlags(id == m_sourceId ? Qt::NoItemFlags : Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_nodeItems.append(item);
            if (id == m_currentTargetId) current = item;
            auto list = children.value(id);
            std::sort(list.begin(), list.end());
            for (const auto &[row, childId] : std::as_const(list)) add(item, childId);
        };
        if (sheet.content.contains(rootId)) add(sheetItem, rootId);
    }
    m_tree->expandAll();
    if (current) {
        m_tree->setCurrentItem(current);
        m_tree->scrollToItem(current);
    }
}

void LinkDialog::filter(const QString &text) {
    const QString needle = text.trimmed().toCaseFolded();
    m_tree->setUpdatesEnabled(false);
    // Backwards: children come after their parent, so a topic already knows whether it has a
    // visible descendant when the visit reaches it.
    QHash<QTreeWidgetItem *, bool> visibleChild;
    visibleChild.reserve(m_nodeItems.size());
    for (auto it = m_nodeItems.crbegin(); it != m_nodeItems.crend(); ++it) {
        QTreeWidgetItem *item = *it;
        const bool visible = needle.isEmpty() || item->data(0, SearchTextRole).toString().contains(needle) || visibleChild.value(item);
        item->setHidden(!visible);
        if (visible && item->parent()) visibleChild[item->parent()] = true;
    }
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *sheet = m_tree->topLevelItem(i);
        sheet->setHidden(!needle.isEmpty() && !visibleChild.value(sheet));
    }
    m_tree->setUpdatesEnabled(true);
    updateButtons();
}

bool LinkDialog::selectNode(const QString &id) {
    buildTree();
    for (QTreeWidgetItem *item : std::as_const(m_nodeItems))
        if (item->data(0, IdRole).toString() == id && (item->flags() & Qt::ItemIsSelectable)) {
            m_tree->setCurrentItem(item);
            return true;
        }
    return false;
}

QString LinkDialog::address() const { return NodeLinks::webAddress(m_address->text()); }

QString LinkDialog::targetId() const {
    const QTreeWidgetItem *item = m_tree->currentItem();
    return item && item->isSelected() && !item->isHidden() ? item->data(0, IdRole).toString() : QString();
}

QString LinkDialog::targetSheetId() const {
    return targetId().isEmpty() ? QString() : m_tree->currentItem()->data(0, SheetRole).toString();
}

bool LinkDialog::alsoBack() const { return m_back->isEnabled() && m_back->isChecked(); }

void LinkDialog::updateButtons() {
    const bool validAddress = !address().isEmpty();
    m_error->setVisible(!validAddress && !m_address->text().trimmed().isEmpty());
    const bool chosen = !targetId().isEmpty();
    acceptButton()->setEnabled(tab() == Tab::Web ? validAddress : chosen);
    // At most one topic link per topic: the link back does not replace an existing one.
    const bool targetLinked = chosen && m_tree->currentItem()->data(0, HasTopicLinkRole).toBool();
    m_back->setEnabled(chosen && !targetLinked);
    if (!m_back->isEnabled()) m_back->setChecked(false);
    m_back->setToolTip(targetLinked ? tr("The chosen topic already has a topic link") : QString());
}
