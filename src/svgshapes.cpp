// FEAT-070 stage 3: the drawn shapes of an SVG sticker, edited one by one. The file is read into a
// small tree (Qt Core's stream reader, every token kept), changed and written back.

#include "svgdocument.h"

#include <QPainterPathStroker>
#include <QtMath>
#include <QRegularExpression>
#include <QHash>
#include <QSet>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace {
const QStringList kShapeTags{QStringLiteral("path"),    QStringLiteral("rect"),     QStringLiteral("circle"), QStringLiteral("ellipse"),
                             QStringLiteral("polygon"), QStringLiteral("polyline"), QStringLiteral("line")};
// Content that is not drawn where it is written (as in svgdocument.cpp).
const QStringList kNotRendered{QStringLiteral("defs"), QStringLiteral("clipPath"), QStringLiteral("mask"), QStringLiteral("symbol"),
                               QStringLiteral("pattern"), QStringLiteral("marker"), QStringLiteral("linearGradient"),
                               QStringLiteral("radialGradient"), QStringLiteral("filter")};
// Attributes of the geometry of a shape that becomes a <path>.
const QStringList kGeometry{QStringLiteral("x"),  QStringLiteral("y"),  QStringLiteral("width"), QStringLiteral("height"),
                            QStringLiteral("rx"), QStringLiteral("ry"), QStringLiteral("cx"),    QStringLiteral("cy"),
                            QStringLiteral("r"),  QStringLiteral("x1"), QStringLiteral("y1"),    QStringLiteral("x2"),
                            QStringLiteral("y2"), QStringLiteral("points")};

struct Node {
    enum Kind { Element, Characters, CData, Comment, Instruction, Dtd, Entity };
    Kind kind = Element;
    QString name;  // qualified element name, instruction target or entity name
    QString text;  // characters, comment, instruction data, DTD
    QList<std::pair<QString, QString>> namespaces;  // prefix, uri
    QList<std::pair<QString, QString>> attributes;
    std::vector<std::unique_ptr<Node>> children;
    Node *parent = nullptr;

    QString localName() const { return name.section(QLatin1Char(':'), -1); }
    QString attribute(const QString &key) const {
        for (const auto &[name, value] : attributes)
            if (name == key) return value;
        return {};
    }
    bool hasAttribute(const QString &key) const {
        return std::any_of(attributes.cbegin(), attributes.cend(), [&](const auto &a) { return a.first == key; });
    }
    void setAttribute(const QString &key, const QString &value) {
        for (auto &[name, current] : attributes)
            if (name == key) {
                current = value;
                return;
            }
        attributes.append({key, value});
    }
    void removeAttribute(const QString &key) {
        attributes.removeIf([&](const auto &a) { return a.first == key; });
    }
};

struct Tree {
    Node document;  // holds the top-level tokens; the <svg> element is one of its children
    bool hasStart = false;
    QString version;
    bool standalone = false;

    Node *root() const {
        for (const auto &child : document.children)
            if (child->kind == Node::Element) return child.get();
        return nullptr;
    }
};

bool parse(const QByteArray &data, Tree &tree) {
    QXmlStreamReader reader(data);
    Node *current = &tree.document;
    auto add = [&](Node::Kind kind) {
        auto node = std::make_unique<Node>();
        node->kind = kind;
        node->parent = current;
        Node *raw = node.get();
        current->children.push_back(std::move(node));
        return raw;
    };
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartDocument:
            tree.hasStart = true;
            tree.version = reader.documentVersion().toString();
            tree.standalone = reader.isStandaloneDocument();
            break;
        case QXmlStreamReader::DTD: add(Node::Dtd)->text = reader.text().toString(); break;
        case QXmlStreamReader::StartElement: {
            Node *node = add(Node::Element);
            node->name = reader.qualifiedName().toString();
            for (const QXmlStreamNamespaceDeclaration &ns : reader.namespaceDeclarations())
                node->namespaces.append({ns.prefix().toString(), ns.namespaceUri().toString()});
            for (const QXmlStreamAttribute &attribute : reader.attributes())
                node->attributes.append({attribute.qualifiedName().toString(), attribute.value().toString()});
            current = node;
            break;
        }
        case QXmlStreamReader::EndElement: current = current->parent; break;
        case QXmlStreamReader::Characters: add(reader.isCDATA() ? Node::CData : Node::Characters)->text = reader.text().toString(); break;
        case QXmlStreamReader::Comment: add(Node::Comment)->text = reader.text().toString(); break;
        case QXmlStreamReader::ProcessingInstruction: {
            Node *node = add(Node::Instruction);
            node->name = reader.processingInstructionTarget().toString();
            node->text = reader.processingInstructionData().toString();
            break;
        }
        case QXmlStreamReader::EntityReference: add(Node::Entity)->name = reader.name().toString(); break;
        default: break;
        }
    }
    return !reader.hasError() && tree.root() && tree.root()->localName() == QLatin1String("svg");
}

void writeNode(QXmlStreamWriter &writer, const Node &node) {
    switch (node.kind) {
    case Node::Element:
        writer.writeStartElement(node.name);
        for (const auto &[prefix, uri] : node.namespaces)
            writer.writeAttribute(prefix.isEmpty() ? QStringLiteral("xmlns") : QStringLiteral("xmlns:") + prefix, uri);
        for (const auto &[name, value] : node.attributes) writer.writeAttribute(name, value);
        for (const auto &child : node.children) writeNode(writer, *child);
        writer.writeEndElement();
        break;
    case Node::Characters: writer.writeCharacters(node.text); break;
    case Node::CData: writer.writeCDATA(node.text); break;
    case Node::Comment: writer.writeComment(node.text); break;
    case Node::Instruction: writer.writeProcessingInstruction(node.name, node.text); break;
    case Node::Dtd: writer.writeDTD(node.text); break;
    case Node::Entity: writer.writeEntityReference(node.name); break;
    }
}

QByteArray write(const Tree &tree) {
    QByteArray out;
    QXmlStreamWriter writer(&out);
    if (tree.hasStart) {
        if (tree.standalone) writer.writeStartDocument(tree.version.isEmpty() ? QStringLiteral("1.0") : tree.version, true);
        else writer.writeStartDocument(tree.version.isEmpty() ? QStringLiteral("1.0") : tree.version);
    }
    for (const auto &child : tree.document.children) writeNode(writer, *child);
    return out;
}

// Every drawn shape, in paint order (depth first), outside the content that is not drawn there.
void collectShapes(Node *node, QList<Node *> &shapes) {
    for (const auto &child : node->children) {
        if (child->kind != Node::Element || kNotRendered.contains(child->localName())) continue;
        if (kShapeTags.contains(child->localName())) shapes.append(child.get());
        collectShapes(child.get(), shapes);
    }
}

Node *findById(Node *node, const QString &id) {
    for (const auto &child : node->children) {
        if (child->kind != Node::Element) continue;
        if (child->attribute(QStringLiteral("id")) == id) return child.get();
        if (Node *found = findById(child.get(), id)) return found;
    }
    return nullptr;
}

QString number(qreal value) { return QString::number(value, 'g', 7); }

// A plain number of an attribute ("12", "12px"); 0 otherwise (percentages are not handled).
qreal length(const QString &text) {
    QString value = text.trimmed();
    if (value.endsWith(QLatin1String("px"))) value.chop(2);
    bool ok = false;
    const qreal result = value.toDouble(&ok);
    return ok ? result : 0;
}

// Reads SVG numbers and flags one by one ("1.5.5" is 1.5 and .5, "-1-2" is -1 and -2).
class Scanner {
public:
    explicit Scanner(const QString &text) : m_text(text) {}
    bool atEnd() {
        skip();
        return m_pos >= m_text.size();
    }
    QChar peek() {
        skip();
        return m_pos < m_text.size() ? m_text[m_pos] : QChar();
    }
    QChar take() { return m_text[m_pos++]; }
    bool startsNumber() {
        const QChar c = peek();
        return c.isDigit() || c == QLatin1Char('-') || c == QLatin1Char('+') || c == QLatin1Char('.');
    }
    // [+-] digits [. digits] [e [+-] digits], read by hand: a regular expression per number made a
    // 2 MB sticker's shapes take half a second.
    bool number(qreal *value) {
        skip();
        const qsizetype begin = m_pos;
        qsizetype i = m_pos;
        const qsizetype size = m_text.size();
        auto digits = [&] {
            const qsizetype from = i;
            while (i < size && m_text[i].isDigit()) ++i;
            return i > from;
        };
        if (i < size && (m_text[i] == QLatin1Char('+') || m_text[i] == QLatin1Char('-'))) ++i;
        bool any = digits();
        if (i < size && m_text[i] == QLatin1Char('.')) {
            ++i;
            any = digits() || any;
        }
        if (!any) return false;
        if (i < size && (m_text[i] == QLatin1Char('e') || m_text[i] == QLatin1Char('E'))) {
            qsizetype e = i + 1;
            if (e < size && (m_text[e] == QLatin1Char('+') || m_text[e] == QLatin1Char('-'))) ++e;
            if (e < size && m_text[e].isDigit()) {
                i = e;
                digits();
            }
        }
        bool ok = false;
        *value = QStringView(m_text).mid(begin, i - begin).toDouble(&ok);
        m_pos = i;
        return ok;
    }
    bool flag(bool *value) {  // arc flags may be written without separators ("a1 1 0 00 1 1")
        skip();
        if (m_pos >= m_text.size() || (m_text[m_pos] != QLatin1Char('0') && m_text[m_pos] != QLatin1Char('1'))) return false;
        *value = m_text[m_pos++] == QLatin1Char('1');
        return true;
    }

private:
    void skip() {
        while (m_pos < m_text.size() && (m_text[m_pos].isSpace() || m_text[m_pos] == QLatin1Char(','))) ++m_pos;
    }
    const QString &m_text;
    qsizetype m_pos = 0;
};

// An elliptical arc as cubic Béziers (SVG 1.1 implementation notes F.6.5 and F.6.6).
void arcTo(QPainterPath &path, const QPointF &from, qreal rx, qreal ry, qreal angle, bool large, bool sweep, const QPointF &to) {
    if (from == to) return;
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (rx == 0 || ry == 0) {
        path.lineTo(to);
        return;
    }
    const qreal phi = qDegreesToRadians(angle), cosPhi = std::cos(phi), sinPhi = std::sin(phi);
    const qreal dx = (from.x() - to.x()) / 2, dy = (from.y() - to.y()) / 2;
    const qreal x1 = cosPhi * dx + sinPhi * dy, y1 = -sinPhi * dx + cosPhi * dy;
    const qreal lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1) {
        rx *= std::sqrt(lambda);
        ry *= std::sqrt(lambda);
    }
    const qreal numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const qreal denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    qreal factor = denominator == 0 ? 0 : std::sqrt(qMax<qreal>(0, numerator / denominator));
    if (large == sweep) factor = -factor;
    const qreal cx1 = factor * rx * y1 / ry, cy1 = -factor * ry * x1 / rx;
    const QPointF center(cosPhi * cx1 - sinPhi * cy1 + (from.x() + to.x()) / 2, sinPhi * cx1 + cosPhi * cy1 + (from.y() + to.y()) / 2);
    auto vectorAngle = [](qreal ux, qreal uy, qreal vx, qreal vy) {
        return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    };
    const qreal theta = vectorAngle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
    qreal delta = vectorAngle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
    if (!sweep && delta > 0) delta -= 2 * M_PI;
    if (sweep && delta < 0) delta += 2 * M_PI;
    const int parts = int(std::ceil(std::abs(delta) / (M_PI / 2) - 1e-9));
    const qreal step = delta / parts, k = 4.0 / 3 * std::tan(step / 4);
    auto point = [&](qreal t, qreal ox, qreal oy) {  // on the ellipse, plus a tangent offset
        const qreal x = rx * (std::cos(t) - ox * std::sin(t)), y = ry * (std::sin(t) + oy * std::cos(t));
        return QPointF(cosPhi * x - sinPhi * y + center.x(), sinPhi * x + cosPhi * y + center.y());
    };
    for (int i = 0; i < parts; ++i) {
        const qreal a = theta + i * step, b = a + step;
        const QPointF end = i + 1 == parts ? to : point(b, 0, 0);
        path.cubicTo(point(a, k, k), point(b, -k, -k), end);
    }
}

// The path data of a <path> (all commands); a mistake ends it there, as SVG draws up to the error.
QPainterPath parsePathData(const QString &d) {
    QPainterPath path;
    Scanner s(d);
    QPointF current, start, lastControl;
    QChar command, previous;
    auto read = [&](qreal *v) { return s.number(v); };
    while (!s.atEnd()) {
        if (!s.startsNumber()) command = s.take();
        else if (command.isNull() || command.toUpper() == QLatin1Char('Z')) break;  // numbers with no command
        const bool relative = command.isLower();
        const QPointF base = relative ? current : QPointF();
        const QChar c = command.toUpper();
        qreal a = 0, b = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        if (c == QLatin1Char('Z')) {
            path.closeSubpath();
            current = start;
            previous = c;
            continue;
        }
        if (c == QLatin1Char('M')) {
            if (!read(&a) || !read(&b)) break;
            current = base + QPointF(a, b);
            start = current;
            path.moveTo(current);
            command = relative ? QLatin1Char('l') : QLatin1Char('L');  // further pairs are lines
        } else if (c == QLatin1Char('L')) {
            if (!read(&a) || !read(&b)) break;
            current = base + QPointF(a, b);
            path.lineTo(current);
        } else if (c == QLatin1Char('H')) {
            if (!read(&a)) break;
            current.setX(relative ? current.x() + a : a);
            path.lineTo(current);
        } else if (c == QLatin1Char('V')) {
            if (!read(&a)) break;
            current.setY(relative ? current.y() + a : a);
            path.lineTo(current);
        } else if (c == QLatin1Char('C')) {
            if (!read(&x1) || !read(&y1) || !read(&x2) || !read(&y2) || !read(&a) || !read(&b)) break;
            lastControl = base + QPointF(x2, y2);
            current = base + QPointF(a, b);
            path.cubicTo(base + QPointF(x1, y1), lastControl, current);
        } else if (c == QLatin1Char('S')) {
            if (!read(&x2) || !read(&y2) || !read(&a) || !read(&b)) break;
            const bool smooth = previous == QLatin1Char('C') || previous == QLatin1Char('S');
            const QPointF first = smooth ? 2 * current - lastControl : current;
            lastControl = base + QPointF(x2, y2);
            current = base + QPointF(a, b);
            path.cubicTo(first, lastControl, current);
        } else if (c == QLatin1Char('Q')) {
            if (!read(&x1) || !read(&y1) || !read(&a) || !read(&b)) break;
            lastControl = base + QPointF(x1, y1);
            current = base + QPointF(a, b);
            path.quadTo(lastControl, current);
        } else if (c == QLatin1Char('T')) {
            if (!read(&a) || !read(&b)) break;
            const bool smooth = previous == QLatin1Char('Q') || previous == QLatin1Char('T');
            lastControl = smooth ? 2 * current - lastControl : current;
            current = base + QPointF(a, b);
            path.quadTo(lastControl, current);
        } else if (c == QLatin1Char('A')) {
            qreal rx = 0, ry = 0, angle = 0;
            bool large = false, sweep = false;
            if (!read(&rx) || !read(&ry) || !read(&angle) || !s.flag(&large) || !s.flag(&sweep) || !read(&a) || !read(&b)) break;
            const QPointF to = base + QPointF(a, b);
            arcTo(path, current, rx, ry, angle, large, sweep, to);
            current = to;
        } else {
            break;  // an unknown command
        }
        previous = c;
    }
    return path;
}

QList<qreal> numbers(const QString &text) {
    QList<qreal> result;
    Scanner s(text);
    qreal value = 0;
    while (!s.atEnd() && s.number(&value)) result.append(value);
    return result;
}

// The geometry of a shape element in its own coordinates.
QPainterPath elementPath(const Node &node) {
    const QString tag = node.localName();
    auto get = [&](const char *key) { return length(node.attribute(QString::fromLatin1(key))); };
    QPainterPath path;
    if (tag == QLatin1String("path")) {
        path = parsePathData(node.attribute(QStringLiteral("d")));
    } else if (tag == QLatin1String("rect")) {
        const QRectF rect(get("x"), get("y"), get("width"), get("height"));
        qreal rx = get("rx"), ry = get("ry");
        if (!node.hasAttribute(QStringLiteral("rx"))) rx = ry;
        if (!node.hasAttribute(QStringLiteral("ry"))) ry = rx;
        if (rx > 0 && ry > 0) path.addRoundedRect(rect, qMin(rx, rect.width() / 2), qMin(ry, rect.height() / 2));
        else path.addRect(rect);
    } else if (tag == QLatin1String("circle")) {
        path.addEllipse(QPointF(get("cx"), get("cy")), get("r"), get("r"));
    } else if (tag == QLatin1String("ellipse")) {
        path.addEllipse(QPointF(get("cx"), get("cy")), get("rx"), get("ry"));
    } else if (tag == QLatin1String("line")) {
        path.moveTo(get("x1"), get("y1"));
        path.lineTo(get("x2"), get("y2"));
    } else if (tag == QLatin1String("polyline") || tag == QLatin1String("polygon")) {
        const QList<qreal> points = numbers(node.attribute(QStringLiteral("points")));
        for (int i = 0; i + 1 < points.size(); i += 2) {
            if (i == 0) path.moveTo(points[0], points[1]);
            else path.lineTo(points[i], points[i + 1]);
        }
        if (tag == QLatin1String("polygon")) path.closeSubpath();
    }
    const QString rule = node.attribute(QStringLiteral("fill-rule"));
    path.setFillRule(rule == QLatin1String("evenodd") ? Qt::OddEvenFill : Qt::WindingFill);
    return path;
}

// One transform attribute (SVG 1.1 "transform" lists) as a QTransform: the first in the list is
// applied last, so the matrices are multiplied from the end.
QTransform parseTransform(const QString &text) {
    static const QRegularExpression item(QStringLiteral(R"((matrix|translate|scale|rotate|skewX|skewY)\s*\(([^)]*)\))"));
    QList<QTransform> list;
    QRegularExpressionMatchIterator it = item.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString name = m.captured(1);
        const QList<qreal> v = numbers(m.captured(2));
        QTransform t;
        if (name == QLatin1String("matrix") && v.size() == 6) t = QTransform(v[0], v[1], v[2], v[3], v[4], v[5]);
        else if (name == QLatin1String("translate") && !v.isEmpty()) t.translate(v[0], v.value(1));
        else if (name == QLatin1String("scale") && !v.isEmpty()) t.scale(v[0], v.size() > 1 ? v[1] : v[0]);
        else if (name == QLatin1String("rotate") && !v.isEmpty()) {
            t.translate(v.value(1), v.value(2));
            t.rotate(v[0]);
            t.translate(-v.value(1), -v.value(2));
        } else if (name == QLatin1String("skewX") && !v.isEmpty()) t = QTransform(1, 0, std::tan(qDegreesToRadians(v[0])), 1, 0, 0);
        else if (name == QLatin1String("skewY") && !v.isEmpty()) t = QTransform(1, std::tan(qDegreesToRadians(v[0])), 0, 1, 0, 0);
        list.append(t);
    }
    QTransform result;
    for (auto i = list.size() - 1; i >= 0; --i) result = result * list[i];
    return result;
}

// Element coordinates to viewBox units: the element's own transform, then each group's up to the root.
// cache (optional) keeps the groups' matrices while many shapes are read.
QTransform ctm(const Node *node, bool own, QHash<const Node *, QTransform> *cache = nullptr) {
    const Node *start = own ? node : node->parent;
    if (!start || !start->parent || !start->parent->parent) return {};  // <svg> itself or above
    if (cache) {
        const auto found = cache->constFind(start);
        if (found != cache->cend()) return *found;
    }
    const QString text = start->attribute(QStringLiteral("transform"));
    const QTransform result = (text.isEmpty() ? QTransform() : parseTransform(text)) * ctm(start, false, cache);
    if (cache) cache->insert(start, result);
    return result;
}

bool isFilled(const Node &node) {
    if (node.localName() == QLatin1String("line")) return false;  // a line has no inside
    if (node.attribute(QStringLiteral("fill")).trimmed() == QLatin1String("none")) return false;
    const QString style = node.attribute(QStringLiteral("style"));
    if (!style.contains(QLatin1String("fill"))) return true;
    static const QRegularExpression none(QStringLiteral(R"((?<![\w-])fill\s*:\s*none)"));
    return !none.match(style).hasMatch();
}

QList<SvgDocument::SubPath> toSubPaths(const QPainterPath &path) {
    QList<SvgDocument::SubPath> result;
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        if (e.isMoveTo() || result.isEmpty()) {
            result.append({QPointF(e), {}, false});
            if (e.isMoveTo()) continue;
        }
        SvgDocument::SubPath &sub = result.last();
        if (e.isLineTo()) {
            sub.segments.append({false, {}, {}, QPointF(e)});
        } else if (e.isCurveTo() && i + 2 < path.elementCount()) {
            sub.segments.append({true, QPointF(e), QPointF(path.elementAt(i + 1)), QPointF(path.elementAt(i + 2))});
            i += 2;
        }
    }
    // Back at the start: a closed sub-path, its last straight line drawn by "Z".
    auto same = [](const QPointF &a, const QPointF &b) { return std::abs(a.x() - b.x()) < 1e-6 && std::abs(a.y() - b.y()) < 1e-6; };
    for (SvgDocument::SubPath &sub : result) {
        if (sub.segments.isEmpty() || !same(sub.segments.last().end, sub.start)) continue;
        sub.closed = true;
        if (!sub.segments.last().curve && sub.segments.size() > 1) sub.segments.removeLast();
    }
    result.removeIf([](const SvgDocument::SubPath &sub) { return sub.segments.isEmpty(); });
    return result;
}

QString pathData(const QList<SvgDocument::SubPath> &path) {
    QStringList parts;
    auto point = [](const QPointF &p) { return number(p.x()) + QLatin1Char(' ') + number(p.y()); };
    for (const SvgDocument::SubPath &sub : path) {
        parts.append(QStringLiteral("M") + point(sub.start));
        for (const SvgDocument::Segment &segment : sub.segments)
            parts.append(segment.curve ? QStringLiteral("C") + point(segment.c1) + QLatin1Char(' ') + point(segment.c2) + QLatin1Char(' ') + point(segment.end)
                                       : QStringLiteral("L") + point(segment.end));
        if (sub.closed) parts.append(QStringLiteral("Z"));
    }
    return parts.join(QLatin1Char(' '));
}

QString matrixText(const QTransform &m) {
    return QStringLiteral("matrix(%1 %2 %3 %4 %5 %6)").arg(number(m.m11()), number(m.m12()), number(m.m21()), number(m.m22()), number(m.dx()), number(m.dy()));
}
}  // namespace

void SvgDocument::ensureShapeIds() {
    Tree tree;
    if (!parse(m_data, tree)) return;
    QSet<QString> used;
    std::function<void(Node *)> gather = [&](Node *node) {
        for (const auto &child : node->children)
            if (child->kind == Node::Element) {
                const QString id = child->attribute(QStringLiteral("id"));
                if (!id.isEmpty()) used.insert(id);
                gather(child.get());
            }
    };
    gather(tree.root());
    QList<Node *> shapes;
    collectShapes(tree.root(), shapes);
    QSet<QString> seen;
    int next = 1;
    bool changed = false;
    for (Node *shape : std::as_const(shapes)) {
        const QString id = shape->attribute(QStringLiteral("id"));
        if (!id.isEmpty() && !seen.contains(id)) {
            seen.insert(id);
            continue;
        }
        QString fresh;
        do fresh = QStringLiteral("mentis-shape-%1").arg(next++);
        while (used.contains(fresh));
        used.insert(fresh);
        seen.insert(fresh);
        shape->setAttribute(QStringLiteral("id"), fresh);
        changed = true;
    }
    if (changed) replaceData(write(tree));
}

QList<SvgDocument::Shape> SvgDocument::drawnShapes() const {
    Tree tree;
    QList<Shape> result;
    if (!parse(m_data, tree)) return result;
    QList<Node *> shapes;
    collectShapes(tree.root(), shapes);
    QHash<const Node *, QTransform> groups;
    for (const Node *node : std::as_const(shapes)) {
        const QString id = node->attribute(QStringLiteral("id"));
        if (id.isEmpty()) continue;
        const QString own = node->attribute(QStringLiteral("transform"));
        const QTransform matrix = (own.isEmpty() ? QTransform() : parseTransform(own)) * ctm(node, false, &groups);
        QPainterPath outline = matrix.map(elementPath(*node));
        result.append({id, outline, isFilled(*node)});
    }
    return result;
}

QString SvgDocument::shapeAt(const QList<Shape> &shapes, const QPointF &point, qreal tolerance) {
    QPainterPathStroker stroker;
    stroker.setWidth(2 * tolerance);
    for (auto i = shapes.size() - 1; i >= 0; --i) {
        const Shape &shape = shapes[i];
        if (!shape.outline.boundingRect().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(point)) continue;
        if ((shape.filled && shape.outline.contains(point)) || stroker.createStroke(shape.outline).contains(point)) return shape.id;
    }
    return {};
}

QRectF SvgDocument::bounds(const QList<Shape> &shapes, const QStringList &ids) {
    QRectF result;
    for (const Shape &shape : shapes)
        if (ids.contains(shape.id)) result = result.isNull() ? shape.outline.boundingRect() : result.united(shape.outline.boundingRect());
    return result;
}

void SvgDocument::transformShapes(const QStringList &ids, const QTransform &matrix) {
    Tree tree;
    if (ids.isEmpty() || matrix.isIdentity() || !parse(m_data, tree)) return;
    for (const QString &id : ids) {
        Node *node = findById(tree.root(), id);
        if (!node) continue;
        // own' * parents = own * parents * matrix  =>  own' = own * parents * matrix * parents^-1
        const QTransform parents = ctm(node, false);
        bool invertible = false;
        const QTransform inverse = parents.inverted(&invertible);
        if (!invertible) continue;
        const QTransform own = parseTransform(node->attribute(QStringLiteral("transform")));
        node->setAttribute(QStringLiteral("transform"), matrixText(own * parents * matrix * inverse));
    }
    replaceData(write(tree));
}

void SvgDocument::removeShapes(const QStringList &ids) {
    Tree tree;
    if (ids.isEmpty() || !parse(m_data, tree)) return;
    for (const QString &id : ids)
        if (Node *node = findById(tree.root(), id)) {
            auto &siblings = node->parent->children;
            siblings.erase(std::remove_if(siblings.begin(), siblings.end(), [&](const auto &child) { return child.get() == node; }), siblings.end());
        }
    replaceData(write(tree));
}

namespace {
// Inherited presentation properties (SVG 1.1 property index, "Inherited: yes"): a shape taken out of
// its group keeps the values it had from it.
const QStringList kInherited{QStringLiteral("fill"), QStringLiteral("fill-opacity"), QStringLiteral("fill-rule"), QStringLiteral("stroke"),
                             QStringLiteral("stroke-width"), QStringLiteral("stroke-opacity"), QStringLiteral("stroke-linecap"),
                             QStringLiteral("stroke-linejoin"), QStringLiteral("stroke-dasharray"), QStringLiteral("stroke-miterlimit"),
                             QStringLiteral("color"), QStringLiteral("visibility")};

// The value a node sets for a property: its style attribute first, then its attribute.
QString ownProperty(const Node *node, const QString &name) {
    const QString style = node->attribute(QStringLiteral("style"));
    for (const QString &part : style.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const qsizetype colon = part.indexOf(QLatin1Char(':'));
        if (colon > 0 && part.left(colon).trimmed() == name) return part.mid(colon + 1).trimmed();
    }
    return node->attribute(name);
}

// Moves node into parent before the child before (nullptr: the end) and keeps its look: its
// transform and the properties it inherited from the groups it leaves.
void moveNode(Node *node, Node *parent, const Node *before) {
    if (node == before) return;
    Node *oldParent = node->parent;
    if (oldParent != parent) {
        for (const QString &name : kInherited) {
            if (!ownProperty(node, name).isEmpty()) continue;
            for (const Node *n = oldParent; n && n->parent && n->parent->parent; n = n->parent)  // up to below <svg>
                if (const QString value = ownProperty(n, name); !value.isEmpty()) {
                    node->setAttribute(name, value);
                    break;
                }
        }
        // own' * newParents = own * oldParents
        const QTransform oldParents = ctm(node, false);
        const QTransform own = parseTransform(node->attribute(QStringLiteral("transform")));
        Node probe;  // a stand-in child of the new parent, for its groups' matrix
        probe.parent = parent;
        bool invertible = false;
        const QTransform inverse = ctm(&probe, false).inverted(&invertible);
        if (invertible) {
            const QTransform moved = own * oldParents * inverse;
            if (moved.isIdentity()) node->removeAttribute(QStringLiteral("transform"));
            else node->setAttribute(QStringLiteral("transform"), matrixText(moved));
        }
    }
    auto &from = oldParent->children;
    auto it = std::find_if(from.begin(), from.end(), [&](const auto &child) { return child.get() == node; });
    std::unique_ptr<Node> owned = std::move(*it);
    from.erase(it);
    owned->parent = parent;
    auto &to = parent->children;
    auto at = std::find_if(to.begin(), to.end(), [&](const auto &child) { return child.get() == before; });
    to.insert(at, std::move(owned));
}

// The sibling after node (nullptr at the end).
const Node *nextSibling(const Node *node) {
    const auto &siblings = node->parent->children;
    for (size_t i = 0; i + 1 < siblings.size(); ++i)
        if (siblings[i].get() == node) return siblings[i + 1].get();
    return nullptr;
}
}  // namespace

void SvgDocument::reorderShapes(const QStringList &ids, Order order) {
    Tree tree;
    if (ids.isEmpty() || !parse(m_data, tree)) return;
    Node *root = tree.root();
    QList<Node *> shapes;
    collectShapes(root, shapes);
    QList<Node *> moving;
    for (Node *shape : std::as_const(shapes))
        if (ids.contains(shape->attribute(QStringLiteral("id")))) moving.append(shape);
    if (moving.isEmpty()) return;
    // The paint order of the whole drawing, across groups: Front and Back go to the root's ends
    // (Front below the label), Forward and Backward past the next shape above or below.
    const Node *label = nullptr;
    for (const auto &child : root->children)
        if (child->kind == Node::Element && child->attribute(QStringLiteral("id")) == QLatin1String("mentis-label")) label = child.get();
    if (order == Order::Front) {
        for (Node *node : std::as_const(moving)) moveNode(node, root, label);
    } else if (order == Order::Back) {
        for (auto i = moving.size() - 1; i >= 0; --i) moveNode(moving[i], root, root->children.front().get());
    } else if (order == Order::Forward) {
        for (auto i = moving.size() - 1; i >= 0; --i) {  // the topmost first, so they keep their order
            const auto at = shapes.indexOf(moving[i]);
            Node *next = nullptr;
            for (auto j = at + 1; j < shapes.size() && !next; ++j)
                if (!moving.contains(shapes[j])) next = shapes[j];
            if (!next) continue;
            moveNode(moving[i], next->parent, nextSibling(next));
            shapes.removeAt(at);
            shapes.insert(shapes.indexOf(next) + 1, moving[i]);
        }
    } else {
        for (Node *node : std::as_const(moving)) {
            const auto at = shapes.indexOf(node);
            Node *previous = nullptr;
            for (auto j = at - 1; j >= 0 && !previous; --j)
                if (!moving.contains(shapes[j])) previous = shapes[j];
            if (!previous) continue;
            moveNode(node, previous->parent, previous);
            shapes.removeAt(at);
            shapes.insert(shapes.indexOf(previous), node);
        }
    }
    replaceData(write(tree));
}

QList<SvgDocument::SubPath> SvgDocument::path(const QString &id) const {
    Tree tree;
    if (!parse(m_data, tree)) return {};
    const Node *node = findById(tree.root(), id);
    return node ? toSubPaths(elementPath(*node)) : QList<SubPath>();
}

QTransform SvgDocument::shapeMatrix(const QString &id) const {
    Tree tree;
    if (!parse(m_data, tree)) return {};
    const Node *node = findById(tree.root(), id);
    return node ? ctm(node, true) : QTransform();
}

void SvgDocument::split(const QStringList &ids, SvgDocument *rest, SvgDocument *only) const {
    Tree tree;
    if (!parse(m_data, tree)) return;
    const QSet<QString> wanted(ids.cbegin(), ids.cend());
    if (rest) {
        // Without the wanted elements.
        std::function<void(Node *)> drop = [&](Node *node) {
            auto &children = node->children;
            children.erase(std::remove_if(children.begin(), children.end(),
                                          [&](const auto &child) { return child->kind == Node::Element && wanted.contains(child->attribute(QStringLiteral("id"))); }),
                           children.end());
            for (const auto &child : children) drop(child.get());
        };
        drop(tree.root());
        rest->load(write(tree));
    }
    if (only) {
        Tree mine;
        if (!parse(m_data, mine)) return;
        // Keeps what is not drawn there (defs, styles...), the wanted elements and the groups holding them.
        std::function<bool(Node *)> keep = [&](Node *node) {
            bool any = false;
            auto &children = node->children;
            for (auto it = children.begin(); it != children.end();) {
                Node *child = it->get();
                bool stays = child->kind != Node::Element;
                if (child->kind == Node::Element) {
                    const QString tag = child->localName();
                    if (wanted.contains(child->attribute(QStringLiteral("id")))) stays = any = true;
                    else if (kNotRendered.contains(tag) || tag == QLatin1String("style")) stays = true;
                    else if (keep(child)) stays = any = true;
                }
                it = stays ? it + 1 : children.erase(it);
            }
            return any;
        };
        keep(mine.root());
        only->load(write(mine));
    }
}

void SvgDocument::setPath(const QString &id, const QList<SubPath> &path) {
    Tree tree;
    if (!parse(m_data, tree)) return;
    Node *node = findById(tree.root(), id);
    if (!node) return;
    if (node->localName() != QLatin1String("path")) {
        const QString prefix = node->name.contains(QLatin1Char(':')) ? node->name.section(QLatin1Char(':'), 0, 0) + QLatin1Char(':') : QString();
        node->name = prefix + QStringLiteral("path");
        for (const QString &key : kGeometry) node->removeAttribute(key);
    }
    node->setAttribute(QStringLiteral("d"), pathData(path));
    replaceData(write(tree));
}

QList<SvgDocument::SubPath> SvgDocument::subPaths(const QPainterPath &path) { return toSubPaths(path); }

namespace {
// The declarations of a style attribute, in order ("fill:#f00; stroke:none").
QList<std::pair<QString, QString>> declarations(const QString &style) {
    QList<std::pair<QString, QString>> result;
    for (const QString &part : style.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const qsizetype colon = part.indexOf(QLatin1Char(':'));
        if (colon > 0) result.append({part.left(colon).trimmed(), part.mid(colon + 1).trimmed()});
    }
    return result;
}

QString paint(const QColor &color) { return color.isValid() ? SvgDocument::colorText(color) : QStringLiteral("none"); }

void writeStyle(Node *node, const SvgDocument::ShapeStyle &style, int parts = SvgDocument::AllStyle) {
    QList<std::pair<QString, QString>> list = declarations(node->attribute(QStringLiteral("style")));
    auto set = [&](const QString &key, const QString &value) {
        for (auto &[name, current] : list)
            if (name == key) {
                current = value;
                return;
            }
        list.append({key, value});
    };
    if (parts & SvgDocument::Fill) set(QStringLiteral("fill"), paint(style.fill));
    if (parts & SvgDocument::Stroke) set(QStringLiteral("stroke"), paint(style.stroke));
    if (parts & SvgDocument::StrokeWidth) set(QStringLiteral("stroke-width"), number(style.strokeWidth));
    QStringList written;
    for (const auto &[name, value] : std::as_const(list)) written.append(name + QLatin1Char(':') + value);
    node->setAttribute(QStringLiteral("style"), written.join(QLatin1Char(';')));
}

QString uniqueShapeId(Node *root) {
    QSet<QString> used;
    std::function<void(Node *)> gather = [&](Node *node) {
        for (const auto &child : node->children)
            if (child->kind == Node::Element) {
                used.insert(child->attribute(QStringLiteral("id")));
                gather(child.get());
            }
    };
    gather(root);
    for (int n = 1;; ++n)
        if (const QString id = QStringLiteral("mentis-shape-%1").arg(n); !used.contains(id)) return id;
}
}  // namespace

QString SvgDocument::addPath(const QList<SubPath> &path, const ShapeStyle &style) {
    Tree tree;
    if (path.isEmpty() || !parse(m_data, tree)) return {};
    Node *root = tree.root();
    const QString id = uniqueShapeId(root);
    auto node = std::make_unique<Node>();
    node->name = root->name.contains(QLatin1Char(':')) ? root->name.section(QLatin1Char(':'), 0, 0) + QStringLiteral(":path") : QStringLiteral("path");
    node->parent = root;
    node->setAttribute(QStringLiteral("id"), id);
    node->setAttribute(QStringLiteral("d"), pathData(path));
    writeStyle(node.get(), style);
    // On top of the drawing, below the label (which stays readable over everything).
    auto &children = root->children;
    auto at = std::find_if(children.begin(), children.end(),
                           [](const auto &child) { return child->kind == Node::Element && child->attribute(QStringLiteral("id")) == QLatin1String("mentis-label"); });
    children.insert(at, std::move(node));
    replaceData(write(tree));
    return id;
}

void SvgDocument::setShapeStyle(const QStringList &ids, const ShapeStyle &style, int parts) {
    Tree tree;
    if (ids.isEmpty() || parts == 0 || !parse(m_data, tree)) return;
    for (const QString &id : ids)
        if (Node *node = findById(tree.root(), id)) writeStyle(node, style, parts);
    replaceData(write(tree));
}

SvgDocument::ShapeStyle SvgDocument::shapeStyle(const QString &id) const {
    Tree tree;
    ShapeStyle style;
    if (!parse(m_data, tree)) return style;
    const Node *node = findById(tree.root(), id);
    if (!node) return style;
    QString fill = node->attribute(QStringLiteral("fill")), stroke = node->attribute(QStringLiteral("stroke"));
    QString width = node->attribute(QStringLiteral("stroke-width"));
    for (const auto &[name, value] : declarations(node->attribute(QStringLiteral("style")))) {
        if (name == QLatin1String("fill")) fill = value;
        else if (name == QLatin1String("stroke")) stroke = value;
        else if (name == QLatin1String("stroke-width")) width = value;
    }
    style.fill = fill.isEmpty() ? QColor(Qt::black) : parseColor(fill);  // SVG's initial fill is black
    style.stroke = parseColor(stroke);
    style.strokeWidth = width.isEmpty() ? (style.stroke.isValid() ? 1 : 0) : length(width);
    return style;
}
