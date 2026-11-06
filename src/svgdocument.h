#pragma once

// FEAT-070: an SVG sticker edited as XML (docs/features/FEAT-070-editor-svg.md). No widgets; the
// XML is read and rewritten with Qt Core's stream classes (no Qt Xml module).
// Stage 1: the colours of the file, found and replaced everywhere they are written.
// Stage 2: the frame (viewBox), a rotation and flips of the drawing, one text label on top. The
// drawing goes into <g id="mentis-content"> with the transform; its values are kept in data-mentis-*
// attributes so a file saved by the editor opens again with them. The label is
// <text id="mentis-label">, outside the group: it does not turn with the drawing.

#include <QByteArray>
#include <QColor>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTransform>

class SvgDocument {
public:
    struct Transform {
        qreal rotation = 0;  // degrees, clockwise, around the pivot
        bool flipH = false;
        bool flipV = false;
        bool operator==(const Transform &) const = default;
    };
    struct Label {
        QString text;              // empty: no label
        QString family;
        qreal size = 0;            // font size in viewBox units
        QColor color = Qt::black;
        bool bold = false;
        QPointF position;          // centre of the baseline, in viewBox units
        bool operator==(const Label &) const = default;
    };

    // False when the data is not well-formed XML with an <svg> root.
    bool load(const QByteArray &data);
    QByteArray data() const { return m_data; }

    // Distinct colours in order of first appearance: the presentation attributes fill, stroke,
    // stop-color, flood-color, lighting-color and color, the same properties in style="..." and in
    // <style> sheets. Black is added when a shape has no fill of its own nor inherited (SVG's
    // default fill). Equal colours written differently (#fff, white) count once.
    QList<QColor> colors() const { return m_colors; }
    // Every use of from becomes to; an alpha below 255 in the original is kept.
    void replaceColor(const QColor &from, const QColor &to);

    // The visible area: the root's viewBox, else 0 0 width height.
    QRectF viewBox() const { return m_viewBox; }
    // Also writes width and height with the viewBox's proportions (longer side kStickerPx).
    void setViewBox(const QRectF &box);
    Transform transform() const { return m_transform; }
    // The frame and the label follow the drawing: the viewBox becomes the bounding box of the old
    // one moved by the change (exact for quarter turns and flips, larger for other angles).
    void setTransform(const Transform &transform);
    Label label() const { return m_label; }
    void setLabel(const Label &label);
    // Bounds of what is drawn (transparent pixels excluded), in viewBox units, looked for in an
    // area three times the viewBox; empty when nothing is drawn.
    QRectF drawingBounds() const;
    // Bounds of the label's text in viewBox units (empty without a label).
    QRectF labelBounds() const;

    static constexpr qreal kStickerPx = 256;  // width/height written with a new viewBox

    // Stage 3 (svgshapes.cpp): the drawn shapes (path, rect, circle, ellipse, polygon, polyline,
    // line) outside defs, clipPath, mask..., each known by its id. Coordinates are viewBox units
    // unless said otherwise.
    struct Segment {
        bool curve = false;  // a cubic Bézier with c1 and c2, else a straight line
        QPointF c1, c2, end;
    };
    struct SubPath {
        QPointF start;
        QList<Segment> segments;
        bool closed = false;
    };
    struct Shape {
        QString id;
        QPainterPath outline;  // as drawn: the shape's transform and its groups' applied
        bool filled = true;    // false with fill="none": only the outline is hit
    };
    // Gives every drawn shape without an id (or with a repeated one) an id "mentis-shape-N".
    void ensureShapeIds();
    QList<Shape> drawnShapes() const;  // in paint order; read once, then asked many times
    // The topmost shape under the point (its fill, or its outline within tolerance), else empty.
    static QString shapeAt(const QList<Shape> &shapes, const QPointF &point, qreal tolerance);
    static QRectF bounds(const QList<Shape> &shapes, const QStringList &ids);
    // Moves, scales... the shapes by matrix (viewBox units), written as each shape's transform.
    void transformShapes(const QStringList &ids, const QTransform &matrix);
    void removeShapes(const QStringList &ids);
    // In the paint order of the whole drawing, across its groups: a shape that changes group keeps
    // its look (its transform and the properties it inherited). Front stays below the label.
    enum class Order { Forward, Backward, Front, Back };
    void reorderShapes(const QStringList &ids, Order order);
    // A shape's geometry as sub-paths of lines and cubics, in its own coordinates; shapeMatrix
    // maps them to viewBox units. setPath writes it as a <path> (a rect, circle... becomes one).
    QList<SubPath> path(const QString &id) const;
    QTransform shapeMatrix(const QString &id) const;
    void setPath(const QString &id, const QList<SubPath> &path);
    // Stage 4: new shapes and the style of shapes. An invalid colour means none.
    struct ShapeStyle {
        QColor fill;
        QColor stroke;
        qreal strokeWidth = 0;  // viewBox units
        bool operator==(const ShapeStyle &) const = default;
    };
    // A new <path> on top of the drawing (below the label), with its style; its id.
    QString addPath(const QList<SubPath> &path, const ShapeStyle &style);
    enum StyleParts { Fill = 1, Stroke = 2, StrokeWidth = 4, AllStyle = 7 };
    // The parts given, written in each shape's style attribute (it wins over the shape's attributes
    // and the style sheets); the others stay as they are (a gradient fill when only the stroke changes).
    void setShapeStyle(const QStringList &ids, const ShapeStyle &style, int parts);
    // The shape's own fill, stroke and width (its style attribute, else its attributes; inherited
    // and style sheet values are not looked for). Unset fill is black, as SVG draws it.
    ShapeStyle shapeStyle(const QString &id) const;
    static QList<SubPath> subPaths(const QPainterPath &path);
    // A colour as SVG writes it (#rgb, rgb(), names...; invalid for none, url()...), and back.
    static QColor parseColor(const QString &text);
    static QString colorText(const QColor &color);

    // For a drag: rest without the elements ids (shapes or the label), only with just them (and
    // their groups, defs and styles). The preview draws rest once and redraws only the small part.
    void split(const QStringList &ids, SvgDocument *rest, SvgDocument *only) const;

private:
    struct Edit;
    // One pass over the XML: reads colours, frame and label; with edit and out, writes the new data.
    bool scan(const QByteArray &data, const Edit *edit, QByteArray *out);
    void rewrite(const Edit &edit);
    // New data written by the shape edits (svgshapes.cpp), read again.
    void replaceData(const QByteArray &data);

    QByteArray m_data;
    QList<QColor> m_colors;
    QRectF m_viewBox;
    Transform m_transform;
    QPointF m_pivot;     // centre of the rotation; the viewBox's centre when the group is made
    bool m_hasGroup = false;
    Label m_label;
};
