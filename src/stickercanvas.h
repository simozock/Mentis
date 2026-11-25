#pragma once

// FEAT-070 (docs/features/FEAT-070-editor-svg.md): the sticker editor's preview without a widget, so
// the desktop (StickerPreview, Qt Widgets) and the phone (StickerCanvasItem, Qt Quick) share its
// gestures and its drawing. Positions are in the view's logical pixels; the owner forwards the
// pointer and the keys and repaints on updateRequested.
//
// Two modes for the pointer, plus the drawing tools:
// - Shapes (stage 3): a press selects the shape under it (inside groups too), Shift adds or removes,
//   a drag on the empty area selects with a rectangle; the selection moves by dragging, resizes by
//   its 8 handles (Shift keeps the proportions), moves with the arrows (Shift: more), Delete removes
//   it. A double click edits the shape's points: drag points and Bézier handles, a click on the
//   outline adds a point, Delete removes the chosen point, Esc or a click on the empty area ends.
// - Frame (stage 2): the frame's edges and corners are dragged.
// - Rectangle ... Star (stage 4): a drag draws a shape; the Pen adds corners and curves.
// The label is dragged in both. Every change is reported once, on release; while dragging only the
// preview moves.

#include "svgdocument.h"

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QSizeF>
#include <QStringList>
#include <QTransform>

#include <functional>
#include <memory>

class QPainter;
class QSvgRenderer;
class QTimer;

class StickerCanvas {
public:
    // Shapes selects and edits; Frame crops; the others draw a new shape (stage 4).
    enum class Mode { Shapes, Frame, Rectangle, Ellipse, Line, Pen, Polygon, Star };
    static bool draws(Mode mode) { return mode != Mode::Shapes && mode != Mode::Frame; }

    // Sizes in logical pixels: the desktop's for a mouse; the phone sets larger reaches for a finger.
    struct Metrics {
        qreal handle = 8;   // the drawn handles and points
        qreal reach = 8;    // how near a frame edge or a selection handle must be
        qreal pick = 4;     // how near an outline, a point or a handle must be
        qreal inset = 16;   // room between the view's edge and the area shown
    };
    struct Colors {
        QColor raised, sunken;  // the checkerboard
        QColor surface;         // the veil outside the frame, the hollow handles
        QColor accent;
    };

    StickerCanvas();
    ~StickerCanvas();
    StickerCanvas(const StickerCanvas &) = delete;
    StickerCanvas &operator=(const StickerCanvas &) = delete;

    void setViewSize(const QSizeF &size);
    QSizeF viewSize() const { return m_size; }
    void setDevicePixelRatio(qreal ratio);
    void setMetrics(const Metrics &metrics) { m_metrics = metrics; }

    void setDocument(const SvgDocument &document);
    bool isValid() const;
    void setMode(Mode mode);
    Mode mode() const { return m_mode; }
    QStringList selection() const { return m_selection; }
    void setSelection(const QStringList &ids);
    QString editedShape() const { return m_pointShape; }  // the shape whose points are edited
    void editPoints(const QString &id);                  // empty: stop
    bool hasChosenPoint() const { return m_chosen.valid() && m_chosen.handle == 0; }
    void removeChosenPoint();
    // Arrow moves wait for a pause to become one step; this writes them now (before any other change).
    void flush();

    // The tools' options: rounded corners (viewBox units), the polygon's sides, the star's points.
    void setToolOptions(qreal cornerRadius, int sides, int points);
    bool penActive() const { return m_penActive; }
    bool penHasSegments() const { return m_penActive && !m_pen.segments.isEmpty(); }
    void finishPen(bool close = false);  // the pen's path as it is (two points at least)
    void cancelPen();
    void removeLastPenPoint();  // the last point, or the whole path when only its start is left

    // View <-> viewBox units, for the area shown now (also for tests).
    QPointF toView(const QPointF &point) const;
    QPointF toDocument(const QPointF &point) const;

    // The pointer. Shift is the keyboard's modifier, or the phone's Shift toggle.
    void press(const QPointF &position, bool shift);
    void doubleClick(const QPointF &position);
    void move(const QPointF &position, bool shift);  // also without a button: the pen's next segment
    void release(const QPointF &position, bool shift);
    bool dragging() const { return m_drag != Drag::None; }
    // The cursor for the position while no button is down (Qt::ArrowCursor: none of its own).
    Qt::CursorShape cursorAt(const QPointF &position) const;
    // Keys: true when the canvas wants key (ShortcutOverride) or used it.
    bool claimsKey(int key) const;
    bool key(int key, bool shift);

    void paint(QPainter *painter, const Colors &colors) const;

    std::function<void()> updateRequested;
    std::function<void(const QRectF &viewBox)> frameChanged;
    std::function<void(const QPointF &position)> labelMoved;
    // nudge: an arrow key, merged with the next ones into one step.
    std::function<void(const QStringList &ids, const QTransform &matrix, bool nudge)> shapesTransformed;
    std::function<void(const QString &id, const QList<SvgDocument::SubPath> &path)> pathChanged;
    std::function<void(const QStringList &ids)> removeRequested;
    std::function<void()> selectionChanged;
    // A shape drawn with a tool, in viewBox units.
    std::function<void(const QList<SvgDocument::SubPath> &path)> shapeDrawn;
    // The pen started or ended, the chosen point or the edited shape changed (buttons follow them).
    std::function<void()> stateChanged;

    static constexpr int kNudgePauseMs = 400;  // arrow moves within this pause make one step

private:
    enum Edge { None = 0, Left = 1, Top = 2, Right = 4, Bottom = 8 };
    enum class Drag { None, FrameEdges, Label, Move, Resize, Rubber, Point, Draw, PenHandle };
    // A point of the edited path: its node, or one of the Bézier handles beside it.
    struct PointRef {
        int sub = -1;
        int node = -1;   // 0 = the sub-path's start, k = the end of segment k-1
        int handle = 0;  // 0 the node, -1 its incoming handle (c2 of segment node-1), +1 outgoing (c1 of segment node)
        bool valid() const { return sub >= 0; }
        bool operator==(const PointRef &) const = default;
    };
    void requestUpdate() const;
    void notifyState() const;
    qreal tolerance() const;  // kPick in viewBox units
    int edgesAt(const QRectF &box, const QPointF &position) const;    // edges of box (viewBox units) under the pointer
    int handlesAt(const QRectF &box, const QPointF &position) const;  // the edges of the handle under the pointer
    bool onLabel(const QPointF &position) const;
    PointRef pointAt(const QPointF &position) const;
    QPointF pointPosition(const QList<SvgDocument::SubPath> &path, const PointRef &ref) const;  // shape coordinates
    void movePoint(QList<SvgDocument::SubPath> &path, const PointRef &ref, const QPointF &to) const;
    bool insertPointAt(const QPointF &position);
    QTransform resizeMatrix(const QPointF &at, bool keepRatio) const;
    QPainterPath toolShape(const QPointF &from, const QPointF &to, bool constrained) const;  // the drawn shape
    QPointF snapped(const QPointF &from, const QPointF &to, bool constrained) const;      // 45° with Shift
    QPainterPath penOutline(bool withRubber) const;
    void updateArea();
    const QList<SvgDocument::Shape> &shapes() const;  // read on first need after each change
    void render(QSvgRenderer *renderer, QImage *image) const;
    // Drag layers: rest drawn once, the moving elements redrawn alone (a 2 MB sticker stays smooth).
    void beginLayers(const QStringList &ids);
    void endLayers();
    void showOnly(const SvgDocument &only);

    SvgDocument m_document;
    mutable QList<SvgDocument::Shape> m_shapes;
    mutable bool m_shapesRead = false;
    std::unique_ptr<QSvgRenderer> m_renderer;
    QImage m_image;  // m_document drawn for m_area: painted many times, drawn once per change
    bool m_layers = false;
    QImage m_restImage;
    SvgDocument m_only;  // the moving elements alone
    SvgDocument m_onlyLive;
    std::unique_ptr<QSvgRenderer> m_onlyRenderer;
    QTransform m_nudge;  // arrow moves not written yet
    std::unique_ptr<QTimer> m_nudgeTimer;
    QSizeF m_size;
    qreal m_ratio = 1;
    Metrics m_metrics;
    Mode m_mode = Mode::Shapes;
    QRectF m_area;   // viewBox units shown in the view
    QRectF m_frame;  // the viewBox, or its dragged version
    Drag m_drag = Drag::None;
    int m_edges = None;
    QPointF m_pressAt;      // viewBox units
    QPointF m_labelAt;
    QTransform m_dragMatrix;
    QRectF m_rubber;        // viewBox units
    QStringList m_selection;
    QStringList m_selectionAtPress;
    QRectF m_selectionBox;  // at the press
    QString m_pointShape;
    QList<SvgDocument::SubPath> m_path;     // the edited shape's points, in its own coordinates
    QList<SvgDocument::SubPath> m_livePath;
    QTransform m_pathMatrix;                // shape coordinates -> viewBox units
    PointRef m_chosen;
    bool m_clickedSelected = false;         // pressed on a shape already selected (Shift+click removes it on release)
    QPainterPath m_drawing;                 // the shape being drawn, viewBox units
    qreal m_cornerRadius = 0;
    int m_sides = 5;
    int m_points = 5;
    // The pen: the path so far, the outgoing handle of its last point, the pointer for the next segment.
    bool m_penActive = false;
    SvgDocument::SubPath m_pen;
    QPointF m_penOut;
    bool m_hasPenOut = false;
    QPointF m_penMouse;
};
