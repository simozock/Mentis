#include "nodeshape.h"

#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>

namespace NodeShapes {

namespace {
// Raggio del rettangolo arrotondato attuale (AGENTS.md, "Raggi degli angoli").
constexpr qreal kRoundedRadius = 8;
// Spazio fra le due linee della doppia linea sotto.
constexpr qreal kUnderlineGap = 3;

// Il testo ha gia' il suo padding (16 ai lati, 8 sopra e sotto). Le forme non rettangolari devono
// contenere solo il testo con un margine piu' piccolo, altrimenti diventerebbero enormi: si usa
// il riquadro interno, ristretto di questi valori, e poi la forma non scende mai sotto il
// riquadro del contenuto.
constexpr qreal kInnerInsetX = 8;
constexpr qreal kInnerInsetY = 4;

// Profondita' delle punte e degli smussi, in proporzione all'altezza.
constexpr qreal kParallelogramSlant = 0.35;
constexpr qreal kHexagonPoint = 0.3;
constexpr qreal kOctagonCut = 0.3;
// Square brackets (measured live 2026-10-24, fishbone variant 1): arms 12 px long with the stroke, the text
// as far from the arms' ends as a rectangle's text from its sides.
constexpr qreal kBracketArm = 12;

QRectF centered(const QRectF &content, qreal width, qreal height) {
    width = std::max(width, content.width());
    height = std::max(height, content.height());
    return {content.center().x() - width / 2, content.center().y() - height / 2, width, height};
}

// FEAT-030 stage A (Advanced). The reference could not be measured (2026-10-26): proportions from photo 01 of
// FEAT-020. Arrows: the body is as tall as the content, the head as tall as the body / kArrowBody.
constexpr qreal kArrowBody = 0.6;
constexpr qreal kArrowHead = 0.5;   // length of the head, in proportion to the height
constexpr qreal kTagPoint = 0.4;    // depth of the tag's point, in proportion to the height
constexpr qreal kFlagNotch = 0.3;   // depth of the flag's notch, in proportion to the height
constexpr qreal kNoteFold = 0.35;   // side of the folded corner, in proportion to the height...
constexpr qreal kNoteFoldMax = 12;  // ...at most this, so the fold stays inside the text's padding
constexpr qreal kStackOffset = 6;   // the back card of the stack, to the right and below
constexpr qreal kSideBar = 12;      // distance of the side bar from the left side

qreal noteFold(qreal height) { return std::min(height * kNoteFold, kNoteFoldMax); }

// Sorted x of the points where the row at y crosses the outline.
QList<qreal> rowCrossings(const QList<QPolygonF> &polygons, qreal y) {
    QList<qreal> xs;
    for (const QPolygonF &polygon : polygons) {
        for (qsizetype i = 0; i < polygon.size(); ++i) {
            const QPointF a = polygon[i], b = polygon[(i + 1) % polygon.size()];
            if ((a.y() <= y) == (b.y() <= y)) continue;
            xs << a.x() + (y - a.y()) * (b.x() - a.x()) / (b.y() - a.y());
        }
    }
    std::sort(xs.begin(), xs.end());
    return xs;
}

// Heart, cloud, star, drop, gem and shield grow in proportion until the text fits (user's decision
// 2026-10-16). Each is drawn in the unit square and keeps the aspect of its drawing.
constexpr int kRows = 200;
struct UnitShape {
    QPainterPath path;  // in [0,1] x [0,1]
    qreal aspect = 1;   // width / height
    // halfWidth[k]: half the width (fraction of the shape's width) of the widest centred rectangle
    // whose half height is k / kRows / 2 of the shape's height. Non-increasing; computed once, so
    // outerRect searches a table instead of testing curves.
    std::array<qreal, kRows + 1> halfWidth{};
};

UnitShape makeUnit(const QPainterPath &drawing) {
    UnitShape unit;
    const QRectF bounds = drawing.boundingRect();
    unit.aspect = bounds.width() / bounds.height();
    QTransform toUnit;
    toUnit.scale(1 / bounds.width(), 1 / bounds.height());
    toUnit.translate(-bounds.left(), -bounds.top());
    unit.path = toUnit.map(drawing);
    const QList<QPolygonF> polygons = unit.path.toSubpathPolygons();
    // Half of the inside row through the centre's column at y (0 when the column is outside there).
    auto rowHalf = [&polygons](qreal y) {
        const QList<qreal> xs = rowCrossings(polygons, y);
        const auto right = std::upper_bound(xs.begin(), xs.end(), 0.5);
        if (right == xs.end() || (right - xs.begin()) % 2 == 0) return 0.0;
        return std::min(*right - 0.5, 0.5 - *(right - 1));
    };
    qreal narrowest = 0.5;
    for (int k = 0; k <= kRows; ++k) {
        const qreal dy = 0.5 * k / kRows;
        narrowest = std::min({narrowest, rowHalf(0.5 - dy), rowHalf(0.5 + dy)});
        unit.halfWidth[k] = narrowest;
    }
    return unit;
}

QPainterPath heartPath() {
    QPainterPath p;
    p.moveTo(0.5, 0.22);
    p.cubicTo(0.5, 0.08, 0.38, 0, 0.26, 0);
    p.cubicTo(0.1, 0, 0, 0.13, 0, 0.3);
    p.cubicTo(0, 0.55, 0.25, 0.75, 0.5, 1);
    p.cubicTo(0.75, 0.75, 1, 0.55, 1, 0.3);
    p.cubicTo(1, 0.13, 0.9, 0, 0.74, 0);
    p.cubicTo(0.62, 0, 0.5, 0.08, 0.5, 0.22);
    p.closeSubpath();
    return QTransform::fromScale(1.1, 1).map(p);
}

QPainterPath cloudPath() {
    // Flat bottom with rounded ends and three bumps, like the icon. QPainterPath::united flattens curves
    // coarsely: the parts are united as polygons of many points instead.
    auto smooth = [](const QPainterPath &curved) {
        QPainterPath flat;
        flat.addPolygon(curved.toFillPolygon(QTransform::fromScale(400, 400)));
        return QTransform::fromScale(1.0 / 400, 1.0 / 400).map(flat);
    };
    QPainterPath base;
    base.addRoundedRect(QRectF(0, 0.45, 1.45, 0.55), 0.275, 0.275);
    QPainterPath cloud = smooth(base);
    for (const auto &[x, y, radius] : {std::array<qreal, 3>{0.42, 0.5, 0.3}, {0.88, 0.4, 0.38}, {1.2, 0.62, 0.2}}) {
        QPainterPath circle;
        circle.addEllipse(QPointF(x, y), radius, radius);
        cloud = cloud.united(smooth(circle));
    }
    return cloud.simplified();
}

QPainterPath starPath() {
    QPolygonF star;
    for (int i = 0; i < 10; ++i) {
        const qreal angle = (-90 + 36 * i) * M_PI / 180, radius = i % 2 ? 0.45 : 1.0;
        star << QPointF(radius * std::cos(angle), radius * std::sin(angle));
    }
    QPainterPath p;
    p.addPolygon(star);
    p.closeSubpath();
    return p;
}

QPainterPath dropPath() {
    // A circle whose top right quarter is a square corner (the icon's point).
    QPainterPath p;
    p.moveTo(0.5, 0);
    p.lineTo(1, 0);
    p.lineTo(1, 0.5);
    p.arcTo(QRectF(0, 0, 1, 1), 0, -270);
    p.closeSubpath();
    return p;
}

QPainterPath gemPath() {
    QPainterPath p;
    p.addPolygon(QPolygonF{{0.3, 0}, {0.9, 0}, {1.2, 0.35}, {0.6, 1}, {0, 0.35}});
    p.closeSubpath();
    return p;
}

QPainterPath shieldPath() {
    QPainterPath p;
    p.moveTo(0, 0.12);
    p.cubicTo(0.18, 0.12, 0.36, 0.06, 0.45, 0);
    p.cubicTo(0.54, 0.06, 0.72, 0.12, 0.9, 0.12);
    p.lineTo(0.9, 0.5);
    p.cubicTo(0.9, 0.78, 0.68, 0.92, 0.45, 1);
    p.cubicTo(0.22, 0.92, 0, 0.78, 0, 0.5);
    p.closeSubpath();
    return p;
}

// In the order of NodeShape, from Cuore; nullptr for the other shapes.
const UnitShape *unitShape(NodeShape shape) {
    static const std::array<UnitShape, 6> units{makeUnit(heartPath()), makeUnit(cloudPath()), makeUnit(starPath()),
                                                makeUnit(dropPath()),  makeUnit(gemPath()),   makeUnit(shieldPath())};
    const int index = static_cast<int>(shape) - static_cast<int>(NodeShape::Cuore);
    return index >= 0 && index < static_cast<int>(units.size()) ? &units[index] : nullptr;
}

// The smallest copy of the unit shape that holds a centred box of innerW x innerH.
QRectF proportionalRect(const UnitShape &unit, const QRectF &content, qreal innerW, qreal innerH) {
    auto fits = [&](qreal height) {
        const qreal b = innerH / (2 * height), a = innerW / (2 * unit.aspect * height);
        const int k = static_cast<int>(std::ceil(b * 2 * kRows));
        return k <= kRows && unit.halfWidth[k] >= a;
    };
    qreal low = 0, high = std::max(innerH, innerW / unit.aspect);
    for (int i = 0; i < 40 && !fits(high); ++i) high *= 2;
    for (int i = 0; i < 30; ++i) {
        const qreal middle = (low + high) / 2;
        (fits(middle) ? high : low) = middle;
    }
    // Never smaller than the content box, still in proportion.
    high = std::max({high, content.height(), content.width() / unit.aspect});
    return centered(content, high * unit.aspect, high);
}

// FEAT-030 stage B, measured live 2026-10-26 (100%, 96 DPI, an 18 px title in a 240 x 43 rounded
// rectangle): what each Puntuale shape adds to every side of the rounded rectangle. The marks stay
// kMarkInset inside the rectangle, which the fill covers whole.
struct Growth { qreal side = 0, vertical = 0; };
Growth punctualGrowth(NodeShape shape) {
    switch (shape) {
    case NodeShape::ParentesiAngolari: return {30, 0};
    case NodeShape::DoppieParentesiAngolari: return {37, 0};
    case NodeShape::ParentesiTonde: return {10, 0};
    case NodeShape::ParentesiGraffe: return {18, 0};
    case NodeShape::Angoli: return {20, 15};
    case NodeShape::Virgolette: return {22, 3};
    default: return {};
    }
}
constexpr qreal kMarkInset = 4;
constexpr qreal kAngleWidth = 20;          // < >: as tall as the topic, the point at mid-height
constexpr qreal kDoubleAngleWidth = 9;     // « »: two marks...
constexpr qreal kDoubleAngleGap = 6;       // ...6 px apart, 15 px in all
constexpr qreal kRoundDepth = 8;           // ( ): 51 px tall in a 43 px topic
constexpr qreal kRoundOverhang = 4;
constexpr qreal kCurlyBar = 4;             // { }: the bar 4 px inside the point...
constexpr qreal kCurlyArm = 13;            // ...arms 13 px long, 47 px tall in a 43 px topic
constexpr qreal kCurlyOverhang = 2;
constexpr qreal kCurlyRound = 4;           // radius of the bends
constexpr qreal kCornerArm = 16;           // corners: 16 px across...
constexpr qreal kCornerLeg = 30;           // ...30 px down
constexpr qreal kQuoteInset = 5;           // quotation marks: 12 x 10 px, centred in height
constexpr qreal kQuoteHeight = 10;
constexpr qreal kQuoteComma = 5.5;         // one of the two commas of a mark

// The left (or top-left) mark; the other is its reflection through the centre.
QPainterPath leftMark(NodeShape shape, const QRectF &r) {
    const qreal x = r.left() + kMarkInset, cy = r.center().y();
    QPainterPath p;
    switch (shape) {
    case NodeShape::ParentesiAngolari:
        p.moveTo(x + kAngleWidth, r.top() + 1);
        p.lineTo(x, cy);
        p.lineTo(x + kAngleWidth, r.bottom() - 1);
        break;
    case NodeShape::DoppieParentesiAngolari:
        for (const qreal tip : {x, x + kDoubleAngleGap}) {
            p.moveTo(tip + kDoubleAngleWidth, r.top() + 2);
            p.lineTo(tip, cy);
            p.lineTo(tip + kDoubleAngleWidth, r.bottom() - 2);
        }
        break;
    case NodeShape::ParentesiTonde: {
        // A quadratic through (x, cy): the control point as far out again as the ends are in.
        const qreal reach = kRoundOverhang - 1;
        p.moveTo(x + kRoundDepth, r.top() - reach);
        p.quadTo(x - kRoundDepth, cy, x + kRoundDepth, r.bottom() + reach);
        break;
    }
    case NodeShape::ParentesiGraffe: {
        const qreal bar = x + kCurlyBar, top = r.top() - kCurlyOverhang + 1, bottom = r.bottom() + kCurlyOverhang - 1;
        p.moveTo(x + kCurlyArm, top);
        p.lineTo(bar + kCurlyRound, top);
        p.quadTo(bar, top, bar, top + kCurlyRound);
        p.lineTo(bar, cy - kCurlyRound);
        p.quadTo(bar, cy, x, cy);
        p.quadTo(bar, cy, bar, cy + kCurlyRound);
        p.lineTo(bar, bottom - kCurlyRound);
        p.quadTo(bar, bottom, bar + kCurlyRound, bottom);
        p.lineTo(x + kCurlyArm, bottom);
        break;
    }
    case NodeShape::Angoli:
        p.moveTo(x, r.top() + kMarkInset + kCornerLeg);
        p.lineTo(x, r.top() + kMarkInset);
        p.lineTo(x + kCornerArm, r.top() + kMarkInset);
        break;
    default:
        break;
    }
    return p;
}

// “: two filled commas, a round head below and a tail rising to the right.
QPainterPath openingQuote(const QPointF &topLeft) {
    QPainterPath quote;
    for (const qreal dx : {0.0, kQuoteComma + 1}) {
        const QPointF o = topLeft + QPointF(dx, 0);
        QPainterPath comma;
        comma.addEllipse(o + QPointF(kQuoteComma / 2, kQuoteHeight - kQuoteComma / 2), kQuoteComma / 2, kQuoteComma / 2);
        QPainterPath tail(o + QPointF(0.2, kQuoteHeight - kQuoteComma / 2));
        tail.cubicTo(o + QPointF(0, 3.5), o + QPointF(1.8, 1), o + QPointF(kQuoteComma, 0));
        tail.lineTo(o + QPointF(kQuoteComma, 1.2));
        tail.cubicTo(o + QPointF(3.4, 2), o + QPointF(2.6, 3.6), o + QPointF(kQuoteComma / 2, kQuoteHeight - kQuoteComma));
        tail.closeSubpath();
        quote = quote.united(comma.united(tail));
    }
    return quote;
}

QTransform throughCentre(const QRectF &r) {
    QTransform reflect;
    reflect.translate(r.center().x(), r.center().y());
    reflect.rotate(180);
    reflect.translate(-r.center().x(), -r.center().y());
    return reflect;
}

// FEAT-030 stage C, measured live 2026-10-26 (the same 240 x 43 rounded rectangle as stage B). Growth per
// side is added to the content box; proportions are in units of its height h.
constexpr qreal kDecisionGrowth = 18;       // decision: a diamond w + 36 wide...
constexpr qreal kDecisionAspect = 0.57;     // ...and 0.57 x as tall (276 x 158)
constexpr qreal kManualInputStrip = 0.8;    // manual input: a sloping strip 0.8 h tall above the text
constexpr qreal kPredefinedGrowth = 38;     // predefined process: 316 x 43...
constexpr qreal kPredefinedBar = 18;        // ...bars 18 px inside the sides
constexpr qreal kFlowSlant = 0.3;           // manual operation, data, trapezoids: sides lean 0.3 h
constexpr qreal kManualOperationGrowth = 18;
constexpr qreal kDataGrowth = 18;
constexpr qreal kTrapezoidGrowth = 15;
constexpr qreal kDatabaseGrowth = 2;        // database: 244 x 121...
constexpr qreal kDatabaseCap = 0.6;         // ...ellipses 1.2 h tall (half of it 0.6 h)
constexpr qreal kDocumentGrowth = 2;        // document: 244 wide...
constexpr qreal kDocumentWave = 2.5;        // ...one wave of 2.5 px along the bottom
constexpr qreal kDocumentsFrontGrowth = 6;  // multiple documents: the front one 252 wide...
constexpr qreal kDocumentsSheet = 4;        // ...two sheets behind, 4 px apart up and right
constexpr qreal kDelayGrowth = 1;           // delay: 242 x 43...
constexpr qreal kDelayCorner = 4;           // ...left corners slightly rounded, right a half circle
constexpr qreal kConnectorSize = 62;        // connectors: a 62 px mark above the text...
constexpr qreal kConnectorTop = 2;          // ...2 px from the top...
constexpr qreal kConnectorGap = 6;          // ...6 px above the text box
constexpr qreal kOffPageSquare = 33;        // off-page connector: square part, then the point
constexpr qreal kDoubleCircleExtra = 13;    // double circle: the box's diagonal + 13 (257)...
constexpr qreal kDoubleCircleInset = 9;     // ...the inner circle 9 px inside
constexpr qreal kDisplayPoint = 0.3;        // display: a point 0.3 h deep on the left

bool isConnector(NodeShape shape) { return shape == NodeShape::Connettore || shape == NodeShape::ConnettoreFuoriPagina; }

// The content box inside the outer rectangle of the shapes that grow unevenly up and down.
QRectF contentIn(NodeShape shape, const QRectF &r) {
    switch (shape) {
    case NodeShape::InputManuale: {
        const qreal h = r.height() / (1 + kManualInputStrip);
        return {r.left(), r.bottom() - h, r.width(), h};
    }
    case NodeShape::Database: {
        const qreal h = r.height() / (1 + 3 * kDatabaseCap);
        return {r.left(), r.top() + 2 * kDatabaseCap * h, r.width(), h};
    }
    case NodeShape::Documento: return r.adjusted(0, 0, 0, -2 * kDocumentWave);
    case NodeShape::DocumentiMultipli: return r.adjusted(0, 2 * kDocumentsSheet, 0, -2 * kDocumentWave);
    case NodeShape::Connettore:
    case NodeShape::ConnettoreFuoriPagina: return r.adjusted(0, kConnectorTop + kConnectorSize + kConnectorGap, 0, 0);
    default: return r;
    }
}

QRectF connectorMark(const QRectF &r) {
    return {r.center().x() - kConnectorSize / 2, r.top() + kConnectorTop, kConnectorSize, kConnectorSize};
}

// A document: straight top and sides, the bottom one wave around `bottom - wave` (lower on the left).
QPolygonF wavyDocument(const QRectF &r) {
    QPolygonF document{r.topLeft(), r.topRight()};
    constexpr int kSteps = 24;
    for (int i = kSteps; i >= 0; --i) {
        const qreal t = static_cast<qreal>(i) / kSteps;
        document << QPointF(r.left() + t * r.width(), r.bottom() - kDocumentWave + kDocumentWave * std::sin(2 * M_PI * t));
    }
    return document;
}

// The front document and the two sheets behind it, up and right.
QRectF frontDocument(const QRectF &r) { return r.adjusted(0, 2 * kDocumentsSheet, -2 * kDocumentsSheet, 0); }
QRectF sheetBehind(const QRectF &r, int k) {
    const QRectF front = frontDocument(r);
    return front.translated(k * kDocumentsSheet, -k * kDocumentsSheet).adjusted(0, 0, 0, -2 * kDocumentWave);
}

QRectF ellipseCap(const QRectF &r, bool top) {
    const qreal capHeight = 2 * kDatabaseCap * contentIn(NodeShape::Database, r).height();
    return {r.left(), top ? r.top() : r.bottom() - capHeight, r.width(), capHeight};
}
}  // namespace

bool isUnderline(NodeShape shape) { return shape == NodeShape::LineaSotto || shape == NodeShape::DoppiaLineaSotto; }
bool isPunctual(NodeShape shape) {
    return shape == NodeShape::ParentesiQuadre || (shape >= NodeShape::ParentesiAngolari && shape <= NodeShape::Virgolette);
}
bool hasOpenBorder(NodeShape shape) { return isUnderline(shape) || isPunctual(shape); }
bool hasTextBelow(NodeShape shape) { return isConnector(shape); }
bool showsBorderWhenChosen(NodeShape shape) {
    return isPunctual(shape) || shape == NodeShape::ProcessoPredefinito || shape == NodeShape::Database
           || shape == NodeShape::DocumentiMultipli;
}

QList<QPainterPath> marks(NodeShape shape, const QRectF &r) {
    const QPainterPath left = r.isEmpty() ? QPainterPath() : leftMark(shape, r);
    if (left.isEmpty()) return {};
    // The right (or bottom-right) mark is the left one turned half round the centre.
    return {left, throughCentre(r).map(left)};
}

QPainterPath filledMarks(NodeShape shape, const QRectF &r) {
    if (shape != NodeShape::Virgolette || r.isEmpty()) return {};
    QPainterPath quotes = openingQuote(QPointF(r.left() + kQuoteInset, r.center().y() - kQuoteHeight / 2));
    quotes.addPath(throughCentre(r).map(quotes));
    return quotes;
}

qreal markOverhang(NodeShape shape) {
    return shape == NodeShape::ParentesiTonde ? kRoundOverhang : shape == NodeShape::ParentesiGraffe ? kCurlyOverhang : 0;
}

QList<QPainterPath> innerLines(NodeShape shape, const QRectF &r, qreal roundedRadius) {
    if (r.isEmpty()) return {};
    if (roundedRadius < 0) roundedRadius = kRoundedRadius;
    QPainterPath path;
    switch (shape) {
    case NodeShape::Nota: {
        const qreal fold = noteFold(r.height());
        path.addPolygon(QPolygonF{{r.right() - fold, r.top()}, {r.right() - fold, r.top() + fold}, {r.right(), r.top() + fold}});
        path.closeSubpath();
        break;
    }
    case NodeShape::PilaDiSchede:
        path.addRoundedRect(r.adjusted(0, 0, -kStackOffset, -kStackOffset), roundedRadius, roundedRadius);
        break;
    case NodeShape::SchedaConBordoLaterale:
        path.moveTo(r.left() + kSideBar, r.top());
        path.lineTo(r.left() + kSideBar, r.bottom());
        break;
    case NodeShape::ProcessoPredefinito: {  // FEAT-030 stage C: the two bars
        QPainterPath right;
        path.moveTo(r.left() + kPredefinedBar, r.top());
        path.lineTo(r.left() + kPredefinedBar, r.bottom());
        right.moveTo(r.right() - kPredefinedBar, r.top());
        right.lineTo(r.right() - kPredefinedBar, r.bottom());
        return {path, right};
    }
    case NodeShape::Database:  // the lower half of the top ellipse
        path.arcMoveTo(ellipseCap(r, true), 180);
        path.arcTo(ellipseCap(r, true), 180, 180);
        break;
    case NodeShape::DocumentiMultipli: {  // the front document and the middle sheet over the last one
        path.addPolygon(wavyDocument(frontDocument(r)));
        path.closeSubpath();
        QPainterPath middle;
        middle.addRect(sheetBehind(r, 1));
        return {path, middle};
    }
    case NodeShape::CerchioDoppio:
        path.addEllipse(r.adjusted(kDoubleCircleInset, kDoubleCircleInset, -kDoubleCircleInset, -kDoubleCircleInset));
        break;
    default:
        return {};
    }
    return {path};
}

QRectF outerRect(NodeShape shape, const QRectF &content, qreal borderWidth, qreal textWidth) {
    if (content.isEmpty()) return content;
    const qreal w = content.width(), h = content.height();
    const qreal innerW = std::max<qreal>(1, w - 2 * kInnerInsetX);
    const qreal innerH = std::max<qreal>(1, h - 2 * kInnerInsetY);
    switch (shape) {
    case NodeShape::RettangoloArrotondato:
    case NodeShape::Rettangolo:
    case NodeShape::LineaSotto:
        return content;
    case NodeShape::DoppiaLineaSotto: {
        // La seconda linea sta sotto la prima: il nodo si allunga di uno spazio e di uno spessore.
        QRectF outer = content;
        outer.setBottom(outer.bottom() + kUnderlineGap + borderWidth);
        return outer;
    }
    case NodeShape::Ellisse:  // l'ellisse che passa per gli angoli del riquadro interno: assi x radice di 2
        return centered(content, innerW * M_SQRT2, innerH * M_SQRT2);
    case NodeShape::Cerchio: {
        // Measured (live 2026-10-24): sqrt(3) x the text width (a 173 px title in a 301 px circle, 265 in
        // 461); at least the circle through the inner box's corners, so tall text stays inside.
        const qreal diameter = std::max(std::hypot(innerW, innerH), std::sqrt(3.0) * textWidth);
        const qreal side = std::max({diameter, w, h});
        return centered(content, side, side);
    }
    case NodeShape::Rombo:  // il rombo che passa per gli angoli del riquadro interno: assi doppi
        return centered(content, innerW * 2, innerH * 2);
    case NodeShape::Parallelogramma:  // i lati obliqui passano per gli angoli del riquadro interno
        return centered(content, innerW + 2 * h * kParallelogramSlant, h);
    case NodeShape::Pillola:
        return centered(content, w + h / 2, h);
    case NodeShape::Esagono:
        return centered(content, w + 2 * h * kHexagonPoint, h);
    case NodeShape::EsagonoVerticale:
        return centered(content, w, h + 2 * h * kHexagonPoint);
    case NodeShape::Ottagono: {
        const qreal cut = std::min(w, h) * kOctagonCut;
        return centered(content, w + cut, h + cut);
    }
    case NodeShape::ParentesiQuadre:
        return content.adjusted(-kBracketArm, 0, kBracketArm, 0);
    case NodeShape::ParentesiAngolari:
    case NodeShape::DoppieParentesiAngolari:
    case NodeShape::ParentesiTonde:
    case NodeShape::ParentesiGraffe:
    case NodeShape::Angoli:
    case NodeShape::Virgolette: {
        const Growth growth = punctualGrowth(shape);
        return content.adjusted(-growth.side, -growth.vertical, growth.side, growth.vertical);
    }
    case NodeShape::Cuore:
    case NodeShape::Nuvola:
    case NodeShape::Stella:
    case NodeShape::Goccia:
    case NodeShape::Gemma:
    case NodeShape::Scudo:
        return proportionalRect(*unitShape(shape), content, innerW, innerH);
    // Points and notches are added to the sides, so the text without its side padding stays in the
    // rectangular part; the text stays at the centre of the shape (layout and anchors expect it).
    case NodeShape::FrecciaSinistra:
    case NodeShape::FrecciaDestra: {
        const qreal height = h / kArrowBody;
        return centered(content, innerW + 2 * height * kArrowHead, height);
    }
    case NodeShape::Etichetta:
        return centered(content, innerW + 2 * h * kTagPoint, h);
    case NodeShape::Bandiera:
        return centered(content, innerW + 2 * h * kFlagNotch, h);
    case NodeShape::Nota:
        return content;
    case NodeShape::PilaDiSchede:
        return centered(content, w + kStackOffset, h + kStackOffset);
    case NodeShape::SchedaConBordoLaterale:
        return centered(content, w + kSideBar, h);
    case NodeShape::Decisione: {  // the text may reach the sides
        const qreal width = w + 2 * kDecisionGrowth;
        return centered(content, width, width * kDecisionAspect);
    }
    case NodeShape::InputManuale:
        return content.adjusted(0, -kManualInputStrip * h, 0, 0);
    case NodeShape::ProcessoPredefinito:
        return centered(content, w + 2 * kPredefinedGrowth, h);
    case NodeShape::OperazioneManuale:
        return centered(content, w + 2 * kManualOperationGrowth, h);
    case NodeShape::Database:
        return content.adjusted(-kDatabaseGrowth, -2 * kDatabaseCap * h, kDatabaseGrowth, kDatabaseCap * h);
    case NodeShape::Documento:
        return content.adjusted(-kDocumentGrowth, 0, kDocumentGrowth, 2 * kDocumentWave);
    case NodeShape::DocumentiMultipli: {
        // The sheets reach 8 px to the right; the left side grows as much, so the text stays centred.
        const qreal side = kDocumentsFrontGrowth + kDocumentsSheet;
        return content.adjusted(-side, -2 * kDocumentsSheet, side, 2 * kDocumentWave);
    }
    case NodeShape::Ritardo: {
        // The half circle on the right holds the inner box's corner: the +1 px for one line (43 px),
        // more for taller text.
        const qreal reach = std::sqrt(std::max<qreal>(0, h * h / 4 - innerH * innerH / 4));
        return centered(content, std::max(w + 2 * kDelayGrowth, innerW + h - 2 * reach), h);
    }
    case NodeShape::Dati:
        return centered(content, w + 2 * kDataGrowth, h);
    case NodeShape::Connettore:
    case NodeShape::ConnettoreFuoriPagina: {
        const qreal width = std::max(w, kConnectorSize + 2 * kConnectorTop);
        const QRectF wide = centered(content, width, h);
        return wide.adjusted(0, -(kConnectorTop + kConnectorSize + kConnectorGap), 0, 0);
    }
    case NodeShape::CerchioDoppio: {
        const qreal side = std::hypot(w, h) + kDoubleCircleExtra;
        return centered(content, side, side);
    }
    case NodeShape::Visualizzazione:
        return centered(content, innerW + 2 * h * kDisplayPoint, h);
    case NodeShape::Trapezio:
    case NodeShape::TrapezioRovesciato:
        return centered(content, w + 2 * kTrapezoidGrowth, h);
    }
    return content;
}

QPainterPath outline(NodeShape shape, const QRectF &r, qreal roundedRadius) {
    QPainterPath path;
    if (r.isEmpty()) return path;
    const qreal h = r.height();
    switch (shape) {
    case NodeShape::RettangoloArrotondato:
        if (roundedRadius < 0) roundedRadius = kRoundedRadius;
        path.addRoundedRect(r, roundedRadius, roundedRadius);
        break;
    case NodeShape::Rettangolo:
    case NodeShape::LineaSotto:
    case NodeShape::DoppiaLineaSotto:
    case NodeShape::ParentesiQuadre:
    case NodeShape::ParentesiAngolari:  // FEAT-030 stage B: the fill covers the rectangle
    case NodeShape::DoppieParentesiAngolari:
    case NodeShape::ParentesiTonde:
    case NodeShape::ParentesiGraffe:
    case NodeShape::Angoli:
    case NodeShape::Virgolette:
        path.addRect(r);
        break;
    case NodeShape::Ellisse:
    case NodeShape::Cerchio:
        path.addEllipse(r);
        break;
    case NodeShape::Rombo:
        path.addPolygon(QPolygonF{{r.center().x(), r.top()}, {r.right(), r.center().y()}, {r.center().x(), r.bottom()},
                                  {r.left(), r.center().y()}});
        break;
    case NodeShape::Parallelogramma: {
        const qreal slant = h * kParallelogramSlant;
        path.addPolygon(QPolygonF{{r.left() + slant, r.top()}, r.topRight(), {r.right() - slant, r.bottom()}, r.bottomLeft()});
        break;
    }
    case NodeShape::Pillola:
        path.addRoundedRect(r, h / 2, h / 2);
        break;
    case NodeShape::Esagono: {
        const qreal point = h * kHexagonPoint;
        path.addPolygon(QPolygonF{{r.left() + point, r.top()}, {r.right() - point, r.top()}, {r.right(), r.center().y()},
                                  {r.right() - point, r.bottom()}, {r.left() + point, r.bottom()}, {r.left(), r.center().y()}});
        break;
    }
    case NodeShape::EsagonoVerticale: {
        const qreal point = (h - h / (1 + 2 * kHexagonPoint)) / 2;  // la parte aggiunta da outerRect
        path.addPolygon(QPolygonF{{r.center().x(), r.top()}, {r.right(), r.top() + point}, {r.right(), r.bottom() - point},
                                  {r.center().x(), r.bottom()}, {r.left(), r.bottom() - point}, {r.left(), r.top() + point}});
        break;
    }
    case NodeShape::Ottagono: {
        const qreal cut = std::min(r.width(), h) * kOctagonCut / (1 + kOctagonCut);
        path.addPolygon(QPolygonF{{r.left() + cut, r.top()}, {r.right() - cut, r.top()}, {r.right(), r.top() + cut},
                                  {r.right(), r.bottom() - cut}, {r.right() - cut, r.bottom()}, {r.left() + cut, r.bottom()},
                                  {r.left(), r.bottom() - cut}, {r.left(), r.top() + cut}});
        break;
    }
    case NodeShape::Cuore:
    case NodeShape::Nuvola:
    case NodeShape::Stella:
    case NodeShape::Goccia:
    case NodeShape::Gemma:
    case NodeShape::Scudo: {
        QTransform toRect;
        toRect.translate(r.left(), r.top());
        toRect.scale(r.width(), h);
        return toRect.map(unitShape(shape)->path);
    }
    case NodeShape::FrecciaSinistra:
    case NodeShape::FrecciaDestra: {
        const qreal half = h * kArrowBody / 2, head = h * kArrowHead, c = r.center().y();
        QPolygonF arrow{{r.left(), c - half}, {r.right() - head, c - half}, {r.right() - head, r.top()}, {r.right(), c},
                        {r.right() - head, r.bottom()}, {r.right() - head, c + half}, {r.left(), c + half}};
        if (shape == NodeShape::FrecciaSinistra)  // mirrored around the centre
            for (QPointF &point : arrow) point.setX(r.left() + r.right() - point.x());
        path.addPolygon(arrow);
        break;
    }
    case NodeShape::Etichetta: {
        const qreal point = h * kTagPoint;
        path.addPolygon(QPolygonF{r.topLeft(), {r.right() - point, r.top()}, {r.right(), r.center().y()},
                                  {r.right() - point, r.bottom()}, r.bottomLeft()});
        break;
    }
    case NodeShape::Bandiera:
        path.addPolygon(QPolygonF{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft(), {r.left() + h * kFlagNotch, r.center().y()}});
        break;
    case NodeShape::Nota: {
        const qreal fold = noteFold(h);
        path.addPolygon(QPolygonF{r.topLeft(), {r.right() - fold, r.top()}, {r.right(), r.top() + fold}, r.bottomRight(), r.bottomLeft()});
        break;
    }
    case NodeShape::PilaDiSchede: {
        if (roundedRadius < 0) roundedRadius = kRoundedRadius;
        QPainterPath front, back;
        front.addRoundedRect(r.adjusted(0, 0, -kStackOffset, -kStackOffset), roundedRadius, roundedRadius);
        back.addRoundedRect(r.adjusted(kStackOffset, kStackOffset, 0, 0), roundedRadius, roundedRadius);
        return front.united(back);
    }
    case NodeShape::SchedaConBordoLaterale:
        if (roundedRadius < 0) roundedRadius = kRoundedRadius;
        path.addRoundedRect(r, roundedRadius, roundedRadius);
        break;
    case NodeShape::Decisione:
        path.addPolygon(QPolygonF{{r.center().x(), r.top()}, {r.right(), r.center().y()}, {r.center().x(), r.bottom()},
                                  {r.left(), r.center().y()}});
        break;
    case NodeShape::InputManuale: {
        const qreal strip = r.height() - contentIn(shape, r).height();
        path.addPolygon(QPolygonF{{r.left(), r.top() + strip}, r.topRight(), r.bottomRight(), r.bottomLeft()});
        break;
    }
    case NodeShape::ProcessoPredefinito:
        path.addRect(r);
        break;
    case NodeShape::OperazioneManuale:
    case NodeShape::TrapezioRovesciato: {
        const qreal slant = h * kFlowSlant;
        path.addPolygon(QPolygonF{r.topLeft(), r.topRight(), {r.right() - slant, r.bottom()}, {r.left() + slant, r.bottom()}});
        break;
    }
    case NodeShape::Trapezio: {
        const qreal slant = h * kFlowSlant;
        path.addPolygon(QPolygonF{{r.left() + slant, r.top()}, {r.right() - slant, r.top()}, r.bottomRight(), r.bottomLeft()});
        break;
    }
    case NodeShape::Dati: {
        const qreal slant = h * kFlowSlant;
        path.addPolygon(QPolygonF{{r.left() + slant, r.top()}, r.topRight(), {r.right() - slant, r.bottom()}, r.bottomLeft()});
        break;
    }
    case NodeShape::Database: {
        const QRectF top = ellipseCap(r, true), bottom = ellipseCap(r, false);
        path.arcMoveTo(top, 180);
        path.arcTo(top, 180, -180);  // over the top
        path.lineTo(r.right(), bottom.center().y());
        path.arcTo(bottom, 0, -180);  // under the bottom
        break;
    }
    case NodeShape::Documento:
        path.addPolygon(wavyDocument(r));
        break;
    case NodeShape::DocumentiMultipli: {
        QPainterPath front, sheets;
        front.addPolygon(wavyDocument(frontDocument(r)));
        front.closeSubpath();
        for (const int k : {1, 2}) sheets.addRect(sheetBehind(r, k));
        return front.united(sheets);
    }
    case NodeShape::Ritardo: {
        const qreal radius = h / 2;
        path.moveTo(r.left() + kDelayCorner, r.top());
        path.lineTo(r.right() - radius, r.top());
        path.arcTo(QRectF(r.right() - h, r.top(), h, h), 90, -180);
        path.lineTo(r.left() + kDelayCorner, r.bottom());
        path.quadTo(r.bottomLeft(), {r.left(), r.bottom() - kDelayCorner});
        path.lineTo(r.left(), r.top() + kDelayCorner);
        path.quadTo(r.topLeft(), {r.left() + kDelayCorner, r.top()});
        break;
    }
    case NodeShape::Connettore:
        path.addEllipse(connectorMark(r));
        break;
    case NodeShape::ConnettoreFuoriPagina: {
        const QRectF mark = connectorMark(r);
        path.addPolygon(QPolygonF{mark.topLeft(), mark.topRight(), {mark.right(), mark.top() + kOffPageSquare},
                                  {mark.center().x(), mark.bottom()}, {mark.left(), mark.top() + kOffPageSquare}});
        break;
    }
    case NodeShape::CerchioDoppio:
        path.addEllipse(r);
        break;
    case NodeShape::Visualizzazione: {
        const qreal point = h * kDisplayPoint;
        path.addPolygon(QPolygonF{{r.left() + point, r.top()}, r.topRight(), r.bottomRight(), {r.left() + point, r.bottom()},
                                  {r.left(), r.center().y()}});
        break;
    }
    }
    path.closeSubpath();
    return path;
}

QList<QLineF> borderLines(NodeShape shape, const QRectF &r, qreal borderWidth) {
    if (!hasOpenBorder(shape) || r.isEmpty()) return {};
    // Le linee stanno dentro il rettangolo: il loro tratto non esce sotto il nodo.
    const qreal half = borderWidth / 2;
    if (shape == NodeShape::ParentesiQuadre) {
        // The pen's cap adds half the stroke at each end: the lines stop short of the corners and arms.
        QList<QLineF> lines;
        for (const bool right : {false, true}) {
            const qreal side = right ? r.right() - half : r.left() + half;
            const qreal end = right ? r.right() - kBracketArm + half : r.left() + kBracketArm - half;
            lines << QLineF(side, r.top() + half, side, r.bottom() - half) << QLineF(side, r.top() + half, end, r.top() + half)
                  << QLineF(side, r.bottom() - half, end, r.bottom() - half);
        }
        return lines;
    }
    if (!isUnderline(shape)) return {};  // FEAT-030: the other Puntuale shapes draw marks()
    if (shape == NodeShape::LineaSotto) return {QLineF(r.left(), r.bottom() - half, r.right(), r.bottom() - half)};
    const qreal second = r.bottom() - half;
    const qreal first = second - borderWidth - kUnderlineGap;
    return {QLineF(r.left(), first, r.right(), first), QLineF(r.left(), second, r.right(), second)};
}

QPointF branchAnchor(NodeShape shape, const QRectF &r, qreal borderWidth, bool right) {
    if (isUnderline(shape)) {
        const QLineF line = borderLines(shape, r, borderWidth).front();
        return right ? line.p2() : line.p1();
    }
    if (shape >= NodeShape::Cuore) {
        // FEAT-030: where the row through the text's centre leaves the outline; for the connectors the
        // row through their mark, above the text.
        const qreal y = isConnector(shape) ? connectorMark(r).center().y() : contentIn(shape, r).center().y();
        const QList<qreal> xs = rowCrossings(outline(shape, r).toSubpathPolygons(), y);
        if (!xs.isEmpty()) return {right ? xs.back() : xs.front(), y};
    }
    qreal x = right ? r.right() : r.left();
    // Parallelogramma: a meta' altezza il lato obliquo e' rientrato di meta' inclinazione.
    if (shape == NodeShape::Parallelogramma) x += (right ? -1 : 1) * r.height() * kParallelogramSlant / 2;
    return {x, r.center().y()};
}

bool startsOnBox(NodeShape shape) {
    return shape == NodeShape::RettangoloArrotondato || shape == NodeShape::Rettangolo || hasOpenBorder(shape) || isConnector(shape);
}

QPointF rayExit(const QPainterPath &outline, const QPointF &from, const QPointF &target) {
    const QLineF direction(from, target);
    if (direction.length() == 0) return from;
    const QRectF bounds = outline.boundingRect();
    QLineF ray = direction;
    ray.setLength(direction.length() + QLineF(bounds.topLeft(), bounds.bottomRight()).length() + QLineF(from, bounds.center()).length());
    QPointF farthest = from;
    qreal farthestLength = 0;
    for (const QPolygonF &polygon : outline.toSubpathPolygons()) {
        for (qsizetype i = 0; i < polygon.size(); ++i) {
            QPointF crossing;
            if (ray.intersects(QLineF(polygon[i], polygon[(i + 1) % polygon.size()]), &crossing) != QLineF::BoundedIntersection) continue;
            const qreal length = QLineF(from, crossing).length();
            if (length > farthestLength) farthest = crossing, farthestLength = length;
        }
    }
    return farthest;
}

}  // namespace NodeShapes
