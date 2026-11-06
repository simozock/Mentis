#pragma once
// FEAT-075 (docs/features/FEAT-075-gantt-foglio-e-vista.md): the Gantt view of the active sheet. Task
// table on the left, time chart on the right. The plan is read from the sheet's model and changed
// only through Document::editGantt, one history step per edit.
#include "datepicker.h"
#include "ganttplan.h"
#include "nodesketch.h"

#include <QAbstractScrollArea>
#include <QHash>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QSet>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <optional>

class Document;
class GanttBaselineMenu;
struct GanttExportSource;
struct MindNode;
class GanttTableModel;
class GanttTaskPanel;
class QMenu;
class QPushButton;
class QStackedLayout;
class QToolButton;
class QTreeView;
class ShortcutRegistry;

// Colours of a bar, those of its topic on the map (FEAT-075, user's choice 2026-10-27): border =
// branch colour, fill and text = the topic's (a fill may be transparent, like on the map).
struct GanttBarLook {
    QColor fill;
    QColor border;
    QColor text;
};

// The colours a bar is painted in: its fill, border, done part and name (FEAT-083: the exports
// take the same from GanttChart::barPaint).
struct GanttBarPaint {
    QColor fill;
    QColor border;
    QColor done;
    QColor text;
};

// Colours of the page around the bars: light, or dark on a dark map like the outliner (FEAT-075,
// user's request 2026-10-27). Never the map's own colour. The task
// panel, the table and the chart are three rounded cards (card) on the page (user, 2026-10-28).
struct GanttColors {
    QColor page, text, muted, grid, line, shade, selected, badge, badgeText, card, headerLine;
    static constexpr int kCardRadius = 10;
    static constexpr int kCardGap = 12;
    static GanttColors light();
    static GanttColors dark();
    DatePicker::Colors picker() const;  // the calendar on the Gantt's cards
};

// The chart: header with the time scale, bars, milestones, summaries, dependency arrows. Rows follow
// the table (same order, height and vertical scroll).
class GanttChart final : public QAbstractScrollArea {
    Q_OBJECT
public:
    enum class Scale { Days, Weeks, Months };
    static constexpr int kRowHeight = 32;
    static constexpr int kHeaderHeight = 48;

    explicit GanttChart(QWidget *parent = nullptr);

    // Edits go through this function (Document::editGantt); vertical offset of the table rows.
    std::function<bool(const std::function<bool(GanttPlan &)> &)> edit;
    std::function<int()> verticalOffset;
    QString menuStyle;  // style sheet of the context menus

    void setData(const GanttPlan &plan, const GanttSchedule &schedule, const QStringList &rows,
                 const QSet<QString> &selected, const QHash<QString, GanttBarLook> &looks = {});
    Scale scale() const { return m_scale; }
    void setScale(Scale scale);
    // A month's name in the header, in the interface language and with a capital (BUG-035).
    QString monthName(int month, QLocale::FormatType format) const;
    // Bars that show their name (the view decides: summaries by default, or the topic's choice).
    void setLabelled(const QSet<QString> &ids) { m_labelled = ids; viewport()->update(); }
    bool showsName(const QString &id) const { return m_labelled.contains(id); }
    // The critical path (red halo and arrows) can be hidden (user, 2026-10-28; app setting gantt/critical).
    void setCriticalShown(bool shown) { m_criticalShown = shown; viewport()->update(); }
    bool criticalShown() const { return m_criticalShown; }
    // Drawn: shown and not comparing with a snapshot, where red means "later" (user, 2026-10-30).
    bool criticalPainted() const { return m_criticalShown && !m_baseline; }
    // FEAT-082: the snapshot compared (none: nullopt). While comparing the bars are shorter, the
    // snapshot's bar a dashed outline behind each and a thin bar under it; new tasks a "New" pill.
    void setBaseline(const std::optional<GanttBaseline> &baseline, const GanttBaselineComparison &comparison);
    bool comparing() const { return m_baseline.has_value(); }
    QRectF baselineRect(const QString &id) const;      // the snapshot's bar (outline); empty if none
    QRectF baselineMarkRect(const QString &id) const;  // the thin grey bar (a small diamond for milestones)
    // The slip on the thin bar's line, from the snapshot's end to the current one; empty when the
    // end is the same. Coloured red (later) or green (earlier); baselineMarkColor grey otherwise.
    QRectF slipRect(const QString &id) const;
    QColor baselineMarkColor(const QString &id) const;
    QRectF newPillRect(const QString &id) const;       // before the bar of a task not in the snapshot
    // FEAT-078: zoom 50-300 % of the day width of every scale; the date at anchorX stays in place.
    static constexpr int kMinZoom = 50;
    static constexpr int kMaxZoom = 300;
    int zoomPercent() const { return m_zoom; }
    void setZoomPercent(int percent, double anchorX = 0);
    // User, 2026-10-29: the day shown kOpenDaysBefore days from the left edge, so the days just
    // before it are in view. Go to today shows today; a sheet opened shows openingDay().
    static constexpr int kOpenDaysBefore = 3;
    void scrollToDay(const QDate &day);
    // FEAT-083 (export): day at the left edge, and no card corners painted.
    void setFirstDay(const QDate &day);
    void setPrinting(bool printing) { m_printing = printing; }
    void scrollToToday() { scrollToDay(QDate::currentDate()); }
    // Today; the last task's end when the plan ended before today; the first task's start when it
    // has not begun yet (user, 2026-10-30).
    QDate openingDay() const;
    void scrollToOpeningDay() { scrollToDay(openingDay()); }
    double xOf(const QDate &date) const { return x(date); }  // left edge of the day, viewport x
    void setColors(const GanttColors &colors);
    // Geometry in viewport coordinates, for the view and the tests. Empty when the row is not shown.
    QRectF barRect(const QString &id) const;
    QPointF linkHandle(const QString &id) const;         // circle after the bar's end: id comes first
    QPointF linkHandleAtStart(const QString &id) const;  // circle before its start: id comes after
    QPointF progressHandle(const QString &id) const;     // centre of the progress badge
    // Badge with the percentage under the done part (progress < 0: the task's own).
    QRectF progressBadge(const QString &id, int progress = -1) const;
    QPolygonF arrowPath(int dependency) const;  // the dependency's line, from predecessor to arrow tip
    int dayWidth() const;
    QDate origin() const { return m_origin; }
    // FEAT-077: the arrow's menu: the four types (the current one checked), Lag…, Delete dependency.
    // Choosing an entry edits the plan (one step, none when the value is the same).
    QMenu *dependencyMenu(int dependency, QWidget *parent);
    bool setDependency(const GanttDependency &dependency);  // type and lag of an existing pair
    static QString typeName(GanttDependency::Type type);
    // FEAT-080: a summary drawn thin over its subtasks (every shape but Bar), one closed outline in
    // rect (also the pictures of the panel's Summaries select). The bracket: a band across the top
    // with a tip hanging down at each end; the group frame: its header (the frame is paintFrames').
    static QPainterPath summaryPath(GanttPlan::SummaryShape shape, const QRectF &rect);
    // FEAT-080: a bar's outline in box, grown by grow px with the same angles (selection ring,
    // critical halo); also the pictures of the panel's Tasks select. barTip: the length of the
    // Arrow's and Hexagon's points and of the Parallelogram's slant (0 for the others).
    static QPainterPath taskPath(GanttPlan::BarShape shape, const QRectF &box, qreal grow = 0);
    static double barTip(GanttPlan::BarShape shape, const QRectF &box);
    // FEAT-080: the pen pattern of the sheet's arrows (also the Line select's pictures).
    static Qt::PenStyle linkPenStyle(GanttPlan::LinkLine line);
    // FEAT-080: the arrows' thickness (critical ones 0.75 px more) and the length of their end
    // mark, in px (user, 2026-11-01; also the Thickness and End size selects' pictures).
    static qreal linkWidthPx(GanttPlan::LinkWidth width);
    static qreal linkEndSizePx(GanttPlan::LinkEndSize size);
    // FEAT-080: the pen of a bar's or milestone's border (None: Qt::NoPen); flat caps keep the dots apart.
    static QPen borderPen(GanttPlan::BorderStyle style, const QColor &color, qreal width);
    static bool isHandDrawn(GanttPlan::BorderStyle style);  // drawn along NodeSketch::roughOutline
    // FEAT-080: a milestone's outline in its box (diamond: the whole box; the others: the square on
    // its centre as tall as the box), grown by grow px (selection ring, critical halo). Also the
    // pictures of the panel's Milestones select.
    static QPainterPath milestonePath(GanttPlan::MilestoneShape shape, const QRectF &box, qreal grow = 0);
    QString dependencyLabel(int dependency) const;  // tooltip: type, and the lag when not 0
    // Text inside a bar: the name, elided; empty when no letter fits (user, 2026-10-27: an empty
    // bar, not one with "…" alone).
    static QString barLabel(const QString &name, const QFont &font, double width);
    // A bar's colours: the topic's hand colour (look, or null), else the sheet's or the view's default
    // for its kind. The light page's colours of the critical path, the snapshot and today (FEAT-083).
    static GanttBarPaint barPaint(const GanttPlan &plan, const QString &id, const GanttBarLook *look, bool summary);
    static QColor criticalColor();
    static QColor baselineColor();
    static QColor todayColor();

signals:
    void taskClicked(const QString &id, Qt::KeyboardModifiers modifiers);
    void rowMenuRequested(const QString &id, const QPoint &globalPos);
    void zoomChanged(int percent);
    void stepRequested(int key, Qt::KeyboardModifiers modifiers);  // Up/Down: the task above or below


protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool viewportEvent(QEvent *event) override;

private:
    enum class Drag { None, Move, Resize, Progress, Link };
    const GanttTask *taskOf(const QString &id) const;  // constant time, unlike GanttPlan::task
    const GanttBaselineTask *baselineTask(const QString &id) const;  // FEAT-082, constant time
    void paintBaseline(QPainter &painter, const QString &id, const QColor &border);
    QPainterPath barShape(const QString &id, const QRectF &box, bool milestone) const;  // pill, rectangle, bracket or milestone
    bool isMilestone(const QString &id) const;  // a diamond: a milestone that is not a summary
    double x(const QDate &date) const;    // left edge of the day
    QDate dateAt(double x) const;
    int rowAt(double y) const;            // -1 outside the rows
    double rowTop(int row) const;
    QString barAt(const QPointF &pos) const;
    bool onResizeGrip(const QRectF &bar, const QPointF &pos) const;  // task whose bar (or milestone) is under pos
    int dependencyAt(const QPointF &pos) const;
    bool linkAllowed(const QString &from, const QString &to) const;
    QString linkHandleAt(const QString &id, const QPointF &pos, bool *atStart = nullptr) const;
    QString linkFrom(const QString &target) const;  // ends of the link being drawn onto target
    QString linkTo(const QString &target) const;
    void updateRange();
    void updateScrollBars();
    void paintHeader(QPainter &painter, const QRect &area);
    void paintTask(QPainter &painter, const QString &id, int row);
    // FEAT-080: the 16 px place of a thin summary shape on its bar's box; a group frame's header
    // reaches out of the summary's days, more for every summary level inside it.
    QRectF summaryPlace(const QString &id, const QRectF &box) const;
    void updateFrames();  // the rows each group frame spans, when the rows change
    GanttPlan::BarShape barShapeOf(const QString &id) const;  // FEAT-080: the task's own, or the sheet's
    GanttPlan::SummaryShape summaryShapeOf(const QString &id) const;
    GanttPlan::MilestoneShape milestoneShapeOf(const QString &id) const;
    void paintFrames(QPainter &painter, int firstRow, int lastRow);  // under the arrows and bars
    // FEAT-080: a bar's or milestone's outline with its border pen; hand-drawn along a rough outline.
    void strokeBorder(QPainter &painter, const QString &id, const QPainterPath &shape, const QPen &pen, bool handDrawn);
    // FEAT-080: a bar's or milestone's shape in the sheet's fill style, as the map's topics (NodeSketch).
    void fillShape(QPainter &painter, const QString &id, const QPainterPath &shape, NodeFillStyle style, const QColor &color);
    void paintArrows(QPainter &painter);
    int barsCrossed(double x, int rowA, int rowB) const;
    void finishDrag(const QPointF &pos);

    GanttPlan m_plan;
    GanttSchedule m_schedule;
    QStringList m_rows;
    QHash<QString, int> m_rowOf;
    QHash<QString, int> m_taskIndex;
    QHash<QString, GanttBarLook> m_looks;
    // FEAT-080: group frames by the summary's row: its last row shown, and the summary levels inside it.
    struct SummaryFrame {
        int last = -1;
        int levels = 0;
    };
    QHash<int, SummaryFrame> m_frames;
    GanttColors m_colors = GanttColors::light();
    QSet<QString> m_selected;
    QSet<QString> m_labelled;
    bool m_criticalShown = true;
    bool m_printing = false;  // FEAT-083
    std::optional<GanttBaseline> m_baseline;  // FEAT-082
    QHash<QString, int> m_baselineIndex;
    GanttBaselineComparison m_comparison;
    Scale m_scale = Scale::Days;
    QLocale m_names = DatePicker::namesLocale();  // read once: the language applies from the next start
    int m_zoom = 100;
    QDate m_origin;
    QDate m_last;
    QString m_hovered;
    Drag m_drag = Drag::None;
    QString m_dragId;
    QPointF m_pressPos;
    QPointF m_dragPos;
    bool m_linkFromStart = false;  // the link drawn from the start circle
    // Hand-drawn fills and patterns by task, at the shape's top left: rebuilt only when the shape's
    // size or the style change, so scrolling does not redraw the strokes' geometry.
    struct Sketch {
        QPainterPath outline;
        NodeFillStyle style = NodeFillStyle::Pieno;
        QPainterPath area;
        NodeSketch::Pattern pattern;
        QPainterPath borderOutline;  // the hand-drawn border, rebuilt when its outline changes
        QPainterPath border;
    };
    QHash<QString, Sketch> m_sketches;
};

class GanttView final : public QWidget {
    Q_OBJECT
public:
    // The task table cannot be dragged narrower than its columns (user, 2026-10-27): every column
    // header stays whole, the name column at least this wide.
    static constexpr int kNameMinWidth = 100;
    // FEAT-078: height of the action buttons' row above the columns (and of the chart's band).
    static constexpr int kActionsHeight = 40;

    GanttView(Document *document, ShortcutRegistry *shortcuts, QWidget *parent = nullptr);

    // Style sheet of the app's popup menus (the same as the other context menus).
    void setMenuStyle(const QString &styleSheet);
    GanttChart::Scale scale() const { return m_chart->scale(); }
    void setScale(GanttChart::Scale scale) { m_chart->setScale(scale); }
    bool criticalShown() const { return m_chart->criticalShown(); }
    void setCriticalShown(bool shown);  // kept in the setting gantt/critical
    // FEAT-082: snapshots of the sheet's plan (one step each, through Document::editGantt) and the
    // one compared, a state of the view (not saved; it stops on another sheet or when removed).
    QString addBaseline(const QString &name);  // its id; empty when refused
    bool renameBaseline(const QString &id, const QString &name);
    bool removeBaseline(const QString &id);
    void compareBaseline(const QString &id);  // empty: none
    QString comparedBaseline() const { return m_baselineId; }
    QString nextBaselineName() const;  // "Baseline N", the first N not used
    void showBaselineMenu(QWidget *anchor);
    // FEAT-083: what the view shows, for the export: the shown columns in their order, every row
    // (folded summaries opened), the bars' colours, the compared snapshot, the costs' inputs.
    GanttExportSource exportSource();
    GanttBaselineMenu *baselineMenu() const { return m_baselineMenu; }
    // Plan, sheet or calendar changed: rebuilds table and chart, keeping selection and folding.
    void refresh();
    void commitPendingEdit();
    // Dark style on a dark map (the window decides with NodeStyle::isDarkBackground, as for the
    // outliner); pageColor is the background the window's bars and fade blend into.
    void setDark(bool dark);
    QColor pageColor() const { return m_colors.page; }
    void focusContent();  // the table, or the "Add task" button of an empty plan

    // Commands of the keys and menus; each is one history step. Rows are the map's topics: addTask
    // adds a sibling after the selected topic (or its child, or a main topic) and opens its name;
    // returns the new id. Indent, outdent, delete and rename work on the topics.
    QString addTask(bool child = false);
    bool indentSelected();
    bool outdentSelected();
    // The topic goes before, inside (last child) or after the target's, with its branch, in one
    // step (table rows dragged, as in the outliner). False for a cycle or the same place.
    enum class DropPlace { Before, Inside, After };
    bool moveTask(const QString &id, const QString &target, DropPlace place);
    bool removeSelected();
    bool removeTasks(const QStringList &ids);  // their topics, map too
    bool renameTask(const QString &id, const QString &name);
    // Run after a change of the map's topics before the step closes (MainWindow: layout of the map,
    // outliner); without it the step is closed at once.
    std::function<void()> afterMapChange;
    // FEAT-085: Copy style / Paste style of the row menu (MainWindow's commands, on the selected tasks).
    std::function<void()> copyStyle, pasteStyle;
    std::function<bool()> canPasteStyle;
    // Finish-start links between the selected tasks in the order they were selected (summaries
    // left out, links that exist or would close a cycle skipped), one step ("Dependencies").
    // When some of the tasks have links, all their links are removed instead (a toggle).
    bool chainSelected();
    // On the model's plan, as chainSelected() edits it; the buttons and menus pass the table's, as
    // MindMapModel::gantt builds a new plan from the map, too slow for every selection change.
    QStringList chainIds() const { return chainIds(currentPlan()); }
    bool chainUnlinks() const { return chainUnlinks(currentPlan()); }  // chainSelected() would remove links
    QStringList chainIds(const GanttPlan &plan) const;
    bool chainUnlinks(const GanttPlan &plan) const;
    void chooseColor(const QPoint &globalPos);
    QStringList selectedIds() const;
    QString currentId() const;
    // Selects the rows shown from one task to the other, both included (Shift+click on a bar).
    void selectRange(const QString &from, const QString &to, bool add);
    void selectTask(const QString &id);

    // FEAT-078: optional columns (Start, End, Duration, %), an app setting; Name is always shown. The
    // number column (FEAT-024) is shown on a numbered map unless hidden (app setting gantt/numberHidden).
    void setColumnShown(int column, bool shown);
    // FEAT-078: keeps No., Name and Extras first after a column was dragged, and saves the order.
    void columnMoved();
    void showColumnMenu(const QPoint &globalPos);
    void updateActions();  // enabled state and tooltips of the action buttons

    QTreeView *table() const { return m_table; }

    QWidget *actionsRow() const { return m_actionsRow; }
    QWidget *baselineLegend() const { return m_legend; }  // FEAT-082: over the chart, while comparing
    // FEAT-078 stage B: the task panel. FEAT-080 (user, 2026-10-29): only the top bar's button opens
    // it (togglePanel, with the current task or none); open, a click on a task shows it on the Task page.
    GanttTaskPanel *panel() const { return m_panel; }
    void openPanel(const QString &id);  // id empty: no task
    void togglePanel();
    void showInPanel(const QString &id);  // only when the panel is open
    // The name on the bars of ids, or of the selected ones (one step; the default is kept as no choice).
    bool setNameOnBar(bool shown, const QStringList &ids = {});
    void closePanel();
    GanttChart *chart() const { return m_chart; }
    QPushButton *addFirstTaskButton() const { return m_addFirst; }
    bool showsEmptyState() const;

signals:
    // A click on the icons of the Extras column: open the topic's web page, or its note next to the icon.
    void webLinkRequested(const QString &id);
    void noteRequested(const QString &id, const QRect &globalAnchor);
    void panelShownChanged(bool shown);
    void resourcesRequested();  // FEAT-081: "Add people…" in the task panel
    void baselineCompared(bool comparing);  // FEAT-082: the top bar's button pressed or not

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void updateChart();
    void refreshLater();  // once, after the current change of the map is complete
    MindNode *node(const QString &id) const;
    void finishMapChange();
    void showRowMenu(const QPoint &globalPos);
    void fitColumns();
    void loadPanel();  // the panel's task again, after a change
    bool editPlan(const std::function<bool(GanttPlan &)> &edit);
    void updateBaselineMenu();
    void setComparedId(const QString &id);

    Document *m_document;
    ShortcutRegistry *m_shortcuts;
    GanttTableModel *m_tableModel;
    QTreeView *m_table;
    GanttChart *m_chart;
    QStackedLayout *m_stack;
    QWidget *m_empty;
    QPushButton *m_addFirst;
    QWidget *m_actionsRow = nullptr;
    QWidget *m_legend = nullptr;
    QList<QToolButton *> m_actionButtons;
    GanttTaskPanel *m_panel = nullptr;
    QString m_panelId;
    QString m_baselineId;     // FEAT-082: the snapshot compared, empty = none
    QString m_baselineSheet;  // openedSheetKey() when the comparison started
    GanttBaselineMenu *m_baselineMenu = nullptr;
    QSet<QString> m_collapsed;  // folded summaries (view state, not saved)
    bool m_numbered = false;      // some topic has a number
    bool m_numberHidden = false;  // number column hidden by the user
    bool m_movingColumns = false;  // columnMoved puts No., Name, Extras back first
    QStringList m_selectionOrder;
    QString m_rangeAnchor;  // the last task clicked without Shift: where a Shift+click range starts
    QString m_menuStyle;
    GanttColors m_colors = GanttColors::light();
    void applyColors();
    bool m_stale = true;
    QString m_openedSheet;  // the sheet last shown: another one opens near its opening day
    QString openedSheetKey() const;  // the document's generation and the sheet's id
    void scrollIfOpened();
    bool m_refreshQueued = false;
    bool m_refreshing = false;
    GanttPlan currentPlan() const;  // the model's (MindMapModel::gantt)  // refresh() restores the selection: its signals do not redo the work
};
