#include "exportframe.h"

#include <QPainter>
#include <QPainterPath>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace {

// The motif drawn on a straight band from (0, 0) to (length, w), the outer edge at y = 0; motifs
// whole, stretched to end on the corner (the count rounded, so at most half a motif of stretch).
// Stroked motifs return their centre line (width w / 5); filled motifs set filled.
QPainterPath motif(ExportFrame::Style style, double length, double w, bool *filled) {
    QPainterPath path;
    *filled = false;
    if (length <= 0) return path;
    const double u = w / 5;  // the meander's grid: five lines across the band
    switch (style) {
    case ExportFrame::Style::GreekKey: {
        // The running key: up, right, down halfway, back, down, then along the bottom to the next.
        const int count = std::max(1, int(std::lround(length / (5 * u))));
        const double p = length / count, ux = p / 5;
        const auto at = [&](double x0, double i, double j) { return QPointF(x0 + (i + 0.5) * ux, (j + 0.5) * u); };
        path.moveTo(QPointF(0, 4.5 * u));
        for (int k = 0; k < count; ++k) {
            const double x0 = k * p;
            path.lineTo(at(x0, 0, 4));
            path.lineTo(at(x0, 0, 0));
            path.lineTo(at(x0, 3, 0));
            path.lineTo(at(x0, 3, 2));
            path.lineTo(at(x0, 1, 2));
            path.lineTo(at(x0, 1, 4));
        }
        path.lineTo(QPointF(length, 4.5 * u));
        break;
    }
    case ExportFrame::Style::Waves: {
        // The Vitruvian scroll: a wave that rises and curls back on itself, one per 1.6 w.
        const int count = std::max(1, int(std::lround(length / (1.6 * w))));
        const double p = length / count;
        path.moveTo(0, 0.85 * w);
        for (int k = 0; k < count; ++k) {
            const double x0 = k * p;
            path.cubicTo(x0 + 0.3 * p, 0.85 * w, x0 + 0.3 * p, 0.15 * w, x0 + 0.58 * p, 0.15 * w);
            path.cubicTo(x0 + 0.78 * p, 0.15 * w, x0 + 0.82 * p, 0.55 * w, x0 + 0.62 * p, 0.58 * w);
            path.cubicTo(x0 + 0.5 * p, 0.6 * w, x0 + 0.48 * p, 0.42 * w, x0 + 0.6 * p, 0.4 * w);
            path.moveTo(x0 + 0.66 * p, 0.62 * w);
            path.cubicTo(x0 + 0.78 * p, 0.85 * w, x0 + 0.9 * p, 0.85 * w, x0 + p, 0.85 * w);
        }
        break;
    }
    case ExportFrame::Style::Chain: {
        // Ovals overlapping by a third, as links.
        const int count = std::max(1, int(std::lround(length / (1.2 * w))));
        const double p = length / count;
        for (int k = 0; k < count; ++k) path.addEllipse(QRectF(k * p - 0.25 * p, 0.2 * w, 1.5 * p, 0.6 * w));
        break;
    }
    case ExportFrame::Style::Dentils: {
        // Blocks on a fillet along the inner edge, as the cornices of temples.
        *filled = true;
        const int count = std::max(1, int(std::lround(length / (0.9 * w))));
        const double p = length / count;
        for (int k = 0; k < count; ++k) path.addRect(QRectF(k * p + 0.2 * p, 0, 0.6 * p, 0.62 * w));
        path.addRect(QRectF(0, 0.76 * w, length, 0.24 * w));
        break;
    }
    default: break;
    }
    return path;
}

}  // namespace

const QColor &ExportFrame::defaultColor() {
    static const QColor accent(0x70, 0x47, 0xeb);
    return accent;
}

double ExportFrame::width() const {
    if (!enabled()) return 0;
    return std::clamp(thickness, isOrnament(style) ? kMinOrnament : kMinThickness, kMaxThickness);
}

double ExportFrame::radius() const { return rounded ? std::clamp(4 * width(), 8.0, 32.0) : 0; }

QString ExportFrame::name(Style style) {
    switch (style) {
    case Style::None: return tr("None");
    case Style::Solid: return tr("Solid");
    case Style::Dashed: return tr("Dashed");
    case Style::Dotted: return tr("Dotted");
    case Style::Double: return tr("Double");
    case Style::GreekKey: return tr("Greek key");
    case Style::Waves: return tr("Waves");
    case Style::Chain: return tr("Chain");
    case Style::Dentils: return tr("Dentils");
    }
    return {};
}

void ExportFrame::paintStrip(QPainter *painter, const QRectF &band) const {
    if (!enabled() || band.isEmpty()) return;
    const double w = band.height();
    const double y = band.center().y();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const auto line = [&](double width, Qt::PenStyle dash, double at) {
        QPen pen(color, width, dash, dash == Qt::DotLine ? Qt::RoundCap : Qt::FlatCap);
        if (dash == Qt::DotLine) pen.setDashPattern({0.01, 2});
        painter->setPen(pen);
        painter->drawLine(QPointF(band.left(), at), QPointF(band.right(), at));
    };
    const double stroke = std::max(1.5, w / 3);
    switch (style) {
    case Style::Solid: line(stroke, Qt::SolidLine, y); break;
    case Style::Dashed: line(stroke, Qt::DashLine, y); break;
    case Style::Dotted: line(stroke, Qt::DotLine, y); break;
    case Style::Double:
        line(std::max(1.0, w / 5), Qt::SolidLine, band.top() + w / 6);
        line(std::max(1.0, w / 5), Qt::SolidLine, band.bottom() - w / 6);
        break;
    default: {
        bool filled = false;
        const QPainterPath path = motif(style, band.width(), w, &filled).translated(band.topLeft());
        if (filled) painter->fillPath(path, color);
        else painter->strokePath(path, QPen(color, std::max(1.0, w / 5), Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        break;
    }
    }
    painter->restore();
}

void ExportFrame::paint(QPainter *painter, const QRectF &outer) const {
    if (!enabled() || outer.width() <= 0 || outer.height() <= 0) return;
    const double w = width();
    const double r = radius();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setBrush(Qt::NoBrush);
    const auto outline = [&](double inset) {  // a rectangle inset from the outer edge, corners following
        QPainterPath path;
        const QRectF rect = outer.adjusted(inset, inset, -inset, -inset);
        if (r > 0) path.addRoundedRect(rect, std::max(0.0, r - inset), std::max(0.0, r - inset));
        else path.addRect(rect);
        return path;
    };
    const auto pen = [&](double width, Qt::PenStyle dash = Qt::SolidLine) {
        QPen line(color, width, dash, dash == Qt::DotLine ? Qt::RoundCap : Qt::FlatCap, r > 0 ? Qt::RoundJoin : Qt::MiterJoin);
        if (dash == Qt::DotLine) line.setDashPattern({0.01, 2});  // round dots one width apart
        return line;
    };
    switch (style) {
    case Style::Solid: painter->setPen(pen(w)); painter->drawPath(outline(w / 2)); break;
    case Style::Dashed: painter->setPen(pen(w, Qt::DashLine)); painter->drawPath(outline(w / 2)); break;
    case Style::Dotted: painter->setPen(pen(w, Qt::DotLine)); painter->drawPath(outline(w / 2)); break;
    case Style::Double:  // two lines of a third, a third apart
        painter->setPen(pen(w / 3));
        painter->drawPath(outline(w / 6));
        painter->drawPath(outline(w - w / 6));
        break;
    default: {
        // Ornaments: the motif along each side between the corners; the corners a square (or the
        // arc's edges when rounded) in the frame's colour.
        const double corner = std::max(w, r);
        const QRectF band = outer;
        struct Side { QPointF origin; double angle; double length; };
        const Side sides[] = {{band.topLeft() + QPointF(corner, 0), 0, band.width() - 2 * corner},
                              {band.topRight() + QPointF(0, corner), 90, band.height() - 2 * corner},
                              {band.bottomRight() - QPointF(corner, 0), 180, band.width() - 2 * corner},
                              {band.bottomLeft() - QPointF(0, corner), 270, band.height() - 2 * corner}};
        for (const Side &side : sides) {
            bool filled = false;
            const QPainterPath path = motif(style, side.length, w, &filled);
            QTransform transform;
            transform.translate(side.origin.x(), side.origin.y());
            transform.rotate(side.angle);
            const QPainterPath placed = transform.map(path);
            if (filled) painter->fillPath(placed, color);
            else painter->strokePath(placed, QPen(color, w / 5, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        }
        const QPointF corners[] = {band.topLeft(), band.topRight() - QPointF(corner, 0), band.bottomRight() - QPointF(corner, corner),
                                   band.bottomLeft() - QPointF(0, corner)};
        painter->setPen(QPen(color, w / 5, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        for (int i = 0; i < 4; ++i) {
            const QRectF box(corners[i], QSizeF(corner, corner));
            if (r > 0) {  // the arc's two edges, on the frame's outer and inner lines
                QPainterPath arcs;
                const QRectF whole = QRectF(i == 1 || i == 2 ? box.right() - 2 * corner : box.left(),
                                            i >= 2 ? box.bottom() - 2 * corner : box.top(), 2 * corner, 2 * corner);
                const double start = i == 0 ? 90 : i == 1 ? 0 : i == 2 ? 270 : 180;
                for (const double inset : {w / 10, w - w / 10}) {
                    const QRectF ring = whole.adjusted(inset, inset, -inset, -inset);
                    arcs.arcMoveTo(ring, start);
                    arcs.arcTo(ring, start, 90);
                }
                painter->drawPath(arcs);
            } else {  // a square with a smaller one inside
                const QRectF outerBox = QRectF(box.topLeft(), QSizeF(w, w)).translated(i == 1 || i == 2 ? corner - w : 0, i >= 2 ? corner - w : 0);
                painter->drawRect(outerBox.adjusted(w / 10, w / 10, -w / 10, -w / 10));
                painter->fillRect(outerBox.adjusted(w * 0.35, w * 0.35, -w * 0.35, -w * 0.35), color);
            }
        }
        break;
    }
    }
    painter->restore();
}
