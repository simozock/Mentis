#pragma once

#include "modaldialog.h"

#include <QHash>
#include <QJsonObject>
#include <QList>

#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class SegmentedControl;

// FEAT-117: one dialog for a topic's links (FEAT-027), with a tab for a web page and a tab for
// another topic. Insert applies the visible tab only; each tab keeps its input while switching.
// Web page: the address, valid as NodeLinks::webAddress, with the reason under the field when the
// scheme is not allowed. Topic: the tree of every sheet with its topics (the history's flat form,
// MindMapModel::toFlatSnapshot) and a search that keeps the matches and their ancestors; the
// source topic cannot be chosen; the checkbox adds the link back and is off when the chosen topic
// already has a topic link. The tree is built the first time the Topic tab shows (sheets() may
// read sheets from the file).
class LinkDialog final : public ModalDialog {
    Q_OBJECT
public:
    enum class Tab { Web, Topic };
    struct SheetContent {
        QString id;
        QString name;
        QHash<QString, QJsonObject> content;
    };
    LinkDialog(QWidget *parent, Tab tab, const QString &currentAddress, std::function<QList<SheetContent>()> sheets,
               const QString &sourceId, const QString &currentTargetId);

    Tab tab() const;
    void setTab(Tab tab);
    SegmentedControl *tabs() const { return m_tabs; }

    QString address() const;  // full address, empty if not valid
    QLineEdit *addressField() const { return m_address; }

    QString targetId() const;
    QString targetSheetId() const;
    bool alsoBack() const;
    QLineEdit *searchField() const { return m_search; }
    QTreeWidget *tree() const { return m_tree; }
    QCheckBox *backCheck() const { return m_back; }
    // Shows only the topics containing text (case ignored) and their ancestors.
    void filter(const QString &text);
    // Selects the topic with that id (tests, and editing a link).
    bool selectNode(const QString &id);

private:
    void buildTree();
    void updateButtons();
    SegmentedControl *m_tabs = nullptr;
    QStackedWidget *m_pages = nullptr;
    QLineEdit *m_address = nullptr;
    QLabel *m_error = nullptr;
    QLineEdit *m_search = nullptr;
    QTreeWidget *m_tree = nullptr;
    QCheckBox *m_back = nullptr;
    std::function<QList<SheetContent>()> m_sheets;  // reset once the tree is built
    QString m_sourceId, m_currentTargetId;
    QList<QTreeWidgetItem *> m_nodeItems;  // every topic, in the tree's order
};
