#include "officeexport.h"

#include "formulatext.h"
#include "mindmapmodel.h"
#include "formularendering.h"
#include "nodegeometry.h"
#include "numbering.h"
#include "ooxml.h"
#include "stickers.h"
#include "zipwriter.h"

#include <QCoreApplication>
#include <QFile>
#include <QImageReader>
#include <QSaveFile>
#include <QSet>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace OfficeExport {

using namespace Ooxml;

namespace {
const QString kW = QStringLiteral("http://schemas.openxmlformats.org/wordprocessingml/2006/main");
const QString kWp = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing");
const QString kA = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main");
const QString kPic = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/picture");
const QString kS = QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main");
const QString kWordType = QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.");
const QString kSheetType = QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.");

// Page A4 (11906 x 16838 twips) with 2 cm (1134 twips) margins, as in the spec. A map pixel is 1/96
// inch: 9525 EMU (914400 EMU per inch, ECMA-376 Part 1, 20.1.2.1); the text is 170 mm wide.
constexpr int kPageWidthTwips = 11906;
constexpr int kPageHeightTwips = 16838;
constexpr int kMarginTwips = 1134;
constexpr qint64 kEmuPerPixel = 9525;
constexpr qint64 kTextWidthEmu = 170 * 36000;
constexpr int kHeadingLevels = 9;  // Word's Heading 1-9
constexpr int kPictureScale = 2;   // pixels per map pixel of a picture drawn by Mentis
constexpr qsizetype kMaxCellText = 32767;  // Excel's cell limit
constexpr int kMaxSheetName = 31;          // Excel's worksheet name limit

QString tr(const char *text) { return QCoreApplication::translate("OfficeExport", text); }

QString taskBox(const MindNode *node) {
    switch (node->task) {
    case MindNode::Task::Open: return QStringLiteral("☐ ");
    case MindNode::Task::Done: return QStringLiteral("☑ ");
    case MindNode::Task::None: break;
    }
    return {};
}

QString shownTitle(const MindNode *node) { return taskBox(node) + Numbering::withNumber(node->number, node->title); }

void forEachTopic(const MindNode *node, int depth, const std::function<void(const MindNode *, int)> &visit) {
    visit(node, depth);
    for (const auto &child : node->children) forEachTopic(child.get(), depth + 1, visit);
}

// --- Word ---

struct Picture {
    QByteArray bytes;
    QString extension;  // png or jpeg
    QSizeF size;        // map pixels
};

// The topic's picture as in the map: the image (JPEG and PNG as they are, if upright), the sticker
// or the formula box. Empty when the node has none or its file is missing.
Picture pictureOf(const MindNode *node, const ImageSource &imageSource, bool *missing) {
    *missing = false;
    if (node->imageSize.isEmpty()) return {};
    const int rotation = node->imageStyle.rotation;
    const QSizeF upright = rotation % 180 ? node->imageSize.transposed() : node->imageSize;
    QImage image;
    if (!node->formula.isEmpty()) {
        image = FormulaRendering::boxImage(node->formula, NodeGeometry::titleFont(node), node->formulaBackground,
                                      FormulaText::textColorOn(node->formulaBackground, node->text), upright, kPictureScale);
    } else if (!node->imagePath.isEmpty() || !node->imageAssetId.isEmpty()) {
        QString path = imageSource ? imageSource(node) : QString();
        if (path.isEmpty() || !QFile::exists(path)) path = node->imagePath;
        QImageReader reader(path);
        reader.setAutoTransform(true);
        if (!reader.canRead()) {
            *missing = true;
            return {};
        }
        const QByteArray format = reader.format();
        const bool asItIs = (format == "png" || format == "jpeg") && rotation == 0 && !(node->imageSticker && node->imageStyle.outline)
                            && reader.transformation() == QImageIOHandler::TransformationNone;
        if (asItIs) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) return {file.readAll(), format == "png" ? QStringLiteral("png") : QStringLiteral("jpeg"), node->imageSize};
        }
        const QSize target = (upright * kPictureScale).toSize().expandedTo(QSize(1, 1));
        const QSize source = reader.size();
        if (source.isValid() && (source.width() > target.width() || source.height() > target.height())) reader.setScaledSize(target);
        image = reader.read();
        if (image.isNull()) {
            *missing = true;
            return {};
        }
        if (node->imageSticker && node->imageStyle.outline) image = Stickers::outlined(image);
    } else {
        return {};
    }
    if (image.isNull()) return {};
    if (rotation != 0) image = image.transformed(QTransform().rotate(rotation), Qt::SmoothTransformation);
    return {png(image), QStringLiteral("png"), node->imageSize};
}

class WordWriter {
public:
    WordWriter(const ImageSource &imageSource) : m_imageSource(imageSource) {}

    QByteArray document(const QList<Sheet> &sheets) {
        return part([&](QXmlStreamWriter &xml) {
            xml.writeNamespace(kW, QStringLiteral("w"));
            xml.writeNamespace(kRel, QStringLiteral("r"));
            xml.writeNamespace(kWp, QStringLiteral("wp"));
            xml.writeNamespace(kA, QStringLiteral("a"));
            xml.writeNamespace(kPic, QStringLiteral("pic"));
            xml.writeStartElement(kW, QStringLiteral("document"));
            xml.writeStartElement(kW, QStringLiteral("body"));
            bool first = true;
            for (const Sheet &sheet : sheets) {
                if (!sheet.model || !sheet.model->root()) continue;
                forEachTopic(sheet.model->root(), 0, [&](const MindNode *node, int depth) { topic(xml, node, depth, depth == 0 && !first); });
                first = false;
            }
            xml.writeStartElement(kW, QStringLiteral("sectPr"));
            element(xml, kW, QStringLiteral("pgSz"), {{w(QStringLiteral("w")), QString::number(kPageWidthTwips)}, {w(QStringLiteral("h")), QString::number(kPageHeightTwips)}});
            const QString margin = QString::number(kMarginTwips);
            element(xml, kW, QStringLiteral("pgMar"), {{w(QStringLiteral("top")), margin}, {w(QStringLiteral("right")), margin}, {w(QStringLiteral("bottom")), margin},
                                                       {w(QStringLiteral("left")), margin}, {w(QStringLiteral("header")), QStringLiteral("709")},
                                                       {w(QStringLiteral("footer")), QStringLiteral("709")}, {w(QStringLiteral("gutter")), QStringLiteral("0")}});
            xml.writeEndElement();
            xml.writeEndElement();
            xml.writeEndElement();
        });
    }

    // rId1 styles, rId2 numbering, then pictures and hyperlinks in the order met.
    QList<Ooxml::Relationship> relations() const {
        QList<Ooxml::Relationship> list{{QStringLiteral("styles"), QStringLiteral("styles.xml")}, {QStringLiteral("numbering"), QStringLiteral("numbering.xml")}};
        return list + m_relations;
    }
    const QList<std::pair<QString, QByteArray>> &media() const { return m_media; }
    const QStringList &skipped() const { return m_skipped; }

private:
    // Attributes of w: elements need the prefix (they are qualified, ECMA-376 Part 1, 17).
    static QString w(const QString &name) { return QStringLiteral("w:") + name; }

    QString addRelation(const QString &type, const QString &target, bool external = false) {
        m_relations.append({type, target, external});
        return QStringLiteral("rId%1").arg(m_relations.size() + 2);
    }

    static void style(QXmlStreamWriter &xml, const QString &id) { element(xml, kW, QStringLiteral("pStyle"), {{w(QStringLiteral("val")), id}}); }

    // Text with its line breaks as <w:br/>, in one run.
    static void run(QXmlStreamWriter &xml, const QString &text, const std::function<void()> &properties = {}) {
        xml.writeStartElement(kW, QStringLiteral("r"));
        if (properties) {
            xml.writeStartElement(kW, QStringLiteral("rPr"));
            properties();
            xml.writeEndElement();
        }
        const QStringList lines = xmlSafe(text).split(QLatin1Char('\n'));
        for (int i = 0; i < lines.size(); ++i) {
            if (i > 0) element(xml, kW, QStringLiteral("br"));
            xml.writeStartElement(kW, QStringLiteral("t"));
            xml.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
            xml.writeCharacters(lines[i]);
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }

    void topic(QXmlStreamWriter &xml, const MindNode *node, int depth, bool newPage) {
        xml.writeStartElement(kW, QStringLiteral("p"));
        xml.writeStartElement(kW, QStringLiteral("pPr"));
        if (depth == 0) {
            style(xml, QStringLiteral("Title"));
        } else if (depth <= kHeadingLevels) {
            style(xml, QStringLiteral("Heading%1").arg(depth));
        } else {
            style(xml, QStringLiteral("ListBullet"));
            xml.writeStartElement(kW, QStringLiteral("numPr"));
            element(xml, kW, QStringLiteral("ilvl"), {{w(QStringLiteral("val")), QString::number(std::min(depth - kHeadingLevels - 1, 8))}});
            element(xml, kW, QStringLiteral("numId"), {{w(QStringLiteral("val")), QStringLiteral("1")}});
            xml.writeEndElement();
        }
        if (newPage) element(xml, kW, QStringLiteral("pageBreakBefore"));
        xml.writeEndElement();
        const QString title = shownTitle(node);
        if (!node->webLink.isEmpty()) {
            xml.writeStartElement(kW, QStringLiteral("hyperlink"));
            xml.writeAttribute(kRel, QStringLiteral("id"), addRelation(QStringLiteral("hyperlink"), node->webLink, true));
            run(xml, title, [&] { element(xml, kW, QStringLiteral("rStyle"), {{w(QStringLiteral("val")), QStringLiteral("Hyperlink")}}); });
            xml.writeEndElement();
        } else {
            run(xml, title);
        }
        xml.writeEndElement();
        if (!node->labels.isEmpty()) {
            xml.writeStartElement(kW, QStringLiteral("p"));
            xml.writeStartElement(kW, QStringLiteral("pPr"));
            style(xml, QStringLiteral("Labels"));
            xml.writeEndElement();
            run(xml, node->labels.join(QStringLiteral(", ")));
            xml.writeEndElement();
        }
        for (const QString &line : node->notes.split(QLatin1Char('\n'))) {
            if (line.trimmed().isEmpty()) continue;
            xml.writeStartElement(kW, QStringLiteral("p"));
            run(xml, line);
            xml.writeEndElement();
        }
        bool missing = false;
        const Picture picture = pictureOf(node, m_imageSource, &missing);
        if (missing) m_skipped.append(node->title);
        if (!picture.bytes.isEmpty()) drawing(xml, picture);
    }

    void drawing(QXmlStreamWriter &xml, const Picture &picture) {
        const int number = int(m_media.size()) + 1;
        const QString name = QStringLiteral("image%1.%2").arg(number).arg(picture.extension);
        m_media.append({name, picture.bytes});
        const QString id = addRelation(QStringLiteral("image"), QStringLiteral("media/") + name);
        qint64 cx = std::llround(picture.size.width() * kEmuPerPixel);
        qint64 cy = std::llround(picture.size.height() * kEmuPerPixel);
        if (cx > kTextWidthEmu) {
            cy = std::max<qint64>(1, cy * kTextWidthEmu / cx);
            cx = kTextWidthEmu;
        }
        xml.writeStartElement(kW, QStringLiteral("p"));
        inlinePicture(xml, number, name, id, cx, cy);
        xml.writeEndElement();
    }

    ImageSource m_imageSource;
    QList<Ooxml::Relationship> m_relations;
    QList<std::pair<QString, QByteArray>> m_media;
    QStringList m_skipped;
};

}  // namespace

// Styles close to Word's built-in ones (proposal in the spec, no fetched source): Calibri, Normal
// 11 pt, Title 28 pt, Heading 1 16 pt, Heading 2 13 pt, Heading 3-9 12 pt; outline levels for the
// navigation pane.
QByteArray wordStyles() {
    return part([](QXmlStreamWriter &xml) {
        const auto val = [&](const QString &name, const QString &value) { element(xml, kW, name, {{QStringLiteral("w:val"), value}}); };
        xml.writeNamespace(kW, QStringLiteral("w"));
        xml.writeStartElement(kW, QStringLiteral("styles"));
        xml.writeStartElement(kW, QStringLiteral("docDefaults"));
        xml.writeStartElement(kW, QStringLiteral("rPrDefault"));
        xml.writeStartElement(kW, QStringLiteral("rPr"));
        element(xml, kW, QStringLiteral("rFonts"), {{QStringLiteral("w:ascii"), QStringLiteral("Calibri")}, {QStringLiteral("w:hAnsi"), QStringLiteral("Calibri")},
                                                    {QStringLiteral("w:eastAsia"), QStringLiteral("Calibri")}, {QStringLiteral("w:cs"), QStringLiteral("Calibri")}});
        val(QStringLiteral("sz"), QStringLiteral("22"));
        val(QStringLiteral("szCs"), QStringLiteral("22"));
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeStartElement(kW, QStringLiteral("pPrDefault"));
        xml.writeStartElement(kW, QStringLiteral("pPr"));
        element(xml, kW, QStringLiteral("spacing"), {{QStringLiteral("w:after"), QStringLiteral("120")}});
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeEndElement();
        struct Style {
            QString id, name;
            int halfPoints = 0;
            int outline = -1;
            bool bold = false;
        };
        QList<Style> styles{{QStringLiteral("Title"), QStringLiteral("Title"), 56, -1, false}};
        for (int level = 1; level <= kHeadingLevels; ++level)
            styles.append({QStringLiteral("Heading%1").arg(level), QStringLiteral("heading %1").arg(level),
                           level == 1 ? 32 : level == 2 ? 26 : 24, level - 1, true});
        xml.writeStartElement(kW, QStringLiteral("style"));
        xml.writeAttribute(QStringLiteral("w:type"), QStringLiteral("paragraph"));
        xml.writeAttribute(QStringLiteral("w:default"), QStringLiteral("1"));
        xml.writeAttribute(QStringLiteral("w:styleId"), QStringLiteral("Normal"));
        val(QStringLiteral("name"), QStringLiteral("Normal"));
        element(xml, kW, QStringLiteral("qFormat"));
        xml.writeEndElement();
        for (const Style &style : std::as_const(styles)) {
            xml.writeStartElement(kW, QStringLiteral("style"));
            xml.writeAttribute(QStringLiteral("w:type"), QStringLiteral("paragraph"));
            xml.writeAttribute(QStringLiteral("w:styleId"), style.id);
            val(QStringLiteral("name"), style.name);
            val(QStringLiteral("basedOn"), QStringLiteral("Normal"));
            val(QStringLiteral("next"), QStringLiteral("Normal"));
            element(xml, kW, QStringLiteral("qFormat"));
            xml.writeStartElement(kW, QStringLiteral("pPr"));
            element(xml, kW, QStringLiteral("keepNext"));
            element(xml, kW, QStringLiteral("spacing"), {{QStringLiteral("w:before"), QStringLiteral("240")}, {QStringLiteral("w:after"), QStringLiteral("80")}});
            if (style.outline >= 0) val(QStringLiteral("outlineLvl"), QString::number(style.outline));
            xml.writeEndElement();
            xml.writeStartElement(kW, QStringLiteral("rPr"));
            if (style.bold) element(xml, kW, QStringLiteral("b"));
            val(QStringLiteral("sz"), QString::number(style.halfPoints));
            val(QStringLiteral("szCs"), QString::number(style.halfPoints));
            xml.writeEndElement();
            xml.writeEndElement();
        }
        const auto simple = [&](const QString &type, const QString &id, const QString &name, const std::function<void()> &body) {
            xml.writeStartElement(kW, QStringLiteral("style"));
            xml.writeAttribute(QStringLiteral("w:type"), type);
            xml.writeAttribute(QStringLiteral("w:styleId"), id);
            val(QStringLiteral("name"), name);
            if (type == QLatin1String("paragraph")) val(QStringLiteral("basedOn"), QStringLiteral("Normal"));
            body();
            xml.writeEndElement();
        };
        simple(QStringLiteral("paragraph"), QStringLiteral("ListBullet"), QStringLiteral("List Bullet"), [&] {
            xml.writeStartElement(kW, QStringLiteral("pPr"));
            element(xml, kW, QStringLiteral("spacing"), {{QStringLiteral("w:after"), QStringLiteral("60")}});
            xml.writeEndElement();
        });
        simple(QStringLiteral("paragraph"), QStringLiteral("Labels"), QStringLiteral("Labels"), [&] {
            xml.writeStartElement(kW, QStringLiteral("rPr"));
            val(QStringLiteral("color"), QStringLiteral("7F7F7F"));
            val(QStringLiteral("sz"), QStringLiteral("18"));
            xml.writeEndElement();
        });
        simple(QStringLiteral("character"), QStringLiteral("Hyperlink"), QStringLiteral("Hyperlink"), [&] {
            xml.writeStartElement(kW, QStringLiteral("rPr"));
            val(QStringLiteral("color"), QStringLiteral("0563C1"));
            val(QStringLiteral("u"), QStringLiteral("single"));
            xml.writeEndElement();
        });
        xml.writeEndElement();
    });
}

namespace {

// One bulleted list with nine levels, 0.5 inch (720 twips) further in per level.
QByteArray wordNumbering() {
    return part([](QXmlStreamWriter &xml) {
        xml.writeNamespace(kW, QStringLiteral("w"));
        xml.writeStartElement(kW, QStringLiteral("numbering"));
        xml.writeStartElement(kW, QStringLiteral("abstractNum"));
        xml.writeAttribute(QStringLiteral("w:abstractNumId"), QStringLiteral("0"));
        for (int level = 0; level < 9; ++level) {
            xml.writeStartElement(kW, QStringLiteral("lvl"));
            xml.writeAttribute(QStringLiteral("w:ilvl"), QString::number(level));
            element(xml, kW, QStringLiteral("start"), {{QStringLiteral("w:val"), QStringLiteral("1")}});
            element(xml, kW, QStringLiteral("numFmt"), {{QStringLiteral("w:val"), QStringLiteral("bullet")}});
            element(xml, kW, QStringLiteral("lvlText"), {{QStringLiteral("w:val"), level % 2 ? QStringLiteral("◦") : QStringLiteral("•")}});
            element(xml, kW, QStringLiteral("lvlJc"), {{QStringLiteral("w:val"), QStringLiteral("left")}});
            xml.writeStartElement(kW, QStringLiteral("pPr"));
            element(xml, kW, QStringLiteral("ind"), {{QStringLiteral("w:left"), QString::number(720 * (level + 1))}, {QStringLiteral("w:hanging"), QStringLiteral("360")}});
            xml.writeEndElement();
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeStartElement(kW, QStringLiteral("num"));
        xml.writeAttribute(QStringLiteral("w:numId"), QStringLiteral("1"));
        element(xml, kW, QStringLiteral("abstractNumId"), {{QStringLiteral("w:val"), QStringLiteral("0")}});
        xml.writeEndElement();
        xml.writeEndElement();
    });
}

}  // namespace

// --- Excel ---

// Column letters: A..Z, AA.. (ECMA-376 Part 1, 18.3.1.4, cell reference).
QString columnName(int index) {
    QString name;
    for (int n = index + 1; n > 0; n = (n - 1) / 26) name.prepend(QChar(u'A' + (n - 1) % 26));
    return name;
}

namespace {

enum CellStyle { Plain = 0, Header = 1, Wrapped = 2 };

void cell(QXmlStreamWriter &xml, int column, int row, const QString &text, CellStyle style) {
    if (text.isEmpty()) return;
    xml.writeStartElement(kS, QStringLiteral("c"));
    xml.writeAttribute(QStringLiteral("r"), columnName(column) + QString::number(row));
    xml.writeAttribute(QStringLiteral("t"), QStringLiteral("inlineStr"));
    if (style != Plain) xml.writeAttribute(QStringLiteral("s"), QString::number(style));
    xml.writeStartElement(kS, QStringLiteral("is"));
    xml.writeStartElement(kS, QStringLiteral("t"));
    xml.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
    QString safe = xmlSafe(text);
    if (safe.size() > kMaxCellText) safe.truncate(safe[kMaxCellText - 1].isHighSurrogate() ? kMaxCellText - 1 : kMaxCellText);
    xml.writeCharacters(safe);
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndElement();
}

QByteArray worksheet(const MindMapModel *model, bool selected) {
    struct Row {
        const MindNode *node;
        int depth;
    };
    QList<Row> rows;
    int deepest = 0;
    bool notes = false, labels = false, links = false, tasks = false, formulas = false;
    if (model && model->root()) forEachTopic(model->root(), 0, [&](const MindNode *node, int depth) {
        rows.append({node, depth});
        deepest = std::max(deepest, depth);
        notes |= !node->notes.isEmpty();
        labels |= !node->labels.isEmpty();
        links |= !node->webLink.isEmpty();
        tasks |= node->task != MindNode::Task::None;
        formulas |= !node->formula.isEmpty();
    });
    // The optional columns the sheet uses, after the levels: header, text of a topic, width, style.
    struct Column {
        QString header;
        std::function<QString(const MindNode *)> text;
        int width;
        CellStyle style;
    };
    QList<Column> extra;
    if (notes) extra.append({tr("Notes"), [](const MindNode *n) { return n->notes; }, 60, Wrapped});
    if (labels) extra.append({tr("Labels"), [](const MindNode *n) { return n->labels.join(QStringLiteral(", ")); }, 24, Plain});
    if (links) extra.append({tr("Link"), [](const MindNode *n) { return n->webLink; }, 30, Plain});
    if (tasks) extra.append({tr("Task"), [](const MindNode *n) { return taskBox(n).trimmed(); }, 8, Plain});
    if (formulas) extra.append({tr("Formula"), [](const MindNode *n) {
        return n->formula.isEmpty() ? QString() : QLatin1Char('$') + n->formula + QLatin1Char('$'); }, 30, Plain});
    const int levels = deepest + 1;
    return part([&](QXmlStreamWriter &xml) {
        xml.writeDefaultNamespace(kS);
        xml.writeStartElement(kS, QStringLiteral("worksheet"));
        xml.writeStartElement(kS, QStringLiteral("sheetViews"));
        xml.writeStartElement(kS, QStringLiteral("sheetView"));
        if (selected) xml.writeAttribute(QStringLiteral("tabSelected"), QStringLiteral("1"));
        xml.writeAttribute(QStringLiteral("workbookViewId"), QStringLiteral("0"));
        element(xml, kS, QStringLiteral("pane"), {{QStringLiteral("ySplit"), QStringLiteral("1")}, {QStringLiteral("topLeftCell"), QStringLiteral("A2")},
                                                  {QStringLiteral("activePane"), QStringLiteral("bottomLeft")}, {QStringLiteral("state"), QStringLiteral("frozen")}});
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeStartElement(kS, QStringLiteral("cols"));
        const auto width = [&](int column, int characters) {
            element(xml, kS, QStringLiteral("col"), {{QStringLiteral("min"), QString::number(column + 1)}, {QStringLiteral("max"), QString::number(column + 1)},
                                                     {QStringLiteral("width"), QString::number(characters)}, {QStringLiteral("customWidth"), QStringLiteral("1")}});
        };
        for (int i = 0; i < levels; ++i) width(i, 24);
        for (int i = 0; i < extra.size(); ++i) width(levels + i, extra[i].width);
        xml.writeEndElement();
        xml.writeStartElement(kS, QStringLiteral("sheetData"));
        xml.writeStartElement(kS, QStringLiteral("row"));
        xml.writeAttribute(QStringLiteral("r"), QStringLiteral("1"));
        cell(xml, 0, 1, tr("Central topic"), Header);
        for (int i = 1; i < levels; ++i) cell(xml, i, 1, tr("Level %1").arg(i), Header);
        for (int i = 0; i < extra.size(); ++i) cell(xml, levels + i, 1, extra[i].header, Header);
        xml.writeEndElement();
        for (int r = 0; r < rows.size(); ++r) {
            const int number = r + 2;
            xml.writeStartElement(kS, QStringLiteral("row"));
            xml.writeAttribute(QStringLiteral("r"), QString::number(number));
            const QString title = Numbering::withNumber(rows[r].node->number, rows[r].node->title);
            cell(xml, rows[r].depth, number, title, title.contains(QLatin1Char('\n')) ? Wrapped : Plain);
            for (int i = 0; i < extra.size(); ++i) cell(xml, levels + i, number, extra[i].text(rows[r].node), extra[i].style);
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeEndElement();
    });
}

// Fonts 0 normal and 1 bold; the two fills Excel expects (none, gray125); cell formats 0 plain,
// 1 bold header, 2 wrapped text at the top (ECMA-376 Part 1, 18.8).
QByteArray workbookStyles() {
    return part([](QXmlStreamWriter &xml) {
        xml.writeDefaultNamespace(kS);
        xml.writeStartElement(kS, QStringLiteral("styleSheet"));
        xml.writeStartElement(kS, QStringLiteral("fonts"));
        xml.writeAttribute(QStringLiteral("count"), QStringLiteral("2"));
        for (const bool bold : {false, true}) {
            xml.writeStartElement(kS, QStringLiteral("font"));
            if (bold) element(xml, kS, QStringLiteral("b"));
            element(xml, kS, QStringLiteral("sz"), {{QStringLiteral("val"), QStringLiteral("11")}});
            element(xml, kS, QStringLiteral("name"), {{QStringLiteral("val"), QStringLiteral("Calibri")}});
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeStartElement(kS, QStringLiteral("fills"));
        xml.writeAttribute(QStringLiteral("count"), QStringLiteral("2"));
        for (const char *pattern : {"none", "gray125"}) {
            xml.writeStartElement(kS, QStringLiteral("fill"));
            element(xml, kS, QStringLiteral("patternFill"), {{QStringLiteral("patternType"), QString::fromLatin1(pattern)}});
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeStartElement(kS, QStringLiteral("borders"));
        xml.writeAttribute(QStringLiteral("count"), QStringLiteral("1"));
        xml.writeStartElement(kS, QStringLiteral("border"));
        for (const char *side : {"left", "right", "top", "bottom", "diagonal"}) element(xml, kS, QString::fromLatin1(side));
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeStartElement(kS, QStringLiteral("cellStyleXfs"));
        xml.writeAttribute(QStringLiteral("count"), QStringLiteral("1"));
        element(xml, kS, QStringLiteral("xf"), {{QStringLiteral("numFmtId"), QStringLiteral("0")}, {QStringLiteral("fontId"), QStringLiteral("0")},
                                                {QStringLiteral("fillId"), QStringLiteral("0")}, {QStringLiteral("borderId"), QStringLiteral("0")}});
        xml.writeEndElement();
        xml.writeStartElement(kS, QStringLiteral("cellXfs"));
        xml.writeAttribute(QStringLiteral("count"), QStringLiteral("3"));
        for (int style = Plain; style <= Wrapped; ++style) {
            xml.writeStartElement(kS, QStringLiteral("xf"));
            xml.writeAttribute(QStringLiteral("numFmtId"), QStringLiteral("0"));
            xml.writeAttribute(QStringLiteral("fontId"), style == Header ? QStringLiteral("1") : QStringLiteral("0"));
            xml.writeAttribute(QStringLiteral("fillId"), QStringLiteral("0"));
            xml.writeAttribute(QStringLiteral("borderId"), QStringLiteral("0"));
            xml.writeAttribute(QStringLiteral("xfId"), QStringLiteral("0"));
            if (style == Header) xml.writeAttribute(QStringLiteral("applyFont"), QStringLiteral("1"));
            if (style == Wrapped) {
                xml.writeAttribute(QStringLiteral("applyAlignment"), QStringLiteral("1"));
                element(xml, kS, QStringLiteral("alignment"), {{QStringLiteral("vertical"), QStringLiteral("top")}, {QStringLiteral("wrapText"), QStringLiteral("1")}});
            }
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeStartElement(kS, QStringLiteral("cellStyles"));
        xml.writeAttribute(QStringLiteral("count"), QStringLiteral("1"));
        element(xml, kS, QStringLiteral("cellStyle"), {{QStringLiteral("name"), QStringLiteral("Normal")}, {QStringLiteral("xfId"), QStringLiteral("0")},
                                                       {QStringLiteral("builtinId"), QStringLiteral("0")}});
        xml.writeEndElement();
        xml.writeEndElement();
    });
}

}  // namespace

QString writeFile(const QString &file, const QByteArray &bytes) {
    QSaveFile output(file);
    if (!output.open(QIODevice::WriteOnly)) return output.errorString();
    if (output.write(bytes) != bytes.size() || !output.commit()) return output.errorString();
    return {};
}

// A picture in a run (the caller writes the paragraph), cx x cy EMU, its part at relationId.
void inlinePicture(QXmlStreamWriter &xml, int number, const QString &name, const QString &relationId, qint64 cx, qint64 cy) {
    const QList<std::pair<QString, QString>> extent{{QStringLiteral("cx"), QString::number(cx)}, {QStringLiteral("cy"), QString::number(cy)}};
    xml.writeStartElement(kW, QStringLiteral("r"));
    xml.writeStartElement(kW, QStringLiteral("drawing"));
    xml.writeStartElement(kWp, QStringLiteral("inline"));
    for (const char *side : {"distT", "distB", "distL", "distR"}) xml.writeAttribute(QString::fromLatin1(side), QStringLiteral("0"));
    element(xml, kWp, QStringLiteral("extent"), extent);
    element(xml, kWp, QStringLiteral("docPr"), {{QStringLiteral("id"), QString::number(number)}, {QStringLiteral("name"), QStringLiteral("Picture %1").arg(number)}});
    xml.writeStartElement(kA, QStringLiteral("graphic"));
    xml.writeStartElement(kA, QStringLiteral("graphicData"));
    xml.writeAttribute(QStringLiteral("uri"), kPic);
    xml.writeStartElement(kPic, QStringLiteral("pic"));
    xml.writeStartElement(kPic, QStringLiteral("nvPicPr"));
    element(xml, kPic, QStringLiteral("cNvPr"), {{QStringLiteral("id"), QString::number(number)}, {QStringLiteral("name"), name}});
    element(xml, kPic, QStringLiteral("cNvPicPr"));
    xml.writeEndElement();
    xml.writeStartElement(kPic, QStringLiteral("blipFill"));
    element(xml, kA, QStringLiteral("blip"), {{QStringLiteral("r:embed"), relationId}});
    xml.writeStartElement(kA, QStringLiteral("stretch"));
    element(xml, kA, QStringLiteral("fillRect"));
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeStartElement(kPic, QStringLiteral("spPr"));
    xml.writeStartElement(kA, QStringLiteral("xfrm"));
    element(xml, kA, QStringLiteral("off"), {{QStringLiteral("x"), QStringLiteral("0")}, {QStringLiteral("y"), QStringLiteral("0")}});
    element(xml, kA, QStringLiteral("ext"), extent);
    xml.writeEndElement();
    xml.writeStartElement(kA, QStringLiteral("prstGeom"));
    xml.writeAttribute(QStringLiteral("prst"), QStringLiteral("rect"));
    element(xml, kA, QStringLiteral("avLst"));
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndElement();  // pic
    xml.writeEndElement();  // graphicData
    xml.writeEndElement();  // graphic
    xml.writeEndElement();  // inline
    xml.writeEndElement();  // drawing
    xml.writeEndElement();  // r
}

QStringList worksheetNames(const QStringList &sheetNames) {
    QStringList names;
    QSet<QString> used{QStringLiteral("history")};  // reserved by Excel
    for (int i = 0; i < sheetNames.size(); ++i) {
        QString base = xmlSafe(sheetNames[i]).simplified();
        for (QChar &c : base)
            if (QStringLiteral("[]:*?/\\").contains(c)) c = u'-';
        if (base.startsWith(QLatin1Char('\''))) base[0] = u'-';
        if (base.endsWith(QLatin1Char('\''))) base[base.size() - 1] = u'-';
        if (base.isEmpty()) base = tr("Sheet %1").arg(i + 1);
        QString name = base.left(kMaxSheetName);
        for (int n = 2; used.contains(name.toLower()); ++n) {
            const QString suffix = QStringLiteral("-%1").arg(n);
            name = base.left(kMaxSheetName - suffix.size()) + suffix;
        }
        used.insert(name.toLower());
        names.append(name);
    }
    return names;
}

Package docx(const QList<Sheet> &sheets, const ImageSource &imageSource, const QString &title) {
    WordWriter writer(imageSource);
    ZipWriter zip;
    zip.add(QStringLiteral("word/document.xml"), writer.document(sheets));
    zip.add(QStringLiteral("word/styles.xml"), wordStyles());
    zip.add(QStringLiteral("word/numbering.xml"), wordNumbering());
    for (const auto &[name, bytes] : writer.media()) zip.add(QStringLiteral("word/media/") + name, bytes);
    const QList<Ooxml::Relationship> relations = writer.relations();
    zip.add(QStringLiteral("word/_rels/document.xml.rels"), part([&](QXmlStreamWriter &xml) { relationships(xml, relations); }));
    finish(zip, QStringLiteral("word/document.xml"), title,
           {QStringLiteral("/word/document.xml|%1document.main+xml").arg(kWordType),
            QStringLiteral("/word/styles.xml|%1styles+xml").arg(kWordType),
            QStringLiteral("/word/numbering.xml|%1numbering+xml").arg(kWordType)});
    return {zip.archive(), writer.skipped()};
}

QByteArray xlsx(const QList<Sheet> &sheets, const QString &title) {
    QStringList sheetNames;
    for (const Sheet &sheet : sheets) sheetNames.append(sheet.name);
    const QStringList names = worksheetNames(sheetNames);
    ZipWriter zip;
    QList<Ooxml::Relationship> relations;
    QStringList overrides{QStringLiteral("/xl/workbook.xml|%1sheet.main+xml").arg(kSheetType),
                          QStringLiteral("/xl/styles.xml|%1styles+xml").arg(kSheetType)};
    for (int i = 0; i < sheets.size(); ++i) {
        const QString path = QStringLiteral("worksheets/sheet%1.xml").arg(i + 1);
        zip.add(QStringLiteral("xl/") + path, worksheet(sheets[i].model, i == 0));
        relations.append({QStringLiteral("worksheet"), path});
        overrides.append(QStringLiteral("/xl/%1|%2worksheet+xml").arg(path, kSheetType));
    }
    relations.append({QStringLiteral("styles"), QStringLiteral("styles.xml")});
    zip.add(QStringLiteral("xl/workbook.xml"), part([&](QXmlStreamWriter &xml) {
        xml.writeDefaultNamespace(kS);
        xml.writeNamespace(kRel, QStringLiteral("r"));
        xml.writeStartElement(kS, QStringLiteral("workbook"));
        xml.writeStartElement(kS, QStringLiteral("sheets"));
        for (int i = 0; i < names.size(); ++i) {
            xml.writeStartElement(kS, QStringLiteral("sheet"));
            xml.writeAttribute(QStringLiteral("name"), names[i]);
            xml.writeAttribute(QStringLiteral("sheetId"), QString::number(i + 1));
            xml.writeAttribute(kRel, QStringLiteral("id"), QStringLiteral("rId%1").arg(i + 1));
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeEndElement();
    }));
    zip.add(QStringLiteral("xl/_rels/workbook.xml.rels"), part([&](QXmlStreamWriter &xml) { relationships(xml, relations); }));
    zip.add(QStringLiteral("xl/styles.xml"), workbookStyles());
    finish(zip, QStringLiteral("xl/workbook.xml"), title, overrides);
    return zip.archive();
}

QString writeDocx(const QString &file, const QList<Sheet> &sheets, const ImageSource &imageSource, const QString &title, QStringList *skipped) {
    const Package package = docx(sheets, imageSource, title);
    if (skipped) *skipped = package.skipped;
    return writeFile(file, package.bytes);
}

QString writeXlsx(const QString &file, const QList<Sheet> &sheets, const QString &title) { return writeFile(file, xlsx(sheets, title)); }

}  // namespace OfficeExport
