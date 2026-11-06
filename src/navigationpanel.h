#pragma once

// FEAT-061 / FEAT-072 C: left navigation panel (picture attachments/FEAT-072/06):
// tabs Outline, Notes and Tags, a close button; Outline has a "Find" field with an options menu
// (current map or whole file, whole words, match case, fields). Without text Outline shows the
// topics of the current map as a tree; with text only the matching topics, the found part highlighted.
// A click (or Enter) asks to show the topic. "Find and replace" (options menu) adds a
// Replace field with Replace (the selected result) and Replace all; only topic titles change.
// The panel knows no models: data and actions come from the callbacks below, set by the window.

#include "maphistory.h"
#include "nodesearch.h"

#include <QHash>
#include <QWidget>

#include <functional>

class QLineEdit;
class QPushButton;
class QListWidget;
class QStackedWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

class NavigationPanel final : public QWidget {
    Q_OBJECT
public:
    enum class Tab { Outline, Notes, Tags };
    struct SheetData {
        QString id;
        QString name;
        bool current = false;
        MapHistory::Snapshot content;
    };

    explicit NavigationPanel(QWidget *parent = nullptr);

    // Sheets to search: the current one always, the others only for "Current file" (allSheets).
    std::function<QList<SheetData>(bool allSheets)> sheets;
    std::function<void(const QString &sheetId, const QString &nodeId)> activateTopic;
    std::function<void(const QString &label)> activateLabel;
    // New titles by sheet id and node id, applied in one step; returns how many topics changed.
    using TitleChanges = QHash<QString, QHash<QString, QString>>;
    std::function<int(const TitleChanges &changes)> replaceTitles;
    // FEAT-060 Tags tab: a label renamed on every sheet (empty to = removed), one step; returns the topics changed.
    std::function<int(const QString &from, const QString &to)> renameLabel;

    void showTab(Tab tab);   // and puts the cursor in its field
    void refresh();          // data changed: rebuilds the visible tab (call it debounced)
    NodeSearch::Options options() const { return m_options; }
    bool allSheets() const { return m_allSheets; }

    // For tests.
    QLineEdit *findField() const { return m_find; }
    QLineEdit *notesField() const { return m_notesFind; }
    QTreeWidget *outlineList() const { return m_outline; }
    QTreeWidget *notesList() const { return m_notes; }
    QListWidget *tagsList() const { return m_tags; }
    QToolButton *optionsButton() const { return m_optionsButton; }
    QLineEdit *replaceField() const { return m_replace; }
    QPushButton *replaceButton() const { return m_replaceOne; }
    QPushButton *replaceAllButton() const { return m_replaceAll; }
    void setReplaceMode(bool on);

signals:
    void closeRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuildOutline();
    void rebuildNotes();
    void rebuildTags();
    void showTagMenu(const QPoint &pos);
    void startRenamingTag(const QString &label);
    void deleteTag(const QString &label);
    void activate(QTreeWidgetItem *item);
    QWidget *searchRow(QLineEdit *field, QToolButton *options);
    void replace(bool all);
    void updateReplaceButtons();

    QList<QToolButton *> m_tabs;
    QStackedWidget *m_pages = nullptr;
    QLineEdit *m_find = nullptr;
    QLineEdit *m_notesFind = nullptr;
    QToolButton *m_optionsButton = nullptr;
    QAction *m_findAndReplace = nullptr;
    QWidget *m_replaceRow = nullptr;
    QLineEdit *m_replace = nullptr;
    QPushButton *m_replaceOne = nullptr;
    QPushButton *m_replaceAll = nullptr;
    QTreeWidget *m_outline = nullptr;
    QTreeWidget *m_notes = nullptr;
    QListWidget *m_tags = nullptr;
    QTimer *m_searchDelay = nullptr;
    NodeSearch::Options m_options;
    bool m_allSheets = false;
    QString m_renamingTag;  // label whose row is being edited
};
