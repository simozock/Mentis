#pragma once
// FEAT-122 stage 0 (docs/features/FEAT-122-gantt-sul-telefono.md): the Gantt chart's drawing and
// geometry without widgets (MentisDomain), shared by the desktop's GanttChart, the exports and the
// phone. The owner scrolls: it gives the canvas its offsets and fonts, and turns the gestures it
// recognises into Gesture. Rules and sizes: FEAT-075..083, docs/design/ui-window.md "Gantt".
#include "ganttplan.h"
#include "nodesketch.h"

#include <QColor>
#include <QCoreApplication>
#include <QDate>
#include <QFont>
#include <QHash>
#include <QLocale>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QRect>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <functional>
#include <optional>

class QPainter;

// Colours of a bar, those of its topic on the map (FEAT-075, user's choice 2026-10-27): border =
// branch colour, fill and text = the topic's (a fill may be transparent, like on the map).
struct GanttBarLook {
    QColor fill;
    QColor border;
    QColor text;
};

// The colours a bar is painted in: its fill, border, done part and name (FEAT-083: the exports
// take the same from GanttCanvas::barPaint).
struct GanttBarPaint {
    QColor fill;
    QColor border;   // the topic's branch colour, or the sheet's: the done part is mixed from it
    QColor outline;  // the colour the border is drawn in (GanttCanvas::outlineColor)
    QColor done;
    QColor text;
};

// Colours of the page around the bars (FEAT-075): light, or the desktop's dark one (GanttColors)
// on a dark map. The task panel, the table and the chart are rounded cards on the page.
struct GanttPalette {
    QColor page, text, muted, grid, line, shade, selected, badge, badgeText, card, headerLine;
    static constexpr int kCardRadius = 10;
    static constexpr int kCardGap = 12;
    static GanttPalette light();
};

// The chart: header with the time scale, bars, milestones, summaries, dependency arrows, the rows
// given by the owner (the table's order).
class GanttCanvas {
    Q_DECLARE_TR_FUNCTIONS(GanttCanvas)
public:
    enum class Scale { Days, Weeks, Months };
    static constexpr int kRowHeight = 32;
    static constexpr int kHeaderHeight = 48;
    // FEAT-078: zoom 50-300 % of the day width of every scale.
    static constexpr int kMinZoom = 50;
    static constexpr int kMaxZoom = 300;
    // User, 2026-10-29: the day shown kOpenDaysBefore days from the left edge, so the days just
    // before it are in view.
    static constexpr int kOpenDaysBefore = 3;
    static constexpr int kDragThreshold = 3;  // px a bar moves before a drag changes the plan
    static constexpr double kBarInset = 4;    // bar = row height - 8

    // A gesture on a bar in progress, drawn as a preview (the plan changes when it ends): the bar
    // moved, its end dragged, its progress, a dependency drawn from one of its circles.
    enum class Drag { None, Move, Resize, Progress, Link };
    struct Gesture {
        QString hovered;  // the bar under the mouse: its circles and progress badge shown
        Drag drag = Drag::None;
        QString id;
        QPointF press;
        QPointF pos;
        bool fromStart = false;  // the link drawn from the start circle
    };

    GanttCanvas();
    // Not copied: the offset callbacks point at the owner.
    GanttCanvas(const GanttCanvas &) = delete;
    GanttCanvas &operator=(const GanttCanvas &) = delete;

    // The owner's scrolling: pixels scrolled sideways and down (rows under the header).
    std::function<int()> horizontalOffset;
    std::function<double()> verticalOffset;
    QFont textFont;    // the bars' text (document font)
    QFont headerFont;  // the time header (interface font, 12 px)
    QColor accent;     // the "New" pill (FEAT-082)
    // The selected bars' ring: the selected nodes' border of Settings (user, 2026-11-18), set by the
    // desktop at each paint; the phone keeps the default, its Theme.selection.
    QColor selectionRing{0x2e, 0xbd, 0xff};
    // FEAT-122 B3: the link circles' distance from the bar's ends and radius, and how far from a circle
    // or an arrow a press still hits it. The desktop's mouse values by default; the phone's fingers
    // need more (MobileGantt).
    struct Reach {
        double handleGap = 8;
        double handleRadius = 4;
        double handle = 7;
        double arrow = 4;
    };
    Reach reach;

    void setData(const GanttPlan &plan, const GanttSchedule &schedule, const QStringList &rows,
                 const QSet<QString> &selected, const QHash<QString, GanttBarLook> &looks = {});
    const GanttPlan &plan() const { return m_plan; }
    const GanttSchedule &schedule() const { return m_schedule; }
    void setSelected(const QSet<QString> &selected) { m_selected = selected; }  // the rows and bars tinted
    const QStringList &rows() const { return m_rows; }
    // From two weeks before the first task (or today) to two months after the last, aligned to a
    // Monday (a month's first day in Months). Returns how many px the dates moved right, so the
    // owner's offset keeps the visible dates where they were.
    int updateRange();
    int contentWidth() const;  // every day of the range
    Scale scale() const { return m_scale; }
    void setScale(Scale scale) { m_scale = scale; }  // then updateRange
    int zoomPercent() const { return m_zoom; }
    void setZoomPercent(int percent) { m_zoom = std::clamp(percent, kMinZoom, kMaxZoom); }
    int dayWidth() const;
    QDate origin() const { return m_origin; }
    int offsetOfDay(const QDate &day) const { return int(m_origin.daysTo(day)) * dayWidth(); }  // the offset that shows it at the left edge
    // Today; the last task's end when the plan ended before today; the first task's start when it
    // has not begun yet (user, 2026-10-30).
    QDate openingDay() const;

    void setColors(const GanttPalette &colors) { m_colors = colors; }
    const GanttPalette &colors() const { return m_colors; }
    // Bars that show their name (the view decides: summaries by default, or the topic's choice).
    void setLabelled(const QSet<QString> &ids) { m_labelled = ids; }
    bool showsName(const QString &id) const { return m_labelled.contains(id); }
    // FEAT-002: the name being typed for one task, drawn in place of its own until the edit ends
    // (user, 2026-11-23: the bar follows the typing, as a topic on the map). An empty id: none.
    // Returns whether anything changed.
    bool setNamePreview(const QString &id, const QString &name);
    QString shownName(const QString &id, const GanttTask &task) const;
    // The critical path (red halo and arrows) can be hidden (user, 2026-10-28).
    void setCriticalShown(bool shown) { m_criticalShown = shown; }
    bool criticalShown() const { return m_criticalShown; }
    // Drawn: shown and not comparing with a snapshot, where red means "later" (user, 2026-10-30).
    bool criticalPainted() const { return m_criticalShown && !m_baseline; }
    // FEAT-082: the snapshot compared (none: nullopt); false when it was already the one shown.
    bool setBaseline(const std::optional<GanttBaseline> &baseline, const GanttBaselineComparison &comparison);
    bool comparing() const { return m_baseline.has_value(); }
    void setPrinting(bool printing) { m_printing = printing; }  // FEAT-083: no card corners
    Gesture &gesture() { return m_gesture; }
    const Gesture &gesture() const { return m_gesture; }

    // Geometry in the owner's viewport coordinates. Empty when the row is not shown.
    double xOf(const QDate &date) const;  // left edge of the day
    QDate dateAt(double x) const;
    int rowAt(double y) const;  // -1 outside the rows
    double rowTop(int row) const;
    QRectF barRect(const QString &id) const;
    QRectF baselineRect(const QString &id) const;      // the snapshot's bar (outline); empty if none
    QRectF baselineMarkRect(const QString &id) const;  // the thin grey bar (a small diamond for milestones)
    // The slip on the thin bar's line, from the snapshot's end to the current one; empty when the
    // end is the same. Coloured red (later) or green (earlier); baselineMarkColor grey otherwise.
    QRectF slipRect(const QString &id) const;
    QColor baselineMarkColor(const QString &id) const;
    QRectF newPillRect(const QString &id) const;  // before the bar of a task not in the snapshot
    QPointF linkHandle(const QString &id) const;         // circle after the bar's end: id comes first
    QPointF linkHandleAtStart(const QString &id) const;  // circle before its start: id comes after
    QPointF progressHandle(const QString &id) const { return progressBadge(id).center(); }
    // Badge with the percentage under the done part (progress < 0: the task's own).
    QRectF progressBadge(const QString &id, int progress = -1) const;
    QPolygonF arrowPath(int dependency) const;  // the dependency's line, from predecessor to arrow tip
    const GanttTask *taskOf(const QString &id) const;  // constant time, unlike GanttPlan::task
    bool isMilestone(const QString &id) const;  // a diamond: a milestone that is not a summary

    // Hit tests, for the owner's input.
    QString barAt(const QPointF &pos) const;  // task whose bar (or milestone) is under pos
    bool onResizeGrip(const QRectF &bar, const QPointF &pos) const;
    int dependencyAt(const QPointF &pos) const;
    // The link circle under pos (end or start) of the bar id; empty if none.
    QString linkHandleAt(const QString &id, const QPointF &pos, bool *atStart = nullptr) const;
    bool linkAllowed(const QString &from, const QString &to) const;
    // The gesture's links drawn onto target: its ends in the plan's order.
    QString linkFrom(const QString &target) const { return m_gesture.fromStart ? target : m_gesture.id; }
    QString linkTo(const QString &target) const { return m_gesture.fromStart ? m_gesture.id : target; }
    // The plan's change for the gesture ended at pos (one history step); empty when nothing changes.
    std::function<bool(GanttPlan &)> gestureEdit(const QPointF &pos) const;

    // Texts: a month's name with a capital in the interface language (BUG-035), the tooltip of a
    // bar and of an arrow (type, and the lag when not 0).
    QString monthName(int month, QLocale::FormatType format) const;
    QString taskTip(const QString &id) const;
    QString dependencyLabel(int dependency) const;
    static QString typeName(GanttDependency::Type type);

    // Paints the chart in area (the owner's viewport rectangle).
    void paint(QPainter &painter, const QRect &area);

    // FEAT-080: a summary drawn thin over its subtasks (every shape but Bar), one closed outline in
    // rect (also the pictures of the panel's Summaries select). The bracket: a band across the top
    // with a tip hanging down at each end; the group frame: its header (the frame is paintFrames').
    static QPainterPath summaryPath(GanttPlan::SummaryShape shape, const QRectF &rect);
    // FEAT-080: a bar's outline in box, grown by grow px with the same angles (selection ring,
    // critical halo); also the pictures of the panel's Tasks select. barTip: the length of the
    // Arrow's and Hexagon's points and of the Parallelogram's slant (0 for the others).
    static QPainterPath taskPath(GanttPlan::BarShape shape, const QRectF &box, qreal grow = 0);
    static double barTip(GanttPlan::BarShape shape, const QRectF &box);
    // FEAT-080: a milestone's outline in its box (diamond: the whole box; the others: the square on
    // its centre as tall as the box), grown by grow px. Also the panel's Milestones select.
    static QPainterPath milestonePath(GanttPlan::MilestoneShape shape, const QRectF &box, qreal grow = 0);
    // FEAT-080: the pen pattern of the sheet's arrows, their thickness (critical ones 0.75 px more)
    // and the length of their end mark, in px (user, 2026-11-01).
    static Qt::PenStyle linkPenStyle(GanttPlan::LinkLine line);
    static qreal linkWidthPx(GanttPlan::LinkWidth width);
    static qreal linkEndSizePx(GanttPlan::LinkEndSize size);
    // FEAT-080: the pen of a bar's or milestone's border (None: Qt::NoPen); flat caps keep the dots apart.
    static QPen borderPen(GanttPlan::BorderStyle style, const QColor &color, qreal width);
    static bool isHandDrawn(GanttPlan::BorderStyle style);  // drawn along NodeSketch::roughOutline
    // Text inside a bar: the name, elided; empty when no letter fits (user, 2026-10-27: an empty
    // bar, not one with "…" alone).
    static QString barLabel(const QString &name, const QFont &font, double width);
    // A bar's colours: the topic's hand colour (look, or null), else the sheet's or the default for
    // its kind (peach tasks, lilac summaries, slate milestones).
    static GanttBarPaint barPaint(const GanttPlan &plan, const QString &id, const GanttBarLook *look, bool summary);
    static QColor defaultBarColor(const GanttPlan &plan, GanttPlan::BarKind kind);
    static QColor barTextColor(const QColor &bar);  // white on dark colours, otherwise a dark shade
    // FEAT-080: the colour a bar's border is drawn in: the task's own, else the sheet's for its kind
    // (user, 2026-11-22), else automatic: the bar's colour made vivid when fill and border match.
    static QColor outlineColor(const GanttPlan &plan, const QString &id, const QColor &fill, const QColor &border);
    // The border colour the Gantt page shows for a kind: the sheet's, or the automatic one of its bars.
    static QColor defaultOutlineColor(const GanttPlan &plan, GanttPlan::BarKind kind);
    // FEAT-080: the colour of the dependency arrows: the sheet's, else the view's grey (critical ones stay red).
    static QColor linkColor(const GanttPlan &plan);
    // The light page's colours of the critical path, the snapshot (the same and earlier) and today.
    static QColor criticalColor();
    static QColor baselineColor();
    static QColor baselineEarlierColor();
    static QColor todayColor();
    // FEAT-082: the "New" pill of the tasks not in the compared snapshot (table and chart).
    static QFont newPillFont(QFont font, int pixels = 11);
    static QSizeF newPillSize(const QFont &font);
    static void paintNewPill(QPainter *painter, const QRectF &rect, const QFont &font, const QColor &accent);

private:
    const GanttBaselineTask *baselineTask(const QString &id) const;
    void paintHeader(QPainter &painter, const QRect &area);
    void paintTask(QPainter &painter, const QString &id, int row);
    void paintBaseline(QPainter &painter, const QString &id, const QColor &border);
    QPainterPath barShape(const QString &id, const QRectF &box, bool milestone) const;  // pill, rectangle, bracket or milestone
    GanttPlan::BarShape barShapeOf(const QString &id) const;  // FEAT-080: the task's own, or the sheet's
    GanttPlan::SummaryShape summaryShapeOf(const QString &id) const;
    GanttPlan::MilestoneShape milestoneShapeOf(const QString &id) const;
    // FEAT-080: the 16 px place of a thin summary shape on its bar's box; a group frame's header
    // reaches out of the summary's days, more for every summary level inside it.
    QRectF summaryPlace(const QString &id, const QRectF &box) const;
    void updateFrames();  // the rows each group frame spans, when the rows change
    void paintFrames(QPainter &painter, int firstRow, int lastRow);  // under the arrows and bars
    // FEAT-080: a bar's or milestone's outline with its border pen; hand-drawn along a rough outline.
    void strokeBorder(QPainter &painter, const QString &id, const QPainterPath &shape, const QPen &pen, bool handDrawn);
    // FEAT-080: a bar's or milestone's shape in the sheet's fill style, as the map's topics (NodeSketch).
    void fillShape(QPainter &painter, const QString &id, const QPainterPath &shape, NodeFillStyle style, const QColor &color);
    void paintArrows(QPainter &painter);
    int barsCrossed(double x, int rowA, int rowB) const;
    int scrollX() const { return horizontalOffset ? horizontalOffset() : 0; }
    double scrollY() const { return verticalOffset ? verticalOffset() : 0; }

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
    GanttPalette m_colors = GanttPalette::light();
    QSet<QString> m_selected;
    QSet<QString> m_labelled;
    QString m_previewId, m_previewName;  // setNamePreview, already as GanttTask::shown
    bool m_criticalShown = true;
    bool m_printing = false;
    std::optional<GanttBaseline> m_baseline;
    QHash<QString, int> m_baselineIndex;
    GanttBaselineComparison m_comparison;
    Scale m_scale = Scale::Days;
    QLocale m_names;  // read once: the language applies from the next start
    int m_zoom = 100;
    QDate m_origin;
    QDate m_last;
    QRect m_area;  // the area being painted
    Gesture m_gesture;
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
