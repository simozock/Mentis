#pragma once

#include "ganttcanvas.h"
#include "ganttcolumns.h"
#include "ganttcommands.h"

#include <QElapsedTimer>
#include <QMap>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QSet>
#include <QTextLayout>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cmath>

class MindMapModel;
class MobileMap;

// FEAT-122 stage A: the active sheet's Gantt on the phone, as the desktop's (FEAT-075..083): a
// frozen Name column (40% of the width, user 2026-11-15) with the shown columns beside it (stage F,
// the desktop's choice and order, GanttColumns) and the chart in the width left, all drawn here,
// the chart by the core's GanttCanvas so it looks as the desktop's. The QML view scrolls it: a
// Flickable gives contentX (the chart's days) and contentY (the rows, both parts), and this item
// asks it to scroll with scrollRequested (opening day, zoom, scale, a new range).
//
// Folding is the view's, as the desktop's table: not saved, every summary open on another sheet.
// The rows are read again when the map's content or selection changes, only while active.
class MobileGantt : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MobileMap *map READ map WRITE setMap NOTIFY mapChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY dataChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY dataChanged)  // no task: "No tasks yet" and Add task
    Q_PROPERTY(int rowCount READ rowCount NOTIFY dataChanged)
    // Days, Weeks, Months (GanttCanvas::Scale); zoom 50-300 % of the day width (FEAT-078), kept
    // while the app runs, 100 % at each start, not saved.
    Q_PROPERTY(int scale READ scale WRITE setScale NOTIFY scaleChanged)
    Q_PROPERTY(int zoomPercent READ zoomPercent NOTIFY zoomPercentChanged)
    // The critical path shown or not: the desktop's top bar button, the same app setting
    // (gantt/critical, shown by default).
    Q_PROPERTY(bool criticalShown READ criticalShown WRITE setCriticalShown NOTIFY criticalShownChanged)
    // The Flickable's scrolling, and the size it scrolls over.
    Q_PROPERTY(qreal contentX READ contentX WRITE setContentX NOTIFY contentXChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY layoutChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY layoutChanged)
    Q_PROPERTY(qreal nameWidth READ nameWidth NOTIFY layoutChanged)
    Q_PROPERTY(qreal tableWidth READ tableWidth NOTIFY layoutChanged)  // the frozen part: the chart starts there
    Q_PROPERTY(qreal nameRight READ nameRight NOTIFY layoutChanged)  // Name's right edge, where its grip is
    // FEAT-122 stage F, the Columns modal: No. then the movable columns in their order,
    // [{field (GanttColumns::Field), title, shown, enabled (No. only on a numbered map), movable}].
    Q_PROPERTY(QVariantList columns READ columns NOTIFY columnsChanged)
    // FEAT-122 stage B1: what the row bar and the row menu can do on the selected tasks (the map's
    // selection, in the order tapped): canIndent, canOutdent, canChain, chainUnlinks, canColor,
    // hasColor (a colour chosen by hand), nameOnBar (the first one's), canRemoveDate, color.
    Q_PROPERTY(QVariantMap actions READ actions NOTIFY actionsChanged)
    // FEAT-122 stage B3: the bar showing its handles (the one task selected, not a summary, with a
    // date), as a picked picture: its link circles, progress badge and end grip.
    Q_PROPERTY(QString handlesId READ handlesId NOTIFY handlesChanged)
    Q_PROPERTY(int maxLag READ maxLag CONSTANT)  // GanttPlan::kMaxLag, for the Lag dialog
    // FEAT-122 stage D, the desktop's baselines (FEAT-082): the sheet's snapshots newest first
    // [{id, name, taken (the interface's short date and time)}], the one compared (a state of the view,
    // not saved: it stops on another sheet or when removed), the names of its tasks removed since,
    // the next default name and whether a snapshot can be taken (the plan has tasks).
    Q_PROPERTY(QVariantList baselines READ baselines NOTIFY baselinesChanged)
    Q_PROPERTY(QString comparedBaseline READ comparedBaseline NOTIFY baselinesChanged)
    Q_PROPERTY(QString comparedName READ comparedName NOTIFY baselinesChanged)
    Q_PROPERTY(QStringList removedTasks READ removedTasks NOTIFY baselinesChanged)
    Q_PROPERTY(QString nextBaselineName READ nextBaselineName NOTIFY baselinesChanged)
    // A row lifted by a long press on the table (the desktop's drag by the name, FEAT-075), the row
    // it would drop on (empty: none, as over its own branch) and where: -1 before, 0 inside, 1 after.
    Q_PROPERTY(QString liftedId READ liftedId NOTIFY rowDragChanged)
    Q_PROPERTY(QString dropId READ dropId NOTIFY rowDragChanged)
    Q_PROPERTY(int dropPlace READ dropPlace NOTIFY rowDragChanged)
    Q_PROPERTY(int headerHeight READ headerHeight CONSTANT)

public:
    static constexpr int kRowHeight = GanttCanvas::kRowHeight;
    static constexpr int kHeaderHeight = GanttCanvas::kHeaderHeight;
    static constexpr int kIndent = 16;      // per level, as the desktop's table
    static constexpr int kSidePadding = 16;  // ui-mobile.md: the 16 px sides
    static constexpr int kArrowWidth = 16;   // the fold arrow ▾ / ▸

    explicit MobileGantt(QQuickItem *parent = nullptr);

    MobileMap *map() const { return m_map; }
    void setMap(MobileMap *map);
    bool active() const { return m_active; }
    void setActive(bool active);
    bool dark() const { return m_dark; }
    bool empty() const { return m_canvas.plan().tasks().isEmpty(); }
    int rowCount() const { return int(m_rows.size()); }
    int scale() const { return int(m_canvas.scale()); }
    void setScale(int scale);
    bool criticalShown() const { return m_canvas.criticalShown(); }
    void setCriticalShown(bool shown);
    int zoomPercent() const { return m_canvas.zoomPercent(); }
    qreal contentX() const { return m_contentX; }
    void setContentX(qreal x);
    qreal contentY() const { return m_contentY; }
    void setContentY(qreal y);
    qreal contentWidth() const;   // the Name column and every day of the chart
    qreal contentHeight() const;  // the header and every row
    // Name keeps its share of the width (40% by default; dragged on the header, user's request
    // 2026-11-22) while the chart keeps kMinChart; then it narrows to kMinName; past that the last
    // columns are cut at the chart's edge (stage F, user 2026-11-16).
    static constexpr int kMinChart = 48;
    static constexpr int kMinName = 96;
    static constexpr int kCellPadding = 8;
    static constexpr qreal kDefaultNameShare = 0.4;
    qreal nameWidth() const { return m_nameWidth; }
    qreal tableWidth() const { return m_tableWidth; }
    qreal nameRight() const;
    // Name's width dragged on the header's grip; `save` (on release) keeps its share in the settings.
    Q_INVOKABLE void setNameWidth(qreal width, bool save);
    // A movable column's width dragged on its grip (user's request 2026-11-23); `save` (on release)
    // keeps it in the desktop's setting (GanttColumns::setWidth). Its right edge for the grip.
    Q_INVOKABLE void setColumnWidth(int field, qreal width, bool save);
    Q_INVOKABLE qreal columnWidth(int field) const;

    QVariantList columns() const;
    // A choice of the app (the desktop's settings), not an Undo step.
    Q_INVOKABLE void setColumnShown(int field, bool shown);
    // The movable column at `from` (in columns' order, No. not counted) to `to`.
    Q_INVOKABLE void moveColumn(int from, int to);
    // For the tests: the shown columns' fields in order, a cell's text and its column's left edge.
    Q_INVOKABLE QVariantList shownFields() const;
    Q_INVOKABLE QString cellText(int row, int field) const;
    Q_INVOKABLE qreal columnLeft(int field) const;

    // A tap at (x, y) in the item: the fold arrow folds or opens the summary, elsewhere on a row
    // (name or bar) its topic is selected, as on the map and in the outline.
    Q_INVOKABLE void tapAt(qreal x, qreal y);
    // Two fingers: the zoom from the pinch's start, the date under anchorX (item x) kept in place.
    Q_INVOKABLE void zoomTo(int percent, qreal anchorX);
    Q_INVOKABLE void scrollToToday();
    // The empty plan's Add task: a main topic, selected (its name is typed in stage B's task panel).
    Q_INVOKABLE QString addTask();

    // Stage B1, the desktop's actions row and row menu (GanttView): each edit one Undo step.
    QVariantMap actions() const;
    // A sibling after the selected task, or its subtask (its summary opened); a main topic when no
    // task is selected. The new topic is selected.
    Q_INVOKABLE QString addRow(bool child);
    Q_INVOKABLE bool indentSelected();
    Q_INVOKABLE bool outdentSelected();
    // Link in a chain / Remove links (FEAT-077), on the tasks in the order they were tapped.
    Q_INVOKABLE bool chainSelected();
    // The bar's colour is the topic's branch colour: it changes the map too (as the desktop).
    Q_INVOKABLE bool setColor(const QColor &color);
    Q_INVOKABLE bool clearColor();
    Q_INVOKABLE bool setNameOnBar(bool shown);
    // FEAT-002 (user, 2026-11-23): the Name typed on the Task page, drawn in the row and on the bar
    // as it is typed (GanttCanvas::setNamePreview); an empty id when the typing ends.
    Q_INVOKABLE void setNamePreview(const QString &id, const QString &name);
    Q_INVOKABLE QString shownName(const QString &id) const;  // as the row and the bar draw it
    // Back to topics without a date (no bar).
    Q_INVOKABLE bool removeDates();

    // Stage B3, the desktop's gestures on the bar with handles (GanttCanvas::Gesture): a press on a link
    // circle draws a dependency, on the badge sets the progress, on the end grip the duration, on the
    // bar moves it. pressAt is false elsewhere: the drag then scrolls. The plan changes when the finger
    // lifts, one Undo step; a lift within the drag distance (a tap) changes nothing.
    QString handlesId() const { return m_handles; }
    static int maxLag() { return GanttPlan::kMaxLag; }
    Q_INVOKABLE bool pressAt(qreal x, qreal y);
    Q_INVOKABLE void dragTo(qreal x, qreal y);
    Q_INVOKABLE bool releaseAt(qreal x, qreal y);
    Q_INVOKABLE void cancelGesture();
    // A double tap on the chart in the row of a task without a date (not a summary) gives it the
    // tapped day as its start; the finger kept down and moved sideways also sets its duration, to the
    // day under it (a milestone keeps only the start). pressAt starts it, dragTo and releaseAt end it,
    // one Undo step. datingId: the task being dated, its preview drawn; empty = none.
    Q_INVOKABLE QString datingId() const { return m_dating; }
    // The arrow's menu (type, Lag…, Delete dependency) on the dependency from -> to.
    Q_INVOKABLE bool setDependency(const QString &from, const QString &to, int type, int lag);
    Q_INVOKABLE bool removeDependency(const QString &from, const QString &to);
    Q_INVOKABLE QStringList dependencyTypes() const;
    // Stage D: taking, renaming and removing a snapshot are one Undo step each (as the desktop);
    // compareBaseline with an empty id stops comparing.
    QVariantList baselines() const { return m_baselines; }
    QString comparedBaseline() const { return m_baselineId; }
    QString comparedName() const;
    QStringList removedTasks() const { return m_comparison.removed; }
    QString nextBaselineName() const { return GanttCommands::nextBaselineName(m_canvas.plan()); }
    Q_INVOKABLE QString addBaseline(const QString &name);  // its id; empty when refused
    Q_INVOKABLE bool renameBaseline(const QString &id, const QString &name);
    Q_INVOKABLE bool removeBaseline(const QString &id);
    Q_INVOKABLE void compareBaseline(const QString &id);
    // For the tests: the chart's comparison marks of task id, in item coordinates.
    Q_INVOKABLE QRectF baselineMarkRect(const QString &id) const;
    Q_INVOKABLE QColor baselineMarkColor(const QString &id) const { return m_canvas.baselineMarkColor(id); }
    Q_INVOKABLE bool isNew(const QString &id) const;

    // The row under (x, y) on the table lifted (its topic selected; not in the multiple selection),
    // the finger followed, then dropped as the desktop's table: over another row's top or bottom 30%
    // before or after it, over its middle as its last subtask; never on the lifted branch. The drop
    // moves the topic on the map too, one Undo step.
    static int headerHeight() { return kHeaderHeight; }
    QString liftedId() const { return m_lifted; }
    QString dropId() const { return m_dropId; }
    int dropPlace() const { return m_dropId.isEmpty() ? 0 : int(m_dropPlace) - 1; }
    Q_INVOKABLE bool liftRow(qreal x, qreal y);
    Q_INVOKABLE void dragRowTo(qreal x, qreal y);
    Q_INVOKABLE bool dropRow();
    Q_INVOKABLE void cancelRowDrag();

    // For the tests: a handle's centre in item coordinates ("end", "start", "progress", "grip", "bar").
    Q_INVOKABLE QPointF handlePoint(const QString &which) const;
    Q_INVOKABLE QPointF arrowPoint(int dependency) const;  // for the tests: on its line, in item coordinates
    // For the tests: the row of topic `id` (-1 when not shown), its bar in item coordinates, folding.
    Q_INVOKABLE int rowOf(const QString &id) const;
    Q_INVOKABLE QRectF barRect(const QString &id) const;
    Q_INVOKABLE bool folded(const QString &id) const { return m_folded.contains(id); }
    Q_INVOKABLE QString rowId(int row) const { return row >= 0 && row < m_rows.size() ? m_rows.at(row).id : QString(); }

    void paint(QPainter *painter) override;

signals:
    void mapChanged();
    void activeChanged();
    void dataChanged();
    void scaleChanged();
    void criticalShownChanged();
    void zoomPercentChanged();
    void contentXChanged();
    void contentYChanged();
    void layoutChanged();
    void actionsChanged();
    void handlesChanged();
    void baselinesChanged();
    void columnsChanged();
    void rowDragChanged();
    void headerTapped();  // a tap on the table's header: the Columns modal
    // A tap on an arrow: {from, to, type, lag}, at (x, y) in the item.
    void dependencyTapped(const QVariantMap &link, qreal x, qreal y);
    void scrollRequested(qreal x);  // the Flickable scrolls the chart to contentX = x
    void scrollTopRequested();      // another sheet: the first rows shown

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void refresh();
    void reload(bool opened);  // opened: another sheet, folding cleared and the opening day shown
    void updateSelection();
    void requestScroll(qreal x);
    void paintNames(QPainter *painter);
    QList<QTextLayout::FormatRange> nameFormats(const QString &id) const;  // none on the name being typed
    QList<MindNode *> selectedTopics() const;  // the selected topics that are rows
    bool finish();  // after an edit: the map's step closed, the selection kept
    void updateHandles();
    QPointF chartPoint(qreal x, qreal y) const { return {x - tableWidth(), y}; }
    void loadCells(const GanttPlan &plan, const GanttSchedule &schedule, const MindMapModel *model);
    void layoutColumns();  // the columns' widths, the Name column's and the frozen part's
    void readColumns();    // the app's choice, read once and again only when changed here
    qreal fieldWidth(GanttColumns::Field field) const;
    void paintCells(QPainter *painter, int row, qreal top);
    void loadBaselines(const GanttPlan &plan, const GanttSchedule &schedule);
    bool editBaselines(const std::function<bool(GanttPlan &)> &change);
    void paintGrip(QPainter *painter);
    void paintRowDrag(QPainter *painter);
    bool startDating(qreal x, qreal y);
    void paintDating(QPainter *painter);
    bool inLiftedBranch(int row) const;
    GanttPalette palette() const;

    QPointer<MobileMap> m_map;
    bool m_active = false;
    bool m_dark = false;
    bool m_openPending = true;  // the opening day shown at the next reload
    GanttCanvas m_canvas;
    QList<GanttCommands::Row> m_rows;
    // FEAT-124: the names' formats (GanttNames::ranges), read when the plan is loaded.
    QHash<QString, QList<QTextLayout::FormatRange>> m_nameFormats;
    QString m_namePreviewId;  // setNamePreview
    QSet<QString> m_folded;
    QSet<QString> m_selected;
    qreal m_contentX = 0;
    qreal m_contentY = 0;
    QString m_handles;
    QString m_baselineId;  // the snapshot compared, empty = none
    GanttBaselineComparison m_comparison;
    QVariantList m_baselines;
    bool m_numbered = false;                  // the map is numbered: No. applies
    QVector<GanttColumns::Field> m_choice;    // the app's shown columns, No. included when not hidden
    QVariantList m_columns;                   // the Columns modal's rows
    QVector<GanttColumns::Field> m_fields;    // the shown columns, Name among them
    QVector<qreal> m_lefts;                   // each shown column's left edge
    QVector<QStringList> m_cells;             // per row, the shown columns' texts (Name's empty)
    qreal m_numberWidth = 0;                  // No.: its widest number
    QMap<GanttColumns::Field, qreal> m_widths;   // the movable columns' widths, read with the choice
    qreal m_nameWidth = 0;
    qreal m_nameShare = kDefaultNameShare;
    qreal m_tableWidth = 0;
    // The last tap on the chart, for the double tap that dates a task.
    QString m_tapId;
    QPointF m_tapPos;
    QElapsedTimer m_tapTime;
    QString m_dating;   // the task dated by a double tap, empty = none
    QDate m_datingFrom; // the day pressed
    QDate m_datingTo;   // the day under the finger
    qreal m_datingX = 0; // where the finger pressed, in the item
    QString m_lifted;   // the row lifted by a long press, empty = none
    qreal m_liftGrab = 0;  // where the finger took it, from the row's top
    qreal m_dragY = 0;     // the finger, in the item
    QString m_dropId;
    GanttCommands::DropPlace m_dropPlace = GanttCommands::DropPlace::Inside;
};
