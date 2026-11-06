#include "svgdocument.h"

#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QSvgRenderer>
#include <QTransform>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <cmath>
#include <optional>

namespace {
// Properties that hold a colour (SVG 1.1 presentation attributes, https://www.w3.org/TR/SVG11/propidx.html).
const QStringList kColorProperties{QStringLiteral("fill"), QStringLiteral("stroke"), QStringLiteral("stop-color"),
                                   QStringLiteral("flood-color"), QStringLiteral("lighting-color"), QStringLiteral("color")};
// Shapes painted with the default black fill when nothing sets one (SVG 1.1, "fill" initial value).
const QStringList kFilledShapes{QStringLiteral("path"), QStringLiteral("rect"), QStringLiteral("circle"), QStringLiteral("ellipse"),
                                QStringLiteral("polygon"), QStringLiteral("polyline"), QStringLiteral("text")};
// Content that is not drawn where it is written.
const QStringList kNotRendered{QStringLiteral("defs"), QStringLiteral("clipPath"), QStringLiteral("mask"), QStringLiteral("symbol"),
                               QStringLiteral("pattern"), QStringLiteral("marker"), QStringLiteral("linearGradient"),
                               QStringLiteral("radialGradient"), QStringLiteral("filter")};
const QString kGroupId = QStringLiteral("mentis-content");
const QString kLabelId = QStringLiteral("mentis-label");

int channel(const QString &value, bool percent) {
    const double number = value.toDouble();
    return qBound(0, int(std::lround(percent ? number * 2.55 : number)), 255);
}

// A colour value as SVG writes it; invalid for none, currentColor, url(#...) and the like.
QColor parseColor(const QString &text) {
    const QString value = text.trimmed().toLower();
    static const QRegularExpression rgb(QStringLiteral(
        R"(^rgba?\(\s*([\d.]+)(%?)\s*[,\s]\s*([\d.]+)(%?)\s*[,\s]\s*([\d.]+)(%?)\s*(?:[,/]\s*([\d.]+)(%?)\s*)?\)$)"));
    if (value.startsWith(QLatin1String("rgb"))) {
        const QRegularExpressionMatch m = rgb.match(value);
        if (!m.hasMatch()) return {};
        QColor color(channel(m.captured(1), !m.captured(2).isEmpty()), channel(m.captured(3), !m.captured(4).isEmpty()),
                     channel(m.captured(5), !m.captured(6).isEmpty()));
        if (m.hasCaptured(7)) {
            const double alpha = m.captured(7).toDouble();
            color.setAlphaF(qBound(0.0, m.captured(8).isEmpty() ? alpha : alpha / 100, 1.0));
        }
        return color;
    }
    if (value.startsWith(QLatin1Char('#'))) {
        if (value.size() == 4 || value.size() == 7) return QColor::fromString(value);
        if (value.size() == 9) {  // #rrggbbaa (CSS Color 4); Qt reads #aarrggbb
            QColor color = QColor::fromString(value.left(7));
            bool ok = false;
            const int alpha = value.mid(7).toInt(&ok, 16);
            if (color.isValid() && ok) color.setAlpha(alpha);
            return ok ? color : QColor();
        }
        return {};
    }
    if (value == QLatin1String("none") || value == QLatin1String("transparent") || value == QLatin1String("currentcolor")) return {};
    return QColor::isValidColorName(value) ? QColor::fromString(value) : QColor();
}

// The replacement keeps the alpha of the colour it replaces.
QString writeColor(const QColor &to, const QColor &from) {
    QColor color = to;
    color.setAlpha(from.alpha());
    if (color.alpha() == 255) return color.name();
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alphaF(), 0, 'g', 3);
}

// "prop: value" declarations of a style attribute or a <style> sheet.
const QRegularExpression &declarationPattern() {
    static const QRegularExpression pattern(
        QStringLiteral(R"((?<![\w-])(fill|stroke|stop-color|flood-color|lighting-color|color)(\s*:\s*)([^;}]+))"));
    return pattern;
}

QString number(qreal value) { return QString::number(value, 'g', 6); }

QList<qreal> numbers(const QString &text) {
    static const QRegularExpression separator(QStringLiteral("[\\s,]+"));
    QList<qreal> result;
    for (const QString &part : text.trimmed().split(separator, Qt::SkipEmptyParts)) {
        bool ok = false;
        const qreal value = part.toDouble(&ok);
        if (!ok) return {};
        result.append(value);
    }
    return result;
}

// "256px", "10.5": the number; 0 when not a plain length (percentages, em...).
qreal length(const QString &text) {
    QString value = text.trimmed();
    if (value.endsWith(QLatin1String("px"))) value.chop(2);
    bool ok = false;
    const qreal result = value.toDouble(&ok);
    return ok ? result : 0;
}

// Flips, then the rotation, around the pivot (SVG applies the rightmost transform first, as
// QTransform's member functions do).
QTransform matrix(const SvgDocument::Transform &t, const QPointF &pivot) {
    QTransform m;
    m.translate(pivot.x(), pivot.y());
    m.rotate(t.rotation);
    m.scale(t.flipH ? -1 : 1, t.flipV ? -1 : 1);
    m.translate(-pivot.x(), -pivot.y());
    return m;
}

QString transformText(const SvgDocument::Transform &t, const QPointF &pivot) {
    return QStringLiteral("translate(%1 %2) rotate(%3) scale(%4 %5) translate(%6 %7)")
        .arg(number(pivot.x()), number(pivot.y()), number(t.rotation), t.flipH ? QStringLiteral("-1") : QStringLiteral("1"),
             t.flipV ? QStringLiteral("-1") : QStringLiteral("1"), number(-pivot.x()), number(-pivot.y()));
}

QString flipText(const SvgDocument::Transform &t) {
    return QString(t.flipH ? QStringLiteral("h") : QString()) + (t.flipV ? QStringLiteral("v") : QString());
}

QFont labelFont(const SvgDocument::Label &label) {
    QFont font(label.family);
    font.setBold(label.bold);
    font.setPixelSize(100);  // measured at 100 px and scaled: sizes in viewBox units can be tiny
    return font;
}
}  // namespace

QColor SvgDocument::parseColor(const QString &text) { return ::parseColor(text); }

QString SvgDocument::colorText(const QColor &color) { return writeColor(color, color); }

struct SvgDocument::Edit {
    const QColor *from = nullptr;
    const QColor *to = nullptr;
    std::optional<QRectF> viewBox;
    std::optional<Transform> transform;
    QPointF pivot;
    std::optional<Label> label;
};

bool SvgDocument::load(const QByteArray &data) {
    m_data.clear();
    if (!scan(data, nullptr, nullptr)) {
        m_colors.clear();
        return false;
    }
    m_data = data;  // kept byte for byte until the first change
    return true;
}

void SvgDocument::replaceColor(const QColor &from, const QColor &to) {
    if (!from.isValid() || !to.isValid() || from.rgba() == to.rgba()) return;
    Edit edit;
    edit.from = &from;
    edit.to = &to;
    rewrite(edit);
}

void SvgDocument::setViewBox(const QRectF &box) {
    if (!box.isValid() || box == m_viewBox) return;
    Edit edit;
    edit.viewBox = box;
    rewrite(edit);
}

void SvgDocument::setTransform(const Transform &transform) {
    if (transform == m_transform) return;
    const QPointF pivot = m_hasGroup ? m_pivot : m_viewBox.center();
    const QTransform move = matrix(m_transform, pivot).inverted() * matrix(transform, pivot);
    Edit edit;
    edit.transform = transform;
    edit.pivot = pivot;
    edit.viewBox = move.mapRect(m_viewBox);
    if (!m_label.text.isEmpty()) {
        Label label = m_label;
        label.position = move.map(label.position);
        edit.label = label;
    }
    rewrite(edit);
}

void SvgDocument::setLabel(const Label &label) {
    if (label == m_label) return;
    Edit edit;
    edit.label = label;
    rewrite(edit);
}

void SvgDocument::rewrite(const Edit &edit) {
    if (m_data.isEmpty()) return;
    QByteArray out;
    if (!scan(m_data, &edit, &out)) return;
    m_data = out;
    scan(m_data, nullptr, nullptr);
}

void SvgDocument::replaceData(const QByteArray &data) {
    if (data.isEmpty() || data == m_data) return;
    QByteArray previous = m_data;
    m_data = data;
    if (!scan(m_data, nullptr, nullptr)) {  // never keep data that does not read back
        m_data = previous;
        scan(m_data, nullptr, nullptr);
    }
}

QRectF SvgDocument::drawingBounds() const {
    QSvgRenderer renderer(m_data);
    if (!renderer.isValid() || m_viewBox.isEmpty()) return {};
    const QRectF area = m_viewBox.adjusted(-m_viewBox.width(), -m_viewBox.height(), m_viewBox.width(), m_viewBox.height());
    renderer.setViewBox(area);
    renderer.setAspectRatioMode(Qt::IgnoreAspectRatio);
    const QSize size = area.size().scaled(QSizeF(768, 768), Qt::KeepAspectRatio).toSize().expandedTo(QSize(1, 1));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        renderer.render(&painter, QRectF(QPointF(0, 0), size));
    }
    int left = size.width(), top = size.height(), right = -1, bottom = -1;
    for (int y = 0; y < size.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < size.width(); ++x)
            if (qAlpha(line[x]) > 0) {
                left = qMin(left, x);
                right = qMax(right, x);
                top = qMin(top, y);
                bottom = qMax(bottom, y);
            }
    }
    if (right < 0) return {};
    const qreal sx = area.width() / size.width(), sy = area.height() / size.height();
    return QRectF(area.x() + left * sx, area.y() + top * sy, (right - left + 1) * sx, (bottom - top + 1) * sy);
}

QRectF SvgDocument::labelBounds() const {
    if (m_label.text.isEmpty() || m_label.size <= 0) return {};
    const QFontMetricsF metrics(labelFont(m_label));
    const qreal scale = m_label.size / 100;
    const qreal width = metrics.horizontalAdvance(m_label.text) * scale;
    return QRectF(m_label.position.x() - width / 2, m_label.position.y() - metrics.ascent() * scale, width,
                  (metrics.ascent() + metrics.descent()) * scale);
}

bool SvgDocument::scan(const QByteArray &data, const Edit *edit, QByteArray *out) {
    const bool writing = edit && out;
    const bool replacing = writing && edit->from && edit->to;
    const QColor *from = replacing ? edit->from : nullptr;
    const QColor *to = replacing ? edit->to : nullptr;
    // Read into locals; only a reading pass stores them (a writing pass describes its input).
    QList<QColor> colors;
    QRectF viewBox(0, 0, 100, 100);  // QSvgRenderer's size without width, height and viewBox
    Transform transform;
    QPointF pivot;
    bool hasGroup = false;
    Label label;

    QSet<QRgb> seen;
    auto note = [&](const QColor &color) {
        if (color.isValid() && !seen.contains(color.rgba())) {
            seen.insert(color.rgba());
            colors.append(color);
        }
    };
    // A colour value: noted, and rewritten when it is the one replaced.
    auto value = [&](const QString &text) -> QString {
        const QColor color = parseColor(text);
        note(color);
        if (replacing && color.isValid() && color.rgba() == from->rgba()) return writeColor(*to, color);
        return text;
    };
    auto declarations = [&](const QString &text, bool *setsFill) -> QString {
        QString result;
        qsizetype last = 0;
        QRegularExpressionMatchIterator it = declarationPattern().globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            if (setsFill && m.captured(1) == QLatin1String("fill")) *setsFill = true;
            QString raw = m.captured(3);
            const qsizetype important = raw.indexOf(QLatin1Char('!'));  // "red !important"
            const QString tail = important >= 0 ? raw.mid(important) : QString();
            if (important >= 0) raw.truncate(important);
            const QString core = raw.trimmed();
            const QString spaceBefore = raw.left(raw.indexOf(core));
            const QString spaceAfter = raw.mid(raw.indexOf(core) + core.size());
            result += text.mid(last, m.capturedStart(3) - last) + spaceBefore + value(core) + spaceAfter + tail;
            last = m.capturedEnd(3);
        }
        return result + text.mid(last);
    };

    // The default black fill is told apart only without style sheets (a class may set the fill).
    const bool hasStyleSheet = data.contains("<style");
    QXmlStreamReader reader(data);
    QByteArray unused;
    QXmlStreamWriter writer(writing ? out : &unused);
    auto writeLabel = [&](const Label &l) {
        if (l.text.isEmpty()) return;
        writer.writeStartElement(QStringLiteral("text"));
        writer.writeAttribute(QStringLiteral("id"), kLabelId);
        writer.writeAttribute(QStringLiteral("x"), number(l.position.x()));
        writer.writeAttribute(QStringLiteral("y"), number(l.position.y()));
        writer.writeAttribute(QStringLiteral("font-family"), l.family);
        writer.writeAttribute(QStringLiteral("font-size"), number(l.size));
        if (l.bold) writer.writeAttribute(QStringLiteral("font-weight"), QStringLiteral("bold"));
        writer.writeAttribute(QStringLiteral("fill"), writeColor(l.color, l.color));
        writer.writeAttribute(QStringLiteral("text-anchor"), QStringLiteral("middle"));
        writer.writeCharacters(l.text);
        writer.writeEndElement();
    };
    QList<bool> fillInherited;  // per open element: some ancestor or the element itself sets a fill
    QList<bool> hidden;         // per open element: inside defs, clipPath, mask...
    QList<QString> names;
    bool root = true;
    bool openedGroup = false;  // this writing pass wrapped the drawing in a new group
    bool inLabel = false;
    int skipDepth = 0;         // inside the old label, replaced by edit->label
    while (!reader.atEnd()) {
        reader.readNext();
        if (skipDepth > 0) {
            if (reader.isStartElement()) ++skipDepth;
            else if (reader.isEndElement()) --skipDepth;
            continue;
        }
        if (reader.isStartElement()) {
            const QString name = reader.name().toString();
            if (root && name != QLatin1String("svg")) return false;
            const bool isRoot = root;
            root = false;
            const QXmlStreamAttributes source = reader.attributes();
            const QString id = source.value(QLatin1String("id")).toString();
            if (names.size() == 1 && name == QLatin1String("text") && id == kLabelId) {
                label = Label();
                label.position = QPointF(source.value(QLatin1String("x")).toDouble(), source.value(QLatin1String("y")).toDouble());
                label.family = source.value(QLatin1String("font-family")).toString();
                label.size = length(source.value(QLatin1String("font-size")).toString());
                label.bold = source.value(QLatin1String("font-weight")) == QLatin1String("bold");
                label.color = parseColor(source.value(QLatin1String("fill")).toString());
                if (!label.color.isValid()) label.color = Qt::black;
                if (writing && edit->label) {
                    skipDepth = 1;
                    continue;
                }
                inLabel = true;
            }
            if (name == QLatin1String("g") && id == kGroupId) {
                hasGroup = true;
                transform.rotation = source.value(QLatin1String("data-mentis-rotate")).toDouble();
                const QString flip = source.value(QLatin1String("data-mentis-flip")).toString();
                transform.flipH = flip.contains(QLatin1Char('h'));
                transform.flipV = flip.contains(QLatin1Char('v'));
                const QList<qreal> p = numbers(source.value(QLatin1String("data-mentis-pivot")).toString());
                if (p.size() == 2) pivot = QPointF(p[0], p[1]);
            }
            if (isRoot) {
                const QList<qreal> box = numbers(source.value(QLatin1String("viewBox")).toString());
                const qreal w = length(source.value(QLatin1String("width")).toString());
                const qreal h = length(source.value(QLatin1String("height")).toString());
                if (box.size() == 4 && box[2] > 0 && box[3] > 0) viewBox = QRectF(box[0], box[1], box[2], box[3]);
                else if (w > 0 && h > 0) viewBox = QRectF(0, 0, w, h);
            }
            bool setsFill = false;
            QList<std::pair<QString, QString>> attributes;
            for (const QXmlStreamAttribute &attribute : source) {
                const QString attributeName = attribute.qualifiedName().toString();
                QString text = attribute.value().toString();
                if (attribute.namespaceUri().isEmpty() && kColorProperties.contains(attributeName)) {
                    if (attributeName == QLatin1String("fill")) setsFill = true;
                    text = value(text);
                } else if (attributeName == QLatin1String("style")) {
                    text = declarations(text, &setsFill);
                }
                attributes.append({attributeName, text});
            }
            const bool inherited = setsFill || (!fillInherited.isEmpty() && fillInherited.last());
            const bool isHidden = kNotRendered.contains(name) || (!hidden.isEmpty() && hidden.last());
            const bool defaultBlack = !hasStyleSheet && !isHidden && !inherited && kFilledShapes.contains(name) &&
                                      !source.hasAttribute(QLatin1String("class"));
            if (defaultBlack) note(Qt::black);
            if (writing) {
                auto set = [&](const QString &key, const QString &text) {
                    for (auto &[attributeName, current] : attributes)
                        if (attributeName == key) {
                            current = text;
                            return;
                        }
                    attributes.append({key, text});
                };
                // The default black is replaced by a fill on the root, inherited by every shape.
                if (replacing && isRoot && !setsFill && from->rgba() == QColor(Qt::black).rgba() && !hasStyleSheet)
                    attributes.append({QStringLiteral("fill"), writeColor(*to, Qt::black)});
                if (isRoot && edit->viewBox) {
                    const QRectF &b = *edit->viewBox;
                    set(QStringLiteral("viewBox"), QStringLiteral("%1 %2 %3 %4").arg(number(b.x()), number(b.y()), number(b.width()), number(b.height())));
                    const qreal scale = kStickerPx / qMax(b.width(), b.height());
                    set(QStringLiteral("width"), number(b.width() * scale));
                    set(QStringLiteral("height"), number(b.height() * scale));
                }
                if (edit->transform && name == QLatin1String("g") && id == kGroupId) {
                    set(QStringLiteral("transform"), transformText(*edit->transform, edit->pivot));
                    set(QStringLiteral("data-mentis-rotate"), number(edit->transform->rotation));
                    set(QStringLiteral("data-mentis-flip"), flipText(*edit->transform));
                }
                writer.writeStartElement(reader.qualifiedName().toString());
                for (const QXmlStreamNamespaceDeclaration &ns : reader.namespaceDeclarations())
                    writer.writeAttribute(ns.prefix().isEmpty() ? QStringLiteral("xmlns") : QStringLiteral("xmlns:") + ns.prefix().toString(),
                                          ns.namespaceUri().toString());
                for (const auto &[attributeName, text] : std::as_const(attributes)) writer.writeAttribute(attributeName, text);
                if (isRoot && edit->transform && !m_hasGroup) {
                    writer.writeStartElement(QStringLiteral("g"));
                    writer.writeAttribute(QStringLiteral("id"), kGroupId);
                    writer.writeAttribute(QStringLiteral("transform"), transformText(*edit->transform, edit->pivot));
                    writer.writeAttribute(QStringLiteral("data-mentis-rotate"), number(edit->transform->rotation));
                    writer.writeAttribute(QStringLiteral("data-mentis-flip"), flipText(*edit->transform));
                    writer.writeAttribute(QStringLiteral("data-mentis-pivot"), number(edit->pivot.x()) + QLatin1Char(' ') + number(edit->pivot.y()));
                    openedGroup = true;
                }
            }
            fillInherited.append(inherited);
            hidden.append(isHidden);
            names.append(name);
        } else if (reader.isEndElement()) {
            if (writing) {
                if (names.size() == 1) {  // the root: the new group closes, the label comes last
                    if (openedGroup) writer.writeEndElement();
                    if (edit->label) writeLabel(*edit->label);
                }
                writer.writeEndElement();
            }
            if (inLabel && names.size() == 2) {
                inLabel = false;
                note(label.color);
            }
            fillInherited.removeLast();
            hidden.removeLast();
            names.removeLast();
        } else if (reader.isCharacters() && !names.isEmpty() && names.last() == QLatin1String("style")) {
            const QString text = declarations(reader.text().toString(), nullptr);
            if (writing) {
                if (reader.isCDATA()) writer.writeCDATA(text);
                else writer.writeCharacters(text);
            }
        } else {
            if (inLabel && reader.isCharacters()) label.text += reader.text().toString();
            if (writing) writer.writeCurrentToken(reader);
        }
    }
    if (reader.hasError() || root) return false;
    if (!writing) {
        label.text = label.text.trimmed();
        m_colors = colors;
        m_viewBox = viewBox;
        m_transform = transform;
        m_pivot = hasGroup ? pivot : viewBox.center();
        m_hasGroup = hasGroup;
        m_label = label.text.isEmpty() ? Label() : label;
    }
    return true;
}
