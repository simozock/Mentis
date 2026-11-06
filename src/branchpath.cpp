#include "branchpath.h"
#include "nodestyle.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QLineF>

namespace BranchPath {

QPointF exitPoint(const QRectF &box, const QPointF &target) {
    const QPointF centre = box.center();
    const QPointF direction = target - centre;
    if (box.contains(target) || (direction.x() == 0 && direction.y() == 0)) return centre;
    // Il bordo piu' vicino lungo la semiretta: il minore fra i passi che portano ai lati verticali
    // e a quelli orizzontali.
    const qreal inf = std::numeric_limits<qreal>::infinity();
    const qreal tx = direction.x() == 0 ? inf : (box.width() / 2) / std::abs(direction.x());
    const qreal ty = direction.y() == 0 ? inf : (box.height() / 2) / std::abs(direction.y());
    return centre + direction * std::min(tx, ty);
}

QPainterPath fromRoot(const QRectF &rootBox, const QPointF &entry) {
    // FEAT-072 A7 (measured live, 2026-10-22): the branch starts where the straight line from the
    // box centre to the child's entry point crosses the box outline, and is a quadratic curve whose
    // control point sits at the child's height, 20% of the horizontal distance from the start. Far
    // children leave almost vertically near the middle of the top or bottom edge, close ones from the
    // side, a child level with the centre gets a straight line; the start follows the box when the
    // central topic changes size.
    return curve(exitPoint(rootBox, entry), entry);
}

QPainterPath curve(const QPointF &start, const QPointF &entry) {
    constexpr qreal kControlShare = 0.2;
    QPainterPath path(start);
    if (std::abs(entry.y() - start.y()) < 1) { path.lineTo(entry); return path; }
    path.quadTo(QPointF(start.x() + kControlShare * (entry.x() - start.x()), entry.y()), entry);
    return path;
}

QPainterPath angled(const QPointF &from, const QPointF &to) {
    const qreal dx = to.x() - from.x();
    const qreal dy = to.y() - from.y();
    QPainterPath path(from);
    // Obliquo per circa due terzi del dislivello, mai oltre meta' della distanza orizzontale.
    const qreal run = std::min(std::abs(dx) / 2, std::abs(dy) * 0.65);
    if (std::abs(dy) >= 1) path.lineTo(from.x() + (dx < 0 ? -run : run), to.y());
    path.lineTo(to);
    return path;
}

QPainterPath square(const QPointF &from, const QPointF &to) {
    QPainterPath path(from);
    if (std::abs(to.y() - from.y()) >= 1) path.lineTo(from.x(), to.y());
    path.lineTo(to);
    return path;
}

QPainterPath squareFromStub(const QPointF &from, const QPointF &to) {
    const QPointF stub(from.x() + (to.x() >= from.x() ? kStubPx : -kStubPx), from.y());
    QPainterPath path(from);
    path.lineTo(stub);
    path.connectPath(square(stub, to));
    return path;
}

QPainterPath straight(const QPointF &from, const QPointF &to) {
    QPainterPath path(from);
    path.lineTo(to);
    return path;
}

QPainterPath bracket(const QPointF &from, const QPointF &to, bool roundCorner) {
    const qreal sx = to.x() >= from.x() ? 1 : -1;
    const qreal barX = from.x() + sx * kStubPx;
    QPainterPath path(from);
    const qreal dy = to.y() - from.y();
    if (std::abs(dy) < 0.5) { path.lineTo(to); return path; }
    const qreal sy = dy > 0 ? 1 : -1;
    const qreal r = roundCorner ? std::min({kCornerPx, std::abs(dy), std::abs(to.x() - barX)}) : 0;
    path.lineTo(barX, from.y());
    path.lineTo(barX, to.y() - sy * r);
    if (r > 0) path.quadTo(barX, to.y(), barX + sx * r, to.y());
    path.lineTo(to);
    return path;
}

QPainterPath braceArm(const QPointF &cusp, qreal endY, qreal radius, int dir) {
    // Cubic quarter circles (handles at 0.5523 of the radius: https://spencermortensen.com/articles/bezier-circle/).
    constexpr qreal kKappa = 0.5523;
    const qreal barX = cusp.x() + dir * radius;
    const QPointF end(cusp.x() + 2 * dir * radius, endY);
    QPainterPath path(cusp);
    const qreal dy = endY - cusp.y();
    if (std::abs(dy) < 0.5) { path.lineTo(end); return path; }
    const qreal sy = dy > 0 ? 1 : -1;
    const qreal r = std::min(radius, std::abs(dy) / 2);
    const qreal k = kKappa * r;
    path.lineTo(barX - dir * r, cusp.y());
    path.cubicTo(barX - dir * r + dir * k, cusp.y(), barX, cusp.y() + sy * (r - k), barX, cusp.y() + sy * r);
    path.lineTo(barX, endY - sy * r);
    path.cubicTo(barX, endY - sy * (r - k), barX + dir * (r - k), endY, barX + dir * r, endY);
    path.lineTo(end);
    return path;
}

QPainterPath fan(const QPointF &from, const QPointF &to, int shape) {
    // shape: the BranchShape values 3-7 (Straight, Curve, SCurve, Angled, RoundedAngled); BranchPath
    // does not depend on NodeStyle, the caller passes the number.
    const qreal sx = to.x() >= from.x() ? 1 : -1;
    const QPointF point(from.x() + sx * kStubPx, from.y());
    QPainterPath path(from);
    path.lineTo(point);
    if (std::abs(to.y() - point.y()) < 0.5) { path.lineTo(to); return path; }
    const qreal dx = to.x() - point.x();
    switch (shape) {
    case 4:  // curve
        path.quadTo(QPointF(point.x() + 0.2 * dx, to.y()), to);
        break;
    case 5:  // S-curve
        path.cubicTo(QPointF(point.x() + dx / 2, point.y()), QPointF(point.x() + dx / 2, to.y()), to);
        break;
    case 6:  // angled
    case 7: {  // angled with a rounded bend
        const QPointF bend(to.x() - sx * std::min(kFanRunPx, std::abs(dx) / 2), to.y());
        if (shape == 6) { path.lineTo(bend); path.lineTo(to); break; }
        const QLineF diagonal(point, bend);
        const qreal r = std::min<qreal>(8, diagonal.length() / 2);
        path.lineTo(diagonal.pointAt(1 - r / diagonal.length()));
        path.quadTo(bend, QPointF(bend.x() + sx * std::min(r, std::abs(to.x() - bend.x())), to.y()));
        path.lineTo(to);
        break;
    }
    default:  // straight
        path.lineTo(to);
        break;
    }
    return path;
}

QPainterPath between(const QPointF &from, const QPointF &to) {
    QPainterPath path(from);
    const qreal middle = (from.x() + to.x()) / 2;
    path.cubicTo({middle, from.y()}, {middle, to.y()}, to);
    return path;
}

QPainterPath elbow(const QPointF &from, const QPointF &to) {
    QPainterPath path(from);
    // La linea orizzontale sta vicino al genitore (al massimo kElbowRunPx): una riga abbassata per far
    // posto ai rami dei figli non la porta sopra di loro.
    const qreal dy = to.y() - from.y();
    const qreal middle = from.y() + (dy > 0 ? 1 : -1) * std::min(std::abs(dy) / 2, kElbowRunPx);
    const qreal dx = to.x() - from.x();
    if (std::abs(dx) < 0.5) { path.lineTo(to); return path; }
    const qreal r = std::min({kCornerPx, std::abs(dx) / 2, std::abs(middle - from.y())});
    const qreal sx = dx > 0 ? 1 : -1;
    const qreal sy = to.y() > from.y() ? 1 : -1;
    path.lineTo(from.x(), middle - sy * r);
    path.quadTo(from.x(), middle, from.x() + sx * r, middle);
    path.lineTo(to.x() - sx * r, middle);
    path.quadTo(to.x(), middle, to.x(), middle + sy * r);
    path.lineTo(to);
    return path;
}

QPainterPath orgFan(const QPointF &from, const QPointF &to) {
    QPainterPath path(from);
    const qreal sy = to.y() >= from.y() ? 1 : -1;
    const QPointF joint(from.x(), from.y() + sy * kOrgStubPx);
    path.lineTo(joint);
    const QPointF corner(to.x(), to.y() - sy * std::min(kOrgRunPx, std::abs(to.y() - joint.y()) / 2));
    const QLineF slant(joint, corner);
    if (std::abs(to.x() - from.x()) < 0.5 || slant.length() < 1) { path.lineTo(to); return path; }
    const qreal r = std::min({kOrgCornerPx, slant.length() / 2, std::abs(to.y() - corner.y())});
    path.lineTo(slant.pointAt(1 - r / slant.length()));
    path.quadTo(corner, QPointF(corner.x(), corner.y() + sy * r));
    path.lineTo(to);
    return path;
}

QPainterPath trunk(const QPointF &from, const QPointF &to) {
    QPainterPath path(from);
    const qreal dx = to.x() - from.x();
    const qreal dy = to.y() - from.y();
    if (std::abs(dx) < 0.5 || std::abs(dy) < 0.5) { path.lineTo(to); return path; }
    const qreal r = std::min({kCornerPx, std::abs(dx), std::abs(dy)});
    path.lineTo(from.x(), to.y() - (dy > 0 ? r : -r));
    path.quadTo(from.x(), to.y(), from.x() + (dx > 0 ? r : -r), to.y());
    path.lineTo(to);
    return path;
}

QPainterPath freehand(const QPainterPath &path, quint64 seed) {
    // Several pieces (a fishbone's bone and spine, FEAT-026 phase E): each by itself, or the stroke would
    // join the end of one to the start of the next.
    bool severalPieces = false;
    for (int i = 1; i < path.elementCount() && !severalPieces; ++i) severalPieces = path.elementAt(i).isMoveTo();
    if (severalPieces) {
        QPainterPath result;
        for (const QPolygonF &piece : path.toSubpathPolygons()) {
            QPainterPath one;
            one.addPolygon(piece);
            result.addPath(freehand(one, seed++));
        }
        return result;
    }
    const qreal length = path.length();
    if (length < 1) return path;
    // Un punto ogni 8 px circa, spostato di lato fino a 0,9 px; le curve passano per i punti medi, cosi'
    // il tratto resta morbido. Generatore lineare congruente: stesso seme, stesso tratto.
    const int steps = std::max(2, int(length / 8));
    quint64 state = seed | 1;
    const auto noise = [&state] {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return (double((state >> 33) & 0xffff) / 65535.0) * 2 - 1;
    };
    QList<QPointF> points;
    for (int i = 0; i <= steps; ++i) {
        const qreal t = path.percentAtLength(length * i / steps);
        const QPointF at = path.pointAtPercent(t);
        const qreal angle = path.angleAtPercent(t) * M_PI / 180.0;
        const QPointF normal(std::sin(angle), std::cos(angle));
        points.append(i == 0 || i == steps ? at : at + normal * (0.9 * noise()));
    }
    QPainterPath result(points.front());
    for (int i = 1; i < points.size() - 1; ++i) result.quadTo(points[i], (points[i] + points[i + 1]) / 2);
    result.lineTo(points.back());
    return result;
}

QPainterPath tapered(const QPainterPath &path, qreal startWidth, qreal endWidth) {
    const qreal length = path.length();
    if (length < 1) return {};
    const int steps = std::max(2, int(length / 4));
    QList<QPointF> left, right;
    for (int i = 0; i <= steps; ++i) {
        const qreal fraction = qreal(i) / steps;
        const qreal t = path.percentAtLength(length * fraction);
        const QPointF at = path.pointAtPercent(t);
        const qreal angle = path.angleAtPercent(t) * M_PI / 180.0;
        const QPointF normal(std::sin(angle), std::cos(angle));
        const qreal half = (startWidth + (endWidth - startWidth) * fraction) / 2;
        left.append(at + normal * half);
        right.append(at - normal * half);
    }
    QPainterPath outline(left.front());
    for (int i = 1; i < left.size(); ++i) outline.lineTo(left[i]);
    for (int i = int(right.size()) - 1; i >= 0; --i) outline.lineTo(right[i]);
    outline.closeSubpath();
    return outline;  // inizio tagliato dritto (prima arrotondato)
}

bool endIsStroke(BranchEnd kind) { return kind == BranchEnd::OpenArrow; }

QPainterPath endMark(BranchEnd kind, const QPointF &tip, const QPointF &outward, qreal lineWidth) {
    const qreal size = std::max(kMinEndMarkPx, 3 * lineWidth);
    const QPointF along = outward * size;
    const QPointF across(-outward.y() * size / 2, outward.x() * size / 2);
    const QPointF centre = tip - along / 2;  // dot, square and diamond end at the tip, outside the node
    QPainterPath mark;
    switch (kind) {
    case BranchEnd::None: break;
    case BranchEnd::OpenArrow:
        mark.moveTo(tip - along + across);
        mark.lineTo(tip);
        mark.lineTo(tip - along - across);
        break;
    case BranchEnd::FilledArrow:
        mark.addPolygon(QPolygonF{tip, tip - along + across, tip - along - across});
        mark.closeSubpath();
        break;
    case BranchEnd::Dot: mark.addEllipse(centre, size / 2, size / 2); break;
    case BranchEnd::Square:
        mark.addPolygon(QPolygonF{tip + across, tip - along + across, tip - along - across, tip - across});
        mark.closeSubpath();
        break;
    case BranchEnd::Diamond:
        mark.addPolygon(QPolygonF{tip, centre + across * 0.7, tip - along, centre - across * 0.7});
        mark.closeSubpath();
        break;
    }
    return mark;
}

}  // namespace BranchPath
