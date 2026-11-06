#pragma once
// FEAT-078 stage B (docs/features/FEAT-078-gantt-colonne-pannello-zoom.md): the panel at the left of
// the Gantt table with all of a task's data (the "Task" panel). It shows the values the view
// gives it and sends every change through the view's functions (one history step each). A rounded
// card light or dark with the Gantt page (user, 2026-10-28). FEAT-080: two pages, Task and
// Gantt (the chart's style), as the map's side panel.
#include "datepicker.h"  // DatePicker::Colors
#include "ganttplan.h"
#include "ganttresources.h"

#include <QColor>
#include <QHash>
#include <QSet>
#include <QWidget>

#include <functional>
#include <optional>

class ColorField;
class QComboBox;
class QDateEdit;
class DateField;
class QLabel;
class QLayout;
class QLineEdit;
class QMenu;
class NumberField;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class SegmentedControl;
class QToolButton;
class QVBoxLayout;
class ToggleSwitch;
struct GanttColors;

class GanttTaskPanel final : public QWidget {
    Q_OBJECT
public:
    static constexpr int kWidth = 300;

    explicit GanttTaskPanel(QWidget *parent = nullptr);

    // Set by the view: plan edits (Document::editGantt), the topic's title, priority and people.
    std::function<bool(const std::function<bool(GanttPlan &)> &)> edit;
    std::function<bool(const QString &id, const QString &name)> rename;
    // On the tasks the panel edits (targets): nullopt = unchanged.
    std::function<bool(const QStringList &ids, std::optional<int> priority)> setInfo;
    // FEAT-081: resources (ids) assigned to all of the tasks, and removed from all of them.
    std::function<bool(const QStringList &ids, const QStringList &assign, const QStringList &unassign)> setPeople;
    // FEAT-083: a person's share of the tasks (ids) that have that person.
    std::function<bool(const QStringList &ids, const QString &person, int percent)> setPercent;
    std::function<void()> openResources;  // "Add people…" with no people in the document
    std::function<bool(const QStringList &ids, bool shown)> setLabel;  // the name on the bar
    std::function<bool(const QStringList &ids, bool milestone)> setMilestone;  // adds or removes the topics' checkbox
    // The bar's colour is the topic's branch colour; nullopt = back to the default colour.
    std::function<bool(const QStringList &ids, const std::optional<QColor> &color)> setBarColor;
    std::function<bool(const QStringList &ids)> remove;  // deletes the tasks' topics (map too)
    std::function<bool(GanttPlan::BarShape shape)> setBarShape;  // FEAT-080: every bar of the sheet
    std::function<bool(GanttPlan::SummaryShape shape)> setSummaryShape;  // every task with subtasks
    // FEAT-080: the tasks' own shape, fill or border (a GanttPlan::styleKey), empty = back to the sheet's.
    std::function<bool(const QStringList &ids, GanttPlan::OwnStyle part, const QString &key)> setTaskStyle;
    std::function<bool(GanttPlan::LinkLine line)> setLinkLine;  // every dependency arrow
    std::function<bool(BranchEnd end)> setLinkEnd;
    std::function<bool(GanttPlan::LinkWidth width)> setLinkWidth;
    std::function<bool(GanttPlan::LinkEndSize size)> setLinkEndSize;
    // The sheet's colour of the tasks or summaries; nullopt = back to the default.
    std::function<bool(GanttPlan::MilestoneShape shape)> setMilestoneShape;  // every milestone
    std::function<bool(GanttPlan::BarKind kind, GanttPlan::BorderStyle style)> setBorderStyle;
    std::function<bool(GanttPlan::BarKind kind, NodeFillStyle style)> setFillStyle;  // every bar of that kind  // every bar of that kind
    std::function<bool(GanttPlan::BarKind kind, const std::optional<QColor> &color)> setDefaultColor;

    enum class Page { Task, Gantt };
    Page page() const;
    void showPage(Page page);
    void showBarShape(GanttPlan::BarShape shape);
    void showSummaryShape(GanttPlan::SummaryShape shape);
    void showMilestoneShape(GanttPlan::MilestoneShape shape);
    void showBorderStyle(GanttPlan::BarKind kind, GanttPlan::BorderStyle style);
    void showFillStyle(GanttPlan::BarKind kind, NodeFillStyle style);
    void showLinkStyle(const GanttPlan &plan);  // line, end, thickness and end size
    void showDefaultColor(GanttPlan::BarKind kind, const QColor &shown, bool chosen);  // chosen: the sheet's own
    void clear();  // no task: the Task page says so

    // Shows the task's values; a field being edited keeps its text. With several targets (the
    // selected tasks, id first) the edits apply to all of them: a value they do not share shows
    // none (priority -1); name and links are the shown task's only. people: each target's
    // resources (ids), as the topics store them.
    void load(const QString &id, const GanttPlan &plan, const GanttSchedule &schedule, int priority,
              const QHash<QString, QStringList> &people, bool labelled = false, const QStringList &targets = {});
    // FEAT-081: the document's resources, for the tags and the "+" menu (the list's order).
    void setResources(const QVector<GanttResource> &people);
    QStringList targets() const { return m_targets; }
    QString taskId() const { return m_id; }
    // The targets' bar colour (invalid = different colours); manual = some has a colour of its own.
    void showBarColor(const QColor &color, bool manual);
    void commitPendingEdit();  // a field being edited is confirmed (view switch, save)
    void setColors(const GanttColors &colors);  // light or dark card, as the Gantt page
    // Applies a style sheet that setColors() kept while the panel was hidden (also on show).
    void applyPendingStyle();

    // Fields, for the tests.
    QLineEdit *nameField() const { return m_name; }
    NumberField *progressField() const { return m_progress; }
    QComboBox *priorityField() const { return m_priority; }
    DateField *startField() const { return m_start; }
    QToolButton *clearStartButton() const { return m_clearStart; }
    QPushButton *addStartButton() const { return m_addStart; }
    NumberField *durationField() const { return m_duration; }
    DateField *endField() const { return m_end; }
    // FEAT-081: the Resources section's table of the people assigned and its "+ Add".
    QTableWidget *peopleTable() const { return m_peopleTable; }
    QToolButton *addPersonButton() const { return m_addPerson; }
    QStringList shownPeople() const { return m_peopleRows; }  // the rows' resource ids, in order
    QToolButton *personRemove(const QString &id) const;  // a row's ×
    class NumberField *personPercent(const QString &id) const;  // FEAT-083: a row's %
    // FEAT-083: target id -> person id -> share, for those not at 100 %; set before load().
    void setPercents(const QHash<QString, QHash<QString, int>> &percents) { m_percents = percents; }
    bool personPartial(const QString &id) const { return m_partialPeople.contains(id); }  // muted row
    QStringList personCandidates() const;  // the "+" menu's people: not assigned to every task
    void fillPersonMenu(QMenu *menu);  // the "+" menu's items
    // The table's columns, "first", "last", "role": shown ones in order (app setting
    // "gantt/resourceColumns"); the last shown one cannot be hidden. The × column stays last.
    QStringList resourceColumns() const;
    bool setResourceColumnShown(const QString &key, bool shown);
    void fillResourceColumnMenu(QMenu *menu);  // the header's right-click menu
    // Every section closes from its title (user, 2026-10-30); kept in "gantt/closedSections".
    bool sectionOpen(const QString &name) const { return !m_closedSections.contains(name); }
    void setSectionOpen(const QString &name, bool open);
    ToggleSwitch *milestoneSwitch() const { return m_milestone; }
    ToggleSwitch *labelSwitch() const { return m_label; }
    ColorField *colorField() const { return m_color; }
    QToolButton *defaultColorButton() const { return m_defaultColor; }
    SegmentedControl *pageSelector() const { return m_pageSelector; }
    QComboBox *barShapeField() const { return m_barShape; }
    QComboBox *summaryShapeField() const { return m_summaryShape; }
    QComboBox *ownBarShapeField() const { return m_ownBarShape; }  // the Task page's Style section
    QComboBox *ownSummaryShapeField() const { return m_ownSummaryShape; }
    QComboBox *ownMilestoneShapeField() const { return m_ownMilestoneShape; }
    QComboBox *ownFillField() const { return m_ownFill; }
    QComboBox *ownBorderField() const { return m_ownBorder; }
    QToolButton *defaultShapeButton() const { return m_defaultShape; }
    QToolButton *defaultFillButton() const { return m_defaultFill; }
    QToolButton *defaultBorderButton() const { return m_defaultBorder; }
    QWidget *styleSection() const { return m_styleSection; }
    QComboBox *milestoneShapeField() const { return m_milestoneShape; }
    QComboBox *borderStyleField(GanttPlan::BarKind kind) const { return m_borderStyle[int(kind)]; }
    QComboBox *fillStyleField(GanttPlan::BarKind kind) const { return m_fillStyle[int(kind)]; }
    QComboBox *linkLineField() const { return m_linkLine; }
    QComboBox *linkEndField() const { return m_linkEnd; }
    QComboBox *linkWidthField() const { return m_linkWidth; }
    QComboBox *linkEndSizeField() const { return m_linkEndSize; }
    ColorField *defaultColorField(GanttPlan::BarKind kind) const { return m_sheetColor[int(kind)]; }
    QToolButton *defaultColorReset(GanttPlan::BarKind kind) const { return m_sheetColorReset[int(kind)]; }
    bool showsNoTask() const;
    // Links of the task shown: predecessors (id comes after) or successors (id comes first).
    QList<GanttDependency> links(bool predecessors) const;
    // The other tasks a new link may reach (leaves, no existing link, no cycle).
    QStringList linkCandidates(bool predecessors) const;
    bool addLink(bool predecessors, const QString &other);
    // The type select, lag field, sentence and remove button of a link row (the other task's id).
    QComboBox *linkType(bool predecessors, const QString &other) const;
    NumberField *linkLag(bool predecessors, const QString &other) const;
    QString linkSentence(bool predecessors, const QString &other) const;
    QToolButton *linkRemove(bool predecessors, const QString &other) const;
    // What a link means in words, from the task shown ("Starts after «A» ends, 2 days later").
    static QString sentence(const GanttDependency &link, bool predecessors, const QString &otherName);
    // A priority's name, 1-5 ("Very low" ... "Very high"); empty for 0 (none).
    static QString priorityName(int priority);

signals:
    void closeRequested();
    // Up or Down pressed in the panel, outside a number or date field (user, 2026-10-29): the view
    // moves to the task above or below, as in the table.
    void stepRequested(int key, Qt::KeyboardModifiers modifiers);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Startup (2026-11-04): setting the style sheet re-styles every control of the panel at once,
    // ~250 ms measured, even while hidden; a hidden panel keeps it here until it is needed.
    QString m_pendingStyleSheet;
    struct LinkRow {
        QWidget *row = nullptr;
        QLabel *name = nullptr;
        QLabel *parent = nullptr;  // the other task's summary, to tell same names apart
        QString sentence;  // what the link means: the card's tooltip
        QComboBox *type = nullptr;
        NumberField *lag = nullptr;
        QToolButton *remove = nullptr;
    };
    // A section as the Style page's: a line, the title (always open), header at the
    // right of the title, then content. name is the section's object name.
    QWidget *section(const QString &name, const QString &title, QLayout *content, QWidget *header = nullptr);
    void loadLinks(bool predecessors);
    void fillBarShapes(const QColor &text);  // the Gantt page's pictures: bars, summaries, relations
    void loadSeveral(bool same);  // same = the tasks shown before
    void loadStyle();  // FEAT-080: the Style section's shape, fill and border
    void loadPeople();  // the rows of the people assigned to the tasks
    void restoreResourceColumns();
    void resourceColumnMoved();  // keeps the × last, saves the order
    QStringList peopleTargets() const;  // the targets that can have people (not summaries)
    LinkRow makeLinkRow(bool predecessors, const QString &other);
    void showAddMenu(bool predecessors);
    QString parentName(const QString &id) const;
    bool editPlan(const std::function<bool(GanttPlan &)> &change);
    // One step that changes every target that is not a summary (and not a milestone if asked).
    bool editLeaves(const std::function<bool(GanttPlan &, const QString &)> &change, bool skipMilestones = false);

    QString m_id;
    GanttPlan m_plan;
    GanttSchedule m_schedule;
    int m_priorityValue = 0;
    QVector<GanttResource> m_resources;
    QHash<QString, QStringList> m_assigned;  // target id -> resource ids
    QHash<QString, QHash<QString, int>> m_percents;  // FEAT-083
    QStringList m_targets;  // the tasks edited: m_id first
    DatePicker::Colors m_listColors;  // the card's colours, also for selects made later (link rows)
    SegmentedControl *m_pageSelector;
    QStackedWidget *m_pages;  // task, no task, Gantt
    QComboBox *m_barShape;
    QComboBox *m_summaryShape;
    QComboBox *m_ownBarShape;
    QComboBox *m_ownSummaryShape;
    QComboBox *m_ownMilestoneShape;
    QComboBox *m_ownFill;
    QComboBox *m_ownBorder;
    QToolButton *m_defaultShape;
    QToolButton *m_defaultFill;
    QToolButton *m_defaultBorder;
    QWidget *m_styleSection;
    QWidget *m_detailsSection = nullptr;  // progress and priority, hidden for summaries
    QComboBox *m_milestoneShape;
    QComboBox *m_fillStyle[GanttPlan::kBarKinds]{};  // by BarKind
    QComboBox *m_borderStyle[GanttPlan::kBarKinds]{};  // by BarKind
    QComboBox *m_linkLine;
    QComboBox *m_linkEnd;
    QComboBox *m_linkWidth;
    QComboBox *m_linkEndSize;
    ColorField *m_sheetColor[GanttPlan::kBarKinds];  // by BarKind
    QToolButton *m_sheetColorReset[GanttPlan::kBarKinds];
    QLabel *m_title;  // the Task section's: "Task", or "3 tasks" with several
    QLineEdit *m_name;
    NumberField *m_progress;
    QComboBox *m_priority;
    DateField *m_start;
    QToolButton *m_clearStart;
    QPushButton *m_addStart;
    NumberField *m_duration;
    DateField *m_end;
    QWidget *m_resourcesSection;  // hidden for summaries
    QLabel *m_noPeople;
    QTableWidget *m_peopleTable;
    QToolButton *m_addPerson;
    QStringList m_peopleRows;  // resource id of each row
    QSet<QString> m_partialPeople;  // on only some of the targets
    QColor m_rowText, m_mutedText;
    QSet<QString> m_closedSections;  // object names
    QHash<QString, std::pair<QLabel *, QWidget *>> m_sectionParts;  // name -> chevron, body
    ToggleSwitch *m_milestone;
    ToggleSwitch *m_label;
    ColorField *m_color;
    QToolButton *m_defaultColor;
    QWidget *m_linkSections;
    QWidget *m_datesSection;  // hidden for summaries
    QPushButton *m_remove;
    QLabel *m_noLinks[2];
    QVBoxLayout *m_linkLists[2];  // successors, predecessors
    QToolButton *m_addLink[2];
    QHash<QString, LinkRow> m_rows[2];  // other task's id -> row
};
