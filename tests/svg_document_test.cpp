// FEAT-070 stage 1: the colours of an SVG found and replaced wherever they are written.
#include "svgdocument.h"

#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QTest>

#include <cmath>

class SvgDocumentTest final : public QObject {
    Q_OBJECT

private slots:
    void colorsFromAttributesStylesAndSheets();
    void replaceKeepsTheRestOfTheFile();
    void defaultBlackFill();
    void rejectsWhatIsNotSvg();
    void quarterTurnsAndFlipsKeepTheFrame();
    void freeAngleWidensTheFrame();
    void frameWritesWidthAndHeight();
    void labelOnTop();
    void drawingBoundsFindWhatIsDrawn();
    void shapesGetIdsAndOutlines();
    void shapesMoveAsQtDrawsThem();
    void shapesRemovedAndReordered();
    void pathsReadAndWrittenBack();
    void newShapesAndTheirStyle();
    void orderAcrossGroups();
};

namespace {
QStringList names(const QList<QColor> &colors) {
    QStringList result;
    for (const QColor &color : colors) result.append(color.name(QColor::HexArgb));
    return result;
}
}  // namespace

void SvgDocumentTest::colorsFromAttributesStylesAndSheets() {
    SvgDocument document;
    QVERIFY(document.load(R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10">
<style>.a { fill: #00FF00 } .b{stroke:rgb(0, 0, 255) !important}</style>
<defs><linearGradient id="g"><stop offset="0" stop-color="yellow"/></linearGradient></defs>
<path class="a" d="M0 0h5v5z"/><rect fill="#f00" stroke="red" style="fill-opacity:.5;stroke:#ff0000" width="2" height="2"/>
<circle fill="url(#g)" stroke="none" r="2"/><path fill="rgba(255,0,0,0.5)" d="M1 1h1v1z"/></svg>)svg"));
    // Order of first appearance; #f00, red and #ff0000 are one colour; the half-transparent red is another.
    QCOMPARE(names(document.colors()), QStringList({QStringLiteral("#ff00ff00"), QStringLiteral("#ff0000ff"), QStringLiteral("#ffffff00"),
                                                    QStringLiteral("#ffff0000"), QStringLiteral("#80ff0000")}));
}

void SvgDocumentTest::replaceKeepsTheRestOfTheFile() {
    SvgDocument document;
    QVERIFY(document.load(R"svg(<?xml version="1.0"?>
<!-- comment -->
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 10 10">
<style><![CDATA[.a{fill:#f00}]]></style>
<path class="a" d="M0 0h5v5z"/><rect fill="red" style="stroke:#F00;stroke-width:2" width="2" height="2"/>
<path fill="rgba(255,0,0,0.5)" d="M1 1h1v1z"/><use xlink:href="#x"/></svg>)svg"));
    document.replaceColor(QColor(Qt::red), QColor(QStringLiteral("#123456")));
    const QByteArray data = document.data();
    QVERIFY(!data.contains("#f00") && !data.contains("#F00") && !data.contains("\"red\""));
    QVERIFY(data.contains(".a{fill:#123456}"));
    QVERIFY(data.contains("fill=\"#123456\""));
    QVERIFY(data.contains("stroke:#123456;stroke-width:2"));
    QVERIFY(data.contains("rgba(255,0,0,0.5)"));  // half-transparent red is a colour of its own
    QVERIFY(data.contains("<!-- comment -->"));
    QVERIFY(data.contains("xlink:href=\"#x\""));
    QVERIFY(data.contains("xmlns:xlink=\"http://www.w3.org/1999/xlink\""));
    QVERIFY(QSvgRenderer(data).isValid());
    QCOMPARE(names(document.colors()), QStringList({QStringLiteral("#ff123456"), QStringLiteral("#80ff0000")}));
    document.replaceColor(QColor(255, 0, 0, 128), QColor(QStringLiteral("#123456")));
    QVERIFY(document.data().contains("rgba(18,52,86,0.502)"));  // the alpha of the replaced colour stays
}

void SvgDocumentTest::defaultBlackFill() {
    SvgDocument document;
    QVERIFY(document.load(R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10"><defs><path id="p" d="M0 0h1v1z"/></defs>
<g fill="#00f"><path d="M0 0h5v5z"/></g><path d="M5 5h5v5z"/></svg>)svg"));
    QCOMPARE(names(document.colors()), QStringList({QStringLiteral("#ff0000ff"), QStringLiteral("#ff000000")}));
    document.replaceColor(QColor(Qt::black), QColor(Qt::green));
    QVERIFY(document.data().contains("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\" fill=\"#00ff00\">"));
    // The fill is now written on the root, so it comes first.
    QCOMPARE(names(document.colors()), QStringList({QStringLiteral("#ff00ff00"), QStringLiteral("#ff0000ff")}));
}

void SvgDocumentTest::rejectsWhatIsNotSvg() {
    SvgDocument document;
    QVERIFY(!document.load("<html><body/></html>"));
    QVERIFY(!document.load("<svg><path></svg>"));
    QVERIFY(document.colors().isEmpty());
    QVERIFY(document.data().isEmpty());
}

namespace {
// Plain literals: moc reads the "//" of a raw string's URL as a comment.
const QByteArray kWide = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 20 10\"><rect x=\"2\" y=\"3\" width=\"4\" height=\"5\" fill=\"#f00\"/></svg>";

bool sameRect(const QRectF &a, const QRectF &b, qreal tolerance = 1e-6) {
    return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance && std::abs(a.width() - b.width()) <= tolerance &&
           std::abs(a.height() - b.height()) <= tolerance;
}
}  // namespace

// FEAT-070 stage 2: the drawing turns inside a group; the frame turns with it, exactly for quarter turns.
void SvgDocumentTest::quarterTurnsAndFlipsKeepTheFrame() {
    SvgDocument document;
    QVERIFY(document.load(kWide));
    document.setTransform({90, false, false});
    QVERIFY(sameRect(document.viewBox(), QRectF(5, -5, 10, 20)));  // around the centre (10, 5)
    QVERIFY(document.data().contains("id=\"mentis-content\""));
    QVERIFY(document.data().contains("rotate(90)"));
    SvgDocument reopened;
    QVERIFY(reopened.load(document.data()));  // the values come back from the file
    QCOMPARE(reopened.transform(), (SvgDocument::Transform{90, false, false}));
    document.setTransform({0, true, false});
    QVERIFY(sameRect(document.viewBox(), QRectF(0, 0, 20, 10)));
    QVERIFY(document.data().contains("scale(-1 1)"));
    QCOMPARE(document.data().count("mentis-content"), 1);  // the group is made once
    QVERIFY(QSvgRenderer(document.data()).isValid());
}

void SvgDocumentTest::freeAngleWidensTheFrame() {
    SvgDocument document;
    const QByteArray square = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\"><path d=\"M0 0h10v10z\"/></svg>";
    QVERIFY(document.load(square));
    document.setTransform({45, false, false});
    const qreal side = 10 * std::sqrt(2.0);
    QVERIFY(sameRect(document.viewBox(), QRectF(5 - side / 2, 5 - side / 2, side, side), 1e-4));
}

void SvgDocumentTest::frameWritesWidthAndHeight() {
    SvgDocument document;
    QVERIFY(document.load(kWide));
    document.setViewBox(QRectF(1, 2, 8, 4));
    QVERIFY(document.data().contains("viewBox=\"1 2 8 4\""));
    QVERIFY(document.data().contains("width=\"256\""));
    QVERIFY(document.data().contains("height=\"128\""));
    QCOMPARE(document.viewBox(), QRectF(1, 2, 8, 4));
}

void SvgDocumentTest::labelOnTop() {
    SvgDocument document;
    QVERIFY(document.load(kWide));
    SvgDocument::Label label;
    label.text = QStringLiteral("3 & <b>");
    label.family = QStringLiteral("Roboto");
    label.size = 4;
    label.color = QColor(QStringLiteral("#0000ff"));
    label.bold = true;
    label.position = QPointF(10, 6);
    document.setLabel(label);
    QCOMPARE(document.label(), label);
    QVERIFY(document.data().contains("3 &amp; &lt;b"));
    QVERIFY(names(document.colors()).contains(QStringLiteral("#ff0000ff")));
    QVERIFY(!document.labelBounds().isEmpty());
    // The label does not turn with the drawing but goes where its place goes.
    document.setTransform({90, false, false});
    QCOMPARE(document.label().position, QPointF(9, 5));
    QCOMPARE(document.data().count("mentis-label"), 1);
    label = document.label();
    label.text.clear();
    document.setLabel(label);
    QVERIFY(!document.data().contains("mentis-label"));
    QVERIFY(document.label().text.isEmpty());
}

void SvgDocumentTest::drawingBoundsFindWhatIsDrawn() {
    SvgDocument document;
    QVERIFY(document.load(kWide));
    QVERIFY(sameRect(document.drawingBounds(), QRectF(2, 3, 4, 5), 0.1));
    document.setViewBox(QRectF(0, 0, 4, 4));  // what lies outside the frame is still found
    QVERIFY(sameRect(document.drawingBounds(), QRectF(2, 3, 4, 5), 0.1));
}

namespace {
const QByteArray kShapes =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
    "<defs><path id=\"hidden\" d=\"M0 0h1v1z\"/></defs>"
    "<path d=\"M10 10h20v20h-20z\" fill=\"#f00\"/>"
    "<rect id=\"r\" x=\"40\" y=\"10\" width=\"20\" height=\"10\" fill=\"#0f0\"/>"
    "<g transform=\"translate(10 0) scale(2)\"><circle cx=\"20\" cy=\"30\" r=\"5\" fill=\"none\" stroke=\"#00f\"/></g></svg>";

QStringList ids(const QList<SvgDocument::Shape> &shapes) {
    QStringList result;
    for (const SvgDocument::Shape &shape : shapes) result.append(shape.id);
    return result;
}

// The pixels Qt draws for an SVG, to compare two writings of the same picture.
QImage rendered(const QByteArray &data) {
    QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    QSvgRenderer(data).render(&painter);
    return image;
}

double differentPixels(const QImage &a, const QImage &b) {
    int different = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (std::abs(qAlpha(a.pixel(x, y)) - qAlpha(b.pixel(x, y))) > 64) ++different;
    return double(different) / (a.width() * a.height());
}
}  // namespace

// FEAT-070 stage 3: drawn shapes get ids; outlines include their transforms and their groups'.
void SvgDocumentTest::shapesGetIdsAndOutlines() {
    SvgDocument document;
    QVERIFY(document.load(kShapes));
    document.ensureShapeIds();
    const QList<SvgDocument::Shape> shapes = document.drawnShapes();
    QCOMPARE(ids(shapes), QStringList({QStringLiteral("mentis-shape-1"), QStringLiteral("r"), QStringLiteral("mentis-shape-2")}));
    QVERIFY(sameRect(shapes[2].outline.boundingRect(), QRectF(40, 50, 20, 20), 1e-3));  // (20,30) r 5, scaled 2, moved 10
    QCOMPARE(SvgDocument::shapeAt(shapes, QPointF(50, 15), 1), QStringLiteral("r"));
    QCOMPARE(SvgDocument::shapeAt(shapes, QPointF(50, 60), 1), QString());  // inside the circle with no fill
    QCOMPARE(SvgDocument::shapeAt(shapes, QPointF(60, 60), 1), QStringLiteral("mentis-shape-2"));  // on its outline
    QVERIFY(sameRect(SvgDocument::bounds(shapes, {QStringLiteral("mentis-shape-1"), QStringLiteral("r")}), QRectF(10, 10, 50, 20)));
    SvgDocument again;
    QVERIFY(again.load(document.data()));
    again.ensureShapeIds();
    QCOMPARE(again.data(), document.data());  // ids are given once
}

void SvgDocumentTest::shapesMoveAsQtDrawsThem() {
    SvgDocument document;
    QVERIFY(document.load(kShapes));
    document.ensureShapeIds();
    QTransform move;
    move.translate(5, 7);
    QTransform grow;  // doubled around the circle's centre (50, 60)
    grow.translate(50, 60);
    grow.scale(2, 2);
    grow.translate(-50, -60);
    document.transformShapes({QStringLiteral("mentis-shape-2")}, grow * move);
    const QList<SvgDocument::Shape> shapes = document.drawnShapes();
    QVERIFY(sameRect(shapes[2].outline.boundingRect(), QRectF(35, 47, 40, 40), 1e-3));
    QVERIFY(sameRect(shapes[1].outline.boundingRect(), QRectF(40, 10, 20, 10), 1e-3));  // the others stay
    // Qt draws it where the outline says, plus half the stroke: width 1, doubled twice.
    document.removeShapes({QStringLiteral("mentis-shape-1"), QStringLiteral("r")});
    const QRectF drawn = document.drawingBounds();
    QVERIFY2(sameRect(drawn, QRectF(33, 45, 44, 44), 1), qPrintable(QStringLiteral("%1 %2 %3 %4").arg(drawn.x()).arg(drawn.y()).arg(drawn.width()).arg(drawn.height())));
}

void SvgDocumentTest::shapesRemovedAndReordered() {
    SvgDocument document;
    QVERIFY(document.load(kShapes));
    document.ensureShapeIds();
    document.reorderShapes({QStringLiteral("mentis-shape-1")}, SvgDocument::Order::Front);
    QCOMPARE(ids(document.drawnShapes()).last(), QStringLiteral("mentis-shape-1"));
    document.reorderShapes({QStringLiteral("mentis-shape-1")}, SvgDocument::Order::Back);
    QCOMPARE(ids(document.drawnShapes()).first(), QStringLiteral("mentis-shape-1"));
    document.removeShapes({QStringLiteral("r")});
    QVERIFY(!ids(document.drawnShapes()).contains(QStringLiteral("r")));
    QVERIFY(document.data().contains("id=\"hidden\""));  // defs untouched
}

void SvgDocumentTest::pathsReadAndWrittenBack() {
    // Every command, relative and absolute, compact numbers and flags.
    const QByteArray data =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\"><path id=\"p\" fill=\"#000\" d=\""
        "M10 10l20-0h5V20H45v10c5 0 10 5 10 10s-5 10-10 10q-5 10-10 0t-10 0"
        "a10 5 30 1015 5A8 8 0 0 1 10 60Z m60 0h10v10h-10zM70.5.5 80 5 75 9z\"/>"
        "<rect id=\"b\" x=\"60\" y=\"80\" width=\"30\" height=\"15\" rx=\"4\"/>"
        "<polygon id=\"g\" points=\"5,80 20,95 5,95\"/></svg>";
    SvgDocument document;
    QVERIFY(document.load(data));
    const QImage before = rendered(data);
    for (const QString &id : {QStringLiteral("p"), QStringLiteral("b"), QStringLiteral("g")}) {
        const QList<SvgDocument::SubPath> path = document.path(id);
        QVERIFY(!path.isEmpty());
        document.setPath(id, path);
    }
    QVERIFY(!document.data().contains("<rect") && !document.data().contains("<polygon"));
    QVERIFY(!document.data().contains(" rx="));
    QCOMPARE(document.path(QStringLiteral("p")).size(), 3);
    QVERIFY(document.path(QStringLiteral("p")).first().closed);
    const double difference = differentPixels(before, rendered(document.data()));
    QVERIFY2(difference < 0.002, qPrintable(QString::number(difference)));
}

// FEAT-070 stage 4: a new path on top (below the label); its style read and changed part by part.
void SvgDocumentTest::newShapesAndTheirStyle() {
    SvgDocument document;
    QVERIFY(document.load(kShapes));
    SvgDocument::Label label;
    label.text = QStringLiteral("1");
    label.size = 10;
    label.position = QPointF(50, 50);
    document.setLabel(label);
    QPainterPath square;
    square.addRect(QRectF(70, 70, 20, 20));
    const SvgDocument::ShapeStyle style{QColor(QStringLiteral("#123456")), QColor(QStringLiteral("#ff0000")), 2};
    const QString id = document.addPath(SvgDocument::subPaths(square), style);
    QVERIFY(!id.isEmpty());
    const QList<SvgDocument::Shape> shapes = document.drawnShapes();
    QCOMPARE(shapes.last().id, id);  // on top of the drawing
    QVERIFY(sameRect(shapes.last().outline.boundingRect(), QRectF(70, 70, 20, 20)));
    const QByteArray data = document.data();
    QVERIFY(data.indexOf(id.toUtf8()) < data.indexOf("mentis-label"));  // the label stays on top
    QCOMPARE(document.shapeStyle(id), style);
    // Only the stroke: a gradient fill stays.
    const QByteArray gradient =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\"><defs><linearGradient id=\"g\"/></defs>"
        "<path id=\"p\" fill=\"url(#g)\" d=\"M0 0h5v5z\"/></svg>";
    QVERIFY(document.load(gradient));
    QCOMPARE(document.shapeStyle(QStringLiteral("p")).fill, QColor());  // not a plain colour
    document.setShapeStyle({QStringLiteral("p")}, {QColor(), QColor(Qt::blue), 0.5}, SvgDocument::Stroke | SvgDocument::StrokeWidth);
    QVERIFY(document.data().contains("fill=\"url(#g)\""));
    QVERIFY(document.data().contains("style=\"stroke:#0000ff;stroke-width:0.5\""));
    QCOMPARE(document.shapeStyle(QStringLiteral("p")).stroke, QColor(Qt::blue));
    // No style at all: black fill, no stroke, as SVG draws it.
    QVERIFY(document.load("<svg xmlns=\"http://www.w3.org/2000/svg\"><path id=\"q\" d=\"M0 0h5v5z\"/></svg>"));
    QCOMPARE(document.shapeStyle(QStringLiteral("q")), (SvgDocument::ShapeStyle{QColor(Qt::black), QColor(), 0}));
}

// The order of the whole drawing: a shape leaving its group keeps its place and its group's colour.
void SvgDocumentTest::orderAcrossGroups() {
    const QByteArray data =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\" width=\"200\" height=\"200\">"
        "<g transform=\"translate(10 0)\" fill=\"#ff0000\"><rect id=\"a\" x=\"10\" y=\"10\" width=\"40\" height=\"40\"/></g>"
        "<rect id=\"b\" x=\"40\" y=\"20\" width=\"40\" height=\"40\" fill=\"#0000ff\"/></svg>";
    auto colourAt = [](const QByteArray &svg, const QPointF &point) { return rendered(svg).pixelColor((point * 2).toPoint()); };
    const QPointF overlap(55, 30);  // a spans 20..60 (moved 10), b 40..80
    SvgDocument document;
    QVERIFY(document.load(data));
    QCOMPARE(colourAt(document.data(), overlap), QColor(Qt::blue));
    const QRectF aBefore = SvgDocument::bounds(document.drawnShapes(), {QStringLiteral("a")});
    document.reorderShapes({QStringLiteral("a")}, SvgDocument::Order::Front);
    QCOMPARE(ids(document.drawnShapes()), QStringList({QStringLiteral("b"), QStringLiteral("a")}));
    QCOMPARE(colourAt(document.data(), overlap), QColor(Qt::red));  // red still: the group's fill came along
    QVERIFY(sameRect(SvgDocument::bounds(document.drawnShapes(), {QStringLiteral("a")}), aBefore, 1e-6));
    document.reorderShapes({QStringLiteral("a")}, SvgDocument::Order::Backward);
    QCOMPARE(ids(document.drawnShapes()), QStringList({QStringLiteral("a"), QStringLiteral("b")}));
    QCOMPARE(colourAt(document.data(), overlap), QColor(Qt::blue));
    document.reorderShapes({QStringLiteral("b")}, SvgDocument::Order::Backward);  // into the first group
    QCOMPARE(ids(document.drawnShapes()), QStringList({QStringLiteral("b"), QStringLiteral("a")}));
    QCOMPARE(colourAt(document.data(), overlap), QColor(Qt::red));
    QVERIFY(sameRect(SvgDocument::bounds(document.drawnShapes(), {QStringLiteral("b")}), QRectF(40, 20, 40, 40), 1e-6));
    document.reorderShapes({QStringLiteral("b")}, SvgDocument::Order::Forward);
    QCOMPARE(ids(document.drawnShapes()), QStringList({QStringLiteral("a"), QStringLiteral("b")}));
    QCOMPARE(colourAt(document.data(), QPointF(45, 25)), QColor(Qt::blue));
}

QTEST_MAIN(SvgDocumentTest)
#include "svg_document_test.moc"
