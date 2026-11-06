#include "pitchexport.h"

#include "ooxml.h"
#include "zipwriter.h"

#include <QCoreApplication>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>
#include <functional>

namespace PitchExport {

using namespace Pitch;
using namespace Ooxml;

qreal fileRatio(Aspect aspect) { return aspectRatio(aspect, 16.0 / 9.0); }

namespace {
// OOXML measures in EMU: 914400 per inch, 12700 per point (ECMA-376 Part 1, 20.1.2.1). The short
// side of a slide is 7.5 inches, PowerPoint's own: 16:9 is 13.333 x 7.5, 4:3 is 10 x 7.5.
constexpr qint64 kShortSideEmu = 6858000;
constexpr qint64 kEmuPerPoint = 12700;
constexpr int kLongSidePx = 1920;  // the pictures of a slide, as sharp as a 1080p screen

const QString kMain = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main");
const QString kPml = QStringLiteral("http://schemas.openxmlformats.org/presentationml/2006/main");

QSize slideEmu(qreal ratio) {
    return ratio >= 1 ? QSize(int(std::lround(kShortSideEmu * ratio)), int(kShortSideEmu))
                      : QSize(int(kShortSideEmu), int(std::lround(kShortSideEmu / ratio)));
}

QSize slidePixels(qreal ratio) {
    return ratio >= 1 ? QSize(kLongSidePx, int(std::lround(kLongSidePx / ratio))) : QSize(int(std::lround(kLongSidePx * ratio)), kLongSidePx);
}

QString hex(const QColor &color) { return color.name(QColor::HexRgb).mid(1).toUpper(); }

QImage render(const Slide &slide, const Look &look, const QSize &size, int shown, QList<TextRun> *texts) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    paint(&painter, QRectF(QPointF(0, 0), size), slide, look, shown, 1.0, texts);
    return image;
}

// What item adds to a slide: the pixels of after that differ from before, the rest transparent,
// cropped to them. Over before, the layer gives after again exactly.
struct Layer {
    QImage image;
    QRect rect;
};
Layer layerBetween(const QImage &before, const QImage &after) {
    QRect bounds;
    for (int y = 0; y < after.height(); ++y) {
        const auto *a = reinterpret_cast<const QRgb *>(before.constScanLine(y));
        const auto *b = reinterpret_cast<const QRgb *>(after.constScanLine(y));
        for (int x = 0; x < after.width(); ++x)
            if (a[x] != b[x]) bounds |= QRect(x, y, 1, 1);
    }
    if (bounds.isEmpty()) return {};
    QImage image(bounds.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    for (int y = 0; y < bounds.height(); ++y) {
        const auto *a = reinterpret_cast<const QRgb *>(before.constScanLine(bounds.top() + y));
        const auto *b = reinterpret_cast<const QRgb *>(after.constScanLine(bounds.top() + y));
        auto *out = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < bounds.width(); ++x)
            if (a[bounds.left() + x] != b[bounds.left() + x]) out[x] = b[bounds.left() + x];
    }
    return {image, bounds};
}

void startPml(QXmlStreamWriter &xml, const QString &root) {
    xml.writeNamespace(kMain, QStringLiteral("a"));
    xml.writeNamespace(kRel, QStringLiteral("r"));
    xml.writeNamespace(kPml, QStringLiteral("p"));
    xml.writeStartElement(kPml, root);
}

void transform(QXmlStreamWriter &xml, qint64 x, qint64 y, qint64 cx, qint64 cy) {
    xml.writeStartElement(kMain, QStringLiteral("xfrm"));
    element(xml, kMain, QStringLiteral("off"), {{QStringLiteral("x"), QString::number(x)}, {QStringLiteral("y"), QString::number(y)}});
    element(xml, kMain, QStringLiteral("ext"), {{QStringLiteral("cx"), QString::number(cx)}, {QStringLiteral("cy"), QString::number(cy)}});
    xml.writeEndElement();
}

void groupStart(QXmlStreamWriter &xml) {
    xml.writeStartElement(kPml, QStringLiteral("cSld"));
    xml.writeStartElement(kPml, QStringLiteral("spTree"));
    xml.writeStartElement(kPml, QStringLiteral("nvGrpSpPr"));
    element(xml, kPml, QStringLiteral("cNvPr"), {{QStringLiteral("id"), QStringLiteral("1")}, {QStringLiteral("name"), QString()}});
    element(xml, kPml, QStringLiteral("cNvGrpSpPr"));
    element(xml, kPml, QStringLiteral("nvPr"));
    xml.writeEndElement();
    xml.writeStartElement(kPml, QStringLiteral("grpSpPr"));
    xml.writeStartElement(kMain, QStringLiteral("xfrm"));
    for (const char *name : {"off", "ext", "chOff", "chExt"}) {
        const bool size = name[0] == 'e' || name[2] == 'E';
        element(xml, kMain, QString::fromLatin1(name),
                {{size ? QStringLiteral("cx") : QStringLiteral("x"), QStringLiteral("0")}, {size ? QStringLiteral("cy") : QStringLiteral("y"), QStringLiteral("0")}});
    }
    xml.writeEndElement();
    xml.writeEndElement();
}

void picture(QXmlStreamWriter &xml, int id, const QString &name, const QString &relation, const QRect &px, qreal emuPerPx) {
    xml.writeStartElement(kPml, QStringLiteral("pic"));
    xml.writeStartElement(kPml, QStringLiteral("nvPicPr"));
    element(xml, kPml, QStringLiteral("cNvPr"), {{QStringLiteral("id"), QString::number(id)}, {QStringLiteral("name"), name}});
    xml.writeStartElement(kPml, QStringLiteral("cNvPicPr"));
    element(xml, kMain, QStringLiteral("picLocks"), {{QStringLiteral("noChangeAspect"), QStringLiteral("1")}});
    xml.writeEndElement();
    element(xml, kPml, QStringLiteral("nvPr"));
    xml.writeEndElement();
    xml.writeStartElement(kPml, QStringLiteral("blipFill"));
    xml.writeStartElement(kMain, QStringLiteral("blip"));
    xml.writeAttribute(kRel, QStringLiteral("embed"), relation);
    xml.writeEndElement();
    xml.writeStartElement(kMain, QStringLiteral("stretch"));
    element(xml, kMain, QStringLiteral("fillRect"));
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeStartElement(kPml, QStringLiteral("spPr"));
    transform(xml, std::llround(px.x() * emuPerPx), std::llround(px.y() * emuPerPx), std::llround(px.width() * emuPerPx), std::llround(px.height() * emuPerPx));
    xml.writeStartElement(kMain, QStringLiteral("prstGeom"));
    xml.writeAttribute(QStringLiteral("prst"), QStringLiteral("rect"));
    element(xml, kMain, QStringLiteral("avLst"));
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndElement();
}

// A text box where Mentis drew the text: same font, size (in points, in proportion to the slide),
// weight, capitals, spacing, colour and alignment. A wrapped text gets a little more room, since
// PowerPoint measures type a little differently, and as much more as a narrowed font loses there
// (PowerPoint has no width axis: Roboto at 75% comes out at full width); a one-line text does not wrap.
void textBox(QXmlStreamWriter &xml, int id, const TextRun &run, qreal emuPerPx) {
    const bool wrap = run.flags & (Qt::TextWordWrap | Qt::TextWrapAnywhere);
    QRectF rect = run.rect;
    if (wrap) {
        const int stretch = run.font.stretch() > 0 ? run.font.stretch() : 100;
        const qreal grow = rect.width() * (1.08 * 100.0 / std::min(stretch, 100) - 1);
        if (run.flags & Qt::AlignRight) rect.adjust(-grow, 0, 0, 0);
        else if (run.flags & Qt::AlignHCenter) rect.adjust(-grow / 2, 0, grow / 2, 0);
        else rect.adjust(0, 0, grow, 0);
    }
    const qreal points = run.font.pixelSize() * emuPerPx / kEmuPerPoint;
    xml.writeStartElement(kPml, QStringLiteral("sp"));
    xml.writeStartElement(kPml, QStringLiteral("nvSpPr"));
    element(xml, kPml, QStringLiteral("cNvPr"),
            {{QStringLiteral("id"), QString::number(id)}, {QStringLiteral("name"), run.item < 0 ? QStringLiteral("Title") : QStringLiteral("Item %1").arg(run.item + 1)}});
    element(xml, kPml, QStringLiteral("cNvSpPr"), {{QStringLiteral("txBox"), QStringLiteral("1")}});
    element(xml, kPml, QStringLiteral("nvPr"));
    xml.writeEndElement();
    xml.writeStartElement(kPml, QStringLiteral("spPr"));
    transform(xml, std::llround(rect.x() * emuPerPx), std::llround(rect.y() * emuPerPx), std::llround(rect.width() * emuPerPx),
              std::llround(rect.height() * emuPerPx));
    xml.writeStartElement(kMain, QStringLiteral("prstGeom"));
    xml.writeAttribute(QStringLiteral("prst"), QStringLiteral("rect"));
    element(xml, kMain, QStringLiteral("avLst"));
    xml.writeEndElement();
    element(xml, kMain, QStringLiteral("noFill"));
    xml.writeEndElement();
    xml.writeStartElement(kPml, QStringLiteral("txBody"));
    xml.writeStartElement(kMain, QStringLiteral("bodyPr"));
    xml.writeAttribute(QStringLiteral("wrap"), wrap ? QStringLiteral("square") : QStringLiteral("none"));
    for (const char *inset : {"lIns", "tIns", "rIns", "bIns"}) xml.writeAttribute(QString::fromLatin1(inset), QStringLiteral("0"));
    xml.writeAttribute(QStringLiteral("anchor"), QStringLiteral("ctr"));
    element(xml, kMain, QStringLiteral("noAutofit"));
    xml.writeEndElement();
    element(xml, kMain, QStringLiteral("lstStyle"));
    xml.writeStartElement(kMain, QStringLiteral("p"));
    element(xml, kMain, QStringLiteral("pPr"),
            {{QStringLiteral("algn"), run.flags & Qt::AlignRight ? QStringLiteral("r") : run.flags & Qt::AlignHCenter ? QStringLiteral("ctr") : QStringLiteral("l")}});
    xml.writeStartElement(kMain, QStringLiteral("r"));
    xml.writeStartElement(kMain, QStringLiteral("rPr"));
    xml.writeAttribute(QStringLiteral("lang"), QStringLiteral("en-US"));
    xml.writeAttribute(QStringLiteral("sz"), QString::number(std::clamp<qint64>(std::llround(points * 100), 100, 400000)));
    xml.writeAttribute(QStringLiteral("b"), run.font.weight() >= QFont::DemiBold ? QStringLiteral("1") : QStringLiteral("0"));
    if (run.font.capitalization() == QFont::AllUppercase) xml.writeAttribute(QStringLiteral("cap"), QStringLiteral("all"));
    // A spacing never set reads as 0 (not 100%): only one set by the theme goes (Soft's 112%).
    if (const qreal spacing = run.font.letterSpacing();
        run.font.letterSpacingType() == QFont::PercentageSpacing && spacing > 0 && spacing != 100)
        xml.writeAttribute(QStringLiteral("spc"), QString::number(std::llround((spacing - 100) / 100.0 * points * 100)));
    xml.writeAttribute(QStringLiteral("dirty"), QStringLiteral("0"));
    xml.writeStartElement(kMain, QStringLiteral("solidFill"));
    element(xml, kMain, QStringLiteral("srgbClr"), {{QStringLiteral("val"), hex(run.color)}});
    xml.writeEndElement();
    element(xml, kMain, QStringLiteral("latin"), {{QStringLiteral("typeface"), run.font.families().value(0, run.font.family())}});
    xml.writeEndElement();
    xml.writeTextElement(kMain, QStringLiteral("t"), xmlSafe(run.text));
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndElement();
}

// The main sequence: per click, the shapes of one item fade in together (preset 10, "Fade";
// ECMA-376 Part 1, 19.5).
void timing(QXmlStreamWriter &xml, const QList<QList<int>> &clicks) {
    int id = 1;
    const auto ctn = [&](const QList<std::pair<QString, QString>> &attributes) {
        xml.writeStartElement(kPml, QStringLiteral("cTn"));
        xml.writeAttribute(QStringLiteral("id"), QString::number(id++));
        for (const auto &[key, value] : attributes) xml.writeAttribute(key, value);
    };
    const auto delay = [&](const QString &value) {
        xml.writeStartElement(kPml, QStringLiteral("stCondLst"));
        element(xml, kPml, QStringLiteral("cond"), {{QStringLiteral("delay"), value}});
        xml.writeEndElement();
    };
    const auto target = [&](int shape) {
        xml.writeStartElement(kPml, QStringLiteral("tgtEl"));
        element(xml, kPml, QStringLiteral("spTgt"), {{QStringLiteral("spid"), QString::number(shape)}});
        xml.writeEndElement();
    };
    xml.writeStartElement(kPml, QStringLiteral("timing"));
    xml.writeStartElement(kPml, QStringLiteral("tnLst"));
    xml.writeStartElement(kPml, QStringLiteral("par"));
    ctn({{QStringLiteral("dur"), QStringLiteral("indefinite")}, {QStringLiteral("restart"), QStringLiteral("never")}, {QStringLiteral("nodeType"), QStringLiteral("tmRoot")}});
    xml.writeStartElement(kPml, QStringLiteral("childTnLst"));
    xml.writeStartElement(kPml, QStringLiteral("seq"));
    xml.writeAttribute(QStringLiteral("concurrent"), QStringLiteral("1"));
    xml.writeAttribute(QStringLiteral("nextAc"), QStringLiteral("seek"));
    ctn({{QStringLiteral("dur"), QStringLiteral("indefinite")}, {QStringLiteral("nodeType"), QStringLiteral("mainSeq")}});
    xml.writeStartElement(kPml, QStringLiteral("childTnLst"));
    for (const QList<int> &shapes : clicks) {
        xml.writeStartElement(kPml, QStringLiteral("par"));
        ctn({{QStringLiteral("fill"), QStringLiteral("hold")}});
        delay(QStringLiteral("indefinite"));
        xml.writeStartElement(kPml, QStringLiteral("childTnLst"));
        xml.writeStartElement(kPml, QStringLiteral("par"));
        ctn({{QStringLiteral("fill"), QStringLiteral("hold")}});
        delay(QStringLiteral("0"));
        xml.writeStartElement(kPml, QStringLiteral("childTnLst"));
        for (int i = 0; i < shapes.size(); ++i) {
            xml.writeStartElement(kPml, QStringLiteral("par"));
            ctn({{QStringLiteral("presetID"), QStringLiteral("10")}, {QStringLiteral("presetClass"), QStringLiteral("entr")},
                 {QStringLiteral("presetSubtype"), QStringLiteral("0")}, {QStringLiteral("fill"), QStringLiteral("hold")},
                 {QStringLiteral("nodeType"), i == 0 ? QStringLiteral("clickEffect") : QStringLiteral("withEffect")}});
            delay(QStringLiteral("0"));
            xml.writeStartElement(kPml, QStringLiteral("childTnLst"));
            xml.writeStartElement(kPml, QStringLiteral("set"));
            xml.writeStartElement(kPml, QStringLiteral("cBhvr"));
            ctn({{QStringLiteral("dur"), QStringLiteral("1")}, {QStringLiteral("fill"), QStringLiteral("hold")}});
            delay(QStringLiteral("0"));
            xml.writeEndElement();  // cTn
            target(shapes[i]);
            xml.writeStartElement(kPml, QStringLiteral("attrNameLst"));
            xml.writeTextElement(kPml, QStringLiteral("attrName"), QStringLiteral("style.visibility"));
            xml.writeEndElement();
            xml.writeEndElement();  // cBhvr
            xml.writeStartElement(kPml, QStringLiteral("to"));
            element(xml, kPml, QStringLiteral("strVal"), {{QStringLiteral("val"), QStringLiteral("visible")}});
            xml.writeEndElement();
            xml.writeEndElement();  // set
            xml.writeStartElement(kPml, QStringLiteral("animEffect"));
            xml.writeAttribute(QStringLiteral("transition"), QStringLiteral("in"));
            xml.writeAttribute(QStringLiteral("filter"), QStringLiteral("fade"));
            xml.writeStartElement(kPml, QStringLiteral("cBhvr"));
            ctn({{QStringLiteral("dur"), QStringLiteral("200")}});  // Mentis fades an item in over 200 ms
            xml.writeEndElement();
            target(shapes[i]);
            xml.writeEndElement();  // cBhvr
            xml.writeEndElement();  // animEffect
            xml.writeEndElement();  // childTnLst
            xml.writeEndElement();  // cTn
            xml.writeEndElement();  // par
        }
        xml.writeEndElement();  // childTnLst
        xml.writeEndElement();  // cTn
        xml.writeEndElement();  // par
        xml.writeEndElement();  // childTnLst
        xml.writeEndElement();  // cTn
        xml.writeEndElement();  // par
    }
    xml.writeEndElement();  // childTnLst
    xml.writeEndElement();  // cTn mainSeq
    for (const char *kind : {"prevCondLst", "nextCondLst"}) {
        xml.writeStartElement(kPml, QString::fromLatin1(kind));
        xml.writeStartElement(kPml, QStringLiteral("cond"));
        xml.writeAttribute(QStringLiteral("evt"), kind[0] == 'p' ? QStringLiteral("onPrev") : QStringLiteral("onNext"));
        xml.writeAttribute(QStringLiteral("delay"), QStringLiteral("0"));
        xml.writeStartElement(kPml, QStringLiteral("tgtEl"));
        element(xml, kPml, QStringLiteral("sldTgt"));
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeEndElement();
    }
    xml.writeEndElement();  // seq
    xml.writeEndElement();  // childTnLst
    xml.writeEndElement();  // cTn root
    xml.writeEndElement();  // par
    xml.writeEndElement();  // tnLst
    xml.writeEndElement();  // timing
}

QByteArray themePart() {
    return part([](QXmlStreamWriter &xml) {
        xml.writeNamespace(kMain, QStringLiteral("a"));
        xml.writeStartElement(kMain, QStringLiteral("theme"));
        xml.writeAttribute(QStringLiteral("name"), QStringLiteral("Mentis"));
        xml.writeStartElement(kMain, QStringLiteral("themeElements"));
        xml.writeStartElement(kMain, QStringLiteral("clrScheme"));
        xml.writeAttribute(QStringLiteral("name"), QStringLiteral("Mentis"));
        const std::pair<const char *, const char *> colours[] = {{"dk1", "000000"}, {"lt1", "FFFFFF"}, {"dk2", "1F2329"}, {"lt2", "EEEEEE"},
                                                                 {"accent1", "4F6FC4"}, {"accent2", "C8502E"}, {"accent3", "7CC47F"},
                                                                 {"accent4", "E3B13C"}, {"accent5", "8F96D6"}, {"accent6", "B23A5A"},
                                                                 {"hlink", "0563C1"}, {"folHlink", "954F72"}};
        for (const auto &[name, value] : colours) {
            xml.writeStartElement(kMain, QString::fromLatin1(name));
            element(xml, kMain, QStringLiteral("srgbClr"), {{QStringLiteral("val"), QString::fromLatin1(value)}});
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeStartElement(kMain, QStringLiteral("fontScheme"));
        xml.writeAttribute(QStringLiteral("name"), QStringLiteral("Mentis"));
        for (const char *kind : {"majorFont", "minorFont"}) {
            xml.writeStartElement(kMain, QString::fromLatin1(kind));
            element(xml, kMain, QStringLiteral("latin"), {{QStringLiteral("typeface"), QStringLiteral("Calibri")}});
            element(xml, kMain, QStringLiteral("ea"), {{QStringLiteral("typeface"), QString()}});
            element(xml, kMain, QStringLiteral("cs"), {{QStringLiteral("typeface"), QString()}});
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeStartElement(kMain, QStringLiteral("fmtScheme"));
        xml.writeAttribute(QStringLiteral("name"), QStringLiteral("Mentis"));
        const auto solid = [&] {
            xml.writeStartElement(kMain, QStringLiteral("solidFill"));
            element(xml, kMain, QStringLiteral("schemeClr"), {{QStringLiteral("val"), QStringLiteral("phClr")}});
            xml.writeEndElement();
        };
        xml.writeStartElement(kMain, QStringLiteral("fillStyleLst"));
        for (int i = 0; i < 3; ++i) solid();
        xml.writeEndElement();
        xml.writeStartElement(kMain, QStringLiteral("lnStyleLst"));
        for (int i = 0; i < 3; ++i) {
            xml.writeStartElement(kMain, QStringLiteral("ln"));
            xml.writeAttribute(QStringLiteral("w"), QString::number(9525 * (i + 1)));
            solid();
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeStartElement(kMain, QStringLiteral("effectStyleLst"));
        for (int i = 0; i < 3; ++i) {
            xml.writeStartElement(kMain, QStringLiteral("effectStyle"));
            element(xml, kMain, QStringLiteral("effectLst"));
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeStartElement(kMain, QStringLiteral("bgFillStyleLst"));
        for (int i = 0; i < 3; ++i) solid();
        xml.writeEndElement();
        xml.writeEndElement();  // fmtScheme
        xml.writeEndElement();  // themeElements
        element(xml, kMain, QStringLiteral("objectDefaults"));
        element(xml, kMain, QStringLiteral("extraClrSchemeLst"));
        xml.writeEndElement();
    });
}

void colourMap(QXmlStreamWriter &xml) {
    xml.writeStartElement(kPml, QStringLiteral("clrMap"));
    const std::pair<const char *, const char *> mapping[] = {{"bg1", "lt1"}, {"tx1", "dk1"}, {"bg2", "lt2"}, {"tx2", "dk2"}, {"accent1", "accent1"},
                                                             {"accent2", "accent2"}, {"accent3", "accent3"}, {"accent4", "accent4"}, {"accent5", "accent5"},
                                                             {"accent6", "accent6"}, {"hlink", "hlink"}, {"folHlink", "folHlink"}};
    for (const auto &[key, value] : mapping) xml.writeAttribute(QString::fromLatin1(key), QString::fromLatin1(value));
    xml.writeEndElement();
}

// A placeholder of the notes pages: the slide's picture or the notes' text.
void notesPlaceholder(QXmlStreamWriter &xml, int id, bool image, bool master, const QString &text) {
    xml.writeStartElement(kPml, QStringLiteral("sp"));
    xml.writeStartElement(kPml, QStringLiteral("nvSpPr"));
    element(xml, kPml, QStringLiteral("cNvPr"), {{QStringLiteral("id"), QString::number(id)}, {QStringLiteral("name"), image ? QStringLiteral("Slide Image") : QStringLiteral("Notes")}});
    xml.writeStartElement(kPml, QStringLiteral("cNvSpPr"));
    QList<std::pair<QString, QString>> locks{{QStringLiteral("noGrp"), QStringLiteral("1")}};
    if (image) locks += {{QStringLiteral("noRot"), QStringLiteral("1")}, {QStringLiteral("noChangeAspect"), QStringLiteral("1")}};
    element(xml, kMain, QStringLiteral("spLocks"), locks);
    xml.writeEndElement();
    xml.writeStartElement(kPml, QStringLiteral("nvPr"));
    QList<std::pair<QString, QString>> placeholder{{QStringLiteral("type"), image ? QStringLiteral("sldImg") : QStringLiteral("body")}};
    if (!image) placeholder.append({QStringLiteral("idx"), QStringLiteral("1")});
    element(xml, kPml, QStringLiteral("ph"), placeholder);
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeStartElement(kPml, QStringLiteral("spPr"));
    if (master) {
        if (image) transform(xml, 381000, 685800, 6096000, 3429000);
        else transform(xml, 685800, 4343400, 5486400, 4114800);
        xml.writeStartElement(kMain, QStringLiteral("prstGeom"));
        xml.writeAttribute(QStringLiteral("prst"), QStringLiteral("rect"));
        element(xml, kMain, QStringLiteral("avLst"));
        xml.writeEndElement();
    }
    xml.writeEndElement();
    if (!image) {
        xml.writeStartElement(kPml, QStringLiteral("txBody"));
        element(xml, kMain, QStringLiteral("bodyPr"));
        element(xml, kMain, QStringLiteral("lstStyle"));
        const QStringList lines = xmlSafe(text).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            xml.writeStartElement(kMain, QStringLiteral("p"));
            if (!line.isEmpty()) {
                xml.writeStartElement(kMain, QStringLiteral("r"));
                element(xml, kMain, QStringLiteral("rPr"), {{QStringLiteral("lang"), QStringLiteral("en-US")}, {QStringLiteral("dirty"), QStringLiteral("0")}});
                xml.writeTextElement(kMain, QStringLiteral("t"), QString(line).remove(QLatin1Char('\r')));
                xml.writeEndElement();
            }
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }
    xml.writeEndElement();
}
}  // namespace

QByteArray pptx(const QList<Slide> &slides, const Look &look, qreal ratio, const QString &title) {
    const QSize emu = slideEmu(ratio);
    const QSize pixels = slidePixels(ratio);
    const qreal emuPerPx = qreal(emu.width()) / pixels.width();
    ZipWriter zip;
    const QString presentationType = QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.");

    QStringList overrides;  // "/part|type"
    int image = 0;
    for (int index = 0; index < slides.size(); ++index) {
        Slide slide = slides[index];
        const int number = index + 1;
        // "One at a time - memorization" goes as "one at a time": PowerPoint does not dim the earlier items.
        if (slide.delivery == Delivery::OneMemorize) slide.delivery = Delivery::One;
        const bool animated = slide.steps() > 0;
        QList<TextRun> texts;
        const QImage base = render(slide, look, pixels, animated ? 0 : -1, &texts);
        QList<Relationship> relations{{QStringLiteral("slideLayout"), QStringLiteral("../slideLayouts/slideLayout1.xml")}};
        if (!slide.notes.isEmpty()) {
            relations.append({QStringLiteral("notesSlide"), QStringLiteral("../notesSlides/notesSlide%1.xml").arg(number)});
        }
        const auto addImage = [&](const QImage &picture) {
            const QString name = QStringLiteral("image%1.png").arg(++image);
            zip.add(QStringLiteral("ppt/media/") + name, png(picture));
            relations.append({QStringLiteral("image"), QStringLiteral("../media/") + name});
            return QStringLiteral("rId%1").arg(relations.size());
        };
        const QString baseRelation = addImage(base);
        QList<Layer> layers;
        QList<QString> layerRelations;
        if (animated) {
            QList<TextRun> all;
            render(slide, look, pixels, -1, &all);
            for (const TextRun &run : std::as_const(all))
                if (run.item >= 0) texts.append(run);
            QImage before = base;
            for (int i = 0; i < slide.items.size(); ++i) {
                QList<TextRun> unused;  // captured, so that no text gets into the pictures
                const QImage after = render(slide, look, pixels, i + 1, &unused);
                layers.append(layerBetween(before, after));
                layerRelations.append(layers.back().image.isNull() ? QString() : addImage(layers.back().image));
                before = after;
            }
        }
        zip.add(QStringLiteral("ppt/slides/slide%1.xml").arg(number), part([&](QXmlStreamWriter &xml) {
            startPml(xml, QStringLiteral("sld"));
            groupStart(xml);
            int id = 2;
            picture(xml, id++, QStringLiteral("Slide"), baseRelation, QRect(QPoint(0, 0), pixels), emuPerPx);
            for (const TextRun &run : std::as_const(texts))
                if (run.item < 0) textBox(xml, id++, run, emuPerPx);
            QList<QList<int>> clicks(slide.items.size());
            for (int i = 0; i < layers.size(); ++i)
                if (!layerRelations[i].isEmpty()) {
                    clicks[i].append(id);
                    picture(xml, id++, QStringLiteral("Item %1 picture").arg(i + 1), layerRelations[i], layers[i].rect, emuPerPx);
                }
            for (const TextRun &run : std::as_const(texts))
                if (run.item >= 0) {
                    if (animated && run.item < clicks.size()) clicks[run.item].append(id);
                    textBox(xml, id++, run, emuPerPx);
                }
            xml.writeEndElement();  // spTree
            xml.writeEndElement();  // cSld
            xml.writeStartElement(kPml, QStringLiteral("clrMapOvr"));
            element(xml, kMain, QStringLiteral("masterClrMapping"));
            xml.writeEndElement();
            xml.writeStartElement(kPml, QStringLiteral("transition"));
            xml.writeAttribute(QStringLiteral("spd"), QStringLiteral("med"));
            element(xml, kPml, QStringLiteral("fade"));
            xml.writeEndElement();
            clicks.removeIf([](const QList<int> &shapes) { return shapes.isEmpty(); });
            if (animated && !clicks.isEmpty()) timing(xml, clicks);
            xml.writeEndElement();
        }));
        zip.add(QStringLiteral("ppt/slides/_rels/slide%1.xml.rels").arg(number), part([&](QXmlStreamWriter &xml) { relationships(xml, relations); }));
        overrides.append(QStringLiteral("/ppt/slides/slide%1.xml|%2slide+xml").arg(number).arg(presentationType));
        if (!slide.notes.isEmpty()) {
            zip.add(QStringLiteral("ppt/notesSlides/notesSlide%1.xml").arg(number), part([&](QXmlStreamWriter &xml) {
                startPml(xml, QStringLiteral("notes"));
                groupStart(xml);
                notesPlaceholder(xml, 2, true, false, {});
                notesPlaceholder(xml, 3, false, false, slide.notes);
                xml.writeEndElement();
                xml.writeEndElement();
                xml.writeStartElement(kPml, QStringLiteral("clrMapOvr"));
                element(xml, kMain, QStringLiteral("masterClrMapping"));
                xml.writeEndElement();
                xml.writeEndElement();
            }));
            zip.add(QStringLiteral("ppt/notesSlides/_rels/notesSlide%1.xml.rels").arg(number), part([&](QXmlStreamWriter &xml) {
                relationships(xml, {{QStringLiteral("notesMaster"), QStringLiteral("../notesMasters/notesMaster1.xml")},
                                    {QStringLiteral("slide"), QStringLiteral("../slides/slide%1.xml").arg(number)}});
            }));
            overrides.append(QStringLiteral("/ppt/notesSlides/notesSlide%1.xml|%2notesSlide+xml").arg(number).arg(presentationType));
        }
    }

    // The presentation: master, theme, properties, notes master, then the slides.
    QList<Relationship> presentationRelations{
        {QStringLiteral("slideMaster"), QStringLiteral("slideMasters/slideMaster1.xml")}, {QStringLiteral("theme"), QStringLiteral("theme/theme1.xml")},
        {QStringLiteral("presProps"), QStringLiteral("presProps.xml")}, {QStringLiteral("viewProps"), QStringLiteral("viewProps.xml")},
        {QStringLiteral("tableStyles"), QStringLiteral("tableStyles.xml")}, {QStringLiteral("notesMaster"), QStringLiteral("notesMasters/notesMaster1.xml")}};
    const int firstSlideRelation = int(presentationRelations.size()) + 1;
    for (int number = 1; number <= slides.size(); ++number)
        presentationRelations.append({QStringLiteral("slide"), QStringLiteral("slides/slide%1.xml").arg(number)});
    zip.add(QStringLiteral("ppt/presentation.xml"), part([&](QXmlStreamWriter &xml) {
        startPml(xml, QStringLiteral("presentation"));
        xml.writeStartElement(kPml, QStringLiteral("sldMasterIdLst"));
        xml.writeStartElement(kPml, QStringLiteral("sldMasterId"));
        xml.writeAttribute(QStringLiteral("id"), QStringLiteral("2147483648"));
        xml.writeAttribute(kRel, QStringLiteral("id"), QStringLiteral("rId1"));
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeStartElement(kPml, QStringLiteral("notesMasterIdLst"));
        xml.writeStartElement(kPml, QStringLiteral("notesMasterId"));
        xml.writeAttribute(kRel, QStringLiteral("id"), QStringLiteral("rId6"));
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeStartElement(kPml, QStringLiteral("sldIdLst"));
        for (int i = 0; i < slides.size(); ++i) {
            xml.writeStartElement(kPml, QStringLiteral("sldId"));
            xml.writeAttribute(QStringLiteral("id"), QString::number(256 + i));
            xml.writeAttribute(kRel, QStringLiteral("id"), QStringLiteral("rId%1").arg(firstSlideRelation + i));
            xml.writeEndElement();
        }
        xml.writeEndElement();
        element(xml, kPml, QStringLiteral("sldSz"), {{QStringLiteral("cx"), QString::number(emu.width())}, {QStringLiteral("cy"), QString::number(emu.height())}});
        element(xml, kPml, QStringLiteral("notesSz"), {{QStringLiteral("cx"), QStringLiteral("6858000")}, {QStringLiteral("cy"), QStringLiteral("9144000")}});
        xml.writeEndElement();
    }));
    zip.add(QStringLiteral("ppt/_rels/presentation.xml.rels"), part([&](QXmlStreamWriter &xml) { relationships(xml, presentationRelations); }));

    zip.add(QStringLiteral("ppt/slideMasters/slideMaster1.xml"), part([](QXmlStreamWriter &xml) {
        startPml(xml, QStringLiteral("sldMaster"));
        groupStart(xml);
        xml.writeEndElement();
        xml.writeEndElement();
        colourMap(xml);
        xml.writeStartElement(kPml, QStringLiteral("sldLayoutIdLst"));
        xml.writeStartElement(kPml, QStringLiteral("sldLayoutId"));
        xml.writeAttribute(QStringLiteral("id"), QStringLiteral("2147483649"));
        xml.writeAttribute(kRel, QStringLiteral("id"), QStringLiteral("rId1"));
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeEndElement();
    }));
    zip.add(QStringLiteral("ppt/slideMasters/_rels/slideMaster1.xml.rels"), part([](QXmlStreamWriter &xml) {
        relationships(xml, {{QStringLiteral("slideLayout"), QStringLiteral("../slideLayouts/slideLayout1.xml")}, {QStringLiteral("theme"), QStringLiteral("../theme/theme1.xml")}});
    }));
    zip.add(QStringLiteral("ppt/slideLayouts/slideLayout1.xml"), part([](QXmlStreamWriter &xml) {
        startPml(xml, QStringLiteral("sldLayout"));
        xml.writeAttribute(QStringLiteral("type"), QStringLiteral("blank"));
        xml.writeAttribute(QStringLiteral("preserve"), QStringLiteral("1"));
        groupStart(xml);
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeStartElement(kPml, QStringLiteral("clrMapOvr"));
        element(xml, kMain, QStringLiteral("masterClrMapping"));
        xml.writeEndElement();
        xml.writeEndElement();
    }));
    zip.add(QStringLiteral("ppt/slideLayouts/_rels/slideLayout1.xml.rels"), part([](QXmlStreamWriter &xml) {
        relationships(xml, {{QStringLiteral("slideMaster"), QStringLiteral("../slideMasters/slideMaster1.xml")}});
    }));
    zip.add(QStringLiteral("ppt/notesMasters/notesMaster1.xml"), part([](QXmlStreamWriter &xml) {
        startPml(xml, QStringLiteral("notesMaster"));
        groupStart(xml);
        notesPlaceholder(xml, 2, true, true, {});
        notesPlaceholder(xml, 3, false, true, {});
        xml.writeEndElement();
        xml.writeEndElement();
        colourMap(xml);
        xml.writeEndElement();
    }));
    zip.add(QStringLiteral("ppt/notesMasters/_rels/notesMaster1.xml.rels"), part([](QXmlStreamWriter &xml) {
        relationships(xml, {{QStringLiteral("theme"), QStringLiteral("../theme/theme2.xml")}});
    }));
    zip.add(QStringLiteral("ppt/theme/theme1.xml"), themePart());
    zip.add(QStringLiteral("ppt/theme/theme2.xml"), themePart());
    zip.add(QStringLiteral("ppt/presProps.xml"), part([](QXmlStreamWriter &xml) {
        startPml(xml, QStringLiteral("presentationPr"));
        xml.writeEndElement();
    }));
    zip.add(QStringLiteral("ppt/viewProps.xml"), part([](QXmlStreamWriter &xml) {
        startPml(xml, QStringLiteral("viewPr"));
        element(xml, kPml, QStringLiteral("gridSpacing"), {{QStringLiteral("cx"), QStringLiteral("76200")}, {QStringLiteral("cy"), QStringLiteral("76200")}});
        xml.writeEndElement();
    }));
    zip.add(QStringLiteral("ppt/tableStyles.xml"), part([](QXmlStreamWriter &xml) {
        xml.writeNamespace(kMain, QStringLiteral("a"));
        xml.writeStartElement(kMain, QStringLiteral("tblStyleLst"));
        xml.writeAttribute(QStringLiteral("def"), QStringLiteral("{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}"));
        xml.writeEndElement();
    }));
    overrides += {QStringLiteral("/ppt/presentation.xml|%1presentation.main+xml").arg(presentationType),
                  QStringLiteral("/ppt/slideMasters/slideMaster1.xml|%1slideMaster+xml").arg(presentationType),
                  QStringLiteral("/ppt/slideLayouts/slideLayout1.xml|%1slideLayout+xml").arg(presentationType),
                  QStringLiteral("/ppt/notesMasters/notesMaster1.xml|%1notesMaster+xml").arg(presentationType),
                  QStringLiteral("/ppt/theme/theme1.xml|application/vnd.openxmlformats-officedocument.theme+xml"),
                  QStringLiteral("/ppt/theme/theme2.xml|application/vnd.openxmlformats-officedocument.theme+xml"),
                  QStringLiteral("/ppt/presProps.xml|%1presProps+xml").arg(presentationType),
                  QStringLiteral("/ppt/viewProps.xml|%1viewProps+xml").arg(presentationType),
                  QStringLiteral("/ppt/tableStyles.xml|%1tableStyles+xml").arg(presentationType)};
    finish(zip, QStringLiteral("ppt/presentation.xml"), title, overrides);
    return zip.archive();
}

QString writePptx(const QString &file, const QList<Slide> &slides, const Look &look, qreal ratio, const QString &title) {
    QSaveFile output(file);
    if (!output.open(QIODevice::WriteOnly)) return output.errorString();
    const QByteArray bytes = pptx(slides, look, ratio, title);
    if (output.write(bytes) != bytes.size() || !output.commit()) return output.errorString();
    return {};
}

void paintFramed(QPainter *painter, const QRectF &rect, const Slide &slide, const Look &look, const ExportFrame &frame) {
    if (!frame.enabled()) {
        paint(painter, rect, slide, look);
        return;
    }
    // The page in pixels: the frame on its edge, 8 px of white, the slide in the rest (its shape kept).
    const qreal band = frame.width() + 8;
    painter->fillRect(rect, Qt::white);
    const QRectF room = rect.adjusted(band, band, -band, -band);
    const QSizeF shown = rect.size().scaled(room.size(), Qt::KeepAspectRatio);
    paint(painter, QRectF(room.center() - QPointF(shown.width() / 2, shown.height() / 2), shown), slide, look);
    frame.paint(painter, rect);
}

QString writePdf(const QString &file, const QList<Slide> &slides, const Look &look, qreal ratio, const QString &title,
                 const ExportFrame &frame) {
    QSaveFile output(file);
    if (!output.open(QIODevice::WriteOnly)) return output.errorString();
    {
        QPdfWriter pdf(&output);
        // The long side 254 mm (10 inches), like PowerPoint's 4:3 width; the short side in proportion.
        const QSizeF size = ratio >= 1 ? QSizeF(254, 254 / ratio) : QSizeF(254 * ratio, 254);
        pdf.setPageSize(QPageSize(size, QPageSize::Millimeter));
        pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
        pdf.setResolution(96);
        pdf.setTitle(title);
        pdf.setCreator(QStringLiteral("Mentis"));
        QPainter painter;
        if (!painter.begin(&pdf)) return QCoreApplication::translate("PitchExport", "The PDF could not be written.");
        for (int i = 0; i < slides.size(); ++i) {
            if (i > 0) pdf.newPage();
            paintFramed(&painter, QRectF(0, 0, pdf.width(), pdf.height()), slides[i], look, frame);
        }
        painter.end();
    }
    if (!output.commit()) return output.errorString();
    return {};
}

}  // namespace PitchExport
