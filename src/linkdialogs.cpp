#include "linkdialogs.h"
#include "help.h"
#include "uitheme.h"

#include "nodelinks.h"

#include <QCheckBox>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {
enum Role { IdRole = Qt::UserRole, SheetRole, HasTopicLinkRole, SearchTextRole };

// Titolo su una riga per l'albero (gli "a capo" del titolo diventano spazi).
QString oneLine(const QString &title) {
    QString text = title;
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text.trimmed().isEmpty() ? TopicLinkDialog::tr("(untitled)") : text;
}
}  // namespace

TopicLinkDialog::TopicLinkDialog(QWidget *parent, const QList<SheetContent> &sheets, const QString &sourceId,
                                 const QString &currentTargetId)
    : ModalDialog(parent, tr("Choose the topic to link"), tr("Insert"), 480) {
    Help::setPage(this, QStringLiteral("topic-contents/links.md"));  // FEAT-092: F1 and "?" open it
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search"));
    m_search->addAction(QIcon(QStringLiteral(":/assets/hugeicons/search.svg")), QLineEdit::LeadingPosition);
    m_search->setClearButtonEnabled(true);
    contentLayout()->addWidget(m_search);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setFixedHeight(320);
    m_tree->setUniformRowHeights(true);  // misura delle righe senza chiederla a ogni voce (10.000 nodi)
    // FEAT-035: albero scuro come il modale (AGENTS.md "Interfaccia scura").
    m_tree->setStyleSheet(UiTheme::qss(QStringLiteral(
        "QTreeWidget { background:@surfaceSunken; color:@text; border:0; border-radius:4px; font-size:14px;"
        " outline:0; padding:4px; }"
        "QTreeWidget::item { min-height:28px; border-radius:4px; }"
        "QTreeWidget::item:hover { background:@divider; }"
        "QTreeWidget::item:selected { background:@surfaceRaised; color:@text; }"
        "QTreeWidget::item:disabled { color:@textMuted; }")));
    contentLayout()->addWidget(m_tree);

    // Albero di ogni foglio dalla forma piatta: figli raggruppati per genitore, ordinati per riga.
    QFont sheetFont = m_tree->font();
    sheetFont.setWeight(QFont::DemiBold);
    QTreeWidgetItem *current = nullptr;
    for (const SheetContent &sheet : sheets) {
        auto *sheetItem = new QTreeWidgetItem(m_tree, {sheet.name});
        sheetItem->setFont(0, sheetFont);
        sheetItem->setFlags(Qt::ItemIsEnabled);  // il foglio non si sceglie
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
            // Collegamento al nodo stesso: non permesso.
            item->setFlags(id == sourceId ? Qt::NoItemFlags : Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_nodeItems.append(item);
            if (id == currentTargetId) current = item;
            auto list = children.value(id);
            std::sort(list.begin(), list.end());
            for (const auto &[row, childId] : std::as_const(list)) add(item, childId);
        };
        if (sheet.content.contains(rootId)) add(sheetItem, rootId);
    }
    m_tree->expandAll();

    m_back = new QCheckBox(tr("Also add the link to the chosen topic"), this);
    contentLayout()->addWidget(m_back);

    connect(m_search, &QLineEdit::textChanged, this, &TopicLinkDialog::filter);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &TopicLinkDialog::updateButtons);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (item->flags() & Qt::ItemIsSelectable) accept();
    });
    if (current) {
        m_tree->setCurrentItem(current);
        m_tree->scrollToItem(current);
    }
    updateButtons();
    m_search->setFocus();
}

void TopicLinkDialog::filter(const QString &text) {
    const QString needle = text.trimmed().toCaseFolded();
    m_tree->setUpdatesEnabled(false);
    // Visita a ritroso: i figli vengono dopo il genitore, quindi un nodo sa gia' se ha un
    // discendente visibile quando si arriva a lui.
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

bool TopicLinkDialog::selectNode(const QString &id) {
    for (QTreeWidgetItem *item : std::as_const(m_nodeItems))
        if (item->data(0, IdRole).toString() == id && (item->flags() & Qt::ItemIsSelectable)) {
            m_tree->setCurrentItem(item);
            return true;
        }
    return false;
}

QString TopicLinkDialog::targetId() const {
    const QTreeWidgetItem *item = m_tree->currentItem();
    return item && item->isSelected() && !item->isHidden() ? item->data(0, IdRole).toString() : QString();
}

QString TopicLinkDialog::targetSheetId() const {
    return targetId().isEmpty() ? QString() : m_tree->currentItem()->data(0, SheetRole).toString();
}

bool TopicLinkDialog::alsoBack() const { return m_back->isEnabled() && m_back->isChecked(); }

void TopicLinkDialog::updateButtons() {
    const bool chosen = !targetId().isEmpty();
    acceptButton()->setEnabled(chosen);
    // Al massimo un collegamento ad argomento per nodo: l'inverso non sostituisce quello esistente.
    const bool targetLinked = chosen && m_tree->currentItem()->data(0, HasTopicLinkRole).toBool();
    m_back->setEnabled(chosen && !targetLinked);
    if (!m_back->isEnabled()) m_back->setChecked(false);
    m_back->setToolTip(targetLinked ? tr("The chosen topic already has a topic link") : QString());
}

WebLinkDialog::WebLinkDialog(QWidget *parent, const QString &currentAddress)
    : ModalDialog(parent, tr("Link to a web page"), tr("Insert"), 400) {
    Help::setPage(this, QStringLiteral("topic-contents/links.md"));  // FEAT-092: F1 and "?" open it
    m_address = new QLineEdit(currentAddress, this);
    m_address->setPlaceholderText(QStringLiteral("https://"));
    contentLayout()->addWidget(m_address);
    m_error = new QLabel(tr("Only http, https and mailto addresses are allowed."), this);
    m_error->setObjectName(QStringLiteral("ModalError"));
    contentLayout()->addWidget(m_error);
    auto update = [this] {
        const bool valid = !address().isEmpty();
        acceptButton()->setEnabled(valid);
        m_error->setVisible(!valid && !m_address->text().trimmed().isEmpty());
    };
    connect(m_address, &QLineEdit::textChanged, this, update);
    update();
    m_address->selectAll();
    m_address->setFocus();
}

QString WebLinkDialog::address() const { return NodeLinks::webAddress(m_address->text()); }
