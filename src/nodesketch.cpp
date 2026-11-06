#include "nodesketch.h"

#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <vector>

namespace NodeSketch {

namespace {
// Distanza fra i punti di un tratto a mano libera: abbastanza fitti da seguire le curve della
// forma, abbastanza radi perche' il tratto ondeggi piano invece di tremare.
constexpr qreal kSampleStep = 14;
// Un vertice con una svolta oltre questo angolo resta nel tratto (gli spigoli non si smussano).
constexpr qreal kCornerDegrees = 30;
// Motivi (foto dell'utente del 2026-10-16, attachments/FEAT-020/24-27, ingrandite circa 3 volte):
// pennellate larghe nel colore del riempimento, larghe circa quanto il passo. Ogni
// pennellata e' un po' inclinata e piegata a caso: le righe non sono parallele e fra due vicine
// restano sottili scaglie affusolate da cui si vede lo sfondo dove si allontanano, mentre dove si
// avvicinano si sovrappongono. Gli estremi sono tagliati netti
// ed escono dalla forma come denti. Misure stimate dalle foto.
struct PatternSpec {
    qreal angleDegrees = 0;  // angolo delle righe (positivo in senso orario sullo schermo)
    qreal period = 0;        // passo fra una riga e la successiva
    qreal penWidth = 0;      // spessore della pennellata, circa uguale al passo
    qreal tilt = 0;          // spostamento laterale massimo di ciascun estremo
    qreal bow = 0;           // spostamento laterale massimo della piega a meta'
    qreal overshoot = 0;     // quanto gli estremi possono uscire dal contorno
};

PatternSpec specFor(NodeFillStyle style) {
    switch (style) {
    case NodeFillStyle::PennellateDiagonali: return {-45, 8, 8, 1.6, 1, 1.5};      // foto 24
    case NodeFillStyle::TratteggioDiagonale: return {-50, 3.2, 3, 1, 0.8, 1.5};    // foto 25
    case NodeFillStyle::PennellateOrizzontali: return {0, 10, 10, 1.2, 1, 2};      // foto 26
    case NodeFillStyle::TratteggioOrizzontale: return {0, 3.2, 3, 1, 0.8, 2.5};    // foto 27
    case NodeFillStyle::Pieno:
    case NodeFillStyle::Nessuno:
    case NodeFillStyle::PienoManoLibera:
        break;
    }
    return {};
}

// splitmix64 (https://prng.di.unimi.it/splitmix64.c): stessi numeri su ogni piattaforma.
class Random {
public:
    explicit Random(quint64 seed) : m_state(seed) {}
    qreal unit() {
        quint64 z = (m_state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<qreal>(z >> 11) / static_cast<qreal>(1ull << 53);
    }
    qreal between(qreal low, qreal high) { return low + (high - low) * unit(); }
    QPointF jitter(qreal amount) { return {between(-amount, amount), between(-amount, amount)}; }

private:
    quint64 m_state;
};

// Semi diversi per contorno, area e motivo, cosi' bordo e riempimento non ondeggiano insieme.
constexpr quint64 kOutlineSalt = 0x0b;
constexpr quint64 kAreaSalt = 0xa7;
constexpr quint64 kPatternSalt = 0x9a7;

QPointF midpoint(const QPointF &a, const QPointF &b) { return (a + b) / 2; }

bool isCorner(const QPointF &previous, const QPointF &at, const QPointF &next) {
    const QPointF in = at - previous, out = next - at;
    const qreal cross = in.x() * out.y() - in.y() * out.x();
    const qreal dot = in.x() * out.x() + in.y() * out.y();
    return std::abs(std::atan2(cross, dot)) > kCornerDegrees * M_PI / 180;
}

// Punti equidistanti lungo il poligono chiuso, piu' gli spigoli.
std::vector<QPointF> resample(QPolygonF polygon) {
    if (polygon.size() > 1 && polygon.first() == polygon.last()) polygon.removeLast();
    const qsizetype n = polygon.size();
    if (n < 3) return {polygon.cbegin(), polygon.cend()};
    qreal perimeter = 0;
    for (qsizetype i = 0; i < n; ++i) perimeter += QLineF(polygon[i], polygon[(i + 1) % n]).length();
    const qreal step = perimeter / std::max(8.0, std::ceil(perimeter / kSampleStep));
    std::vector<QPointF> points;
    qreal offset = 0;  // distanza dall'inizio del lato al prossimo punto
    for (qsizetype i = 0; i < n; ++i) {
        const QPointF a = polygon[i], b = polygon[(i + 1) % n];
        const qreal length = QLineF(a, b).length();
        if (offset > 0 && isCorner(polygon[(i + n - 1) % n], a, b)) points.push_back(a);
        for (; offset < length; offset += step) points.push_back(a + (b - a) * (offset / length));
        offset -= length;
    }
    return points;
}

// Tratto morbido che passa vicino ai punti (curve quadratiche fra i punti medi).
void addSmoothOpen(QPainterPath &path, const std::vector<QPointF> &points) {
    if (points.size() < 2) return;
    path.moveTo(points.front());
    for (size_t i = 1; i + 1 < points.size(); ++i) path.quadTo(points[i], midpoint(points[i], points[i + 1]));
    path.lineTo(points.back());
}

std::vector<QPointF> outlinePoints(const QPainterPath &outline) {
    const QList<QPolygonF> polygons = outline.toSubpathPolygons();
    return polygons.isEmpty() ? std::vector<QPointF>{} : resample(polygons.front());
}

// Righe parallele dentro il contorno, a distanza spec.period; spec.angleDegrees e' l'angolo delle
// righe rispetto all'orizzontale (positivo in senso orario sullo schermo). Ogni segmento va da un
// lato all'altro della forma.
std::vector<QLineF> hatch(const QPainterPath &outline, const PatternSpec &spec) {
    QTransform toHorizontal;
    toHorizontal.rotate(-spec.angleDegrees);
    const QTransform back = toHorizontal.inverted();
    const QList<QPolygonF> polygons = toHorizontal.map(outline).toSubpathPolygons();
    QRectF bounds;
    for (const QPolygonF &polygon : polygons) bounds |= polygon.boundingRect();
    std::vector<QLineF> lines;
    for (qreal y = bounds.top() + spec.period / 2; y < bounds.bottom(); y += spec.period) {
        std::vector<qreal> crossings;
        for (const QPolygonF &polygon : polygons)
            for (qsizetype i = 0; i < polygon.size(); ++i) {
                const QPointF a = polygon[i], b = polygon[(i + 1) % polygon.size()];
                if ((a.y() <= y) != (b.y() <= y)) crossings.push_back(a.x() + (y - a.y()) * (b.x() - a.x()) / (b.y() - a.y()));
            }
        std::sort(crossings.begin(), crossings.end());
        for (size_t i = 0; i + 1 < crossings.size(); i += 2)
            lines.push_back(back.map(QLineF(crossings[i], y, crossings[i + 1], y)));
    }
    return lines;
}

// Pennellata: estremi che escono dal contorno o restano un poco dentro, ciascuno spostato di lato
// a caso (la riga si inclina), e una piega a meta'. Due segmenti; il secondo ricomincia mezzo
// spessore prima della piega, cosi' fra i due non resta una fessura.
void addStroke(QList<QLineF> &lines, const QLineF &row, const PatternSpec &spec, Random &random) {
    if (row.length() < 1) return;
    const QPointF direction = (row.p2() - row.p1()) / row.length();
    const QPointF normal(-direction.y(), direction.x());
    const QPointF start = row.p1() - direction * random.between(-1, spec.overshoot) + normal * random.between(-spec.tilt, spec.tilt);
    const QPointF end = row.p2() + direction * random.between(-1, spec.overshoot) + normal * random.between(-spec.tilt, spec.tilt);
    const QPointF middle = midpoint(start, end) + normal * random.between(-spec.bow, spec.bow);
    lines.append({start, middle});
    const QLineF second(middle, end);
    if (second.length() <= spec.penWidth) {
        lines.append(second);
        return;
    }
    lines.append({middle - (end - middle) / second.length() * (spec.penWidth / 2), end});
}
}  // namespace

quint64 seedFor(const QString &id) {
    quint64 hash = 0xcbf29ce484222325ull;
    for (const QChar character : id) {
        hash ^= character.unicode();
        hash *= 0x100000001b3ull;
    }
    return hash;
}

bool isFreehand(NodeBorderStyle style) {
    return style == NodeBorderStyle::ContinuoManoLibera || style == NodeBorderStyle::TratteggiatoManoLibera;
}

QPainterPath roughOutline(const QPainterPath &outline, quint64 seed) {
    const std::vector<QPointF> points = outlinePoints(outline);
    if (points.size() < 3) return outline;
    Random random(seed ^ kOutlineSalt);
    // Il tratto parte da un punto diverso per ogni nodo e alla fine ripassa i primi due punti
    // con uno scostamento nuovo: la sovrapposizione si vede come nelle foto 05 e 06.
    const size_t start = static_cast<size_t>(random.unit() * points.size()) % points.size();
    std::vector<QPointF> stroke;
    for (size_t i = 0; i <= points.size() + 1; ++i)
        stroke.push_back(points[(start + i) % points.size()] + random.jitter(kBorderWobble));
    QPainterPath path;
    addSmoothOpen(path, stroke);
    return path;
}

QPainterPath roughLine(const QLineF &line, quint64 seed) {
    QPainterPath path;
    if (line.length() < 1) return path;
    Random random(seed ^ kOutlineSalt);
    const int segments = std::max(2, static_cast<int>(std::ceil(line.length() / kSampleStep)));
    const QPointF direction = (line.p2() - line.p1()) / line.length();
    const QPointF normal(-direction.y(), direction.x());
    std::vector<QPointF> points;
    for (int i = 0; i <= segments; ++i)
        points.push_back(line.pointAt(static_cast<qreal>(i) / segments) + normal * random.between(-kBorderWobble, kBorderWobble));
    points.front() -= direction * random.between(0, kBorderWobble);
    points.back() += direction * random.between(0, kBorderWobble);
    addSmoothOpen(path, points);
    return path;
}

QPainterPath roughPath(const QPainterPath &path, quint64 seed) {
    QPainterPath rough;
    Random random(seed ^ kOutlineSalt);
    for (const QPolygonF &polyline : path.toSubpathPolygons()) {
        if (polyline.size() < 2) continue;
        // Points every kSampleStep along the open polyline, plus its corners and both ends.
        std::vector<QPointF> points{polyline.front()};
        qreal offset = kSampleStep;
        for (qsizetype i = 0; i + 1 < polyline.size(); ++i) {
            const QPointF a = polyline[i], b = polyline[i + 1];
            const qreal length = QLineF(a, b).length();
            if (i > 0 && isCorner(polyline[i - 1], a, b)) { points.push_back(a); offset = kSampleStep; }
            for (; offset < length; offset += kSampleStep) points.push_back(a + (b - a) * (offset / length));
            offset -= length;
        }
        points.push_back(polyline.back());
        for (QPointF &point : points) point += random.jitter(kBorderWobble);
        addSmoothOpen(rough, points);
    }
    return rough;
}

QPainterPath roughArea(const QPainterPath &outline, quint64 seed) {
    std::vector<QPointF> points = outlinePoints(outline);
    if (points.size() < 3) return outline;
    Random random(seed ^ kAreaSalt);
    for (QPointF &point : points) point += random.jitter(kFillOvershoot);
    QPainterPath path;
    path.moveTo(midpoint(points.back(), points.front()));
    for (size_t i = 0; i < points.size(); ++i) path.quadTo(points[i], midpoint(points[i], points[(i + 1) % points.size()]));
    path.closeSubpath();
    return path;
}

Pattern pattern(NodeFillStyle style, const QPainterPath &outline, quint64 seed) {
    const PatternSpec spec = specFor(style);
    Pattern result{{}, spec.penWidth};
    if (outline.isEmpty() || spec.period <= 0) return result;
    Random random(seed ^ kPatternSalt);
    for (const QLineF &row : hatch(outline, spec)) addStroke(result.lines, row, spec, random);
    return result;
}

QColor opaqueOver(const QColor &color, const QColor &background) {
    const qreal alpha = color.alphaF();
    return QColor::fromRgbF(color.redF() * alpha + background.redF() * (1 - alpha),
                            color.greenF() * alpha + background.greenF() * (1 - alpha),
                            color.blueF() * alpha + background.blueF() * (1 - alpha));
}

}  // namespace NodeSketch
