#include "boundary.h"

namespace Boundaries {

namespace {

std::optional<QColor> colorFromJson(const QJsonValue &value) {
    const QColor color(value.toString());
    return color.isValid() ? std::optional<QColor>(color) : std::nullopt;
}

}  // namespace

QString shapeKey(Boundary::Shape shape) {
    return shape == Boundary::Shape::Rectangle ? QStringLiteral("rectangle") : QStringLiteral("rounded");
}

std::optional<Boundary::Shape> shapeFromKey(const QString &key) {
    for (auto shape : {Boundary::Shape::RoundedRectangle, Boundary::Shape::Rectangle})
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
        if (style == Qt::SolidLine || style == Qt::DashLine || style == Qt::DotLine) boundary.linePattern = style;
    }
    boundary.fillColor = colorFromJson(object.value(QStringLiteral("fillColor")));
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

QPainterPath outline(Boundary::Shape shape, const QRectF &stroke) {
    QPainterPath path;
    if (shape == Boundary::Shape::Rectangle) path.addRect(stroke);
    else path.addRoundedRect(stroke, kCornerRadiusPx, kCornerRadiusPx);
    return path;
}

QRectF titleRect(const QRectF &stroke, qreal textWidth) {
    return {stroke.left() + kTitleInsetPx, stroke.top() - kTitleHeightPx, textWidth + 2 * kTitlePaddingPx, kTitleHeightPx};
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

qreal padAbove(const Boundary &boundary) {
    return padBelow(boundary) + (boundary.title.isEmpty() ? 0 : kTitleHeightPx);
}

qreal padBelow(const Boundary &boundary) {
    return kMarginPx + boundary.lineWidth.value_or(kDefaultLineWidth) / 2;
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
    result.titleText = NodeStyle::annotationColor(QColor("#303640"), backdrop);
    result.font.setFamily(sheetFontFamily.isEmpty() ? NodeStyle::kDefaultMapFontFamily : sheetFontFamily);
    result.font.setPixelSize(qRound(kTitleFontPx));
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
                QRectF need = strokeRect(covers[i]);
                const qreal half = boundaries[i].lineWidth.value_or(kDefaultLineWidth) / 2;
                need.adjust(-half, -half - (boundaries[i].title.isEmpty() ? 0 : kTitleHeightPx), half, half);
                if (covers[o].contains(need)) continue;
                covers[o] = covers[o].united(need);
                grown = true;
            }
    }
}

}  // namespace Boundaries
