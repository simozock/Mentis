#pragma once
// FEAT-075 (docs/features/FEAT-075-gantt-foglio-e-vista.md): the Gantt view of the active sheet. Task
// table on the left, time chart on the right. The plan is read from the sheet's model and changed
// only through Document::editGantt, one history step per edit.
#include "datepicker.h"
#include "ganttcanvas.h"
#include "ganttcommands.h"
#include "ganttplan.h"

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

// The page's colours (GanttPalette) with the desktop's dark page, on the app's dark tokens like the
// outliner (FEAT-075, user's request 2026-10-27). Never the map's own colour.
struct GanttColors : GanttPalette {
    static GanttColors light();
    static GanttColors dark();
    DatePicker::Colors picker() const;  // the calendar on the Gantt's cards
};

// The chart widget: scrolls and turns the mouse into the canvas's gestures; GanttCanvas (core) lays
// out and paints. Rows follow the table (same order, height and vertical scroll).
class GanttChart final : public QAbstractScrollArea {
    Q_OBJECT
public:
    using Scale = GanttCanvas::Scale;
    static constexpr int kRowHeight = GanttCanvas::kRowHeight;
    static constexpr int kHeaderHeight = GanttCanvas::kHeaderHeight;
    static constexpr int kMinZoom = GanttCanvas::kMinZoom;
    static constexpr int kMaxZoom = GanttCanvas::kMaxZoom;
    static constexpr int kOpenDaysBefore = GanttCanvas::kOpenDaysBefore;

    explicit GanttChart(QWidget *parent = nullptr);

    // Edits go through this function (Document::editGantt); vertical offset of the table rows.
    std::function<bool(const std::function<bool(GanttPlan &)> &)> edit;
    std::function<int()> verticalOffset;
    QString menuStyle;  // style sheet of the context menus

    GanttCanvas &canvas() { return m_canvas; }
    const GanttCanvas &canvas() const { return m_canvas; }
    void setData(const GanttPlan &plan, const GanttSchedule &schedule, const QStringList &rows,
                 const QSet<QString> &selected, const QHash<QString, GanttBarLook> &looks = {});
    Scale scale() const { return m_canvas.scale(); }
    void setScale(Scale scale);
    void setLabelled(const QSet<QString> &ids) { m_canvas.setLabelled(ids); viewport()->update(); }
    void setNamePreview(const QString &id, const QString &name) { if (m_canvas.setNamePreview(id, name)) viewport()->update(); }
    bool showsName(const QString &id) const { return m_canvas.showsName(id); }
    // The critical path (app setting gantt/critical).
    void setCriticalShown(bool shown) { m_canvas.setCriticalShown(shown); viewport()->update(); }
    bool criticalShown() const { return m_canvas.criticalShown(); }
    bool criticalPainted() const { return m_canvas.criticalPainted(); }
    // FEAT-082: the snapshot compared (none: nullopt).
    void setBaseline(const std::optional<GanttBaseline> &baseline, const GanttBaselineComparison &comparison);
    bool comparing() const { return m_canvas.comparing(); }
    QRectF baselineRect(const QString &id) const { return m_canvas.baselineRect(id); }
    QRectF baselineMarkRect(const QString &id) const { return m_canvas.baselineMarkRect(id); }
    QRectF slipRect(const QString &id) const { return m_canvas.slipRect(id); }
    QColor baselineMarkColor(const QString &id) const { return m_canvas.baselineMarkColor(id); }
    QRectF newPillRect(const QString &id) const { return m_canvas.newPillRect(id); }
    // FEAT-078: zoom 50-300 % of the day width of every scale; the date at anchorX stays in place.
    int zoomPercent() const { return m_canvas.zoomPercent(); }
    void setZoomPercent(int percent, double anchorX = 0);
    // Go to today shows today; a sheet opened shows openingDay(), kOpenDaysBefore days from the edge.
    void scrollToDay(const QDate &day);
    // FEAT-083 (export): day at the left edge.
    void setFirstDay(const QDate &day);
    void scrollToToday() { scrollToDay(GanttToday::date()); }
    QDate openingDay() const { return m_canvas.openingDay(); }
    void scrollToOpeningDay() { scrollToDay(openingDay()); }
    double xOf(const QDate &date) const { return m_canvas.xOf(date); }  // left edge of the day, viewport x
    void setColors(const GanttPalette &colors) { m_canvas.setColors(colors); viewport()->update(); }
    // Geometry in viewport coordinates, for the view and the tests (GanttCanvas).
    QRectF barRect(const QString &id) const { return m_canvas.barRect(id); }
    QPointF linkHandle(const QString &id) const { return m_canvas.linkHandle(id); }
    QPointF linkHandleAtStart(const QString &id) const { return m_canvas.linkHandleAtStart(id); }
    QPointF progressHandle(const QString &id) const { return m_canvas.progressHandle(id); }
    QPolygonF arrowPath(int dependency) const { return m_canvas.arrowPath(dependency); }
    int dayWidth() const { return m_canvas.dayWidth(); }
    QDate origin() const { return m_canvas.origin(); }
    // FEAT-077: the arrow's menu: the four types (the current one checked), Lag…, Delete dependency.
    // Choosing an entry edits the plan (one step, none when the value is the same).
    QMenu *dependencyMenu(int dependency, QWidget *parent);
    bool setDependency(const GanttDependency &dependency);  // type and lag of an existing pair
    QString dependencyLabel(int dependency) const { return m_canvas.dependencyLabel(dependency); }
    QString monthName(int month, QLocale::FormatType format) const { return m_canvas.monthName(month, format); }
    // The canvas's rules, under the names the tests use.
    static QString barLabel(const QString &name, const QFont &font, double width) { return GanttCanvas::barLabel(name, font, width); }
    static QPainterPath taskPath(GanttPlan::BarShape shape, const QRectF &box, qreal grow = 0) { return GanttCanvas::taskPath(shape, box, grow); }
    static QPainterPath summaryPath(GanttPlan::SummaryShape shape, const QRectF &rect) { return GanttCanvas::summaryPath(shape, rect); }
    static GanttBarPaint barPaint(const GanttPlan &plan, const QString &id, const GanttBarLook *look, bool summary) {
        return GanttCanvas::barPaint(plan, id, look, summary);
    }

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
    using Drag = GanttCanvas::Drag;
    void updateRange();
    void updateScrollBars();

    GanttCanvas m_canvas;
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
    using DropPlace = GanttCommands::DropPlace;
    bool moveTask(const QString &id, const QString &target, DropPlace place);
    bool removeSelected();
    bool removeTasks(const QStringList &ids);  // their topics, map too
    bool renameTask(const QString &id, const QString &name, const QString &html);
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
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void updateChart();
    void refreshLater();  // once, after the current change of the map is complete
    MindNode *node(const QString &id) const;
    void finishMapChange();
    void showRowMenu(const QPoint &globalPos);
    void fitColumns();
    void loadPanel();  // the panel's task again, after a change
    // FEAT-002 (user, 2026-11-23): the name typed in the table or in the panel shows at once on the
    // bar, in the row and in the other field, as a topic's title on the map; the task keeps its own
    // name until the typing is confirmed. endNamePreview: the field closed (confirmed or not).
    void previewName(const QString &id, const QString &name, bool fromPanel);
    void endNamePreview();
    QString m_namePreviewId;
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
    // The task panel's dragged width (user's request 2026-11-22): at most half the view.
    int panelWidthFor(int wanted) const;
    void applyPanelWidth();
    int m_panelDraggedWidth = 0;  // read from the settings once
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
