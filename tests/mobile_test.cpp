// FEAT-097 stage A: the mobile app's adapters and pages on the desktop, without Qt Widgets. The
// local copy of a chosen map (MapLibrary), the open map and its sheets (MobileMap), the drawn map's
// positions against the desktop's (MapSnapshot, the FEAT-096 fixtures) and the QML pages loaded
// with the software renderer. Stage B1: edits through Document, their Undo steps and the saved file.
#include "document.h"
#include "formulatext.h"
#include "iconlibrary.h"
#include "markers.h"
#include "layout_fixtures.h"
#include "mapcanvas.h"
#include "shortcutcatalog.h"
#include "shortcuts.h"
#include "mapscene.h"
#include "incomingmaps.h"
#include "mapfolder.h"
#include "maplibrary.h"
#include "mapsnapshot.h"
#include "mobileapp.h"
#include "mobilemap.h"
#include "nodegeometry.h"
#include "nodesearch.h"
#include "nodeshape.h"
#include "performance.h"
#include "stickerlibrary.h"
#include "stickers.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFontMetricsF>
#include <QClipboard>
#include <QGuiApplication>
#include <QStyleHints>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSGGeometryNode>
#include <QSGOpacityNode>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextDocument>
#include <QQuickTextDocument>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Mentis_MobilePlugin)

using LayoutFixtures::Fixture;

namespace {

constexpr qreal kTolerancePx = 0.01;

// A map with a sheet of three topics and a second sheet of two, laid out as the desktop saves it
// (the snapshot writes the positions, as MindMapScene::placeBranch).
QString writeTwoSheetMap(const QString &path) {
    Document document;
    document.startUntitled();
    MindNode *root = document.model()->root();
    document.addChild(root, QStringLiteral("First"));
    document.addChild(root, QStringLiteral("Second"));
    { const MapSnapshot laidOut(document.model()); }
    document.addSheet();
    document.addChild(document.model()->root(), QStringLiteral("Other sheet"));
    { const MapSnapshot laidOut(document.model()); }
    const Document::Result saved = document.save(path, BmapStorage::SaveReason::Explicit);
    document.close();
    return saved.ok ? QString() : saved.error;
}

}  // namespace

class MobileTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void noWidgets();
    void localCopyOfAChosenMap();
    void damagedMapLeavesNoCopy();
    void opensAndSwitchesSheet();
    void snapshotMatchesTheDesktop();
    void collapsedBranchesAreHidden();
    void sceneFillsShapes();
    void sceneStrokesLines();
    void largeGeometryIsSplit();
    void canvasFitsAndZooms();
    void editsAreSavedAndUndone();
    void drawingIsNotAnEdit();
    void newTopicAndTitleAreOneStep();
    void collapseAndDelete();
    void undoInAnotherSheet();
    void tapSelectsATopic();
    void stickerIsInsertedAndSaved();
    void topicIsMovedByLongPress();
    void formatPainterPastesTheStyle();
    void relationshipsOnThePhone();  // FEAT-110
    void relationshipsAreDrawn();    // FEAT-110
    void pickedImageIsInsertedAndEmbedded();
    void markersAreGivenAndTakenOff();
    void filesAreAddedToTheLibrary();
    void keyboardCommands();
    void notesAndRichTitles();
    void checkboxAndWebLink();
    void topicContentsAreRemovedAndPlaced();
    void markerMenuOnThePage();
    void pagesLoadAndOpenAMap();
    void mapFromAnotherAppIsOpened();
    void folderLevelsInTheLibrary();
    void homeFoldersOnThePage();
    void multipleSelection();        // FEAT-111
    void multipleSelectionOnThePage();  // FEAT-111
    void sheetCommands();            // FEAT-112
    void sheetCommandsOnThePage();   // FEAT-112
    void searchFindsAndReveals();    // FEAT-113
    void searchOnThePage();          // FEAT-113
    void labelsOnThePage();          // FEAT-114
    void filesOnThePage();           // FEAT-114
    void topicLinksOnThePage();      // FEAT-114
    void boundariesOnThePhone();     // FEAT-115
    void boundariesOnThePage();      // FEAT-115
    void summariesOnThePhone();      // FEAT-115 step 2
    void summariesOnThePage();       // FEAT-115 step 2
    void formulasOnThePhone();       // FEAT-116
    void formulasOnThePage();        // FEAT-116

private:
    QFont m_defaultFont;  // Qt's, before MobileApp::prepare sets the interface font
};

void MobileTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisMobileTest"));
    Performance::setMode(Performance::Mode::Standard);
    m_defaultFont = QGuiApplication::font();
    MobileApp::prepareSurfaceFormat();  // read when the window is made, as in the app
    MobileApp::prepare();
}

void MobileTest::noWidgets() {
    QVERIFY(!QCoreApplication::instance()->inherits("QApplication"));
}

void MobileTest::localCopyOfAChosenMap() {
    QTemporaryDir source, storage;
    QVERIFY(source.isValid() && storage.isValid());
    const QString original = source.filePath(QStringLiteral("Plan.mentis"));
    QCOMPARE(writeTwoSheetMap(original), QString());
    MapLibrary library(storage.path(), nullptr);
    QVERIFY(library.maps().isEmpty());

    bool created = false;
    const QString copy = library.localCopy(QUrl::fromLocalFile(original), &created);
    QVERIFY2(!copy.isEmpty(), qPrintable(library.lastError()));
    QVERIFY(created);
    QCOMPARE(QFileInfo(copy).fileName(), QStringLiteral("Plan.mentis"));
    QCOMPARE(QFileInfo(copy).absolutePath(), QFileInfo(storage.path()).absoluteFilePath());
    QCOMPARE(library.maps().size(), 1);
    QCOMPARE(library.maps().first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Plan"));
    QVERIFY(!QFileInfo::exists(copy + QStringLiteral(".part")));

    // The same bytes chosen again: the same copy.
    QCOMPARE(library.localCopy(QUrl::fromLocalFile(original), &created), copy);
    QVERIFY(!created);
    // A map of the storage itself opens where it is.
    QCOMPARE(library.localCopy(QUrl::fromLocalFile(copy), &created), copy);
    QVERIFY(!created);

    // Different bytes under the same name get a free name; the first copy is untouched.
    QFile changed(original);
    QVERIFY(changed.open(QIODevice::Append));
    changed.write("x");
    changed.close();
    const QString second = library.localCopy(QUrl::fromLocalFile(original), &created);
    QVERIFY(created);
    QCOMPARE(QFileInfo(second).fileName(), QStringLiteral("Plan (2).mentis"));
    QCOMPARE(library.maps().size(), 2);

    // A copy cut short by a killed app is removed when the library starts again.
    QFile partial(QDir(storage.path()).filePath(QStringLiteral("Cut.mentis.part")));
    QVERIFY(partial.open(QIODevice::WriteOnly));
    partial.close();
    QCOMPARE(MapLibrary(storage.path(), nullptr).maps().size(), 2);
    QVERIFY(!partial.exists());

    // Missing file: an error, no copy.
    QVERIFY(library.localCopy(QUrl::fromLocalFile(source.filePath(QStringLiteral("missing.mentis"))), &created).isEmpty());
    QVERIFY(!created);
    QVERIFY(!library.lastError().isEmpty());
    QCOMPARE(library.maps().size(), 2);
    // Only the storage's own files are removed.
    QVERIFY(!library.discard(original));
    QVERIFY(QFileInfo::exists(original));
    QVERIFY(library.discard(second));
    QCOMPARE(library.maps().size(), 1);
}

void MobileTest::damagedMapLeavesNoCopy() {
    QTemporaryDir source, storage;
    const QString damaged = source.filePath(QStringLiteral("Broken.mentis"));
    QFile file(damaged);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not a database");
    file.close();
    MapLibrary library(storage.path(), nullptr);
    MobileMap map;
    QVERIFY(!map.openUrl(QUrl::fromLocalFile(damaged), &library));
    QVERIFY(!map.lastError().isEmpty());
    QVERIFY(!map.isOpen());
    QVERIFY(library.maps().isEmpty());
    QVERIFY(QFileInfo::exists(damaged));  // the original is never touched
}

void MobileTest::opensAndSwitchesSheet() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Sheets.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    const QByteArray before = [&] { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }();

    MobileMap map;
    QSignalSpy snapshots(&map, &MobileMap::snapshotChanged);
    QVERIFY2(map.open(path), qPrintable(map.lastError()));
    QVERIFY(map.isOpen());
    QCOMPARE(map.title(), QStringLiteral("Sheets"));
    QCOMPARE(map.sheets().size(), 2);
    QCOMPARE(map.currentSheet(), 0);
    QCOMPARE(map.topicCount(), 3);
    QVERIFY(map.openMs() >= 0);
    QVERIFY(!snapshots.isEmpty());

    QVERIFY(map.switchSheet(1));
    QCOMPARE(map.currentSheet(), 1);
    QCOMPARE(map.topicCount(), 2);
    QVERIFY(!map.switchSheet(5));
    QCOMPARE(map.currentSheet(), 1);
    QCOMPARE(map.topicCount(), 2);
    QVERIFY(map.switchSheet(0));
    QCOMPARE(map.topicCount(), 3);

    map.close();
    QVERIFY(!map.isOpen());
    QCOMPARE(map.topicCount(), 0);
    // Read only: the file has the same bytes after open, sheet switches and close.
    QFile after(path);
    QVERIFY(after.open(QIODevice::ReadOnly));
    QCOMPARE(after.readAll(), before);
}

void MobileTest::snapshotMatchesTheDesktop() {
    // The positions the desktop scene gives, recorded by tests/layout_regression_test.cpp under
    // "plain-<map>" (as tests/domain_layout_test.cpp reads them); another platform or Qt skips.
    QFile file(QStringLiteral(MENTIS_TEST_DATA_DIR "/layout_regression/%1-qt%2.json").arg(QSysInfo::kernelType(), QString::fromLatin1(qVersion())));
    if (!file.exists()) QSKIP(qPrintable(QStringLiteral("No recorded layout at %1").arg(file.fileName())));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject recorded = QJsonDocument::fromJson(file.readAll()).object();

    // The recording was made with Qt's default application font, which the shown notes are measured
    // with (NodeGeometry::updateNotePreview); the app sets Atkinson Hyperlegible, as the desktop does.
    const QFont appFont = QGuiApplication::font();
    QGuiApplication::setFont(m_defaultFont);
    const auto restoreFont = qScopeGuard([&] { QGuiApplication::setFont(appFont); });
    QStringList differences;
    int maps = 0;
    for (const Fixture &fixture : LayoutFixtures::all()) {
        if (fixture.content == Fixture::Content::Justify) continue;  // Justify is not drawn by the prototype
        const QJsonObject expected = recorded.value(QStringLiteral("plain-") + fixture.name).toObject();
        QVERIFY2(!expected.isEmpty(), qPrintable(fixture.name + QStringLiteral(": not recorded")));
        Document document;
        LayoutFixtures::build(document, fixture, nullptr);
        const MapSnapshot snapshot(document.model());
        QVERIFY(!snapshot.topics().isEmpty());
        for (const MapSnapshot::Topic &topic : snapshot.topics()) {
            const QString key = LayoutFixtures::keyOf(topic.node);
            const QJsonArray at = expected.value(key).toArray();
            if (at.size() != 2) { differences.append(QStringLiteral("%1: %2 not shown by the desktop").arg(fixture.name, key)); continue; }
            const QPointF desktop(at[0].toDouble(), at[1].toDouble());
            if (QLineF(topic.position, desktop).length() > kTolerancePx)
                differences.append(QStringLiteral("%1: %2 at (%3, %4), the desktop at (%5, %6)").arg(fixture.name, key)
                                       .arg(topic.position.x()).arg(topic.position.y()).arg(desktop.x()).arg(desktop.y()));
        }
        document.close();
        NodeGeometry::showAllNotes() = false;
        ++maps;
    }
    QCOMPARE(maps, 24);
    QVERIFY2(differences.isEmpty(), qPrintable(differences.mid(0, 20).join(QLatin1Char('\n'))));
}

void MobileTest::collapsedBranchesAreHidden() {
    Document document;
    document.startUntitled();
    MindMapModel &model = *document.model();
    MindNode *main = model.addChild(model.root(), QStringLiteral("Main"));
    model.addChild(main, QStringLiteral("Child"));
    model.addChild(model.root(), QStringLiteral("Other"));
    {
        const MapSnapshot open(&model);
        QCOMPARE(open.topics().size(), 4);
        QCOMPARE(open.branches().size(), 3);
    }
    main->collapsed = true;
    const MapSnapshot collapsed(&model);
    QCOMPARE(collapsed.topics().size(), 3);
    QCOMPARE(collapsed.branches().size(), 2);
    QVERIFY(!collapsed.bounds().isEmpty());
    document.close();
}

namespace {

qreal trianglesArea(const MapScene::Vertices &vertices) {
    qreal area = 0;
    for (qsizetype i = 0; i + 2 < vertices.size(); i += 3) {
        const QPointF a(vertices[i].x, vertices[i].y), b(vertices[i + 1].x, vertices[i + 1].y), c(vertices[i + 2].x, vertices[i + 2].y);
        area += std::abs((b.x() - a.x()) * (c.y() - a.y()) - (c.x() - a.x()) * (b.y() - a.y())) / 2;
    }
    return area;
}

qreal polygonArea(const QPolygonF &polygon) {
    qreal area = 0;
    for (qsizetype i = 0; i < polygon.size(); ++i) {
        const QPointF a = polygon.at(i), b = polygon.at((i + 1) % polygon.size());
        area += a.x() * b.y() - b.x() * a.y();
    }
    return std::abs(area) / 2;
}

}  // namespace

void MobileTest::sceneFillsShapes() {
    // The triangles cover each shape once: their areas add up to the outline's, convex or not.
    const QRectF outer(10, 20, 160, 48);
    for (const NodeShape shape : {NodeShape::RettangoloArrotondato, NodeShape::Ellisse, NodeShape::Stella, NodeShape::Cuore,
                                  NodeShape::Nuvola, NodeShape::FrecciaDestra, NodeShape::Esagono}) {
        const QList<QPolygonF> polygons = MapScene::fillPolygons(NodeShapes::outline(shape, outer));
        QVERIFY(!polygons.isEmpty());
        qreal expected = 0;
        MapScene::Vertices vertices;
        for (const QPolygonF &polygon : polygons) {
            expected += polygonArea(polygon);
            MapScene::appendFill(vertices, polygon, Qt::red);
        }
        QVERIFY2(std::abs(trianglesArea(vertices) - expected) < expected * 1e-3,
                 qPrintable(QStringLiteral("shape %1: %2 against %3").arg(int(shape)).arg(trianglesArea(vertices)).arg(expected)));
        // Premultiplied colour, as QSGVertexColorMaterial expects.
        QCOMPARE(vertices.constFirst().r, uchar(255));
    }
    MapScene::Vertices half;
    MapScene::appendFill(half, QPolygonF{QPointF(0, 0), QPointF(10, 0), QPointF(0, 10)}, QColor(255, 0, 0, 128));
    QCOMPARE(half.constFirst().r, uchar(128));
    QCOMPARE(half.constFirst().a, uchar(128));
}

// The Fairphone 6 drew no line past the 65,536th vertex of one node (16-bit batch indices).
void MobileTest::largeGeometryIsSplit() {
    MapScene::Vertices vertices;
    const int total = 2 * MapScene::kMaxNodeVertices + 300;
    for (int i = 0; i < total; ++i) {
        QSGGeometry::ColoredPoint2D v;
        v.set(float(i), 0, 0, 0, 0, 255);
        vertices.append(v);
    }
    const QList<QSGGeometryNode *> nodes = MapScene::geometryNodes(vertices);
    const auto cleanup = qScopeGuard([&] { qDeleteAll(nodes); });
    QCOMPARE(nodes.size(), 3);
    QCOMPARE(MapScene::kMaxNodeVertices % 3, 0);
    int next = 0;
    for (const QSGGeometryNode *node : nodes) {
        const QSGGeometry *geometry = node->geometry();
        QVERIFY(geometry->vertexCount() <= MapScene::kMaxNodeVertices);
        QCOMPARE(geometry->vertexCount() % 3, 0);
        const QSGGeometry::ColoredPoint2D *data = geometry->vertexDataAsColoredPoint2D();
        for (int i = 0; i < geometry->vertexCount(); ++i, ++next) QCOMPARE(int(data[i].x), next);
    }
    QCOMPARE(next, total);
    QVERIFY(MapScene::geometryNodes({}).isEmpty());
}

void MobileTest::sceneStrokesLines() {
    // A straight line: a band `width` wide along it, with two half circles for the round caps.
    MapScene::Vertices straight;
    MapScene::appendStroke(straight, QPolygonF{QPointF(0, 0), QPointF(100, 0)}, 4, false, false, Qt::black);
    QCOMPARE(straight.size(), 6);
    QVERIFY(std::abs(trianglesArea(straight) - 400) < 1e-3);
    MapScene::Vertices capped;
    MapScene::appendStroke(capped, QPolygonF{QPointF(0, 0), QPointF(100, 0)}, 4, false, true, Qt::black);
    const qreal caps = 16 * 0.5 * 2 * 2 * std::sin(std::numbers::pi / 8);  // two half 16-gons of radius 2
    QVERIFY(std::abs(trianglesArea(capped) - 400 - caps) < 1e-3);
    // A closed square border 2 wide: outer 12x12 minus inner 8x8, mitred corners.
    MapScene::Vertices square;
    MapScene::appendStroke(square, QPolygonF{QPointF(0, 0), QPointF(10, 0), QPointF(10, 10), QPointF(0, 10), QPointF(0, 0)}, 2, true,
                           false, Qt::black);
    QVERIFY(std::abs(trianglesArea(square) - (144 - 64)) < 1e-3);
    // A branch curve flattened finely enough for 400%: no chord far from the curve.
    QPainterPath curve(QPointF(0, 0));
    curve.cubicTo(QPointF(100, 0), QPointF(0, 100), QPointF(100, 100));
    const QList<QPolygonF> lines = MapScene::strokePolylines(curve);
    QCOMPARE(lines.size(), 1);
    QVERIFY(lines.constFirst().size() > 16);
}

void MobileTest::canvasFitsAndZooms() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Fit.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MapCanvas canvas;
    canvas.setSize(QSizeF(400, 800));
    canvas.setMap(&map);
    // Fit: the whole map inside the margins, at most 100%.
    const QRectF bounds = map.snapshot()->bounds();
    QVERIFY(canvas.zoom() > 0 && canvas.zoom() <= 1);
    const QRectF shown(canvas.itemPoint(bounds.topLeft()), canvas.itemPoint(bounds.bottomRight()));
    QVERIFY(QRectF(0, 0, 400, 800).adjusted(MapCanvas::kFitMarginPx - 0.5, MapCanvas::kFitMarginPx - 0.5,
                                            0.5 - MapCanvas::kFitMarginPx, 0.5 - MapCanvas::kFitMarginPx).contains(shown));
    // A pinch keeps the map point under the fingers.
    const QPointF fingers(120, 300);
    const QPointF under = canvas.mapPoint(fingers);
    canvas.zoomBy(2, fingers.x(), fingers.y());
    QVERIFY(QLineF(canvas.mapPoint(fingers), under).length() < 1e-6);
    // Limits: the desktop's 400% at most.
    canvas.zoomBy(100, 0, 0);
    QCOMPARE(canvas.zoom(), MapCanvas::kMaxZoom);
    // A pan moves the map with the finger.
    const QPointF before = canvas.itemPoint(under);
    canvas.panBy(30, -10);
    QVERIFY(QLineF(canvas.itemPoint(under), before + QPointF(30, -10)).length() < 1e-6);
    canvas.fit();
    QVERIFY(canvas.zoom() <= 1);
    map.close();
}

namespace {

const MindNode *childTitled(const MindNode *parent, const QString &title) {
    for (const auto &child : parent->children)
        if (child->title == title) return child.get();
    return nullptr;
}

}  // namespace

void MobileTest::editsAreSavedAndUndone() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Edits.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    QSignalSpy fits(&map, &MobileMap::snapshotChanged);
    const MindNode *root = map.document()->model()->root();
    map.select(root->id);
    QVERIFY(map.selectedIsCentral());
    QVERIFY(!map.removeSelected());  // the central topic stays
    QVERIFY(!map.canCollapse());

    QVERIFY(map.addChild());
    QCOMPARE(map.topicCount(), 4);
    const QString created = map.selectedId();
    QVERIFY(!created.isEmpty() && created != root->id);
    QVERIFY(map.rename(QStringLiteral("Typed")));
    QCOMPARE(map.selectedTitle(), QStringLiteral("Typed"));
    QVERIFY(!map.rename(QStringLiteral("Typed")));  // the same title is no edit
    QVERIFY(map.isDirty());
    QVERIFY(map.canUndo());
    QVERIFY(fits.isEmpty());  // an edit keeps the view: no new sheet
    // The model holds the drawn positions (MindMapScene::placeBranch), so the step and the file do.
    QHash<QString, QPointF> drawn;
    for (const MapSnapshot::Topic &topic : map.snapshot()->topics()) {
        QCOMPARE(topic.node->position, topic.position);
        drawn.insert(topic.node->id, topic.position);
    }

    // One Undo per edit: the title, then the new topic with its layout.
    QVERIFY(map.undo());
    QCOMPARE(map.selectedId(), created);
    QVERIFY(map.selectedTitle() != QStringLiteral("Typed"));
    QVERIFY(map.undo());
    QCOMPARE(map.topicCount(), 3);
    QVERIFY(map.selectedId().isEmpty());  // the selected topic is gone
    QVERIFY(map.redo());
    QVERIFY(map.redo());
    QCOMPARE(map.topicCount(), 4);
    for (const MapSnapshot::Topic &topic : map.snapshot()->topics()) QCOMPARE(topic.position, drawn.value(topic.node->id));

    // Close writes the changes; the file holds the topic where it was drawn.
    QVERIFY(map.close());
    Document document;
    QVERIFY(document.open(path, false).ok);
    const MindNode *typed = childTitled(document.model()->root(), QStringLiteral("Typed"));
    QVERIFY(typed);
    QCOMPARE(typed->id, created);
    QCOMPARE(typed->position, drawn.value(created));
    document.close();
}

void MobileTest::drawingIsNotAnEdit() {
    // A file whose stored positions differ from the phone's layout (here: never laid out; on a
    // phone, text measured with other fonts). Drawing it is no edit: no step, nothing written.
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Stale.mentis"));
    {
        Document document;
        document.startUntitled();
        document.addChild(document.model()->root(), QStringLiteral("First"));
        document.addSheet();
        document.addChild(document.model()->root(), QStringLiteral("Other sheet"));
        QVERIFY(document.save(path, BmapStorage::SaveReason::Explicit).ok);
        document.close();
    }
    const QByteArray before = [&] { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }();
    MobileMap map;
    QVERIFY(map.open(path));
    QVERIFY(map.switchSheet(1));
    QVERIFY(map.switchSheet(0));
    QVERIFY(!map.isDirty());
    QVERIFY(!map.canUndo());
    QVERIFY(map.save());
    QVERIFY(map.close());
    QFile after(path);
    QVERIFY(after.open(QIODevice::ReadOnly));
    QCOMPARE(after.readAll(), before);
}

void MobileTest::newTopicAndTitleAreOneStep() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Title.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    map.select(map.document()->model()->root()->id);
    map.setEditingTitle(true);  // as the page, before the add
    QVERIFY(map.addChild());
    QTest::qWait(300);  // past the history step's delay: the step stays open while the title is asked
    QVERIFY(map.rename(QStringLiteral("Named")));
    map.setEditingTitle(false);
    QVERIFY(map.undo());
    QCOMPARE(map.topicCount(), 3);  // one Undo takes back the topic with its title
    QVERIFY(map.redo());
    QCOMPARE(map.selectedTitle(), QString());  // the redone topic is not selected
    QVERIFY(childTitled(map.document()->model()->root(), QStringLiteral("Named")));
    // A failed close keeps the map open; closing without saving leaves the file as saved last.
    QVERIFY(map.save());
    map.closeWithoutSaving();
    QVERIFY(!map.isOpen());
    Document document;
    QVERIFY(document.open(path, false).ok);
    QVERIFY(childTitled(document.model()->root(), QStringLiteral("Named")));
    document.close();
}

void MobileTest::collapseAndDelete() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Collapse.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    const QString first = childTitled(map.document()->model()->root(), QStringLiteral("First"))->id;
    map.select(first);
    QVERIFY(!map.canCollapse());  // no branches yet
    QVERIFY(map.addChild());
    const QString child = map.selectedId();
    map.select(first);
    QVERIFY(map.canCollapse());
    QVERIFY(map.toggleCollapsed());
    QVERIFY(map.selectedCollapsed());
    QCOMPARE(map.topicCount(), 3);
    // A child added under a collapsed topic opens its branch.
    QVERIFY(map.addChild());
    QCOMPARE(map.topicCount(), 5);
    map.select(first);
    QVERIFY(!map.selectedCollapsed());
    // Delete selects the parent.
    map.select(child);
    QVERIFY(map.removeSelected());
    QCOMPARE(map.selectedId(), first);
    QCOMPARE(map.topicCount(), 4);
    QVERIFY(map.undo());
    QCOMPARE(map.topicCount(), 5);
    QVERIFY(map.close());
}

void MobileTest::undoInAnotherSheet() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Other.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    map.select(map.document()->model()->root()->id);
    QVERIFY(map.addChild());
    QVERIFY(map.switchSheet(1));
    QCOMPARE(map.topicCount(), 2);
    // As the desktop: the step stays in its sheet and the user is told which one.
    QVERIFY(!map.undo());
    QCOMPARE(map.topicCount(), 2);
    QVERIFY(map.message().contains(map.sheets().at(0)));
    QVERIFY(map.switchSheet(0));
    QVERIFY(map.undo());
    QCOMPARE(map.topicCount(), 3);
    QVERIFY(map.message().isEmpty());
    QVERIFY(map.close());
}

void MobileTest::tapSelectsATopic() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Tap.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MapCanvas canvas;
    canvas.setSize(QSizeF(400, 800));
    canvas.setMap(&map);
    const MapSnapshot::Topic *second = map.snapshot()->topicOf(childTitled(map.document()->model()->root(), QStringLiteral("Second")));
    QVERIFY(second);
    const QPointF at = canvas.itemPoint(second->bounds.center());
    canvas.tapAt(at.x(), at.y());
    QCOMPARE(map.selectedId(), second->node->id);
    QCOMPARE(map.selectedTopic(), second);
    canvas.tapAt(1, 1);  // the background, outside the fitted map
    QVERIFY(map.selectedId().isEmpty());
    // A topic added off the view is brought into it.
    map.select(second->node->id);
    canvas.zoomBy(1.5, 0, 0);
    canvas.panBy(-5000, 0);
    QVERIFY(map.addChild());
    const QRectF shown(canvas.itemPoint(map.selectedTopic()->bounds.topLeft()), canvas.itemPoint(map.selectedTopic()->bounds.bottomRight()));
    QVERIFY(QRectF(0, 0, 400, 800).contains(shown));
    // Editing its title: the topic in the middle of the band above the keyboard, readable.
    canvas.fit();
    canvas.setBandTop(200);
    canvas.setBandBottom(500);
    canvas.centerSelected();
    QVERIFY(QLineF(canvas.itemPoint(map.selectedTopic()->bounds.center()), QPointF(200, 350)).length() < 1e-6);
    QCOMPARE(canvas.zoom(), MapCanvas::kEditZoom);
    // The field sits on the topic's text, at the left end for a left-aligned title.
    const MapSnapshot::Topic *edited = map.selectedTopic();
    const QRectF text = edited->geometry->textArea().translated(edited->position);
    QVERIFY(QLineF(canvas.editorAnchor(), canvas.itemPoint(QPointF(text.left() + edited->geometry->padX(), text.center().y()))).length() < 1e-6);
    QCOMPARE(canvas.editorWidth(), edited->geometry->plainLayoutSize().width());
    // A topic added while the keyboard covers the bottom is revealed inside the band.
    canvas.panBy(0, -280);  // the topic above the band, under the top groups
    QVERIFY(map.addSibling());
    const QRectF revealed(canvas.itemPoint(map.selectedTopic()->bounds.topLeft()), canvas.itemPoint(map.selectedTopic()->bounds.bottomRight()));
    QVERIFY(revealed.top() >= 200 && revealed.bottom() <= 500);
    // A band too low for the topic at 100% zooms out until it fits.
    const qreal tight = map.selectedTopic()->bounds.height() / 2 + 2 * MapCanvas::kFitMarginPx;
    canvas.setBandTop(500);
    canvas.setBandBottom(500 + tight);
    canvas.centerSelected();
    QVERIFY(canvas.zoom() < MapCanvas::kEditZoom);
    QVERIFY(qAbs(map.selectedTopic()->bounds.height() * canvas.zoom() - (tight - 2 * MapCanvas::kFitMarginPx)) < 1e-6);
    QVERIFY(map.close());
}

// B2: a topic picked up by a long press and dropped on another becomes its last child, with its
// layout in one Undo step (the desktop's rules, ReorderTarget); drops without effect change nothing.
void MobileTest::topicIsMovedByLongPress() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Move.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MapCanvas canvas;
    canvas.setSize(QSizeF(400, 800));
    canvas.setMap(&map);
    MindNode *root = map.document()->model()->root();
    const MindNode *first = childTitled(root, QStringLiteral("First"));
    const MindNode *second = childTitled(root, QStringLiteral("Second"));
    QVERIFY(first && second);
    const auto centreOf = [&](const MindNode *node) { return canvas.itemPoint(map.snapshot()->topicOf(node)->bounds.center()); };
    const QString secondId = second->id;

    // The central topic and the background are not picked up; the central topic is selected.
    QPointF at = centreOf(root);
    QVERIFY(!canvas.beginMove(at.x(), at.y()));
    QCOMPARE(map.selectedId(), root->id);
    QVERIFY(!canvas.beginMove(1, 1));
    QVERIFY(!canvas.moving());

    // Lifted where it was taken: no step, nothing drawn again.
    QSignalSpy content(&map, &MobileMap::contentChanged);
    at = centreOf(second);
    QVERIFY(canvas.beginMove(at.x(), at.y()));
    QVERIFY(canvas.moving());
    QCOMPARE(map.selectedId(), secondId);
    QCOMPARE(map.moveTarget(secondId, canvas.mapPoint(at)).kind, ReorderTarget::Kind::None);
    canvas.endMove(true);
    QVERIFY(!canvas.moving());
    QVERIFY(content.isEmpty());
    QVERIFY(!map.canUndo());
    // On the background: a floating topic on the desktop, nothing on the phone.
    QCOMPARE(map.moveTarget(secondId, QPointF(-5000, -5000)).kind, ReorderTarget::Kind::Float);
    QVERIFY(!map.moveTopic(secondId, QPointF(-5000, -5000)));

    // Dropped on First: its last child, selected, at the drawn position, one Undo step.
    at = centreOf(second);
    QVERIFY(canvas.beginMove(at.x(), at.y()));
    const QPointF onFirst = centreOf(first);
    canvas.moveTo(onFirst.x(), onFirst.y());
    const ReorderTarget target = map.moveTarget(secondId, canvas.mapPoint(onFirst));
    QCOMPARE(target.kind, ReorderTarget::Kind::Child);
    QCOMPARE(target.parent, first);
    canvas.endMove(true);
    QVERIFY(!canvas.moving());
    const MindNode *moved = map.document()->model()->nodeById(secondId);
    QCOMPARE(moved->parent, first);
    QCOMPARE(first->children.back().get(), moved);
    QCOMPARE(map.selectedId(), secondId);
    for (const MapSnapshot::Topic &topic : map.snapshot()->topics()) QCOMPARE(topic.node->position, topic.position);
    QVERIFY(map.undo());
    QCOMPARE(map.document()->model()->nodeById(secondId)->parent, map.document()->model()->root());
    QVERIFY(!map.canUndo());

    // Near the edges the view scrolls by the desktop's steps (FEAT-033), also with the finger still.
    QCOMPARE(canvas.autoScrollStep(QPointF(200, 400)), QPointF());
    QCOMPARE(canvas.autoScrollStep(QPointF(0, 400)), QPointF(-MapCanvas::kAutoScrollMaxStepPx, 0));
    QCOMPARE(canvas.autoScrollStep(QPointF(-30, 400)), QPointF(-MapCanvas::kAutoScrollMaxStepPx, 0));
    QCOMPARE(canvas.autoScrollStep(QPointF(388, 799)), QPointF(22, 4 + 36.0 * 23 / 24));
    canvas.setBandTop(100);  // the band's top counts as the edge
    QCOMPARE(canvas.autoScrollStep(QPointF(200, 112)).y(), -22.0);
    canvas.setBandTop(0);
    at = centreOf(map.document()->model()->nodeById(secondId));
    QVERIFY(canvas.beginMove(at.x(), at.y()));
    const QPointF origin = canvas.mapPoint(QPointF());
    canvas.moveTo(0, 400);
    QTRY_VERIFY(canvas.mapPoint(QPointF()).x() < origin.x());
    canvas.endMove(false);  // given up: nothing moves
    QCOMPARE(map.document()->model()->nodeById(secondId)->parent, map.document()->model()->root());
    QVERIFY(!map.canUndo());
    QVERIFY(map.close());
}

// FEAT-085 on the phone: the topic bar's format painter copies the selected topic's style, a tap
// pastes it in one Undo step without selecting; a sticky one stays armed until the background.
void MobileTest::formatPainterPastesTheStyle() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Paint.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MapCanvas canvas;
    canvas.setSize(QSizeF(400, 800));
    canvas.setMap(&map);
    MindMapModel *model = map.document()->model();
    MindNode *first = const_cast<MindNode *>(childTitled(model->root(), QStringLiteral("First")));
    const QString secondId = childTitled(model->root(), QStringLiteral("Second"))->id;
    MindMapModel::TopicStyle red = model->topicStyle(first);
    red.style.fill = QColor(Qt::red);
    QVERIFY(model->pasteStyle(red, {first}));
    map.document()->markChanged();
    map.document()->flushPendingStep();
    const auto tap = [&](const QString &id) {
        const QPointF at = canvas.itemPoint(map.snapshot()->topicOf(model->nodeById(id))->bounds.center());
        canvas.tapAt(at.x(), at.y());
    };

    map.select(first->id);
    map.startFormatPainter(false);
    QVERIFY(map.formatPainter());
    QVERIFY(!map.formatPainterSticky());
    tap(secondId);
    QCOMPARE(model->nodeById(secondId)->fill, QColor(Qt::red));
    QVERIFY(!map.formatPainter());  // one paste, then off
    QCOMPARE(map.selectedId(), first->id);  // the selection stays
    QVERIFY(map.undo());
    QVERIFY(model->nodeById(secondId)->fill != QColor(Qt::red));

    map.startFormatPainter(true);
    QVERIFY(map.formatPainterSticky());
    tap(secondId);
    QVERIFY(map.formatPainter());  // sticky: still armed
    QCOMPARE(model->nodeById(secondId)->fill, QColor(Qt::red));
    canvas.tapAt(1, 1);  // the background turns it off
    QVERIFY(!map.formatPainter());
    map.startFormatPainter(true);
    QVERIFY(map.switchSheet(1));  // so does a sheet switch
    QVERIFY(!map.formatPainter());
    QVERIFY(map.close());
}

// FEAT-110: Relationship as Copy style (one tap = one, kept on = many from the same start), a
// relationship selected by a tap on its line, its label renamed and the relationship deleted, each
// one Undo step; the start removed turns the tool off.
void MobileTest::relationshipsOnThePhone() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Relate.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MapCanvas canvas;
    canvas.setSize(QSizeF(400, 800));
    canvas.setMap(&map);
    MindMapModel *model = map.document()->model();
    const QString rootId = model->root()->id;
    const QString firstId = childTitled(model->root(), QStringLiteral("First"))->id;
    const QString secondId = childTitled(model->root(), QStringLiteral("Second"))->id;
    const auto tap = [&](const QString &id) {
        const QPointF at = canvas.itemPoint(map.snapshot()->topicOf(model->nodeById(id))->bounds.center());
        canvas.tapAt(at.x(), at.y());
    };

    map.select(firstId);
    map.startRelation(false);
    QVERIFY(map.relationTool());
    QVERIFY(!map.relationToolKept());
    tap(secondId);
    QCOMPARE(model->relations.size(), 1);
    QCOMPARE(model->relations.front().from, firstId);
    QCOMPARE(model->relations.front().to, secondId);
    QVERIFY(!map.relationTool());              // one, then off
    QCOMPARE(map.selectedId(), firstId);       // the selection stays
    QCOMPARE(map.snapshot()->relations().size(), 1);
    QVERIFY(map.undo());
    QVERIFY(model->relations.isEmpty());
    QVERIFY(map.snapshot()->relations().isEmpty());

    // Kept on: many from the same start, one step each; the start does nothing, the background ends it.
    map.startRelation(true);
    tap(secondId);
    tap(firstId);
    tap(rootId);
    QCOMPARE(model->relations.size(), 2);
    QVERIFY(map.relationToolKept());
    canvas.tapAt(1, 1);
    QVERIFY(!map.relationTool());
    QVERIFY(map.undo());
    QCOMPARE(model->relations.size(), 1);
    // Copy style and Relationship exclude each other.
    map.startRelation(true);
    map.startFormatPainter(false);
    QVERIFY(!map.relationTool());
    map.startRelation(false);
    QVERIFY(!map.formatPainter());
    map.stopRelation();

    // A tap on the line selects the relationship (no topic); Rename sets its label, Delete removes it.
    const MapSnapshot::Relation &drawn = map.snapshot()->relations().front();
    const QPointF onLine = canvas.itemPoint(drawn.geometry.path.pointAtPercent(0.5));
    QVERIFY(!map.snapshot()->topicAt(drawn.geometry.path.pointAtPercent(0.5)));
    canvas.tapAt(onLine.x(), onLine.y());
    const QString relationId = model->relations.front().id;
    QCOMPARE(map.selectedRelationId(), relationId);
    QVERIFY(map.selectedId().isEmpty());
    QVERIFY(map.renameRelation(QStringLiteral("  why\nnot ")));
    QCOMPARE(model->relations.front().label, QStringLiteral("why not"));  // one line, as the desktop's
    QCOMPARE(map.selectedRelationLabel(), QStringLiteral("why not"));
    QVERIFY(!map.snapshot()->relations().front().labelRect.isEmpty());
    QVERIFY(!map.renameRelation(QStringLiteral("why not")));  // unchanged: no step
    QVERIFY(map.undo());
    QVERIFY(model->relations.front().label.isEmpty());
    QCOMPARE(map.selectedRelationId(), relationId);  // still selected after the step
    QVERIFY(map.removeSelected());
    QVERIFY(model->relations.isEmpty());
    QVERIFY(map.selectedRelationId().isEmpty());
    QVERIFY(map.undo());
    QCOMPARE(model->relations.size(), 1);
    // A tap on a topic selects it instead.
    tap(firstId);
    QCOMPARE(map.selectedId(), firstId);
    QVERIFY(map.selectedRelationId().isEmpty());

    // The start removed turns the tool off; so does a sheet switch.
    map.select(secondId);
    map.startRelation(true);
    QVERIFY(map.removeSelected());
    QVERIFY(!map.relationTool());
    QVERIFY(map.undo());
    map.select(firstId);
    map.startRelation(true);
    QVERIFY(map.switchSheet(1));
    QVERIFY(!map.relationTool());
    QVERIFY(map.close());
}

// FEAT-110: a relationship is drawn as on the desktop: its line dashed by QPen's pattern, its end
// marks, its label on the backdrop.
void MobileTest::relationshipsAreDrawn() {
    // A dashed 2 px line 100 long: dashes of 4 x 2 px, gaps of 2 x 2 px (QPen::dashPattern).
    const QList<QPolygonF> dashed = MapScene::dashes(QPolygonF{QPointF(0, 0), QPointF(100, 0)}, Qt::DashLine, 2);
    QCOMPARE(dashed.size(), 9);  // 12 px a period: 8 whole dashes and one of 4 px at the end
    QCOMPARE(QLineF(dashed.front().first(), dashed.front().last()).length(), 8.0);
    QCOMPARE(dashed.at(1).first(), QPointF(12, 0));
    QCOMPARE(MapScene::dashes(QPolygonF{QPointF(0, 0), QPointF(100, 0)}, Qt::SolidLine, 2).size(), 1);
    QVERIFY(MapScene::dashes(QPolygonF{QPointF(0, 0), QPointF(100, 0)}, Qt::NoPen, 2).isEmpty());

    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Drawn.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MindMapModel *model = map.document()->model();
    const QString firstId = childTitled(model->root(), QStringLiteral("First"))->id;
    const QString secondId = childTitled(model->root(), QStringLiteral("Second"))->id;
    QVERIFY(!map.document()->addRelation(firstId, secondId).isEmpty());  // as made on the desktop
    QVERIFY(map.switchSheet(1));  // drawn again from the file's state
    QVERIFY(map.switchSheet(0));
    QCOMPARE(map.snapshot()->relations().size(), 1);
    const MapSnapshot::Relation &relation = map.snapshot()->relations().front();
    QCOMPARE(relation.look.linePattern, int(Qt::DashLine));
    QCOMPARE(relation.look.endMark, BranchEnd::FilledArrow);
    QVERIFY(relation.labelRect.isEmpty());
    MapScene::Vertices line;
    MapScene::appendRelation(line, relation);
    MapSnapshot::Relation unmarked = relation;
    unmarked.look.endMark = BranchEnd::None;
    MapScene::Vertices withoutMark;
    MapScene::appendRelation(withoutMark, unmarked);
    QVERIFY(line.size() > withoutMark.size());  // the arrow
    QVERIFY(!withoutMark.isEmpty());            // the dashes
    // The snapshot's bounds hold the line, its label and its marks.
    QVERIFY(map.snapshot()->bounds().contains(relation.geometry.path.boundingRect()));
    QVERIFY(map.close());
}

// FEAT-037 on the phone: a picked image is copied into the cache, checked, put in the topic at the
// desktop's fitted size in one Undo step, embedded by the closing save; the copies then go.
void MobileTest::pickedImageIsInsertedAndEmbedded() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Image.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    const QString picture = storage.filePath(QStringLiteral("wide.png"));
    QImage image(800, 400, QImage::Format_RGB32);
    image.fill(Qt::darkGreen);
    QVERIFY(image.save(picture));
    const QString text = storage.filePath(QStringLiteral("notes.png"));
    { QFile file(text); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("not a picture"); }
    MobileMap map;
    QVERIFY(map.open(path));
    MindNode *first = const_cast<MindNode *>(childTitled(map.document()->model()->root(), QStringLiteral("First")));
    map.select(first->id);

    const QVariantMap wrong = map.pickImage(QUrl::fromLocalFile(text));
    QVERIFY(!wrong.value(QStringLiteral("ok")).toBool());
    QVERIFY(wrong.value(QStringLiteral("reason")).toString().contains(QStringLiteral("notes.png")));
    QVERIFY(QDir(map.pickedImagesFolder()).entryList(QDir::Files).isEmpty());  // the bad copy is gone

    const QVariantMap picked = map.pickImage(QUrl::fromLocalFile(picture));
    QVERIFY(picked.value(QStringLiteral("ok")).toBool());
    QVERIFY(!picked.value(QStringLiteral("large")).toBool());
    const QString copy = picked.value(QStringLiteral("path")).toString();
    QVERIFY(copy.startsWith(map.pickedImagesFolder()));
    const qreal room = map.selectedTopic()->geometry->imageRoomWidth();
    QVERIFY(map.insertImage(copy));
    QCOMPARE(first->imagePath, copy);
    QCOMPARE(first->imageSize, NodeGeometry::fittedImageSize(picture, room));
    QVERIFY(first->imageSize.width() <= std::max<qreal>(std::floor(room), NodeGeometry::kMinInsertWidthPx));
    QVERIFY(map.undo());
    QVERIFY(map.document()->model()->nodeById(first->id)->imagePath.isEmpty());
    QVERIFY(map.redo());

    QVERIFY(map.close());
    QVERIFY(!QFileInfo::exists(map.pickedImagesFolder()));
    Document document;
    QVERIFY(document.open(path, false).ok);
    const MindNode *saved = childTitled(document.model()->root(), QStringLiteral("First"));
    QVERIFY(saved && !saved->imageAssetId.isEmpty());
    document.close();
}

// FEAT-051 on the phone, the marker page: a tap gives the topic the marker (one per series), a tap
// on the one it has takes it off; one Undo step each.
void MobileTest::markersAreGivenAndTakenOff() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Markers.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    StickerLibrary library;
    library.setMarkers(true);
    const QVariantList collections = library.collections();
    QVERIFY(!collections.isEmpty());
    // The built-in priorities: drawn markers, whose id is their path.
    QString one, two, collection;
    for (const QVariant &c : collections)
        for (const QVariant &i : c.toMap().value(QStringLiteral("items")).toList()) {
            const QString marker = i.toMap().value(QStringLiteral("marker")).toString();
            if (marker == QStringLiteral("priority:1")) { one = marker; collection = c.toMap().value(QStringLiteral("id")).toString(); }
            if (marker == QStringLiteral("priority:2")) two = marker;
        }
    QVERIFY(!one.isEmpty() && !two.isEmpty());
    const MindNode *first = childTitled(map.document()->model()->root(), QStringLiteral("First"));
    map.select(first->id);
    QVERIFY(map.toggleMarker(one, QString(), collection));
    QCOMPARE(map.selectedMarkers(), QStringList{one});
    QVERIFY(map.toggleMarker(two, QString(), collection));  // the same series: replaced
    QCOMPARE(map.selectedMarkers(), QStringList{two});
    QVERIFY(map.toggleMarker(two, QString(), collection));  // the one it has: taken off
    QVERIFY(map.selectedMarkers().isEmpty());
    QVERIFY(map.undo());
    QCOMPARE(map.selectedMarkers(), QStringList{two});
    QVERIFY(!map.toggleMarker(QStringLiteral("not:a-marker"), QString(), collection));
    QVERIFY(map.close());
}

// Add in the modal: a picked file checked as on the desktop, named, put in the first own collection
// (made when missing) and in the topic; an own marker carries its image into the map.
void MobileTest::filesAreAddedToTheLibrary() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Library.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    const auto picture = [&](const QString &name, int side) {
        QImage image(side, side, QImage::Format_ARGB32);
        image.fill(Qt::darkCyan);
        const QString file = storage.filePath(name);
        return image.save(file) ? file : QString();
    };
    const QString big = picture(QStringLiteral("Big sticker.png"), 300);
    const QString tiny = picture(QStringLiteral("Tiny.png"), 32);
    const QString text = storage.filePath(QStringLiteral("text.png"));
    { QFile file(text); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("not a picture"); }
    const auto ownCollections = [](IconLibrary::Kind kind) {
        QStringList ids;
        for (const IconLibrary::Collection &c : IconLibrary::collections(kind)) if (!c.builtIn) ids.append(c.id);
        return ids;
    };
    // The own collections and their files go before and after: a file left by another run would
    // take the name first.
    const auto clearOwn = [&] {
        for (const IconLibrary::Kind kind : {IconLibrary::Kind::Sticker, IconLibrary::Kind::Marker}) {
            for (const QString &id : ownCollections(kind)) IconLibrary::removeCollection(kind, id);
            IconLibrary::removeDeletedFiles(kind);
        }
    };
    clearOwn();
    MobileMap map;
    QVERIFY(map.open(path));
    MindNode *first = const_cast<MindNode *>(childTitled(map.document()->model()->root(), QStringLiteral("First")));
    map.select(first->id);

    const QVariantMap refused = map.pickLibraryFile(QUrl::fromLocalFile(text), false);
    QVERIFY(!refused.value(QStringLiteral("ok")).toBool());
    QVERIFY(refused.value(QStringLiteral("reason")).toString().startsWith(QStringLiteral("\"text.png\": ")));

    const QVariantMap sticker = map.pickLibraryFile(QUrl::fromLocalFile(big), false);
    QVERIFY(sticker.value(QStringLiteral("ok")).toBool());
    QVERIFY(!sticker.value(QStringLiteral("small")).toBool());
    QCOMPARE(sticker.value(QStringLiteral("name")).toString(), IconLibrary::displayName(big));
    QVERIFY(map.addToLibrary(sticker.value(QStringLiteral("path")).toString(), QStringLiteral("Mine"), false));
    QCOMPARE(ownCollections(IconLibrary::Kind::Sticker).size(), 1);
    const IconLibrary::Collection stickers = IconLibrary::collection(IconLibrary::Kind::Sticker, ownCollections(IconLibrary::Kind::Sticker).front());
    QCOMPARE(stickers.items.size(), 1);
    QCOMPARE(stickers.items.front().name, QStringLiteral("Mine"));
    QVERIFY(first->imageSticker);
    QCOMPARE(first->imagePath, stickers.items.front().path);

    const QVariantMap marker = map.pickLibraryFile(QUrl::fromLocalFile(tiny), true);
    QVERIFY(marker.value(QStringLiteral("ok")).toBool());
    QVERIFY(marker.value(QStringLiteral("small")).toBool());  // under 64 px: asked
    QVERIFY(map.addToLibrary(marker.value(QStringLiteral("path")).toString(), QString(), true));
    QCOMPARE(ownCollections(IconLibrary::Kind::Marker).size(), 1);
    QCOMPARE(first->markers.size(), 1);
    QVERIFY(Markers::isCustom(first->markers.front()));
    QVERIFY(map.document()->model()->markerImages.contains(Markers::customImage(first->markers.front())));
    QVERIFY(map.close());
    clearOwn();
}

// FEAT-051 on the phone: a sticker of the library in the selected topic, at the desktop's insertion
// size, laid out in one Undo step and embedded in the file.
void MobileTest::stickerIsInsertedAndSaved() {
    const QVariantList collections = StickerLibrary().collections();
    QVERIFY(!collections.isEmpty());
    const QVariantMap first = collections.constFirst().toMap().value(QStringLiteral("items")).toList().constFirst().toMap();
    const QString sticker = first.value(QStringLiteral("path")).toString();
    QVERIFY(sticker.startsWith(QStringLiteral(":/stickers/")));
    QCOMPARE(first.value(QStringLiteral("source")).toUrl().scheme(), QStringLiteral("qrc"));

    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Sticker.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    QVERIFY(!map.insertSticker(sticker));  // nothing selected
    const MindNode *root = map.document()->model()->root();
    map.select(root->id);
    const QRectF before = map.selectedTopic()->bounds;
    QVERIFY(map.insertSticker(sticker));
    QVERIFY(root->imageSticker);
    QVERIFY(root->imageStyle.outline);
    QCOMPARE(root->imageSize, Stickers::insertionSize(sticker));
    QVERIFY(map.selectedTopic()->bounds.height() > before.height());  // the topic makes room for it
    QVERIFY(!map.insertSticker(sticker));  // the same sticker again is no edit (BUG-029)
    QVERIFY(!map.insertSticker(storage.filePath(QStringLiteral("missing.png"))));
    QVERIFY(!map.lastError().isEmpty());

    QVERIFY(map.undo());  // the sticker and its layout are one step
    QVERIFY(root->imagePath.isEmpty());
    QCOMPARE(map.selectedTopic()->bounds, before);
    QVERIFY(map.redo());
    QVERIFY(map.close());
    Document document;
    QVERIFY(document.open(path, false).ok);
    QVERIFY(document.model()->root()->imageSticker);
    QVERIFY(!document.model()->root()->imageAssetId.isEmpty());  // embedded, not a path to the resource
    document.close();
}

void MobileTest::keyboardCommands() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Keys.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MindMapModel *model = map.document()->model();
    const MindNode *root = model->root();
    // With nothing selected an arrow starts from the central topic, as on the desktop: a main topic
    // on that side, or the central topic when there is none that way.
    const bool anyRight = std::any_of(root->children.cbegin(), root->children.cend(), [&](const auto &child) {
        return map.snapshot()->topicOf(child.get())->position.x() > map.snapshot()->topicOf(root)->position.x();
    });
    QVERIFY(map.selectNeighbour(Qt::Key_Right));
    QCOMPARE(model->nodeById(map.selectedId())->parent == root, anyRight);
    map.select(QString());
    QVERIFY(map.selectNeighbour(Qt::Key_Up));  // nothing above the central topic: itself
    QCOMPARE(map.selectedId(), root->id);
    QVERIFY(!map.selectNeighbour(Qt::Key_Up));  // selected, nothing that way: no change
    // From the central topic to a main topic on that side, and back (MapNavigation).
    for (const MindNode *child : {childTitled(root, QStringLiteral("First")), childTitled(root, QStringLiteral("Second"))}) {
        const bool left = map.snapshot()->topicOf(child)->position.x() < map.snapshot()->topicOf(root)->position.x();
        map.select(root->id);
        QVERIFY(map.selectNeighbour(left ? Qt::Key_Left : Qt::Key_Right));
        const MindNode *reached = model->nodeById(map.selectedId());
        QCOMPARE(reached->parent, root);
        QCOMPARE(map.snapshot()->topicOf(reached)->position.x() < map.snapshot()->topicOf(root)->position.x(), left);
        QVERIFY(map.selectNeighbour(left ? Qt::Key_Right : Qt::Key_Left));
        QCOMPARE(map.selectedId(), root->id);
    }
    // Alt+Up / Alt+Down among the siblings, one Undo step each.
    const QString firstId = childTitled(root, QStringLiteral("First"))->id;
    map.select(firstId);
    QVERIFY(map.addChild());
    const QString a = map.selectedId();
    map.select(firstId);
    QVERIFY(map.addChild());
    const QString b = map.selectedId();
    const MindNode *first = model->nodeById(firstId);
    QCOMPARE(first->children.back()->id, b);
    QVERIFY(map.moveAmongSiblings(-1));
    QCOMPARE(first->children.front()->id, b);
    QCOMPARE(map.selectedId(), b);
    QVERIFY(map.undo());
    QCOMPARE(first->children.back()->id, b);
    // Shift+Enter: a sibling before; Ctrl+Enter: a parent over the selected topic; both selected.
    map.select(b);
    QVERIFY(map.addTopicBefore());
    QCOMPARE(first->children.size(), 3);
    QCOMPARE(first->children[1]->id, map.selectedId());
    QVERIFY(map.undo());
    QCOMPARE(first->children.size(), 2);
    map.select(a);
    QVERIFY(map.addParentTopic());
    const MindNode *inserted = model->nodeById(map.selectedId());
    QVERIFY(inserted && inserted->parent == first);
    QCOMPARE(inserted->children.size(), 1);
    QCOMPARE(inserted->children.front()->id, a);
    QVERIFY(map.undo());
    QCOMPARE(model->nodeById(a)->parent, first);
    // Ctrl+R: the central topic.
    map.selectCentral();
    QCOMPARE(map.selectedId(), root->id);
    // Copy and paste style: the selected topic gets the copied style in one Undo step.
    MindNode *source = model->nodeById(a);
    MindMapModel::TopicStyle red = model->topicStyle(source);
    red.style.fill = QColor(Qt::red);
    QVERIFY(model->pasteStyle(red, {source}));
    map.document()->markChanged();
    map.document()->flushPendingStep();
    map.select(QString());
    map.copyStyle();  // nothing selected: nothing copied
    QVERIFY(!map.pasteStyle());
    map.select(a);
    map.copyStyle();
    map.select(b);
    QVERIFY(map.pasteStyle());
    QCOMPARE(model->topicStyle(model->nodeById(b)).style, model->topicStyle(source).style);
    QCOMPARE(map.selectedId(), b);
    QVERIFY(map.undo());
    QVERIFY(model->topicStyle(model->nodeById(b)).style != model->topicStyle(source).style);
    QVERIFY(map.close());
}

// FEAT-107: the phone's Home shows one folder level, folders with their colour; maps move between them.
void MobileTest::folderLevelsInTheLibrary() {
    QTemporaryDir storage;
    const QString root = QDir::cleanPath(storage.path());
    MapLibrary library(root, nullptr);
    QVERIFY(library.atRoot());
    QVERIFY(library.createFolder(QStringLiteral("Exams")));
    QCOMPARE(library.folders().size(), 1);
    const QString exams = library.folders().first().toMap().value(QStringLiteral("path")).toString();
    QCOMPARE(library.folderColors().size(), MapFolder::folderColors().size() + 1);  // "No colour" first
    QCOMPARE(library.folderColors().first().toMap().value(QStringLiteral("color")).toString(), QString());
    const QString blue = MapFolder::folderColors().at(7);
    QVERIFY(library.setFolderColor(exams, blue));
    QCOMPARE(library.folders().first().toMap().value(QStringLiteral("color")).toString(), blue);

    const QString map = QDir(root).filePath(QStringLiteral("Plan.mentis"));
    QCOMPARE(writeTwoSheetMap(map), QString());
    library.refresh();
    QCOMPARE(library.maps().size(), 1);
    const QVariantList targets = library.moveTargets(map);
    QCOMPARE(targets.size(), 2);
    QVERIFY(targets.at(0).toMap().value(QStringLiteral("here")).toBool());  // its own folder
    QCOMPARE(targets.at(1).toMap().value(QStringLiteral("label")).toString(), QFileInfo(root).fileName() + QStringLiteral(" › Exams"));
    QVERIFY(library.moveMap(map, exams));
    QVERIFY(library.maps().isEmpty());
    QVERIFY(!library.removeFolder(exams));  // not empty
    QVERIFY(!library.lastError().isEmpty());

    QSignalSpy shown(&library, &MapLibrary::shownChanged);
    library.enter(exams);
    QCOMPARE(shown.size(), 1);
    QVERIFY(!library.atRoot());
    QCOMPARE(library.shownName(), QStringLiteral("Exams"));
    QCOMPARE(library.maps().size(), 1);
    QVERIFY(library.folders().isEmpty());
    library.up();
    QVERIFY(library.atRoot());
    library.enter(QDir::tempPath());  // outside the folder: the folder itself
    QVERIFY(library.atRoot());

    QVERIFY(library.renameFolder(exams, QStringLiteral("Done")));
    QCOMPARE(library.folders().first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Done"));
    QCOMPARE(library.folders().first().toMap().value(QStringLiteral("color")).toString(), blue);
}

void MobileTest::homeFoldersOnThePage() {
    QTemporaryDir storage;
    const QString root = QDir::cleanPath(storage.path());
    QVERIFY(QDir(root).mkdir(QStringLiteral("Courses")));
    const QString courses = QDir(root).filePath(QStringLiteral("Courses"));
    QString error;
    QVERIFY(MapFolder::setFolderColor(courses, MapFolder::folderColors().first(), &error));
    QCOMPARE(writeTwoSheetMap(QDir(root).filePath(QStringLiteral("Plan.mentis"))), QString());
    MapFolder::setConfiguredPath(root);
    const auto restore = qScopeGuard([] { MapFolder::setConfiguredPath(QString()); });

    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    window->resize(400, 800);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *library = window->findChild<MapLibrary *>();
    QVERIFY(library);
    QCOMPARE(QDir::cleanPath(library->folder()), root);
    auto *list = window->findChild<QQuickItem *>(QStringLiteral("mapList"));
    QVERIFY(list);
    auto *listContent = list->property("contentItem").value<QQuickItem *>();
    const auto rowNamed = [&](const QString &name) -> QQuickItem * {
        for (QQuickItem *item : listContent->childItems()) if (item->objectName() == name && item->isVisible()) return item;
        return nullptr;
    };
    const auto centre = [&](QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); };
    QQuickItem *folderRow = nullptr;
    QTRY_VERIFY((folderRow = rowNamed(QStringLiteral("folderRow"))));
    QVERIFY(rowNamed(QStringLiteral("mapRow")));
    QCOMPARE(list->property("count").toInt(), 2);

    // A tap enters the folder; Android's Back goes up again.
    QTest::mouseClick(window, Qt::LeftButton, {}, centre(folderRow));
    QTRY_VERIFY(!library->atRoot());
    QCOMPARE(window->findChild<QObject *>(QStringLiteral("homeTitle"))->property("text").toString(), QStringLiteral("Courses"));
    QTest::keyClick(window, Qt::Key_Back);
    QTRY_VERIFY(library->atRoot());
    QTest::qWait(100);  // the rows of the level are made again

    // A long press lifts the row at once; dragged onto the folder, the map moves there.
    const auto dragOnto = [&](QQuickItem *source, QQuickItem *target) {
        const QPoint from = centre(source);
        const QPoint to = centre(target);
        QTest::mousePress(window, Qt::LeftButton, {}, from);
        QTest::qWait(1000);  // past the long press
        QTRY_VERIFY(window->findChild<QQuickItem *>(QStringLiteral("entryGhost"))->isVisible());  // before any move
        for (int step = 1; step <= 10; ++step) {
            QTest::mouseMove(window, from + (to - from) * step / 10);
            QTest::qWait(20);
        }
        QTest::mouseRelease(window, Qt::LeftButton, {}, to);
    };
    QTRY_VERIFY((folderRow = rowNamed(QStringLiteral("folderRow"))));
    QQuickItem *mapRow = rowNamed(QStringLiteral("mapRow"));
    QVERIFY(mapRow);
    dragOnto(mapRow, folderRow);
    QTRY_COMPARE(library->maps().size(), 0);
    QVERIFY(QFileInfo::exists(QDir(courses).filePath(QStringLiteral("Plan.mentis"))));

    // A long press released in place: no menu, nothing moved.
    QObject *menu = window->findChild<QObject *>(QStringLiteral("entryMenu"));
    QVERIFY(menu);
    QTRY_VERIFY((folderRow = rowNamed(QStringLiteral("folderRow"))));
    QTest::mousePress(window, Qt::LeftButton, {}, centre(folderRow));
    QTest::qWait(1000);
    QTest::mouseRelease(window, Qt::LeftButton, {}, centre(folderRow));
    QTest::qWait(50);
    QVERIFY(!menu->property("visible").toBool());
    QVERIFY(library->atRoot());

    // The row's ⋮: its menu.
    QQuickItem *more = nullptr;
    for (QQuickItem *item : folderRow->childItems()) if (item->objectName() == QStringLiteral("entryMoreButton")) more = item;
    QVERIFY(more);
    QTest::mouseClick(window, Qt::LeftButton, {}, centre(more));
    QTRY_VERIFY(menu->property("opened").toBool());
    QCOMPARE(menu->property("actions").toList().size(), 5);
    QVERIFY(!menu->property("actions").toList().at(4).toMap().value(QStringLiteral("enabled")).toBool());  // Delete: not empty
    QVERIFY(QMetaObject::invokeMethod(menu, "close"));
    QTRY_VERIFY(!menu->property("visible").toBool());

    // Inside a folder ".." comes first; a map dragged onto it goes up a level.
    QTest::mouseClick(window, Qt::LeftButton, {}, centre(folderRow));
    QTRY_VERIFY(!library->atRoot());
    QTest::qWait(100);
    QQuickItem *upRow = nullptr;
    QTRY_VERIFY((upRow = rowNamed(QStringLiteral("upRow"))));
    QTRY_VERIFY((mapRow = rowNamed(QStringLiteral("mapRow"))));
    QVERIFY(upRow->y() < mapRow->y());
    dragOnto(mapRow, upRow);
    QTRY_COMPARE(library->maps().size(), 0);
    QVERIFY(QFileInfo::exists(QDir(root).filePath(QStringLiteral("Plan.mentis"))));
    // A tap on "..": the level above.
    QTRY_VERIFY((upRow = rowNamed(QStringLiteral("upRow"))));
    QTest::mouseClick(window, Qt::LeftButton, {}, centre(upRow));
    QTRY_VERIFY(library->atRoot());
    QTest::qWait(100);

    // A folder dragged onto another goes into it, with its colour.
    QVERIFY(library->createFolder(QStringLiteral("Archive")));
    QTest::qWait(100);
    QQuickItem *archive = nullptr;
    QQuickItem *coursesRow = nullptr;
    QTRY_VERIFY([&] {
        archive = coursesRow = nullptr;
        for (QQuickItem *item : listContent->childItems()) {
            if (item->objectName() != QStringLiteral("folderRow") || !item->isVisible()) continue;
            const QString name = item->property("modelData").toMap().value(QStringLiteral("name")).toString();
            if (name == QStringLiteral("Archive")) archive = item;
            if (name == QStringLiteral("Courses")) coursesRow = item;
        }
        return archive && coursesRow;
    }());
    dragOnto(coursesRow, archive);
    QTRY_COMPARE(library->folders().size(), 1);
    const QString movedCourses = QDir(root).filePath(QStringLiteral("Archive/Courses"));
    QVERIFY(QFileInfo(movedCourses).isDir());
    QCOMPARE(MapFolder::folderColor(movedCourses), MapFolder::folderColors().first());
    // "Move to…" for a folder: neither itself nor its subfolders; the folder it is in marked.
    const QVariantList targets = library->moveTargets(QDir(root).filePath(QStringLiteral("Archive")));
    QCOMPARE(targets.size(), 1);
    QVERIFY(targets.first().toMap().value(QStringLiteral("here")).toBool());
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).toString()));
}

void MobileTest::pagesLoadAndOpenAMap() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Pages.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());

    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(window->findChild<QObject *>(QStringLiteral("openFileButton")));
    auto *library = window->findChild<MapLibrary *>();
    QVERIFY(library);

    // As the picker's answer: the map is copied into the storage and shown.
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *canvas = nullptr;
    QTRY_VERIFY((canvas = window->findChild<MapCanvas *>()));
    QVERIFY(canvas->map() && canvas->map()->isOpen());
    QCOMPARE(canvas->map()->topicCount(), 3);
    QTRY_VERIFY(canvas->width() > 0);
    // The frame measurement runs to its end and reports.
    QMetaObject::invokeMethod(canvas, "runBenchmark");
    QVERIFY(canvas->benchmarkRunning());
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->benchmarkRunning(), 60000);
    QVERIFY(canvas->frameReport().contains(QStringLiteral("vertices")));
    // The drawn map is not blank: inside the canvas some pixel differs from the map's background.
    canvas->fit();
    QTest::qWait(50);
    const QImage frame = window->grabWindow();
    const QRect area = canvas->mapRectToScene(QRectF(0, 0, canvas->width(), canvas->height())).toRect().intersected(frame.rect());
    QVERIFY(!area.isEmpty());
    bool drawn = false;
    for (int y = area.top(); y <= area.bottom() && !drawn; y += 2)
        for (int x = area.left(); x <= area.right() && !drawn; x += 2) drawn = frame.pixelColor(x, y) != canvas->map()->snapshot()->background();
    QVERIFY(drawn);
    // B1: with a topic selected, the desktop's "+" knobs (2026-11-11): the child one 15 px beside
    // the topic on its branch side, its 48 px target outside the topic; none for a sibling of the
    // central topic. The child knob adds a child and asks its title.
    MobileMap *map = canvas->map();
    auto *bar = window->findChild<QQuickItem *>(QStringLiteral("topicBar"));
    auto *childKnob = window->findChild<QQuickItem *>(QStringLiteral("addChildKnob"));
    auto *siblingKnob = window->findChild<QQuickItem *>(QStringLiteral("addSiblingKnob"));
    QVERIFY(bar && childKnob && siblingKnob);
    QVERIFY(bar->isVisible());  // with nothing selected it holds only Select more (2026-11-12)
    QVERIFY(!childKnob->isVisible());
    map->select(map->document()->model()->root()->id);
    QTRY_VERIFY(bar->isVisible());
    QVERIFY(childKnob->isVisible());
    QVERIFY(!siblingKnob->isVisible());
    {
        const MapSnapshot::Topic *rootTopic = map->selectedTopic();
        const qreal outerRight = canvas->itemPoint(rootTopic->bounds.bottomRight() - QPointF(4, 4)).x();
        // BUG-051: the offsets follow the zoom, the target keeps 48 px.
        QCOMPARE(canvas->childKnob().x(), outerRight + 15 * canvas->zoom());
        QCOMPARE(childKnob->x(), outerRight);
        QCOMPARE(childKnob->width(), std::max(48.0, 24 * canvas->zoom()));
    }
    QVERIFY(QMetaObject::invokeMethod(childKnob, "clicked"));
    QCOMPARE(map->topicCount(), 4);
    // The title is typed in the topic: the field is over it, the scene leaves the title out, and
    // every change reaches the map while it is typed.
    auto *field = window->findChild<QQuickItem *>(QStringLiteral("titleEditor"));
    QVERIFY(field);
    QTRY_VERIFY(field->isVisible());
    QVERIFY(canvas->editing());
    QVERIFY(map->editingTitle());
    QCOMPARE(field->property("text").toString(), map->selectedTitle());
    const QPointF anchor = canvas->editorAnchor();
    QVERIFY(QLineF(field->mapToItem(canvas, QPointF(0, field->height() / 2)), anchor).length() < 1);
    field->setProperty("text", QStringLiteral("From the page"));
    QTRY_COMPARE(map->selectedTitle(), QStringLiteral("From the page"));
    QVERIFY(map->editingTitle());  // still one open step
    QObject *page = window->findChild<QObject *>(QStringLiteral("mapPage"));
    QVERIFY(page);
    QVERIFY(QMetaObject::invokeMethod(page, "endEdit"));
    QVERIFY(!field->isVisible());
    QVERIFY(!canvas->editing());
    QVERIFY(!map->editingTitle());  // confirmed: the step with the title is closed
    QVERIFY(map->undo());
    QCOMPARE(map->topicCount(), 3);
    QVERIFY(map->redo());
    // A main topic has both knobs, the sibling one 17 px under it; none while a title is typed.
    map->select(map->document()->model()->root()->children.back()->id);
    QTRY_VERIFY(siblingKnob->isVisible());
    {
        const MapSnapshot::Topic *child = map->selectedTopic();
        const qreal outerBottom = canvas->itemPoint(child->bounds.bottomRight() - QPointF(4, 4)).y();
        QCOMPARE(canvas->siblingKnob().y(), outerBottom + 17 * canvas->zoom());
        QCOMPARE(siblingKnob->y(), outerBottom);
        const int before = map->topicCount();
        QVERIFY(QMetaObject::invokeMethod(siblingKnob, "clicked"));
        QCOMPARE(map->topicCount(), before + 1);
        QTRY_VERIFY(!siblingKnob->isVisible() && !childKnob->isVisible());
        QVERIFY(QMetaObject::invokeMethod(page, "endEdit"));
        QVERIFY(map->undo());
        QCOMPARE(map->topicCount(), before);
    }
    // The desktop's collapse control beside the selected topic (2026-11-11), not in the topic bar:
    // none without branches; an open branch's 11 px circle 12 px from the shape on the children's
    // side, the child knob 4 px past it, the two targets meeting halfway; tapped, the branch closes
    // and the control shows the count of hidden topics, tapped again it opens.
    {
        auto *collapse = window->findChild<QQuickItem *>(QStringLiteral("collapseControl"));
        QVERIFY(collapse);
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("collapseButton")));
        MindNode *main = map->document()->model()->root()->children.front().get();
        map->select(main->id);
        QTRY_VERIFY(childKnob->isVisible());
        QVERIFY(!collapse->isVisible());
        const int before = map->topicCount();
        QVERIFY(QMetaObject::invokeMethod(childKnob, "clicked"));
        QVERIFY(QMetaObject::invokeMethod(page, "endEdit"));
        map->select(main->id);
        QTRY_VERIFY(collapse->isVisible());
        const MapSnapshot::Topic *topic = map->selectedTopic();
        const QRectF shape = topic->geometry->shapeRect().translated(topic->position);
        const bool right = canvas->collapseControl().center().x() > canvas->itemPoint(shape.center()).x();
        const qreal side = right ? canvas->itemPoint(shape.bottomRight()).x() : canvas->itemPoint(shape.topLeft()).x();
        const QRectF circle = canvas->collapseControl();
        const qreal z = canvas->zoom();  // BUG-051: the control and the knob follow the zoom
        QCOMPARE(circle.size(), QSizeF(11 * z, 11 * z));
        QCOMPARE(circle.center().x(), side + (right ? 12 : -12) * z);
        QCOMPARE(canvas->childKnob().x(), right ? circle.right() + 13 * z : circle.left() - 13 * z);
        const QRectF target = canvas->collapseTarget(), knobTarget = canvas->childKnobTarget();
        QCOMPARE(right ? target.right() : target.left(), right ? knobTarget.left() : knobTarget.right());
        QVERIFY(target.contains(circle) && knobTarget.contains(canvas->childKnob()));
        QVERIFY(!bar->isAncestorOf(collapse));
        QVERIFY(QMetaObject::invokeMethod(collapse, "clicked"));
        QTRY_VERIFY(map->selectedCollapsed());
        QCOMPARE(canvas->collapseCount(), 1);
        QCOMPARE(map->topicCount(), before);
        QCOMPARE(canvas->collapseControl().height(), 18 * z);
        canvas->zoomStep(0);  // actual size, then twice it
        canvas->zoomStep(100);
        QCOMPARE(canvas->zoom(), 2.0);
        QCOMPARE(canvas->collapseControl().height(), 36.0);
        // BUG-052: the count's digits are centred in the pill, on their ink, not on the line box.
        {
            auto *count = window->findChild<QQuickItem *>(QStringLiteral("collapseCount"));
            QVERIFY(count);
            QTRY_VERIFY(count->isVisible());
            const QRectF ink = QFontMetricsF(count->property("font").value<QFont>()).tightBoundingRect(QStringLiteral("1"));
            const QPointF origin = count->mapToScene(QPointF(0, count->baselineOffset()));
            const QPointF inkCentre = origin + ink.center() * canvas->zoom();
            const QPointF pillCentre = canvas->mapToScene(canvas->collapseControl().center());
            QVERIFY2(qAbs(inkCentre.x() - pillCentre.x()) < 1 && qAbs(inkCentre.y() - pillCentre.y()) < 1,
                     qPrintable(QStringLiteral("ink centre %1,%2, pill centre %3,%4").arg(inkCentre.x()).arg(inkCentre.y())
                                    .arg(pillCentre.x()).arg(pillCentre.y())));
        }
        QVERIFY(QMetaObject::invokeMethod(collapse, "clicked"));
        QTRY_VERIFY(!map->selectedCollapsed());
        QCOMPARE(canvas->collapseCount(), 0);
        for (int step = 0; step < 3; ++step) QVERIFY(map->undo());
        QCOMPARE(map->topicCount(), before);
    }
    // Present as on the desktop's top bar waits for Pitch on the phone; the top group has no
    // Stickers button (user's request 2026-11-11). The picker opens on Sticker, as the Stickers
    // key does, and a tap puts the sticker in the selected topic.
    QObject *present = window->findChild<QObject *>(QStringLiteral("presentButton"));
    QVERIFY(present);
    QVERIFY(!present->property("enabled").toBool());
    QVERIFY(!window->findChild<QObject *>(QStringLiteral("stickersButton")));
    // Fit to window in the top group (user's request 2026-11-12) fits the map as the double tap.
    {
        QObject *fitButton = window->findChild<QObject *>(QStringLiteral("fitButton"));
        QVERIFY(fitButton);
        canvas->fit();
        const qreal fitted = canvas->zoom();
        canvas->zoomStep(200);
        canvas->panBy(canvas->width(), 0);
        QVERIFY(QMetaObject::invokeMethod(fitButton, "clicked"));
        QCOMPARE(canvas->zoom(), fitted);
    }
    const MindNode *root = map->document()->model()->root();
    map->select(root->id);
    QObject *picker = window->findChild<QObject *>(QStringLiteral("stickerPicker"));
    QVERIFY(picker);
    QVERIFY(QMetaObject::invokeMethod(picker, "openOn", Q_ARG(QVariant, false)));
    QTRY_VERIFY(picker->property("opened").toBool());
    // A list's delegates have no QObject parent: the cell is found among the visual children.
    auto *list = window->findChild<QQuickItem *>(QStringLiteral("stickerCollections"));
    QVERIFY(list);
    // The list ends above the buttons, inside the modal: the last collections can be reached.
    auto *footer = picker->property("footer").value<QQuickItem *>();
    QVERIFY(footer);
    QTRY_VERIFY(list->height() > 0);
    const QRectF listBox = list->mapRectToScene(QRectF(0, 0, list->width(), list->height()));
    QVERIFY2(listBox.bottom() <= footer->mapToScene(QPointF()).y() + 0.5,
             qPrintable(QStringLiteral("list bottom %1, buttons top %2").arg(listBox.bottom()).arg(footer->mapToScene(QPointF()).y())));
    const std::function<QQuickItem *(QQuickItem *)> findCell = [&](QQuickItem *item) -> QQuickItem * {
        if (item->objectName() == QStringLiteral("sticker")) return item;
        for (QQuickItem *child : item->childItems())
            if (QQuickItem *found = findCell(child)) return found;
        return nullptr;
    };
    QQuickItem *cell = nullptr;
    QTRY_VERIFY((cell = findCell(list)));
    QVERIFY(QMetaObject::invokeMethod(cell, "clicked"));
    QVERIFY(root->imageSticker);
    QTRY_VERIFY(!picker->property("visible").toBool());
    // The sticker is drawn: inside its place most pixels differ from the topic's fill.
    canvas->fit();
    QTest::qWait(50);
    const QImage withSticker = window->grabWindow();
    const MapSnapshot::Topic *topic = map->snapshot()->topicOf(root);
    QVERIFY(topic);
    const QRectF place = topic->geometry->imageRect().translated(topic->position);
    const QRect shown = canvas->mapRectToScene(QRectF(canvas->itemPoint(place.topLeft()), canvas->itemPoint(place.bottomRight())))
                            .toRect().intersected(withSticker.rect());
    QVERIFY(shown.width() > 4 && shown.height() > 4);
    int differing = 0, total = 0;
    for (int y = shown.top(); y <= shown.bottom(); ++y)
        for (int x = shown.left(); x <= shown.right(); ++x, ++total) differing += withSticker.pixelColor(x, y) != root->fill;
    QVERIFY2(differing > total / 4, qPrintable(QStringLiteral("%1 of %2").arg(differing).arg(total)));
    // The topic bar's Sticker opens the modal on Marker: a tap gives the topic a marker and closes
    // the modal, as a sticker does (user's report 2026-11-10), and the map draws it in its place.
    QObject *topicSticker = window->findChild<QObject *>(QStringLiteral("topicStickerButton"));
    QVERIFY(topicSticker);
    QTRY_VERIFY(topicSticker->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(topicSticker, "clicked"));
    QTRY_VERIFY(picker->property("opened").toBool());
    QVERIFY(picker->property("markers").toBool());
    const std::function<QQuickItem *(QQuickItem *)> findMarker = [&](QQuickItem *item) -> QQuickItem * {
        if (item->objectName() == QStringLiteral("marker")) return item;
        for (QQuickItem *child : item->childItems())
            if (QQuickItem *found = findMarker(child)) return found;
        return nullptr;
    };
    QQuickItem *markerCell = nullptr;
    QTRY_VERIFY((markerCell = findMarker(list)));
    // A finger dragged over the cells moves the list, never the map under the modal (user's report
    // 2026-11-10).
    {
        static QPointingDevice *finger = QTest::createTouchDevice();
        QSignalSpy panned(canvas, &MapCanvas::viewChanged);
        const QPoint from = markerCell->mapToScene(QPointF(markerCell->width() / 2, markerCell->height() / 2)).toPoint();
        QTest::touchEvent(window, finger).press(0, from, window);
        for (int step = 1; step <= 10; ++step) {
            QTest::qWait(16);
            QTest::touchEvent(window, finger).move(0, from - QPoint(0, step * 15), window);
        }
        QTest::touchEvent(window, finger).release(0, from - QPoint(0, 150), window);
        QTest::qWait(50);
        QVERIFY2(panned.isEmpty(), "the map moved under the modal");
        QVERIFY(root->markers.isEmpty());  // a drag is no tap
    }
    QVERIFY(QMetaObject::invokeMethod(markerCell, "clicked"));
    QCOMPARE(root->markers.size(), 1);
    QTRY_VERIFY(!picker->property("visible").toBool());
    canvas->fit();
    QTest::qWait(50);
    const QImage withMarker = window->grabWindow();
    const MapSnapshot::Topic *marked = map->snapshot()->topicOf(root);
    QVERIFY(marked && !marked->geometry->markerRects().isEmpty());
    const QRectF markerPlace = marked->geometry->markerRects().front().translated(marked->position);
    const QRect markerShown = canvas->mapRectToScene(QRectF(canvas->itemPoint(markerPlace.topLeft()), canvas->itemPoint(markerPlace.bottomRight())))
                                  .toRect().intersected(withMarker.rect());
    int markerPixels = 0;
    for (int y = markerShown.top(); y <= markerShown.bottom(); ++y)
        for (int x = markerShown.left(); x <= markerShown.right(); ++x) markerPixels += withMarker.pixelColor(x, y) != root->fill;
    QVERIFY2(markerPixels > 0, "the marker is drawn");
    // The desktop's topic tools are in the topic bar at the bottom, after edit and delete
    // (2026-11-11), the tools the phone cannot draw yet disabled; no bar over the topic. Too wide
    // for the page, the bar takes two rows inside 16 px from the page's edges; otherwise one row.
    QVERIFY(!window->findChild<QQuickItem *>(QStringLiteral("topicTools")));
    QTRY_VERIFY(bar->isVisible());
    // Add child and Add sibling are the knobs only (2026-11-11).
    QVERIFY(!window->findChild<QObject *>(QStringLiteral("addChildButton")));
    QVERIFY(!window->findChild<QObject *>(QStringLiteral("addSiblingButton")));
    for (const char *name : {"selectMoreButton", "renameButton", "deleteButton", "copyTopicsButton", "pasteTopicsButton",
                             "relationButton", "webLinkButton", "imageButton", "noteButton", "checkboxButton", "topicStickerButton", "formatPainterButton"}) {
        auto *button = window->findChild<QQuickItem *>(QLatin1String(name));
        QVERIFY2(button && bar->isAncestorOf(button), name);
    }
    const auto barFrame = [&] { return bar->mapRectToItem(canvas, QRectF(0, 0, bar->width(), bar->height())).adjusted(4, 4, -4, -4); };
    QVERIFY(barFrame().left() >= 16 - 0.5 && barFrame().right() <= canvas->width() - 16 + 0.5);
    // The visible buttons 2 px apart, as on the desktop (2026-11-11): 32 px buttons every 34 px, the
    // first one 4 px inside the frame.
    auto *renameItem = window->findChild<QQuickItem *>(QStringLiteral("renameButton"));
    auto *stickerItem = window->findChild<QQuickItem *>(QStringLiteral("topicStickerButton"));
    auto *painterItem = window->findChild<QQuickItem *>(QStringLiteral("formatPainterButton"));
    QCOMPARE(painterItem->x() - stickerItem->x(), 34.0);
    // FEAT-111: Select more is the first button.
    QCOMPARE(window->findChild<QQuickItem *>(QStringLiteral("selectMoreButton"))->mapToItem(canvas, QPointF(1, 0)).x() - barFrame().left(), 4.0);
    // With room the bar takes one row; a narrower page as a phone's two (resizing the window would
    // loop the dialogs' implicit heights). Since Copy and Paste (2026-11-12) the test page is narrower
    // than the bar.
    const QVariant pageWidth = bar->property("maximumWidth");
    QVERIFY(bar->setProperty("maximumWidth", 1000));
    QTRY_COMPARE(bar->height(), 48.0);
    QVERIFY(bar->setProperty("maximumWidth", 340));  // 9 buttons in a row: 9 below, 9 above (FEAT-116: 18 buttons)
    QTRY_COMPARE(bar->height(), 2 * 48.0);
    // The bottom row is never the emptier one, the buttons in reading order (2026-11-11).
    int topRow = 0, bottomRow = 0;
    for (QQuickItem *button : renameItem->parentItem()->childItems())
        if (button->isVisible()) ++(button->y() == 0 ? topRow : bottomRow);
    QVERIFY2(bottomRow >= topRow, qPrintable(QStringLiteral("%1 above, %2 below").arg(topRow).arg(bottomRow)));
    QCOMPARE(renameItem->y(), 0.0);
    QCOMPARE(window->findChild<QQuickItem *>(QStringLiteral("formatPainterButton"))->y(), 48.0);
    QVERIFY(bar->setProperty("maximumWidth", pageWidth));
    QTRY_COMPARE(bar->height(), 2 * 48.0);
    for (const char *name : {"relationButton", "webLinkButton", "imageButton", "noteButton", "checkboxButton", "topicStickerButton", "formatPainterButton"})
        QVERIFY2(window->findChild<QObject *>(QLatin1String(name))->property("enabled").toBool(), name);
    // FEAT-110: a press on a disabled button (Delete on the central topic) stays in the bar: the
    // topic stays selected (it reached the map and deselected it, user's report 2026-11-12).
    {
        auto *remove = window->findChild<QQuickItem *>(QStringLiteral("deleteButton"));
        QVERIFY(map->selectedIsCentral() && !remove->isEnabled());
        const QPoint at = remove->mapToScene(QPointF(remove->width() / 2, remove->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, {}, at);
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);  // the tap is reported after the double tap time
        QCOMPARE(map->selectedId(), map->document()->model()->root()->id);
        QVERIFY(bar->isVisible());
    }
    // FEAT-110: Relationship as the format painter: a tap arms it, a second one at once keeps it on.
    QObject *relationButton = window->findChild<QObject *>(QStringLiteral("relationButton"));
    QVERIFY(QMetaObject::invokeMethod(relationButton, "clicked"));
    QVERIFY(map->relationTool());
    QVERIFY(relationButton->property("checked").toBool());
    QVERIFY(QMetaObject::invokeMethod(relationButton, "clicked"));
    QVERIFY(map->relationToolKept());
    map->stopRelation();
    QVERIFY(!relationButton->property("checked").toBool());
    // A relationship made by a tap, selected by a tap on its line: the bar holds Rename and Delete
    // only; its label is typed in the page's field (then emptied), one step each.
    {
        MindMapModel *model = map->document()->model();
        const QString start = map->selectedId();
        const QString target = model->root()->children.front()->id;
        map->startRelation(false);
        const QPointF on = canvas->itemPoint(map->snapshot()->topicOf(model->nodeById(target))->bounds.center());
        canvas->tapAt(on.x(), on.y());
        QCOMPARE(model->relations.size(), 1);
        QCOMPARE(model->relations.front().from, start);
        const QPointF middle = map->snapshot()->relations().front().geometry.path.pointAtPercent(0.5);
        QVERIFY(!map->snapshot()->topicAt(middle));
        const QPointF onLine = canvas->itemPoint(middle);
        canvas->tapAt(onLine.x(), onLine.y());
        QCOMPARE(map->selectedRelationId(), model->relations.front().id);
        QTRY_VERIFY(bar->isVisible());
        QVERIFY(window->findChild<QQuickItem *>(QStringLiteral("renameButton"))->isVisible());
        QVERIFY(window->findChild<QQuickItem *>(QStringLiteral("deleteButton"))->isEnabled());
        QVERIFY(!window->findChild<QQuickItem *>(QStringLiteral("relationButton"))->isVisible());
        QVERIFY(!window->findChild<QQuickItem *>(QStringLiteral("noteButton"))->isVisible());
        // The curve's handles (2026-11-12): a tap on one keeps the selection; a drag from 10 px
        // beside the target's handle bends the line, the handle keeping its distance from the
        // finger; lifting it is one Undo step. Off the handles a drag pans.
        {
            QVERIFY(!model->relations.front().toControl);
            const QPointF handle = canvas->itemPoint(map->snapshot()->relations().front().geometry.toControl);
            canvas->tapAt(handle.x(), handle.y());
            QCOMPARE(map->selectedRelationId(), model->relations.front().id);
            QVERIFY(!canvas->beginRelationBend(handle.x() + 200, handle.y() + 200));
            const QPointF press = handle + QPointF(10, 0);
            QVERIFY(canvas->beginRelationBend(press.x(), press.y()));
            QVERIFY(canvas->moving());
            canvas->moveTo(press.x() + 30, press.y() + 20);
            QVERIFY(model->relations.front().label.isEmpty() && !model->relations.front().toControl);  // written when lifted
            canvas->endMove(true);
            QVERIFY(!canvas->moving());
            const Relationships::Geometry &bent = map->snapshot()->relations().front().geometry;
            QVERIFY(model->relations.front().toControl);
            QCOMPARE(*model->relations.front().toControl, canvas->mapPoint(handle + QPointF(30, 20)) - bent.toCentre);
            QVERIFY(!model->relations.front().fromControl);
            QCOMPARE(map->selectedRelationId(), model->relations.front().id);
            QVERIFY(map->undo());
            QVERIFY(!model->relations.front().toControl);
            QVERIFY(map->redo());
            QVERIFY(model->relations.front().toControl);
            QVERIFY(map->undo());
            QCOMPARE(map->selectedRelationId(), model->relations.front().id);  // Undo keeps it selected
        }
        // The empty label's placeholder is drawn while selected, and goes while its label is typed
        // (2026-11-12: it stayed visible under the field). The field is hidden for the comparison;
        // the view, centred by beginEdit, stays still between the two frames.
        QVERIFY(QMetaObject::invokeMethod(page, "beginEdit", Q_ARG(QVariant, false)));
        QTRY_VERIFY(field->isVisible());
        field->setOpacity(0);
        const MapSnapshot::Relation &drawn = map->snapshot()->relations().front();
        const QRectF placeholderBox = Relationships::labelRect(
            drawn.geometry.labelPoint, Relationships::labelSize(drawn.look.font, QStringLiteral("Relationship")));
        const QRect labelBox =
            canvas->mapRectToScene(QRectF(canvas->itemPoint(placeholderBox.topLeft()), canvas->itemPoint(placeholderBox.bottomRight())))
                .toRect();
        QTest::qWait(50);
        const QImage whileTyping = window->grabWindow().copy(labelBox);
        QVERIFY(QMetaObject::invokeMethod(page, "endEdit"));  // still empty: no step
        field->setOpacity(1);
        QTest::qWait(50);
        QVERIFY(window->grabWindow().copy(labelBox) != whileTyping);  // the placeholder is back
        QVERIFY(QMetaObject::invokeMethod(page, "beginEdit", Q_ARG(QVariant, false)));
        QTRY_VERIFY(field->isVisible());
        QVERIFY(field->setProperty("text", QStringLiteral("because")));
        QVERIFY(model->relations.front().label.isEmpty());  // written when the editing ends
        QVERIFY(QMetaObject::invokeMethod(page, "endEdit"));
        QCOMPARE(model->relations.front().label, QStringLiteral("because"));
        QVERIFY(QMetaObject::invokeMethod(page, "beginEdit", Q_ARG(QVariant, false)));
        QCOMPARE(field->property("text").toString(), QStringLiteral("because"));
        QVERIFY(field->setProperty("text", QString()));
        QVERIFY(QMetaObject::invokeMethod(page, "endEdit"));
        QVERIFY(model->relations.front().label.isEmpty());
        QVERIFY(map->undo());
        QCOMPARE(model->relations.front().label, QStringLiteral("because"));
        QVERIFY(map->removeSelected());
        QVERIFY(model->relations.isEmpty());
        map->select(start);
    }
    QObject *painter = window->findChild<QObject *>(QStringLiteral("formatPainterButton"));
    QVERIFY(QMetaObject::invokeMethod(painter, "clicked"));
    QVERIFY(map->formatPainter());
    QVERIFY(painter->property("checked").toBool());
    QVERIFY(QMetaObject::invokeMethod(painter, "clicked"));  // a second tap at once keeps it on
    QVERIFY(map->formatPainterSticky());
    map->stopFormatPainter();
    QVERIFY(!painter->property("checked").toBool());
    QVERIFY(QMetaObject::invokeMethod(page, "beginEdit", Q_ARG(QVariant, false)));
    QTRY_VERIFY(!bar->isVisible());
    QVERIFY(QMetaObject::invokeMethod(page, "endEdit"));
    QTRY_VERIFY(bar->isVisible());
    // A hardware keyboard: the desktop's keys (FEAT-041) for the phone's commands. Every command the
    // page binds is in the shared list.
    {
        ShortcutRegistry registry;
        ShortcutCatalog::addAll(registry);
        int bound = 0;
        const std::function<void(QObject *)> check = [&](QObject *object) {
            if (const QVariant command = object->property("command"); command.isValid() && object->inherits("QQuickShortcut")) {
                QVERIFY2(registry.contains(command.toString()), qPrintable(command.toString()));
                ++bound;
            }
            for (QObject *child : object->children()) check(child);
        };
        check(page);
        QVERIFY(bound > 20);
    }
    window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(window));
    const int before = map->topicCount();
    map->select(root->id);
    QTest::keyClick(window, Qt::Key_Tab);  // Add child, its title in editing
    QCOMPARE(map->topicCount(), before + 1);
    QTRY_VERIFY(field->isVisible());
    QTest::keyClick(window, Qt::Key_Tab);  // typed in the title: no second child
    QCOMPARE(map->topicCount(), before + 1);
    QVERIFY(field->property("text").toString().contains(QLatin1Char('	')));
    QTest::keyClick(window, Qt::Key_Return);  // confirms the title
    QTRY_VERIFY(!field->isVisible());
    const QString added = map->selectedId();
    QVERIFY(added != root->id);
    // The arrow towards the central topic goes back to it.
    const bool addedLeft = map->snapshot()->topicOf(map->document()->model()->nodeById(added))->position.x()
        < map->snapshot()->topicOf(root)->position.x();
    QTest::keyClick(window, addedLeft ? Qt::Key_Right : Qt::Key_Left);
    QCOMPARE(map->selectedId(), root->id);
    map->select(added);
    QTest::keyClick(window, Qt::Key_F2);
    QTRY_VERIFY(field->isVisible());
    QVERIFY(QMetaObject::invokeMethod(page, "endEdit"));
    QTest::keyClick(window, Qt::Key_Delete);
    QCOMPARE(map->topicCount(), before);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(map->topicCount(), before + 1);
    QTest::keyClick(window, Qt::Key_R, Qt::ControlModifier);
    QCOMPARE(map->selectedId(), root->id);
    const qreal zoom = canvas->zoom();
    QTest::keyClick(window, Qt::Key_Minus, Qt::ControlModifier);
    QVERIFY(canvas->zoom() < zoom);
    canvas->zoomStep(200);
    QTest::keyClick(window, Qt::Key_0, Qt::ControlModifier);  // fit
    const qreal fitted = canvas->zoom();
    canvas->zoomStep(100);
    canvas->fit();
    QCOMPARE(canvas->zoom(), fitted);
    // Ctrl+R shows the central topic once the view has moved off it.
    canvas->zoomStep(50);
    canvas->panBy(canvas->width() * 3, 0);
    QTest::keyClick(window, Qt::Key_R, Qt::ControlModifier);
    const QRectF centreBox = map->snapshot()->topicOf(root)->bounds;
    const QRectF shownBox(canvas->itemPoint(centreBox.topLeft()), canvas->itemPoint(centreBox.bottomRight()));
    QVERIFY(QRectF(0, 0, canvas->width(), canvas->height()).contains(shownBox));
    // An arrow to a topic out of view scrolls the map until it shows between the top groups and
    // the topic bar (user's request 2026-11-10), which the band leaves out.
    auto *history = window->findChild<QQuickItem *>(QStringLiteral("historyGroup"));
    QVERIFY(history);
    QTRY_VERIFY(bar->isVisible());
    QVERIFY(qAbs(canvas->bandTop() - (history->y() + history->height() - 4)) < 0.5);
    QVERIFY(qAbs(canvas->bandBottom() - (bar->y() + 4)) < 0.5);
    const auto inBand = [&](const MindNode *node) {
        const QRectF box = map->snapshot()->topicOf(node)->bounds;
        const QRectF item(canvas->itemPoint(box.topLeft()), canvas->itemPoint(box.bottomRight()));
        return item.left() >= 0 && item.right() <= canvas->width() && item.top() >= canvas->bandTop() - 0.5
            && item.bottom() <= canvas->bandBottom() + 0.5;
    };
    for (const auto &child : root->children) {
        map->select(root->id);
        canvas->revealSelected();
        const bool left = map->snapshot()->topicOf(child.get())->position.x() < map->snapshot()->topicOf(root)->position.x();
        // Zoomed in and moved away from the child: the child is off the view.
        canvas->zoomStep(30);
        canvas->panBy(left ? -1.5 * canvas->width() : 1.5 * canvas->width(), 0);
        QVERIFY2(!inBand(child.get()), qPrintable(child->title));
        QTest::keyClick(window, left ? Qt::Key_Left : Qt::Key_Right);
        const MindNode *reached = map->document()->model()->nodeById(map->selectedId());
        QVERIFY(reached && reached->parent == root);
        QVERIFY2(inBand(reached), qPrintable(reached->title));
        QTest::keyClick(window, Qt::Key_0, Qt::ControlModifier);
    }
    // A topic under the topic bar is out of view: revealing it brings it above the bar.
    map->select(root->children.back()->id);
    canvas->revealSelected();
    QVERIFY(inBand(root->children.back().get()));
    const QRectF last = map->snapshot()->topicOf(root->children.back().get())->bounds;
    // Its top just under the bar's top: on the screen, but covered.
    canvas->panBy(0, canvas->bandBottom() - canvas->itemPoint(last.topLeft()).y() + 2);
    QVERIFY(!inBand(root->children.back().get()));
    canvas->revealSelected();
    QVERIFY(inBand(root->children.back().get()));
    QTest::keyClick(window, Qt::Key_0, Qt::ControlModifier);
    // A modal over the map stops the keys.
    QTest::keyClick(window, Qt::Key_M, Qt::ControlModifier | Qt::AltModifier);
    QTRY_VERIFY(picker->property("opened").toBool());
    QTest::keyClick(window, Qt::Key_Tab);
    QCOMPARE(map->topicCount(), before + 1);
    QMetaObject::invokeMethod(picker, "close");
    QTRY_VERIFY(!picker->property("visible").toBool());
    // Esc disarms the format painter.
    map->startFormatPainter(false);
    QTest::keyClick(window, Qt::Key_Escape);
    QVERIFY(!map->formatPainter());
    // Android's Back on the map goes back to the Home; the key is taken, so Qt does not quit.
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QVERIFY(stack);
    QCOMPARE(stack->property("depth").toInt(), 2);
    QTest::keyClick(window, Qt::Key_Back);  // through the shortcut map, as the platform's keys
    QTRY_COMPARE(stack->property("depth").toInt(), 1);
    QVERIFY(!map->isOpen());
    // Back to Home: the copy is listed.
    QVERIFY(library->maps().size() >= 1);
    for (const QQmlError &error : std::as_const(errors)) qWarning("%s", qPrintable(error.toString()));
    QVERIFY(errors.isEmpty());
    library->discard(library->maps().constFirst().toMap().value(QStringLiteral("path")).toString());
}

// B3: a topic's note read and edited in the note modal, its icon drawn and tapped; a formatted title
// edited keeping its formatting.
// FEAT-106: a tap on the picture selects it; Remove takes off only the picture (also from the
// central topic); a drag from the selected picture puts it in the zone under the finger; a tap on a
// marker asks for its menu, whose values and Remove change the topic; the note is removed. One Undo
// step each.
// FEAT-105 stage 4: a map given by another app ("Open with", "Share") is copied into the Mentis
// folder and opened; one already open is saved and closed first.
void MobileTest::mapFromAnotherAppIsOpened() {
    QTemporaryDir source;
    const QString first = source.filePath(QStringLiteral("Shared.mentis"));
    const QString second = source.filePath(QStringLiteral("Second.mentis"));
    QCOMPARE(writeTwoSheetMap(first), QString());
    QCOMPARE(writeTwoSheetMap(second), QString());

    QQmlApplicationEngine engine;
    engine.loadFromModule("Mentis.Mobile", "Main");
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window);
    auto *incoming = window->findChild<IncomingMaps *>();
    auto *library = window->findChild<MapLibrary *>();
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QVERIFY(incoming && library && stack);
    const QDir folder(library->folder());
    QCOMPARE(QDir::cleanPath(library->folder()), QDir::cleanPath(MapFolder::current().path()));

    emit incoming->received(QUrl::fromLocalFile(first));
    QTRY_COMPARE(stack->property("depth").toInt(), 2);
    auto *map = window->findChild<MobileMap *>();
    QVERIFY(map && map->isOpen());
    QVERIFY(folder.exists(QStringLiteral("Shared.mentis")));

    map->select(map->document()->model()->root()->id);
    QVERIFY(map->addChild());  // an unsaved change, written when the next map comes
    const int topics = map->topicCount();
    emit incoming->received(QUrl::fromLocalFile(second));
    QTRY_VERIFY(folder.exists(QStringLiteral("Second.mentis")));
    QTRY_COMPARE(stack->property("depth").toInt(), 2);
    QVERIFY(map->isOpen());
    QCOMPARE(map->topicCount(), topics - 1);  // the second map, without the first one's new topic
    MobileMap reopened;
    QVERIFY(reopened.open(folder.filePath(QStringLiteral("Shared.mentis"))));
    QCOMPARE(reopened.topicCount(), topics);
    QVERIFY(reopened.close());
    QVERIFY(map->close());
    // Not on the phone: the access to the shared storage is not asked.
    auto *access = window->findChild<QObject *>(QStringLiteral("folderAccessDialog"));
    QVERIFY(access);
    QVERIFY(!access->property("visible").toBool());

    // The Mentis folder changes (on the phone: the access to all files given): the maps move there.
    QTemporaryDir target;
    MapFolder::setConfiguredPath(target.path());
    QVERIFY(library->reload() >= 2);  // with the maps other tests left in the folder
    MapFolder::setConfiguredPath(QString());
    QCOMPARE(QDir::cleanPath(library->folder()), QDir::cleanPath(target.path()));
    QVERIFY(QFileInfo::exists(QDir(target.path()).filePath(QStringLiteral("Shared.mentis"))));
    QVERIFY(QFileInfo::exists(QDir(target.path()).filePath(QStringLiteral("Second.mentis"))));
    QVERIFY(!folder.exists(QStringLiteral("Shared.mentis")));
    QVERIFY(library->maps().size() >= 2);
}

void MobileTest::topicContentsAreRemovedAndPlaced() {
    const QString sticker = StickerLibrary().collections().constFirst().toMap().value(QStringLiteral("items")).toList()
                                .constFirst().toMap().value(QStringLiteral("path")).toString();
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Contents.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MapCanvas canvas;
    canvas.setSize(QSizeF(400, 800));
    canvas.setMap(&map);
    const MindNode *root = map.document()->model()->root();
    const MindNode *first = childTitled(root, QStringLiteral("First"));
    map.select(first->id);
    QVERIFY(map.insertSticker(sticker));
    canvas.zoomStep(0);
    canvas.reveal(map.selectedTopic()->bounds);
    const auto pictureOf = [&](const MindNode *node) {
        const MapSnapshot::Topic *topic = map.snapshot()->topicOf(node);
        return topic->geometry->imageRect().translated(topic->position);
    };
    const auto tapOn = [&](const QPointF &mapPoint) {
        const QPointF at = canvas.itemPoint(mapPoint);
        canvas.tapAt(at.x(), at.y());
    };

    // Picked by a tap on it; a tap on the title picks only the topic.
    const MapSnapshot::Topic *topic = map.snapshot()->topicOf(first);
    tapOn(topic->geometry->textArea().translated(topic->position).center());
    QCOMPARE(map.selectedId(), first->id);
    QVERIFY(!map.pictureSelected());
    tapOn(pictureOf(first).center());
    QCOMPARE(map.selectedId(), first->id);
    QVERIFY(map.pictureSelected());
    map.select(root->id);  // another topic: its picture is not picked
    QVERIFY(!map.pictureSelected());

    // A drag away from the picture pans the map; from the picture it moves it to the zone under the
    // finger (Below), with the layout in one step; a drop outside the zones changes nothing.
    tapOn(pictureOf(first).center());
    QVERIFY(!canvas.beginPictureDrag(1, 1));
    QPointF at = canvas.itemPoint(pictureOf(first).center());
    QVERIFY(canvas.beginPictureDrag(at.x(), at.y()));
    QVERIFY(canvas.moving());
    topic = map.snapshot()->topicOf(first);
    const QPointF below = topic->geometry->imageDropZones().at(int(MindNode::ImagePlacement::Below)).center() + topic->position;
    at = canvas.itemPoint(below);
    canvas.moveTo(at.x(), at.y());
    canvas.endMove(true);
    QVERIFY(!canvas.moving());
    QCOMPARE(first->imagePlacement, MindNode::ImagePlacement::Below);
    QVERIFY(map.pictureSelected());  // it stays picked
    QVERIFY(pictureOf(first).top() > map.snapshot()->topicOf(first)->geometry->textArea().translated(map.snapshot()->topicOf(first)->position).top());
    QVERIFY(!map.placePicture(first->id, QPointF(-1e6, -1e6)));
    QVERIFY(map.undo());
    QCOMPARE(first->imagePlacement, MindNode::ImagePlacement::Above);
    // A drag from a corner of the picked picture resizes it with its proportions, one Undo step;
    // a tap on a corner keeps the picture picked.
    tapOn(pictureOf(first).center());
    const QSizeF startSize = first->imageSize;
    at = canvas.itemPoint(pictureOf(first).bottomRight());
    QVERIFY(canvas.beginPictureResize(at.x() - 5, at.y() + 5));
    canvas.endMove(false);
    QCOMPARE(first->imageSize, startSize);
    QVERIFY(!canvas.beginPictureResize(1, 1));
    QVERIFY(canvas.beginPictureResize(at.x(), at.y()));
    canvas.moveTo(at.x() + 20 * canvas.zoom(), at.y());
    QCOMPARE(first->imageSize, startSize);  // applied on the drop only
    canvas.endMove(true);
    QCOMPARE(first->imageSize.width(), startSize.width() + 40);
    QCOMPARE(first->imageSize.height(), std::round((startSize.width() + 40) * startSize.height() / startSize.width()));
    QVERIFY(map.pictureSelected());
    at = canvas.itemPoint(pictureOf(first).topLeft());
    canvas.tapAt(at.x(), at.y());
    QVERIFY(map.pictureSelected());
    QVERIFY(map.undo());
    QCOMPARE(first->imageSize, startSize);
    // A long press on the picked picture picks up the picture, not the topic (B2).
    tapOn(pictureOf(first).center());
    at = canvas.itemPoint(pictureOf(first).center());
    QVERIFY(canvas.beginMove(at.x(), at.y()));
    canvas.endMove(false);
    QCOMPARE(first->parent, root);

    // Remove with the picture picked takes off only the picture.
    tapOn(pictureOf(first).center());
    QVERIFY(map.removeSelected());
    QVERIFY(first->imagePath.isEmpty());
    QCOMPARE(map.selectedId(), first->id);
    QVERIFY(!map.pictureSelected());
    QVERIFY(map.undo());
    QVERIFY(!first->imagePath.isEmpty());
    QVERIFY(!map.pictureSelected());  // Undo does not pick it again
    // From the central topic too, which is never removed itself.
    map.select(root->id);
    QVERIFY(map.insertSticker(sticker));
    tapOn(pictureOf(root).center());
    QVERIFY(map.pictureSelected());
    QVERIFY(map.removeSelected());
    QVERIFY(root->imagePath.isEmpty());
    QVERIFY(!map.removeSelected());
    QVERIFY(map.document()->model()->root() == root);

    // A tap on a marker asks for its menu; its series' values, the topic's one checked.
    map.select(first->id);
    QVERIFY(map.setMarker(QStringLiteral("priority:1")));
    QSignalSpy markerRequested(&canvas, &MapCanvas::markerRequested);
    QSignalSpy noteRequested(&canvas, &MapCanvas::noteRequested);
    topic = map.snapshot()->topicOf(first);
    tapOn(topic->geometry->markerRects().constFirst().translated(topic->position).center());
    QCOMPARE(markerRequested.count(), 1);
    QCOMPARE(markerRequested.constFirst().constFirst().toString(), QStringLiteral("priority:1"));
    QCOMPARE(noteRequested.count(), 0);
    const QVariantList choices = map.markerChoices(QStringLiteral("priority:1"));
    QVERIFY(choices.size() > 2);
    QCOMPARE(choices.constFirst().toMap().value(QStringLiteral("marker")).toString(), QStringLiteral("priority:1"));
    QVERIFY(choices.constFirst().toMap().value(QStringLiteral("checked")).toBool());
    QVERIFY(!choices.at(1).toMap().value(QStringLiteral("checked")).toBool());
    QVERIFY(map.setMarker(choices.at(2).toMap().value(QStringLiteral("marker")).toString()));
    QCOMPARE(first->markers, QStringList{QStringLiteral("priority:3")});
    QVERIFY(map.removeMarker(QStringLiteral("priority:3")));
    QVERIFY(first->markers.isEmpty());
    QVERIFY(!map.removeMarker(QStringLiteral("priority:3")));
    QVERIFY(map.undo());
    QCOMPARE(first->markers, QStringList{QStringLiteral("priority:3")});
    // An own marker's group has no values: its menu has only Remove.
    QVERIFY(map.markerChoices(QStringLiteral("custom:group:00")).isEmpty());

    // The note removed with its icon, in one step.
    QVERIFY(!map.selectedHasNote());
    QVERIFY(!map.removeNote());
    QVERIFY(map.document()->setNote(const_cast<MindNode *>(first), QStringLiteral("Note"), QString()));
    map.document()->flushPendingStep();
    QVERIFY(map.selectedHasNote());
    QVERIFY(map.removeNote());
    QVERIFY(first->notes.isEmpty());
    QVERIFY(!map.selectedHasNote());
    QVERIFY(map.undo());
    QCOMPARE(first->notes, QStringLiteral("Note"));
    QVERIFY(map.close());
}

// FEAT-106 on the page: the marker's menu opens from the canvas's request and its rows change the
// topic; the note modal's Remove shows only for a topic with a note; the topic bar's Delete says
// Remove while the picture is picked and is enabled for the central topic's picture.
void MobileTest::markerMenuOnThePage() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Menu.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *library = window->findChild<MapLibrary *>();
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *canvas = nullptr;
    QTRY_VERIFY((canvas = window->findChild<MapCanvas *>()));
    QTRY_VERIFY(canvas->width() > 0 && canvas->map()->isOpen());
    MobileMap *map = canvas->map();
    const QString copy = map->document()->filePath();
    const auto cleanup = qScopeGuard([&] { map->close(); library->discard(copy); });
    const MindNode *root = map->document()->model()->root();
    const MindNode *first = childTitled(root, QStringLiteral("First"));
    QObject *menu = window->findChild<QObject *>(QStringLiteral("markerMenu"));
    QObject *dialog = window->findChild<QObject *>(QStringLiteral("noteDialog"));
    QVERIFY(menu && dialog);

    map->select(first->id);
    QVERIFY(map->setMarker(QStringLiteral("priority:1")));
    emit canvas->markerRequested(QStringLiteral("priority:1"), 100, 100);
    QTRY_VERIFY(menu->property("opened").toBool());
    QList<QObject *> rows;
    // A Popup's rows are visual children of its content item (a Repeater's are not its QObject children).
    auto *menuContent = menu->property("contentItem").value<QQuickItem *>();
    QVERIFY(menuContent);
    const auto rowsNamed = [&](const QString &name) {
        QList<QObject *> found;
        for (QQuickItem *item : menuContent->childItems()) if (item->objectName() == name) found.append(item);
        return found;
    };
    QTRY_VERIFY((rows = rowsNamed(QStringLiteral("markerChoice"))).size() > 2);
    QVERIFY(QMetaObject::invokeMethod(rows.at(1), "clicked"));
    QTRY_VERIFY(!menu->property("visible").toBool());
    QCOMPARE(first->markers, QStringList{QStringLiteral("priority:2")});
    emit canvas->markerRequested(QStringLiteral("priority:2"), 100, 100);
    QTRY_VERIFY(menu->property("opened").toBool());
    QObject *remove = menuContent->findChild<QObject *>(QStringLiteral("removeMarkerButton"));
    QVERIFY(remove && QMetaObject::invokeMethod(remove, "clicked"));
    QTRY_VERIFY(first->markers.isEmpty());

    // The note modal's Remove: hidden without a note, removing it with one.
    QObject *removeNote = window->findChild<QObject *>(QStringLiteral("noteRemoveButton"));
    QVERIFY(removeNote);
    QVERIFY(!removeNote->property("visible").toBool());
    QVERIFY(map->document()->setNote(const_cast<MindNode *>(first), QStringLiteral("Note"), QString()));
    map->document()->flushPendingStep();
    map->select(root->id);
    map->select(first->id);  // the properties read again
    QVERIFY(QMetaObject::invokeMethod(dialog, "openNote"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(removeNote->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(removeNote, "clicked"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(first->notes.isEmpty());

    // The topic bar's Delete for a picked picture of the central topic.
    QObject *deleteButton = window->findChild<QObject *>(QStringLiteral("deleteButton"));
    QVERIFY(deleteButton);
    map->select(root->id);
    QVERIFY(!deleteButton->property("enabled").toBool());
    const QString sticker = StickerLibrary().collections().constFirst().toMap().value(QStringLiteral("items")).toList()
                                .constFirst().toMap().value(QStringLiteral("path")).toString();
    QVERIFY(map->insertSticker(sticker));
    const MapSnapshot::Topic *topic = map->snapshot()->topicOf(root);
    map->selectAt(topic->geometry->imageRect().translated(topic->position).center());
    QVERIFY(map->pictureSelected());
    // With the picture picked the bar holds only Remove, Image (replace it) and Copy.
    QStringList shown;
    for (QQuickItem *button : window->findChild<QQuickItem *>(QStringLiteral("topicBar"))->findChildren<QQuickItem *>())
        if (button->objectName().endsWith(QStringLiteral("Button")) && button->isVisible()) shown.append(button->objectName());
    shown.sort();
    QCOMPARE(shown, (QStringList{QStringLiteral("copyPictureButton"), QStringLiteral("deleteButton"), QStringLiteral("imageButton")}));
    QCOMPARE(window->findChild<QObject *>(QStringLiteral("imageButton"))->property("text").toString(), QStringLiteral("Replace the image"));
    QGuiApplication::clipboard()->clear();
    QVERIFY(QMetaObject::invokeMethod(window->findChild<QObject *>(QStringLiteral("copyPictureButton")), "clicked"));
    QVERIFY(!QGuiApplication::clipboard()->image().isNull());
    QCOMPARE(map->message(), QStringLiteral("Image copied"));
    QVERIFY(deleteButton->property("enabled").toBool());
    QCOMPARE(deleteButton->property("text").toString(), QStringLiteral("Remove"));
    QVERIFY(QMetaObject::invokeMethod(deleteButton, "clicked"));
    QVERIFY(root->imagePath.isEmpty());
    QCOMPARE(deleteButton->property("text").toString(), QStringLiteral("Delete"));
    for (const QQmlError &error : std::as_const(errors)) qWarning() << error.toString();
    QVERIFY(errors.isEmpty());
}

void MobileTest::notesAndRichTitles() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Notes.mentis"));
    {
        Document document;
        document.startUntitled();
        MindNode *root = document.model()->root();
        MindNode *noted = document.addChild(root, QStringLiteral("Noted"));
        MindNode *rich = document.addChild(root, QStringLiteral("Bold title"));
        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);
        QTextDocument note;
        QTextCursor cursor(&note);
        cursor.insertText(QStringLiteral("Strong"), bold);
        cursor.insertText(QStringLiteral(" plain"), QTextCharFormat());
        QVERIFY(document.setNote(noted, note.toPlainText(), note.toHtml()));
        QTextDocument title;
        QTextCursor titleCursor(&title);
        titleCursor.insertText(QStringLiteral("Bold"), bold);
        titleCursor.insertText(QStringLiteral(" title"), QTextCharFormat());
        QVERIFY(document.setFormattedTitle(rich, title.toPlainText(), title.toHtml()));
        { const MapSnapshot laidOut(document.model()); }
        QVERIFY(document.save(path, BmapStorage::SaveReason::Explicit).ok);
        document.close();
    }
    // The character at `position` is bold.
    const auto boldAt = [](QTextDocument *document, int position) {
        QTextCursor cursor(document);
        cursor.setPosition(position + 1);
        return cursor.charFormat().fontWeight() >= QFont::Bold;
    };

    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *library = window->findChild<MapLibrary *>();
    QVERIFY(library);
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *canvas = nullptr;
    QTRY_VERIFY((canvas = window->findChild<MapCanvas *>()));
    QTRY_VERIFY(canvas->width() > 0 && canvas->map()->isOpen());
    MobileMap *map = canvas->map();
    const QString copy = map->document()->filePath();
    const auto cleanup = qScopeGuard([&] { map->close(); library->discard(copy); });
    const MindNode *root = map->document()->model()->root();
    const MindNode *noted = childTitled(root, QStringLiteral("Noted"));
    const MindNode *rich = childTitled(root, QStringLiteral("Bold title"));
    QVERIFY(noted && rich);
    QObject *page = window->findChild<QObject *>(QStringLiteral("mapPage"));
    QObject *dialog = window->findChild<QObject *>(QStringLiteral("noteDialog"));
    auto *noteText = window->findChild<QQuickItem *>(QStringLiteral("noteText"));
    QVERIFY(page && dialog && noteText);
    QTextDocument *field = noteText->property("textDocument").value<QQuickTextDocument *>()->textDocument();
    const auto click = [&](const char *name) {
        QObject *button = window->findChild<QObject *>(QLatin1String(name));
        return button && QMetaObject::invokeMethod(button, "clicked");
    };

    // The icon is drawn where the desktop puts it and a tap on it opens the note; a tap on the topic
    // away from the icon only selects it.
    const MapSnapshot::Topic *topic = map->snapshot()->topicOf(noted);
    const QRectF icon = topic->geometry->noteRect().translated(topic->position);
    QVERIFY(!icon.isEmpty());
    QVERIFY(map->snapshot()->topicOf(rich)->geometry->noteRect().isEmpty());
    canvas->zoomStep(0);
    canvas->reveal(topic->bounds);
    {
        // The button's white over the topic's fill, beside the icon's stroke. Under load the first
        // frames may still show the Home page: wait for the map's.
        QVERIFY2(QColor(noted->fill).lightness() < 230, "the topic's own fill must differ from the button");
        const QPointF corner = canvas->mapToScene(canvas->itemPoint(icon.topLeft() + QPointF(2, 2)));
        QColor drawn;
        QTRY_VERIFY2((drawn = [&] {
                          const QImage frame = window->grabWindow();
                          return frame.pixelColor((corner * frame.devicePixelRatio()).toPoint());
                      }()).lightness() > 230,
                     qPrintable(drawn.name()));
    }
    QSignalSpy noteRequested(canvas, &MapCanvas::noteRequested);
    const QPointF onTopicOnly = canvas->itemPoint(QPointF(topic->bounds.left() + 6, topic->bounds.center().y()));
    canvas->tapAt(onTopicOnly.x(), onTopicOnly.y());
    QCOMPARE(map->selectedId(), noted->id);
    QCOMPARE(noteRequested.count(), 0);
    // Zoomed far out the icon's 48 px target would cover the whole topic: a tap on the title still
    // only selects.
    canvas->zoomStep(0);
    canvas->zoomStep(-75);  // the smallest zoom, 25%
    QVERIFY(canvas->zoom() < 0.26);
    QVERIFY(MapCanvas::kKnobTargetPx / canvas->zoom() > topic->bounds.width());
    const QPointF onTitle = canvas->itemPoint(topic->geometry->textArea().translated(topic->position).center());
    canvas->tapAt(onTitle.x(), onTitle.y());
    QCOMPARE(map->selectedId(), noted->id);
    QCOMPARE(noteRequested.count(), 0);
    canvas->zoomStep(0);
    canvas->reveal(topic->bounds);
    const QPointF onIcon = canvas->itemPoint(icon.center());
    canvas->tapAt(onIcon.x(), onIcon.y());
    QCOMPARE(noteRequested.count(), 1);
    QTRY_VERIFY(dialog->property("opened").toBool());
    // Read: the note's text with its bold kept, and no keyboard asked.
    QCOMPARE(field->toPlainText(), QStringLiteral("Strong plain"));
    QVERIFY(boldAt(field, 0));
    QVERIFY(!boldAt(field, 7));
    QVERIFY(!noteText->hasActiveFocus());
    // Closed without editing: nothing changes, no Undo step.
    const QString htmlBefore = noted->notesHtml;
    const bool couldUndo = map->canUndo();
    QVERIFY(click("noteDoneButton"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(map->canUndo(), couldUndo);
    QCOMPARE(noted->notesHtml, htmlBefore);

    // Edited from the topic bar's Note: typed text takes the format at the cursor, the rest is kept;
    // Done writes it in one Undo step.
    QVERIFY(click("noteButton"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    {
        QTextCursor cursor(field);
        cursor.setPosition(3);  // inside "Strong"
        cursor.insertText(QStringLiteral("XY"));
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(QStringLiteral(" more"));
    }
    QVERIFY(click("noteDoneButton"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(noted->notes, QStringLiteral("StrXYong plain more"));
    {
        QTextDocument saved;
        saved.setHtml(noted->notesHtml);
        QVERIFY(boldAt(&saved, 3));    // typed in the bold word: bold
        QVERIFY(!boldAt(&saved, 15));  // typed after the plain one: plain
    }
    QVERIFY(map->undo());
    QCOMPARE(noted->notes, QStringLiteral("Strong plain"));
    QVERIFY(map->redo());

    // Cancel leaves the note as it was; the desktop's key (Ctrl+Shift+N) opens it too.
    map->select(noted->id);
    QTest::keyClick(window, Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_VERIFY(dialog->property("opened").toBool());
    QTextCursor(field).insertText(QStringLiteral("Lost "));
    QVERIFY(click("noteCancelButton"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(noted->notes, QStringLiteral("StrXYong plain more"));

    // Emptied: the note is removed with its icon, in one step. Closing (Back, Esc) writes as Done.
    QVERIFY(QMetaObject::invokeMethod(page, "openNote"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    {
        QTextCursor cursor(field);
        cursor.select(QTextCursor::Document);
        cursor.removeSelectedText();
    }
    QTRY_VERIFY(window->findChild<QQuickItem *>(QStringLiteral("notePlaceholder"))->isVisible());
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(noted->notes.isEmpty() && noted->notesHtml.isEmpty());
    QVERIFY(map->snapshot()->topicOf(noted)->geometry->noteRect().isEmpty());
    QVERIFY(map->undo());
    QCOMPARE(noted->notes, QStringLiteral("StrXYong plain more"));

    // A note written on a topic without one.
    map->select(rich->id);
    QVERIFY(QMetaObject::invokeMethod(page, "openNote"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(field->toPlainText().isEmpty());
    QTextCursor(field).insertText(QStringLiteral("New note"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(rich->notes, QStringLiteral("New note"));
    QVERIFY(!map->snapshot()->topicOf(rich)->geometry->noteRect().isEmpty());

    // A formatted title is edited keeping its formatting: the text typed at the end takes the plain
    // format there, the bold word stays bold; one Undo step.
    auto *titleEditor = window->findChild<QQuickItem *>(QStringLiteral("titleEditor"));
    QVERIFY(titleEditor);
    auto *titleDocument = titleEditor->property("textDocument").value<QQuickTextDocument *>();
    QTextDocument *titleField = titleDocument->textDocument();
    QVERIFY(map->selectedTitleRich());
    QVERIFY(QMetaObject::invokeMethod(page, "beginEdit", Q_ARG(QVariant, true)));
    QTRY_VERIFY(titleEditor->isVisible());
    QCOMPARE(titleField->toPlainText(), QStringLiteral("Bold title"));
    QVERIFY(boldAt(titleField, 0));
    QTest::keyClick(window, 's');
    QTRY_COMPARE(rich->title, QStringLiteral("Bold titles"));
    QVERIFY(QMetaObject::invokeMethod(page, "endEdit"));
    {
        QTextDocument saved;
        saved.setHtml(rich->titleHtml);
        QCOMPARE(saved.toPlainText(), QStringLiteral("Bold titles"));
        QVERIFY(boldAt(&saved, 0));
        QVERIFY(!boldAt(&saved, 9));
    }
    QVERIFY(map->undo());
    QCOMPARE(rich->title, QStringLiteral("Bold title"));
    QVERIFY(!rich->titleHtml.isEmpty());
    // The word being composed is in the topic already (renameFrom's preedit at the cursor).
    QVERIFY(QMetaObject::invokeMethod(page, "beginEdit", Q_ARG(QVariant, true)));
    QTRY_VERIFY(titleEditor->isVisible());
    QVERIFY(map->renameFrom(titleDocument, QStringLiteral("!!"), 4));
    QCOMPARE(rich->title, QStringLiteral("Bold!! title"));
    QVERIFY(QMetaObject::invokeMethod(page, "endEdit"));
    // A plain title stays plain: no formatting is made for it.
    map->select(noted->id);
    QVERIFY(!map->selectedTitleRich());
    QVERIFY(QMetaObject::invokeMethod(page, "beginEdit", Q_ARG(QVariant, true)));
    QTRY_VERIFY(titleEditor->isVisible());
    QTest::keyClick(window, '!');
    QTRY_COMPARE(noted->title, QStringLiteral("Noted!"));
    QVERIFY(QMetaObject::invokeMethod(page, "endEdit"));
    QVERIFY(noted->titleHtml.isEmpty());

    // Saved and opened again: the note and the formatted title are in the file.
    QVERIFY(map->close());
    {
        MobileMap reopened;
        QVERIFY(reopened.open(copy));
        const MindNode *again = childTitled(reopened.document()->model()->root(), QStringLiteral("Bold!! title"));
        QVERIFY(again);
        QCOMPARE(again->notes, QStringLiteral("New note"));
        QVERIFY(!again->titleHtml.isEmpty());
        QVERIFY(reopened.close());
    }
    for (const QQmlError &error : std::as_const(errors)) qWarning("%s", qPrintable(error.toString()));
    QVERIFY(errors.isEmpty());
}

// FEAT-108: takes the URLs QDesktopServices would give the browser (setUrlHandler).
class UrlCatcher : public QObject {
    Q_OBJECT
public:
    QList<QUrl> urls;
public slots:
    void open(const QUrl &url) { urls.append(url); }
};

void MobileTest::checkboxAndWebLink() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Tasks.mentis"));
    {
        Document document;
        document.startUntitled();
        MindNode *root = document.model()->root();
        document.addChild(root, QStringLiteral("Task"));
        MindNode *linked = document.addChild(root, QStringLiteral("Linked"));
        QVERIFY(document.setNote(linked, QStringLiteral("Beside"), QString()));
        { const MapSnapshot laidOut(document.model()); }
        QVERIFY(document.save(path, BmapStorage::SaveReason::Explicit).ok);
        document.close();
    }
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *library = window->findChild<MapLibrary *>();
    QVERIFY(library);
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *canvas = nullptr;
    QTRY_VERIFY((canvas = window->findChild<MapCanvas *>()));
    QTRY_VERIFY(canvas->width() > 0 && canvas->map()->isOpen());
    MobileMap *map = canvas->map();
    const QString copy = map->document()->filePath();
    UrlCatcher browser;
    QDesktopServices::setUrlHandler(QStringLiteral("https"), &browser, "open");
    const auto cleanup = qScopeGuard([&] {
        QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
        map->close();
        library->discard(copy);
    });
    const MindNode *root = map->document()->model()->root();
    MindNode *task = const_cast<MindNode *>(childTitled(root, QStringLiteral("Task")));
    MindNode *linked = const_cast<MindNode *>(childTitled(root, QStringLiteral("Linked")));
    QVERIFY(task && linked);
    QObject *dialog = window->findChild<QObject *>(QStringLiteral("webLinkDialog"));
    QObject *field = window->findChild<QObject *>(QStringLiteral("webLinkField"));
    QObject *insert = window->findChild<QObject *>(QStringLiteral("webLinkInsertButton"));
    QObject *error = window->findChild<QObject *>(QStringLiteral("webLinkError"));
    QObject *remove = window->findChild<QObject *>(QStringLiteral("webLinkRemoveButton"));
    QVERIFY(dialog && field && insert && error && remove);
    const auto click = [&](const char *name) {
        QObject *button = window->findChild<QObject *>(QLatin1String(name));
        return button && button->property("enabled").toBool() && QMetaObject::invokeMethod(button, "clicked");
    };
    const auto pixelAt = [&](const QPointF &mapPoint) {
        const QPointF scenePoint = canvas->mapToScene(canvas->itemPoint(mapPoint));
        const QImage frame = window->grabWindow();
        return frame.pixelColor((scenePoint * frame.devicePixelRatio()).toPoint());
    };
    const auto near = [](const QColor &a, const QColor &b) {
        return std::abs(a.red() - b.red()) < 24 && std::abs(a.green() - b.green()) < 24 && std::abs(a.blue() - b.blue()) < 24;
    };

    // Checkbox: the topic bar's button adds it (the topic widens), one Undo step.
    map->select(task->id);
    const qreal plainWidth = map->snapshot()->topicOf(task)->geometry->shapeRect().width();
    QVERIFY(click("checkboxButton"));
    QCOMPARE(task->task, MindNode::Task::Open);
    QVERIFY(map->selectedHasCheckbox());
    QVERIFY(map->snapshot()->topicOf(task)->geometry->shapeRect().width() > plainWidth);
    QVERIFY(map->undo());
    QCOMPARE(task->task, MindNode::Task::None);
    QVERIFY(map->redo());
    QCOMPARE(task->task, MindNode::Task::Open);
    // A tap on the box of a topic not selected ticks it and selects the topic; it is drawn filled
    // with the branch colour. One Undo step per tap.
    canvas->zoomStep(0);
    const MapSnapshot::Topic *topic = map->snapshot()->topicOf(task);
    canvas->reveal(topic->bounds);
    map->select(QString());
    const QRectF box = topic->geometry->taskRect().translated(topic->position);
    QVERIFY(!box.isEmpty());
    const QPointF onBox = canvas->itemPoint(box.center());
    canvas->tapAt(onBox.x(), onBox.y());
    QCOMPARE(map->selectedId(), task->id);
    QCOMPARE(task->task, MindNode::Task::Done);
    {
        const qreal inset = (NodeGeometry::kLinkButtonPx - MapScene::kTaskBoxPx) / 2;
        const QPointF filled = box.topLeft() + QPointF(inset + 3, inset + 4);  // off the tick
        const QColor branch = map->snapshot()->topicOf(task)->node->branchColor;
        QColor drawn;
        QTRY_VERIFY2(near(drawn = pixelAt(filled), branch), qPrintable(drawn.name() + QLatin1String(" vs ") + branch.name()));
    }
    QVERIFY(map->undo());
    QCOMPARE(task->task, MindNode::Task::Open);
    QVERIFY(map->redo());
    canvas->tapAt(onBox.x(), onBox.y());
    QCOMPARE(task->task, MindNode::Task::Open);
    // The button takes it off again.
    QVERIFY(click("checkboxButton"));
    QCOMPARE(task->task, MindNode::Task::None);
    QVERIFY(map->snapshot()->topicOf(task)->geometry->taskRect().isEmpty());
    QVERIFY(map->undo());
    QCOMPARE(task->task, MindNode::Task::Open);

    // Web link: the dialog completes the address and sets it in one Undo step.
    map->select(linked->id);
    QVERIFY(click("webLinkButton"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QCOMPARE(field->property("text").toString(), QString());
    QVERIFY(!remove->property("visible").toBool());
    field->setProperty("text", QStringLiteral("ftp://example.com"));
    QVERIFY(!insert->property("enabled").toBool());
    QVERIFY(error->property("visible").toBool());
    field->setProperty("text", QStringLiteral(" example.com "));
    QVERIFY(insert->property("enabled").toBool());
    QVERIFY(!error->property("visible").toBool());
    QVERIFY(click("webLinkInsertButton"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(linked->webLink, QStringLiteral("https://example.com"));
    QVERIFY(map->isDirty());
    QVERIFY(map->undo());
    QVERIFY(linked->webLink.isEmpty());
    QVERIFY(map->redo());
    QCOMPARE(linked->webLink, QStringLiteral("https://example.com"));
    // Edited: the current address is in the field; Cancel leaves it.
    QVERIFY(click("webLinkButton"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QCOMPARE(field->property("text").toString(), QStringLiteral("https://example.com"));
    QVERIFY(remove->property("visible").toBool());
    field->setProperty("text", QStringLiteral("https://mentis.test/page"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(linked->webLink, QStringLiteral("https://example.com"));

    // The link's button is drawn and a tap on it opens the page; the note's button beside it still
    // opens the note.
    topic = map->snapshot()->topicOf(linked);
    canvas->reveal(topic->bounds);
    const QRectF linkIcon = topic->geometry->linkIconRect(false).translated(topic->position);
    const QRectF noteIcon = topic->geometry->noteRect().translated(topic->position);
    QVERIFY(!linkIcon.isEmpty() && !noteIcon.isEmpty());
    {
        QColor drawn;
        QTRY_VERIFY2((drawn = pixelAt(linkIcon.topLeft() + QPointF(2, 2))).lightness() > 230, qPrintable(drawn.name()));
    }
    QSignalSpy noteRequested(canvas, &MapCanvas::noteRequested);
    const QPointF onLink = canvas->itemPoint(linkIcon.center());
    canvas->tapAt(onLink.x(), onLink.y());
    QCOMPARE(browser.urls, QList<QUrl>{QUrl(QStringLiteral("https://example.com"))});
    QCOMPARE(noteRequested.count(), 0);
    const QPointF onNote = canvas->itemPoint(noteIcon.center());
    canvas->tapAt(onNote.x(), onNote.y());
    QCOMPARE(noteRequested.count(), 1);
    QCOMPARE(browser.urls.size(), 1);
    QObject *noteDialog = window->findChild<QObject *>(QStringLiteral("noteDialog"));
    QTRY_VERIFY(noteDialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(noteDialog, "close"));
    QTRY_VERIFY(!noteDialog->property("visible").toBool());

    // A link from someone else's file that may not be opened is only reported.
    QVERIFY(map->document()->model()->setLinks({{linked, {}, QStringLiteral("file:///C:/tool.exe")}}));
    QVERIFY(!map->openWebLink());
    QVERIFY(map->message().contains(QStringLiteral("file:///C:/tool.exe")));
    QCOMPARE(browser.urls.size(), 1);
    QVERIFY(map->setWebLink(QStringLiteral("https://example.com/kept")));

    // Remove takes it off in one Undo step.
    QVERIFY(click("webLinkButton"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(remove->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(remove, "clicked"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(linked->webLink.isEmpty());
    QVERIFY(map->snapshot()->topicOf(linked)->geometry->linkIconRect(false).isEmpty());
    QVERIFY(map->undo());
    QCOMPARE(linked->webLink, QStringLiteral("https://example.com/kept"));

    // Saved and opened again: the checkbox and the link are in the file.
    QVERIFY(map->close());
    {
        MobileMap reopened;
        QVERIFY(reopened.open(copy));
        const MindNode *rootAgain = reopened.document()->model()->root();
        QCOMPARE(childTitled(rootAgain, QStringLiteral("Task"))->task, MindNode::Task::Open);
        QCOMPARE(childTitled(rootAgain, QStringLiteral("Linked"))->webLink, QStringLiteral("https://example.com/kept"));
        QVERIFY(reopened.close());
    }
    for (const QQmlError &qmlError : std::as_const(errors)) qWarning("%s", qPrintable(qmlError.toString()));
    QVERIFY(errors.isEmpty());
}

// FEAT-111: the multiple selection mode: taps add and remove topics; Delete, markers, stickers and
// Paste style act on all of them in one Undo step each; the mode ends by each way of the card.
void MobileTest::multipleSelection() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Multi.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MapCanvas canvas;
    canvas.setSize(QSizeF(400, 800));
    canvas.setMap(&map);
    MindMapModel *model = map.document()->model();
    const QString rootId = model->root()->id;
    const QString firstId = childTitled(model->root(), QStringLiteral("First"))->id;
    const QString secondId = childTitled(model->root(), QStringLiteral("Second"))->id;
    map.select(firstId);
    QVERIFY(map.addChild());  // First has a child, removed with its branch
    const QString childId = map.selectedId();
    QCOMPARE(map.topicCount(), 4);
    const auto tap = [&](const QString &id) {
        const QPointF at = canvas.itemPoint(map.snapshot()->topicOf(model->nodeById(id))->bounds.center());
        canvas.tapAt(at.x(), at.y());
    };
    QVERIFY(!map.canPasteStyle());  // nothing copied yet
    QVERIFY(!map.pasteStyle());
    const auto start = [&](const QStringList &ids) {
        map.select(ids.first());
        map.startMultiSelect();
        for (qsizetype i = 1; i < ids.size(); ++i) tap(ids[i]);
        return map.multiSelect() && map.selectedIds() == ids;
    };

    // Taps add and remove; the view does not move; selectedId is the last added still selected.
    QVERIFY(!map.multiSelect());
    map.select(QString());
    map.startMultiSelect();  // nothing selected: the mode starts empty (2026-11-12)
    QVERIFY(map.multiSelect());
    QVERIFY(map.selectedIds().isEmpty());
    QVERIFY(!map.canRemoveSelection());
    QVERIFY(!map.toggleCheckbox());
    tap(secondId);
    QCOMPARE(map.selectedIds(), QStringList{secondId});
    QCOMPARE(map.selectedId(), secondId);
    map.stopMultiSelect();
    map.select(firstId);
    map.startMultiSelect();
    QVERIFY(map.multiSelect());
    QCOMPARE(map.selectedIds(), QStringList{firstId});
    const qreal zoom = canvas.zoom();
    const QPointF origin = canvas.mapPoint(QPointF());
    tap(secondId);
    tap(rootId);
    QCOMPARE(map.selectedIds(), (QStringList{firstId, secondId, rootId}));
    QCOMPARE(map.selectedId(), rootId);
    QCOMPARE(canvas.zoom(), zoom);
    QCOMPARE(canvas.mapPoint(QPointF()), origin);
    tap(rootId);  // removed: the last one added that is still selected is Second
    QCOMPARE(map.selectedIds(), (QStringList{firstId, secondId}));
    QCOMPARE(map.selectedId(), secondId);
    tap(firstId);
    tap(secondId);  // the last one removed ends the mode, with nothing selected
    QVERIFY(!map.multiSelect());
    QVERIFY(map.selectedId().isEmpty());

    // The other ways out keep only the last topic tapped.
    QVERIFY(start({firstId, secondId}));
    map.stopMultiSelect();  // Select more tapped again, Back, Esc
    QVERIFY(!map.multiSelect());
    QCOMPARE(map.selectedId(), secondId);
    QCOMPARE(map.selectedIds(), QStringList{secondId});
    QVERIFY(start({firstId, secondId}));
    canvas.tapAt(1, 1);  // the background
    QVERIFY(!map.multiSelect());
    QCOMPARE(map.selectedId(), secondId);
    QVERIFY(start({firstId, secondId}));
    map.select(rootId);  // a topic selected another way (a key)
    QVERIFY(!map.multiSelect());
    QCOMPARE(map.selectedId(), rootId);
    QVERIFY(start({firstId, secondId}));
    map.startFormatPainter(false);  // the tools end each other
    QVERIFY(!map.multiSelect());
    QVERIFY(map.formatPainter());
    map.startMultiSelect();
    QVERIFY(map.multiSelect());
    QVERIFY(!map.formatPainter());
    map.stopMultiSelect();
    map.startRelation(false);
    map.startMultiSelect();
    QVERIFY(!map.relationTool());
    map.stopMultiSelect();

    // Delete: First with its child and Second, the central topic skipped, one step; the parent is
    // selected after (no sibling stays).
    QVERIFY(start({rootId}));
    QVERIFY(!map.canRemoveSelection());
    QVERIFY(!map.removeSelected());
    tap(firstId);
    tap(secondId);
    QVERIFY(map.canRemoveSelection());
    QVERIFY(map.removeSelected());
    QCOMPARE(map.topicCount(), 1);
    QVERIFY(!map.multiSelect());
    QCOMPARE(map.selectedId(), rootId);
    QVERIFY(map.undo());
    QCOMPARE(map.topicCount(), 4);
    QVERIFY(model->nodeById(childId));
    // A topic and its descendant: the branch goes once; the next sibling is selected after.
    QVERIFY(start({firstId, childId}));
    QVERIFY(map.removeSelected());
    QCOMPARE(map.topicCount(), 2);
    QCOMPARE(map.selectedId(), secondId);
    QVERIFY(map.undo());

    // Markers: added to all (also where one has it), then taken off from all when every one has it.
    StickerLibrary library;
    library.setMarkers(true);
    QString one, collection;
    for (const QVariant &c : library.collections())
        for (const QVariant &i : c.toMap().value(QStringLiteral("items")).toList())
            if (i.toMap().value(QStringLiteral("marker")).toString() == QStringLiteral("priority:1")) {
                one = QStringLiteral("priority:1");
                collection = c.toMap().value(QStringLiteral("id")).toString();
            }
    QVERIFY(!one.isEmpty());
    map.select(firstId);
    QVERIFY(map.toggleMarker(one, QString(), collection));
    QVERIFY(start({firstId, secondId}));
    QVERIFY(map.selectedMarkers().isEmpty());  // only the markers every one has
    QVERIFY(map.toggleMarker(one, QString(), collection));
    QVERIFY(model->nodeById(firstId)->markers.contains(one) && model->nodeById(secondId)->markers.contains(one));
    QCOMPARE(map.selectedMarkers(), QStringList{one});
    QVERIFY(map.multiSelect());  // the mode stays on
    QVERIFY(map.toggleMarker(one, QString(), collection));
    QVERIFY(model->nodeById(firstId)->markers.isEmpty() && model->nodeById(secondId)->markers.isEmpty());
    QVERIFY(map.undo());  // one step for both
    QVERIFY(!map.multiSelect());
    QVERIFY(model->nodeById(firstId)->markers.contains(one) && model->nodeById(secondId)->markers.contains(one));

    // Stickers: into every selected topic, one step.
    const QString sticker = StickerLibrary().collections().constFirst().toMap().value(QStringLiteral("items")).toList()
                                .constFirst().toMap().value(QStringLiteral("path")).toString();
    QVERIFY(start({firstId, secondId}));
    QVERIFY(map.insertSticker(sticker));
    QVERIFY(model->nodeById(firstId)->imageSticker && model->nodeById(secondId)->imageSticker);
    QVERIFY(map.multiSelect());
    QVERIFY(map.undo());
    QVERIFY(model->nodeById(firstId)->imagePath.isEmpty() && model->nodeById(secondId)->imagePath.isEmpty());

    // Checkbox: on the topics without one, then off from all when every one has it; one step each.
    map.select(firstId);
    QVERIFY(map.toggleCheckbox());
    QVERIFY(start({firstId, secondId}));
    QVERIFY(map.toggleCheckbox());
    QCOMPARE(model->nodeById(firstId)->task, MindNode::Task::Open);
    QCOMPARE(model->nodeById(secondId)->task, MindNode::Task::Open);
    QVERIFY(map.multiSelect());
    QVERIFY(map.toggleCheckbox());
    QCOMPARE(model->nodeById(firstId)->task, MindNode::Task::None);
    QCOMPARE(model->nodeById(secondId)->task, MindNode::Task::None);
    QVERIFY(map.undo());
    QVERIFY(model->nodeById(firstId)->task == MindNode::Task::Open && model->nodeById(secondId)->task == MindNode::Task::Open);
    QVERIFY(map.undo());
    QVERIFY(model->nodeById(secondId)->task == MindNode::Task::None);
    QVERIFY(map.undo());
    QVERIFY(model->nodeById(firstId)->task == MindNode::Task::None);

    // Image: the same picture in every selected topic, one step.
    QImage pixels(40, 30, QImage::Format_RGB32);
    pixels.fill(Qt::blue);
    const QString picture = storage.filePath(QStringLiteral("blue.png"));
    QVERIFY(pixels.save(picture));
    QVERIFY(start({firstId, secondId}));
    QVERIFY(map.insertImage(picture));
    QCOMPARE(model->nodeById(firstId)->imagePath, picture);
    QCOMPARE(model->nodeById(secondId)->imagePath, picture);
    QVERIFY(map.multiSelect());
    QVERIFY(map.undo());
    QVERIFY(model->nodeById(firstId)->imagePath.isEmpty() && model->nodeById(secondId)->imagePath.isEmpty());

    // Paste style: the copied style on all, one step.
    MindNode *child = model->nodeById(childId);
    MindMapModel::TopicStyle red = model->topicStyle(child);  // a subtopic's style, as FEAT-085's tests
    red.style.fill = QColor(Qt::red);
    QVERIFY(model->pasteStyle(red, {child}));
    map.document()->markChanged();
    map.document()->flushPendingStep();
    QSignalSpy copied(&map, &MobileMap::copiedStyleChanged);
    map.select(childId);
    map.copyStyle();
    QCOMPARE(copied.size(), 1);
    QVERIFY(map.canPasteStyle());
    QVERIFY(start({firstId, secondId}));
    QVERIFY(map.pasteStyle());
    QCOMPARE(model->nodeById(firstId)->fill, QColor(Qt::red));
    QCOMPARE(model->nodeById(secondId)->fill, QColor(Qt::red));
    QVERIFY(map.multiSelect());
    QVERIFY(map.undo());
    QVERIFY(model->nodeById(firstId)->fill != QColor(Qt::red) && model->nodeById(secondId)->fill != QColor(Qt::red));

    // Copy and paste (as the desktop's Ctrl+C / Ctrl+V): one topic with its branch, then two from
    // the mode, pasted under the topic tapped last as its last children, one step each.
    QGuiApplication::clipboard()->clear();
    map.select(secondId);
    QVERIFY(!map.pasteBranches());  // nothing on the clipboard
    QCOMPARE(map.message(), QStringLiteral("Nothing to paste"));
    map.select(firstId);
    QVERIFY(map.copyBranches());
    QCOMPARE(map.message(), QStringLiteral("Topic copied"));
    map.select(secondId);
    QVERIFY(map.pasteBranches());
    QCOMPARE(map.topicCount(), 6);  // First and its child under Second
    MindNode *pasted = model->nodeById(map.selectedId());
    QVERIFY(pasted && pasted->parent == model->nodeById(secondId));
    QCOMPARE(pasted->title, QStringLiteral("First"));
    QCOMPARE(pasted->children.size(), 1);
    // A copy, not a cut: the original stays where it is and both are drawn (user's report 2026-11-12).
    QCOMPARE(model->nodeById(firstId)->parent, model->root());
    QCOMPARE(map.snapshot()->topics().size(), 6);
    QVERIFY(map.snapshot()->topicOf(model->nodeById(firstId)));
    QVERIFY(map.snapshot()->topicOf(pasted)->position != map.snapshot()->topicOf(model->nodeById(firstId))->position);
    QVERIFY(map.undo());
    QCOMPARE(map.topicCount(), 4);
    QVERIFY(start({firstId, secondId}));
    QVERIFY(map.copyBranches());
    QCOMPARE(map.message(), QStringLiteral("Topics copied"));
    tap(rootId);  // the last one tapped receives the paste
    QVERIFY(map.pasteBranches());
    QVERIFY(!map.multiSelect());  // the new topic is selected, out of the selection
    QCOMPARE(map.topicCount(), 7);
    QCOMPARE(model->root()->children.size(), 4);
    QVERIFY(map.undo());
    QCOMPARE(map.topicCount(), 4);
    // Android's clipboard keeps only the text: the copy's markers and picture still come along while
    // the clipboard holds that copy's text (user's report 2026-11-12).
    map.select(firstId);
    QVERIFY(map.insertImage(picture));
    if (!model->nodeById(firstId)->markers.contains(one)) QVERIFY(map.toggleMarker(one, QString(), collection));
    QVERIFY(map.copyBranches());
    QGuiApplication::clipboard()->setText(QGuiApplication::clipboard()->text());  // as Android: text only
    QVERIFY(!QGuiApplication::clipboard()->mimeData()->hasFormat(Document::branchesMimeType()));
    map.select(secondId);
    QVERIFY(map.pasteBranches());
    pasted = model->nodeById(map.selectedId());
    QVERIFY(pasted && pasted->parent == model->nodeById(secondId));
    QCOMPARE(pasted->imagePath, picture);
    QVERIFY(pasted->markers.contains(one));
    QCOMPARE(pasted->children.size(), 1);
    QVERIFY(map.undo());
    // Text from another app: read as Markdown, as on the desktop.
    QGuiApplication::clipboard()->setText(QStringLiteral("- One\n- Two"));
    map.select(secondId);
    QVERIFY(map.pasteBranches());
    QCOMPARE(model->nodeById(secondId)->children.size(), 2);
    QVERIFY(map.undo());

    // A sheet switch ends the mode.
    QVERIFY(start({firstId, secondId}));
    QVERIFY(map.switchSheet(1));
    QVERIFY(!map.multiSelect());
    QVERIFY(map.close());
}

// FEAT-111 on the page: Select more and the bar of the mode, a ring on each selected topic, the
// knobs hidden, a double tap that does not edit, Esc and Android's Back ending the mode.
void MobileTest::multipleSelectionOnThePage() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("MultiPage.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *library = window->findChild<MapLibrary *>();
    QVERIFY(library);
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *canvas = nullptr;
    QTRY_VERIFY((canvas = window->findChild<MapCanvas *>()));
    QTRY_VERIFY(canvas->width() > 0 && canvas->map()->isOpen());
    MobileMap *map = canvas->map();
    const QString copy = map->document()->filePath();
    const auto cleanup = qScopeGuard([&] {
        map->close();
        library->discard(copy);
    });
    MindMapModel *model = map->document()->model();
    const MindNode *first = childTitled(model->root(), QStringLiteral("First"));
    const MindNode *second = childTitled(model->root(), QStringLiteral("Second"));
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QVERIFY(stack);
    QTRY_VERIFY(!stack->property("busy").toBool());  // the map page has slid in
    canvas->fit();
    const auto item = [&](const char *name) { return window->findChild<QQuickItem *>(QLatin1String(name)); };
    const auto click = [&](const char *name) {
        QQuickItem *button = item(name);
        return button && button->isVisible() && button->isEnabled() && QMetaObject::invokeMethod(button, "clicked");
    };
    const auto tap = [&](const MindNode *node) {
        const QPointF at = canvas->itemPoint(map->snapshot()->topicOf(node)->bounds.center());
        canvas->tapAt(at.x(), at.y());
    };
    // A pixel of the selection colour on the ring's top edge, above the topic's middle.
    const auto ringed = [&](const MindNode *node) {
        const QImage frame = window->grabWindow();
        const QRectF bounds = map->snapshot()->topicOf(node)->bounds;
        for (qreal y = bounds.top() - 2; y <= bounds.top() + 8; y += 0.5) {
            const QPointF scenePoint = canvas->mapToScene(canvas->itemPoint(QPointF(bounds.center().x(), y)));
            const QColor pixel = frame.pixelColor((scenePoint * frame.devicePixelRatio()).toPoint());
            if (std::abs(pixel.red() - MapCanvas::kSelectionColor.red()) < 40 && std::abs(pixel.green() - MapCanvas::kSelectionColor.green()) < 40
                && std::abs(pixel.blue() - MapCanvas::kSelectionColor.blue()) < 40)
                return true;
        }
        return false;
    };

    // With nothing selected the bar holds only Select more, which starts an empty selection whose
    // buttons wait for a topic (user's request 2026-11-12).
    const auto shown = [&] {
        QStringList names;
        for (QQuickItem *button : item("topicBar")->findChildren<QQuickItem *>())
            if (button->objectName().endsWith(QStringLiteral("Button")) && button->isVisible()) names.append(button->objectName());
        names.sort();
        return names;
    };
    map->select(QString());
    QTRY_VERIFY(item("topicBar")->isVisible());
    QTRY_COMPARE(shown(), QStringList{QStringLiteral("selectMoreButton")});
    QVERIFY(click("selectMoreButton"));
    QVERIFY(map->multiSelect());
    QVERIFY(map->selectedIds().isEmpty());
    for (const char *name : {"deleteButton", "copyTopicsButton", "pasteTopicsButton", "imageButton", "checkboxButton", "topicStickerButton", "pasteStyleButton"})
        QTRY_VERIFY2(item(name)->isVisible() && !item(name)->isEnabled(), name);
    tap(first);
    QTRY_VERIFY(item("checkboxButton")->isEnabled());
    QVERIFY(item("imageButton")->isEnabled() && item("topicStickerButton")->isEnabled() && item("deleteButton")->isEnabled());
    QCOMPARE(item("imageButton")->property("text").toString(), QStringLiteral("Insert an image in the selected topics"));
    QVERIFY(click("selectMoreButton"));  // tapped again: the mode ends
    QVERIFY(!map->multiSelect());

    map->select(first->id);
    QTRY_VERIFY(item("topicBar")->isVisible());
    QCOMPARE(item("selectMoreButton")->property("text").toString(), QStringLiteral("Select more topics"));
    QVERIFY(item("copyTopicsButton")->isVisible() && item("pasteTopicsButton")->isVisible());
    QVERIFY(!item("pasteStyleButton")->isVisible());
    QVERIFY(item("addChildKnob")->isVisible());
    QVERIFY(click("selectMoreButton"));
    QVERIFY(map->multiSelect());
    QVERIFY(item("selectMoreButton")->property("checked").toBool());
    // The bar of the mode: Select more, Delete, Copy, Paste, Image, Checkbox, Marker and sticker,
    // Paste style.
    for (const char *name : {"selectMoreButton", "deleteButton", "copyTopicsButton", "pasteTopicsButton", "imageButton", "checkboxButton", "topicStickerButton", "pasteStyleButton"})
        QTRY_VERIFY2(item(name)->isVisible(), name);
    for (const char *name : {"renameButton", "relationButton", "webLinkButton", "noteButton", "formatPainterButton"})
        QVERIFY2(!item(name)->isVisible(), name);
    QVERIFY(!item("pasteStyleButton")->isEnabled());  // no style copied in this session's map yet
    QVERIFY(!item("addChildKnob")->isVisible());
    QVERIFY(!item("addSiblingKnob")->isVisible());
    tap(second);
    QCOMPARE(map->selectedIds(), (QStringList{first->id, second->id}));
    // The offscreen platform draws no strokes of the phone's map (no branches either): the rings are
    // checked with QT_QPA_PLATFORM=windows.
    if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
        QTRY_VERIFY(ringed(first));
        QVERIFY(ringed(second));
        QVERIFY(!ringed(model->root()));
    }
    // A double tap only adds and removes: no title is edited.
    const QPointF onRoot = canvas->mapToScene(canvas->itemPoint(map->snapshot()->topicOf(model->root())->bounds.center()));
    QTest::mouseDClick(window, Qt::LeftButton, {}, onRoot.toPoint());
    QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
    QVERIFY(!item("titleEditor")->isVisible());
    QVERIFY(map->multiSelect());
    // Delete on all of them, one step.
    QVERIFY(click("deleteButton"));
    QVERIFY(!map->multiSelect());
    QCOMPARE(map->topicCount(), 1);
    QVERIFY(map->undo());
    QCOMPARE(map->topicCount(), 3);
    // Esc and Android's Back end the mode; Back does not leave the map then.
    map->select(model->root()->id);
    QVERIFY(click("selectMoreButton"));
    QTest::keyClick(window, Qt::Key_Escape);
    QVERIFY(!map->multiSelect());
    QVERIFY(click("selectMoreButton"));
    QTest::keyClick(window, Qt::Key_Back);
    QVERIFY(!map->multiSelect());
    QCOMPARE(stack->property("depth").toInt(), 2);
    QVERIFY(map->isOpen());
    // Select more again turns it off; the copied style enables Paste style.
    QVERIFY(click("selectMoreButton"));
    QVERIFY(click("selectMoreButton"));
    QVERIFY(!map->multiSelect());
    map->copyStyle();
    QVERIFY(click("selectMoreButton"));
    QTRY_VERIFY(item("pasteStyleButton")->isEnabled());
    map->stopMultiSelect();
    for (const QQmlError &error : std::as_const(errors)) qWarning("%s", qPrintable(error.toString()));
    QVERIFY(errors.isEmpty());
}

void MobileTest::sheetCommands() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("SheetCommands.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY2(map.open(path), qPrintable(map.lastError()));
    QSignalSpy shown(&map, &MobileMap::snapshotChanged);
    const bool couldUndo = map.canUndo();

    // Add: "Sheet 3" shown, laid out (its central topic measured), not a history step.
    map.select(map.document()->model()->root()->id);
    map.addSheet();
    QCOMPARE(map.sheets().size(), 3);
    QCOMPARE(map.currentSheet(), 2);
    QCOMPARE(map.sheets().at(2), QStringLiteral("Sheet 3"));
    QVERIFY(map.selectedId().isEmpty());
    QVERIFY(!shown.isEmpty());
    QVERIFY(map.topicCount() >= 1);
    QVERIFY(!map.snapshot()->topicOf(map.document()->model()->root())->bounds.isEmpty());
    map.document()->flushPendingStep();  // the pending change equals the new sheet: no step
    QCOMPARE(map.canUndo(), couldUndo);
    QVERIFY(map.isDirty());

    // Rename: trimmed; empty or unchanged keeps the name.
    QVERIFY(map.renameSheet(QStringLiteral("  Plans\n")));
    QCOMPARE(map.sheets().at(2), QStringLiteral("Plans"));
    QVERIFY(!map.renameSheet(QStringLiteral("   ")));
    QVERIFY(!map.renameSheet(QStringLiteral("Plans")));
    QCOMPARE(map.sheets().at(2), QStringLiteral("Plans"));

    // Duplicate: the copy right after the original, shown, with the message.
    QVERIFY(map.switchSheet(0));
    QVERIFY(map.duplicateSheet());
    QCOMPARE(map.sheets().size(), 4);
    QCOMPARE(map.currentSheet(), 1);
    QCOMPARE(map.sheets().at(1), map.sheets().at(0) + QStringLiteral(" (copy)"));
    QCOMPARE(map.topicCount(), 3);
    QCOMPARE(map.message(), QStringLiteral("Sheet \"%1\" duplicated").arg(map.sheets().at(0)));

    // Move: the order changes, the active sheet stays.
    const QString active = map.sheets().at(map.currentSheet());
    QVERIFY(map.moveSheet(3, 0));
    QCOMPARE(map.sheets().at(0), QStringLiteral("Plans"));
    QCOMPARE(map.sheets().at(map.currentSheet()), active);
    QVERIFY(!map.moveSheet(1, 1));
    QVERIFY(!map.moveSheet(0, 9));

    // Delete: the previous sheet shown, the message; the first deleted shows the next one.
    QVERIFY(map.switchSheet(2));
    const QString deleted = map.sheets().at(2);
    const QString previous = map.sheets().at(1);
    QVERIFY(map.deleteSheet());
    QCOMPARE(map.sheets().size(), 3);
    QCOMPARE(map.sheets().at(map.currentSheet()), previous);
    QCOMPARE(map.message(), QStringLiteral("Sheet \"%1\" deleted").arg(deleted));
    QVERIFY(map.switchSheet(0));
    const QString next = map.sheets().at(1);
    QVERIFY(map.deleteSheet());
    QCOMPARE(map.currentSheet(), 0);
    QCOMPARE(map.sheets().at(0), next);
    QVERIFY(map.deleteSheet());
    QCOMPARE(map.sheets().size(), 1);
    QVERIFY(!map.deleteSheet());  // the last sheet stays
    QCOMPARE(map.sheets().size(), 1);

    // Names and order are saved.
    const QStringList names = map.sheets();
    QVERIFY(map.close());
    MobileMap again;
    QVERIFY2(again.open(path), qPrintable(again.lastError()));
    QCOMPARE(again.sheets(), names);
    again.close();
}

namespace {
// FEAT-113: "budget" in a title, a note and a tag; the title under a collapsed branch; and on a
// second sheet.
QString writeSearchMap(const QString &path) {
    Document document;
    document.startUntitled();
    MindNode *root = document.model()->root();
    MindNode *plan = document.addChild(root, QStringLiteral("Plan"));
    document.addChild(plan, QStringLiteral("Budget <draft>"));
    document.setCollapsed(plan, true);
    MindNode *noted = document.addChild(root, QStringLiteral("Costs"));
    document.setNotes(noted, QStringLiteral("See the budget"));
    MindNode *tagged = document.addChild(root, QStringLiteral("Money"));
    document.setLabels(tagged, {QStringLiteral("budget")});
    { const MapSnapshot laidOut(document.model()); }
    document.addSheet();
    document.addChild(document.model()->root(), QStringLiteral("Budget 2027"));
    { const MapSnapshot laidOut(document.model()); }
    const Document::Result saved = document.save(path, BmapStorage::SaveReason::Explicit);
    document.close();
    return saved.ok ? QString() : saved.error;
}

QStringList rowTexts(const QVariantList &rows) {
    QStringList texts;
    for (const QVariant &row : rows) {
        const QVariantMap map = row.toMap();
        texts.append((map.value(QStringLiteral("header")).toBool() ? QStringLiteral("# ") : QString()) + map.value(QStringLiteral("text")).toString());
    }
    return texts;
}
}  // namespace

void MobileTest::searchFindsAndReveals() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Search.mentis"));
    QCOMPARE(writeSearchMap(path), QString());
    MobileMap map;
    QVERIFY2(map.open(path), qPrintable(map.lastError()));
    QVERIFY(map.switchSheet(0));
    const int all = NodeSearch::AllFields;

    // Every sheet, grouped under its name, in tree order; title, note and tag hits.
    const QVariantList rows = map.search(QStringLiteral("budget"), true, false, false, all);
    const QString first = map.sheets().at(0), second = map.sheets().at(1);
    QCOMPARE(rowTexts(rows), (QStringList{QStringLiteral("# ") + first, QStringLiteral("Budget <draft>"),
                                          QStringLiteral("Costs · See the budget"), QStringLiteral("Money · budget"),
                                          QStringLiteral("# ") + second, QStringLiteral("Budget 2027")}));
    // The found part highlighted, the rest escaped.
    const QVariantMap hit = rows.at(1).toMap();
    QCOMPARE(hit.value(QStringLiteral("ranges")).toList().value(0).toList(), (QVariantList{0, 6}));
    QCOMPARE(hit.value(QStringLiteral("html")).toString(),
             QStringLiteral("<span style=\"background-color:#b3d4fc;color:#202630\">Budget</span> &lt;draft&gt;"));
    // Options: the current map only, match case, fields, whole words.
    QCOMPARE(rowTexts(map.search(QStringLiteral("budget"), false, false, false, all)).size(), 3);
    QCOMPARE(rowTexts(map.search(QStringLiteral("budget"), false, false, true, all)),
             (QStringList{QStringLiteral("Costs · See the budget"), QStringLiteral("Money · budget")}));
    QCOMPARE(rowTexts(map.search(QStringLiteral("budget"), false, false, false, NodeSearch::Title)),
             QStringList{QStringLiteral("Budget <draft>")});
    QCOMPARE(rowTexts(map.search(QStringLiteral("budge"), false, true, false, all)), QStringList());
    QVERIFY(map.search(QStringLiteral("  "), true, false, false, all).isEmpty());

    // Reveal in the collapsed branch: opened in one Undo step, selected.
    const QVariantMap draft = rows.at(1).toMap();
    const QString draftId = draft.value(QStringLiteral("id")).toString();
    const bool couldUndo = map.canUndo();
    QVERIFY(map.reveal(draft.value(QStringLiteral("sheet")).toInt(), draftId));
    QCOMPARE(map.selectedTitle(), QStringLiteral("Budget <draft>"));
    QVERIFY(!map.document()->model()->nodeById(draftId)->parent->collapsed);
    QVERIFY(map.snapshot()->topicOf(map.document()->model()->nodeById(draftId)));
    QVERIFY(map.canUndo());
    QVERIFY(map.undo());
    QVERIFY(map.document()->model()->nodeById(draftId)->parent->collapsed);
    QCOMPARE(map.canUndo(), couldUndo);
    // In another sheet: the sheet switches; the tools end.
    map.startMultiSelect();
    const QVariantMap other = rows.at(5).toMap();
    QVERIFY(map.reveal(other.value(QStringLiteral("sheet")).toInt(), other.value(QStringLiteral("id")).toString()));
    QCOMPARE(map.currentSheet(), 1);
    QCOMPARE(map.selectedTitle(), QStringLiteral("Budget 2027"));
    QVERIFY(!map.multiSelect());
    QVERIFY(!map.reveal(0, QStringLiteral("gone")));
    map.close();
}

void MobileTest::searchOnThePage() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("SearchPage.mentis"));
    QCOMPARE(writeSearchMap(path), QString());
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *library = window->findChild<MapLibrary *>();
    QVERIFY(library);
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *canvas = nullptr;
    QTRY_VERIFY((canvas = window->findChild<MapCanvas *>()));
    QTRY_VERIFY(canvas->width() > 0 && canvas->map()->isOpen());
    MobileMap *map = canvas->map();
    const QString copy = map->document()->filePath();
    const auto cleanup = qScopeGuard([&] {
        map->close();
        library->discard(copy);
    });
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QVERIFY(stack);
    QTRY_VERIFY(!stack->property("busy").toBool());
    QVERIFY(map->switchSheet(0));
    const auto item = [&](const char *name) { return window->findChild<QQuickItem *>(QLatin1String(name)); };
    const auto object = [&](const char *name) { return window->findChild<QObject *>(QLatin1String(name)); };
    const auto centre = [](QQuickItem *target) { return target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint(); };

    // Find in the top right group, before Undo; it opens the panel with the field focused.
    QQuickItem *find = item("findButton");
    QVERIFY(find && find->isVisible());
    QCOMPARE(find->property("text").toString(), QStringLiteral("Find"));
    QVERIFY(find->mapToScene(QPointF()).x() < item("undoButton")->mapToScene(QPointF()).x());
    QVERIFY(QMetaObject::invokeMethod(find, "clicked"));
    QObject *panel = object("searchPanel");
    QVERIFY(panel);
    QTRY_VERIFY(panel->property("opened").toBool());
    QQuickItem *field = item("findField");
    QVERIFY(field);
    QTRY_VERIFY(field->hasActiveFocus());

    // Typing: the results after the delay, grouped by sheet.
    for (const char key : QByteArrayLiteral("budget")) QTest::keyClick(window, key);
    QTRY_COMPARE(panel->property("results").toList().size(), 6);
    QQuickItem *list = item("searchResults");
    QVERIFY(list);
    QList<QQuickItem *> rows;
    const auto findRows = [&] {
        rows.clear();
        for (QQuickItem *row : list->property("contentItem").value<QQuickItem *>()->childItems())
            if (row->objectName() == QStringLiteral("searchResultRow")) rows.append(row);
        std::sort(rows.begin(), rows.end(), [](QQuickItem *a, QQuickItem *b) { return a->y() < b->y(); });
        return rows.size();
    };
    QTRY_COMPARE(findRows(), 4);
    QVERIFY(rows.at(0)->height() >= 48);

    // Options: Current map leaves the other sheet out.
    QVERIFY(QMetaObject::invokeMethod(object("searchOptionsButton"), "clicked"));
    QObject *options = object("searchOptionsMenu");
    QVERIFY(options);
    QTRY_VERIFY(options->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(object("currentMapOption"), "clicked"));
    QTRY_COMPARE(panel->property("results").toList().size(), 3);
    QVERIFY(QMetaObject::invokeMethod(object("currentFileOption"), "clicked"));
    QTRY_COMPARE(panel->property("results").toList().size(), 6);
    QVERIFY(QMetaObject::invokeMethod(options, "close"));

    // Nothing found: the note.
    field->setProperty("text", QStringLiteral("zzz"));
    QVERIFY(QMetaObject::invokeMethod(panel, "refresh"));
    QTRY_VERIFY(item("noResultsLabel")->isVisible());
    QCOMPARE(item("noResultsLabel")->property("text").toString(), QStringLiteral("No topics found"));
    field->setProperty("text", QStringLiteral("budget"));
    QVERIFY(QMetaObject::invokeMethod(panel, "refresh"));
    QTRY_COMPARE(findRows(), 4);

    // A tap on the result in the collapsed branch: the panel closes, the topic is selected and shown.
    QTest::mouseClick(window, Qt::LeftButton, {}, centre(rows.at(0)));
    QTRY_VERIFY(!panel->property("visible").toBool());
    QCOMPARE(map->selectedTitle(), QStringLiteral("Budget <draft>"));
    const MapSnapshot::Topic *topic = map->selectedTopic();
    QVERIFY(topic);
    const QPointF topLeft = canvas->itemPoint(topic->bounds.topLeft());
    const QPointF bottomRight = canvas->itemPoint(topic->bounds.bottomRight());
    QVERIFY(topLeft.x() >= 0 && topLeft.y() >= 0 && bottomRight.x() <= canvas->width() && bottomRight.y() <= canvas->height());

    // Ctrl+F opens it again; Close closes it.
    QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
    QTRY_VERIFY(panel->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(object("closeSearchButton"), "clicked"));
    QTRY_VERIFY(!panel->property("visible").toBool());
    QVERIFY2(errors.isEmpty(), qPrintable(errors.isEmpty() ? QString() : errors.first().toString()));
}

void MobileTest::sheetCommandsOnThePage() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("SheetPage.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *library = window->findChild<MapLibrary *>();
    QVERIFY(library);
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *canvas = nullptr;
    QTRY_VERIFY((canvas = window->findChild<MapCanvas *>()));
    QTRY_VERIFY(canvas->width() > 0 && canvas->map()->isOpen());
    MobileMap *map = canvas->map();
    const QString copy = map->document()->filePath();
    const auto cleanup = qScopeGuard([&] {
        map->close();
        library->discard(copy);
    });
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QVERIFY(stack);
    QTRY_VERIFY(!stack->property("busy").toBool());
    const auto item = [&](const char *name) { return window->findChild<QQuickItem *>(QLatin1String(name)); };
    const auto object = [&](const char *name) { return window->findChild<QObject *>(QLatin1String(name)); };
    const auto centre = [](QQuickItem *target) { return target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint(); };
    const int holdMs = QGuiApplication::styleHints()->mousePressAndHoldInterval() + 200;
    const auto longPress = [&](QQuickItem *target) {
        QTest::mousePress(window, Qt::LeftButton, {}, centre(target));
        QTest::qWait(holdMs);
        QTest::mouseRelease(window, Qt::LeftButton, {}, centre(target));
    };

    // Add sheet, in the sheet group at the top left.
    QQuickItem *add = item("addSheetButton");
    QVERIFY(add && add->isVisible());
    QCOMPARE(add->property("text").toString(), QStringLiteral("Add sheet"));
    QVERIFY(QMetaObject::invokeMethod(add, "clicked"));
    QCOMPARE(map->sheets().size(), 3);
    QCOMPARE(map->currentSheet(), 2);

    // A long press on the select: the menu; Rename sheet… opens the modal with the name.
    QQuickItem *selectArea = item("sheetSelectArea");
    QObject *menu = object("sheetMenu");
    QVERIFY(selectArea && menu);
    longPress(selectArea);
    QTRY_VERIFY(menu->property("opened").toBool());
    auto *menuContent = menu->property("contentItem").value<QQuickItem *>();
    QVERIFY(menuContent);
    QObject *rename = menuContent->findChild<QObject *>(QStringLiteral("renameSheetButton"));
    QVERIFY(rename && QMetaObject::invokeMethod(rename, "clicked"));
    QObject *renameDialog = object("renameSheetDialog");
    QTRY_VERIFY(renameDialog->property("opened").toBool());
    QObject *field = object("sheetNameField");
    QCOMPARE(field->property("text").toString(), QStringLiteral("Sheet 3"));
    field->setProperty("text", QStringLiteral("Ideas"));
    QVERIFY(QMetaObject::invokeMethod(object("renameSheetConfirm"), "clicked"));
    QTRY_COMPARE(map->sheets().at(2), QStringLiteral("Ideas"));
    QTRY_VERIFY(!renameDialog->property("visible").toBool());

    // Duplicate sheet from the menu.
    longPress(selectArea);
    QTRY_VERIFY(menu->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(menuContent->findChild<QObject *>(QStringLiteral("duplicateSheetButton")), "clicked"));
    QTRY_COMPARE(map->sheets().size(), 4);
    QCOMPARE(map->sheets().at(3), QStringLiteral("Ideas (copy)"));

    // Delete sheet: the confirmation, then the previous sheet.
    longPress(selectArea);
    QTRY_VERIFY(menu->property("opened").toBool());
    QObject *remove = menuContent->findChild<QObject *>(QStringLiteral("deleteSheetButton"));
    QVERIFY(remove->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(remove, "clicked"));
    QObject *deleteDialog = object("deleteSheetDialog");
    QTRY_VERIFY(deleteDialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(object("deleteSheetConfirm"), "clicked"));
    QTRY_COMPARE(map->sheets().size(), 3);
    QCOMPARE(map->currentSheet(), 2);
    QCOMPARE(map->sheets().at(2), QStringLiteral("Ideas"));
    QTRY_VERIFY(!deleteDialog->property("visible").toBool());

    // Reorder: in the open list a long press on the first row, dragged under the second.
    QTest::mouseClick(window, Qt::LeftButton, {}, centre(selectArea));
    QQuickItem *list = nullptr;
    QTRY_VERIFY((list = item("sheetList")) && list->isVisible());
    QList<QQuickItem *> rows;
    const auto findRows = [&] {
        rows.clear();
        for (QQuickItem *row : list->property("contentItem").value<QQuickItem *>()->childItems())
            if (row->objectName() == QStringLiteral("sheetRow")) rows.append(row);
        std::sort(rows.begin(), rows.end(), [](QQuickItem *a, QQuickItem *b) { return a->y() < b->y(); });
        return rows.size();
    };
    QTRY_COMPARE(findRows(), 3);
    const QStringList before = map->sheets();
    const QPoint from = centre(rows.at(0));
    const QPoint to = rows.at(1)->mapToScene(QPointF(rows.at(1)->width() / 2, rows.at(1)->height() - 4)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, {}, from);
    QTest::qWait(holdMs);
    for (int step = 1; step <= 10; ++step) {
        QTest::mouseMove(window, from + (to - from) * step / 10);
        QTest::qWait(20);
    }
    QVERIFY(item("sheetDropLine")->isVisible());
    QTest::mouseRelease(window, Qt::LeftButton, {}, to);
    QTRY_COMPARE(map->sheets(), (QStringList{before.at(1), before.at(0), before.at(2)}));
    QCOMPARE(map->currentSheet(), 2);  // the active sheet stays the same
    QVERIFY(list->isVisible());        // the list stays open
    // A tap on a row still picks the sheet.
    QTRY_COMPARE(findRows(), 3);
    QTest::mouseClick(window, Qt::LeftButton, {}, centre(rows.at(0)));
    QTRY_COMPARE(map->currentSheet(), 0);

    // The sheet group is at the top left (user's request 2026-11-12): Back, the select, Add sheet.
    // A long name is elided so the group stays clear of the history group; the menu opens under it.
    QQuickItem *group = item("sheetGroup");
    QQuickItem *history = item("historyGroup");
    QQuickItem *select = item("sheetSelect");
    QVERIFY(group && history && select);
    QCOMPARE(group->mapToScene(QPointF()).y(), history->mapToScene(QPointF()).y());
    QVERIFY(group->mapToScene(QPointF()).x() < select->mapToScene(QPointF()).x());
    QVERIFY(select->mapToScene(QPointF()).x() < add->mapToScene(QPointF()).x());
    map->renameSheet(QString(80, QLatin1Char('W')));
    QTRY_COMPARE(map->sheets().at(0), QString(80, QLatin1Char('W')));
    QTRY_VERIFY(group->mapToScene(QPointF(group->width(), 0)).x() <= history->mapToScene(QPointF()).x());
    QVERIFY(select->width() < select->implicitWidth());
    longPress(selectArea);
    QTRY_VERIFY(menu->property("opened").toBool());
    auto *menuItem = menu->property("background").value<QQuickItem *>();
    QVERIFY(menuItem);
    QVERIFY(menuItem->mapToScene(QPointF()).y() >= group->mapToScene(QPointF(0, group->height())).y());
    QVERIFY(QMetaObject::invokeMethod(menu, "close"));
    for (const QQmlError &error : std::as_const(errors)) qWarning("%s", qPrintable(error.toString()));
    QVERIFY(errors.isEmpty());
}

// FEAT-114: tags drawn as pills under the topic; a tap on one, or the topic bar's button, opens the
// Labels modal with the topic's tags and the document's others; Done writes them (sorted when Auto
// sort is on) in one Undo step, Cancel leaves them; saved in the file.
void MobileTest::labelsOnThePage() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Tags.mentis"));
    {
        Document document;
        document.startUntitled();
        MindNode *root = document.model()->root();
        MindNode *tagged = document.addChild(root, QStringLiteral("Tagged"));
        QVERIFY(document.setLabels(tagged, {QStringLiteral("zeta"), QStringLiteral("Alpha")}));
        MindNode *other = document.addChild(root, QStringLiteral("Other"));
        QVERIFY(document.setLabels(other, {QStringLiteral("exam")}));
        document.addChild(root, QStringLiteral("Plain"));
        { const MapSnapshot laidOut(document.model()); }
        QVERIFY(document.save(path, BmapStorage::SaveReason::Explicit).ok);
        document.close();
    }
    Labels::setAutoSort(true);
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *library = window->findChild<MapLibrary *>();
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *canvas = nullptr;
    QTRY_VERIFY((canvas = window->findChild<MapCanvas *>()));
    QTRY_VERIFY(canvas->width() > 0 && canvas->map()->isOpen());
    MobileMap *map = canvas->map();
    const QString copy = map->document()->filePath();
    const auto cleanup = qScopeGuard([&] {
        Labels::setAutoSort(true);
        map->close();
        library->discard(copy);
    });
    const MindNode *root = map->document()->model()->root();
    MindNode *tagged = const_cast<MindNode *>(childTitled(root, QStringLiteral("Tagged")));
    MindNode *plain = const_cast<MindNode *>(childTitled(root, QStringLiteral("Plain")));
    QVERIFY(tagged && plain);
    QCOMPARE(tagged->labels, (QStringList{QStringLiteral("Alpha"), QStringLiteral("zeta")}));
    QObject *dialog = window->findChild<QObject *>(QStringLiteral("labelsDialog"));
    QObject *field = window->findChild<QObject *>(QStringLiteral("labelsField"));
    QVERIFY(dialog && field);
    const auto click = [&](const char *name) {
        QObject *button = window->findChild<QObject *>(QLatin1String(name));
        return button && button->property("visible").toBool() && QMetaObject::invokeMethod(button, "clicked");
    };

    // The pills are drawn under the topic, outside its selection ring; a tap on one selects the
    // topic and opens its tags.
    canvas->zoomStep(0);
    const MapSnapshot::Topic *topic = map->snapshot()->topicOf(tagged);
    const QList<QRectF> pills = topic->geometry->labelRects();
    QCOMPARE(pills.size(), 2);
    const QRectF pill = pills.at(0).translated(topic->position);
    QVERIFY(pill.top() > topic->geometry->shapeRect().translated(topic->position).bottom());
    QVERIFY(topic->contains(pill.center()));
    canvas->reveal(pill.united(topic->bounds));
    {
        const QPointF scenePoint = canvas->mapToScene(canvas->itemPoint(QPointF(pill.left() + 1, pill.center().y())));
        QColor drawn;
        const auto sample = [&] {
            const QImage frame = window->grabWindow();
            drawn = frame.pixelColor((scenePoint * frame.devicePixelRatio()).toPoint());
            return drawn.lightness() < 250;  // the pill's border on the white map
        };
        QTRY_VERIFY2(sample(), qPrintable(drawn.name()));
    }
    // A tap on a pill selects the topic and highlights the topics with its tag (any case): the
    // others, with their pills, are drawn at 20%. Another tap or Back ends it.
    MindNode *other = const_cast<MindNode *>(childTitled(root, QStringLiteral("Other")));
    QVERIFY(other);
    const MapSnapshot::Topic *otherTopic = map->snapshot()->topicOf(other);
    const QRectF otherPill = otherTopic->geometry->labelRects().at(0).translated(otherTopic->position);
    canvas->reveal(otherPill.united(pill).united(topic->bounds));
    // The scene as built for the highlight (a grab misses the shapes rebuilt by the test's software
    // renderer; checked with the windows platform): the triangles' alpha lower, the faded topics'
    // titles and tags under a 20% opacity node, the lit topic's title not.
    {
        struct Drawn { int alpha = 0; QList<qreal> textOpacities; };
        const auto drawn = [&](const QString &highlight) {
            Drawn out;
            const MapScene::Nodes nodes = MapScene::build(*map->snapshot(), window, nullptr, nullptr, {}, {}, highlight);
            for (QSGNode *child = nodes.content->firstChild(); child; child = child->nextSibling())
                if (child->type() == QSGNode::GeometryNodeType) {
                    const QSGGeometry *geometry = static_cast<QSGGeometryNode *>(child)->geometry();
                    const QSGGeometry::ColoredPoint2D *data = geometry->vertexDataAsColoredPoint2D();
                    for (int i = 0; i < geometry->vertexCount(); ++i) out.alpha += data[i].a;
                }
            for (QSGNode *child = nodes.text->firstChild(); child; child = child->nextSibling())
                if (child->type() == QSGNode::OpacityNodeType)
                    out.textOpacities.append(static_cast<QSGOpacityNode *>(child)->opacity());
            delete nodes.content;
            return out;
        };
        const Drawn plainScene = drawn(QString());
        const Drawn highlighted = drawn(QStringLiteral("alpha"));  // any case, as Labels::same
        QVERIFY(plainScene.textOpacities.isEmpty());
        QCOMPARE(highlighted.textOpacities, QList<qreal>{MapScene::kFadedOpacity});
        QVERIFY(highlighted.alpha < plainScene.alpha);
        QVERIFY(highlighted.alpha > plainScene.alpha * MapScene::kFadedOpacity);  // Tagged's pills stay
    }
    map->select(QString());
    const QPointF onPill = canvas->itemPoint(pill.center());
    canvas->tapAt(onPill.x(), onPill.y());
    QCOMPARE(map->selectedId(), tagged->id);
    QCOMPARE(canvas->labelHighlight(), QStringLiteral("Alpha"));
    QVERIFY(!dialog->property("opened").toBool());
    map->select(QString());  // the highlight stays
    QCOMPARE(canvas->labelHighlight(), QStringLiteral("Alpha"));
    const QPointF onOther = canvas->itemPoint(otherPill.center());
    canvas->tapAt(onOther.x(), onOther.y());
    QCOMPARE(canvas->labelHighlight(), QStringLiteral("exam"));
    QTest::keyClick(window, Qt::Key_Back);
    QCOMPARE(canvas->labelHighlight(), QString());
    QVERIFY(map->isOpen());
    const QPointF onPillNow = canvas->itemPoint(pill.center());  // a tap may pan to the selected topic
    canvas->tapAt(onPillNow.x(), onPillNow.y());
    QCOMPARE(canvas->labelHighlight(), QStringLiteral("Alpha"));
    const QPointF onBackground = canvas->itemPoint(topic->bounds.topLeft() - QPointF(200, 200));
    canvas->tapAt(onBackground.x(), onBackground.y());
    QCOMPARE(canvas->labelHighlight(), QString());
    // A long press on a pill opens its menu instead of picking the topic up; Edit tags opens them.
    map->select(QString());
    QSignalSpy labelMenuRequested(canvas, &MapCanvas::labelMenuRequested);
    QVERIFY(canvas->labelMenuAt(onPillNow.x(), onPillNow.y()));
    QVERIFY(!canvas->moving());
    QCOMPARE(map->selectedId(), tagged->id);
    QCOMPARE(labelMenuRequested.count(), 1);
    QObject *labelMenu = window->findChild<QObject *>(QStringLiteral("labelMenu"));
    QVERIFY(labelMenu);
    QTRY_VERIFY(labelMenu->property("opened").toBool());
    QVERIFY(!canvas->labelMenuAt(onBackground.x(), onBackground.y()));
    QVERIFY(click("editLabelsButton"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QCOMPARE(field->property("text").toString(), QStringLiteral("Alpha, zeta, "));
    // The document's other tags are offered; the word being typed filters them.
    QCOMPARE(dialog->property("shownSuggestions").toStringList(), QStringList{QStringLiteral("exam")});
    field->setProperty("text", QStringLiteral("Alpha, zeta, xyz"));
    QVERIFY(dialog->property("shownSuggestions").toStringList().isEmpty());
    field->setProperty("text", QStringLiteral("Alpha, zeta, EX"));
    QCOMPARE(dialog->property("shownSuggestions").toStringList(), QStringList{QStringLiteral("exam")});
    // A tap on a pill adds it after the tags already written (as LabelEditor::addSuggestion).
    field->setProperty("text", QStringLiteral("Alpha, zeta, "));
    QVERIFY(QMetaObject::invokeMethod(dialog, "addSuggestion", Q_ARG(QVariant, QStringLiteral("exam"))));
    QCOMPARE(field->property("text").toString(), QStringLiteral("Alpha, zeta, exam, "));
    QVERIFY(click("labelsDoneButton"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(tagged->labels, (QStringList{QStringLiteral("Alpha"), QStringLiteral("exam"), QStringLiteral("zeta")}));
    QCOMPARE(map->snapshot()->topicOf(tagged)->geometry->labelRects().size(), 3);
    QVERIFY(map->undo());
    QCOMPARE(tagged->labels, (QStringList{QStringLiteral("Alpha"), QStringLiteral("zeta")}));
    QVERIFY(map->redo());
    QCOMPARE(tagged->labels.size(), 3);

    // Cancel leaves the tags; the topic bar's button opens the modal on a topic without tags.
    map->select(plain->id);
    QVERIFY(click("labelsButton"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QCOMPARE(field->property("text").toString(), QString());
    QCOMPARE(dialog->property("shownSuggestions").toStringList().size(), 3);
    field->setProperty("text", QStringLiteral("gone"));
    QVERIFY(click("labelsCancelButton"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(plain->labels.isEmpty());
    // Auto sort off: the tags keep the order typed; duplicates (any case) are dropped.
    QVERIFY(click("labelsButton"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QObject *autoSort = window->findChild<QObject *>(QStringLiteral("labelsAutoSort"));
    QVERIFY(autoSort && autoSort->property("checked").toBool());
    QVERIFY(QMetaObject::invokeMethod(autoSort, "toggle"));
    QVERIFY(QMetaObject::invokeMethod(autoSort, "toggled"));
    QVERIFY(!Labels::autoSort());
    field->setProperty("text", QStringLiteral("beta, Alpha, BETA "));
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));  // Back writes, as Done
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(plain->labels, (QStringList{QStringLiteral("beta"), QStringLiteral("Alpha")}));
    // Emptied: the pills go.
    QVERIFY(map->setLabels(QString()));
    QVERIFY(plain->labels.isEmpty());
    QVERIFY(map->snapshot()->topicOf(plain)->pills.isEmpty());
    QVERIFY(map->undo());

    // Saved and opened again: the tags are in the file.
    QVERIFY(map->close());
    {
        MobileMap reopened;
        QVERIFY(reopened.open(copy));
        const MindNode *rootAgain = reopened.document()->model()->root();
        QCOMPARE(childTitled(rootAgain, QStringLiteral("Tagged"))->labels.size(), 3);
        QCOMPARE(childTitled(rootAgain, QStringLiteral("Plain"))->labels, (QStringList{QStringLiteral("beta"), QStringLiteral("Alpha")}));
        QVERIFY(reopened.close());
    }
    for (const QQmlError &qmlError : std::as_const(errors)) qWarning("%s", qPrintable(qmlError.toString()));
    QVERIFY(errors.isEmpty());
}

// FEAT-114: attached files on the phone: the paper clip drawn with its count, a tap opens the file
// list; files picked are checked and attached in one Undo step; Open and Share hand a copy to the
// system (a program asks first); Remove in one Undo step; embedded in the file.
void MobileTest::filesOnThePage() {
    QTemporaryDir source;
    const auto writeFile = [&](const QString &name, const QByteArray &content) {
        QFile file(source.filePath(name));
        if (!file.open(QIODevice::WriteOnly)) return QString();
        file.write(content);
        return file.fileName();
    };
    const QString thesis = writeFile(QStringLiteral("Thesis.pdf"), QByteArray("%PDF-1.4 thesis"));
    const QString notes = writeFile(QStringLiteral("Notes.txt"), QByteArray("some notes"));
    const QString tool = writeFile(QStringLiteral("tool.exe"), QByteArray("MZ program"));
    const QString empty = writeFile(QStringLiteral("Empty.txt"), QByteArray());
    QVERIFY(!thesis.isEmpty() && !notes.isEmpty() && !tool.isEmpty() && !empty.isEmpty());
    const QString path = source.filePath(QStringLiteral("Files.mentis"));
    {
        Document document;
        document.startUntitled();
        MindNode *root = document.model()->root();
        MindNode *documents = document.addChild(root, QStringLiteral("Documents"));
        QVERIFY(document.addFiles(documents, {thesis}));
        { const MapSnapshot laidOut(document.model()); }
        QVERIFY(document.save(path, BmapStorage::SaveReason::Explicit).ok);
        document.close();
    }
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *library = window->findChild<MapLibrary *>();
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *canvas = nullptr;
    QTRY_VERIFY((canvas = window->findChild<MapCanvas *>()));
    QTRY_VERIFY(canvas->width() > 0 && canvas->map()->isOpen());
    MobileMap *map = canvas->map();
    const QString copy = map->document()->filePath();
    UrlCatcher system;
    QDesktopServices::setUrlHandler(QStringLiteral("file"), &system, "open");
    const auto cleanup = qScopeGuard([&] {
        QDesktopServices::unsetUrlHandler(QStringLiteral("file"));
        map->close();
        library->discard(copy);
    });
    MindNode *documents = const_cast<MindNode *>(childTitled(map->document()->model()->root(), QStringLiteral("Documents")));
    QVERIFY(documents);
    QCOMPARE(documents->files.size(), 1);
    QObject *page = window->findChild<QObject *>(QStringLiteral("mapPage"));
    QObject *menu = window->findChild<QObject *>(QStringLiteral("filesMenu"));
    QVERIFY(page && menu);

    // The paper clip is drawn; a tap on it asks for the list below it.
    canvas->zoomStep(0);
    const MapSnapshot::Topic *topic = map->snapshot()->topicOf(documents);
    const QRectF clip = topic->geometry->filesRect().translated(topic->position);
    QVERIFY(!clip.isEmpty());
    canvas->reveal(topic->bounds);
    {
        const QPointF scenePoint = canvas->mapToScene(canvas->itemPoint(clip.topLeft() + QPointF(2, 2)));
        QColor drawn;
        const auto sample = [&] {
            const QImage frame = window->grabWindow();
            drawn = frame.pixelColor((scenePoint * frame.devicePixelRatio()).toPoint());
            return drawn.lightness() > 230;  // the button's white over the topic's fill
        };
        QTRY_VERIFY2(sample(), qPrintable(drawn.name()));
    }
    map->select(QString());
    QSignalSpy filesRequested(canvas, &MapCanvas::filesRequested);
    const QPointF onClip = canvas->itemPoint(clip.center());
    canvas->tapAt(onClip.x(), onClip.y());
    QCOMPARE(map->selectedId(), documents->id);
    QCOMPARE(filesRequested.count(), 1);
    QTRY_VERIFY(menu->property("opened").toBool());
    QVariantList files = map->selectedFiles();
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Thesis.pdf"));
    QVERIFY(!files.at(0).toMap().value(QStringLiteral("size")).toString().isEmpty());

    // Open hands a copy named attach01.pdf to the system; Share shows its folder off Android.
    // A Popup's rows are visual children of its content item (a Repeater's are not its QObject
    // children): searched down the item tree.
    const auto shownButtons = [&](const QString &name) {
        QList<QObject *> shown;
        auto *content = menu->property("contentItem").value<QQuickItem *>();
        std::function<void(QQuickItem *)> visit = [&](QQuickItem *item) {
            if (item->objectName() == name && item->isVisible()) shown.append(item);
            for (QQuickItem *child : item->childItems()) visit(child);
        };
        if (content) visit(content);
        return shown;
    };
    QList<QObject *> openButtons;
    QTRY_COMPARE((openButtons = shownButtons(QStringLiteral("fileOpenButton"))).size(), 1);
    QVERIFY(QMetaObject::invokeMethod(openButtons.first(), "clicked"));
    QTRY_VERIFY(!menu->property("visible").toBool());
    QCOMPARE(system.urls.size(), 1);
    const QString opened = system.urls.at(0).toLocalFile();
    QVERIFY2(opened.startsWith(map->openedFilesFolder()), qPrintable(opened));
    QCOMPARE(QFileInfo(opened).fileName(), QStringLiteral("attach01.pdf"));
    {
        QFile file(opened);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("%PDF-1.4 thesis"));
    }
    QVERIFY(map->shareFile(0));
    QCOMPARE(system.urls.size(), 2);
    QVERIFY(QFileInfo(system.urls.at(1).toLocalFile()).isDir());

    // Picked: the readable files are attached in one Undo step, an empty one is refused and said.
    const QList<QUrl> picked{QUrl::fromLocalFile(notes), QUrl::fromLocalFile(empty), QUrl::fromLocalFile(tool)};
    QVERIFY(QMetaObject::invokeMethod(page, "attachPicked", Q_ARG(QVariant, QVariant::fromValue(picked))));
    QTRY_COMPARE(documents->files.size(), 3);
    QVERIFY(page->property("imageError").toString().contains(QStringLiteral("Empty.txt")));
    QCOMPARE(map->selectedFiles().size(), 3);
    QVERIFY(map->undo());
    QCOMPARE(documents->files.size(), 1);
    QVERIFY(map->redo());
    QCOMPARE(documents->files.size(), 3);
    // The count is drawn on the clip from two files.
    QVERIFY(!map->snapshot()->topicOf(documents)->geometry->filesRect().isEmpty());

    // A program asks before it is opened.
    QVERIFY(!map->fileIsRisky(1));
    QVERIFY(map->fileIsRisky(2));
    QVERIFY(QMetaObject::invokeMethod(menu, "openAt", Q_ARG(QVariant, 100.0), Q_ARG(QVariant, 100.0)));
    QTRY_VERIFY(menu->property("opened").toBool());
    QTRY_COMPARE((openButtons = shownButtons(QStringLiteral("fileOpenButton"))).size(), 3);
    QObject *risky = window->findChild<QObject *>(QStringLiteral("riskyFileDialog"));
    QVERIFY(risky);
    QVERIFY(QMetaObject::invokeMethod(openButtons.last(), "clicked"));
    QTRY_VERIFY(risky->property("opened").toBool());
    QCOMPARE(system.urls.size(), 2);  // not yet
    QVERIFY(QMetaObject::invokeMethod(risky, "accept"));
    QTRY_COMPARE(system.urls.size(), 3);
    QCOMPARE(QFileInfo(system.urls.last().toLocalFile()).fileName(), QStringLiteral("attach03.exe"));
    QTRY_VERIFY(!risky->property("visible").toBool());

    // Remove takes one off in one Undo step.
    QVERIFY(map->removeFile(1));
    QCOMPARE(documents->files.size(), 2);
    QCOMPARE(documents->files.at(1).name, QStringLiteral("tool.exe"));
    QVERIFY(map->undo());
    QCOMPARE(documents->files.size(), 3);
    QVERIFY(map->removeFile(2));

    // Saved and opened again: the attached files are embedded (the originals are gone).
    QVERIFY(map->close());
    QVERIFY(QFile::remove(notes) && QFile::remove(thesis));
    {
        MobileMap reopened;
        QVERIFY(reopened.open(copy));
        const MindNode *again = childTitled(reopened.document()->model()->root(), QStringLiteral("Documents"));
        QCOMPARE(again->files.size(), 2);
        QCOMPARE(again->files.at(1).name, QStringLiteral("Notes.txt"));
        reopened.select(again->id);
        UrlCatcher later;
        QDesktopServices::setUrlHandler(QStringLiteral("file"), &later, "open");
        QVERIFY(reopened.openFile(1));
        QCOMPARE(later.urls.size(), 1);
        QFile file(later.urls.at(0).toLocalFile());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("some notes"));
        QVERIFY(reopened.close());
    }
    for (const QQmlError &qmlError : std::as_const(errors)) qWarning("%s", qPrintable(qmlError.toString()));
    QVERIFY(errors.isEmpty());
}

// FEAT-114: topic links on the phone: the search panel picks the target (the topic itself not
// offered), the link and its back link in another sheet are one Undo step; a tap on the chain
// jumps there, Back returns; a link to a deleted topic is grey and says so.
void MobileTest::topicLinksOnThePage() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Links.mentis"));
    {
        Document document;
        document.startUntitled();
        MindNode *root = document.model()->root();
        document.addChild(root, QStringLiteral("Start here"));
        document.addChild(root, QStringLiteral("Nearby goal"));
        { const MapSnapshot laidOut(document.model()); }
        document.addSheet();
        MindNode *folder = document.addChild(document.model()->root(), QStringLiteral("Folder"));
        document.addChild(folder, QStringLiteral("Faraway goal"));
        document.setCollapsed(folder, true);
        { const MapSnapshot laidOut(document.model()); }
        QVERIFY(document.save(path, BmapStorage::SaveReason::Explicit).ok);
        document.close();
    }
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *library = window->findChild<MapLibrary *>();
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *canvas = nullptr;
    QTRY_VERIFY((canvas = window->findChild<MapCanvas *>()));
    QTRY_VERIFY(canvas->width() > 0 && canvas->map()->isOpen());
    MobileMap *map = canvas->map();
    const QString copy = map->document()->filePath();
    const auto cleanup = qScopeGuard([&] {
        map->close();
        library->discard(copy);
    });
    QVERIFY(map->switchSheet(0));
    const auto topicTitled = [&](const QString &title) {
        return const_cast<MindNode *>(childTitled(map->document()->model()->root(), title));
    };
    QString startId = topicTitled(QStringLiteral("Start here"))->id;
    const QString nearbyId = topicTitled(QStringLiteral("Nearby goal"))->id;
    QObject *panel = window->findChild<QObject *>(QStringLiteral("searchPanel"));
    QObject *field = window->findChild<QObject *>(QStringLiteral("findField"));
    QObject *backSwitch = window->findChild<QObject *>(QStringLiteral("alsoBackSwitch"));
    QVERIFY(panel && field && backSwitch);
    const auto click = [&](const char *name) {
        QObject *button = window->findChild<QObject *>(QLatin1String(name));
        return button && button->property("visible").toBool() && QMetaObject::invokeMethod(button, "clicked");
    };
    const auto search = [&](const QString &text) {
        field->setProperty("text", text);
        QVERIFY(QMetaObject::invokeMethod(panel, "refresh"));
    };
    const auto resultIds = [&] {
        QStringList ids;
        for (const QVariant &row : panel->property("results").toList())
            if (!row.toMap().value(QStringLiteral("header")).toBool()) ids.append(row.toMap().value(QStringLiteral("id")).toString());
        return ids;
    };

    // The topic bar's button opens the panel as a picker: the topic itself is not offered.
    map->select(startId);
    QVERIFY(click("topicLinkButton"));
    QTRY_VERIFY(panel->property("opened").toBool());
    QVERIFY(panel->property("picking").toBool());
    QVERIFY(backSwitch->property("visible").toBool());
    QVERIFY(!window->findChild<QObject *>(QStringLiteral("removeTopicLinkButton"))->property("visible").toBool());
    search(QStringLiteral("e"));
    QVERIFY(!resultIds().contains(startId));
    QVERIFY(resultIds().contains(nearbyId));
    // A target in the same sheet: one link, one Undo step.
    QVERIFY(QMetaObject::invokeMethod(panel, "reveal", Q_ARG(QVariant, 0), Q_ARG(QVariant, nearbyId)));
    QTRY_VERIFY(!panel->property("visible").toBool());
    MindNode *start = topicTitled(QStringLiteral("Start here"));
    QCOMPARE(start->topicLink, nearbyId);
    QVERIFY(topicTitled(QStringLiteral("Nearby goal"))->topicLink.isEmpty());
    QCOMPARE(map->selectedId(), startId);
    QVERIFY(!map->snapshot()->topicOf(start)->geometry->linkIconRect(true).isEmpty());
    QVERIFY(map->undo());
    QVERIFY(topicTitled(QStringLiteral("Start here"))->topicLink.isEmpty());
    QVERIFY(map->redo());

    // Linked again to a topic in the other sheet, with the back link: one Undo step for both.
    map->select(startId);
    QVERIFY(click("topicLinkButton"));
    QTRY_VERIFY(panel->property("opened").toBool());
    QVERIFY(window->findChild<QObject *>(QStringLiteral("removeTopicLinkButton"))->property("visible").toBool());
    QVERIFY(!backSwitch->property("checked").toBool());
    QVERIFY(QMetaObject::invokeMethod(backSwitch, "toggle"));
    QVERIFY(QMetaObject::invokeMethod(backSwitch, "toggled"));
    QVERIFY(panel->property("alsoBack").toBool());
    search(QStringLiteral("faraway"));
    const QVariantList rows = panel->property("results").toList();
    QCOMPARE(rows.size(), 2);  // the sheet's name, then the topic
    const QVariantMap far = rows.at(1).toMap();
    const int farSheet = far.value(QStringLiteral("sheet")).toInt();
    const QString farId = far.value(QStringLiteral("id")).toString();
    QCOMPARE(farSheet, 1);
    QVERIFY(QMetaObject::invokeMethod(panel, "reveal", Q_ARG(QVariant, farSheet), Q_ARG(QVariant, farId)));
    QTRY_VERIFY(!panel->property("visible").toBool());
    start = topicTitled(QStringLiteral("Start here"));
    QCOMPARE(start->topicLink, farId);
    QVERIFY(map->topicHasLink(farSheet, farId));
    QVERIFY(map->isDirty());
    QVERIFY(map->undo());
    QCOMPARE(topicTitled(QStringLiteral("Start here"))->topicLink, nearbyId);
    QVERIFY(!map->topicHasLink(farSheet, farId));
    QVERIFY(map->redo());
    QVERIFY(map->topicHasLink(farSheet, farId));

    // A tap on the chain: the other sheet, the collapsed branch opened, the target selected.
    canvas->zoomStep(0);
    start = topicTitled(QStringLiteral("Start here"));
    const MapSnapshot::Topic *topic = map->snapshot()->topicOf(start);
    canvas->reveal(topic->bounds);
    map->select(startId);
    QVERIFY(!map->canFollowBack());
    const QRectF chain = topic->geometry->linkIconRect(true).translated(topic->position);
    QVERIFY(!chain.isEmpty());
    QSignalSpy topicLinkRequested(canvas, &MapCanvas::topicLinkRequested);
    const QPointF onChain = canvas->itemPoint(chain.center());
    canvas->tapAt(onChain.x(), onChain.y());
    QCOMPARE(topicLinkRequested.count(), 1);
    QTRY_COMPARE(map->currentSheet(), 1);
    QCOMPARE(map->selectedId(), farId);
    QVERIFY(map->canFollowBack());
    // Its back link leads home again; Back after the two jumps returns along them.
    QCOMPARE(map->selectedTopicLink(), startId);
    QVERIFY(map->followTopicLink());
    QCOMPARE(map->currentSheet(), 0);
    QCOMPARE(map->selectedId(), startId);
    QVERIFY(map->followBack());
    QCOMPARE(map->currentSheet(), 1);
    QCOMPARE(map->selectedId(), farId);
    QVERIFY(map->followBack());
    QCOMPARE(map->currentSheet(), 0);
    QCOMPARE(map->selectedId(), startId);
    QVERIFY(!map->canFollowBack());
    // Another selection forgets the jumps: Back then leaves the map as before.
    QVERIFY(map->followTopicLink());
    QVERIFY(map->canFollowBack());
    map->select(QString());
    QVERIFY(!map->canFollowBack());

    // The target deleted: the chain turns grey and a tap says so.
    QVERIFY(map->switchSheet(1));
    MindNode *farNode = map->document()->model()->nodeById(farId);
    QVERIFY(farNode);
    map->select(farId);
    QVERIFY(map->removeSelected());
    QVERIFY(map->switchSheet(0));
    start = topicTitled(QStringLiteral("Start here"));
    QTRY_VERIFY(map->snapshot()->linkBroken(start));
    map->select(startId);
    QVERIFY(!map->followTopicLink());
    QVERIFY(map->message().contains(QStringLiteral("no longer exists")));
    QCOMPARE(map->currentSheet(), 0);
    // Remove link in the picker takes it off in one Undo step.
    QVERIFY(click("topicLinkButton"));
    QTRY_VERIFY(panel->property("opened").toBool());
    QVERIFY(click("removeTopicLinkButton"));
    QTRY_VERIFY(!panel->property("visible").toBool());
    QVERIFY(topicTitled(QStringLiteral("Start here"))->topicLink.isEmpty());
    QVERIFY(map->undo());
    QCOMPARE(topicTitled(QStringLiteral("Start here"))->topicLink, farId);

    // Saved and opened again: the links are in the file.
    QVERIFY(map->close());
    {
        MobileMap reopened;
        QVERIFY(reopened.open(copy));
        QVERIFY(reopened.switchSheet(0));
        QCOMPARE(childTitled(reopened.document()->model()->root(), QStringLiteral("Start here"))->topicLink, farId);
        QVERIFY(reopened.close());
    }
    for (const QQmlError &qmlError : std::as_const(errors)) qWarning("%s", qPrintable(qmlError.toString()));
    QVERIFY(errors.isEmpty());
}

// FEAT-115: boundaries drawn as the desktop places them, created on one topic or the multiple
// selection with the desktop's rules and messages, selected by a tap, titled, deleted and made a
// relationship's target, each one Undo step; saved in the file.
void MobileTest::boundariesOnThePhone() {
    // The default dash: 7 px on, 2 px off, whatever the width (QPen's pattern in units of it).
    const Boundaries::Look look = Boundaries::look(Boundary{}, NodeStyle::Backdrop{}, QString());
    const QList<QPolygonF> dashed = MapScene::dashes(QPolygonF{QPointF(0, 0), QPointF(90, 0)}, look.pen);
    QCOMPARE(dashed.size(), 10);
    QCOMPARE(QLineF(dashed.front().first(), dashed.front().last()).length(), 7.0);
    QCOMPARE(dashed.at(1).first(), QPointF(9, 0));

    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Bounded.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MapCanvas canvas;
    canvas.setSize(QSizeF(400, 800));
    canvas.setMap(&map);
    MindMapModel *model = map.document()->model();
    MindNode *root = model->root();
    MindNode *first = const_cast<MindNode *>(childTitled(root, QStringLiteral("First")));
    MindNode *second = const_cast<MindNode *>(childTitled(root, QStringLiteral("Second")));
    QVERIFY(first && second);
    const auto shapeOf = [&](const MindNode *node) {
        const MapSnapshot::Topic *topic = map.snapshot()->topicOf(node);
        return topic->geometry->shapeRect().translated(topic->position);
    };

    // The central topic is refused with the desktop's message.
    map.select(root->id);
    QVERIFY(!map.addBoundary());
    QCOMPARE(map.message(), QStringLiteral("The central topic cannot have a boundary"));
    QVERIFY(model->boundaries.isEmpty());

    // One topic: a boundary 8 px around it, selected; one Undo step with the layout.
    map.select(first->id);
    QVERIFY(map.addBoundary());
    QCOMPARE(model->boundaries.size(), 1);
    const QString boundaryId = model->boundaries.front().id;
    QCOMPARE(map.selectedBoundaryId(), boundaryId);
    QVERIFY(map.selectedId().isEmpty());
    QCOMPARE(map.snapshot()->boundaries().size(), 1);
    const MapSnapshot::BoundaryShape *drawn = map.snapshot()->boundaryOf(boundaryId);
    QVERIFY(drawn);
    QCOMPARE(drawn->stroke, Boundaries::strokeRect(shapeOf(first)));
    QVERIFY(drawn->titleRect.isEmpty());
    QVERIFY(map.snapshot()->bounds().contains(drawn->stroke));
    MapScene::Vertices vertices;
    MapScene::appendBoundary(vertices, *drawn);
    QVERIFY(!vertices.isEmpty());
    map.select(first->id);
    QVERIFY(!map.addBoundary());  // BUG-024: no second one on the same run
    QCOMPARE(map.message(), QStringLiteral("These topics already have a boundary"));
    QVERIFY(map.undo());
    QVERIFY(model->boundaries.isEmpty());
    QVERIFY(map.snapshot()->boundaries().isEmpty());
    QVERIFY(map.redo());
    QCOMPARE(model->boundaries.size(), 1);

    // A tap on its fill beside the topic selects it; so does one just outside its border.
    canvas.zoomStep(0);
    canvas.reveal(map.snapshot()->boundaryOf(boundaryId)->frame());
    const QRectF stroke = map.snapshot()->boundaryOf(boundaryId)->stroke;
    const QPointF onFill(stroke.left() + 3, stroke.center().y());
    QVERIFY(!map.snapshot()->topicAt(onFill));
    map.select(QString());
    QPointF at = canvas.itemPoint(onFill);
    canvas.tapAt(at.x(), at.y());
    QCOMPARE(map.selectedBoundaryId(), boundaryId);
    map.select(QString());
    QVERIFY(map.selectedBoundaryId().isEmpty());
    at = canvas.itemPoint(QPointF(stroke.left() - 10, stroke.center().y()));
    canvas.tapAt(at.x(), at.y());
    QCOMPARE(map.selectedBoundaryId(), boundaryId);
    // A tap on the topic selects the topic instead.
    at = canvas.itemPoint(shapeOf(first).center());
    canvas.tapAt(at.x(), at.y());
    QCOMPARE(map.selectedId(), first->id);
    QVERIFY(map.selectedBoundaryId().isEmpty());
    QVERIFY(!map.renameBoundary(QStringLiteral("none selected")));

    // The title, one line as the desktop's: the tab on the top edge, the room above it, one step.
    canvas.tapAt(canvas.itemPoint(onFill).x(), canvas.itemPoint(onFill).y());
    QCOMPARE(map.selectedBoundaryId(), boundaryId);
    QVERIFY(map.renameBoundary(QStringLiteral("  Exam\npart ")));
    QCOMPARE(model->boundaries.front().title, QStringLiteral("Exam part"));
    QCOMPARE(map.selectedBoundaryTitle(), QStringLiteral("Exam part"));
    QCOMPARE(map.selectedBoundaryId(), boundaryId);
    drawn = map.snapshot()->boundaryOf(boundaryId);
    QVERIFY(!drawn->titleRect.isEmpty());
    QCOMPARE(drawn->titleRect.bottom(), drawn->stroke.top());
    QCOMPARE(drawn->titleRect.left(), drawn->stroke.left() + Boundaries::kTitleInsetPx);
    QVERIFY(map.snapshot()->boundaryAt(drawn->titleRect.center(), 0));  // the tab is a target too
    QVERIFY(!map.renameBoundary(QStringLiteral("Exam part")));  // unchanged: no step
    QVERIFY(map.undo());
    QVERIFY(model->boundaries.front().title.isEmpty());
    QCOMPARE(map.selectedBoundaryId(), boundaryId);  // still selected after the step
    QVERIFY(map.redo());

    // A relationship from a topic to the boundary, drawn to its outline.
    map.select(second->id);
    map.startRelation(false);
    at = canvas.itemPoint(map.snapshot()->boundaryOf(boundaryId)->titleRect.center());
    canvas.tapAt(at.x(), at.y());
    QCOMPARE(model->relations.size(), 1);
    QCOMPARE(model->relations.front().to, boundaryId);
    QCOMPARE(map.snapshot()->relations().size(), 1);
    QVERIFY(map.undo());

    // Delete removes the boundary, its topics stay; one Undo brings it back.
    map.select(QString());
    canvas.tapAt(canvas.itemPoint(onFill).x(), canvas.itemPoint(onFill).y());
    QCOMPARE(map.selectedBoundaryId(), boundaryId);
    QVERIFY(map.removeSelected());
    QVERIFY(model->boundaries.isEmpty());
    QVERIFY(map.selectedBoundaryId().isEmpty());
    QVERIFY(model->nodeById(first->id));
    QVERIFY(map.undo());
    QCOMPARE(model->boundaries.size(), 1);

    // Nested: a boundary on a child widens its parent's, and is drawn above it. A sheet switch
    // draws the sheet again from the model, and replaces its topics: they are found again by id.
    const QString firstId = first->id, secondId = second->id;
    const auto byId = [&](const QString &id) { return model->nodeById(id); };
    MindNode *inner = map.document()->addChild(first, QStringLiteral("Inner"));
    QVERIFY(inner);
    QVERIFY(map.document()->addBoundaries({inner}).size() == 1);
    QVERIFY(map.switchSheet(1));
    QVERIFY(map.switchSheet(0));
    QCOMPARE(map.snapshot()->boundaries().size(), 2);
    const MapSnapshot::BoundaryShape &outer = map.snapshot()->boundaries().at(0);
    const MapSnapshot::BoundaryShape &nested = map.snapshot()->boundaries().at(1);
    QCOMPARE(outer.boundary.id, boundaryId);
    QVERIFY(outer.stroke.contains(nested.frame()));
    QCOMPARE(map.snapshot()->boundaryAt(nested.stroke.center() + QPointF(0, nested.stroke.height() / 2 - 2), 0)->boundary.id,
             nested.boundary.id);
    // Collapsed: the hidden branch is left out of the outer one, the inner one is not drawn.
    QVERIFY(map.document()->setCollapsed(byId(firstId), true));
    QVERIFY(map.switchSheet(1));
    QVERIFY(map.switchSheet(0));
    QCOMPARE(map.snapshot()->boundaries().size(), 1);
    QCOMPARE(map.snapshot()->boundaries().front().stroke, Boundaries::strokeRect(shapeOf(byId(firstId))));
    QVERIFY(map.document()->setCollapsed(byId(firstId), false));

    // The multiple selection: one boundary per parent and side, all in one Undo step.
    MindNode *third = map.document()->addChild(model->root(), QStringLiteral("Third"));
    QVERIFY(third);
    const QString thirdId = third->id;
    QVERIFY(map.switchSheet(1));
    QVERIFY(map.switchSheet(0));
    const MapLayout *layout = map.snapshot()->layout();
    QSet<int> sides{layout->sideOf(byId(secondId)), layout->sideOf(byId(thirdId))};
    map.select(secondId);
    map.startMultiSelect();
    map.toggleInSelection(thirdId);
    QCOMPARE(map.selectedIds().size(), 2);
    const qsizetype before = model->boundaries.size();
    QVERIFY(map.addBoundary());
    QCOMPARE(model->boundaries.size(), before + sides.size());
    QVERIFY(!map.multiSelect());  // the new boundary is selected alone
    QVERIFY(!map.selectedBoundaryId().isEmpty());
    QVERIFY(map.undo());
    QCOMPARE(model->boundaries.size(), before);

    // Saved and read again from the file.
    map.select(QString());
    canvas.tapAt(canvas.itemPoint(onFill).x(), canvas.itemPoint(onFill).y());
    QCOMPARE(map.selectedBoundaryTitle(), QStringLiteral("Exam part"));
    QVERIFY(map.close());
    MobileMap reopened;
    QVERIFY(reopened.open(path));
    QCOMPARE(reopened.document()->model()->boundaries.size(), 2);
    QVERIFY(reopened.snapshot()->boundaryOf(boundaryId));
    QCOMPARE(reopened.snapshot()->boundaryOf(boundaryId)->boundary.title, QStringLiteral("Exam part"));
    QVERIFY(reopened.close());
}

// FEAT-115 on the page: the topic bar's Boundary, then the boundary's bar (Rename and Delete), its
// title typed in the tab with the placeholder while empty, Esc leaving it as it was.
void MobileTest::boundariesOnThePage() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Bounds.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *library = window->findChild<MapLibrary *>();
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *canvas = nullptr;
    QTRY_VERIFY((canvas = window->findChild<MapCanvas *>()));
    QTRY_VERIFY(canvas->width() > 0 && canvas->map()->isOpen());
    MobileMap *map = canvas->map();
    const QString copy = map->document()->filePath();
    const auto cleanup = qScopeGuard([&] {
        map->close();
        library->discard(copy);
    });
    QObject *page = canvas->parentItem();
    while (page && !page->property("editingBoundary").isValid()) page = page->parent();
    QVERIFY(page);
    MindMapModel *model = map->document()->model();
    const MindNode *first = childTitled(model->root(), QStringLiteral("First"));
    QVERIFY(first);
    const auto item = [&](const char *name) { return window->findChild<QQuickItem *>(QLatin1String(name)); };
    const auto click = [&](const char *name) {
        QQuickItem *button = item(name);
        return button && button->isVisible() && button->isEnabled() && QMetaObject::invokeMethod(button, "clicked");
    };

    map->select(first->id);
    QTRY_VERIFY(item("boundaryButton")->isVisible());
    QCOMPARE(item("boundaryButton")->property("text").toString(), QStringLiteral("Add a boundary around the topic"));
    QVERIFY(click("boundaryButton"));
    QCOMPARE(model->boundaries.size(), 1);
    QCOMPARE(map->selectedBoundaryId(), model->boundaries.front().id);
    // The boundary's bar: Rename and Delete only.
    QTRY_VERIFY(item("renameButton")->isVisible());
    QVERIFY(item("deleteButton")->isVisible() && item("deleteButton")->isEnabled());
    for (const char *name : {"selectMoreButton", "boundaryButton", "relationButton", "noteButton", "copyTopicsButton"})
        QVERIFY2(!item(name)->isVisible(), name);
    // Rename: the field in the tab, the placeholder while empty; the title written when it ends.
    auto *field = item("titleEditor");
    QVERIFY(click("renameButton"));
    QTRY_VERIFY(field->isVisible());
    QVERIFY(page->property("editingBoundary").toBool());
    QVERIFY(field->setProperty("text", QStringLiteral("Chapter one")));
    QVERIFY(model->boundaries.front().title.isEmpty());
    QVERIFY(QMetaObject::invokeMethod(page, "endEdit"));
    QCOMPARE(model->boundaries.front().title, QStringLiteral("Chapter one"));
    QVERIFY(!page->property("editingBoundary").toBool());
    // The field starts after the tab's padding, on the tab's middle.
    QVERIFY(QMetaObject::invokeMethod(page, "beginEdit", Q_ARG(QVariant, false)));
    QCOMPARE(field->property("text").toString(), QStringLiteral("Chapter one"));
    const QRectF tab = map->snapshot()->boundaryOf(map->selectedBoundaryId())->titleRect;
    QCOMPARE(canvas->editorAnchor(), canvas->itemPoint(QPointF(tab.left() + Boundaries::kTitlePaddingPx, tab.center().y())));
    QVERIFY(field->setProperty("text", QStringLiteral("changed")));
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!field->isVisible());
    QCOMPARE(model->boundaries.front().title, QStringLiteral("Chapter one"));
    QVERIFY(click("deleteButton"));
    QVERIFY(model->boundaries.isEmpty());
    QVERIFY(map->undo());
    QCOMPARE(model->boundaries.size(), 1);
    QVERIFY(errors.isEmpty());
}

// FEAT-115 step 2: summaries placed as the desktop places them (bracket beside the run, the summary
// topic beside it, its branch away from it, the siblings apart for a tall one), created on one topic
// or the multiple selection with the desktop's rules and messages, the summary topic a topic (its
// subtopics, a relationship's end, Delete), each one Undo step; saved in the file.
void MobileTest::summariesOnThePhone() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Summed.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MapCanvas canvas;
    canvas.setSize(QSizeF(400, 800));
    canvas.setMap(&map);
    MindMapModel *model = map.document()->model();
    const auto byTitle = [&](const QString &title) { return const_cast<MindNode *>(childTitled(model->root(), title)); };
    const QString firstId = byTitle(QStringLiteral("First"))->id, secondId = byTitle(QStringLiteral("Second"))->id;
    const auto byId = [&](const QString &id) { return model->nodeById(id); };
    const auto shapeOf = [&](const MindNode *node) {
        const MapSnapshot::Topic *topic = map.snapshot()->topicOf(node);
        return topic ? topic->geometry->shapeRect().translated(topic->position) : QRectF();
    };
    // A logic chart: the main topics in one column on the right.
    QVERIFY(map.document()->setStructure(model->root(), MapStructure::typeKey(MapStructure::Type::LogicChart), QString()));
    QVERIFY(map.switchSheet(1));
    QVERIFY(map.switchSheet(0));

    // The central topic is refused with the desktop's message.
    map.select(model->root()->id);
    QVERIFY(!map.addSummary());
    QCOMPARE(map.message(), QStringLiteral("The central topic cannot have a summary"));
    QVERIFY(model->summaries.isEmpty());

    // One topic: the bracket on its right, the summary topic beside it, selected; no branch line
    // into it; one Undo step with the layout.
    const qsizetype branches = map.snapshot()->branches().size();
    map.select(firstId);
    QVERIFY(map.addSummary());
    QCOMPARE(model->summaries.size(), 1);
    QString summaryId = model->summaries.front().id;
    const MindNode *topic = model->summaries.front().topic.get();
    QString topicId = topic->id;
    QCOMPARE(topic->title, QStringLiteral("Summary"));
    QCOMPARE(map.selectedId(), topicId);
    QCOMPARE(map.snapshot()->summaries().size(), 1);
    const QRectF covered = shapeOf(byId(firstId));
    QCOMPARE(map.snapshot()->summaries().front().bracket, Summaries::bracket(covered, Summaries::Side::Right));
    const QRectF topicShape = shapeOf(topic);
    QVERIFY(!topicShape.isNull());
    QCOMPARE(topicShape.left(), Summaries::topicAnchor(covered, Summaries::Side::Right).x());
    QCOMPARE(topicShape.center().y(), covered.center().y());
    QCOMPARE(map.snapshot()->branches().size(), branches);
    QVERIFY(map.snapshot()->bounds().contains(map.snapshot()->summaries().front().bracket.boundingRect()));
    MapScene::Vertices vertices;
    for (const QPolygonF &polyline : MapScene::strokePolylines(map.snapshot()->summaries().front().bracket))
        MapScene::appendStroke(vertices, polyline, Summaries::kLineWidth, false, false, Qt::black);
    QVERIFY(!vertices.isEmpty());
    map.select(firstId);
    QVERIFY(!map.addSummary());  // the same run has one
    QCOMPARE(map.message(), QStringLiteral("These topics already have a summary"));
    QVERIFY(map.undo());
    QVERIFY(model->summaries.isEmpty());
    QVERIFY(map.snapshot()->summaries().isEmpty());
    QVERIFY(map.redo());
    QCOMPARE(model->summaries.size(), 1);
    QVERIFY(byId(topicId));  // the same topic again

    // The title typed after it joins the step, as a new topic's.
    QVERIFY(map.undo());
    map.select(firstId);
    map.setEditingTitle(true);
    QVERIFY(map.addSummary());
    QVERIFY(map.rename(QStringLiteral("Total")));
    map.setEditingTitle(false);
    QCOMPARE(model->summaries.front().topic->title, QStringLiteral("Total"));
    QVERIFY(map.undo());
    QVERIFY(model->summaries.isEmpty());
    QVERIFY(map.redo());
    QCOMPARE(model->summaries.front().topic->title, QStringLiteral("Total"));
    summaryId = model->summaries.front().id;
    topicId = model->summaries.front().topic->id;
    topic = byId(topicId);
    QVERIFY(topic);

    // A tap on the summary topic selects it: only the child knob, no move; no collapse control
    // while it has no subtopics.
    canvas.zoomStep(0);
    canvas.reveal(shapeOf(topic));
    map.select(QString());
    QPointF at = canvas.itemPoint(shapeOf(topic).center());
    canvas.tapAt(at.x(), at.y());
    QCOMPARE(map.selectedId(), topicId);
    QVERIFY(canvas.childKnobTarget().width() > 0);
    QVERIFY(canvas.siblingKnobTarget().isEmpty());
    QVERIFY(!map.canCollapse());
    QVERIFY(canvas.collapseTarget().isEmpty());
    QVERIFY(!map.addSibling());
    QVERIFY(!canvas.beginMove(at.x(), at.y()));

    // Its subtopics, laid out away from the bracket with their lines; a tall branch moves the
    // siblings apart (the layout's room from the last placement).
    const qreal gap = shapeOf(byId(secondId)).top() - covered.bottom();
    for (int i = 0; i < 4; ++i) {
        map.select(topicId);
        QVERIFY(map.addChild());
    }
    QCOMPARE(byId(topicId)->children.size(), 4);
    QCOMPARE(map.snapshot()->branches().size(), branches + 4);
    const MindNode *sub = byId(topicId)->children.front().get();
    QVERIFY(shapeOf(sub).left() > shapeOf(byId(topicId)).right());
    QVERIFY(!canvas.beginMove(canvas.itemPoint(shapeOf(sub).center()).x(), canvas.itemPoint(shapeOf(sub).center()).y()));
    map.select(firstId);
    QVERIFY(map.rename(QStringLiteral("First topic")));  // another edit: the room is read from this layout
    QVERIFY(shapeOf(byId(secondId)).top() - shapeOf(byId(firstId)).bottom() > gap);
    const QRectF summaryBranch = shapeOf(byId(topicId)).united(shapeOf(byId(topicId)->children.back().get()));
    QVERIFY(!summaryBranch.intersects(shapeOf(byId(secondId))));

    // With subtopics it collapses (user, 2026-11-12, as the desktop): the control on the branch
    // side, the subtopics and their lines hidden, one Undo step, kept in the file.
    map.select(topicId);
    QVERIFY(map.canCollapse());
    QVERIFY(canvas.collapseControl().left() > canvas.itemPoint(shapeOf(byId(topicId)).center()).x());
    QVERIFY(map.toggleCollapsed());
    QVERIFY(byId(topicId)->collapsed);
    QVERIFY(map.selectedCollapsed());
    QCOMPARE(canvas.collapseCount(), 4);
    QVERIFY(!map.snapshot()->topicOf(byId(topicId)->children.front().get()));
    QCOMPARE(map.snapshot()->branches().size(), branches);
    MindMapModel closed;
    QVERIFY(closed.fromJson(model->toJson()));
    QVERIFY(closed.summaries.front().topic->collapsed);
    QVERIFY(map.undo());
    QVERIFY(!byId(topicId)->collapsed);
    QCOMPARE(map.snapshot()->branches().size(), branches + 4);

    // A relationship from a topic to the summary topic.
    map.select(secondId);
    map.startRelation(false);
    at = canvas.itemPoint(shapeOf(byId(topicId)).center());
    canvas.reveal(shapeOf(byId(topicId)));
    at = canvas.itemPoint(shapeOf(byId(topicId)).center());
    canvas.tapAt(at.x(), at.y());
    QCOMPARE(model->relations.size(), 1);
    QCOMPARE(model->relations.front().to, topicId);
    QCOMPARE(map.snapshot()->relations().size(), 1);

    // Delete on the summary topic removes the summary with its branch and the relationship; the
    // covered topic stays; one Undo brings it all back.
    map.select(topicId);
    QVERIFY(map.removeSelected());
    QVERIFY(model->summaries.isEmpty());
    QVERIFY(model->relations.isEmpty());
    QVERIFY(byId(firstId));
    QVERIFY(map.snapshot()->summaries().isEmpty());
    QVERIFY(map.undo());
    QCOMPARE(model->summaries.size(), 1);
    QCOMPARE(model->relations.size(), 1);
    QCOMPARE(byId(topicId)->children.size(), 4);

    // Tree tables refuse summaries with the desktop's message.
    MindNode *table = map.document()->addChild(byId(secondId), QStringLiteral("Row"));
    QVERIFY(table && map.document()->setStructure(byId(secondId), MapStructure::typeKey(MapStructure::Type::TreeTable), QString()));
    const QString rowId = table->id;
    QVERIFY(map.switchSheet(1));
    QVERIFY(map.switchSheet(0));
    map.select(rowId);
    QVERIFY(!map.addSummary());
    QCOMPARE(map.message(), QStringLiteral("Tree tables cannot have summaries"));
    QVERIFY(map.document()->setStructure(byId(secondId), MapStructure::typeKey(MapStructure::Type::LogicChart), QString()));

    // An org chart: a row, the bracket below the run.
    MindNode *third = map.document()->addChild(model->root(), QStringLiteral("Third"));
    QVERIFY(third);
    const QString thirdId = third->id;
    QVERIFY(map.document()->setStructure(model->root(), MapStructure::typeKey(MapStructure::Type::OrgChart), QString()));
    QVERIFY(map.switchSheet(1));
    QVERIFY(map.switchSheet(0));
    map.select(secondId);
    map.startMultiSelect();
    map.toggleInSelection(thirdId);
    QCOMPARE(map.selectedIds().size(), 2);
    QVERIFY(map.addSummary());
    QCOMPARE(model->summaries.size(), 2);
    QVERIFY(!map.multiSelect());  // the new summary topic is selected alone
    const MindNode *rowTopic = model->summaries.back().topic.get();
    QCOMPARE(map.selectedId(), rowTopic->id);
    const QRectF run = map.snapshot()->summaries().back().bracket.boundingRect();
    QVERIFY(run.top() >= shapeOf(byId(thirdId)).bottom());
    QVERIFY(shapeOf(rowTopic).top() >= run.bottom());
    QVERIFY(map.undo());
    QCOMPARE(model->summaries.size(), 1);

    // Saved and read again from the file.
    QVERIFY(map.close());
    MobileMap reopened;
    QVERIFY(reopened.open(path));
    QCOMPARE(reopened.document()->model()->summaries.size(), 1);
    QCOMPARE(reopened.document()->model()->summaries.front().id, summaryId);
    const MindNode *saved = reopened.document()->model()->nodeById(topicId);
    QVERIFY(saved);
    QCOMPARE(saved->title, QStringLiteral("Total"));
    QCOMPARE(saved->children.size(), 4);
    QCOMPARE(reopened.snapshot()->summaries().size(), 1);
    QVERIFY(reopened.snapshot()->topicOf(saved));
    QVERIFY(reopened.close());
}

// FEAT-115 step 2 on the page: the topic bar's Summary, the new summary topic's title open for
// typing, one Undo step with it.
void MobileTest::summariesOnThePage() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Sums.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *library = window->findChild<MapLibrary *>();
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *canvas = nullptr;
    QTRY_VERIFY((canvas = window->findChild<MapCanvas *>()));
    QTRY_VERIFY(canvas->width() > 0 && canvas->map()->isOpen());
    MobileMap *map = canvas->map();
    const QString copy = map->document()->filePath();
    const auto cleanup = qScopeGuard([&] {
        map->close();
        library->discard(copy);
    });
    MindMapModel *model = map->document()->model();
    const MindNode *first = childTitled(model->root(), QStringLiteral("First"));
    QVERIFY(first);
    const auto item = [&](const char *name) { return window->findChild<QQuickItem *>(QLatin1String(name)); };
    const auto click = [&](const char *name) {
        QQuickItem *button = item(name);
        return button && button->isVisible() && button->isEnabled() && QMetaObject::invokeMethod(button, "clicked");
    };

    map->select(first->id);
    QTRY_VERIFY(item("summaryButton")->isVisible());
    QCOMPARE(item("summaryButton")->property("text").toString(), QStringLiteral("Add a summary to the topic"));
    QVERIFY(click("summaryButton"));
    QCOMPARE(model->summaries.size(), 1);
    QCOMPARE(map->selectedId(), model->summaries.front().topic->id);
    // The title is open, the whole of it selected: the typed text replaces it.
    auto *field = item("titleEditor");
    QTRY_VERIFY(field->isVisible());
    QCOMPARE(field->property("selectedText").toString(), QStringLiteral("Summary"));
    for (const char key : {'S', 'u', 'm'}) QTest::keyClick(window, key);
    QTRY_COMPARE(model->summaries.front().topic->title, QStringLiteral("Sum"));
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_VERIFY(!field->isVisible());
    QVERIFY(map->undo());  // the summary and its title, one step
    QVERIFY(model->summaries.isEmpty());
    // The central topic: refused, the message shown.
    map->select(model->root()->id);
    QVERIFY(click("summaryButton"));
    QCOMPARE(map->message(), QStringLiteral("The central topic cannot have a summary"));
    QVERIFY(errors.isEmpty());
}

// FEAT-116: a topic's formula set as the desktop's FormulaDialog sets it (natural size, an image or
// sticker replaced, the size kept when the code stays), removed and copied, each one Undo step;
// a title's $...$ laid out as a formula; saved in the file.
void MobileTest::formulasOnThePhone() {
    const QString sticker = StickerLibrary().collections().constFirst().toMap().value(QStringLiteral("items")).toList()
                                .constFirst().toMap().value(QStringLiteral("path")).toString();
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Formulas.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    {
        MobileMap map;
        QVERIFY(map.open(path));
        MapCanvas canvas;
        canvas.setSize(QSizeF(400, 800));
        canvas.setMap(&map);
        const MindNode *first = childTitled(map.document()->model()->root(), QStringLiteral("First"));
        map.select(first->id);
        QVERIFY(map.insertSticker(sticker));
        QVERIFY(!map.setFormula(QStringLiteral("  ")));
        // Inserted: the sticker replaced, the natural size, below the title, white.
        QVERIFY(map.setFormula(QStringLiteral("\\frac{a}{b}")));
        QCOMPARE(first->formula, QStringLiteral("\\frac{a}{b}"));
        QVERIFY(first->imagePath.isEmpty());
        QCOMPARE(first->imageSize, FormulaText::boxSize(first->formula, NodeGeometry::titleFont(first)));
        QCOMPARE(first->imagePlacement, MindNode::ImagePlacement::Below);
        QCOMPARE(map.selectedFormula(), first->formula);
        QCOMPARE(map.selectedFormulaBackground(), QColor(Qt::white));
        QVERIFY(map.undo());  // one step back to the sticker
        QVERIFY(first->formula.isEmpty());
        QVERIFY(first->imageSticker && !first->imagePath.isEmpty());
        QVERIFY(map.redo());
        QCOMPARE(first->formula, QStringLiteral("\\frac{a}{b}"));
        const MapSnapshot::Topic *topic = map.snapshot()->topicOf(first);
        QVERIFY(!topic->geometry->imageRect().isEmpty());
        // Picked by a tap on it, as a picture.
        canvas.zoomStep(0);
        canvas.reveal(map.selectedTopic()->bounds);
        topic = map.snapshot()->topicOf(first);
        const QPointF at = canvas.itemPoint(topic->geometry->imageRect().translated(topic->position).center());
        canvas.tapAt(at.x(), at.y());
        QVERIFY(map.pictureSelected());
        // The same code changes nothing, and a resized formula keeps its size.
        QVERIFY(!map.setFormula(QStringLiteral("\\frac{a}{b}  ")));
        QVERIFY(map.resizePicture(first->imageSize * 2));
        const QSizeF resized = first->imageSize;
        QVERIFY(!map.setFormula(QStringLiteral("\\frac{a}{b}")));
        QCOMPARE(first->imageSize, resized);
        // Edited: the natural size again, one Undo step back to the first code.
        QVERIFY(map.setFormula(QStringLiteral("x^2")));
        QCOMPARE(first->imageSize, FormulaText::boxSize(QStringLiteral("x^2"), NodeGeometry::titleFont(first)));
        QVERIFY(map.undo());
        QCOMPARE(first->formula, QStringLiteral("\\frac{a}{b}"));
        QCOMPARE(first->imageSize, resized);
        // Copy the LaTeX code: the code as text, said in the message.
        QVERIFY(map.copyFormulaCode());
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("\\frac{a}{b}"));
        QCOMPARE(map.message(), QStringLiteral("LaTeX code copied"));
        // Removed: the picture is no longer picked; Undo brings it back.
        QVERIFY(map.removeFormula());
        QVERIFY(first->formula.isEmpty());
        QVERIFY(!map.pictureSelected());
        QVERIFY(!map.removeFormula());
        QVERIFY(!map.copyFormulaCode());
        QVERIFY(map.undo());
        QCOMPARE(first->formula, QStringLiteral("\\frac{a}{b}"));
        // A title with $...$ is laid out as a formula (FormulaText's hooks are installed).
        map.select(first->id);
        QVERIFY(map.rename(QStringLiteral("Area $\\pi r^2$")));
        QVERIFY(map.snapshot()->topicOf(first)->geometry->formula());
        QVERIFY(map.save());
        map.close();
    }
    // Saved: the desktop's model reads the formula back.
    MobileMap reopened;
    QVERIFY(reopened.open(path));
    const MindNode *first = childTitled(reopened.document()->model()->root(), QStringLiteral("Area $\\pi r^2$"));
    QVERIFY(first);
    QCOMPARE(first->formula, QStringLiteral("\\frac{a}{b}"));
    reopened.close();
}

// FEAT-116: the topic bar's Formula opens the modal (the code, the live preview, the desktop's
// error and a disabled Insert while it is not valid); the formula is drawn in its place and a
// title's formula in the title; with the formula picked the bar holds Remove, Edit the formula and
// Copy the LaTeX code; the modal's Remove takes it off.
void MobileTest::formulasOnThePage() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Formulas.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *library = window->findChild<MapLibrary *>();
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *canvas = nullptr;
    QTRY_VERIFY((canvas = window->findChild<MapCanvas *>()));
    QTRY_VERIFY(canvas->width() > 0 && canvas->map()->isOpen());
    MobileMap *map = canvas->map();
    const QString copy = map->document()->filePath();
    const auto cleanup = qScopeGuard([&] {
        map->close();
        library->discard(copy);
    });
    MindMapModel *model = map->document()->model();
    const MindNode *first = childTitled(model->root(), QStringLiteral("First"));
    QVERIFY(first);
    const QString firstId = first->id;
    const auto item = [&](const char *name) { return window->findChild<QQuickItem *>(QLatin1String(name)); };
    const auto click = [&](const char *name) {
        QQuickItem *button = item(name);
        return button && button->isVisible() && button->isEnabled() && QMetaObject::invokeMethod(button, "clicked");
    };
    // The share of a map rectangle's pixels (in %) that differ from the topic's fill.
    const auto drawnIn = [&](const QRectF &mapRect) {
        const QImage frame = window->grabWindow();
        const QRect shown = canvas->mapRectToScene(QRectF(canvas->itemPoint(mapRect.topLeft()), canvas->itemPoint(mapRect.bottomRight())))
                                .toRect().intersected(frame.rect());
        int differing = 0, total = 0;
        for (int y = shown.top(); y <= shown.bottom(); ++y)
            for (int x = shown.left(); x <= shown.right(); ++x, ++total) differing += frame.pixelColor(x, y) != first->fill;
        return total > 16 ? differing * 100 / total : -1;
    };

    map->select(firstId);
    QTRY_VERIFY(item("formulaButton")->isVisible());
    QCOMPARE(item("formulaButton")->property("text").toString(), QStringLiteral("Insert a LaTeX formula in the topic"));
    QVERIFY(click("formulaButton"));
    QObject *dialog = window->findChild<QObject *>(QStringLiteral("formulaDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    QQuickItem *field = item("formulaCodeField");
    QQuickItem *preview = item("formulaPreview");
    QVERIFY(field && preview);
    QCOMPARE(field->property("text").toString(), QString());
    QVERIFY(!item("formulaRemoveButton")->isVisible());
    QCOMPARE(item("formulaInsertButton")->property("text").toString(), QStringLiteral("Insert"));
    QVERIFY(!item("formulaInsertButton")->isEnabled());
    // Not valid: the desktop's message, Insert disabled.
    QVERIFY(field->setProperty("text", QStringLiteral("\\frac{a}{b")));
    QVERIFY(!preview->property("valid").toBool());
    QTRY_VERIFY(item("formulaError")->isVisible());
    QVERIFY(item("formulaError")->property("text").toString().startsWith(QStringLiteral("The formula is not valid: ")));
    QVERIFY(!item("formulaInsertButton")->isEnabled());
    QVERIFY(field->setProperty("text", QStringLiteral("\\frac{a}{b}")));
    QVERIFY(preview->property("valid").toBool());
    QTRY_VERIFY(!item("formulaError")->isVisible());
    QVERIFY(preview->height() >= 96);
    QVERIFY(click("formulaInsertButton"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(first->formula, QStringLiteral("\\frac{a}{b}"));

    // Drawn in its place (white box, dark formula), and a title's formula in the title.
    canvas->fit();
    QTest::qWait(50);
    const MapSnapshot::Topic *topic = map->snapshot()->topicOf(first);
    QTRY_VERIFY(drawnIn(topic->geometry->imageRect().translated(topic->position)) > 50);
    map->select(firstId);
    QVERIFY(map->rename(QStringLiteral("$\\sum_{i=1}^{n} i$")));
    QTest::qWait(50);
    topic = map->snapshot()->topicOf(first);
    QVERIFY(topic->geometry->formula());
    const QSizeF titleFormula = topic->geometry->formula()->size();
    const QRectF titleArea = topic->geometry->textArea().translated(topic->position);
    const QRectF formulaBox(titleArea.center() - QPointF(titleFormula.width(), titleFormula.height()) / 2, titleFormula);
    QTRY_VERIFY(drawnIn(formulaBox) > 3);
    if (const QString folder = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR"); !folder.isEmpty())  // to look at it
        window->grabWindow().save(QDir(folder).filePath(QStringLiteral("mobile-formulas.png")));

    // The picked formula: Remove, Edit the formula, Copy the LaTeX code; no image tools.
    const QPointF at = canvas->itemPoint(topic->geometry->imageRect().translated(topic->position).center());
    canvas->tapAt(at.x(), at.y());
    QVERIFY(map->pictureSelected());
    QTRY_VERIFY(item("copyFormulaButton")->isVisible());
    QCOMPARE(item("formulaButton")->property("text").toString(), QStringLiteral("Edit the formula"));
    QCOMPARE(item("deleteButton")->property("text").toString(), QStringLiteral("Remove"));
    for (const char *name : {"imageButton", "copyPictureButton", "renameButton", "noteButton"}) QVERIFY2(!item(name)->isVisible(), name);
    QVERIFY(click("copyFormulaButton"));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("\\frac{a}{b}"));
    // Edit: the code in the field, Save; the modal's Remove takes it off.
    QVERIFY(click("formulaButton"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QCOMPARE(field->property("text").toString(), QStringLiteral("\\frac{a}{b}"));
    QCOMPARE(item("formulaInsertButton")->property("text").toString(), QStringLiteral("Save"));
    QVERIFY(click("formulaRemoveButton"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(first->formula.isEmpty());
    QVERIFY(map->undo());
    QCOMPARE(model->nodeById(firstId)->formula, QStringLiteral("\\frac{a}{b}"));
    QVERIFY(errors.isEmpty());
}

QTEST_MAIN(MobileTest)
#include "mobile_test.moc"
