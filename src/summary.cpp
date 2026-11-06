#include "summary.h"

#include <algorithm>
#include <cmath>

namespace Summaries {

namespace {

bool vertical(Side side) { return side == Side::Down || side == Side::Up; }
qreal sign(Side side) { return side == Side::Left || side == Side::Up ? -1 : 1; }

// Coordinate, across the run, where the arms start (beyond the covered branches).
qreal armStart(const QRectF &covered, Side side) {
    switch (side) {
    case Side::Right: return covered.right() + kGapPx;
    case Side::Left: return covered.left() - kGapPx;
    case Side::Down: return covered.bottom() + kGapPx;
    case Side::Up: return covered.top() - kGapPx;
    }
    return 0;
}

}  // namespace

Side sideFor(const QList<QPointF> &centres, const QRectF &covered, const QPointF &parentCentre, bool row) {
    if (!row && centres.size() >= 2) {
        qreal minX = centres.front().x(), maxX = minX, minY = centres.front().y(), maxY = minY;
        for (const QPointF &p : centres) {
            minX = std::min(minX, p.x()); maxX = std::max(maxX, p.x());
            minY = std::min(minY, p.y()); maxY = std::max(maxY, p.y());
        }
        row = maxX - minX > maxY - minY;
    } else if (!row) {
        const QPointF away = covered.center() - parentCentre;
        row = std::abs(away.y()) > std::abs(away.x());
    }
    if (row) return covered.center().y() >= parentCentre.y() ? Side::Down : Side::Up;
    return covered.center().x() >= parentCentre.x() ? Side::Right : Side::Left;
}

QPainterPath bracket(const QRectF &covered, Side side) {
    const qreal dir = sign(side);
    const qreal start = armStart(covered, side);
    const qreal spine = start + dir * (kArmPx - kLineWidth / 2);  // centre of the line
    const qreal tick = spine + dir * (kLineWidth / 2 + kTickPx);
    // Along the run: from 2 px before the first box to 2 px after the last one.
    const qreal from = (vertical(side) ? covered.left() : covered.top()) - kOverhangPx;
    const qreal to = (vertical(side) ? covered.right() : covered.bottom()) + kOverhangPx;
    const qreal middle = (from + to) / 2;
    // Written for a bracket on the right (x across, y along) and turned for one below or above.
    const auto at = [&side](qreal across, qreal along) { return vertical(side) ? QPointF(along, across) : QPointF(across, along); };
    QPainterPath path;
    path.moveTo(at(start, from));
    path.lineTo(at(spine, from));
    path.lineTo(at(spine, to));
    path.lineTo(at(start, to));
    path.moveTo(at(spine, middle));
    path.lineTo(at(tick, middle));
    return path;
}

QPointF topicAnchor(const QRectF &covered, Side side) {
    const qreal across = armStart(covered, side) + sign(side) * (kArmPx + kTickPx + kTopicGapPx);
    return vertical(side) ? QPointF(covered.center().x(), across) : QPointF(across, covered.center().y());
}

QRectF extent(const QRectF &covered, Side side, const QSizeF &topicSize) {
    const QPointF anchor = topicAnchor(covered, side);
    const qreal w = topicSize.width(), h = topicSize.height();
    QRectF topic;
    switch (side) {
    case Side::Right: topic = QRectF(anchor.x(), anchor.y() - h / 2, w, h); break;
    case Side::Left: topic = QRectF(anchor.x() - w, anchor.y() - h / 2, w, h); break;
    case Side::Down: topic = QRectF(anchor.x() - w / 2, anchor.y(), w, h); break;
    case Side::Up: topic = QRectF(anchor.x() - w / 2, anchor.y() - h, w, h); break;
    }
    return bracket(covered, side).boundingRect().united(topic);
}

}  // namespace Summaries
