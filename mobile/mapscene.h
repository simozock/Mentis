#pragma once

#include "mapsnapshot.h"

#include <QColor>
#include <QString>
#include <QList>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QSGGeometry>

struct MindNode;
class QSGTextNode;
class QQuickWindow;
class QSGGeometryNode;
class QSGNode;
class QSGOpacityNode;

// FEAT-097 stage A: the map as Qt Quick scene graph nodes, built once when the sheet is opened.
// Pan and zoom only change the transform above them, so a frame redraws the same vertices on the
// GPU instead of painting every curve and title again with QPainter (the measured bottleneck of
// the QQuickPaintedItem canvas, FEAT-097 "Emulator results").
//
// Lines and shapes are triangles with a colour per vertex (QSGVertexColorMaterial), in a few
// geometry nodes of at most kMaxNodeVertices each, so the renderer draws them in a few batches; their edges are smoothed by the
// window's multisampling (main.cpp). Titles are QSGTextNode (distance-field glyphs, sharp at any
// zoom: https://doc.qt.io/qt-6/qsgtextnode.html).
//
// Pictures (images and stickers), markers, the note's and the web link's buttons, the checkbox and
// (FEAT-114) the paper clip are image nodes, decoded or drawn once and kept in a cache.
//
// FEAT-114: tag pills as the shapes (triangles), their texts with the titles.
//
// FEAT-115: the boundaries under the branch lines (BoundaryItem::paint: fill, border with its
// pattern, title tab), their titles with the topics' titles. Step 2: the summaries' brackets with
// the branch lines; their topics are topics.
//
// Prototype limits as before: plain fills, solid borders (no dash), titles, one curve per line; no
// picture borders and shadows, patterns, inner lines or marks. FEAT-116: formulas are drawn, a title's
// as an image with the titles, a formula content as a picture.
// FEAT-110: relationships are drawn as on the desktop (RelationshipItem::paint): the line with its
// pattern, its end marks, the label on its backdrop; above the topics' shapes and pictures, under
// the titles (the desktop draws them above the titles too).
namespace MapScene {

using Vertices = QList<QSGGeometry::ColoredPoint2D>;

// Curves are flattened at this scale of map px, so they stay smooth up to the 400% zoom.
constexpr qreal kFlattenScale = 4;
// FEAT-051: a marker is drawn 18 px wide inside its place (NodeItem::paint), from an image made for
// this zoom times the screen's pixel ratio.
constexpr qreal kMarkerDrawPx = 18;
constexpr qreal kMarkerSharpZoom = 2;
// B3: the note's button, as NodeItem's link buttons (kLinkIconPx, kLinkButtonRadiusPx): a 16 px icon
// inside the 22 px square (NodeGeometry::kLinkButtonPx) with radius 4, sharp up to kMarkerSharpZoom.
constexpr qreal kNoteIconPx = 16;
constexpr qreal kNoteButtonRadiusPx = 4;
// FEAT-108: the checkbox inside its 22 px place, as NodeItem::kTaskBoxPx.
constexpr qreal kTaskBoxPx = 16;
// FEAT-114: the files' count pill reaches this far past the paper clip's right and bottom edges
// (NodeItem::paint: 4 px).
constexpr qreal kFilesBadgeOverflowPx = 4;

// Vertices of one geometry node. The renderer merges geometry without indices into batches with
// 16-bit indices ("Batching only works for 16-bit indices",
// https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph-renderer.html): on a Fairphone 6 the
// lines past the 65,536th vertex of a 146,514-vertex node were not drawn (FEAT-097, 2026-11-10).
// A multiple of 3, so no triangle is split between two nodes.
constexpr int kMaxNodeVertices = 65535;
// FEAT-114: MindMapScene::setLabelHighlight's 20%.
constexpr qreal kFadedOpacity = 0.2;

// The triangles as geometry nodes (vertex colour material) of at most kMaxNodeVertices each,
// in order; owned by the caller.
QList<QSGGeometryNode *> geometryNodes(const Vertices &vertices);

// The closed polygons of a path's outline and the open polylines of its strokes, in map px.
QList<QPolygonF> fillPolygons(const QPainterPath &path);
QList<QPolygonF> strokePolylines(const QPainterPath &path);

// Appends the triangles that fill a simple polygon (ear clipping; a polygon that cannot be cut is
// filled as a fan from its first point).
void appendFill(Vertices &out, const QPolygonF &polygon, const QColor &color);
// Appends the triangles of a stroke `width` wide along a polyline: mitred joins (limited to twice
// the half width), round caps on open lines when `roundCaps`.
void appendStroke(Vertices &out, const QPolygonF &polyline, qreal width, bool closed, bool roundCaps, const QColor &color);
// FEAT-110: the dashes of a polyline drawn with a pen style `width` wide, as QPen's dash pattern
// (units of the width, https://doc.qt.io/qt-6/qpen.html#dashPattern); the whole line when solid,
// none for Qt::NoPen.
QList<QPolygonF> dashes(const QPolygonF &polyline, int penStyle, qreal width);
// FEAT-115: the same for a pen with its own dash pattern (QPen::dashPattern, in units of its width).
QList<QPolygonF> dashes(const QPolygonF &polyline, const QPen &pen);
// FEAT-115: a boundary's fill, border and (withTab) title tab, at `opacity` (a tag's highlight).
void appendBoundary(Vertices &out, const MapSnapshot::BoundaryShape &shape, bool withTab = true, qreal opacity = 1);
// FEAT-110: a relationship's line, end marks and (withLabel) label backdrop, at `opacity` (FEAT-114); and a label's text
// node (owned by the caller), `text` drawn in `color` at the middle of `box`.
void appendRelation(Vertices &out, const MapSnapshot::Relation &relation, bool withLabel = true, qreal opacity = 1);
QSGTextNode *labelNode(QQuickWindow *window, const QFont &font, const QString &text, const QColor &color, const QRectF &box);

struct Nodes {
    QSGNode *content = nullptr;      // lines, shapes, pictures, then titles; owned by the caller
    QSGOpacityNode *text = nullptr;  // the titles, inside content: hidden below the detail zoom
    int vertexCount = 0;
};
// The whole sheet in map px. The title of `withoutTitle` is left out: the page's field draws it
// while it is edited, as the label of the relationship `withoutLabel` (FEAT-110) or the title tab
// of the boundary with that id (FEAT-115). The relationship
// `withoutRelation` is left out whole while a handle bends it (MapCanvas draws it). With `onlyTopic`,
// that topic alone without lines (the copy dragged in B2). FEAT-114: with `highlight`, the desktop's
// Highlight related topics (MindMapScene::setLabelHighlight): the topics without that tag, the lines
// and the relationships drawn at kFadedOpacity.
Nodes build(const MapSnapshot &snapshot, QQuickWindow *window, const MindNode *withoutTitle = nullptr,
            const MindNode *onlyTopic = nullptr, const QString &withoutLabel = QString(),
            const QString &withoutRelation = QString(), const QString &highlight = QString());

}  // namespace MapScene
