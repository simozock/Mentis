#include "stickereditor.h"
#include "help.h"

#include "nodeitem.h"
#include "segmentedcontrol.h"
#include "numberfield.h"
#include "stylecontrols.h"
#include "uitheme.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QFile>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStackedWidget>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <memory>
#include <tuple>

namespace {
constexpr int kPreviewInsetPx = 16;
constexpr int kCheckerPx = 8;  // as the export preview (FEAT-054)
constexpr int kSwatchPx = 32;
constexpr int kSwatchColumns = 8;
constexpr int kNumberFieldPx = 96;  // as the Export dialog's
constexpr int kTextFieldPx = 200;
constexpr qreal kRoomAround = 0.2;  // the preview shows this share of the frame on every side
constexpr int kDefaultLabelSizePct = 25;
constexpr int kNudgePauseMs = 400;  // arrow moves within this pause make one step
constexpr int kTypingPauseMs = 400;
constexpr qsizetype kLargeBytes = 256 * 1024;      // from here typing waits for a pause
constexpr qsizetype kHistoryBytes = 64 * 1024 * 1024;
const QString kLabelId = QStringLiteral("mentis-label");  // SvgDocument's label element
constexpr qreal kStarInner = 0.5;  // a star's inner points, a share of its radius

// The label's fonts: the ones bundled with the app, so the sticker looks the same everywhere in it.
QStringList labelFamilies() {
    NodeItem::loadBundledFonts();
    QStringList result;
    for (const QString &family : {QStringLiteral("Atkinson Hyperlegible"), QStringLiteral("Atkinson Hyperlegible Next"), QStringLiteral("Roboto"),
                                  QStringLiteral("Montserrat"), QStringLiteral("Arvo"), QStringLiteral("OpenDyslexic")})
        if (QFontDatabase::hasFamily(family)) result.append(family);
    return result;
}

qreal normalizedAngle(qreal degrees) {
    qreal angle = std::fmod(degrees, 360.0);
    if (angle > 180) angle -= 360;
    if (angle <= -180) angle += 360;
    return angle;
}

QString swatchStyle(const QColor &color) {
    return UiTheme::qss(QStringLiteral("QToolButton { background:%1; border:1px solid @divider; border-radius:8px; }"
                                       "QToolButton:hover, QToolButton:focus { border:2px solid @accent; }")
                            .arg(color.name()));
}
}  // namespace

// --- Preview -------------------------------------------------------------------------------------

StickerPreview::StickerPreview(QWidget *parent)
    : QWidget(parent), m_renderer(new QSvgRenderer(this)), m_onlyRenderer(new QSvgRenderer(this)), m_nudgeTimer(new QTimer(this)) {
    setFixedSize(kSizePx, kSizePx);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);  // arrows, Delete and Esc for the selection and the points
    // Held arrows: one step written after a pause, not one per key (each step rewrites the file).
    m_nudgeTimer->setSingleShot(true);
    m_nudgeTimer->setInterval(kNudgePauseMs);
    connect(m_nudgeTimer, &QTimer::timeout, this, &StickerPreview::flush);
}

const QList<SvgDocument::Shape> &StickerPreview::shapes() {
    if (!m_shapesRead) {
        m_shapes = m_document.drawnShapes();
        m_shapesRead = true;
    }
    return m_shapes;
}

void StickerPreview::render(QSvgRenderer *renderer, QImage *image) const {
    const qreal ratio = devicePixelRatioF();
    *image = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
    image->setDevicePixelRatio(ratio);
    image->fill(Qt::transparent);
    if (!renderer->isValid() || m_area.isEmpty()) return;
    QPainter painter(image);
    renderer->setViewBox(m_area);
    renderer->setAspectRatioMode(Qt::IgnoreAspectRatio);
    renderer->render(&painter, QRectF(toWidget(m_area.topLeft()), toWidget(m_area.bottomRight())));
}

void StickerPreview::beginLayers(const QStringList &ids) {
    if (m_layers) return;
    SvgDocument rest;
    m_document.split(ids, &rest, &m_only);
    QSvgRenderer renderer(rest.data());
    render(&renderer, &m_restImage);
    showOnly(m_only);
    m_layers = true;
}

void StickerPreview::endLayers() {
    m_layers = false;
    m_restImage = QImage();
    m_only = SvgDocument();
    m_onlyLive = SvgDocument();
}

void StickerPreview::showOnly(const SvgDocument &only) {
    m_onlyLive = only;
    m_onlyRenderer->load(only.data());
}

void StickerPreview::flush() {
    m_nudgeTimer->stop();
    if (m_nudge.isIdentity()) return;
    const QTransform nudge = m_nudge;
    m_nudge = QTransform();
    endLayers();
    if (shapesTransformed) shapesTransformed(m_selection, nudge, true);
    update();
}

void StickerPreview::setDocument(const SvgDocument &document) {
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
    render(m_renderer, &m_image);
    update();
    if (selectionChanges && selectionChanged) selectionChanged();
}

bool StickerPreview::isValid() const { return m_renderer->isValid(); }

void StickerPreview::setMode(Mode mode) {
    flush();
    if (mode != Mode::Pen) finishPen();
    m_mode = mode;
    if (mode != Mode::Shapes) editPoints(QString());
    unsetCursor();
    update();
}

void StickerPreview::setSelection(const QStringList &ids) {
    if (ids == m_selection) return;
    flush();
    m_selection = ids;
    if (!m_pointShape.isEmpty() && !ids.contains(m_pointShape)) editPoints(QString());
    update();
    if (selectionChanged) selectionChanged();
}

void StickerPreview::editPoints(const QString &id) {
    m_pointShape = id;
    m_chosen = {};
    if (!id.isEmpty()) {
        m_path = m_document.path(id);
        m_pathMatrix = m_document.shapeMatrix(id);
        if (!m_selection.contains(id) || m_selection.size() != 1) setSelection({id});
    }
    update();
}

void StickerPreview::updateArea() {
    const QRectF box = m_document.viewBox();
    const QRectF area = box.adjusted(-box.width() * kRoomAround, -box.height() * kRoomAround, box.width() * kRoomAround, box.height() * kRoomAround);
    if (area == m_area) return;
    m_area = area;
    render(m_renderer, &m_image);
}

QPointF StickerPreview::toWidget(const QPointF &point) const {
    const QRectF target = QRectF(rect()).adjusted(kPreviewInsetPx, kPreviewInsetPx, -kPreviewInsetPx, -kPreviewInsetPx);
    const qreal scale = qMin(target.width() / m_area.width(), target.height() / m_area.height());
    const QPointF origin = target.center() - QPointF(m_area.width(), m_area.height()) * scale / 2;
    return origin + (point - m_area.topLeft()) * scale;
}

QPointF StickerPreview::toDocument(const QPointF &point) const {
    const QRectF target = QRectF(rect()).adjusted(kPreviewInsetPx, kPreviewInsetPx, -kPreviewInsetPx, -kPreviewInsetPx);
    const qreal scale = qMin(target.width() / m_area.width(), target.height() / m_area.height());
    const QPointF origin = target.center() - QPointF(m_area.width(), m_area.height()) * scale / 2;
    return m_area.topLeft() + (point - origin) / scale;
}

int StickerPreview::edgesAt(const QRectF &box, const QPointF &position) const {
    if (box.isEmpty()) return None;
    const QRectF frame(toWidget(box.topLeft()), toWidget(box.bottomRight()));
    const qreal reach = kHandlePx;
    int edges = None;
    const bool alongX = position.x() > frame.left() - reach && position.x() < frame.right() + reach;
    const bool alongY = position.y() > frame.top() - reach && position.y() < frame.bottom() + reach;
    if (alongY && std::abs(position.x() - frame.left()) <= reach) edges |= Left;
    if (alongY && std::abs(position.x() - frame.right()) <= reach) edges |= Right;
    if (alongX && std::abs(position.y() - frame.top()) <= reach) edges |= Top;
    if (alongX && std::abs(position.y() - frame.bottom()) <= reach) edges |= Bottom;
    return edges;
}

int StickerPreview::handlesAt(const QRectF &box, const QPointF &position) const {
    if (box.isNull()) return None;
    const QRectF shown(toWidget(box.topLeft()), toWidget(box.bottomRight()));
    const qreal reach = kHandlePx;
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

bool StickerPreview::onLabel(const QPointF &position) const {
    const QRectF label = m_document.labelBounds();
    return !label.isEmpty() && QRectF(toWidget(label.topLeft()), toWidget(label.bottomRight())).contains(position);
}

QPointF StickerPreview::pointPosition(const QList<SvgDocument::SubPath> &path, const PointRef &ref) const {
    if (!ref.valid() || ref.sub >= path.size()) return {};
    const SvgDocument::SubPath &sub = path[ref.sub];
    if (ref.node < 0 || ref.node > sub.segments.size()) return {};
    if (ref.handle < 0) return sub.segments[ref.node - 1].c2;
    if (ref.handle > 0) return sub.segments[ref.node].c1;
    return ref.node == 0 ? sub.start : sub.segments[ref.node - 1].end;
}

// A node carries its handles with it; the start of a closed sub-path is also the end of its last
// curve when that curve comes back to it.
void StickerPreview::movePoint(QList<SvgDocument::SubPath> &path, const PointRef &ref, const QPointF &to) const {
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

StickerPreview::PointRef StickerPreview::pointAt(const QPointF &position) const {
    if (m_pointShape.isEmpty()) return {};
    const qreal reach = kPickPx + kHandlePx / 2;
    PointRef best;
    qreal bestDistance = reach;
    for (int s = 0; s < m_path.size(); ++s) {
        const SvgDocument::SubPath &sub = m_path[s];
        for (int n = 0; n <= sub.segments.size(); ++n) {
            for (int handle : {0, -1, 1}) {
                if (handle < 0 && (n == 0 || !sub.segments[n - 1].curve)) continue;
                if (handle > 0 && (n == sub.segments.size() || !sub.segments[n].curve)) continue;
                const PointRef ref{s, n, handle};
                const QPointF at = toWidget(m_pathMatrix.map(pointPosition(m_path, ref)));
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

bool StickerPreview::insertPointAt(const QPointF &position) {
    // The segment nearest to the mouse, sampled; split there (de Casteljau for a curve).
    int bestSub = -1, bestSegment = -1;
    qreal bestT = 0, bestDistance = kPickPx;
    for (int s = 0; s < m_path.size(); ++s) {
        const SvgDocument::SubPath &sub = m_path[s];
        QPointF from = sub.start;
        for (int i = 0; i < sub.segments.size(); ++i) {
            const SvgDocument::Segment &seg = sub.segments[i];
            for (int step = 1; step < 32; ++step) {
                const qreal t = step / 32.0, u = 1 - t;
                const QPointF p = seg.curve ? u * u * u * from + 3 * u * u * t * seg.c1 + 3 * u * t * t * seg.c2 + t * t * t * seg.end
                                            : from + (seg.end - from) * t;
                const qreal distance = QLineF(toWidget(m_pathMatrix.map(p)), position).length();
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
    return true;
}

void StickerPreview::removeChosenPoint() {
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
}

QTransform StickerPreview::resizeMatrix(const QPointF &at, bool keepRatio) const {
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

void StickerPreview::setToolOptions(qreal cornerRadius, int sides, int points) {
    m_cornerRadius = cornerRadius;
    m_sides = sides;
    m_points = points;
}

QPointF StickerPreview::snapped(const QPointF &from, const QPointF &to, bool constrained) const {
    if (!constrained || from == to) return to;
    const QLineF line(from, to);
    const qreal angle = std::round(line.angle() / 45) * 45;  // QLineF angles run counter-clockwise
    QLineF result = QLineF::fromPolar(line.length(), angle);
    return from + result.p2();
}

QPainterPath StickerPreview::toolShape(const QPointF &from, const QPointF &to, bool constrained) const {
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

QPainterPath StickerPreview::penOutline(bool withRubber) const {
    QPainterPath path(m_pen.start);
    for (const SvgDocument::Segment &segment : m_pen.segments)
        segment.curve ? path.cubicTo(segment.c1, segment.c2, segment.end) : path.lineTo(segment.end);
    if (withRubber) {
        if (m_hasPenOut) path.cubicTo(m_penOut, m_penMouse, m_penMouse);
        else path.lineTo(m_penMouse);
    }
    return path;
}

void StickerPreview::finishPen(bool close) {
    if (!m_penActive) return;
    m_penActive = false;
    SvgDocument::SubPath path = m_pen;
    path.closed = close;
    m_pen = {};
    m_hasPenOut = false;
    update();
    if (!path.segments.isEmpty() && shapeDrawn) shapeDrawn({path});
}

void StickerPreview::cancelPen() {
    m_penActive = false;
    m_pen = {};
    m_hasPenOut = false;
    update();
}

void StickerPreview::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), UiTheme::color(UiTheme::Token::SurfaceRaised));
    const QColor dark = UiTheme::color(UiTheme::Token::SurfaceSunken);
    for (int y = 0; y < height(); y += kCheckerPx)
        for (int x = (y / kCheckerPx) % 2 * kCheckerPx; x < width(); x += 2 * kCheckerPx)
            painter.fillRect(x, y, kCheckerPx, kCheckerPx, dark);
    if (!m_renderer->isValid() || m_area.isEmpty()) return;
    QTransform view;  // viewBox units -> widget
    {
        const QPointF origin = toWidget(QPointF(0, 0)), unit = toWidget(QPointF(1, 1)) - origin;
        view = QTransform(unit.x(), 0, 0, unit.y(), origin.x(), origin.y());
    }
    // A move, resize or nudge not written yet; the file is written on release (or after a pause).
    const QTransform pending = (m_drag == Drag::Move || m_drag == Drag::Resize) ? m_dragMatrix : m_nudge;
    if (m_layers) {
        // The still part as drawn at the press, the moving part redrawn on top (it may come forward
        // while dragging; the file keeps the order).
        painter.drawImage(QPointF(0, 0), m_restImage);
        painter.save();
        painter.setWorldTransform(view.inverted() * pending * view);
        m_onlyRenderer->setViewBox(m_area);
        m_onlyRenderer->setAspectRatioMode(Qt::IgnoreAspectRatio);
        m_onlyRenderer->render(&painter, QRectF(toWidget(m_area.topLeft()), toWidget(m_area.bottomRight())));
        painter.restore();
    } else {
        painter.drawImage(QPointF(0, 0), m_image);
    }
    painter.setRenderHint(QPainter::Antialiasing);
    // Outside the frame: veiled, it is cut away.
    const QRectF frame(toWidget(m_frame.topLeft()), toWidget(m_frame.bottomRight()));
    QPainterPath veil;
    veil.addRect(QRectF(rect()));
    veil.addRect(frame);
    QColor scrim = UiTheme::color(UiTheme::Token::Surface);
    scrim.setAlpha(150);
    painter.fillPath(veil, scrim);
    const QColor accent = UiTheme::color(UiTheme::Token::Accent);
    const qreal h = kHandlePx;
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
            const QRectF shown(toWidget(box.topLeft()), toWidget(box.bottomRight()));
            painter.setPen(QPen(accent, 1, Qt::DashLine));
            painter.drawRect(shown);
            handles(shown);
        }
        if (!m_pointShape.isEmpty()) {
            const QList<SvgDocument::SubPath> &path = m_drag == Drag::Point ? m_livePath : m_path;
            const QTransform toView = m_pathMatrix * view;
            QPainterPath outline;
            for (const SvgDocument::SubPath &sub : path) {
                outline.moveTo(sub.start);
                for (const SvgDocument::Segment &seg : sub.segments)
                    seg.curve ? outline.cubicTo(seg.c1, seg.c2, seg.end) : outline.lineTo(seg.end);
                if (sub.closed) outline.closeSubpath();
            }
            painter.setPen(QPen(accent, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(toView.map(outline));
            for (int s = 0; s < path.size(); ++s) {
                const SvgDocument::SubPath &sub = path[s];
                for (int n = 0; n <= sub.segments.size(); ++n) {
                    const PointRef node{s, n, 0};
                    const QPointF at = toView.map(pointPosition(path, node));
                    // Handles of the chosen point and of its neighbours' segments.
                    const bool beside = m_chosen.sub == s && std::abs(m_chosen.node - n) <= 1;  // not "near": a Windows macro
                    for (int handle : {-1, 1}) {
                        if (!beside) break;
                        if (handle < 0 && (n == 0 || !sub.segments[n - 1].curve)) continue;
                        if (handle > 0 && (n == sub.segments.size() || !sub.segments[n].curve)) continue;
                        const QPointF control = toView.map(pointPosition(path, {s, n, handle}));
                        painter.setPen(QPen(accent, 1));
                        painter.drawLine(at, control);
                        painter.setBrush(UiTheme::color(UiTheme::Token::Surface));
                        painter.drawEllipse(control, h / 2, h / 2);
                    }
                    if (sub.closed && n == sub.segments.size() && n > 0 && sub.segments.last().curve) continue;  // the start again
                    painter.setPen(QPen(accent, 1));
                    painter.setBrush(node == m_chosen ? accent : UiTheme::color(UiTheme::Token::Surface));
                    painter.drawRect(QRectF(at - QPointF(h / 2, h / 2), QSizeF(h, h)));
                }
            }
        }
        if (m_drag == Drag::Rubber) {
            painter.setPen(QPen(accent, 1, Qt::DashLine));
            QColor fill = accent;
            fill.setAlpha(40);
            painter.setBrush(fill);
            painter.drawRect(QRectF(toWidget(m_rubber.topLeft()), toWidget(m_rubber.bottomRight())));
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
            painter.setBrush(i == 0 ? accent : UiTheme::color(UiTheme::Token::Surface));  // the first point closes
            painter.drawRect(QRectF(at - QPointF(h / 2, h / 2), QSizeF(h, h)));
        }
        if (m_hasPenOut) {
            const QPointF node = view.map(nodes.last()), handle = view.map(m_penOut);
            painter.drawLine(node, handle);
            painter.setBrush(UiTheme::color(UiTheme::Token::Surface));
            painter.drawEllipse(handle, h / 2, h / 2);
        }
    }
    const QRectF label = (m_drag == Drag::Label && m_layers ? m_onlyLive : m_document).labelBounds();
    if (!label.isEmpty()) {
        painter.setPen(QPen(accent, 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(toWidget(label.topLeft()), toWidget(label.bottomRight())));
    }
}

void StickerPreview::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    const QPointF position = event->position();
    m_pressAt = toDocument(position);
    flush();  // arrow moves first, as their own step
    m_drag = Drag::None;
    m_dragMatrix = QTransform();
    if (m_mode == Mode::Pen) {
        // A click adds a corner, a drag from it pulls its handles (a curve); a click on the first
        // point closes the path.
        QPointF at = m_pressAt;
        if (!m_penActive) {
            m_pen = {at, {}, false};
            m_penActive = true;
            m_hasPenOut = false;
        } else {
            if (!m_pen.segments.isEmpty() && QLineF(toWidget(m_pen.start), position).length() <= kPickPx + kHandlePx / 2) {
                finishPen(true);
                return;
            }
            const QPointF from = m_pen.segments.isEmpty() ? m_pen.start : m_pen.segments.last().end;
            at = snapped(from, at, event->modifiers() & Qt::ShiftModifier);
            m_pen.segments.append({m_hasPenOut, m_hasPenOut ? m_penOut : QPointF(), at, at});
            m_hasPenOut = false;
        }
        m_pressAt = at;
        m_penMouse = at;
        m_drag = Drag::PenHandle;
        update();
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
            update();
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
    const qreal tolerance = kPickPx * m_area.width() / (kSizePx - 2 * kPreviewInsetPx);
    const QString hit = SvgDocument::shapeAt(shapes(), m_pressAt, tolerance);
    const bool shift = event->modifiers() & Qt::ShiftModifier;
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

void StickerPreview::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_mode == Mode::Pen) {
        m_drag = Drag::None;
        finishPen();
        return;
    }
    if (event->button() != Qt::LeftButton || m_mode != Mode::Shapes) return;
    const qreal tolerance = kPickPx * m_area.width() / (kSizePx - 2 * kPreviewInsetPx);
    const QString hit = SvgDocument::shapeAt(shapes(), toDocument(event->position()), tolerance);
    m_drag = Drag::None;
    if (!hit.isEmpty() && hit != m_pointShape) editPoints(hit);
}

void StickerPreview::updateCursor(const QPointF &position) {
    if (draws(m_mode)) {
        setCursor(Qt::CrossCursor);
        return;
    }
    int edges = None;
    if (m_mode == Mode::Frame) edges = edgesAt(m_frame, position);
    else if (m_pointShape.isEmpty() && !m_selection.isEmpty()) edges = handlesAt(SvgDocument::bounds(shapes(), m_selection), position);
    const bool horizontal = edges & (Left | Right), vertical = edges & (Top | Bottom);
    const bool falling = ((edges & Left) && (edges & Top)) || ((edges & Right) && (edges & Bottom));
    if (horizontal && vertical) setCursor(falling ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
    else if (horizontal) setCursor(Qt::SizeHorCursor);
    else if (vertical) setCursor(Qt::SizeVerCursor);
    else if (onLabel(position) || (m_mode == Mode::Shapes && pointAt(position).valid())) setCursor(Qt::SizeAllCursor);
    else unsetCursor();
}

void StickerPreview::mouseMoveEvent(QMouseEvent *event) {
    const QPointF position = event->position();
    if (m_drag == Drag::None) {
        updateCursor(position);
        if (m_penActive) {  // the next segment follows the mouse
            const QPointF from = m_pen.segments.isEmpty() ? m_pen.start : m_pen.segments.last().end;
            m_penMouse = snapped(from, toDocument(position), event->modifiers() & Qt::ShiftModifier);
            update();
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
        else matrix = resizeMatrix(at, event->modifiers() & Qt::ShiftModifier);
        m_dragMatrix = matrix;
        beginLayers(m_selection);
        break;
    }
    case Drag::Rubber: {
        m_rubber = QRectF(m_pressAt, at).normalized();
        QStringList ids = (event->modifiers() & Qt::ShiftModifier) ? m_selectionAtPress : QStringList();
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
        m_drawing = toolShape(m_pressAt, at, event->modifiers() & Qt::ShiftModifier);
        break;
    case Drag::PenHandle: {
        if (QLineF(toWidget(at), toWidget(m_pressAt)).length() < 2) break;
        // The point's handles: outgoing where the mouse is, incoming mirrored (a smooth point).
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
    update();
}

void StickerPreview::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || m_drag == Drag::None) return;
    const Drag drag = m_drag;
    m_drag = Drag::None;
    const bool moved = QLineF(toWidget(toDocument(event->position())), toWidget(m_pressAt)).length() >= 1;
    switch (drag) {
    case Drag::Label: {
        const QPointF position = m_layers ? m_onlyLive.label().position : m_document.label().position;
        if (position != m_document.label().position && labelMoved) labelMoved(position);
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
            const QString hit = SvgDocument::shapeAt(shapes(), m_pressAt, kPickPx * m_area.width() / (kSizePx - 2 * kPreviewInsetPx));
            QStringList ids = m_selection;
            if (event->modifiers() & Qt::ShiftModifier) ids.removeAll(hit);
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
        if (QLineF(toWidget(box.topLeft()), toWidget(box.bottomRight())).length() >= 3 && shapeDrawn)
            shapeDrawn(SvgDocument::subPaths(drawn));
        break;
    }
    case Drag::PenHandle:
    case Drag::Rubber:
    case Drag::None: break;
    }
    m_dragMatrix = QTransform();
    endLayers();
    updateArea();
    update();
}

// Delete, Backspace, the arrows and Esc belong to the preview while it has the focus: the main
// window's shortcuts (Delete removes topics) must not take them.
bool StickerPreview::event(QEvent *event) {
    if (event->type() == QEvent::ShortcutOverride && m_penActive) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape || key == Qt::Key_Backspace || key == Qt::Key_Delete) {
            event->accept();
            return true;
        }
    }
    if (event->type() == QEvent::ShortcutOverride && m_mode == Mode::Shapes) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        const bool ours = key == Qt::Key_Delete || key == Qt::Key_Backspace || key == Qt::Key_Left || key == Qt::Key_Right ||
                          key == Qt::Key_Up || key == Qt::Key_Down ||
                          (key == Qt::Key_Escape && (!m_pointShape.isEmpty() || !m_selection.isEmpty()));
        if (ours) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void StickerPreview::keyPressEvent(QKeyEvent *event) {
    if (m_penActive) {
        const int key = event->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            finishPen();
            return;
        }
        if (key == Qt::Key_Escape) {
            cancelPen();
            return;
        }
        if (key == Qt::Key_Backspace || key == Qt::Key_Delete) {  // the last point
            if (m_pen.segments.isEmpty()) cancelPen();
            else m_pen.segments.removeLast();
            m_hasPenOut = false;
            update();
            return;
        }
    }
    if (m_mode != Mode::Shapes) {
        QWidget::keyPressEvent(event);
        return;
    }
    const int key = event->key();
    const bool arrow = key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;
    // Shift held for larger steps (or Ctrl, Alt) is not another key: the arrow moves keep waiting.
    const bool modifier = key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta;
    if (modifier) return;
    if (!arrow) flush();
    if (key == Qt::Key_Escape && (!m_pointShape.isEmpty() || !m_selection.isEmpty())) {
        // Esc ends the point editing, then empties the selection; only then it closes the editor.
        if (!m_pointShape.isEmpty()) editPoints(QString());
        else setSelection({});
        return;
    }
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
        if (!m_pointShape.isEmpty()) removeChosenPoint();
        else if (!m_selection.isEmpty() && removeRequested) removeRequested(m_selection);
        return;
    }
    QPointF step;
    if (key == Qt::Key_Left) step = QPointF(-1, 0);
    else if (key == Qt::Key_Right) step = QPointF(1, 0);
    else if (key == Qt::Key_Up) step = QPointF(0, -1);
    else if (key == Qt::Key_Down) step = QPointF(0, 1);
    if (!step.isNull() && !m_selection.isEmpty() && m_pointShape.isEmpty()) {
        // 1% of the frame's longer side, 10% with Shift.
        const QRectF box = m_document.viewBox();
        const qreal amount = qMax(box.width(), box.height()) * ((event->modifiers() & Qt::ShiftModifier) ? 0.1 : 0.01);
        // Shown at once on the layers, written after a pause (kNudgePauseMs) as one step.
        beginLayers(m_selection);
        m_nudge = m_nudge * QTransform::fromTranslate(step.x() * amount, step.y() * amount);
        m_nudgeTimer->start();
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

// --- Dialog --------------------------------------------------------------------------------------

namespace {
// Hugeicons (docs/design/ui-foundations.md, "Icone"), light stroke for the dark modal.
QIcon hugeicon(const char *name) { return QIcon(QStringLiteral(":/assets/hugeicons/editor-%1.svg").arg(QLatin1String(name))); }

QIcon toolIcon(StickerPreview::Mode mode) {
    using Mode = StickerPreview::Mode;
    switch (mode) {
    case Mode::Shapes: return hugeicon("select");
    case Mode::Frame: return hugeicon("crop");
    case Mode::Rectangle: return hugeicon("rectangle");
    case Mode::Ellipse: return hugeicon("ellipse");
    case Mode::Line: return hugeicon("line");
    case Mode::Pen: return hugeicon("pen");
    case Mode::Polygon: return hugeicon("polygon");
    case Mode::Star: return hugeicon("star");
    }
    return {};
}

enum class Glyph { RotateLeft, RotateRight, FlipH, FlipV, Forward, Backward, Front, Back, Delete, Bold, Undo, Redo };

QIcon glyphIcon(Glyph glyph) {
    switch (glyph) {
    case Glyph::RotateLeft: return hugeicon("rotate-left");
    case Glyph::RotateRight: return hugeicon("rotate-right");
    case Glyph::FlipH: return hugeicon("flip-horizontal");
    case Glyph::FlipV: return hugeicon("flip-vertical");
    case Glyph::Forward: return hugeicon("bring-forward");
    case Glyph::Backward: return hugeicon("send-backward");
    case Glyph::Front: return hugeicon("bring-to-front");
    case Glyph::Back: return hugeicon("send-to-back");
    case Glyph::Delete: return hugeicon("delete");
    case Glyph::Bold: return hugeicon("bold");
    case Glyph::Undo: return hugeicon("undo");
    case Glyph::Redo: return hugeicon("redo");
    }
    return {};
}

// A grid of label / control rows, as the Export dialog (labels left, controls right).
QGridLayout *rowsGrid() {
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(12);
    grid->setColumnStretch(1, 1);
    return grid;
}

void addRow(QGridLayout *grid, const QString &text, QWidget *control) {
    const int row = grid->rowCount();
    grid->addWidget(new QLabel(text), row, 0);
    grid->addWidget(control, row, 1, Qt::AlignRight | Qt::AlignVCenter);
    if (control->accessibleName().isEmpty()) control->setAccessibleName(text);
}

void addRow(QGridLayout *grid, const QString &text, const QList<QWidget *> &controls) {
    auto *box = new QWidget;
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    for (QWidget *control : controls) row->addWidget(control);
    addRow(grid, text, box);
}

NumberField *numberField(double minimum, double maximum, double value, int decimals = 0) {
    auto *field = new NumberField;
    field->setRange(minimum, maximum);
    field->setDecimals(decimals);
    field->setValue(value);
    field->setFixedWidth(kNumberFieldPx);
    return field;
}

NodeStyle::Reading colorReading(const QColor &color) {
    NodeStyle::Reading reading;
    reading.empty = !color.isValid();  // "none": shown as mixed (the dashed field)
    reading.value = color;
    return reading;
}
}  // namespace

// The editor's buttons in the modal's colours (docs/design/ui-components.md, "Modali"): set on each
// button, as a property selector on the dialog did not reach the tool buttons.
static QString toolStyle() {
    return UiTheme::qss(QStringLiteral("QToolButton { background:@surfaceSunken; border:0; border-radius:6px; }"
                                       "QToolButton:hover { background:@divider; }"
                                       "QToolButton:checked { background:@accent; }"));
}

static QString iconButtonStyle() {
    return UiTheme::qss(QStringLiteral("QToolButton { background:@surfaceRaised; border:1px solid @divider; border-radius:4px; }"
                                       "QToolButton:hover { background:@divider; }"
                                       "QToolButton:checked { background:@accent; border-color:@accent; }"));
}

static QString secondaryStyle() {
    return UiTheme::qss(QStringLiteral("QPushButton { background:@surfaceRaised; color:@text; border:1px solid @divider; }"
                                       "QPushButton:hover { background:@divider; }"
                                       "QPushButton:disabled { color:@textMuted; }"));
}

static QLabel *sectionTitle(const QString &text) {
    auto *label = new QLabel(text);
    label->setStyleSheet(UiTheme::qss(QStringLiteral("QLabel { color:@textMuted; font-size:12px; font-weight:600; }")));
    return label;
}

QToolButton *StickerEditorDialog::iconButton(const QIcon &icon, const QString &tip, const std::function<void()> &action, bool checkable) {
    auto *button = new QToolButton;
    button->setStyleSheet(iconButtonStyle());
    button->setIcon(icon);
    button->setIconSize(QSize(20, 20));
    button->setFixedSize(kIconButtonPx, kIconButtonPx);
    button->setCheckable(checkable);
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    button->setCursor(Qt::PointingHandCursor);
    if (action) connect(button, &QToolButton::clicked, this, action);
    return button;
}

QPushButton *StickerEditorDialog::textButton(const QString &text, const std::function<void()> &action) {
    auto *button = new QPushButton(text);
    button->setStyleSheet(secondaryStyle());
    button->setCursor(Qt::PointingHandCursor);
    connect(button, &QPushButton::clicked, this, action);
    return button;
}

StickerEditorDialog::StickerEditorDialog(QWidget *parent, const QString &path, const QList<IconLibrary::Collection> &collections,
                                         const QString &collectionId)
    : ModalDialog(parent, tr("Edit sticker"), tr("Save as new"), 860) {
    Help::setPage(this, QStringLiteral("topic-contents/sticker-editor.md"));  // FEAT-092: F1 and "?" open it
    setObjectName(QStringLiteral("StickerEditorDialog"));
    QFile file(path);
    const QByteArray data = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    m_valid = m_document.load(data) && QSvgRenderer(data).isValid();
    if (m_valid) m_document.ensureShapeIds();  // stage 3: every drawn shape is picked by its id
    m_history.append(m_document.data());
    m_preview = new StickerPreview;
    m_preview->setDocument(m_document);
    m_preview->frameChanged = [this](const QRectF &box) { setFrame(box); };
    m_preview->labelMoved = [this](const QPointF &position) { setLabelPosition(position); };
    m_preview->shapesTransformed = [this](const QStringList &ids, const QTransform &matrix, bool nudge) {
        m_document.transformShapes(ids, matrix);
        commit(nudge ? QStringLiteral("nudge") : QString());
    };
    m_preview->pathChanged = [this](const QString &id, const QList<SvgDocument::SubPath> &path) {
        m_document.setPath(id, path);
        commit();
    };
    m_preview->removeRequested = [this](const QStringList &ids) { removeShapes(ids); };
    m_preview->selectionChanged = [this] {
        // One shape chosen: the Style page shows its style, so a change starts from it.
        const QStringList selection = m_preview->selection();
        if (selection.size() == 1) {
            m_style = m_document.shapeStyle(selection.first());
            const QRectF box = m_document.viewBox();
            m_style.strokeWidth = m_style.strokeWidth * SvgDocument::kStickerPx / qMax(box.width(), box.height());
            syncStyle();
        }
        updateState();
    };
    m_preview->shapeDrawn = [this](const QList<SvgDocument::SubPath> &path) { addShape(path); };

    auto *columns = new QHBoxLayout;
    columns->setSpacing(32);

    // Left: the tools, their options, the preview, Undo and Redo.
    using Mode = StickerPreview::Mode;
    auto *left = new QVBoxLayout;
    left->setSpacing(12);
    left->addWidget(sectionTitle(tr("Tools")));
    auto *tools = new QHBoxLayout;
    tools->setSpacing(4);
    auto *toolGroup = new QButtonGroup(this);
    const QList<std::tuple<Mode, QString, QString>> toolList{
        {Mode::Shapes, tr("Select: move and resize the shapes; double click to edit the points"), QStringLiteral("StickerShapesMode")},
        {Mode::Frame, tr("Frame: drag the edges to crop"), QStringLiteral("StickerFrameMode")},
        {Mode::Rectangle, tr("Rectangle (Shift: square)"), QStringLiteral("StickerRectangleTool")},
        {Mode::Ellipse, tr("Ellipse (Shift: circle)"), QStringLiteral("StickerEllipseTool")},
        {Mode::Line, tr("Line (Shift: steps of 45°)"), QStringLiteral("StickerLineTool")},
        {Mode::Pen, tr("Pen: click for a corner, drag for a curve; double click or Enter ends, a click on the first point closes"),
         QStringLiteral("StickerPenTool")},
        {Mode::Polygon, tr("Polygon, from its centre (Shift: upright)"), QStringLiteral("StickerPolygonTool")},
        {Mode::Star, tr("Star, from its centre (Shift: upright)"), QStringLiteral("StickerStarTool")}};
    for (const auto &[mode, tip, name] : toolList) {
        auto *button = new QToolButton;
        button->setStyleSheet(toolStyle());
        button->setObjectName(name);
        button->setIcon(toolIcon(mode));
        button->setIconSize(QSize(22, 22));
        button->setFixedSize(kToolPx, kToolPx);
        button->setToolTip(tip);
        button->setAccessibleName(tip);
        button->setCheckable(true);
        button->setChecked(mode == Mode::Shapes);
        button->setCursor(Qt::PointingHandCursor);
        toolGroup->addButton(button);
        connect(button, &QToolButton::toggled, this, [this, mode](bool on) {
            if (on) m_preview->setMode(mode);
            updateState();
        });
        tools->addWidget(button);
        if (mode == Mode::Frame) tools->addSpacing(12);  // selecting and cropping, then drawing
    }
    tools->addStretch();
    left->addLayout(tools);
    // The chosen tool's option, in a row of fixed height so the preview does not move.
    auto *optionsRow = new QWidget;
    optionsRow->setFixedHeight(kOptionsRowPx);
    auto *options = new QHBoxLayout(optionsRow);
    options->setContentsMargins(0, 0, 0, 0);
    options->setSpacing(8);
    auto option = [this, options](const QString &label, NumberField *field, const QString &name) {
        auto *caption = new QLabel(label);
        field->setObjectName(name);
        field->setAccessibleName(label);
        connect(field, &NumberField::valueChosen, this, &StickerEditorDialog::updateToolOptions);
        options->addWidget(caption);
        options->addWidget(field);
        m_optionLabels.append(caption);
        return field;
    };
    m_cornerRadius = option(tr("Corners (px)"), numberField(0, 128, 0), QStringLiteral("StickerCornerRadius"));
    m_sides = option(tr("Sides"), numberField(3, 12, 5), QStringLiteral("StickerSides"));
    m_points = option(tr("Points"), numberField(3, 12, 5), QStringLiteral("StickerPoints"));
    // Without options: what the tool does, in a word.
    m_toolHint = new QLabel;
    m_toolHint->setObjectName(QStringLiteral("StickerToolHint"));
    m_toolHint->setStyleSheet(UiTheme::qss(QStringLiteral("QLabel { color:@textMuted; font-size:12px; }")));
    options->addWidget(m_toolHint);
    options->addStretch();
    left->addWidget(optionsRow);
    left->addWidget(m_preview);
    auto *history = new QHBoxLayout;
    history->setSpacing(8);
    m_undo = iconButton(glyphIcon(Glyph::Undo), tr("Undo (%1)").arg(QKeySequence(QKeySequence::Undo).toString(QKeySequence::NativeText)), [this] { undo(); });
    m_redo = iconButton(glyphIcon(Glyph::Redo), tr("Redo (%1)").arg(QKeySequence(QKeySequence::Redo).toString(QKeySequence::NativeText)), [this] { redo(); });
    history->addWidget(m_undo);
    history->addWidget(m_redo);
    history->addStretch();
    left->addLayout(history);
    left->addStretch();
    // As wide as the preview on every page: the right column does not move when the page changes.
    auto *leftColumn = new QWidget;
    leftColumn->setFixedWidth(StickerPreview::kSizePx);
    leftColumn->setLayout(left);
    left->setContentsMargins(0, 0, 0, 0);
    columns->addWidget(leftColumn, 0, Qt::AlignTop);

    // Right: three pages (Style, Frame, Text), then where the new sticker goes.
    auto *right = new QVBoxLayout;
    right->setSpacing(16);
    m_pageSelect = new SegmentedControl({tr("Style"), tr("Frame"), tr("Text")});
    m_pageSelect->setObjectName(QStringLiteral("StickerPages"));
    right->addWidget(m_pageSelect);
    auto *pages = new QStackedWidget;
    connect(m_pageSelect, &SegmentedControl::segmentActivated, pages, &QStackedWidget::setCurrentIndex);
    auto page = [pages](QGridLayout *grid) {
        auto *widget = new QWidget;
        auto *box = new QVBoxLayout(widget);
        box->setContentsMargins(0, 0, 0, 0);
        box->addLayout(grid);
        box->addStretch();
        pages->addWidget(widget);
    };

    // Style: the drawing's colours, the selected or next shape's fill and outline, the order.
    QGridLayout *style = rowsGrid();
    style->addWidget(sectionTitle(tr("Colors of the drawing")), 0, 0, 1, 2);
    m_swatchGrid = new QGridLayout;
    m_swatchGrid->setSpacing(8);
    auto *swatches = new QWidget;
    swatches->setLayout(m_swatchGrid);
    m_swatchGrid->setContentsMargins(0, 0, 0, 0);
    style->addWidget(swatches, 1, 0, 1, 2, Qt::AlignLeft);
    m_fillKind = new SegmentedControl({tr("Color"), tr("None")});
    m_fillKind->setObjectName(QStringLiteral("StickerFillKind"));
    m_fillKind->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(m_fillKind, &SegmentedControl::segmentActivated, this, [this](int index) {
        SvgDocument::ShapeStyle changed = m_style;
        changed.fill = index == 1 ? QColor() : (m_lastFill.isValid() ? m_lastFill : QColor(Qt::black));
        setShapeStyle(changed, SvgDocument::Fill);
    });
    m_fillColor = new ColorField;
    m_fillColor->setObjectName(QStringLiteral("StickerFillColor"));
    m_fillColor->allowOpacity = false;
    connect(m_fillColor, &ColorField::colorChosen, this, [this](const QColor &color) {
        SvgDocument::ShapeStyle changed = m_style;
        changed.fill = color;
        setShapeStyle(changed, SvgDocument::Fill);
    });
    addRow(style, tr("Fill"), {m_fillKind, m_fillColor});
    m_strokeColor = new ColorField;
    m_strokeColor->setObjectName(QStringLiteral("StickerStrokeColor"));
    m_strokeColor->allowOpacity = false;
    connect(m_strokeColor, &ColorField::colorChosen, this, [this](const QColor &color) {
        SvgDocument::ShapeStyle changed = m_style;
        changed.stroke = color;
        if (changed.strokeWidth <= 0) changed.strokeWidth = 1;
        setShapeStyle(changed, SvgDocument::Stroke | SvgDocument::StrokeWidth);
    });
    addRow(style, tr("Outline"), m_strokeColor);
    m_strokeWidth = numberField(0, 40, 0, 1);
    m_strokeWidth->setStep(0.5);
    m_strokeWidth->setObjectName(QStringLiteral("StickerStrokeWidth"));
    m_strokeWidth->setToolTip(tr("On the sticker at 256 px; 0: no outline"));
    connect(m_strokeWidth, &NumberField::valueChosen, this, [this](double width) {
        SvgDocument::ShapeStyle changed = m_style;
        changed.strokeWidth = width;
        if (width > 0 && !changed.stroke.isValid()) changed.stroke = Qt::black;
        setShapeStyle(changed, SvgDocument::Stroke | SvgDocument::StrokeWidth);
    });
    addRow(style, tr("Outline width (px)"), m_strokeWidth);
    using Order = SvgDocument::Order;
    QList<QWidget *> order;
    for (const auto &[glyph, tip, value] : {std::tuple{Glyph::Forward, tr("Bring forward"), Order::Forward},
                                            std::tuple{Glyph::Backward, tr("Send backward"), Order::Backward},
                                            std::tuple{Glyph::Front, tr("Bring to front"), Order::Front},
                                            std::tuple{Glyph::Back, tr("Send to back"), Order::Back}}) {
        QToolButton *button = iconButton(glyphIcon(glyph), tip, [this, value] { reorderShapes(value); });
        m_shapeButtons.append(button);
        order.append(button);
    }
    QToolButton *remove = iconButton(glyphIcon(Glyph::Delete), tr("Delete"), [this] { removeShapes(m_preview->selection()); });
    m_shapeButtons.append(remove);
    order.append(remove);
    addRow(style, tr("Selected shapes"), order);
    page(style);

    // Frame: turns, flips, the angle, fitting and a margin. The edges are dragged with the Frame tool.
    QGridLayout *frame = rowsGrid();
    addRow(frame, tr("Rotate"), {iconButton(glyphIcon(Glyph::RotateLeft), tr("Rotate left"), [this] { rotateBy(-90); }),
                                 iconButton(glyphIcon(Glyph::RotateRight), tr("Rotate right"), [this] { rotateBy(90); })});
    addRow(frame, tr("Flip"), {iconButton(glyphIcon(Glyph::FlipH), tr("Flip horizontally"), [this] { flipHorizontally(); }),
                               iconButton(glyphIcon(Glyph::FlipV), tr("Flip vertically"), [this] { flipVertically(); })});
    m_angle = numberField(-180, 180, 0);
    m_angle->setObjectName(QStringLiteral("StickerAngle"));
    connect(m_angle, &NumberField::valueChosen, this, [this](double value) { setRotation(value); });
    addRow(frame, tr("Angle (°)"), m_angle);
    addRow(frame, tr("Fit to the drawing"), textButton(tr("Fit"), [this] { fitToDrawing(); }));
    m_margin = numberField(1, 50, 10);
    m_margin->setObjectName(QStringLiteral("StickerMargin"));
    m_margin->setToolTip(tr("A share of the longer side, on every side"));
    addRow(frame, tr("Margin (%)"), {m_margin, textButton(tr("Add"), [this] { addMargin(); })});
    page(frame);

    // Text: one label on top of the drawing, dragged on the preview.
    QGridLayout *text = rowsGrid();
    m_labelText = new QLineEdit;
    m_labelText->setObjectName(QStringLiteral("StickerLabelText"));
    m_labelText->setPlaceholderText(tr("A word or a number"));
    m_labelText->setFixedWidth(kTextFieldPx);
    // Typing on a large sticker: one step after a pause (each step rewrites the whole file).
    m_textTimer = new QTimer(this);
    m_textTimer->setSingleShot(true);
    m_textTimer->setInterval(kTypingPauseMs);
    connect(m_textTimer, &QTimer::timeout, this, &StickerEditorDialog::flushText);
    connect(m_labelText, &QLineEdit::textEdited, this, [this](const QString &value) {
        if (m_document.data().size() < kLargeBytes) {
            setLabelText(value);
            return;
        }
        m_pendingText = value;
        m_textPending = true;
        m_textTimer->start();
    });
    addRow(text, tr("Text"), m_labelText);
    m_labelFont = makeDarkSelect();
    m_labelFont->setObjectName(QStringLiteral("StickerLabelFont"));
    m_labelFont->addItems(labelFamilies());
    m_labelFont->setFixedWidth(kTextFieldPx);
    connect(m_labelFont, &QComboBox::currentTextChanged, this,
            [this](const QString &family) { applyLabel([&](SvgDocument::Label &l) { l.family = family; }, QString()); });
    addRow(text, tr("Font"), m_labelFont);
    m_labelSize = numberField(5, 100, kDefaultLabelSizePct);
    m_labelSize->setObjectName(QStringLiteral("StickerLabelSize"));
    m_labelSize->setToolTip(tr("A share of the longer side"));
    connect(m_labelSize, &NumberField::valueChosen, this, [this](double percent) {
        const QRectF box = m_document.viewBox();
        applyLabel([&](SvgDocument::Label &l) { l.size = qMax(box.width(), box.height()) * percent / 100; }, QStringLiteral("size"));
    });
    addRow(text, tr("Size (%)"), m_labelSize);
    m_labelColor = new ColorField;
    m_labelColor->setObjectName(QStringLiteral("StickerLabelColor"));
    m_labelColor->allowOpacity = false;
    connect(m_labelColor, &ColorField::colorChosen, this,
            [this](const QColor &color) { applyLabel([&](SvgDocument::Label &l) { l.color = color; }, QStringLiteral("color")); });
    addRow(text, tr("Color"), m_labelColor);
    m_labelBold = iconButton(glyphIcon(Glyph::Bold), tr("Bold"), {}, true);
    m_labelBold->setObjectName(QStringLiteral("StickerLabelBold"));
    connect(m_labelBold, &QToolButton::toggled, this,
            [this](bool bold) { applyLabel([&](SvgDocument::Label &l) { l.bold = bold; }, QString()); });
    addRow(text, tr("Bold"), m_labelBold);
    page(text);
    right->addWidget(pages);

    // Where the new sticker goes.
    right->addWidget(sectionTitle(tr("Save as")));
    QGridLayout *save = rowsGrid();
    m_name = new QLineEdit(IconLibrary::displayName(path));
    m_name->setObjectName(QStringLiteral("StickerName"));
    m_name->setFixedWidth(kTextFieldPx);
    addRow(save, tr("Name"), m_name);
    m_collection = makeDarkSelect();
    m_collection->setObjectName(QStringLiteral("StickerCollection"));
    m_collection->setFixedWidth(kTextFieldPx);
    for (const IconLibrary::Collection &c : collections)
        if (!c.hidden) m_collection->addItem(c.name, c.id);
    m_collection->addItem(tr("New collection…"), QString());
    m_collection->setCurrentIndex(qMax(0, m_collection->findData(collectionId)));
    addRow(save, tr("Collection"), m_collection);
    m_newCollection = new QLineEdit;
    m_newCollection->setObjectName(QStringLiteral("StickerNewCollection"));
    m_newCollection->setPlaceholderText(tr("Name of the new collection"));
    m_newCollection->setFixedWidth(kTextFieldPx);
    addRow(save, tr("New collection"), m_newCollection);
    m_newCollectionLabel = qobject_cast<QLabel *>(save->itemAtPosition(save->rowCount() - 1, 0)->widget());
    right->addLayout(save);
    right->addStretch();
    columns->addLayout(right, 1);
    contentLayout()->addLayout(columns);

    connect(m_name, &QLineEdit::textChanged, this, &StickerEditorDialog::updateState);
    connect(m_newCollection, &QLineEdit::textChanged, this, &StickerEditorDialog::updateState);
    connect(m_collection, &QComboBox::currentIndexChanged, this, &StickerEditorDialog::updateState);
    rebuildSwatches();
    syncControls();
    syncStyle();
    updateToolOptions();
    updateState();
}

void StickerEditorDialog::updateToolOptions() {
    // Corners in px of the sticker at 256 px, as the outline width.
    const QRectF box = m_document.viewBox();
    const qreal unit = qMax(box.width(), box.height()) / SvgDocument::kStickerPx;
    m_preview->setToolOptions(m_cornerRadius->value() * unit, int(m_sides->value()), int(m_points->value()));
}

void StickerEditorDialog::addShape(const QList<SvgDocument::SubPath> &path) {
    settle();
    const QRectF box = m_document.viewBox();
    SvgDocument::ShapeStyle style = m_style;
    style.strokeWidth = m_style.strokeWidth * qMax(box.width(), box.height()) / SvgDocument::kStickerPx;
    if (style.strokeWidth <= 0) style.stroke = QColor();
    const QString id = m_document.addPath(path, style);
    if (id.isEmpty()) return;
    commit();
    m_preview->setSelection({id});
}

void StickerEditorDialog::setShapeStyle(const SvgDocument::ShapeStyle &style, int parts) {
    m_style = style;
    if (m_style.fill.isValid()) m_lastFill = m_style.fill;
    const QStringList selection = m_preview->selection();
    if (!selection.isEmpty()) {
        settle();
        SvgDocument::ShapeStyle written = style;
        const QRectF box = m_document.viewBox();
        written.strokeWidth = style.strokeWidth * qMax(box.width(), box.height()) / SvgDocument::kStickerPx;
        if (written.strokeWidth <= 0) written.stroke = QColor();
        m_document.setShapeStyle(selection, written, parts);
        commit(parts == (SvgDocument::Stroke | SvgDocument::StrokeWidth) ? QStringLiteral("strokeWidth") : QString());
    }
    syncStyle();
}

void StickerEditorDialog::syncStyle() {
    m_fillKind->setCurrentIndex(m_style.fill.isValid() ? 0 : 1);
    m_fillColor->setEnabled(m_style.fill.isValid());
    m_fillColor->setReading(colorReading(m_style.fill.isValid() ? m_style.fill : m_lastFill));
    m_strokeColor->setReading(colorReading(m_style.stroke));
    m_strokeWidth->setValue(m_style.stroke.isValid() ? m_style.strokeWidth : 0);
}

void StickerEditorDialog::removeShapes(const QStringList &ids) {
    settle();
    m_document.removeShapes(ids);
    commit();
}

void StickerEditorDialog::reorderShapes(SvgDocument::Order order) {
    settle();
    m_document.reorderShapes(m_preview->selection(), order);
    commit();
}

void StickerEditorDialog::setColor(int index, const QColor &color) {
    const QList<QColor> current = m_document.colors();
    if (index >= 0 && index < current.size()) replaceColor(current[index], color);
}

void StickerEditorDialog::replaceColor(const QColor &from, const QColor &to) {
    settle();
    if (!from.isValid() || !to.isValid() || from.rgb() == to.rgb()) return;
    m_document.replaceColor(from, to);
    commit();
}

void StickerEditorDialog::rotateBy(qreal degrees) {
    settle();
    SvgDocument::Transform t = m_document.transform();
    t.rotation = normalizedAngle(t.rotation + degrees);
    m_document.setTransform(t);
    commit();  // each quarter turn is a step
}

void StickerEditorDialog::setRotation(qreal degrees) {
    settle();
    // The angle field's steps are one change: it starts again from the file before the first one, so
    // the frame widens once for the final angle, not at every degree.
    if (m_lastKey == QLatin1String("angle") && m_step > 0 && m_step + 1 == m_history.size()) m_document.load(m_history[m_step - 1]);
    SvgDocument::Transform t = m_document.transform();
    t.rotation = normalizedAngle(degrees);
    m_document.setTransform(t);
    commit(QStringLiteral("angle"));
}

void StickerEditorDialog::flipHorizontally() {
    settle();
    SvgDocument::Transform t = m_document.transform();
    t.flipH = !t.flipH;
    m_document.setTransform(t);
    commit();
}

void StickerEditorDialog::flipVertically() {
    settle();
    SvgDocument::Transform t = m_document.transform();
    t.flipV = !t.flipV;
    m_document.setTransform(t);
    commit();
}

void StickerEditorDialog::setFrame(const QRectF &viewBox) {
    settle();
    m_document.setViewBox(viewBox);
    commit();
}

void StickerEditorDialog::fitToDrawing() {
    const QRectF bounds = m_document.drawingBounds();
    if (!bounds.isEmpty()) setFrame(bounds);
}

void StickerEditorDialog::addMargin() {
    const QRectF box = m_document.viewBox();
    const qreal margin = qMax(box.width(), box.height()) * m_margin->value() / 100;
    setFrame(box.adjusted(-margin, -margin, margin, margin));
}

void StickerEditorDialog::setLabelText(const QString &text) {
    applyLabel([&](SvgDocument::Label &l) { l.text = text.trimmed(); }, QStringLiteral("text"));
}

void StickerEditorDialog::setLabelPosition(const QPointF &position) {
    applyLabel([&](SvgDocument::Label &l) { l.position = position; }, QString());
}

void StickerEditorDialog::applyLabel(const std::function<void(SvgDocument::Label &)> &change, const QString &key) {
    settle();
    SvgDocument::Label label = m_document.label();
    if (label.text.isEmpty()) {
        // A new label: the controls' font, size, colour and weight, in the middle of the frame.
        const QRectF box = m_document.viewBox();
        label.family = m_labelFont->currentText();
        label.size = qMax(box.width(), box.height()) * m_labelSize->value() / 100;
        label.bold = m_labelBold->isChecked();
        label.color = m_labelColorValue;
        label.position = QPointF(box.center().x(), box.center().y() + label.size / 3);
    }
    change(label);
    m_labelColorValue = label.color;
    if (label.text.isEmpty() && m_document.label().text.isEmpty()) {
        syncControls();  // a style chosen before any text: kept by the controls for the new label
        return;
    }
    m_document.setLabel(label);
    commit(key);
}

void StickerEditorDialog::flushText() {
    // A flag, not the timer: at its timeout a single-shot timer is no longer active.
    if (!m_textPending) return;
    m_textPending = false;
    m_textTimer->stop();
    setLabelText(m_pendingText);
}

void StickerEditorDialog::settle() {
    m_preview->finishPen();  // a pen path being drawn is kept as it is
    m_preview->flush();
    flushText();
}

void StickerEditorDialog::accept() {
    settle();  // what is shown is what is saved
    ModalDialog::accept();
}

void StickerEditorDialog::commit(const QString &key) {
    const QByteArray data = m_document.data();
    if (data == m_history.value(m_step)) return;
    if (!key.isEmpty() && key == m_lastKey && m_step > 0 && m_step + 1 == m_history.size()) {
        m_history[m_step] = data;  // typing or a spin box: one step for the whole change
    } else {
        m_history.resize(m_step + 1);  // a change after Undo drops the steps that were undone
        m_history.append(data);
        m_step = int(m_history.size()) - 1;
        // At most kHistoryBytes of steps (a 2 MB sticker would hold 200 MB in 100 steps): the
        // oldest go, the original (step 0) stays so "modified" and "discard" still know it.
        qsizetype total = 0;
        for (const QByteArray &step : std::as_const(m_history)) total += step.size();
        while (total > kHistoryBytes && m_history.size() > 2) {
            total -= m_history[1].size();
            m_history.removeAt(1);
            --m_step;
        }
    }
    m_lastKey = key;
    apply();
}

void StickerEditorDialog::undo() {
    settle();
    if (m_step == 0) return;
    --m_step;
    m_lastKey.clear();
    m_document.load(m_history[m_step]);
    apply();
}

void StickerEditorDialog::redo() {
    settle();
    if (m_step + 1 >= m_history.size()) return;
    ++m_step;
    m_lastKey.clear();
    m_document.load(m_history[m_step]);
    apply();
}

void StickerEditorDialog::apply() {
    m_preview->setDocument(m_document);
    rebuildSwatches();
    syncControls();
    updateState();
}

void StickerEditorDialog::syncControls() {
    const QSignalBlocker text(m_labelText), font(m_labelFont), bold(m_labelBold);
    m_angle->setValue(m_document.transform().rotation);
    const SvgDocument::Label label = m_document.label();
    if (!label.text.isEmpty()) {
        if (m_labelText->text().trimmed() != label.text) m_labelText->setText(label.text);
        const int family = m_labelFont->findText(label.family);
        if (family >= 0) m_labelFont->setCurrentIndex(family);
        const QRectF box = m_document.viewBox();
        m_labelSize->setValue(qRound(label.size * 100 / qMax(box.width(), box.height())));
        m_labelBold->setChecked(label.bold);
        m_labelColorValue = label.color;
    } else if (!m_labelText->text().trimmed().isEmpty()) {
        m_labelText->clear();
    }
    m_labelColor->setReading(colorReading(m_labelColorValue));
}

void StickerEditorDialog::rebuildSwatches() {
    // Swatches are reused, never deleted while the dialog is open: the picker that changes a colour
    // runs its own event loop inside a swatch's click. Spare ones are hidden.
    const QList<QColor> colors = m_document.colors();
    for (int i = 0; i < colors.size(); ++i) {
        if (i == m_swatches.size()) {
            auto *swatch = new QToolButton;
            swatch->setObjectName(QStringLiteral("StickerSwatch"));
            swatch->setFixedSize(kSwatchPx, kSwatchPx);
            swatch->setCursor(Qt::PointingHandCursor);
            swatch->setAccessibleName(tr("Color %1").arg(i + 1));
            connect(swatch, &QToolButton::clicked, this, [this, i, swatch] {
                const QColor color = m_document.colors().value(i);
                if (!color.isValid()) return;
                const QRect anchor(swatch->mapToGlobal(QPoint(0, 0)), swatch->size());
                // The picker can stay open (free colour panel): each choice replaces the previous one,
                // followed by value because the colours' order can change (the default black).
                auto current = std::make_shared<QColor>(color);
                ColorPicker::exec(anchor, ColorPicker::Placement::Above, {color, {}, false}, [this, current](const QColor &chosen) {
                    QColor next = chosen;
                    next.setAlpha(current->alpha());
                    replaceColor(*current, chosen);
                    *current = next;
                });
            });
            m_swatchGrid->addWidget(swatch, i / kSwatchColumns, i % kSwatchColumns);
            m_swatches.append(swatch);
        }
        QToolButton *swatch = m_swatches[i];
        swatch->setToolTip(colors[i].name());
        swatch->setStyleSheet(swatchStyle(colors[i]));
        swatch->show();
    }
    for (int i = int(colors.size()); i < m_swatches.size(); ++i) m_swatches[i]->hide();
}

void StickerEditorDialog::updateState() {
    // The options of the chosen tool only.
    using Mode = StickerPreview::Mode;
    const Mode mode = m_preview->mode();
    const QList<std::pair<QWidget *, bool>> shown{{m_cornerRadius, mode == Mode::Rectangle}, {m_sides, mode == Mode::Polygon}, {m_points, mode == Mode::Star}};
    for (int i = 0; i < shown.size(); ++i) {
        shown[i].first->setVisible(shown[i].second);
        m_optionLabels[i]->setVisible(shown[i].second);
    }
    QString hint;
    if (mode == Mode::Shapes) hint = tr("Click a shape to select it, double click to edit its points");
    else if (mode == Mode::Frame) hint = tr("Drag the frame's edges to crop the sticker");
    else if (mode == Mode::Ellipse || mode == Mode::Line) hint = tr("Drag on the preview; Shift keeps the proportions");
    else if (mode == Mode::Pen) hint = tr("Click for corners, drag for curves; Enter ends");
    m_toolHint->setText(hint);
    m_toolHint->setVisible(!hint.isEmpty());
    for (QToolButton *button : std::as_const(m_shapeButtons)) button->setEnabled(!m_preview->selection().isEmpty());
    m_undo->setEnabled(m_step > 0);
    m_redo->setEnabled(m_step + 1 < m_history.size());
    const bool newCollection = m_collection->currentData().toString().isEmpty();
    m_newCollection->setVisible(newCollection);
    if (m_newCollectionLabel) m_newCollectionLabel->setVisible(newCollection);
    acceptButton()->setEnabled(m_valid && !stickerName().isEmpty() && (!newCollection || !newCollectionName().isEmpty()));
}

QString StickerEditorDialog::stickerName() const {
    QString name = m_name->text().trimmed();
    // Characters a file name cannot hold on Windows, macOS or Linux.
    static const QString forbidden = QStringLiteral("\\/:*?\"<>|");
    for (QChar &c : name)
        if (forbidden.contains(c)) c = QLatin1Char(' ');
    return name.simplified();
}

QString StickerEditorDialog::collectionId() const { return m_collection->currentData().toString(); }

QString StickerEditorDialog::newCollectionName() const { return m_newCollection->text().trimmed(); }

// Ctrl+Z / Ctrl+Y as keys of the editor, not shortcuts: they work whatever window is active and the
// main window's Undo cannot take them. A text field keeps its own Undo (it handles the key first).
bool StickerEditorDialog::event(QEvent *event) {
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo)) {
            event->accept();
            return true;
        }
    }
    return ModalDialog::event(event);
}

void StickerEditorDialog::keyPressEvent(QKeyEvent *event) {
    if (event->matches(QKeySequence::Undo)) undo();
    else if (event->matches(QKeySequence::Redo)) redo();
    else ModalDialog::keyPressEvent(event);
}

void StickerEditorDialog::reject() {
    settle();
    if (isModified()) {
        const bool discard = confirmDiscard ? confirmDiscard()
                                            : ModalDialog::ask(this, tr("Discard changes?"), tr("The changes to this sticker will be lost."),
                                                               tr("Discard")) == ModalDialog::Choice::Primary;
        if (!discard) return;
    }
    ModalDialog::reject();
}
