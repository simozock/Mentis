#include "stickercanvas.h"

#include <QLineF>
#include <QPainter>
#include <QSvgRenderer>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kCheckerPx = 8;  // as the export preview (FEAT-054)
constexpr qreal kRoomAround = 0.2;  // the preview shows this share of the frame on every side
constexpr qreal kStarInner = 0.5;  // a star's inner points, a share of its radius
const QString kLabelId = QStringLiteral("mentis-label");  // SvgDocument's label element
}  // namespace

StickerCanvas::StickerCanvas()
    : m_renderer(std::make_unique<QSvgRenderer>()), m_onlyRenderer(std::make_unique<QSvgRenderer>()), m_nudgeTimer(std::make_unique<QTimer>()) {
    // Held arrows: one step written after a pause, not one per key (each step rewrites the file).
    m_nudgeTimer->setSingleShot(true);
    m_nudgeTimer->setInterval(kNudgePauseMs);
    QObject::connect(m_nudgeTimer.get(), &QTimer::timeout, m_nudgeTimer.get(), [this] { flush(); });
}

StickerCanvas::~StickerCanvas() = default;

void StickerCanvas::requestUpdate() const {
    if (updateRequested) updateRequested();
}

void StickerCanvas::notifyState() const {
    if (stateChanged) stateChanged();
}

void StickerCanvas::setViewSize(const QSizeF &size) {
    if (size == m_size) return;
    m_size = size;
    render(m_renderer.get(), &m_image);
    requestUpdate();
}

void StickerCanvas::setDevicePixelRatio(qreal ratio) {
    if (qFuzzyCompare(ratio, m_ratio) || ratio <= 0) return;
    m_ratio = ratio;
    render(m_renderer.get(), &m_image);
    requestUpdate();
}

const QList<SvgDocument::Shape> &StickerCanvas::shapes() const {
    if (!m_shapesRead) {
        m_shapes = m_document.drawnShapes();
        m_shapesRead = true;
    }
    return m_shapes;
}

void StickerCanvas::render(QSvgRenderer *renderer, QImage *image) const {
    const QSize pixels = (m_size * m_ratio).toSize();
    if (pixels.isEmpty()) {
        *image = QImage();
        return;
    }
    *image = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
    image->setDevicePixelRatio(m_ratio);
    image->fill(Qt::transparent);
    if (!renderer->isValid() || m_area.isEmpty()) return;
    QPainter painter(image);
    renderer->setViewBox(m_area);
    renderer->setAspectRatioMode(Qt::IgnoreAspectRatio);
    renderer->render(&painter, QRectF(toView(m_area.topLeft()), toView(m_area.bottomRight())));
}

void StickerCanvas::beginLayers(const QStringList &ids) {
    if (m_layers) return;
    SvgDocument rest;
    m_document.split(ids, &rest, &m_only);
    QSvgRenderer renderer(rest.data());
    render(&renderer, &m_restImage);
    showOnly(m_only);
    m_layers = true;
}

void StickerCanvas::endLayers() {
    m_layers = false;
    m_restImage = QImage();
    m_only = SvgDocument();
    m_onlyLive = SvgDocument();
}

void StickerCanvas::showOnly(const SvgDocument &only) {
    m_onlyLive = only;
    m_onlyRenderer->load(only.data());
}

void StickerCanvas::flush() {
    m_nudgeTimer->stop();
    if (m_nudge.isIdentity()) return;
    const QTransform nudge = m_nudge;
    m_nudge = QTransform();
    endLayers();
    if (shapesTransformed) shapesTransformed(m_selection, nudge, true);
    requestUpdate();
}

void StickerCanvas::setDocument(const SvgDocument &document) {
    m_document = document;
    m_renderer->load(document.data());
    m_frame = document.viewBox();
    m_shapesRead = false;  // read again on first need: a recolour in Frame mode does not pay for it
    QStringList kept;
    if (!m_selection.isEmpty()) {
        const QList<SvgDocument::Shape> &all = shapes();
        for (const QString &id : std::as_const(m_selection))
            if (std::any_of(all.cbegin(), all.cend(), [&](const SvgDocument::Shape &s) { return s.id == id; })) kept.append(id);
    }
    const bool selectionChanges = kept != m_selection;
    m_selection = kept;
    if (!m_pointShape.isEmpty()) {
        if (kept.contains(m_pointShape)) {
            m_path = document.path(m_pointShape);
            m_pathMatrix = document.shapeMatrix(m_pointShape);
            if (m_chosen.valid() && (m_chosen.sub >= m_path.size() || m_chosen.node > m_path[m_chosen.sub].segments.size())) m_chosen = {};
        } else {
            m_pointShape.clear();
            m_chosen = {};
        }
    }
    if (m_drag == Drag::None) updateArea();
    render(m_renderer.get(), &m_image);
    requestUpdate();
    if (selectionChanges && selectionChanged) selectionChanged();
    notifyState();
}

bool StickerCanvas::isValid() const { return m_renderer->isValid(); }

void StickerCanvas::setMode(Mode mode) {
    flush();
    if (mode != Mode::Pen) finishPen();
    m_mode = mode;
    if (mode != Mode::Shapes) editPoints(QString());
    requestUpdate();
    notifyState();
}

void StickerCanvas::setSelection(const QStringList &ids) {
    if (ids == m_selection) return;
    flush();
    m_selection = ids;
    if (!m_pointShape.isEmpty() && !ids.contains(m_pointShape)) editPoints(QString());
    requestUpdate();
    if (selectionChanged) selectionChanged();
}

void StickerCanvas::editPoints(const QString &id) {
    m_pointShape = id;
    m_chosen = {};
    if (!id.isEmpty()) {
        m_path = m_document.path(id);
        m_pathMatrix = m_document.shapeMatrix(id);
        if (!m_selection.contains(id) || m_selection.size() != 1) setSelection({id});
    }
    requestUpdate();
    notifyState();
}

void StickerCanvas::updateArea() {
    const QRectF box = m_document.viewBox();
    const QRectF area = box.adjusted(-box.width() * kRoomAround, -box.height() * kRoomAround, box.width() * kRoomAround, box.height() * kRoomAround);
    if (area == m_area) return;
    m_area = area;
    render(m_renderer.get(), &m_image);
}

QPointF StickerCanvas::toView(const QPointF &point) const {
    const qreal inset = m_metrics.inset;
    const QRectF target = QRectF(QPointF(0, 0), m_size).adjusted(inset, inset, -inset, -inset);
    const qreal scale = qMin(target.width() / m_area.width(), target.height() / m_area.height());
    const QPointF origin = target.center() - QPointF(m_area.width(), m_area.height()) * scale / 2;
    return origin + (point - m_area.topLeft()) * scale;
}

QPointF StickerCanvas::toDocument(const QPointF &point) const {
    const qreal inset = m_metrics.inset;
    const QRectF target = QRectF(QPointF(0, 0), m_size).adjusted(inset, inset, -inset, -inset);
    const qreal scale = qMin(target.width() / m_area.width(), target.height() / m_area.height());
    const QPointF origin = target.center() - QPointF(m_area.width(), m_area.height()) * scale / 2;
    return m_area.topLeft() + (point - origin) / scale;
}

qreal StickerCanvas::tolerance() const {
    const qreal shown = QLineF(toView(QPointF(0, 0)), toView(QPointF(1, 0))).length();  // view px per viewBox unit
    return shown > 0 ? m_metrics.pick / shown : 0;
}

int StickerCanvas::edgesAt(const QRectF &box, const QPointF &position) const {
    if (box.isEmpty()) return None;
    const QRectF frame(toView(box.topLeft()), toView(box.bottomRight()));
    const qreal reach = m_metrics.reach;
    int edges = None;
    const bool alongX = position.x() > frame.left() - reach && position.x() < frame.right() + reach;
    const bool alongY = position.y() > frame.top() - reach && position.y() < frame.bottom() + reach;
    if (alongY && std::abs(position.x() - frame.left()) <= reach) edges |= Left;
    if (alongY && std::abs(position.x() - frame.right()) <= reach) edges |= Right;
    if (alongX && std::abs(position.y() - frame.top()) <= reach) edges |= Top;
    if (alongX && std::abs(position.y() - frame.bottom()) <= reach) edges |= Bottom;
    return edges;
}

int StickerCanvas::handlesAt(const QRectF &box, const QPointF &position) const {
    if (box.isNull()) return None;
    const QRectF shown(toView(box.topLeft()), toView(box.bottomRight()));
    const qreal reach = m_metrics.reach;
    const QList<std::pair<QPointF, int>> handles{{shown.topLeft(), Left | Top},
                                                 {shown.topRight(), Right | Top},
                                                 {shown.bottomLeft(), Left | Bottom},
                                                 {shown.bottomRight(), Right | Bottom},
                                                 {QPointF(shown.center().x(), shown.top()), Top},
                                                 {QPointF(shown.center().x(), shown.bottom()), Bottom},
                                                 {QPointF(shown.left(), shown.center().y()), Left},
                                                 {QPointF(shown.right(), shown.center().y()), Right}};
    // The nearest one: on a thin selection the handles overlap.
    int best = None;
    qreal bestDistance = reach * 2;
    for (const auto &[at, edges] : handles) {
        if (std::abs(at.x() - position.x()) > reach || std::abs(at.y() - position.y()) > reach) continue;
        const qreal distance = QLineF(at, position).length();
        if (distance < bestDistance) {
            bestDistance = distance;
            best = edges;
        }
    }
    return best;
}

bool StickerCanvas::onLabel(const QPointF &position) const {
    const QRectF label = m_document.labelBounds();
    return !label.isEmpty() && QRectF(toView(label.topLeft()), toView(label.bottomRight())).contains(position);
}

QPointF StickerCanvas::pointPosition(const QList<SvgDocument::SubPath> &path, const PointRef &ref) const {
    if (!ref.valid() || ref.sub >= path.size()) return {};
    const SvgDocument::SubPath &sub = path[ref.sub];
    if (ref.node < 0 || ref.node > sub.segments.size()) return {};
    if (ref.handle < 0) return sub.segments[ref.node - 1].c2;
    if (ref.handle > 0) return sub.segments[ref.node].c1;
    return ref.node == 0 ? sub.start : sub.segments[ref.node - 1].end;
}

// A node carries its handles with it; the start of a closed sub-path is also the end of its last
// curve when that curve comes back to it.
void StickerCanvas::movePoint(QList<SvgDocument::SubPath> &path, const PointRef &ref, const QPointF &to) const {
    SvgDocument::SubPath &sub = path[ref.sub];
    if (ref.handle < 0) {
        sub.segments[ref.node - 1].c2 = to;
        return;
    }
    if (ref.handle > 0) {
        sub.segments[ref.node].c1 = to;
        return;
    }
    const QPointF delta = to - pointPosition(path, ref);
    auto shiftIn = [&](int segment) {
        sub.segments[segment].end += delta;
        sub.segments[segment].c2 += delta;
    };
    if (ref.node == 0) {
        sub.start += delta;
        if (sub.closed && !sub.segments.isEmpty() && sub.segments.last().curve) shiftIn(int(sub.segments.size()) - 1);
    } else {
        shiftIn(ref.node - 1);
    }
    if (ref.node < sub.segments.size()) sub.segments[ref.node].c1 += delta;
}

StickerCanvas::PointRef StickerCanvas::pointAt(const QPointF &position) const {
    if (m_pointShape.isEmpty()) return {};
    const qreal reach = m_metrics.pick + m_metrics.handle / 2;
    PointRef best;
    qreal bestDistance = reach;
    for (int s = 0; s < m_path.size(); ++s) {
        const SvgDocument::SubPath &sub = m_path[s];
        for (int n = 0; n <= sub.segments.size(); ++n) {
            for (int handle : {0, -1, 1}) {
                if (handle < 0 && (n == 0 || !sub.segments[n - 1].curve)) continue;
                if (handle > 0 && (n == sub.segments.size() || !sub.segments[n].curve)) continue;
                const PointRef ref{s, n, handle};
                const QPointF at = toView(m_pathMatrix.map(pointPosition(m_path, ref)));
                const qreal distance = QLineF(at, position).length();
                if (distance < bestDistance || (distance <= bestDistance && handle == 0 && best.handle != 0)) {
                    bestDistance = distance;
                    best = ref;
                }
            }
        }
    }
    return best;
}

bool StickerCanvas::insertPointAt(const QPointF &position) {
    // The segment nearest to the pointer, sampled; split there (de Casteljau for a curve).
    int bestSub = -1, bestSegment = -1;
    qreal bestT = 0, bestDistance = m_metrics.pick;
    for (int s = 0; s < m_path.size(); ++s) {
        const SvgDocument::SubPath &sub = m_path[s];
        QPointF from = sub.start;
        for (int i = 0; i < sub.segments.size(); ++i) {
            const SvgDocument::Segment &seg = sub.segments[i];
            for (int step = 1; step < 32; ++step) {
                const qreal t = step / 32.0, u = 1 - t;
                const QPointF p = seg.curve ? u * u * u * from + 3 * u * u * t * seg.c1 + 3 * u * t * t * seg.c2 + t * t * t * seg.end
                                            : from + (seg.end - from) * t;
                const qreal distance = QLineF(toView(m_pathMatrix.map(p)), position).length();
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestSub = s;
                    bestSegment = i;
                    bestT = t;
                }
            }
            from = seg.end;
        }
    }
    if (bestSub < 0) return false;
    SvgDocument::SubPath &sub = m_path[bestSub];
    const QPointF from = bestSegment == 0 ? sub.start : sub.segments[bestSegment - 1].end;
    const SvgDocument::Segment seg = sub.segments[bestSegment];
    SvgDocument::Segment first = seg, second = seg;
    if (seg.curve) {
        auto lerp = [&](const QPointF &a, const QPointF &b) { return a + (b - a) * bestT; };
        const QPointF a = lerp(from, seg.c1), b = lerp(seg.c1, seg.c2), c = lerp(seg.c2, seg.end);
        const QPointF d = lerp(a, b), e = lerp(b, c), split = lerp(d, e);
        first = {true, a, d, split};
        second = {true, e, c, seg.end};
    } else {
        first.end = from + (seg.end - from) * bestT;
    }
    sub.segments[bestSegment] = first;
    sub.segments.insert(bestSegment + 1, second);
    m_chosen = {bestSub, bestSegment + 1, 0};
    if (pathChanged) pathChanged(m_pointShape, m_path);
    notifyState();
    return true;
}

void StickerCanvas::removeChosenPoint() {
    if (!m_chosen.valid() || m_chosen.handle != 0) return;
    QList<SvgDocument::SubPath> path = m_path;
    SvgDocument::SubPath &sub = path[m_chosen.sub];
    const int n = m_chosen.node;
    if (n == 0) {
        sub.start = sub.segments.first().end;
        sub.segments.removeFirst();
    } else if (n == sub.segments.size()) {
        sub.segments.removeLast();
    } else {
        // The two segments around the point become one: a curve if either was one.
        const SvgDocument::Segment in = sub.segments[n - 1], out = sub.segments[n];
        SvgDocument::Segment merged{in.curve || out.curve, in.curve ? in.c1 : in.end, out.curve ? out.c2 : out.end, out.end};
        if (merged.curve && !in.curve) merged.c1 = n - 1 == 0 ? sub.start : sub.segments[n - 2].end;
        sub.segments[n - 1] = merged;
        sub.segments.removeAt(n);
    }
    if (sub.segments.isEmpty()) path.removeAt(m_chosen.sub);
    m_chosen = {};
    if (path.isEmpty()) {
        const QString id = m_pointShape;
        editPoints(QString());
        if (removeRequested) removeRequested({id});
    } else if (pathChanged) {
        pathChanged(m_pointShape, path);
    }
    notifyState();
}

QTransform StickerCanvas::resizeMatrix(const QPointF &at, bool keepRatio) const {
    const QRectF box = m_selectionBox;
    if (box.width() <= 0 && box.height() <= 0) return {};
    // The opposite side stays where it is.
    const qreal anchorX = (m_edges & Left) ? box.right() : box.left();
    const qreal anchorY = (m_edges & Top) ? box.bottom() : box.top();
    qreal sx = 1, sy = 1;
    if ((m_edges & (Left | Right)) && box.width() > 0) sx = ((m_edges & Left) ? anchorX - at.x() : at.x() - anchorX) / box.width();
    if ((m_edges & (Top | Bottom)) && box.height() > 0) sy = ((m_edges & Top) ? anchorY - at.y() : at.y() - anchorY) / box.height();
    const qreal minimum = 0.02;  // never flat: 2% of the size at the press
    sx = qMax(minimum, sx);
    sy = qMax(minimum, sy);
    if (keepRatio) {
        const qreal s = (m_edges & (Left | Right)) && (m_edges & (Top | Bottom)) ? qMax(sx, sy) : ((m_edges & (Left | Right)) ? sx : sy);
        sx = sy = s;
    }
    QTransform m;
    m.translate(anchorX, anchorY);
    m.scale(sx, sy);
    m.translate(-anchorX, -anchorY);
    return m;
}

void StickerCanvas::setToolOptions(qreal cornerRadius, int sides, int points) {
    m_cornerRadius = cornerRadius;
    m_sides = sides;
    m_points = points;
}

QPointF StickerCanvas::snapped(const QPointF &from, const QPointF &to, bool constrained) const {
    if (!constrained || from == to) return to;
    const QLineF line(from, to);
    const qreal angle = std::round(line.angle() / 45) * 45;  // QLineF angles run counter-clockwise
    QLineF result = QLineF::fromPolar(line.length(), angle);
    return from + result.p2();
}

QPainterPath StickerCanvas::toolShape(const QPointF &from, const QPointF &to, bool constrained) const {
    QPainterPath path;
    if (m_mode == Mode::Rectangle || m_mode == Mode::Ellipse) {
        QPointF corner = to;
        if (constrained) {  // a square, a circle
            const qreal side = qMax(std::abs(to.x() - from.x()), std::abs(to.y() - from.y()));
            corner = from + QPointF(to.x() < from.x() ? -side : side, to.y() < from.y() ? -side : side);
        }
        const QRectF box = QRectF(from, corner).normalized();
        if (m_mode == Mode::Ellipse) path.addEllipse(box);
        else if (m_cornerRadius > 0) path.addRoundedRect(box, qMin(m_cornerRadius, box.width() / 2), qMin(m_cornerRadius, box.height() / 2));
        else path.addRect(box);
    } else if (m_mode == Mode::Line) {
        path.moveTo(from);
        path.lineTo(snapped(from, to, constrained));
    } else if (m_mode == Mode::Polygon || m_mode == Mode::Star) {
        // From the centre: the drag gives the size and the turn; Shift stands it upright.
        const qreal radius = QLineF(from, to).length();
        const qreal start = constrained ? -M_PI / 2 : std::atan2(to.y() - from.y(), to.x() - from.x());
        const bool star = m_mode == Mode::Star;
        const int corners = star ? 2 * m_points : m_sides;
        for (int i = 0; i < corners; ++i) {
            const qreal r = star && i % 2 ? radius * kStarInner : radius;
            const qreal a = start + i * 2 * M_PI / corners;
            const QPointF point = from + QPointF(std::cos(a), std::sin(a)) * r;
            if (i == 0) path.moveTo(point);
            else path.lineTo(point);
        }
        path.closeSubpath();
    }
    return path;
}

QPainterPath StickerCanvas::penOutline(bool withRubber) const {
    QPainterPath path(m_pen.start);
    for (const SvgDocument::Segment &segment : m_pen.segments)
        segment.curve ? path.cubicTo(segment.c1, segment.c2, segment.end) : path.lineTo(segment.end);
    if (withRubber) {
        if (m_hasPenOut) path.cubicTo(m_penOut, m_penMouse, m_penMouse);
        else path.lineTo(m_penMouse);
    }
    return path;
}

void StickerCanvas::finishPen(bool close) {
    if (!m_penActive) return;
    m_penActive = false;
    SvgDocument::SubPath path = m_pen;
    path.closed = close;
    m_pen = {};
    m_hasPenOut = false;
    requestUpdate();
    if (!path.segments.isEmpty() && shapeDrawn) shapeDrawn({path});
    notifyState();
}

void StickerCanvas::cancelPen() {
    if (!m_penActive) return;
    m_penActive = false;
    m_pen = {};
    m_hasPenOut = false;
    requestUpdate();
    notifyState();
}

void StickerCanvas::removeLastPenPoint() {
    if (!m_penActive) return;
    if (m_pen.segments.isEmpty()) {
        cancelPen();
        return;
    }
    m_pen.segments.removeLast();
    m_hasPenOut = false;
    // The rubber line starts again from the point that is now the last one.
    m_penMouse = m_pen.segments.isEmpty() ? m_pen.start : m_pen.segments.last().end;
    requestUpdate();
    notifyState();
}

void StickerCanvas::paint(QPainter *painterPointer, const Colors &colors) const {
    QPainter &painter = *painterPointer;
    const QRectF bounds(QPointF(0, 0), m_size);
    painter.fillRect(bounds, colors.raised);
    for (int y = 0; y < m_size.height(); y += kCheckerPx)
        for (int x = (y / kCheckerPx) % 2 * kCheckerPx; x < m_size.width(); x += 2 * kCheckerPx)
            painter.fillRect(QRectF(x, y, kCheckerPx, kCheckerPx), colors.sunken);
    if (!m_renderer->isValid() || m_area.isEmpty()) return;
    QTransform view;  // viewBox units -> view
    {
        const QPointF origin = toView(QPointF(0, 0)), unit = toView(QPointF(1, 1)) - origin;
        view = QTransform(unit.x(), 0, 0, unit.y(), origin.x(), origin.y());
    }
    // A move, resize or nudge not written yet; the file is written on release (or after a pause).
    const QTransform pending = (m_drag == Drag::Move || m_drag == Drag::Resize) ? m_dragMatrix : m_nudge;
    if (m_layers) {
        // The still part as drawn at the press, the moving part redrawn on top (it may come forward
        // while dragging; the file keeps the order).
        painter.drawImage(QPointF(0, 0), m_restImage);
        painter.save();
        painter.setWorldTransform(view.inverted() * pending * view, true);
        m_onlyRenderer->setViewBox(m_area);
        m_onlyRenderer->setAspectRatioMode(Qt::IgnoreAspectRatio);
        m_onlyRenderer->render(&painter, QRectF(toView(m_area.topLeft()), toView(m_area.bottomRight())));
        painter.restore();
    } else {
        painter.drawImage(QPointF(0, 0), m_image);
    }
    painter.setRenderHint(QPainter::Antialiasing);
    // Outside the frame: veiled, it is cut away.
    const QRectF frame(toView(m_frame.topLeft()), toView(m_frame.bottomRight()));
    QPainterPath veil;
    veil.addRect(bounds);
    veil.addRect(frame);
    QColor scrim = colors.surface;
    scrim.setAlpha(150);
    painter.fillPath(veil, scrim);
    const QColor accent = colors.accent;
    const qreal h = m_metrics.handle;
    auto handles = [&](const QRectF &box) {
        painter.setBrush(accent);
        painter.setPen(Qt::NoPen);
        for (const QPointF &handle : {box.topLeft(), box.topRight(), box.bottomLeft(), box.bottomRight(), QPointF(box.center().x(), box.top()),
                                      QPointF(box.center().x(), box.bottom()), QPointF(box.left(), box.center().y()),
                                      QPointF(box.right(), box.center().y())})
            painter.drawRect(QRectF(handle - QPointF(h / 2, h / 2), QSizeF(h, h)));
    };
    painter.setPen(QPen(accent, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(frame);
    if (m_mode == Mode::Frame) handles(frame);

    if (m_mode == Mode::Shapes) {
        const QTransform &drag = pending;
        painter.setPen(QPen(accent, 1));
        painter.setBrush(Qt::NoBrush);
        if (!m_selection.isEmpty())
            for (const SvgDocument::Shape &shape : shapes())
                if (m_selection.contains(shape.id) && shape.id != m_pointShape) painter.drawPath((view.map(drag.map(shape.outline))));
        if (!m_selection.isEmpty() && m_pointShape.isEmpty()) {
            const QRectF box = drag.mapRect(SvgDocument::bounds(shapes(), m_selection));
            const QRectF shown(toView(box.topLeft()), toView(box.bottomRight()));
            painter.setPen(QPen(accent, 1, Qt::DashLine));
            painter.drawRect(shown);
            handles(shown);
        }
        if (!m_pointShape.isEmpty()) {
            const QList<SvgDocument::SubPath> &path = m_drag == Drag::Point ? m_livePath : m_path;
            const QTransform toPoints = m_pathMatrix * view;
            QPainterPath outline;
            for (const SvgDocument::SubPath &sub : path) {
                outline.moveTo(sub.start);
                for (const SvgDocument::Segment &seg : sub.segments)
                    seg.curve ? outline.cubicTo(seg.c1, seg.c2, seg.end) : outline.lineTo(seg.end);
                if (sub.closed) outline.closeSubpath();
            }
            painter.setPen(QPen(accent, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(toPoints.map(outline));
            for (int s = 0; s < path.size(); ++s) {
                const SvgDocument::SubPath &sub = path[s];
                for (int n = 0; n <= sub.segments.size(); ++n) {
                    const PointRef node{s, n, 0};
                    const QPointF at = toPoints.map(pointPosition(path, node));
                    // Handles of the chosen point and of its neighbours' segments.
                    const bool beside = m_chosen.sub == s && std::abs(m_chosen.node - n) <= 1;  // not "near": a Windows macro
                    for (int handle : {-1, 1}) {
                        if (!beside) break;
                        if (handle < 0 && (n == 0 || !sub.segments[n - 1].curve)) continue;
                        if (handle > 0 && (n == sub.segments.size() || !sub.segments[n].curve)) continue;
                        const QPointF control = toPoints.map(pointPosition(path, {s, n, handle}));
                        painter.setPen(QPen(accent, 1));
                        painter.drawLine(at, control);
                        painter.setBrush(colors.surface);
                        painter.drawEllipse(control, h / 2, h / 2);
                    }
                    if (sub.closed && n == sub.segments.size() && n > 0 && sub.segments.last().curve) continue;  // the start again
                    painter.setPen(QPen(accent, 1));
                    painter.setBrush(node == m_chosen ? accent : colors.surface);
                    painter.drawRect(QRectF(at - QPointF(h / 2, h / 2), QSizeF(h, h)));
                }
            }
        }
        if (m_drag == Drag::Rubber) {
            painter.setPen(QPen(accent, 1, Qt::DashLine));
            QColor fill = accent;
            fill.setAlpha(40);
            painter.setBrush(fill);
            painter.drawRect(QRectF(toView(m_rubber.topLeft()), toView(m_rubber.bottomRight())));
        }
    }
    if (m_drag == Drag::Draw && !m_drawing.isEmpty()) {
        painter.setPen(QPen(accent, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(view.map(m_drawing));
    }
    if (m_penActive) {
        painter.setPen(QPen(accent, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(view.map(penOutline(m_drag == Drag::None)));
        QList<QPointF> nodes{m_pen.start};
        for (const SvgDocument::Segment &segment : std::as_const(m_pen.segments)) nodes.append(segment.end);
        for (int i = 0; i < nodes.size(); ++i) {
            const QPointF at = view.map(nodes[i]);
            painter.setBrush(i == 0 ? accent : colors.surface);  // the first point closes
            painter.drawRect(QRectF(at - QPointF(h / 2, h / 2), QSizeF(h, h)));
        }
        if (m_hasPenOut) {
            const QPointF node = view.map(nodes.last()), handle = view.map(m_penOut);
            painter.drawLine(node, handle);
            painter.setBrush(colors.surface);
            painter.drawEllipse(handle, h / 2, h / 2);
        }
    }
    const QRectF label = (m_drag == Drag::Label && m_layers ? m_onlyLive : m_document).labelBounds();
    if (!label.isEmpty()) {
        painter.setPen(QPen(accent, 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(toView(label.topLeft()), toView(label.bottomRight())));
    }
}

void StickerCanvas::press(const QPointF &position, bool shift) {
    m_pressAt = toDocument(position);
    flush();  // arrow moves first, as their own step
    m_drag = Drag::None;
    m_dragMatrix = QTransform();
    if (m_mode == Mode::Pen) {
        // A click adds a corner, a drag from it pulls its handles (a curve); a click on the first
        // point closes the path.
        QPointF at = m_pressAt;
        const bool starts = !m_penActive;
        if (starts) {
            m_pen = {at, {}, false};
            m_penActive = true;
            m_hasPenOut = false;
        } else {
            if (!m_pen.segments.isEmpty() && QLineF(toView(m_pen.start), position).length() <= m_metrics.pick + m_metrics.handle / 2) {
                finishPen(true);
                return;
            }
            const QPointF from = m_pen.segments.isEmpty() ? m_pen.start : m_pen.segments.last().end;
            at = snapped(from, at, shift);
            m_pen.segments.append({m_hasPenOut, m_hasPenOut ? m_penOut : QPointF(), at, at});
            m_hasPenOut = false;
        }
        m_pressAt = at;
        m_penMouse = at;
        m_drag = Drag::PenHandle;
        requestUpdate();
        notifyState();
        return;
    }
    if (draws(m_mode)) {
        m_drawing = QPainterPath();
        m_drag = Drag::Draw;
        return;
    }
    if (m_mode == Mode::Frame) {
        m_edges = edgesAt(m_frame, position);
        if (m_edges != None) m_drag = Drag::FrameEdges;
        else if (onLabel(position)) m_drag = Drag::Label;
        m_labelAt = m_document.label().position;
        return;
    }
    if (!m_pointShape.isEmpty()) {
        const PointRef ref = pointAt(position);
        if (ref.valid()) {
            m_chosen = ref;
            m_livePath = m_path;
            m_drag = Drag::Point;
            requestUpdate();
            notifyState();
            return;
        }
        if (insertPointAt(position)) return;
        editPoints(QString());  // a click away from the points ends the point editing
    }
    const QRectF box = SvgDocument::bounds(shapes(), m_selection);
    m_edges = m_selection.isEmpty() ? None : handlesAt(box, position);  // only the 8 handles resize
    if (m_edges != None) {
        m_selectionBox = box;
        m_drag = Drag::Resize;
        return;
    }
    if (onLabel(position)) {
        m_labelAt = m_document.label().position;
        m_drag = Drag::Label;
        return;
    }
    const QString hit = SvgDocument::shapeAt(shapes(), m_pressAt, tolerance());
    m_selectionAtPress = m_selection;
    if (hit.isEmpty()) {
        if (!shift) setSelection({});
        m_rubber = QRectF(m_pressAt, m_pressAt);
        m_drag = Drag::Rubber;
        return;
    }
    m_clickedSelected = m_selection.contains(hit);
    if (!m_clickedSelected) setSelection(shift ? m_selection + QStringList{hit} : QStringList{hit});
    m_drag = Drag::Move;
}

void StickerCanvas::doubleClick(const QPointF &position) {
    if (m_mode == Mode::Pen) {
        m_drag = Drag::None;
        finishPen();
        return;
    }
    if (m_mode != Mode::Shapes) return;
    const QString hit = SvgDocument::shapeAt(shapes(), toDocument(position), tolerance());
    m_drag = Drag::None;
    if (!hit.isEmpty() && hit != m_pointShape) editPoints(hit);
}

Qt::CursorShape StickerCanvas::cursorAt(const QPointF &position) const {
    if (draws(m_mode)) return Qt::CrossCursor;
    int edges = None;
    if (m_mode == Mode::Frame) edges = edgesAt(m_frame, position);
    else if (m_pointShape.isEmpty() && !m_selection.isEmpty()) edges = handlesAt(SvgDocument::bounds(shapes(), m_selection), position);
    const bool horizontal = edges & (Left | Right), vertical = edges & (Top | Bottom);
    const bool falling = ((edges & Left) && (edges & Top)) || ((edges & Right) && (edges & Bottom));
    if (horizontal && vertical) return falling ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
    if (horizontal) return Qt::SizeHorCursor;
    if (vertical) return Qt::SizeVerCursor;
    if (onLabel(position) || (m_mode == Mode::Shapes && pointAt(position).valid())) return Qt::SizeAllCursor;
    return Qt::ArrowCursor;
}

void StickerCanvas::move(const QPointF &position, bool shift) {
    if (m_drag == Drag::None) {
        if (m_penActive) {  // the next segment follows the mouse
            const QPointF from = m_pen.segments.isEmpty() ? m_pen.start : m_pen.segments.last().end;
            m_penMouse = snapped(from, toDocument(position), shift);
            requestUpdate();
        }
        return;
    }
    const QPointF at = toDocument(position);
    switch (m_drag) {
    case Drag::Label: {
        SvgDocument::Label label = m_document.label();
        label.position = m_labelAt + (at - m_pressAt);
        beginLayers({kLabelId});
        SvgDocument moved = m_only;
        moved.setLabel(label);
        showOnly(moved);
        break;
    }
    case Drag::FrameEdges: {
        QRectF frame = m_document.viewBox();
        const qreal minimum = qMax(frame.width(), frame.height()) / 50;  // never narrower than 2% of the longer side
        if (m_edges & Left) frame.setLeft(qMin(at.x(), frame.right() - minimum));
        if (m_edges & Right) frame.setRight(qMax(at.x(), frame.left() + minimum));
        if (m_edges & Top) frame.setTop(qMin(at.y(), frame.bottom() - minimum));
        if (m_edges & Bottom) frame.setBottom(qMax(at.y(), frame.top() + minimum));
        m_frame = frame;
        break;
    }
    case Drag::Move:
    case Drag::Resize: {
        QTransform matrix;
        if (m_drag == Drag::Move) matrix.translate(at.x() - m_pressAt.x(), at.y() - m_pressAt.y());
        else matrix = resizeMatrix(at, shift);
        m_dragMatrix = matrix;
        beginLayers(m_selection);
        break;
    }
    case Drag::Rubber: {
        m_rubber = QRectF(m_pressAt, at).normalized();
        QStringList ids = shift ? m_selectionAtPress : QStringList();
        for (const SvgDocument::Shape &shape : shapes())
            if (m_rubber.intersects(shape.outline.boundingRect()) && !ids.contains(shape.id)) ids.append(shape.id);
        setSelection(ids);
        break;
    }
    case Drag::Point: {
        bool invertible = false;
        const QTransform inverse = m_pathMatrix.inverted(&invertible);
        if (!invertible) break;
        m_livePath = m_path;
        movePoint(m_livePath, m_chosen, inverse.map(at));
        beginLayers({m_pointShape});
        SvgDocument moved = m_only;
        moved.setPath(m_pointShape, m_livePath);
        showOnly(moved);
        break;
    }
    case Drag::Draw:
        m_drawing = toolShape(m_pressAt, at, shift);
        break;
    case Drag::PenHandle: {
        if (QLineF(toView(at), toView(m_pressAt)).length() < 2) break;
        // The point's handles: outgoing where the pointer is, incoming mirrored (a smooth point).
        m_penOut = at;
        m_hasPenOut = true;
        if (!m_pen.segments.isEmpty()) {
            SvgDocument::Segment &last = m_pen.segments.last();
            if (!last.curve) {
                last.curve = true;
                last.c1 = m_pen.segments.size() > 1 ? m_pen.segments[m_pen.segments.size() - 2].end : m_pen.start;
            }
            last.c2 = 2 * m_pressAt - at;
        }
        m_penMouse = m_pressAt;
        break;
    }
    case Drag::None: break;
    }
    requestUpdate();
}

void StickerCanvas::release(const QPointF &position, bool shift) {
    if (m_drag == Drag::None) return;
    const Drag drag = m_drag;
    m_drag = Drag::None;
    const bool moved = QLineF(toView(toDocument(position)), toView(m_pressAt)).length() >= 1;
    switch (drag) {
    case Drag::Label: {
        const QPointF labelAt = m_layers ? m_onlyLive.label().position : m_document.label().position;
        if (labelAt != m_document.label().position && labelMoved) labelMoved(labelAt);
        break;
    }
    case Drag::FrameEdges:
        if (m_frame != m_document.viewBox() && frameChanged) frameChanged(m_frame);
        break;
    case Drag::Move:
    case Drag::Resize:
        if (moved && !m_dragMatrix.isIdentity() && shapesTransformed) {
            shapesTransformed(m_selection, m_dragMatrix, false);
        } else if (!moved && drag == Drag::Move && m_clickedSelected) {
            // A click on a selected shape: Shift removes it, a plain click keeps only it.
            const QString hit = SvgDocument::shapeAt(shapes(), m_pressAt, tolerance());
            QStringList ids = m_selection;
            if (shift) ids.removeAll(hit);
            else ids = {hit};
            setSelection(ids);
        }
        break;
    case Drag::Point:
        if (moved && pathChanged) pathChanged(m_pointShape, m_livePath);
        break;
    case Drag::Draw: {
        const QRectF box = m_drawing.boundingRect();
        const QPainterPath drawn = m_drawing;
        m_drawing = QPainterPath();
        // A click without a drag draws nothing.
        if (QLineF(toView(box.topLeft()), toView(box.bottomRight())).length() >= 3 && shapeDrawn) shapeDrawn(SvgDocument::subPaths(drawn));
        break;
    }
    case Drag::PenHandle:
    case Drag::Rubber:
    case Drag::None: break;
    }
    m_dragMatrix = QTransform();
    endLayers();
    updateArea();
    requestUpdate();
}

// Delete, Backspace, the arrows and Esc belong to the preview while it has the focus: the main
// window's shortcuts (Delete removes topics) must not take them.
bool StickerCanvas::claimsKey(int key) const {
    if (m_penActive && (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape || key == Qt::Key_Backspace || key == Qt::Key_Delete))
        return true;
    if (m_mode != Mode::Shapes) return false;
    return key == Qt::Key_Delete || key == Qt::Key_Backspace || key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up ||
           key == Qt::Key_Down || (key == Qt::Key_Escape && (!m_pointShape.isEmpty() || !m_selection.isEmpty()));
}

bool StickerCanvas::key(int key, bool shift) {
    if (m_penActive) {
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            finishPen();
            return true;
        }
        if (key == Qt::Key_Escape) {
            cancelPen();
            return true;
        }
        if (key == Qt::Key_Backspace || key == Qt::Key_Delete) {  // the last point
            removeLastPenPoint();
            return true;
        }
    }
    if (m_mode != Mode::Shapes) return false;
    const bool arrow = key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;
    // Shift held for larger steps (or Ctrl, Alt) is not another key: the arrow moves keep waiting.
    const bool modifier = key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta;
    if (modifier) return true;
    if (!arrow) flush();
    if (key == Qt::Key_Escape && (!m_pointShape.isEmpty() || !m_selection.isEmpty())) {
        // Esc ends the point editing, then empties the selection; only then it closes the editor.
        if (!m_pointShape.isEmpty()) editPoints(QString());
        else setSelection({});
        return true;
    }
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
        if (!m_pointShape.isEmpty()) removeChosenPoint();
        else if (!m_selection.isEmpty() && removeRequested) removeRequested(m_selection);
        return true;
    }
    QPointF step;
    if (key == Qt::Key_Left) step = QPointF(-1, 0);
    else if (key == Qt::Key_Right) step = QPointF(1, 0);
    else if (key == Qt::Key_Up) step = QPointF(0, -1);
    else if (key == Qt::Key_Down) step = QPointF(0, 1);
    if (!step.isNull() && !m_selection.isEmpty() && m_pointShape.isEmpty()) {
        // 1% of the frame's longer side, 10% with Shift.
        const QRectF box = m_document.viewBox();
        const qreal amount = qMax(box.width(), box.height()) * (shift ? 0.1 : 0.01);
        // Shown at once on the layers, written after a pause (kNudgePauseMs) as one step.
        beginLayers(m_selection);
        m_nudge = m_nudge * QTransform::fromTranslate(step.x() * amount, step.y() * amount);
        m_nudgeTimer->start();
        requestUpdate();
        return true;
    }
    return false;
}
