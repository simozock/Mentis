#include "relationship.h"

#include <QLineF>
#include <QtMath>

#include <cmath>

namespace Relationships {

namespace {

QPointF rotated(const QPointF &vector, qreal degrees) {
    const qreal radians = qDegreesToRadians(degrees);
    return {vector.x() * std::cos(radians) - vector.y() * std::sin(radians),
            vector.x() * std::sin(radians) + vector.y() * std::cos(radians)};
}

QPointF unit(const QPointF &vector) {
    const qreal length = std::hypot(vector.x(), vector.y());
    return length > 0 ? vector / length : QPointF(1, 0);
}

std::optional<QPointF> pointFromJson(const QJsonValue &value) {
    const QJsonArray pair = value.toArray();
    if (pair.size() != 2 || !pair[0].isDouble() || !pair[1].isDouble()) return std::nullopt;
    return QPointF(pair[0].toDouble(), pair[1].toDouble());
}

std::optional<QColor> colorFromJson(const QJsonValue &value) {
    const QColor color(value.toString());
    return color.isValid() ? std::optional<QColor>(color) : std::nullopt;
}

}  // namespace

QString shapeKey(Relationship::Shape shape) {
    switch (shape) {
    case Relationship::Shape::Curved: return QStringLiteral("curved");
    case Relationship::Shape::Straight: return QStringLiteral("straight");
    case Relationship::Shape::Elbow: return QStringLiteral("elbow");
    }
    return QStringLiteral("curved");
}

std::optional<Relationship::Shape> shapeFromKey(const QString &key) {
    for (auto shape : {Relationship::Shape::Curved, Relationship::Shape::Straight, Relationship::Shape::Elbow})
        if (shapeKey(shape) == key) return shape;
    return std::nullopt;
}

QJsonObject toJson(const Relationship &relationship) {
    QJsonObject object{{"id", relationship.id}, {"from", relationship.from}, {"to", relationship.to}};
    // Only the values that differ from the default, like the rest of the sheet.
    if (relationship.shape != Relationship::Shape::Curved) object.insert(QStringLiteral("shape"), shapeKey(relationship.shape));
    if (relationship.fromControl) object.insert(QStringLiteral("fromControl"), QJsonArray{relationship.fromControl->x(), relationship.fromControl->y()});
    if (relationship.toControl) object.insert(QStringLiteral("toControl"), QJsonArray{relationship.toControl->x(), relationship.toControl->y()});
    if (!relationship.label.isEmpty()) object.insert(QStringLiteral("label"), relationship.label);
    if (relationship.lineColor) object.insert(QStringLiteral("lineColor"), relationship.lineColor->name(QColor::HexArgb));
    if (relationship.lineWidth) object.insert(QStringLiteral("lineWidth"), *relationship.lineWidth);
    if (relationship.linePattern) object.insert(QStringLiteral("linePattern"), *relationship.linePattern);
    if (relationship.startMark) object.insert(QStringLiteral("startMark"), NodeStyle::branchEndKey(*relationship.startMark));
    if (relationship.endMark) object.insert(QStringLiteral("endMark"), NodeStyle::branchEndKey(*relationship.endMark));
    if (!relationship.fontFamily.isEmpty()) object.insert(QStringLiteral("fontFamily"), relationship.fontFamily);
    if (relationship.fontSize) object.insert(QStringLiteral("fontSize"), *relationship.fontSize);
    if (relationship.textColor) object.insert(QStringLiteral("textColor"), relationship.textColor->name(QColor::HexArgb));
    return object;
}

std::optional<Relationship> fromJson(const QJsonObject &object) {
    Relationship relationship;
    relationship.id = object.value(QStringLiteral("id")).toString();
    relationship.from = object.value(QStringLiteral("from")).toString();
    relationship.to = object.value(QStringLiteral("to")).toString();
    if (relationship.id.isEmpty() || relationship.from.isEmpty() || relationship.to.isEmpty() || relationship.from == relationship.to)
        return std::nullopt;
    relationship.shape = shapeFromKey(object.value(QStringLiteral("shape")).toString()).value_or(Relationship::Shape::Curved);
    relationship.fromControl = pointFromJson(object.value(QStringLiteral("fromControl")));
    relationship.toControl = pointFromJson(object.value(QStringLiteral("toControl")));
    relationship.label = object.value(QStringLiteral("label")).toString();
    relationship.lineColor = colorFromJson(object.value(QStringLiteral("lineColor")));
    // Values from a file are kept inside the ranges the Style page offers.
    if (const QJsonValue width = object.value(QStringLiteral("lineWidth")); width.isDouble() && width.toDouble() > 0)
        relationship.lineWidth = std::min(width.toDouble(), 20.0);
    if (const int pattern = object.value(QStringLiteral("linePattern")).toInt(); pattern >= Qt::SolidLine && pattern <= Qt::DotLine)
        relationship.linePattern = pattern;
    relationship.startMark = NodeStyle::branchEndFromKey(object.value(QStringLiteral("startMark")).toString());
    relationship.endMark = NodeStyle::branchEndFromKey(object.value(QStringLiteral("endMark")).toString());
    relationship.fontFamily = object.value(QStringLiteral("fontFamily")).toString();
    if (const QJsonValue size = object.value(QStringLiteral("fontSize")); size.isDouble() && size.toDouble() > 0)
        relationship.fontSize = std::min(size.toDouble(), 200.0);
    relationship.textColor = colorFromJson(object.value(QStringLiteral("textColor")));
    return relationship;
}

QJsonArray toJson(const QList<Relationship> &relationships) {
    QJsonArray array;
    for (const Relationship &relationship : relationships) array.append(toJson(relationship));
    return array;
}

QList<Relationship> listFromJson(const QJsonArray &array) {
    QList<Relationship> relationships;
    for (const QJsonValue &value : array)
        if (value.isObject())
            if (auto relationship = fromJson(value.toObject())) relationships.append(*relationship);
    return relationships;
}

QPointF outlineExit(const QPainterPath &outline, const QPointF &centre, const QPointF &towards) {
    if (!outline.contains(centre)) return centre;
    // Bisection between the centre (inside) and a point surely outside, along the ray.
    const QRectF box = outline.boundingRect();
    const QPointF direction = unit(towards - centre);
    QPointF inside = centre;
    QPointF outside = centre + direction * (std::hypot(box.width(), box.height()) + 1);
    for (int step = 0; step < 24; ++step) {
        const QPointF middle = (inside + outside) / 2;
        (outline.contains(middle) ? inside : outside) = middle;
    }
    return outside;
}

Geometry geometry(const Relationship &relationship, const End &from, const End &to) {
    Geometry result;
    result.fromCentre = from.centre;
    result.toCentre = to.centre;
    if (relationship.shape == Relationship::Shape::Straight) {
        result.start = outlineExit(from.outline, from.centre, to.centre);
        result.end = outlineExit(to.outline, to.centre, from.centre);
        result.fromControl = result.start;
        result.toControl = result.end;
        result.path.moveTo(result.start);
        result.path.lineTo(result.end);
        result.labelPoint = (result.start + result.end) / 2;
        result.endDirection = unit(result.end - result.start);
        result.startDirection = -result.endDirection;
        return result;
    }
    // An automatic end sits on the line joining the centres; a moved handle pulls its end
    // towards itself. Automatic control points come from the ends, turned like an S.
    const auto automaticControl = [](const QPointF &at, const QPointF &other) {
        return at + rotated((other - at) * kControlShare, kControlAngleDeg);
    };
    result.start = outlineExit(from.outline, from.centre, relationship.fromControl ? from.centre + *relationship.fromControl : to.centre);
    result.end = outlineExit(to.outline, to.centre, relationship.toControl ? to.centre + *relationship.toControl : from.centre);
    result.fromControl = relationship.fromControl ? from.centre + *relationship.fromControl : automaticControl(result.start, result.end);
    result.toControl = relationship.toControl ? to.centre + *relationship.toControl : automaticControl(result.end, result.start);
    result.path.moveTo(result.start);
    if (relationship.shape == Relationship::Shape::Elbow) {
        result.path.lineTo(result.fromControl);
        result.path.lineTo(result.toControl);
        result.path.lineTo(result.end);
    } else {
        result.path.cubicTo(result.fromControl, result.toControl, result.end);
    }
    result.labelPoint = result.path.pointAtPercent(0.5);
    // Direction at each end: from its control point, or along the chord if they coincide.
    const QPointF arriving = result.end - result.toControl;
    result.endDirection = unit(std::hypot(arriving.x(), arriving.y()) > 0.5 ? arriving : result.end - result.start);
    const QPointF leaving = result.start - result.fromControl;
    result.startDirection = unit(std::hypot(leaving.x(), leaving.y()) > 0.5 ? leaving : result.start - result.end);
    return result;
}

}  // namespace Relationships
