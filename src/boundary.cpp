#include "boundary.h"

#include <QLineF>
#include <QPolygonF>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace Boundaries {

namespace {

std::optional<QColor> colorFromJson(const QJsonValue &value) {
    const QColor color(value.toString());
    return color.isValid() ? std::optional<QColor>(color) : std::nullopt;
}

}  // namespace

namespace {

constexpr std::array kShapes{Boundary::Shape::RoundedRectangle, Boundary::Shape::Rectangle, Boundary::Shape::Waves,
                             Boundary::Shape::Scallops,         Boundary::Shape::Tension,   Boundary::Shape::Polygon,
                             Boundary::Shape::Ellipse,          Boundary::Shape::Corners,   Boundary::Shape::Tag,
                             Boundary::Shape::RoundedLeft,      Boundary::Shape::Frame};

}  // namespace

QString shapeKey(Boundary::Shape shape) {
    switch (shape) {
    case Boundary::Shape::Rectangle: return QStringLiteral("rectangle");
    case Boundary::Shape::Waves: return QStringLiteral("waves");
    case Boundary::Shape::Scallops: return QStringLiteral("scallops");
    case Boundary::Shape::Tension: return QStringLiteral("tension");
    case Boundary::Shape::Polygon: return QStringLiteral("polygon");
    case Boundary::Shape::Ellipse: return QStringLiteral("ellipse");
    case Boundary::Shape::Corners: return QStringLiteral("corners");
    case Boundary::Shape::Tag: return QStringLiteral("tag");
    case Boundary::Shape::RoundedLeft: return QStringLiteral("roundedLeft");
    case Boundary::Shape::Frame: return QStringLiteral("frame");
    case Boundary::Shape::RoundedRectangle: break;
    }
    return QStringLiteral("rounded");
}

std::optional<Boundary::Shape> shapeFromKey(const QString &key) {
    for (auto shape : kShapes)
        if (shapeKey(shape) == key) return shape;
    return std::nullopt;
}

QJsonObject toJson(const Boundary &boundary) {
    QJsonObject object{{"id", boundary.id}, {"topics", QJsonArray::fromStringList(boundary.topics)}};
    // Only the values that differ from the default, like the rest of the sheet.
    if (!boundary.title.isEmpty()) object.insert(QStringLiteral("title"), boundary.title);
    if (boundary.shape != Boundary::Shape::RoundedRectangle) object.insert(QStringLiteral("shape"), shapeKey(boundary.shape));
    if (boundary.lineColor) object.insert(QStringLiteral("lineColor"), boundary.lineColor->name(QColor::HexArgb));
    if (boundary.lineWidth) object.insert(QStringLiteral("lineWidth"), *boundary.lineWidth);
    if (boundary.linePattern) object.insert(QStringLiteral("linePattern"), *boundary.linePattern);
    if (boundary.fillColor) object.insert(QStringLiteral("fillColor"), boundary.fillColor->name(QColor::HexArgb));
    if (boundary.fillStyle != NodeFillStyle::Pieno) object.insert(QStringLiteral("fillStyle"), NodeStyle::fillStyleKey(boundary.fillStyle));
    if (!boundary.titleFont.isEmpty()) object.insert(QStringLiteral("titleFont"), boundary.titleFont);
    if (boundary.titleSize) object.insert(QStringLiteral("titleSize"), *boundary.titleSize);
    if (boundary.titleColor) object.insert(QStringLiteral("titleColor"), boundary.titleColor->name(QColor::HexArgb));
    if (boundary.titleBold) object.insert(QStringLiteral("titleBold"), true);
    return object;
}

std::optional<Boundary> fromJson(const QJsonObject &object) {
    Boundary boundary;
    boundary.id = object.value(QStringLiteral("id")).toString();
    for (const QJsonValue &topic : object.value(QStringLiteral("topics")).toArray())
        if (const QString id = topic.toString(); !id.isEmpty() && !boundary.topics.contains(id)) boundary.topics.append(id);
    if (boundary.id.isEmpty() || boundary.topics.isEmpty()) return std::nullopt;
    boundary.title = object.value(QStringLiteral("title")).toString();
    boundary.shape = shapeFromKey(object.value(QStringLiteral("shape")).toString()).value_or(Boundary::Shape::RoundedRectangle);
    boundary.lineColor = colorFromJson(object.value(QStringLiteral("lineColor")));
    if (const QJsonValue width = object.value(QStringLiteral("lineWidth")); width.isDouble() && width.toDouble() > 0)
        boundary.lineWidth = width.toDouble();
    if (const QJsonValue pattern = object.value(QStringLiteral("linePattern")); pattern.isDouble()) {
        const int style = pattern.toInt();
        if (style == Qt::SolidLine || style == Qt::DashLine || style == Qt::DotLine || style == Qt::NoPen) boundary.linePattern = style;
    }
    boundary.fillColor = colorFromJson(object.value(QStringLiteral("fillColor")));
    boundary.fillStyle = NodeStyle::fillStyleFromKey(object.value(QStringLiteral("fillStyle")).toString()).value_or(NodeFillStyle::Pieno);
    boundary.titleFont = object.value(QStringLiteral("titleFont")).toString();
    // The Style page's sizes; a damaged value is held to them.
    if (const QJsonValue size = object.value(QStringLiteral("titleSize")); size.isDouble() && size.toDouble() > 0)
        boundary.titleSize = std::clamp(size.toDouble(), qreal(NodeStyle::kFontSizesPx.first()), qreal(NodeStyle::kFontSizesPx.last()));
    boundary.titleColor = colorFromJson(object.value(QStringLiteral("titleColor")));
    boundary.titleBold = object.value(QStringLiteral("titleBold")).toBool();
    return boundary;
}

QJsonArray toJson(const QList<Boundary> &boundaries) {
    QJsonArray array;
    for (const Boundary &boundary : boundaries) array.append(toJson(boundary));
    return array;
}

QList<Boundary> listFromJson(const QJsonArray &array) {
    QList<Boundary> boundaries;
    for (const QJsonValue &value : array)
        if (value.isObject())
            if (auto boundary = fromJson(value.toObject())) boundaries.append(*boundary);
    return boundaries;
}

QRectF strokeRect(const QRectF &covered) { return covered.adjusted(-kMarginPx, -kMarginPx, kMarginPx, kMarginPx); }

namespace {

// One edge of a closed shape from `a` to `b`, clockwise; `out` is the outward unit normal. Waves and
// scallops fit a whole number of pieces, so every edge starts and ends on its corner.
void appendEdge(QPainterPath &path, Boundary::Shape shape, const QPointF &a, const QPointF &b, const QPointF &out) {
    const QLineF edge(a, b);
    const qreal length = edge.length();
    const QPointF along = length > 0 ? (b - a) / length : QPointF();
    switch (shape) {
    case Boundary::Shape::Waves: {
        // Half waves as quadratic curves: a control point twice the amplitude off the edge puts the
        // crest at the amplitude.
        const int halves = 2 * std::max(1, int(std::lround(length / kWaveLengthPx)));
        const qreal step = length / halves;
        for (int i = 0; i < halves; ++i) {
            const qreal sign = i % 2 == 0 ? 1 : -1;
            path.quadTo(a + along * (step * (i + 0.5)) + out * (2 * kWaveAmplitudePx * sign), a + along * (step * (i + 1)));
        }
        return;
    }
    case Boundary::Shape::Scallops: {
        const int count = std::max(1, int(std::lround(length / kScallopLengthPx)));
        const qreal step = length / count;
        for (int i = 0; i < count; ++i)
            path.quadTo(a + along * (step * (i + 0.5)) + out * (2 * kScallopBulgePx), a + along * (step * (i + 1)));
        return;
    }
    case Boundary::Shape::Tension:
        path.quadTo((a + b) / 2 - out * (2 * std::min(kTensionPx, length / 6)), b);
        return;
    default: path.lineTo(b); return;
    }
}

qreal quarterOf(const QRectF &rect, qreal value) { return std::min({value, rect.width() / 4, rect.height() / 4}); }

// FEAT-134: the right side of Tag and Rounded left, clockwise from the top edge to the bottom one,
// corners of the rounded rectangle's radius.
void appendRoundedRight(QPainterPath &path, const QRectF &r) {
    const qreal d = 2 * std::min(kCornerRadiusPx, std::min(r.width(), r.height()) / 2);
    path.lineTo(r.right() - d / 2, r.top());
    path.arcTo(QRectF(r.right() - d, r.top(), d, d), 90, -90);
    path.lineTo(r.right(), r.bottom() - d / 2);
    path.arcTo(QRectF(r.right() - d, r.bottom() - d, d, d), 0, -90);
}

// Where the title tab stands: on the shape's reach, on a frame's rectangle (between the overruns).
QRectF tabBase(Boundary::Shape shape, const QRectF &stroke) {
    return shape == Boundary::Shape::Frame ? stroke : shapeBounds(shape, stroke);
}

}  // namespace

QPainterPath outline(Boundary::Shape shape, const QRectF &stroke) {
    QPainterPath path;
    switch (shape) {
    case Boundary::Shape::Rectangle:
    case Boundary::Shape::Corners:
    case Boundary::Shape::Frame: path.addRect(stroke); break;
    case Boundary::Shape::Tag: {
        const qreal c = quarterOf(stroke, kPolygonCutPx);
        path.moveTo(stroke.left() + c, stroke.top());
        appendRoundedRight(path, stroke);
        path.lineTo(stroke.left() + c, stroke.bottom());
        path.lineTo(stroke.left(), stroke.bottom() - c);
        path.lineTo(stroke.left(), stroke.top() + c);
        path.closeSubpath();
        break;
    }
    case Boundary::Shape::RoundedLeft: {
        const qreal d = 2 * std::min({kRoundedLeftRadiusPx, stroke.height() / 2, stroke.width() / 4});
        path.moveTo(stroke.left() + d / 2, stroke.top());
        appendRoundedRight(path, stroke);
        path.lineTo(stroke.left() + d / 2, stroke.bottom());
        path.arcTo(QRectF(stroke.left(), stroke.bottom() - d, d, d), 270, -90);
        path.lineTo(stroke.left(), stroke.top() + d / 2);
        path.arcTo(QRectF(stroke.left(), stroke.top(), d, d), 180, -90);
        path.closeSubpath();
        break;
    }
    case Boundary::Shape::Ellipse: path.addEllipse(shapeBounds(shape, stroke)); break;
    case Boundary::Shape::Polygon: {
        const qreal c = quarterOf(stroke, kPolygonCutPx);
        const QRectF r = stroke;
        path.addPolygon(QPolygonF{{r.left() + c, r.top()}, {r.right() - c, r.top()}, {r.right(), r.top() + c},
                                  {r.right(), r.bottom() - c}, {r.right() - c, r.bottom()}, {r.left() + c, r.bottom()},
                                  {r.left(), r.bottom() - c}, {r.left(), r.top() + c}});
        path.closeSubpath();
        break;
    }
    case Boundary::Shape::Waves:
    case Boundary::Shape::Scallops:
    case Boundary::Shape::Tension:
        path.moveTo(stroke.topLeft());
        appendEdge(path, shape, stroke.topLeft(), stroke.topRight(), {0, -1});
        appendEdge(path, shape, stroke.topRight(), stroke.bottomRight(), {1, 0});
        appendEdge(path, shape, stroke.bottomRight(), stroke.bottomLeft(), {0, 1});
        appendEdge(path, shape, stroke.bottomLeft(), stroke.topLeft(), {-1, 0});
        path.closeSubpath();
        break;
    case Boundary::Shape::RoundedRectangle: path.addRoundedRect(stroke, kCornerRadiusPx, kCornerRadiusPx); break;
    }
    return path;
}

QPainterPath border(Boundary::Shape shape, const QRectF &stroke) {
    QPainterPath path;
    if (shape == Boundary::Shape::Frame) {
        // Each edge on its own, running on past both corners.
        const qreal o = kFrameOverrunPx;
        const QRectF r = stroke;
        for (const QLineF &edge : {QLineF(r.left() - o, r.top(), r.right() + o, r.top()), QLineF(r.left() - o, r.bottom(), r.right() + o, r.bottom()),
                                   QLineF(r.left(), r.top() - o, r.left(), r.bottom() + o), QLineF(r.right(), r.top() - o, r.right(), r.bottom() + o)}) {
            path.moveTo(edge.p1());
            path.lineTo(edge.p2());
        }
        return path;
    }
    if (shape != Boundary::Shape::Corners) return outline(shape, stroke);
    const qreal arm = quarterOf(stroke, kCornerArmPx);
    const QRectF r = stroke;
    // Each mark from one arm's end through the corner to the other's.
    const auto mark = [&path](const QPointF &from, const QPointF &corner, const QPointF &to) {
        path.moveTo(from);
        path.lineTo(corner);
        path.lineTo(to);
    };
    mark({r.left(), r.top() + arm}, r.topLeft(), {r.left() + arm, r.top()});
    mark({r.right() - arm, r.top()}, r.topRight(), {r.right(), r.top() + arm});
    mark({r.right(), r.bottom() - arm}, r.bottomRight(), {r.right() - arm, r.bottom()});
    mark({r.left() + arm, r.bottom()}, r.bottomLeft(), {r.left(), r.bottom() - arm});
    return path;
}

QRectF shapeBounds(Boundary::Shape shape, const QRectF &stroke) {
    switch (shape) {
    case Boundary::Shape::Waves: return stroke.adjusted(-kWaveAmplitudePx, -kWaveAmplitudePx, kWaveAmplitudePx, kWaveAmplitudePx);
    case Boundary::Shape::Scallops: return stroke.adjusted(-kScallopBulgePx, -kScallopBulgePx, kScallopBulgePx, kScallopBulgePx);
    case Boundary::Shape::Frame: return stroke.adjusted(-kFrameOverrunPx, -kFrameOverrunPx, kFrameOverrunPx, kFrameOverrunPx);
    case Boundary::Shape::Ellipse: {
        // The ellipse of the rect's proportions through its corners: half axes times sqrt(2).
        const qreal dx = stroke.width() / 2 * (std::numbers::sqrt2 - 1), dy = stroke.height() / 2 * (std::numbers::sqrt2 - 1);
        return stroke.adjusted(-dx, -dy, dx, dy);
    }
    default: return stroke;
    }
}

qreal titleHeight(const Boundary &boundary) {
    return std::max(kTitleHeightPx, boundary.titleSize.value_or(kTitleFontPx) + 4);
}

QRectF titleRect(const Boundary &boundary, const QRectF &stroke, qreal textWidth) {
    const QRectF bounds = tabBase(boundary.shape, stroke);
    const qreal width = textWidth + 2 * kTitlePaddingPx, height = titleHeight(boundary);
    const qreal left = boundary.shape == Boundary::Shape::Ellipse ? bounds.center().x() - width / 2 : bounds.left() + kTitleInsetPx;
    return {left, bounds.top() - height, width, height};
}

QPainterPath titlePath(const QRectF &tab) {
    // Rounded top corners, square bottom: the tab stands on the border.
    QPainterPath path;
    const qreal r = kTitleRadiusPx;
    path.moveTo(tab.bottomLeft());
    path.lineTo(tab.left(), tab.top() + r);
    path.quadTo(tab.topLeft(), {tab.left() + r, tab.top()});
    path.lineTo(tab.right() - r, tab.top());
    path.quadTo(tab.topRight(), {tab.right(), tab.top() + r});
    path.lineTo(tab.bottomRight());
    path.closeSubpath();
    return path;
}

QMarginsF room(const Boundary &boundary, const QSizeF &covered) {
    const qreal base = kMarginPx + boundary.lineWidth.value_or(kDefaultLineWidth) / 2;
    const QRectF stroke = strokeRect(QRectF(QPointF(), covered));
    const QRectF bounds = shapeBounds(boundary.shape, stroke);
    const qreal dx = stroke.left() - bounds.left(), dy = stroke.top() - bounds.top();
    const qreal tab = boundary.title.isEmpty() ? 0 : titleHeight(boundary);
    // The tab stands on the reach, or on a frame's rectangle, where the overrun may already hold it.
    const qreal above = std::max(dy, stroke.top() - tabBase(boundary.shape, stroke).top() + tab);
    return {base + dx, base + above, base + dx, base + dy};
}

Fill fill(const Boundary &boundary, const QRectF &stroke) {
    Fill result;
    if (boundary.fillStyle == NodeFillStyle::Nessuno) return result;
    const QPainterPath body = outline(boundary.shape, stroke);
    const quint64 seed = NodeSketch::seedFor(boundary.id);
    if (boundary.fillStyle == NodeFillStyle::PienoManoLibera) result.area = NodeSketch::roughArea(body, seed);
    else if (NodeStyle::isPatternFill(boundary.fillStyle)) result.pattern = NodeSketch::pattern(boundary.fillStyle, body, seed);
    else result.area = body;
    return result;
}

Look look(const Boundary &boundary, const NodeStyle::Backdrop &backdrop, const QString &sheetFontFamily) {
    const QColor ink = NodeStyle::annotationColor(Qt::black, backdrop);
    QColor line = ink;
    line.setAlphaF(kLineAlpha);
    QColor fill = ink;
    fill.setAlphaF(kFillAlpha);
    Look result;
    const qreal width = boundary.lineWidth.value_or(kDefaultLineWidth);
    result.pen = QPen(boundary.lineColor.value_or(line), width, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
    const int pattern = boundary.linePattern.value_or(kDefaultLinePattern);
    // QPen's dash pattern is in units of the width (https://doc.qt.io/qt-6/qpen.html#setDashPattern).
    if (pattern == Qt::DashLine) result.pen.setDashPattern({kDashOnPx / width, kDashOffPx / width});
    else result.pen.setStyle(static_cast<Qt::PenStyle>(pattern));
    result.fill = boundary.fillColor.value_or(fill);
    result.titleText = boundary.titleColor.value_or(NodeStyle::annotationColor(QColor("#303640"), backdrop));
    const QString family = !boundary.titleFont.isEmpty() ? boundary.titleFont : sheetFontFamily;
    result.font.setFamily(family.isEmpty() ? NodeStyle::kDefaultMapFontFamily : family);
    result.font.setPixelSize(qRound(boundary.titleSize.value_or(kTitleFontPx)));
    if (boundary.titleBold) result.font.setBold(true);
    return result;
}

void widenAroundNested(const QList<Boundary> &boundaries, QList<QRectF> &covers,
                       const std::function<bool(const Boundary &, const Boundary &)> &inside) {
    for (bool grown = true; grown;) {
        grown = false;
        for (qsizetype o = 0; o < boundaries.size(); ++o)
            for (qsizetype i = 0; i < boundaries.size(); ++i) {
                if (i == o || covers[i].isNull() || covers[o].isNull() || !inside(boundaries[i], boundaries[o])) continue;
                if (inside(boundaries[o], boundaries[i]) && o < i) continue;  // same topics: the later one is outside
                QRectF need = shapeBounds(boundaries[i].shape, strokeRect(covers[i]));
                const qreal half = boundaries[i].lineWidth.value_or(kDefaultLineWidth) / 2;
                need.adjust(-half, -half, half, half);
                if (!boundaries[i].title.isEmpty())
                    need.setTop(std::min(need.top(), tabBase(boundaries[i].shape, strokeRect(covers[i])).top() - titleHeight(boundaries[i]) - half));
                if (covers[o].contains(need)) continue;
                // The union's right and bottom are x + width again, which can round a hair inside
                // need's: contains() then stays false. Only a union that changes the rect counts
                // (QRectF's == is fuzzy), otherwise the loop never ends (the phone froze, 2026-11-13).
                const QRectF united = covers[o].united(need);
                if (united == covers[o]) continue;
                covers[o] = united;
                grown = true;
            }
    }
}

}  // namespace Boundaries
