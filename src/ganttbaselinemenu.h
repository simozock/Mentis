#pragma once
// FEAT-082 (docs/features/FEAT-082-gantt-baseline.md): the popover of the top bar's Baseline
// button. It shows the sheet's snapshots and asks for the changes through its signals; the Gantt
// view applies them and calls setEntries again, so the popover stays open on the new list.
#include <QDateTime>
#include <QFrame>
#include <QList>
#include <QString>
#include <QStringList>

class QLineEdit;
class QPushButton;
class QVBoxLayout;

class GanttBaselineMenu final : public QFrame {
    Q_OBJECT
public:
    static constexpr int kWidth = 300;
    struct Entry {
        QString id;
        QString name;
        QDateTime taken;
    };

    explicit GanttBaselineMenu(QWidget *parent = nullptr);

    // Entries oldest first (shown newest first); compared = the id shown in the chart (empty: none);
    // removed = names of the compared snapshot's tasks gone since. canCreate: the sheet has tasks.
    void setEntries(const QList<Entry> &entries, const QString &compared, const QStringList &removed, bool canCreate,
                    const QString &defaultName);
    void popupBelow(QWidget *anchor);

    QLineEdit *nameField() const { return m_name; }
    QPushButton *saveButton() const { return m_save; }

signals:
    void createRequested(const QString &name);
    void compareRequested(const QString &id);  // empty: none
    void renameRequested(const QString &id, const QString &name);
    void removeRequested(const QString &id);

protected:
    void paintEvent(QPaintEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild();
    void startRename(const QString &id);

    QLineEdit *m_name = nullptr;
    QPushButton *m_save = nullptr;
    QWidget *m_list = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QList<Entry> m_entries;
    QString m_compared;
    QStringList m_removed;
    bool m_removedOpen = false;
};
