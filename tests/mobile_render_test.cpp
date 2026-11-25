// FEAT-119 stage 0: the phone draws every style as the desktop. Each fixture is drawn by the desktop's
// scene (MindMapScene, QPainter) and by the phone's triangles (MapSnapshot, MapScene::appendShape and
// appendBranch, filled here with QPainter), then the two images are compared: every inked pixel of one
// must have an inked pixel of the other nearby (antialiasing and the triangles' straight segments
// differ by a pixel or so), and the solid areas must have the same colour. Titles are drawn
// transparent: the phone draws them with the scene graph's text nodes, not compared here.
//
// MENTIS_RENDER_DIR=<folder> writes the two images of each fixture there, to look at them.

#include "mapscene.h"
#include "mapsnapshot.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mapstructure.h"
#include "nodegeometry.h"
#include "nodestyle.h"
#include "numbering.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QTest>
#include <QTextLayout>

#include <functional>

Q_DECLARE_METATYPE(MapStructure::Type)
Q_DECLARE_METATYPE(MapStructure::Direction)

namespace {

constexpr int kNearPx = 1;             // how far the other image's paint may be
constexpr qreal kMaxMissRatio = 0.002; // painted pixels without a match in the other image
// Colour distances, the sum of the three channels: from the background, a painted pixel (pale fills
// included); between two pixels, the same solid colour.
constexpr int kPaintDistance = 12;
constexpr int kSameColour = 24;

int distance(QRgb a, QRgb b) { return qAbs(qRed(a) - qRed(b)) + qAbs(qGreen(a) - qGreen(b)) + qAbs(qBlue(a) - qBlue(b)); }

struct Comparison {
    qsizetype ink = 0, misses = 0, solid = 0, wrongColour = 0;
};

// Pixels painted in `a` without paint within kNearPx in `b`; solid pixels of `a` (its 3x3 neighbours
// alike) without a pixel of the same colour within 1 px in `b`.
void compareInto(Comparison &result, const QImage &a, const QImage &b, QRgb background) {
    const auto inked = [&](const QImage &image, int x, int y) { return distance(image.pixel(x, y), background) > kPaintDistance; };
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            if (!inked(a, x, y)) continue;
            ++result.ink;
            bool near = false;
            for (int dy = -kNearPx; dy <= kNearPx && !near; ++dy)
                for (int dx = -kNearPx; dx <= kNearPx && !near; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    near = nx >= 0 && ny >= 0 && nx < b.width() && ny < b.height() && inked(b, nx, ny);
                }
            if (!near) ++result.misses;
            if (x == 0 || y == 0 || x + 1 == a.width() || y + 1 == a.height()) continue;
            const QRgb colour = a.pixel(x, y);
            bool solid = true;
            for (int dy = -1; dy <= 1 && solid; ++dy)
                for (int dx = -1; dx <= 1 && solid; ++dx) solid = distance(a.pixel(x + dx, y + dy), colour) < 12;
            if (!solid) continue;
            ++result.solid;
            bool same = false;
            for (int dy = -1; dy <= 1 && !same; ++dy)
                for (int dx = -1; dx <= 1 && !same; ++dx) same = distance(b.pixel(x + dx, y + dy), colour) < kSameColour;
            if (!same) ++result.wrongColour;
        }
}

QImage phoneImage(const MapSnapshot &snapshot, const QRectF &area) {
    MapScene::Vertices vertices;
    for (const MapSnapshot::Branch &branch : snapshot.branches()) MapScene::appendBranch(vertices, branch);
    for (const MapSnapshot::Topic &topic : snapshot.topics()) {
        const QRectF shape = topic.geometry->shapeRect().translated(topic.position);
        if (!shape.isEmpty()) MapScene::appendShape(vertices, topic.node, shape, topic.geometry->cornerRadius(), snapshot.background());
    }
    QImage image(area.size().toSize(), QImage::Format_RGB32);
    image.fill(snapshot.background());
    QPainter painter(&image);
    painter.translate(-area.topLeft());
    painter.setPen(Qt::NoPen);
    // The vertices are premultiplied (QSGVertexColorMaterial): back to straight alpha for QPainter.
    for (qsizetype i = 0; i + 2 < vertices.size(); i += 3) {
        const auto &v = vertices.at(i);
        const int alpha = v.a;
        if (alpha == 0) continue;
        painter.setBrush(QColor(v.r * 255 / alpha, v.g * 255 / alpha, v.b * 255 / alpha, alpha));
        painter.drawPolygon(QPolygonF{QPointF(v.x, v.y), QPointF(vertices.at(i + 1).x, vertices.at(i + 1).y),
                                      QPointF(vertices.at(i + 2).x, vertices.at(i + 2).y)});
    }
    return image;
}

QImage desktopImage(MindMapScene &scene, const QRectF &area, const QColor &background) {
    QImage image(area.size().toSize(), QImage::Format_RGB32);
    image.fill(background);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    scene.render(&painter, QRectF(QPointF(), area.size()), area);
    return image;
}

// Titles transparent on every topic, so only shapes and lines are compared.
void hideTitles(MindNode *node) {
    node->style.text = QColor(Qt::transparent);
    for (const auto &child : node->children) hideTitles(child.get());
}

}  // namespace

class MobileRenderTest : public QObject {
    Q_OBJECT

private:
    void compare(MindMapModel &model, const QString &name) {
        hideTitles(model.root());
        model.refreshStyles();
        MindMapScene scene(&model);
        scene.autoLayout();
        const MapSnapshot snapshot(&model);
        QVERIFY(!snapshot.branches().isEmpty());
        const QRectF area = snapshot.bounds().adjusted(-20, -20, 20, 20).toAlignedRect();
        scene.setBackgroundBrush(snapshot.background());  // MainWindow::applyBackdrop
        const QImage desktop = desktopImage(scene, area, snapshot.background());
        const QImage phone = phoneImage(snapshot, area);
        if (const QString dir = qEnvironmentVariable("MENTIS_RENDER_DIR"); !dir.isEmpty()) {
            desktop.save(QDir(dir).filePath(name + QStringLiteral("-desktop.png")));
            phone.save(QDir(dir).filePath(name + QStringLiteral("-phone.png")));
        }
        Comparison both;
        compareInto(both, desktop, phone, snapshot.background().rgb());
        compareInto(both, phone, desktop, snapshot.background().rgb());
        QVERIFY2(both.ink > 0, qPrintable(name));
        const qreal missRatio = qreal(both.misses) / both.ink;
        const qreal colourRatio = both.solid ? qreal(both.wrongColour) / both.solid : 0;
        QVERIFY2(missRatio < kMaxMissRatio, qPrintable(QStringLiteral("%1: %2 of %3 inked pixels far from the other image's")
                                                         .arg(name).arg(both.misses).arg(both.ink)));
        QVERIFY2(colourRatio < kMaxMissRatio, qPrintable(QStringLiteral("%1: %2 of %3 solid pixels of another colour")
                                                           .arg(name).arg(both.wrongColour).arg(both.solid)));
    }

    // A mind map whose main topics take, in turn, every fill and border style, line pattern, branch
    // shape and end mark, and whose subtopics take shapes with inner lines, marks and underlines.
    static void styledMap(MindMapModel &model) {
        const QList<NodeFillStyle> fills{NodeFillStyle::Pieno, NodeFillStyle::PienoManoLibera, NodeFillStyle::PennellateDiagonali,
                                         NodeFillStyle::TratteggioDiagonale, NodeFillStyle::PennellateOrizzontali,
                                         NodeFillStyle::TratteggioOrizzontale, NodeFillStyle::Nessuno};
        const QList<NodeBorderStyle> borders{NodeBorderStyle::Continuo, NodeBorderStyle::Tratteggiato, NodeBorderStyle::ContinuoManoLibera,
                                             NodeBorderStyle::TratteggiatoManoLibera, NodeBorderStyle::Nessuno};
        const QList<Qt::PenStyle> lines{Qt::SolidLine, Qt::DashLine, Qt::DotLine, NodeStyle::kFreehandSolidLine,
                                        NodeStyle::kFreehandDashLine, Qt::SolidLine, Qt::DashLine, Qt::SolidLine};
        const QList<BranchShape> shapes{BranchShape::Auto, BranchShape::RoundedBracket, BranchShape::Bracket, BranchShape::Straight,
                                        BranchShape::Curve, BranchShape::SCurve, BranchShape::Angled, BranchShape::RoundedAngled};
        const QList<BranchEnd> ends{BranchEnd::None, BranchEnd::OpenArrow, BranchEnd::FilledArrow, BranchEnd::Dot, BranchEnd::Square,
                                    BranchEnd::Diamond};
        const QList<NodeShape> subShapes{NodeShape::Nota, NodeShape::PilaDiSchede, NodeShape::Database, NodeShape::ParentesiGraffe,
                                         NodeShape::DoppiaLineaSotto, NodeShape::Documento, NodeShape::Angoli, NodeShape::Connettore,
                                         NodeShape::CerchioDoppio, NodeShape::Virgolette, NodeShape::SchedaConBordoLaterale,
                                         NodeShape::ProcessoPredefinito, NodeShape::Stella, NodeShape::Cuore, NodeShape::LineaSotto,
                                         NodeShape::DocumentiMultipli};
        model.root()->title = QStringLiteral("Centre");
        model.root()->style.branchStart = BranchEnd::Dot;
        model.root()->style.branchEnd = BranchEnd::FilledArrow;
        for (int i = 0; i < 8; ++i) {
            MindNode *main = model.addChild(model.root(), QStringLiteral("Main topic %1").arg(i + 1));
            main->style.fillStyle = fills.at(i % fills.size());
            main->style.borderStyle = borders.at(i % borders.size());
            main->style.borderWidth = 2;
            main->style.branchStyle = lines.at(i);
            main->style.branchShape = shapes.at(i);
            main->style.branchStart = ends.at(i % ends.size());
            main->style.branchEnd = ends.at((i + 2) % ends.size());
            main->style.branchWidth = 1 + i % 3;
            for (int j = 0; j < 2; ++j) {
                MindNode *sub = model.addChild(main, QStringLiteral("Subtopic %1").arg(j + 1));
                sub->style.shape = subShapes.at((2 * i + j) % subShapes.size());
                sub->style.fillStyle = fills.at((i + j + 2) % fills.size());
                sub->style.borderStyle = borders.at((i + j + 1) % borders.size());
                sub->style.borderWidth = 2;
            }
        }
    }

    static void structureMap(MindMapModel &model, MapStructure::Type type, MapStructure::Direction direction) {
        model.root()->title = QStringLiteral("Centre");
        model.root()->structure = MapStructure::typeKey(type);
        model.root()->structureDirection = MapStructure::directionKey(direction);
        for (int i = 0; i < 4; ++i) {
            MindNode *main = model.addChild(model.root(), QStringLiteral("Main topic %1").arg(i + 1));
            main->style.branchEnd = BranchEnd::FilledArrow;
            for (int j = 0; j < 2; ++j) model.addChild(main, QStringLiteral("Subtopic %1").arg(j + 1));
        }
    }

private slots:
    void styles() {
        MindMapModel model;
        styledMap(model);
        compare(model, QStringLiteral("styles"));
    }

    void tapered() {
        MindMapModel model;
        styledMap(model);
        model.sheetStyle.tapered = true;
        compare(model, QStringLiteral("tapered"));
    }

    void variantLines_data() {
        QTest::addColumn<QString>("variant");
        QTest::newRow("angled") << QStringLiteral("mindmap-4");
        QTest::newRow("square") << QStringLiteral("mindmap-5");
        QTest::newRow("straight") << QStringLiteral("mindmap-9");
    }
    void variantLines() {
        QFETCH(QString, variant);
        MindMapModel model;
        structureMap(model, MapStructure::Type::MindMap, MapStructure::Direction::BothSides);
        model.sheetStyle.variant = variant;
        compare(model, variant);
    }

    void structures_data() {
        using MapStructure::Direction;
        using MapStructure::Type;
        QTest::addColumn<Type>("type");
        QTest::addColumn<Direction>("direction");
        QTest::newRow("logic") << Type::LogicChart << Direction::Right;
        QTest::newRow("org") << Type::OrgChart << Direction::Down;
        QTest::newRow("tree") << Type::TreeChart << Direction::Right;
        QTest::newRow("brace") << Type::Brace << Direction::Right;
        QTest::newRow("timeline") << Type::Timeline << Direction::Right;
        QTest::newRow("timeline-off-axis") << Type::Timeline << Direction::OffAxis;
        QTest::newRow("fishbone") << Type::Fishbone << Direction::Right;
    }
    // FEAT-024 on the phone: the number is part of the measured title (NodeGeometry::shownTitle), as the
    // desktop draws it.
    void numbering() {
        MindMapModel model;
        BranchNumbering numbering;
        numbering.format = QStringLiteral("1.2");
        model.root()->style.numbering = numbering;
        MindNode *main = model.addChild(model.root(), QStringLiteral("Main"));
        MindNode *sub = model.addChild(main, QStringLiteral("Sub"));
        model.refreshStyles();
        const MapSnapshot snapshot(&model);
        for (const auto &[node, shown] : {std::pair{main, QStringLiteral("1 Main")}, std::pair{sub, QStringLiteral("1.1 Sub")}}) {
            const MapSnapshot::Topic *topic = snapshot.topicOf(node);
            QVERIFY(topic && topic->geometry->plainLayout());
            QCOMPARE(topic->geometry->plainLayout()->text(), shown);
        }
    }

    void structures() {
        QFETCH(MapStructure::Type, type);
        QFETCH(MapStructure::Direction, direction);
        MindMapModel model;
        structureMap(model, type, direction);
        compare(model, QString::fromLatin1(QTest::currentDataTag()));
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    MobileRenderTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "mobile_render_test.moc"
