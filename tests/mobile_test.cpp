// FEAT-097 stage A: the mobile app's adapters and pages on the desktop, without Qt Widgets. The
// local copy of a chosen map (MapLibrary), the open map and its sheets (MobileMap), the drawn map's
// positions against the desktop's (MapSnapshot, the FEAT-096 fixtures) and the QML pages loaded
// with the software renderer. Stage B1: edits through Document, their Undo steps and the saved file.
#include "document.h"
#include <QQmlProperty>
#include "formulatext.h"
#include "iconlibrary.h"
#include "markers.h"
#include "markdownshortcuts.h"
#include "layout_fixtures.h"
#include "controlkey.h"
#include "mapcanvas.h"
#include "mapexport.h"
#include "ganttexport.h"
#include "mappages.h"
#include "exportframe.h"
#include "shortcutcatalog.h"
#include "shortcuts.h"
#include "mapscene.h"
#include "mobilepitch.h"
#include "pitchchoices.h"
#include "mobilestyle.h"
#include "mapvariants.h"
#include "variantsample.h"
#include "stylechoices.h"
#include "incomingmaps.h"
#include "keycommands.h"
#include "mapfolder.h"
#include "maplibrary.h"
#include "mapsnapshot.h"
#include "mobileapp.h"
#include "mobilemap.h"
#include "mobileoutline.h"
#include "mobilegantt.h"
#include "panelsize.h"
#include "ganttcolumns.h"
#include "ganttcommands.h"
#include "mobilegantttask.h"
#include "mobileresources.h"
#include "ganttpeoplecharts.h"
#include "ganttcosts.h"
#include "mobileganttstyle.h"
#include "ganttplan.h"
#include "outlineformats.h"
#include "nodegeometry.h"
#include "nodesearch.h"
#include "nodeshape.h"
#include "performance.h"
#include "stickerlibrary.h"
#include "mobilestickereditor.h"
#include "stickercanvasview.h"
#include "svgdocument.h"
#include "stickers.h"

#include <QDesktopServices>
#include <QDir>
#include <QRegularExpression>
#include <QSettings>
#include <QFile>
#include <QFontMetricsF>
#include <QClipboard>
#include <QGuiApplication>
#include <QStyleHints>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSValue>
#include <QLineF>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSet>
#include <QTextBlock>
#include <QTextList>
#include <QSGGeometryNode>
#include <QSGOpacityNode>
#include <QPainter>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTabletEvent>
#include <private/qhighdpiscaling_p.h>
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

// FEAT-118: whether a topic bar tool is offered: on the bar, or folded under its "+".
bool onTopicBar(const QQuickItem *button) {
    return button && (button->isVisible()
                      || (button->property("adding").toBool() && button->property("folded").toBool() && button->property("shown").toBool()));
}

// FEAT-118: taps a topic bar tool on the bar or, folded, through the "+" menu's row. False when the
// tool is not offered or is disabled.
bool tapTopicTool(QQuickItem *button) {
    if (!button) return false;
    if (button->isVisible()) return button->isEnabled() && QMetaObject::invokeMethod(button, "clicked");
    if (!onTopicBar(button)) return false;
    QQuickWindow *window = button->window();
    auto *plus = window->findChild<QQuickItem *>(QStringLiteral("addToolsButton"));
    QObject *menu = window->findChild<QObject *>(QStringLiteral("addMenu"));
    if (!plus || !menu || !plus->isVisible() || !QMetaObject::invokeMethod(plus, "clicked")) return false;
    if (!QTest::qWaitFor([&] { return menu->property("opened").toBool(); })) return false;
    for (QQuickItem *row : menu->property("rows").value<QQuickItem *>()->childItems())
        if (row->objectName() == button->objectName() + QLatin1String("Row"))
            return row->isEnabled() && QMetaObject::invokeMethod(row, "clicked");
    return false;
}

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
    void ctrlTapAddsToTheSelection();
    void stickerIsInsertedAndSaved();
    void stickerEditorOnThePhone();
    void topicIsMovedByLongPress();
    void formatPainterPastesTheStyle();
    void relationshipsOnThePhone();  // FEAT-110
    void relationshipsAreDrawn();    // FEAT-110
    void pickedImageIsInsertedAndEmbedded();
    void markersAreGivenAndTakenOff();
    void filesAreAddedToTheLibrary();
    void keyboardCommands();
    void settingsPageShortcuts();
    void notesAndRichTitles();
    void markdownWhileTyping();      // FEAT-124
    void checkboxAndWebLink();
    void topicContentsAreRemovedAndPlaced();
    void topicWidthIsDraggedFromItsBorder();
    void markerMenuOnThePage();
    void pagesLoadAndOpenAMap();
    void penWorksAsTheMouse();
    void buttonsShowTheirName();
    void labelsShowTheirName();
    void mapFromAnotherAppIsOpened();
    void folderLevelsInTheLibrary();
    void homeFoldersOnThePage();
    void newMapInTheFolderShown();   // FEAT-123
    void multipleSelection();        // FEAT-111
    void multipleSelectionOnThePage();  // FEAT-111
    void sheetCommands();            // FEAT-112
    void sheetCommandsOnThePage();   // FEAT-112
    void searchFindsAndReveals();    // FEAT-113
    void searchOnThePage();          // FEAT-113
    void sidePanelStyle();           // FEAT-119 stage A
    void sidePanelObjectPages();     // FEAT-119 stage A: picture, relationship, boundary
    void sidePanelMapPage();         // FEAT-119 stage B
    void pitchOnThePhone();          // FEAT-120 stage A
    void pitchTemplatesOnThePhone(); // FEAT-120 stage B
    void exportFromTheHome();        // FEAT-120 stage C1
    void labelsOnThePage();          // FEAT-114
    void filesOnThePage();           // FEAT-114
    void topicLinksOnThePage();      // FEAT-114
    void boundariesOnThePhone();     // FEAT-115
    void boundarySplitsAcrossSides();  // FEAT-052 point 8
    void boundariesOnThePage();      // FEAT-115
    void summariesOnThePhone();      // FEAT-115 step 2
    void treeTableCells();           // FEAT-028 on the phone
    void summariesOnThePage();       // FEAT-115 step 2
    void formulasOnThePhone();       // FEAT-116
    void formulasOnThePage();        // FEAT-116
    void topicBarOverflowOnThePage();  // FEAT-118
    void outlinerOnThePhone();       // FEAT-121
    void outlinerOnThePage();        // FEAT-121
    void outlinerContents();         // FEAT-121 A3
    void outlinerMultipleSelection(); // FEAT-121 B
    void outlinerExports();          // FEAT-121 C
    void ganttOnThePhone();          // FEAT-122 A
    void ganttOnThePage();           // FEAT-122 A
    void ganttRowActions();          // FEAT-122 B1
    void ganttTaskPanel();           // FEAT-122 B2
    void ganttTaskPage();            // FEAT-122 B2
    void ganttTypingRenamesTask();   // FEAT-002 with a hardware keyboard
    void ganttChartEdits();          // FEAT-122 B3
    void ganttChartDrag();           // FEAT-122 B3
    void ganttRowDrag();             // FEAT-122: a long press on the table moves the row
    void ganttDoubleTapDates();      // FEAT-122: a double tap dates a task, a drag gives its duration
    void ganttResources();           // FEAT-122 C1
    void ganttResourcesDialog();     // FEAT-122 C1
    void ganttResourceCharts();      // FEAT-127
    void ganttStylePage();           // FEAT-122 C2
    void ganttStylePanel();          // FEAT-122 C2
    void ganttBaselines();           // FEAT-122 D
    void ganttBaselinesDialog();     // FEAT-122 D
    void ganttExports();             // FEAT-122 E
    void ganttColumns();             // FEAT-122 F

private:
    QFont m_defaultFont;  // Qt's, before MobileApp::prepare sets the interface font
};

void MobileTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisMobileTest"));
    Performance::setMode(Performance::Mode::Standard);
    // FEAT-122: the Gantt tests measure the chart beside the Name column alone (the phone before
    // stage F); ganttColumns and ganttExports take the desktop's default columns and put this back.
    QSettings().remove(QStringLiteral("gantt/columnOrder"));
    QSettings().remove(QStringLiteral("gantt/numberHidden"));
    QSettings().setValue(QStringLiteral("gantt/columns"), QStringList());
    QSettings().remove(QStringLiteral("gantt/critical"));  // the critical path shown, by default
    // The side panel's and Name's sizes at their defaults (dragged, user's request 2026-11-22).
    QSettings().remove(QStringLiteral("mobile/sidePanelWidth"));
    QSettings().remove(QStringLiteral("mobile/sidePanelBottomShare"));
    QSettings().remove(QStringLiteral("gantt/mobileNameShare"));
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

void MobileTest::ctrlTapAddsToTheSelection() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("CtrlTap.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MapCanvas canvas;
    canvas.setSize(QSizeF(400, 800));
    canvas.setMap(&map);
    const MindNode *root = map.document()->model()->root();
    const QString firstId = childTitled(root, QStringLiteral("First"))->id;
    const QString secondId = childTitled(root, QStringLiteral("Second"))->id;
    const auto tap = [&](const QString &id) {
        const QPointF at = canvas.itemPoint(map.snapshot()->topicOf(map.document()->model()->nodeById(id))->bounds.center());
        canvas.tapAt(at.x(), at.y());
    };
    ControlKey control;
    QObject receiver;  // the application's filter sees the keys of any object
    const auto ctrl = [&](bool down) {
        QKeyEvent key(down ? QEvent::KeyPress : QEvent::KeyRelease, Qt::Key_Control, down ? Qt::ControlModifier : Qt::NoModifier);
        QCoreApplication::sendEvent(&receiver, &key);
    };
    tap(firstId);
    QVERIFY(!map.multiSelect());
    ctrl(true);
    QVERIFY(ControlKey::held());
    tap(secondId);  // the mode turns on with the selected topic first
    QVERIFY(map.multiSelect());
    QCOMPARE(map.selectedIds(), (QStringList{firstId, secondId}));
    tap(firstId);  // again: removed
    QCOMPARE(map.selectedIds(), QStringList{secondId});
    ctrl(false);
    QVERIFY(!ControlKey::held());
    QVERIFY(!map.extendSelection(firstId));  // without Ctrl a plain tap
    map.stopMultiSelect();
    tap(firstId);
    QVERIFY(!map.multiSelect());
    QCOMPARE(map.selectedId(), firstId);
    // The modifier of a pen or mouse press counts too.
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(), QPointF(), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    QCoreApplication::sendEvent(&receiver, &press);
    tap(secondId);
    QCOMPARE(map.selectedIds(), (QStringList{firstId, secondId}));
    ctrl(false);
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
    // Editing its title: the topic readable and inside the band above the keyboard.
    const auto onScreen = [&] {
        return QRectF(canvas.itemPoint(map.selectedTopic()->bounds.topLeft()), canvas.itemPoint(map.selectedTopic()->bounds.bottomRight()));
    };
    canvas.fit();
    canvas.setBandTop(200);
    canvas.setBandBottom(500);
    canvas.revealForEditing();
    QCOMPARE(canvas.zoom(), MapCanvas::kEditZoom);
    QVERIFY(onScreen().top() >= 200 && onScreen().bottom() <= 500 && onScreen().left() >= 0 && onScreen().right() <= 400);
    // Already in the band: the map does not move (not centred).
    canvas.panBy(0, 200 + MapCanvas::kFitMarginPx + 1 - onScreen().top());
    const QRectF before = onScreen();
    QVERIFY(qAbs(before.center().y() - 350) > 1);
    canvas.revealForEditing();
    QCOMPARE(onScreen(), before);
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
    canvas.revealForEditing();
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
    QVERIFY(map.addToLibrary(sticker.value(QStringLiteral("path")).toString(), QStringLiteral("Mine"), false, QString(), QString()));
    QCOMPARE(ownCollections(IconLibrary::Kind::Sticker).size(), 1);
    const IconLibrary::Collection stickers = IconLibrary::collection(IconLibrary::Kind::Sticker, ownCollections(IconLibrary::Kind::Sticker).front());
    QCOMPARE(stickers.items.size(), 1);
    QCOMPARE(stickers.items.front().name, QStringLiteral("Mine"));
    QVERIFY(first->imageSticker);
    QCOMPARE(first->imagePath, stickers.items.front().path);

    const QVariantMap marker = map.pickLibraryFile(QUrl::fromLocalFile(tiny), true);
    QVERIFY(marker.value(QStringLiteral("ok")).toBool());
    QVERIFY(marker.value(QStringLiteral("small")).toBool());  // under 64 px: asked
    QVERIFY(map.ownCollections(true).isEmpty());
    QVERIFY(map.addToLibrary(marker.value(QStringLiteral("path")).toString(), QString(), true, QString(), QStringLiteral("Moods")));
    QCOMPARE(ownCollections(IconLibrary::Kind::Marker).size(), 1);  // the new group, named
    const QString group = ownCollections(IconLibrary::Kind::Marker).front();
    QCOMPARE(IconLibrary::collection(IconLibrary::Kind::Marker, group).name, QStringLiteral("Moods"));
    QCOMPARE(map.ownCollections(true).size(), 1);
    QCOMPARE(first->markers.size(), 1);
    QVERIFY(Markers::isCustom(first->markers.front()));
    QVERIFY(map.document()->model()->markerImages.contains(Markers::customImage(first->markers.front())));
    const QString firstOwn = first->markers.front();
    // A second marker into the chosen group replaces the first in the topic (one per group); the
    // marker menu lists both, as a drawn series' values, and a choice puts its image in the map.
    const QVariantMap second = map.pickLibraryFile(QUrl::fromLocalFile(big), true);
    QVERIFY(map.addToLibrary(second.value(QStringLiteral("path")).toString(), QStringLiteral("Big"), true, group, QString()));
    QCOMPARE(ownCollections(IconLibrary::Kind::Marker).size(), 1);
    QCOMPARE(IconLibrary::collection(IconLibrary::Kind::Marker, group).items.size(), 2);
    QCOMPARE(first->markers.size(), 1);
    const QString secondOwn = first->markers.front();
    QVERIFY(secondOwn != firstOwn);
    const QVariantList choices = map.markerChoices(secondOwn);
    QCOMPARE(choices.size(), 2);
    QCOMPARE(choices.at(0).toMap().value(QStringLiteral("marker")).toString(), firstOwn);
    QVERIFY(choices.at(1).toMap().value(QStringLiteral("checked")).toBool());
    QCOMPARE(choices.at(1).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Big"));
    QCOMPARE(IconLibrary::nextMarker(secondOwn), firstOwn);  // the last goes back to the first
    map.document()->model()->markerImages.clear();
    QVERIFY(map.setMarker(firstOwn));
    QCOMPARE(first->markers, QStringList{firstOwn});
    QVERIFY(map.document()->model()->markerImages.contains(Markers::customImage(firstOwn)));
    QVERIFY(map.close());
    clearOwn();
}

// FEAT-051 on the phone: a sticker of the library in the selected topic, at the desktop's insertion
// size, laid out in one Undo step and embedded in the file.
// FEAT-070 on the phone: a long press on an SVG sticker > Edit… opens the editor; the finger crops
// the frame, selects a shape, double taps into its points and draws with the pen; the copy goes into
// a new collection; the X asks before discarding changes.
void MobileTest::stickerEditorOnThePhone() {
    const auto ownStickers = [] {
        QStringList ids;
        for (const IconLibrary::Collection &c : IconLibrary::collections(IconLibrary::Kind::Sticker)) if (!c.builtIn) ids.append(c.id);
        return ids;
    };
    const auto clearOwn = [&] {
        for (const QString &id : ownStickers()) IconLibrary::removeCollection(IconLibrary::Kind::Sticker, id);
        IconLibrary::removeDeletedFiles(IconLibrary::Kind::Sticker);
    };
    clearOwn();
    const auto cleanup = qScopeGuard(clearOwn);
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Stickers.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *mapCanvas = nullptr;
    QTRY_VERIFY((mapCanvas = window->findChild<MapCanvas *>()));
    MobileMap *map = mapCanvas->map();
    QVERIFY(map && map->isOpen());
    map->select(map->document()->model()->root()->id);
    QObject *picker = window->findChild<QObject *>(QStringLiteral("stickerPicker"));
    QVERIFY(picker);
    QVERIFY(QMetaObject::invokeMethod(picker, "openOn", Q_ARG(QVariant, false)));
    QTRY_VERIFY(picker->property("opened").toBool());
    auto *list = window->findChild<QQuickItem *>(QStringLiteral("stickerCollections"));
    QVERIFY(list);
    const std::function<QQuickItem *(QQuickItem *, const QString &)> find = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name) return item;
        for (QQuickItem *child : item->childItems())
            if (QQuickItem *found = find(child, name)) return found;
        return nullptr;
    };
    // The first SVG sticker shown (a collection of PNG ones may come first).
    const auto pathOf = [](QQuickItem *item) {
        QVariant data = item->property("modelData");
        if (data.canConvert<QJSValue>()) data = data.value<QJSValue>().toVariant();
        return data.toMap().value(QStringLiteral("path")).toString();
    };
    const std::function<QQuickItem *(QQuickItem *)> findSvg = [&](QQuickItem *item) -> QQuickItem * {
        if (item->objectName() == QStringLiteral("sticker") && pathOf(item).endsWith(QLatin1String(".svg"))) return item;
        for (QQuickItem *child : item->childItems())
            if (QQuickItem *found = findSvg(child)) return found;
        return nullptr;
    };
    QQuickItem *cell = nullptr;
    QTRY_VERIFY((cell = findSvg(list)));
    const QString stickerPath = pathOf(cell);

    // A long press of the finger: Edit… in a menu, no sticker put in the topic.
    static QPointingDevice *finger = QTest::createTouchDevice();
    const QPoint onCell = cell->mapToScene(QPointF(cell->width() / 2, cell->height() / 2)).toPoint();
    QTest::touchEvent(window, finger).press(0, onCell, window);
    QTest::qWait(QGuiApplication::styleHints()->mousePressAndHoldInterval() + 200);
    QTest::touchEvent(window, finger).release(0, onCell, window);
    QVERIFY(!map->document()->model()->root()->imageSticker);
    QObject *itemMenu = window->findChild<QObject *>(QStringLiteral("stickerItemMenu"));
    QVERIFY(itemMenu);
    QTRY_VERIFY(itemMenu->property("opened").toBool());
    QObject *edit = window->findChild<QObject *>(QStringLiteral("stickerEditItem"));
    QVERIFY(edit);
    QVERIFY(QMetaObject::invokeMethod(edit, "clicked"));
    QObject *dialog = window->findChild<QObject *>(QStringLiteral("stickerEditor"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto *editor = dialog->property("editor").value<MobileStickerEditor *>();
    QVERIFY(editor && editor->valid());
    auto *view = window->findChild<StickerCanvasView *>(QStringLiteral("stickerCanvas"));
    QVERIFY(view);
    QTRY_VERIFY(view->width() > 100 && view->height() == view->width());
    QTRY_VERIFY(view->y() > 0);  // the column is laid out at the next frame
    auto *scroll = window->findChild<QQuickItem *>(QStringLiteral("stickerEditorList"));
    QVERIFY(scroll);
    const auto scene = [&](const QPointF &document) { return view->mapToScene(view->toView(document)).toPoint(); };
    SvgDocument original;
    {
        QFile file(stickerPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(original.load(file.readAll()));
        original.ensureShapeIds();  // as the editor: the same ids
    }

    // Frame: the right edge dragged by the finger crops in one step, and the list does not scroll.
    editor->setMode(int(StickerCanvas::Mode::Frame));
    const QRectF frame = original.viewBox();
    const QPoint edge = scene(QPointF(frame.right(), frame.center().y()));
    const QPoint inside = scene(QPointF(frame.right() - frame.width() / 4, frame.center().y()));
    const qreal scrolled = scroll->property("contentY").toReal();
    QTest::touchEvent(window, finger).press(0, edge, window);
    for (int step = 1; step <= 8; ++step) {
        QTest::qWait(16);
        QTest::touchEvent(window, finger).move(0, edge + (inside - edge) * step / 8, window);
    }
    QTest::touchEvent(window, finger).release(0, inside, window);
    QTRY_VERIFY(editor->canUndo());
    QCOMPARE(scroll->property("contentY").toReal(), scrolled);
    editor->undo();
    QVERIFY(!editor->modified());

    // Select: a tap on a shape selects it, a double tap edits its points; Done ends.
    editor->setMode(int(StickerCanvas::Mode::Shapes));
    const QList<SvgDocument::Shape> shapes = original.drawnShapes();
    QString shapeId;
    QPointF onShape;
    for (const SvgDocument::Shape &shape : shapes) {
        const QPointF centre = shape.outline.boundingRect().center();
        if (shape.outline.boundingRect().width() > frame.width() / 8 && SvgDocument::shapeAt(shapes, centre, 0) == shape.id) {
            shapeId = shape.id;
            onShape = centre;
            break;
        }
    }
    QVERIFY(!shapeId.isEmpty());
    QTest::touchEvent(window, finger).press(0, scene(onShape), window);
    QTest::touchEvent(window, finger).release(0, scene(onShape), window);
    QTRY_COMPARE(editor->selectionCount(), 1);
    QVERIFY(!editor->pointEditing());
    QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 100);  // not a double tap yet
    for (int tap = 0; tap < 2; ++tap) {
        QTest::touchEvent(window, finger).press(0, scene(onShape), window);
        QTest::touchEvent(window, finger).release(0, scene(onShape), window);
        QTest::qWait(30);
    }
    QTRY_VERIFY(editor->pointEditing());
    QVERIFY(QMetaObject::invokeMethod(window->findChild<QObject *>(QStringLiteral("stickerPointsDone")), "clicked"));
    QVERIFY(!editor->pointEditing());
    QVERIFY(!editor->modified());
    // "Edit points" does what the double tap does.
    QObject *editPoints = window->findChild<QObject *>(QStringLiteral("stickerEditPoints"));
    QVERIFY(editPoints && editPoints->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(editPoints, "clicked"));
    QVERIFY(editor->pointEditing());
    editor->endPointEditing();

    // Pen: three taps, then Finish: a new shape, selected, one step.
    editor->setMode(int(StickerCanvas::Mode::Pen));
    for (const QPointF &at : {frame.topLeft() + QPointF(frame.width() * 0.2, frame.height() * 0.2),
                              frame.topLeft() + QPointF(frame.width() * 0.8, frame.height() * 0.2),
                              frame.topLeft() + QPointF(frame.width() * 0.5, frame.height() * 0.8)}) {
        QTest::touchEvent(window, finger).press(0, scene(at), window);
        QTest::touchEvent(window, finger).release(0, scene(at), window);
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
    }
    QTRY_VERIFY(editor->penActive());
    QObject *finish = window->findChild<QObject *>(QStringLiteral("stickerPenFinish"));
    QVERIFY(finish && finish->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(finish, "clicked"));
    QVERIFY(!editor->penActive());
    QVERIFY(editor->modified());
    QCOMPARE(editor->selectionCount(), 1);

    // The X asks first; Discard is not chosen yet: the editor stays open.
    QObject *close = window->findChild<QObject *>(QStringLiteral("stickerEditorClose"));
    QVERIFY(close);
    QVERIFY(QMetaObject::invokeMethod(close, "clicked"));
    QObject *discard = window->findChild<QObject *>(QStringLiteral("discardStickerDialog"));
    QVERIFY(discard);
    QTRY_VERIFY(discard->property("opened").toBool());
    QVERIFY(dialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(discard, "close"));
    QTRY_VERIFY(!discard->property("visible").toBool());

    // Save as new, into a new collection: the copy is in the library, the editor closes.
    auto *name = window->findChild<QObject *>(QStringLiteral("stickerName"));
    QVERIFY(name);
    name->setProperty("text", QStringLiteral("Phone copy"));
    auto *collection = window->findChild<QObject *>(QStringLiteral("stickerCollection"));
    QVERIFY(collection);
    collection->setProperty("value", QStringLiteral("new"));
    QObject *save = window->findChild<QObject *>(QStringLiteral("stickerSaveButton"));
    QVERIFY(save);
    QVERIFY(!save->property("enabled").toBool());  // the new collection needs a name
    window->findChild<QObject *>(QStringLiteral("stickerNewCollection"))->setProperty("text", QStringLiteral("Edited"));
    QVERIFY(save->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(ownStickers().size(), 1);
    const IconLibrary::Collection edited = IconLibrary::collection(IconLibrary::Kind::Sticker, ownStickers().front());
    QCOMPARE(edited.name, QStringLiteral("Edited"));
    QCOMPARE(edited.items.size(), 1);
    QCOMPARE(IconLibrary::displayName(edited.items.front().path), QStringLiteral("Phone copy"));
    {
        QFile file(edited.items.front().path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        SvgDocument copy;
        QVERIFY(copy.load(file.readAll()));
        QCOMPARE(copy.drawnShapes().size(), shapes.size() + 1);  // the pen's path
    }
    // A built-in original stays: no question to delete it.
    QObject *deleteOriginal = window->findChild<QObject *>(QStringLiteral("deleteOriginalDialog"));
    QVERIFY(deleteOriginal);
    QVERIFY(!deleteOriginal->property("visible").toBool());
    QVERIFY(map->close());
    QStringList warnings;
    for (const QQmlError &error : std::as_const(errors)) warnings.append(error.toString());
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
}

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
    // FEAT-126: Ctrl+arrows add the topic that way to the selection; the mode turns on.
    {
        const MindNode *child = childTitled(root, QStringLiteral("First"));
        const bool left = map.snapshot()->topicOf(child)->position.x() < map.snapshot()->topicOf(root)->position.x();
        map.select(root->id);
        QVERIFY(map.addNeighbour(left ? Qt::Key_Left : Qt::Key_Right));
        QVERIFY(map.multiSelect());
        QCOMPARE(map.selectedIds().size(), 2);
        QCOMPARE(map.selectedIds().front(), root->id);
        QCOMPARE(model->nodeById(map.selectedId())->parent, root);
        // Back to the central topic: already in the selection, it is the one reached, nothing added.
        QVERIFY(map.addNeighbour(left ? Qt::Key_Right : Qt::Key_Left));
        QCOMPARE(map.selectedId(), root->id);
        QCOMPARE(map.selectedIds().size(), 2);
        QVERIFY(!map.addNeighbour(Qt::Key_Up));  // nothing that way
        map.stopMultiSelect();
        map.select(QString());
        QVERIFY(map.addNeighbour(Qt::Key_Up));  // none selected: as a plain arrow
        QVERIFY(!map.multiSelect());
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

// FEAT-125: the Home's Settings button opens the shortcuts list; a field records a key, a key in use
// asks, Restore and Restore all go back to the defaults, Back closes the page.
void MobileTest::settingsPageShortcuts() {
    QSettings().remove(QStringLiteral("shortcuts"));
    const auto cleanup = qScopeGuard([] { QSettings().remove(QStringLiteral("shortcuts")); });
    QTemporaryDir root;
    MapFolder::setConfiguredPath(root.path());
    const auto restore = qScopeGuard([] { MapFolder::setConfiguredPath(QString()); });
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    window->resize(400, 800);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(window));
    auto *settingsButton = window->findChild<QQuickItem *>(QStringLiteral("settingsButton"));
    QVERIFY(settingsButton && settingsButton->isVisible());
    QVERIFY(QMetaObject::invokeMethod(settingsButton, "clicked"));
    QQuickItem *page = nullptr;
    QTRY_VERIFY((page = window->findChild<QQuickItem *>(QStringLiteral("settingsPage"))) && page->isVisible());
    // Repeater rows: found through the visual tree.
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name) return item;
        for (QQuickItem *child : item->childItems())
            if (QQuickItem *found = visual(child, name)) return found;
        return nullptr;
    };
    const auto saved = [](const QString &id) { return QSettings().value(QStringLiteral("shortcuts/") + id).toString(); };
    ShortcutRegistry registry;
    ShortcutCatalog::addAll(registry);
    // Only the phone's commands: Save yes, Save as no.
    QVERIFY(visual(page, QStringLiteral("shortcutRow_save")));
    QVERIFY(!visual(page, QStringLiteral("shortcutRow_saveAs")));
    // A tap records; a modifier alone waits; Ctrl+F11 (free) is saved.
    auto *field = visual(page, QStringLiteral("shortcutField_save_0"));
    QVERIFY(field);
    QVERIFY(QMetaObject::invokeMethod(field, "clicked"));
    QTRY_VERIFY(field->property("recording").toBool());
    auto *recorder = visual(page, QStringLiteral("shortcutRecorder"));
    QVERIFY(recorder && recorder->hasActiveFocus());
    QTest::keyPress(window, Qt::Key_Control);
    QVERIFY(field->property("recording").toBool());
    QTest::keyClick(window, Qt::Key_F11, Qt::ControlModifier);
    QTRY_COMPARE(saved(QStringLiteral("save")), QStringLiteral("Ctrl+F11"));
    QVERIFY(!field->property("recording").toBool());
    QCOMPARE(registry.shortcuts(QStringLiteral("save")).value(0), QKeySequence(QStringLiteral("Ctrl+F11")));
    // Esc stops recording without a change.
    QVERIFY(QMetaObject::invokeMethod(field, "clicked"));
    QTest::keyClick(window, Qt::Key_Escape);
    QVERIFY(!field->property("recording").toBool());
    QCOMPARE(saved(QStringLiteral("save")), QStringLiteral("Ctrl+F11"));
    // Ctrl+Z (Undo, General): the modal; Replace moves it.
    QVERIFY(QMetaObject::invokeMethod(field, "clicked"));
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    auto *dialog = window->findChild<QObject *>(QStringLiteral("shortcutConflictDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto *replace = window->findChild<QObject *>(QStringLiteral("shortcutConflictReplace"));
    QVERIFY(replace && QMetaObject::invokeMethod(replace, "clicked"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(registry.shortcuts(QStringLiteral("save")).value(0), QKeySequence(QKeySequence::Undo));
    QVERIFY(!registry.shortcuts(QStringLiteral("undo")).contains(QKeySequence(QKeySequence::Undo)));
    // Restore on the row, then Restore all.
    auto *restoreRow = visual(page, QStringLiteral("shortcutRestore_save"));
    QVERIFY(restoreRow && restoreRow->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(restoreRow, "clicked"));
    QVERIFY(registry.isDefault(QStringLiteral("save")));
    QTRY_VERIFY(!restoreRow->property("enabled").toBool());
    auto *clear = visual(page, QStringLiteral("shortcutField_remove_1Clear"));
    QVERIFY(clear && QMetaObject::invokeMethod(clear, "clicked"));  // Backspace off
    QCOMPARE(registry.shortcuts(QStringLiteral("remove")), QList<QKeySequence>{QKeySequence(Qt::Key_Delete)});
    auto *restoreAll = page->findChild<QObject *>(QStringLiteral("shortcutRestoreAll"));
    QVERIFY(restoreAll && QMetaObject::invokeMethod(restoreAll, "clicked"));
    for (const QString &id : {QStringLiteral("undo"), QStringLiteral("remove")}) QVERIFY(registry.isDefault(id));
    QCOMPARE(registry.shortcuts(QStringLiteral("remove")), (QList<QKeySequence>{QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)}));
    // Back closes the page.
    auto *back = page->findChild<QObject *>(QStringLiteral("settingsBackButton"));
    QVERIFY(back && QMetaObject::invokeMethod(back, "clicked"));
    QTRY_VERIFY(!window->findChild<QQuickItem *>(QStringLiteral("settingsPage")));
    QVERIFY2(errors.isEmpty(), qPrintable(errors.isEmpty() ? QString() : errors.front().toString()));
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

// FEAT-123: New map asks the type and the name and writes the map in the folder shown.
void MobileTest::newMapInTheFolderShown() {
    QTemporaryDir storage;
    const QString root = QDir::cleanPath(storage.path());
    QVERIFY(QDir(root).mkdir(QStringLiteral("Courses")));
    const QString courses = QDir(root).filePath(QStringLiteral("Courses"));
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
    auto *map = window->findChild<MobileMap *>();
    QVERIFY(library && map);
    library->enter(courses);
    QTRY_VERIFY(!library->atRoot());

    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name) return item;
        for (QQuickItem *child : item->childItems())
            if (QQuickItem *found = visual(child, name)) return found;
        return nullptr;
    };
    QQuickItem *scene = window->contentItem()->parentItem() ? window->contentItem()->parentItem() : window->contentItem();
    // The header's "+": its menu, New map first.
    auto *plus = window->findChild<QQuickItem *>(QStringLiteral("newButton"));
    QVERIFY(plus && plus->isVisible());
    QVERIFY(QMetaObject::invokeMethod(plus, "clicked"));
    QObject *menu = window->findChild<QObject *>(QStringLiteral("entryMenu"));
    QTRY_VERIFY(menu->property("opened").toBool());
    const QVariantList actions = menu->property("actions").toList();
    QCOMPARE(actions.size(), 2);
    QCOMPARE(actions.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("newMap"));
    QCOMPARE(actions.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("newFolder"));
    QQuickItem *newMap = nullptr;
    QTRY_VERIFY((newMap = visual(scene, QStringLiteral("entryAction_newMap"))));
    QVERIFY(QMetaObject::invokeMethod(newMap, "clicked"));
    QObject *typeSheet = window->findChild<QObject *>(QStringLiteral("newMapTypeSheet"));
    QVERIFY(typeSheet);
    QTRY_VERIFY(typeSheet->property("opened").toBool());
    QVERIFY(typeSheet->property("current").toString().isEmpty());  // no tile ringed
    // A tap outside leaves it open; its X closes it (then New map again).
    QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(4, 790));
    QTest::qWait(50);
    QVERIFY(typeSheet->property("opened").toBool());
    QQuickItem *closeX = visual(scene, QStringLiteral("newMapTypeSheetClose"));
    QVERIFY(closeX && closeX->isVisible());
    QVERIFY(QMetaObject::invokeMethod(closeX, "clicked"));
    QTRY_VERIFY(!typeSheet->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(plus, "clicked"));
    QTRY_VERIFY(menu->property("opened").toBool());
    QTRY_VERIFY((newMap = visual(scene, QStringLiteral("entryAction_newMap"))));
    QVERIFY(QMetaObject::invokeMethod(newMap, "clicked"));
    QTRY_VERIFY(typeSheet->property("opened").toBool());
    const QString treeVariant = MapVariants::ofType(MapStructure::Type::TreeChart).first()->id;
    QQuickItem *tile = nullptr;
    QTRY_VERIFY((tile = visual(scene, QStringLiteral("mapTypeTile-") + treeVariant)));
    QVERIFY(QMetaObject::invokeMethod(tile, "clicked"));
    QTRY_VERIFY(!typeSheet->property("opened").toBool());

    QObject *nameDialog = window->findChild<QObject *>(QStringLiteral("folderNameDialog"));
    QVERIFY(nameDialog);
    QTRY_VERIFY(nameDialog->property("opened").toBool());
    QQuickItem *field = visual(scene, QStringLiteral("folderNameField"));
    QVERIFY(field);
    QCOMPARE(field->property("text").toString(), QStringLiteral("New map"));
    QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(4, 790));  // a tap outside: still open
    QTest::qWait(50);
    QVERIFY(nameDialog->property("opened").toBool());
    field->setProperty("text", QStringLiteral("Biology"));
    QQuickItem *create = visual(scene, QStringLiteral("folderNameAcceptButton"));
    QVERIFY(create && create->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(create, "clicked"));

    const QString path = QDir(courses).filePath(QStringLiteral("Biology.mentis"));
    QTRY_VERIFY(map->isOpen());
    QVERIFY(QFileInfo::exists(path));
    QCOMPARE(QFileInfo(map->document()->filePath()).absoluteFilePath(), QFileInfo(path).absoluteFilePath());
    const MindMapModel *model = map->document()->model();
    QCOMPARE(model->sheetStyle.variant, treeVariant);
    QCOMPARE(model->root()->structure, MapStructure::typeKey(MapStructure::Type::TreeChart));
    QCOMPARE(int(model->root()->children.size()), 4);
    QVERIFY(QFileInfo::exists(QDir(root).filePath(QStringLiteral("Courses"))));
    QVERIFY(!QFileInfo::exists(QDir(root).filePath(QStringLiteral("Biology.mentis"))));  // not at the top
    QVERIFY(map->close());

    // The same name again: a free one. A folder that is not there: nothing written, an error.
    QVERIFY(map->create(courses, QStringLiteral("Biology"), treeVariant));
    QVERIFY(QFileInfo::exists(QDir(courses).filePath(QStringLiteral("Biology (2).mentis"))));
    QVERIFY(map->close());
    QVERIFY(!map->create(QDir(root).filePath(QStringLiteral("Missing")), QStringLiteral("X"), treeVariant));
    QVERIFY(!map->lastError().isEmpty());
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).toString()));
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

    // A map's ⋮ ends with Delete…, which asks first: Cancel keeps the map, Delete removes it.
    const QString spare = QDir(root).filePath(QStringLiteral("Spare.mentis"));
    QCOMPARE(writeTwoSheetMap(spare), QString());
    library->refresh();
    QTest::qWait(100);
    // The menu and the dialog are in the overlay, beside the content item.
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name && item->isVisible()) return item;
        for (QQuickItem *child : item->childItems())
            if (QQuickItem *found = visual(child, name)) return found;
        return nullptr;
    };
    QQuickItem *scene = window->contentItem()->parentItem() ? window->contentItem()->parentItem() : window->contentItem();
    const auto openSpareMenu = [&] {
        QQuickItem *spareRow = nullptr;
        QTRY_VERIFY((spareRow = rowNamed(QStringLiteral("mapRow"))));
        QQuickItem *mapMore = nullptr;
        for (QQuickItem *item : spareRow->childItems()) if (item->objectName() == QStringLiteral("entryMoreButton")) mapMore = item;
        QVERIFY(mapMore);
        QTest::mouseClick(window, Qt::LeftButton, {}, centre(mapMore));
        QTRY_VERIFY(menu->property("opened").toBool());
        QCOMPARE(menu->property("actions").toList().last().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("deleteMap"));
        QQuickItem *deleteAction = nullptr;
        QTRY_VERIFY((deleteAction = visual(scene, QStringLiteral("entryAction_deleteMap"))));
        QVERIFY(QMetaObject::invokeMethod(deleteAction, "clicked"));
    };
    // Rename…, second in a map's ⋮, gives the file a new name in the folder name modal.
    {
        QQuickItem *spareRow = nullptr;
        QTRY_VERIFY((spareRow = rowNamed(QStringLiteral("mapRow"))));
        QQuickItem *mapMore = nullptr;
        for (QQuickItem *item : spareRow->childItems()) if (item->objectName() == QStringLiteral("entryMoreButton")) mapMore = item;
        QVERIFY(mapMore);
        QTest::mouseClick(window, Qt::LeftButton, {}, centre(mapMore));
        QTRY_VERIFY(menu->property("opened").toBool());
        QCOMPARE(menu->property("actions").toList().at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("renameMap"));
        QQuickItem *renameAction = nullptr;
        QTRY_VERIFY((renameAction = visual(scene, QStringLiteral("entryAction_renameMap"))));
        QVERIFY(QMetaObject::invokeMethod(renameAction, "clicked"));
        QObject *nameDialog = window->findChild<QObject *>(QStringLiteral("folderNameDialog"));
        QVERIFY(nameDialog);
        QTRY_VERIFY(nameDialog->property("opened").toBool());
        QObject *nameField = window->findChild<QObject *>(QStringLiteral("folderNameField"));
        QVERIFY(nameField);
        QCOMPARE(nameField->property("text").toString(), QStringLiteral("Spare"));
        nameField->setProperty("text", QStringLiteral("Kept"));
        QVERIFY(QMetaObject::invokeMethod(nameDialog, "accept"));
        const QString kept = QDir(root).filePath(QStringLiteral("Kept.mentis"));
        QTRY_VERIFY(QFileInfo::exists(kept));
        QVERIFY(!QFileInfo::exists(spare));
        QVERIFY(library->renameMap(kept, QStringLiteral("Spare")));
        QVERIFY(QFileInfo::exists(spare));
        QTest::qWait(100);
    }
    QObject *deleteDialog = window->findChild<QObject *>(QStringLiteral("deleteMapDialog"));
    QVERIFY(deleteDialog);
    openSpareMenu();
    QTRY_VERIFY(deleteDialog->property("opened").toBool());
    QVERIFY(QFileInfo::exists(spare));  // nothing deleted before the answer
    QVERIFY(QMetaObject::invokeMethod(deleteDialog, "reject"));
    QTRY_VERIFY(!deleteDialog->property("visible").toBool());
    QVERIFY(QFileInfo::exists(spare));
    openSpareMenu();
    QTRY_VERIFY(deleteDialog->property("opened").toBool());
    QQuickItem *confirm = nullptr;
    QTRY_VERIFY((confirm = visual(scene, QStringLiteral("deleteMapConfirm"))));
    QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
    QTRY_VERIFY(!QFileInfo::exists(spare));
    QTRY_COMPARE(library->maps().size(), 0);
    QTest::qWait(100);
    QTRY_VERIFY((folderRow = rowNamed(QStringLiteral("folderRow"))));

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

// BUG (user's report 2026-11-16, tablet with a pen): a pen tap selected a topic but never pressed
// the "+" knobs: the map's handlers took the tablet events the buttons ignore. The pen is the mouse.
static void penTap(QQuickWindow *window, QPointF at) {
    static const QPointingDevice pen(QStringLiteral("test pen"), 4242, QInputDevice::DeviceType::Stylus,
                                     QPointingDevice::PointerType::Pen,
                                     QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 2);
    const QPointF global = window->mapToGlobal(at);
    static ulong time = 1000;  // a platform's milliseconds, rising
    QTabletEvent press(QEvent::TabletPress, &pen, at, global, 0.6, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
    press.setTimestamp(time += 1000);
    QGuiApplication::sendEvent(window, &press);
    QTabletEvent release(QEvent::TabletRelease, &pen, at, global, 0, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::NoButton);
    release.setTimestamp(time += 80);
    QGuiApplication::sendEvent(window, &release);
    QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);  // not a double tap
}

void MobileTest::penWorksAsTheMouse() {
    // A tablet's high DPI factor (1.75 on a Galaxy Tab S9 Ultra): the mouse made from the pen is
    // given in the platform's native pixels (user's report 2026-11-16: taps landed up and left).
    QHighDpiScaling::setGlobalFactor(1.75);
    const auto resetScale = qScopeGuard([] { QHighDpiScaling::setGlobalFactor(1); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Pen.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    QQmlApplicationEngine engine;
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *canvas = nullptr;
    QTRY_VERIFY((canvas = window->findChild<MapCanvas *>()));
    QTRY_VERIFY(canvas->width() > 0);
    MobileMap *map = canvas->map();
    canvas->fit();
    // The page's push has ended: a StackView takes no input while busy.
    QObject *stack = nullptr;
    for (QQuickItem *item : window->findChildren<QQuickItem *>())
        if (item->inherits("QQuickStackView")) stack = item;
    QVERIFY(stack);
    QTRY_VERIFY(!stack->property("busy").toBool());
    // A pen tap on the central topic selects it, one on its "+" adds a child.
    const MapSnapshot::Topic *rootTopic = map->snapshot()->topicOf(map->document()->model()->root());
    QVERIFY(rootTopic);
    penTap(window, canvas->mapToScene(canvas->itemPoint(rootTopic->bounds.center())));
    QCOMPARE(map->selectedId(), map->document()->model()->root()->id);
    auto *childKnob = window->findChild<QQuickItem *>(QStringLiteral("addChildKnob"));
    QTRY_VERIFY(childKnob && childKnob->isVisible());
    const int before = map->topicCount();
    penTap(window, childKnob->mapToScene(QPointF(childKnob->width() / 2, childKnob->height() / 2)));
    QCOMPARE(map->topicCount(), before + 1);
    QVERIFY(QMetaObject::invokeMethod(window->findChild<QObject *>(QStringLiteral("mapPage")), "endEdit"));
    // A pen drag on the background pans the map, as the mouse's.
    const QPointF shown = canvas->itemPoint(rootTopic->bounds.center());
    QTabletEvent::Type steps[] = {QEvent::TabletPress, QEvent::TabletMove, QEvent::TabletMove, QEvent::TabletMove, QEvent::TabletRelease};
    static const QPointingDevice pen(QStringLiteral("drag pen"), 4243, QInputDevice::DeviceType::Stylus,
                                     QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position, 1, 2);
    for (int i = 0; i < 5; ++i) {  // the first move starts the drag, the next two pan it
        const QPointF at(20 + 40 * qMin(i, 3), 120);
        const bool up = steps[i] == QEvent::TabletRelease;
        QTabletEvent step(steps[i], &pen, at, window->mapToGlobal(at), up ? 0 : 0.6, 0, 0, 0, 0, 0, Qt::NoModifier,
                          steps[i] == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton, up ? Qt::NoButton : Qt::LeftButton);
        step.setTimestamp(100000 + 30 * i);
        QGuiApplication::sendEvent(window, &step);
    }
    QVERIFY(canvas->itemPoint(map->snapshot()->topicOf(map->document()->model()->root())->bounds.center()).x() > shown.x() + 60);
    QVERIFY(map->close());
}

// User's request 2026-11-19: an icon button shows its name in a tooltip, with the pen over it
// (without touching the glass) or on a long press, which does not press it.
static bool toolTipShown(QQuickItem *button, const QString &text) {
    auto *tip = button->property("tip").value<QObject *>();  // made the first time it is wanted
    return tip && tip->property("visible").toBool() && tip->property("text").toString() == text;
}

void MobileTest::buttonsShowTheirName() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Tips.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    QQmlApplicationEngine engine;
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *canvas = nullptr;
    QTRY_VERIFY((canvas = window->findChild<MapCanvas *>()));
    MobileMap *map = canvas->map();
    QObject *stack = nullptr;
    for (QQuickItem *item : window->findChildren<QQuickItem *>())
        if (item->inherits("QQuickStackView")) stack = item;
    QVERIFY(stack);
    QTRY_VERIFY(!stack->property("busy").toBool());
    auto *back = window->findChild<QQuickItem *>(QStringLiteral("backButton"));
    auto *addSheet = window->findChild<QQuickItem *>(QStringLiteral("addSheetButton"));
    QVERIFY(back && addSheet);
    QVERIFY(!back->property("tip").value<QObject *>());

    // The pen over Back, not touching: after the delay its name; the pen lifted away hides it.
    static const QPointingDevice pen(QStringLiteral("hover pen"), 4244, QInputDevice::DeviceType::Stylus,
                                     QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position, 1, 2);
    const QPointF onBack = back->mapToScene(QPointF(back->width() / 2, back->height() / 2));
    QTabletEvent hover(QEvent::TabletMove, &pen, onBack, window->mapToGlobal(onBack), 0, 0, 0, 0, 0, 0, Qt::NoModifier,
                       Qt::NoButton, Qt::NoButton);
    hover.setTimestamp(200000);
    QGuiApplication::sendEvent(window, &hover);
    QTRY_VERIFY(back->property("hovered").toBool());
    QTRY_VERIFY(toolTipShown(back, QStringLiteral("Back")));
    QTabletEvent away(QEvent::TabletLeaveProximity, &pen, QPointF(), QPointF(), 0, 0, 0, 0, 0, 0, Qt::NoModifier,
                      Qt::NoButton, Qt::NoButton);
    QGuiApplication::sendEvent(QGuiApplication::instance(), &away);
    QTRY_VERIFY(!back->property("hovered").toBool());
    QTRY_VERIFY(!toolTipShown(back, QStringLiteral("Back")));

    // A long press on Add sheet: its name, no new sheet; the name goes a moment after the release.
    const int sheets = map->sheets().size();
    const QPoint onAdd = addSheet->mapToScene(QPointF(addSheet->width() / 2, addSheet->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, {}, onAdd);
    QTRY_VERIFY(toolTipShown(addSheet, QStringLiteral("Add sheet")));
    QTest::mouseRelease(window, Qt::LeftButton, {}, onAdd);
    QCOMPARE(map->sheets().size(), sheets);
    QVERIFY(toolTipShown(addSheet, QStringLiteral("Add sheet")));
    QTest::mouseMove(window, QPoint(window->width() / 2, window->height() / 2));
    QTRY_VERIFY_WITH_TIMEOUT(!toolTipShown(addSheet, QStringLiteral("Add sheet")), 3000);
    // A tap still presses it.
    QTest::mouseClick(window, Qt::LeftButton, {}, onAdd);
    QTRY_COMPARE(map->sheets().size(), sheets + 1);
    QVERIFY(map->close());
}

void MobileTest::labelsShowTheirName() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("LabelTips.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    QQmlApplicationEngine engine;
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(QMetaObject::invokeMethod(window, "openMap", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    MapCanvas *canvas = nullptr;
    QTRY_VERIFY((canvas = window->findChild<MapCanvas *>()));
    QTRY_VERIFY(canvas->width() > 0 && canvas->map()->isOpen());
    MobileMap *map = canvas->map();
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    const auto item = [&](const char *name) { return window->findChild<QQuickItem *>(QLatin1String(name)); };
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *root, const QString &name) -> QQuickItem * {
        if (root->objectName() == name) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = visual(child, name)) return found;
        return nullptr;
    };
    const std::function<QQuickItem *(QQuickItem *, const QString &)> tipLabel = [&](QQuickItem *root, const QString &text) -> QQuickItem * {
        if (root->property("tipLabel").toBool() && root->property("text").toString() == text) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = tipLabel(child, text)) return found;
        return nullptr;
    };
    MindMapModel *model = map->document()->model();
    window->resize(800, 600);  // landscape, within the offscreen screen (hover is redelivered from global positions)
    QVERIFY(QMetaObject::invokeMethod(item("panelButton"), "clicked"));
    QQuickItem *panel = nullptr;
    QTRY_VERIFY((panel = item("sidePanel")) && panel->isVisible());
    QQuickItem *mapSegment = visual(panel, QStringLiteral("mapSegment"));
    QVERIFY(mapSegment);
    QVERIFY(QMetaObject::invokeMethod(mapSegment, "clicked"));
    QTRY_VERIFY(item("mapPanelPage")->isVisible());
    QQuickItem *colorRow = visual(panel, QStringLiteral("branchLineColorRow"));
    QQuickItem *switchRow = visual(panel, QStringLiteral("coloredBranchSwitch"));
    QVERIFY(colorRow && switchRow);
    QQuickItem *colour = tipLabel(colorRow, QStringLiteral("Color"));
    QQuickItem *colored = tipLabel(switchRow, QStringLiteral("Colored branch"));
    QVERIFY(colour && colored);
    QVERIFY(!colour->property("tip").value<QObject *>());
    // Landscape, docked at the right: measured once the panel is in place.
    QTRY_COMPARE(panel->width(), 276.0);
    QTRY_COMPARE(panel->x() + panel->width(), item("mapPage")->width());
    const auto onText = [](QQuickItem *label) {
        return label->mapToScene(QPointF(qMin(label->implicitWidth(), label->width()) / 2, label->height() / 2)).toPoint();
    };

    // The pen over a label: after the delay its text; the pen lifted away hides it. Also in a
    // disabled row: "Color" is disabled while Colored branch is on.
    static const QPointingDevice pen(QStringLiteral("label pen"), 4245, QInputDevice::DeviceType::Stylus,
                                     QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position, 1, 2);
    qint64 time = 300000;
    for (const bool colouredBranches : {true, false}) {
        if (model->sheetStyle.coloredBranches != colouredBranches)
            QVERIFY(QMetaObject::invokeMethod(switchRow, "clicked"));
        QTRY_COMPARE(colorRow->isEnabled(), !colouredBranches);
        // Qt drops a move to where the pointer already was: one pixel aside the second time.
        const QPointF at = onText(colour) + QPointF(colouredBranches ? 0 : 1, 0);
        QTabletEvent hover(QEvent::TabletMove, &pen, at, window->mapToGlobal(at), 0, 0, 0, 0, 0, 0, Qt::NoModifier,
                           Qt::NoButton, Qt::NoButton);
        hover.setTimestamp(time += 100000);
        QGuiApplication::sendEvent(window, &hover);
        QTRY_VERIFY(toolTipShown(colour, QStringLiteral("Color")));
        QTabletEvent away(QEvent::TabletLeaveProximity, &pen, QPointF(), QPointF(), 0, 0, 0, 0, 0, 0, Qt::NoModifier,
                          Qt::NoButton, Qt::NoButton);
        QGuiApplication::sendEvent(QGuiApplication::instance(), &away);
        QTRY_VERIFY(!toolTipShown(colour, QStringLiteral("Color")));
    }

    // A long press on a switch's label: its text at once, the switch unchanged; the text goes a
    // moment after the release. A tap on the label still toggles it.
    const bool before = model->sheetStyle.coloredBranches;
    QTest::mousePress(window, Qt::LeftButton, {}, onText(colored));
    QTRY_VERIFY(toolTipShown(colored, QStringLiteral("Colored branch")));
    QTest::mouseRelease(window, Qt::LeftButton, {}, onText(colored));
    QCOMPARE(model->sheetStyle.coloredBranches, before);
    QVERIFY(toolTipShown(colored, QStringLiteral("Colored branch")));
    QTest::mouseMove(window, QPoint(window->width() / 4, window->height() / 2));  // off the panel, as a finger lifted
    QTRY_VERIFY_WITH_TIMEOUT(!toolTipShown(colored, QStringLiteral("Colored branch")), 3000);
    // Past the double click interval of QTest's clock: a double click presses no button.
    QTest::mouseClick(window, Qt::LeftButton, {}, onText(colored), QGuiApplication::styleHints()->mouseDoubleClickInterval() + 10);
    QTRY_COMPARE(model->sheetStyle.coloredBranches, !before);
    // A long press beside the label (on the switch) toggles it, as before, with no tooltip.
    const QPoint onTrack = switchRow->mapToScene(QPointF(switchRow->width() - 20, switchRow->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, {}, onTrack);
    QTRY_COMPARE(model->sheetStyle.coloredBranches, before);
    QTest::mouseRelease(window, Qt::LeftButton, {}, onTrack);
    QCOMPARE(model->sheetStyle.coloredBranches, before);
    QVERIFY(!toolTipShown(colored, QStringLiteral("Colored branch")));
    // A long press on the label of a disabled row ("Color" with Colored branch on) shows it too.
    QTest::mouseMove(window, QPoint(window->width() / 4, window->height() / 2));
    if (!model->sheetStyle.coloredBranches)
        QVERIFY(QMetaObject::invokeMethod(switchRow, "clicked"));
    QTRY_VERIFY(!colorRow->isEnabled());
    QTest::mousePress(window, Qt::LeftButton, {}, onText(colour), QGuiApplication::styleHints()->mouseDoubleClickInterval() + 10);
    QTRY_VERIFY(toolTipShown(colour, QStringLiteral("Color")));
    QTest::mouseRelease(window, Qt::LeftButton, {}, onText(colour));
    QTest::mouseMove(window, QPoint(window->width() / 4, window->height() / 2));
    QTRY_VERIFY_WITH_TIMEOUT(!toolTipShown(colour, QStringLiteral("Color")), 3000);
    QVERIFY(map->close());
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
    QVERIFY(!bar->isVisible());  // hidden with nothing selected (2026-11-13)
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
    // Present as on the desktop's top bar, enabled since FEAT-120; the top group has no
    // Stickers button (user's request 2026-11-11). The picker opens on Sticker, as the Stickers
    // key does, and a tap puts the sticker in the selected topic.
    QObject *present = window->findChild<QObject *>(QStringLiteral("presentButton"));
    QVERIFY(present);
    QVERIFY(present->property("enabled").toBool());
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
    // (2026-11-11), the tools the phone cannot draw yet disabled; no bar over the topic. It stays
    // inside 16 px from the page's edges.
    QVERIFY(!window->findChild<QQuickItem *>(QStringLiteral("topicTools")));
    QTRY_VERIFY(bar->isVisible());
    // Add child and Add sibling are the knobs only (2026-11-11).
    QVERIFY(!window->findChild<QObject *>(QStringLiteral("addChildButton")));
    QVERIFY(!window->findChild<QObject *>(QStringLiteral("addSiblingButton")));
    for (const char *name : {"selectMoreButton", "renameButton", "deleteButton", "copyTopicsButton", "pasteTopicsButton",
                             "relationButton", "linkButton", "imageButton", "noteButton", "checkboxButton", "topicStickerButton", "formatPainterButton"}) {
        auto *button = window->findChild<QQuickItem *>(QLatin1String(name));
        QVERIFY2(button && bar->isAncestorOf(button), name);
    }
    const auto barFrame = [&] { return bar->mapRectToItem(canvas, QRectF(0, 0, bar->width(), bar->height())).adjusted(4, 4, -4, -4); };
    QVERIFY(barFrame().left() >= 16 - 0.5 && barFrame().right() <= canvas->width() - 16 + 0.5);
    // The visible buttons 2 px apart, as on the desktop (2026-11-11): 32 px buttons every 34 px, the
    // first one 4 px inside the frame. FEAT-118: the actions on the topic, then the adding tools.
    const auto barItem = [&](const QString &name) { return window->findChild<QQuickItem *>(name); };
    const QStringList fixedPart{QStringLiteral("selectMoreButton"), QStringLiteral("renameButton"), QStringLiteral("deleteButton"),
                                QStringLiteral("copyTopicsButton"), QStringLiteral("pasteTopicsButton"), QStringLiteral("formatPainterButton")};
    const QStringList addingPart{QStringLiteral("relationButton"), QStringLiteral("boundaryButton"), QStringLiteral("summaryButton"),
                                 QStringLiteral("linkButton"), QStringLiteral("imageButton"), QStringLiteral("formulaButton"),
                                 QStringLiteral("noteButton"), QStringLiteral("labelsButton"), QStringLiteral("attachFilesButton"),
                                 QStringLiteral("checkboxButton"), QStringLiteral("topicStickerButton")};
    auto *renameItem = barItem(QStringLiteral("renameButton"));
    auto *plusItem = barItem(QStringLiteral("addToolsButton"));
    QVERIFY(plusItem);
    QCOMPARE(barItem(QStringLiteral("selectMoreButton"))->mapToItem(canvas, QPointF(1, 0)).x() - barFrame().left(), 4.0);
    // Resizing the window would loop the dialogs' implicit heights: the bar's room is set instead.
    const QVariant pageWidth = bar->property("maximumWidth");
    // Wide (a tablet, a phone in landscape): one row of all 17 in that order, no "+".
    QVERIFY(bar->setProperty("maximumWidth", 800 - 2 * 12));
    QTRY_COMPARE(bar->height(), 48.0);
    QVERIFY(!plusItem->isVisible());
    const QStringList everything = fixedPart + addingPart;
    QList<qreal> wideX;
    for (qsizetype i = 0; i < everything.size(); ++i) {
        QQuickItem *button = barItem(everything.at(i));
        QVERIFY2(button->isVisible() && button->y() == 0, qPrintable(everything.at(i)));
        if (i > 0) QCOMPARE(button->x() - barItem(everything.at(i - 1))->x(), 34.0);
        wideX.append(button->x());
    }
    // A 360 px phone: one row, the fixed part where it was, then the "+"; the adding tools under it.
    QVERIFY(bar->setProperty("maximumWidth", 360 - 2 * 12));
    QTRY_VERIFY(plusItem->isVisible());
    QCOMPARE(bar->height(), 48.0);
    for (qsizetype i = 0; i < fixedPart.size(); ++i) QCOMPARE(barItem(fixedPart.at(i))->x(), wideX.at(i));
    QCOMPARE(plusItem->x(), barItem(QStringLiteral("formatPainterButton"))->x() + 34);
    for (const QString &name : addingPart) QVERIFY2(!barItem(name)->isVisible() && onTopicBar(barItem(name)), qPrintable(name));
    QCOMPARE(bar->property("addingTools").toList().size(), addingPart.size());
    // Split screen: the fixed part itself does not fit; it wraps as before and the "+" follows it.
    QVERIFY(bar->setProperty("maximumWidth", 200));
    QTRY_COMPARE(bar->height(), 2 * 48.0);
    int topRow = 0, bottomRow = 0;
    for (QQuickItem *button : renameItem->parentItem()->childItems())
        if (button->isVisible()) ++(button->y() == 0 ? topRow : bottomRow);
    QVERIFY2(bottomRow >= topRow, qPrintable(QStringLiteral("%1 above, %2 below").arg(topRow).arg(bottomRow)));
    QCOMPARE(plusItem->y(), 48.0);
    QCOMPARE(plusItem->x(), barItem(QStringLiteral("formatPainterButton"))->x() + 34);
    QVERIFY(bar->setProperty("maximumWidth", pageWidth));
    QTRY_COMPARE(bar->height(), 48.0);
    for (const char *name : {"relationButton", "linkButton", "imageButton", "noteButton", "checkboxButton", "topicStickerButton", "formatPainterButton"})
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
        QVERIFY(!onTopicBar(window->findChild<QQuickItem *>(QStringLiteral("relationButton"))));
        QVERIFY(!onTopicBar(window->findChild<QQuickItem *>(QStringLiteral("noteButton"))));
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
        QSet<QString> boundIds;
        const std::function<void(QObject *)> check = [&](QObject *object) {
            if (const QVariant command = object->property("command"); command.isValid() && object->inherits("QQuickShortcut")) {
                QVERIFY2(registry.contains(command.toString()), qPrintable(command.toString()));
                boundIds.insert(command.toString());
                ++bound;
            }
            for (QObject *child : object->children()) check(child);
        };
        check(page);
        QVERIFY(bound > 20);
        // FEAT-125: the Settings page lists exactly the commands the page binds.
        const QStringList phone = KeyCommands::phoneCommands();
        QCOMPARE(QSet<QString>(phone.cbegin(), phone.cend()), boundIds);
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
    // Esc leaves the title as it was, as on the desktop (it is written into the map as it is typed).
    {
        const MindNode *node = map->document()->model()->nodeById(added);
        const QString title = node->title;
        const bool couldRedo = map->canRedo();
        QVERIFY(QMetaObject::invokeMethod(page, "beginEdit", Q_ARG(QVariant, true)));
        QTRY_VERIFY(field->isVisible());
        for (const char key : {'x', 'y', 'z'}) QTest::keyClick(window, key);
        QTRY_COMPARE(node->title, title + QStringLiteral("xyz"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!field->isVisible());
        QCOMPARE(node->title, title);
        QCOMPARE(map->selectedId(), added);
        QCOMPARE(map->canRedo(), couldRedo);
        // no step of its own: one Undo takes back the topic added before
        QVERIFY(map->undo());
        QCOMPARE(map->topicCount(), before);
        QVERIFY(map->redo());
        map->select(added);
    }
    // FEAT-002: a printable key on the selected topic replaces its whole title; Esc puts it back,
    // Enter keeps the typed one.
    {
        const MindNode *node = map->document()->model()->nodeById(added);
        const QString title = node->title;
        QTest::keyClick(window, 'q');
        QTRY_VERIFY(field->isVisible());
        QTest::keyClick(window, '7');
        QTRY_COMPARE(node->title, QStringLiteral("q7"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!field->isVisible());
        QCOMPARE(node->title, title);
        QTest::keyClick(window, '5');  // the page has the keys again after the editing
        QTRY_VERIFY(field->isVisible());
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!field->isVisible());
        QCOMPARE(node->title, QStringLiteral("5"));
        QVERIFY(map->undo());
        QCOMPARE(map->document()->model()->nodeById(added)->title, title);
        map->select(added);
    }
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

void MobileTest::topicWidthIsDraggedFromItsBorder() {
    // FEAT-018 on the phone: the selected topic's left or right border, dragged, sets its hand width
    // (applied on the drop, one Undo step); a double tap on it, off the title, makes it automatic.
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Width.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MapCanvas canvas;
    canvas.setSize(QSizeF(400, 800));
    canvas.setMap(&map);
    const MindNode *first = childTitled(map.document()->model()->root(), QStringLiteral("First"));
    QVERIFY(first);
    QCOMPARE(first->customWidth, 0.0);
    // Not on an unselected topic.
    const auto shapeOf = [&] {
        const MapSnapshot::Topic *topic = map.snapshot()->topicOf(first);
        const QRectF shape = topic->geometry->shapeRect().translated(topic->position);
        return QRectF(canvas.itemPoint(shape.topLeft()), canvas.itemPoint(shape.bottomRight()));
    };
    const auto textWidth = [&] { return map.snapshot()->topicOf(first)->geometry->textRect().width(); };
    map.select(QString());
    canvas.zoomStep(0);
    canvas.reveal(map.snapshot()->topicOf(first)->bounds);
    QRectF shape = shapeOf();
    QVERIFY(!canvas.beginTopicResize(shape.right(), shape.center().y()));
    map.select(first->id);
    shape = shapeOf();
    QVERIFY(!canvas.beginTopicResize(shape.center().x(), shape.center().y()));  // the middle pans
    QVERIFY(!canvas.beginTopicResize(shape.right(), shape.bottom() + 10));       // below the shape

    // The right border, 40 px to the right: 40 px wider on the drop, the model untouched before it.
    const qreal start = textWidth();
    QVERIFY(canvas.beginTopicResize(shape.right() + 10, shape.center().y()));
    QVERIFY(canvas.moving());
    canvas.moveTo(shape.right() + 10 + 40 * canvas.zoom(), shape.center().y());
    QCOMPARE(first->customWidth, 0.0);
    canvas.endMove(true);
    QVERIFY(!canvas.moving());
    QCOMPARE(first->customWidth, std::round(start + 40));
    QCOMPARE(map.selectedId(), first->id);
    QVERIFY(map.undo());
    QCOMPARE(first->customWidth, 0.0);

    // Given up: nothing changes. The left border, 30 px to the left: 30 px wider.
    map.select(first->id);
    shape = shapeOf();
    QVERIFY(canvas.beginTopicResize(shape.left() - 10, shape.center().y()));
    canvas.moveTo(shape.left() - 50 * canvas.zoom(), shape.center().y());
    canvas.endMove(false);
    QCOMPARE(first->customWidth, 0.0);
    QVERIFY(canvas.beginTopicResize(shape.left() + 2, shape.center().y()));
    canvas.moveTo(shape.left() + 2 - 30 * canvas.zoom(), shape.center().y());
    canvas.endMove(true);
    QCOMPARE(first->customWidth, std::round(start + 30));

    // Never under the topic's minimum nor over 3000 px.
    shape = shapeOf();
    QVERIFY(canvas.beginTopicResize(shape.right(), shape.center().y()));
    canvas.moveTo(shape.right() - 4000, shape.center().y());
    canvas.endMove(true);
    QCOMPARE(first->customWidth, std::min(map.snapshot()->topicOf(first)->geometry->minimumCustomWidth(), kMaxManualNodeWidthPx));
    shape = shapeOf();
    QVERIFY(canvas.beginTopicResize(shape.right(), shape.center().y()));
    canvas.moveTo(shape.right() + 4000 * canvas.zoom(), shape.center().y());
    canvas.endMove(true);
    QCOMPARE(first->customWidth, kMaxManualNodeWidthPx);

    // A tap just outside the border keeps the selection; a double tap there makes the width
    // automatic, one step; on the title it is not taken (it edits).
    map.select(QString());
    map.select(first->id);
    canvas.reveal(map.snapshot()->topicOf(first)->bounds);
    shape = shapeOf();
    canvas.tapAt(shape.right() + 10, shape.center().y());
    QCOMPARE(map.selectedId(), first->id);
    const MapSnapshot::Topic *topic = map.snapshot()->topicOf(first);
    const QPointF title = canvas.itemPoint(topic->geometry->textArea().translated(topic->position).center());
    QVERIFY(!canvas.fitTopicWidthAt(title.x(), title.y()));
    QVERIFY(canvas.fitTopicWidthAt(shape.right() + 10, shape.center().y()));
    QCOMPARE(first->customWidth, 0.0);
    QVERIFY(map.undo());
    QCOMPARE(first->customWidth, kMaxManualNodeWidthPx);
    QVERIFY(map.redo());
    QCOMPARE(first->customWidth, 0.0);
    // Already automatic: taken, no step.
    shape = shapeOf();
    QVERIFY(canvas.fitTopicWidthAt(shape.right() + 10, shape.center().y()));
    QVERIFY(map.undo());
    QCOMPARE(first->customWidth, kMaxManualNodeWidthPx);
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

    // the X leaves the note as it was; the desktop's key (Ctrl+Shift+N) opens it too.
    map->select(noted->id);
    QTest::keyClick(window, Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_VERIFY(dialog->property("opened").toBool());
    QTextCursor(field).insertText(QStringLiteral("Lost "));
    QVERIFY(click("noteDialogClose"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(noted->notes, QStringLiteral("StrXYong plain more"));

    // A keyboard's Ctrl+Enter presses Done (user's request 2026-11-24, every modal's primary button).
    QTest::keyClick(window, Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_VERIFY(dialog->property("opened").toBool());
    QTextCursor(field).insertText(QStringLiteral("Kept "));
    QTest::keyClick(window, Qt::Key_Return, Qt::ControlModifier);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(noted->notes, QStringLiteral("Kept StrXYong plain more"));
    QVERIFY(map->undo());
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
    // The field as wide as the formatted title laid out (it has no plain layout).
    QCOMPARE(titleEditor->width(), map->snapshot()->topicOf(rich)->geometry->richDocument()->textWidth());
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

// FEAT-124: the desktop's Markdown shortcuts and format keys on the phone: the map's title field,
// the outliner's, the note; the soft keyboard's text (an input method's commit) as the keys'.
void MobileTest::markdownWhileTyping() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Markdown.mentis"));
    {
        Document document;
        document.startUntitled();
        MindNode *root = document.model()->root();
        for (const char *title : {"Plain", "Second", "Third", "F $x$", "Task", "Off", "Keys", "Row"})
            document.addChild(root, QString::fromLatin1(title));
        { const MapSnapshot laidOut(document.model()); }
        QVERIFY(document.save(path, BmapStorage::SaveReason::Explicit).ok);
        document.close();
    }
    MarkdownShortcuts::setEnabled(true);
    const auto restore = qScopeGuard([] { MarkdownShortcuts::setEnabled(true); });
    const auto formatAt = [](const QTextDocument *document, int position) {
        QTextCursor cursor(const_cast<QTextDocument *>(document));
        cursor.setPosition(position + 1);
        return cursor.charFormat();
    };
    const auto savedFormatAt = [&](const MindNode *node, int position) {
        QTextDocument saved;
        saved.setHtml(node->titleHtml);
        return formatAt(&saved, position);
    };

    QQmlApplicationEngine engine;
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
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    const MindNode *root = map->document()->model()->root();
    const auto topic = [&](const char *title) { return childTitled(root, QString::fromLatin1(title)); };
    QObject *page = window->findChild<QObject *>(QStringLiteral("mapPage"));
    auto *titleEditor = window->findChild<QQuickItem *>(QStringLiteral("titleEditor"));
    QVERIFY(page && titleEditor);
    QTextDocument *field = titleEditor->property("textDocument").value<QQuickTextDocument *>()->textDocument();
    // Typed one key at a time, as a person does: the shortcut is read on the next turn of the loop.
    const auto type = [&](const QString &text) {
        for (const QChar c : text) {
            QTest::keyClick(window, c.toLatin1());
            QCoreApplication::processEvents();
        }
    };
    const auto edit = [&](const MindNode *node, bool atEnd) {
        map->select(node->id);
        return QMetaObject::invokeMethod(page, "beginEdit", Q_ARG(QVariant, atEnd)) && titleEditor->hasActiveFocus();
    };
    const auto endEdit = [&] { return QMetaObject::invokeMethod(page, "endEdit"); };

    // A plain title: "**ciao**" becomes bold "ciao", without the asterisks, saved formatted.
    const MindNode *plain = topic("Plain");
    QVERIFY(edit(plain, true));
    type(QStringLiteral(" **ciao**"));
    QTRY_COMPARE(field->toPlainText(), QStringLiteral("Plain ciao"));
    QCOMPARE(plain->title, QStringLiteral("Plain ciao"));
    QVERIFY(formatAt(field, 6).fontWeight() >= QFont::Bold);
    QVERIFY(formatAt(field, 4).fontWeight() < QFont::Bold);
    // The first Undo in the field gives the symbols back; they stay as typed.
    QTest::keySequence(window, QKeySequence::Undo);
    QTRY_COMPARE(field->toPlainText(), QStringLiteral("Plain **ciao**"));
    QCoreApplication::processEvents();
    QCOMPARE(field->toPlainText(), QStringLiteral("Plain **ciao**"));
    QVERIFY(endEdit());
    QCOMPARE(plain->title, QStringLiteral("Plain **ciao**"));
    QVERIFY(map->undo());
    QCOMPARE(plain->title, QStringLiteral("Plain"));  // the whole typing, one step

    // The text typed right after the format is not in it.
    const MindNode *second = topic("Second");
    QVERIFY(edit(second, true));
    type(QStringLiteral(" **b**x"));
    QTRY_COMPARE(second->title, QStringLiteral("Second bx"));
    QVERIFY(endEdit());
    QVERIFY(savedFormatAt(second, 7).fontWeight() >= QFont::Bold);
    QVERIFY(savedFormatAt(second, 8).fontWeight() < QFont::Bold);

    // The soft keyboard's text, committed at once, works as the keys.
    const MindNode *third = topic("Third");
    QVERIFY(edit(third, true));
    {
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral(" ~~s~~"));
        QCoreApplication::sendEvent(titleEditor, &commit);
    }
    QTRY_COMPARE(third->title, QStringLiteral("Third s"));
    QVERIFY(endEdit());
    QVERIFY(savedFormatAt(third, 6).fontStrikeOut());

    // A title holding a formula stays plain text.
    const MindNode *formula = topic("F $x$");
    QVERIFY(edit(formula, true));
    type(QStringLiteral(" **a**"));
    QTRY_COMPARE(formula->title, QStringLiteral("F $x$ **a**"));
    QVERIFY(endEdit());
    QVERIFY(formula->titleHtml.isEmpty());

    // "- [ ] " at the start stays text: no checkbox (user, 2026-11-17).
    const MindNode *task = topic("Task");
    QVERIFY(edit(task, false));
    type(QStringLiteral("- [ ] Do"));
    QTRY_COMPARE(task->title, QStringLiteral("- [ ] Do"));
    QVERIFY(endEdit());
    QCOMPARE(task->task, MindNode::Task::None);

    // Switched off in the settings: the symbols stay text.
    MarkdownShortcuts::setEnabled(false);
    const MindNode *off = topic("Off");
    QVERIFY(edit(off, true));
    type(QStringLiteral(" **n**"));
    QTRY_COMPARE(off->title, QStringLiteral("Off **n**"));
    QVERIFY(endEdit());
    MarkdownShortcuts::setEnabled(true);

    // The format keys. A selected topic: every character, one Undo step; again: off.
    const MindNode *keys = topic("Keys");
    map->select(keys->id);
    QTest::keyClick(window, Qt::Key_B, Qt::ControlModifier);
    QTRY_VERIFY(!keys->titleHtml.isEmpty());
    QVERIFY(savedFormatAt(keys, 0).fontWeight() >= QFont::Bold && savedFormatAt(keys, 3).fontWeight() >= QFont::Bold);
    QTest::keyClick(window, Qt::Key_B, Qt::ControlModifier);
    QTRY_VERIFY(savedFormatAt(keys, 0).fontWeight() < QFont::Bold);
    QVERIFY(map->undo());
    QVERIFY(savedFormatAt(keys, 0).fontWeight() >= QFont::Bold);
    QVERIFY(map->undo());
    QVERIFY(keys->titleHtml.isEmpty() || savedFormatAt(keys, 0).fontWeight() < QFont::Bold);
    // Typing: the selection only; without a selection nothing.
    QVERIFY(edit(keys, false));  // the whole title selected
    QTest::keyClick(window, Qt::Key_I, Qt::ControlModifier);
    QTRY_VERIFY(formatAt(field, 0).fontItalic());
    QTest::keyClick(window, Qt::Key_End);
    type(QStringLiteral("!"));
    QTest::keyClick(window, Qt::Key_U, Qt::ControlModifier);
    QVERIFY(endEdit());
    QCOMPARE(keys->title, QStringLiteral("Keys!"));
    QVERIFY(savedFormatAt(keys, 0).fontItalic() && !savedFormatAt(keys, 0).fontUnderline());

    // The outliner's field: "==h==" highlighted; Ctrl+B on its selection.
    QQuickItem *outliner = window->findChild<QQuickItem *>(QStringLiteral("outliner"));
    QQuickItem *toggle = window->findChild<QQuickItem *>(QStringLiteral("outlinerButton"));
    QVERIFY(outliner && toggle);
    QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
    QTRY_VERIFY(outliner->isVisible());
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name) return item;
        for (QQuickItem *child : item->childItems())
            if (QQuickItem *found = visual(child, name)) return found;
        return nullptr;
    };
    const MindNode *row = topic("Row");
    QQuickItem *rowItem = nullptr;
    QTRY_VERIFY((rowItem = visual(window->contentItem(), QStringLiteral("outlineRow_") + row->id)));
    auto *rowField = rowItem->property("field").value<QQuickItem *>();
    QVERIFY(rowField);
    QVERIFY(QMetaObject::invokeMethod(outliner, "beginEdit", Q_ARG(QVariant, QVariant::fromValue(rowField)),
                                      Q_ARG(QVariant, row->id), Q_ARG(QVariant, 3)));
    QTRY_VERIFY(rowField->hasActiveFocus());
    type(QStringLiteral(" ==h=="));
    QTRY_COMPARE(row->title, QStringLiteral("Row h"));
    QTest::keySequence(window, QKeySequence::SelectAll);
    QTest::keyClick(window, Qt::Key_B, Qt::ControlModifier);
    QVERIFY(QMetaObject::invokeMethod(outliner, "endEdit", Q_ARG(QVariant, false)));
    QVERIFY(MarkdownShortcuts::isHighlight(savedFormatAt(row, 4)));
    QVERIFY(savedFormatAt(row, 0).fontWeight() >= QFont::Bold);
    QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
    QTRY_VERIFY(!outliner->isVisible());

    // The note: a heading, a list and a link.
    QObject *dialog = window->findChild<QObject *>(QStringLiteral("noteDialog"));
    auto *noteText = window->findChild<QQuickItem *>(QStringLiteral("noteText"));
    QVERIFY(dialog && noteText);
    QTextDocument *note = noteText->property("textDocument").value<QQuickTextDocument *>()->textDocument();
    map->select(plain->id);
    QVERIFY(QMetaObject::invokeMethod(page, "openNote"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    noteText->forceActiveFocus();
    type(QStringLiteral("## Head"));
    QTRY_COMPARE(note->firstBlock().text(), QStringLiteral("Head"));
    QCOMPARE(note->firstBlock().blockFormat().headingLevel(), 2);
    QTest::keyClick(window, Qt::Key_Return);
    type(QStringLiteral("- item"));
    QTRY_VERIFY(note->lastBlock().textList());
    QCOMPARE(note->lastBlock().text(), QStringLiteral("item"));
    type(QStringLiteral(" [site](example.com)"));
    QTRY_COMPARE(note->lastBlock().text(), QStringLiteral("item site"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(plain->notesHtml.contains(QStringLiteral("example.com")));
}

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
    QObject *dialog = window->findChild<QObject *>(QStringLiteral("linkDialog"));
    QObject *field = window->findChild<QObject *>(QStringLiteral("webLinkField"));
    QObject *insert = window->findChild<QObject *>(QStringLiteral("webLinkInsertButton"));
    QObject *error = window->findChild<QObject *>(QStringLiteral("webLinkError"));
    QObject *remove = window->findChild<QObject *>(QStringLiteral("linkRemoveButton"));
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
    QVERIFY(click("linkButton"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(!dialog->property("topicTab").toBool());  // FEAT-117: no link yet, the Web page tab
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
    QVERIFY(click("linkButton"));
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
    QVERIFY(click("linkButton"));
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
        return tapTopicTool(button);
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

    // With nothing selected the bar is hidden (user's request 2026-11-13); an empty mode's buttons
    // wait for a topic (2026-11-12).
    map->select(QString());
    QTRY_VERIFY(!item("topicBar")->isVisible());
    map->startMultiSelect();
    QTRY_VERIFY(item("topicBar")->isVisible());
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
    for (const char *name : {"renameButton", "relationButton", "linkButton", "noteButton", "formatPainterButton"})
        QVERIFY2(!onTopicBar(item(name)), name);
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

void MobileTest::sidePanelStyle() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("SidePanel.mentis"));
    QCOMPARE(writeSearchMap(path), QString());
    QQmlApplicationEngine engine;
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
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    const auto item = [&](const char *name) { return window->findChild<QQuickItem *>(QLatin1String(name)); };
    const auto object = [&](const char *name) { return window->findChild<QObject *>(QLatin1String(name)); };
    const auto shownInGroup = [&] {
        QStringList shown;
        for (QQuickItem *child : item("undoButton")->parentItem()->childItems())
            if (child->isVisible()) shown << child->objectName();
        return shown;
    };
    const auto menuRows = [&] {
        QStringList rows;
        for (QQuickItem *row : object("mapMenu")->property("rows").value<QQuickItem *>()->childItems())
            if (!row->objectName().isEmpty()) rows << row->objectName();
        return rows;
    };

    // Top right, small page (360 dp): no Fit in the group, Fit first in the map menu.
    window->resize(360, 720);
    QTRY_COMPARE(shownInGroup(), (QStringList{QStringLiteral("mapMenuButton"), QStringLiteral("undoButton"),
                                              QStringLiteral("redoButton"), QStringLiteral("panelButton")}));
    QVERIFY(QMetaObject::invokeMethod(item("mapMenuButton"), "clicked"));
    QTRY_VERIFY(object("mapMenu")->property("opened").toBool());
    QCOMPARE(menuRows().value(0), QStringLiteral("fitMenuButtonRow"));
    // Rotating closes the open menu and puts Fit back into the group.
    window->resize(800, 360);
    QTRY_VERIFY(!object("mapMenu")->property("opened").toBool());
    QTRY_COMPARE(shownInGroup(), (QStringList{QStringLiteral("mapMenuButton"), QStringLiteral("undoButton"),
                                              QStringLiteral("redoButton"), QStringLiteral("fitButton"), QStringLiteral("panelButton")}));
    QVERIFY(QMetaObject::invokeMethod(item("mapMenuButton"), "clicked"));
    QTRY_VERIFY(object("mapMenu")->property("opened").toBool());
    QVERIFY(!menuRows().contains(QStringLiteral("fitMenuButtonRow")));
    QVERIFY(menuRows().contains(QStringLiteral("outlinerButtonRow")));
    QMetaObject::invokeMethod(object("mapMenu"), "close");
    QTRY_VERIFY(!object("mapMenu")->property("opened").toBool());
    // Large landscape page (>= 840 dp, user's request 2026-11-22): the views are icons in the group,
    // checked while shown; the map menu keeps Find.
    window->resize(1000, 600);
    QTRY_COMPARE(shownInGroup(), (QStringList{QStringLiteral("mapMenuButton"), QStringLiteral("outlinerButton"),
                                              QStringLiteral("ganttButton"), QStringLiteral("presentButton"),
                                              QStringLiteral("undoButton"), QStringLiteral("redoButton"),
                                              QStringLiteral("fitButton"), QStringLiteral("panelButton")}));
    QVERIFY(QMetaObject::invokeMethod(item("mapMenuButton"), "clicked"));
    QTRY_VERIFY(object("mapMenu")->property("opened").toBool());
    QCOMPARE(menuRows(), QStringList{QStringLiteral("findButtonRow")});
    QMetaObject::invokeMethod(object("mapMenu"), "close");
    QTRY_VERIFY(!object("mapMenu")->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(item("outlinerButton"), "clicked"));
    QTRY_VERIFY(item("outlinerButton")->property("checked").toBool());
    QVERIFY(QMetaObject::invokeMethod(item("ganttButton"), "clicked"));
    QTRY_VERIFY(item("ganttButton")->property("checked").toBool());
    QVERIFY(!item("outlinerButton")->property("checked").toBool());
    QVERIFY(QMetaObject::invokeMethod(item("ganttButton"), "clicked"));
    QTRY_VERIFY(!item("ganttButton")->property("checked").toBool());

    // Panel docking: portrait at the bottom, half high; landscape at the right, 276 px; the page kept.
    window->resize(360, 720);
    QQuickItem *panelButton = item("panelButton");
    // The panel is made when it first opens (it is not part of the map's opening).
    QVERIFY(panelButton && !item("sidePanel"));
    QVERIFY(QMetaObject::invokeMethod(panelButton, "clicked"));
    QQuickItem *panel = nullptr;
    QTRY_VERIFY((panel = item("sidePanel")) && panel->isVisible());
    QVERIFY(panelButton->property("checked").toBool());
    QQuickItem *mapPage = item("mapPage");
    QTRY_COMPARE(panel->width(), mapPage->width());
    QCOMPARE(panel->height(), qRound(mapPage->height() / 2));
    QCOMPARE(panel->y() + panel->height(), mapPage->height());
    QVERIFY(canvas->bandBottom() <= panel->y());
    window->resize(800, 360);
    QTRY_COMPARE(panel->width(), 276.0);
    QCOMPARE(panel->height(), mapPage->height());
    QCOMPARE(panel->x() + panel->width(), mapPage->width());
    QCOMPARE(canvas->bandRight(), panel->x());
    // The top right group moves beside the panel.
    QVERIFY(panelButton->mapToItem(mapPage, QPointF(panelButton->width(), 0)).x() <= panel->x());
    // User's request 2026-11-22: the grip on the edge toward the map is dragged; landscape 276 px up
    // to half the page, portrait 30-85% of the page's height; kept in the settings.
    {
        const auto dragGrip = [&](QPointF delta) {
            QQuickItem *grip = item("panelResizeGrip");
            QVERIFY(grip && grip->isVisible());
            const QPoint from = grip->mapToScene(QPointF(grip->width() / 2, grip->height() / 2)).toPoint();
            QTest::mousePress(window, Qt::LeftButton, {}, from);
            for (int step = 1; step <= 10; ++step) QTest::mouseMove(window, from + (delta * step / 10).toPoint());
            QTest::mouseRelease(window, Qt::LeftButton, {}, from + delta.toPoint());
        };
        dragGrip(QPointF(-60, 0));  // at the right: to the left is wider
        QTRY_COMPARE(panel->width(), 336.0);
        QCOMPARE(panel->x() + panel->width(), mapPage->width());
        QCOMPARE(QSettings().value(QStringLiteral("mobile/sidePanelWidth")).toReal(), 336.0);
        dragGrip(QPointF(-1000, 0));
        QTRY_COMPARE(panel->width(), mapPage->width() / 2);
        dragGrip(QPointF(1000, 0));
        QTRY_COMPARE(panel->width(), 276.0);
        window->resize(360, 720);
        QTRY_COMPARE(panel->width(), mapPage->width());
        dragGrip(QPointF(0, -100));  // at the bottom: up is taller
        QTRY_COMPARE(panel->height(), qRound(mapPage->height() / 2) + 100.0);
        QCOMPARE(panel->y() + panel->height(), mapPage->height());
        dragGrip(QPointF(0, -1000));
        QTRY_COMPARE(panel->height(), qRound(mapPage->height() * 0.85) * 1.0);
        dragGrip(QPointF(0, 1000));
        QTRY_COMPARE(panel->height(), qRound(mapPage->height() * 0.3) * 1.0);
        QVERIFY(QSettings().value(QStringLiteral("mobile/sidePanelBottomShare")).toReal() < 0.31);
        auto *size = panel->findChild<PanelSize *>();
        QVERIFY(size);
        size->setSideWidth(0);  // the defaults again
        size->setBottomShare(0);
        QTRY_COMPARE(panel->height(), qRound(mapPage->height() / 2) * 1.0);
        window->resize(800, 360);
        QTRY_COMPARE(panel->width(), 276.0);
    }

    // Same pages: Style, Pitch (FEAT-120), Map (stage B).
    // Repeater delegates have no QObject parent: found through the visual tree.
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *root, const QString &name) -> QQuickItem * {
        if (root->objectName() == name) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = visual(child, name)) return found;
        return nullptr;
    };
    QQuickItem *styleSegment = visual(panel, QStringLiteral("styleSegment"));
    QQuickItem *pitchSegment = visual(panel, QStringLiteral("pitchSegment"));
    QVERIFY(styleSegment && styleSegment->isEnabled());
    QVERIFY(pitchSegment && pitchSegment->isEnabled());
    // Sections in the desktop's order, unfolded in landscape.
    for (const char *name : {"sectionShape", "sectionText", "sectionStructure", "sectionBranch", "sectionNumbering"}) QVERIFY(item(name));

    auto *style = panel->findChild<MobileStyle *>();
    QVERIFY(style);
    MindMapModel *model = map->document()->model();
    MindNode *costs = nullptr, *money = nullptr;
    for (const auto &child : model->root()->children) {
        if (child->title == QLatin1String("Costs")) costs = child.get();
        if (child->title == QLatin1String("Money")) money = child.get();
    }
    QVERIFY(costs && money);

    // Live change: Ellipse, one Undo step.
    map->select(costs->id);
    QTRY_COMPARE(style->topicCount(), 1);
    // The "+" knobs stay with the panel open (user's request 2026-11-13). The topic bar stays beside
    // the panel in landscape, centred left of it, and hides under the bottom panel in portrait.
    QTRY_VERIFY(item("addChildKnob")->isVisible());
    QQuickItem *topicBar = item("topicBar");
    QTRY_VERIFY(topicBar->isVisible());
    QVERIFY(topicBar->x() + topicBar->width() <= panel->x());
    QVERIFY(qAbs(topicBar->x() + topicBar->width() / 2 - panel->x() / 2) <= 1);
    window->resize(360, 720);
    QTRY_VERIFY(!topicBar->isVisible());
    QVERIFY(item("addChildKnob")->isVisible());
    window->resize(800, 360);
    QTRY_COMPARE(panel->width(), 276.0);
    QVERIFY(style->choose(MobileStyle::Shape, int(NodeShape::Ellisse)));
    QCOMPARE(costs->shape, NodeShape::Ellisse);
    QCOMPARE(style->reading(MobileStyle::Shape).value(QStringLiteral("value")).toInt(), int(NodeShape::Ellisse));
    QVERIFY(map->undo());
    QCOMPARE(model->nodeById(costs->id)->shape, NodeShape::RettangoloArrotondato);
    costs = model->nodeById(costs->id);
    money = model->nodeById(money->id);

    // Text toggles tapped in the panel (a window tall enough to show them): italic, strikethrough and
    // underline on, then off again.
    QQuickItem *toggles = visual(panel, QStringLiteral("textToggles"));
    QVERIFY(toggles);
    const auto tapToggle = [&](int index) {
        const char *names[] = {"boldToggle", "italicToggle", "strikeToggle", "underlineToggle"};
        QQuickItem *segment = visual(toggles, QLatin1String(names[index]));
        QVERIFY(segment);
        const QPointF centre = segment->mapToScene(QPointF(segment->width() / 2, segment->height() / 2));
        // A finger, as on the phone.
        static QPointingDevice *finger = QTest::createTouchDevice();
        QTest::touchEvent(window, finger).press(0, centre.toPoint(), window);
        QTest::touchEvent(window, finger).release(0, centre.toPoint(), window);
        QTest::qWait(50);
    };
    window->resize(1200, 1000);
    QTRY_COMPARE(panel->x() + panel->width(), mapPage->width());
    QTest::qWait(100);  // the panel's content laid out again
    map->select(costs->id);
    QTRY_COMPARE(style->topicCount(), 1);
    for (int index : {1, 2, 3}) tapToggle(index);
    costs = model->nodeById(costs->id);
    QVERIFY(costs->italic && costs->strikeOut && costs->underline);
    QTRY_COMPARE(toggles->property("checkedSet").toList(), (QVariantList{false, true, true, true}));
    for (int index : {1, 2, 3}) tapToggle(index);
    costs = model->nodeById(costs->id);
    QVERIFY(!costs->italic && !costs->strikeOut && !costs->underline);

    // Multiple selection with different fills: "—"; a pick sets both in one step.
    map->select(costs->id);
    QVERIFY(style->chooseColor(MobileStyle::FillColor, QColor(QStringLiteral("#ff0000"))));
    map->startMultiSelect();
    map->toggleInSelection(money->id);
    QTRY_COMPARE(style->topicCount(), 2);
    QVERIFY(style->reading(MobileStyle::FillColor).value(QStringLiteral("mixed")).toBool());
    QVERIFY(style->choose(MobileStyle::FillStyle, int(NodeFillStyle::TratteggioDiagonale)));
    costs = model->nodeById(costs->id);
    money = model->nodeById(money->id);
    QCOMPARE(costs->fillStyle, NodeFillStyle::TratteggioDiagonale);
    QCOMPARE(money->fillStyle, NodeFillStyle::TratteggioDiagonale);
    QVERIFY(map->undo());
    QCOMPARE(model->nodeById(costs->id)->fillStyle, NodeFillStyle::Pieno);
    QCOMPARE(model->nodeById(money->id)->fillStyle, NodeFillStyle::Pieno);
    map->stopMultiSelect();

    // Follows the selection: the rows read the topic tapped.
    map->select(model->nodeById(costs->id)->id);
    QTRY_COMPARE(style->reading(MobileStyle::FillColor).value(QStringLiteral("value")).value<QColor>(), QColor(QStringLiteral("#ff0000")));
    map->select(model->nodeById(money->id)->id);
    QTRY_VERIFY(style->reading(MobileStyle::FillColor).value(QStringLiteral("value")).value<QColor>() != QColor(QStringLiteral("#ff0000")));

    // Colour sheet: a colour at 50% opacity; the sheet opens from the Fill row's colour.
    QVERIFY(style->chooseColor(MobileStyle::FillColor, QColor::fromRgbF(0, 0.5, 1, 0.5)));
    money = model->nodeById(money->id);
    QCOMPARE(money->fill.alpha(), 128);
    QVERIFY(StyleChoices::recentColors().value(0) == QColor::fromRgbF(0, 0.5, 1, 0.5).name(QColor::HexArgb));
    QObject *sheet = object("colorSheet");
    QVERIFY(sheet);
    QVERIFY(QMetaObject::invokeMethod(sheet, "openFor", Q_ARG(QVariant, int(MobileStyle::FillColor)), Q_ARG(QVariant, QStringLiteral("Fill")),
                                      Q_ARG(QVariant, style->reading(MobileStyle::FillColor))));
    QTRY_VERIFY(sheet->property("opened").toBool());
    QCOMPARE(sheet->property("opacityPercent").toInt(), 50);
    QMetaObject::invokeMethod(sheet, "close");
    QTRY_VERIFY(!sheet->property("opened").toBool());

    // Reset style: the hand-set values go, one step.
    QVERIFY(style->canResetStyle());
    QVERIFY(style->resetStyle());
    money = model->nodeById(money->id);
    QCOMPARE(money->style, NodeStyleOverrides());
    QVERIFY(!style->canResetStyle());
    QVERIFY(map->undo());
    QVERIFY(model->nodeById(money->id)->style != NodeStyleOverrides());

    // Width: a step sets it by hand, Fit goes back to automatic.
    map->select(model->nodeById(money->id)->id);
    QTRY_VERIFY(!style->widthReading().value(QStringLiteral("empty")).toBool());
    const qreal width = style->widthReading().value(QStringLiteral("value")).toDouble();
    QVERIFY(style->stepWidth(10));
    QCOMPARE(model->nodeById(money->id)->customWidth, width + 10);
    QVERIFY(style->canFitWidth());
    QVERIFY(style->fitWidth());
    QCOMPARE(model->nodeById(money->id)->customWidth, 0.0);

    // Structure of one topic: Same as parent first; a type takes its first direction.
    QCOMPARE(style->structureTypes().value(0).toMap().value(QStringLiteral("key")).toString(), QString());
    QVERIFY(style->chooseStructure(MapStructure::typeKey(MapStructure::Type::OrgChart), QString()));
    QCOMPARE(style->structureType(), MapStructure::typeKey(MapStructure::Type::OrgChart));
    QCOMPARE(style->structureDirection(), MapStructure::directionKey(MapStructure::Direction::Down));
    // Only the directions that change the layout can be picked: both sides is the central topic's.
    const auto pickable = [style](MapStructure::Type type) {
        QStringList keys;
        for (const QVariant &item : style->structureDirections(MapStructure::typeKey(type)))
            if (item.toMap().value(QStringLiteral("enabled")).toBool()) keys.append(item.toMap().value(QStringLiteral("key")).toString());
        return keys;
    };
    QCOMPARE(pickable(MapStructure::Type::OrgChart), (QStringList{QStringLiteral("down"), QStringLiteral("up")}));
    QVERIFY(pickable(MapStructure::Type::LogicChart).isEmpty());  // beside its parent it grows on its side

    // Numbering: a preset, then the first number.
    QVariantMap numbering = style->numbering();
    numbering[QStringLiteral("format")] = style->numberingPresets(QStringLiteral(".")).value(0);
    QVERIFY(style->chooseNumbering(numbering));
    QCOMPARE(style->numbering().value(QStringLiteral("preset")).toInt(), 0);
    numbering = style->numbering();
    numbering[QStringLiteral("start")] = 3;
    QVERIFY(style->chooseNumbering(numbering));
    QCOMPARE(style->numbering().value(QStringLiteral("start")).toInt(), 3);

    // Portrait: the sections fold by their header and stay as left.
    window->resize(360, 720);
    QQuickItem *shapeHeader = item("sectionShape");
    QTRY_VERIFY(shapeHeader->isEnabled());
    QQuickItem *shapeSelect = item("shapeSelect");
    QVERIFY(shapeSelect->isVisible());
    QVERIFY(QMetaObject::invokeMethod(shapeHeader, "clicked"));
    QTRY_VERIFY(!shapeSelect->isVisible());
    // A finger dragged up inside the panel scrolls its content, not the map; Style | Pitch | Map stays.
    {
        QVERIFY(QMetaObject::invokeMethod(shapeHeader, "clicked"));  // unfolded again: more to scroll
        QTRY_VERIFY(shapeSelect->isVisible());
        QQuickItem *content = item("panelContent");
        QQuickItem *segments = item("panelSegments");
        QVERIFY(content && segments);
        QTRY_VERIFY(content->property("contentHeight").toReal() > content->height());
        const QPointF segmentsAt = segments->mapToScene(QPointF());
        QSignalSpy panned(canvas, &MapCanvas::viewChanged);
        static QPointingDevice *finger = QTest::createTouchDevice();
        // On a row's label, which takes no touch (user's report 2026-11-13: there the map panned).
        QQuickItem *thickness = item("borderWidthStepper");
        QVERIFY(thickness && thickness->isVisible());
        const QPoint from = thickness->mapToScene(QPointF(10, thickness->height() / 2)).toPoint();
        QVERIFY(content->contains(content->mapFromScene(QPointF(from))));
        QTest::touchEvent(window, finger).press(0, from, window);
        for (int step = 1; step <= 10; ++step) {
            QTest::touchEvent(window, finger).move(0, from - QPoint(0, step * 15), window);
            QTest::qWait(16);
        }
        QTest::touchEvent(window, finger).release(0, from - QPoint(0, 150), window);
        QTRY_VERIFY(content->property("contentY").toReal() > 50);
        // The map's gestures stop at the panel's edge.
        QQuickItem *gestures = item("mapGestures");
        QVERIFY(gestures);
        QCOMPARE(gestures->height(), panel->y());
        QCOMPARE(gestures->width(), canvas->width());
        QCOMPARE(segments->mapToScene(QPointF()), segmentsAt);
        QCOMPARE(panned.count(), 0);
        QVERIFY(QMetaObject::invokeMethod(shapeHeader, "clicked"));  // folded, as the check below expects
        QTRY_VERIFY(!shapeSelect->isVisible());
    }
    // Back closes the panel first.
    QTest::keyClick(window, Qt::Key_Back);
    QTRY_VERIFY(!panel->isVisible());
    QVERIFY(QMetaObject::invokeMethod(panelButton, "clicked"));
    QTRY_VERIFY(panel->isVisible());
    QVERIFY(!item("shapeSelect")->isVisible());  // still folded

    // FEAT-072 B9: with one topic the preset field shows its pill, not the preset's name.
    window->resize(800, 360);
    map->select(costs->id);
    QQuickItem *topicPill = visual(panel, QStringLiteral("topicPreview"));
    QVERIFY(topicPill);
    QTRY_VERIFY(topicPill->isVisible());
    QCOMPARE(topicPill->property("topic").toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Costs"));
    QCOMPARE(style->topicPreview().value(QStringLiteral("text")).value<QColor>(), costs->text);
    map->select(QString());  // no topic: no pill (several topics neither, MobileStyle::topicPreview)
    QTRY_VERIFY(!topicPill->isVisible());
    QVERIFY(style->topicPreview().isEmpty());

    // A boundary selected: its page instead of the topics' (MainWindow::refreshStylePage); a topic
    // again: the topics' page.
    window->resize(800, 360);
    map->select(money->id);
    QVERIFY(map->addBoundary());
    QTRY_VERIFY(item("boundaryStylePage")->isVisible());
    QVERIFY(!item("topicStylePage")->isVisible());
    QVERIFY(!item("relationStylePage")->isVisible() && !item("pictureStylePage")->isVisible());
    QTRY_VERIFY(item("boundaryShapeSelect")->isVisible());
    QVERIFY(item("boundaryShapeSelect")->isEnabled());
    QVERIFY(item("boundaryFillRow")->isEnabled());
    map->select(costs->id);
    QTRY_VERIFY(item("topicStylePage")->isVisible());
    QVERIFY(!item("boundaryStylePage")->isVisible());

    // Stage B: the Map segment shows the Map page; a variant tile and the colour theme menu.
    QQuickItem *mapSegment = visual(panel, QStringLiteral("mapSegment"));
    QVERIFY(mapSegment && mapSegment->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(mapSegment, "clicked"));
    QTRY_VERIFY(item("mapPanelPage")->isVisible());
    QVERIFY(!item("topicStylePage")->isVisible());
    QQuickItem *tile = visual(item("mapPanelPage"), QStringLiteral("variantTile2"));
    QVERIFY(tile);
    QVERIFY(QMetaObject::invokeMethod(tile, "clicked"));
    QTRY_COMPARE(model->paletteVariant, 2);
    QVERIFY(QMetaObject::invokeMethod(item("paletteSelect"), "clicked"));
    QObject *paletteSheet = object("paletteSheet");
    QTRY_VERIFY(paletteSheet->property("opened").toBool());
    QMetaObject::invokeMethod(paletteSheet, "close");
    QTRY_VERIFY(!paletteSheet->property("opened").toBool());
    // B3: "+" opens the theme editor; a square opens the colour sheet without opacity, whose pick
    // fills it; Save stays off until the six are chosen.
    QVERIFY(item("newPaletteButton"));
    QVERIFY(QMetaObject::invokeMethod(item("newPaletteButton"), "clicked"));
    QObject *editor = object("newPaletteSheet");
    QVERIFY(editor);
    QTRY_VERIFY(editor->property("opened").toBool());
    QQuickItem *firstSwatch = nullptr;
    QTRY_VERIFY((firstSwatch = visual(window->contentItem(), QStringLiteral("paletteSwatch0"))));
    QVERIFY(QMetaObject::invokeMethod(firstSwatch, "clicked"));
    QObject *colorSheet = object("colorSheet");
    QVERIFY(colorSheet);
    QTRY_VERIFY(colorSheet->property("opened").toBool());
    QVERIFY(!colorSheet->property("withOpacityField").toBool());
    QVERIFY(QMetaObject::invokeMethod(colorSheet, "pick", Q_ARG(QVariant, QVariant(QColor(255, 0, 0, 128))), Q_ARG(QVariant, true)));
    QTRY_VERIFY(!colorSheet->property("opened").toBool());
    QCOMPARE(editor->property("colors").toStringList().value(0), QStringLiteral("#ff0000"));  // opaque
    auto *save = qobject_cast<QQuickItem *>(object("savePalette"));
    QVERIFY(save);
    QVERIFY(!save->isEnabled());
    QMetaObject::invokeMethod(editor, "close");
    QTRY_VERIFY(!editor->property("opened").toBool());
    QVERIFY(style->palettes(3).isEmpty());  // Cancel adds nothing

    // B2: the map type card with its sample map; its menu chooses a variant.
    auto *cardSample = qobject_cast<VariantSample *>(visual(item("mapPanelPage"), QStringLiteral("mapTypeCardSample")));
    QVERIFY(cardSample);
    QVERIFY(!cardSample->sampleBounds().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(item("mapTypeCard"), "clicked"));
    QObject *typeSheet = object("mapTypeSheet");
    QTRY_VERIFY(typeSheet->property("opened").toBool());
    const QString treeVariant = MapVariants::ofType(MapStructure::Type::TreeChart).first()->id;
    QQuickItem *typeTile = nullptr;
    QTRY_VERIFY((typeTile = visual(window->contentItem()->parentItem() ? window->contentItem()->parentItem() : window->contentItem(),
                                   QStringLiteral("mapTypeTile-") + treeVariant)));
    QVERIFY(QMetaObject::invokeMethod(typeTile, "clicked"));
    QTRY_VERIFY(!typeSheet->property("opened").toBool());
    QCOMPARE(model->sheetStyle.variant, treeVariant);
    // Reopened, the tiles draw their samples again: closing the popup takes its items out of the
    // window and drops their scene graph nodes, and a sample built once came back as a blank tile.
    const auto firstTile = [&]() -> VariantSample * {
        QQuickItem *tile = visual(window->contentItem()->parentItem() ? window->contentItem()->parentItem() : window->contentItem(),
                                  QStringLiteral("mapTypeTile-") + MapVariants::all().first().id);
        return tile ? tile->findChild<VariantSample *>() : nullptr;
    };
    QVERIFY(QMetaObject::invokeMethod(item("mapTypeCard"), "clicked"));
    QTRY_VERIFY(typeSheet->property("opened").toBool());
    VariantSample *tileSample = firstTile();
    QVERIFY(tileSample);
    window->grabWindow();
    const int builds = tileSample->sceneBuilds();
    QVERIFY(builds > 0);
    QMetaObject::invokeMethod(typeSheet, "close");
    QTRY_VERIFY(!typeSheet->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(item("mapTypeCard"), "clicked"));
    QTRY_VERIFY(typeSheet->property("opened").toBool());
    QCOMPARE(firstTile(), tileSample);
    window->grabWindow();
    QCOMPARE(tileSample->sceneBuilds(), builds + 1);
    QMetaObject::invokeMethod(typeSheet, "close");
    QTRY_VERIFY(!typeSheet->property("opened").toBool());
    QVERIFY(map->undo());
    QVERIFY(map->undo());
    QTRY_VERIFY(model->paletteVariant != 2);
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

    // The top right group (user's request 2026-11-13): the map menu's arrow, Undo, Redo, Fit; Find,
    // Present, Outliner and Gantt are rows of the menu, which opens 4 px under the arrow, right edges aligned.
    {
        QStringList shown;
        for (QQuickItem *child : item("undoButton")->parentItem()->childItems())
            if (child->isVisible()) shown << child->objectName();
        // FEAT-119: the panel's button is the rightmost; on this 412 dp page Fit is a row of the menu.
        QVERIFY(window->width() < 600);
        QCOMPARE(shown, (QStringList{QStringLiteral("mapMenuButton"), QStringLiteral("undoButton"),
                                     QStringLiteral("redoButton"), QStringLiteral("panelButton")}));
    }
    QQuickItem *find = item("findButton");
    QVERIFY(find && !find->isVisible());
    QCOMPARE(find->property("text").toString(), QStringLiteral("Find"));
    QQuickItem *arrow = item("mapMenuButton");
    QObject *mapMenu = object("mapMenu");
    QVERIFY(arrow && mapMenu);
    QVERIFY(QMetaObject::invokeMethod(arrow, "clicked"));
    QTRY_VERIFY(mapMenu->property("opened").toBool());
    QQuickItem *findRow = nullptr;
    QStringList menuRows;
    for (QQuickItem *row : mapMenu->property("rows").value<QQuickItem *>()->childItems()) {
        if (row->objectName().isEmpty()) continue;  // the Repeater
        menuRows << row->objectName();
        if (row->objectName() == QLatin1String("findButtonRow")) findRow = row;
        // Present works since FEAT-120, Outliner since FEAT-121, Gantt since FEAT-122.
        if (row->objectName() == QLatin1String("ganttButtonRow")) QVERIFY(row->isEnabled());
        if (row->objectName() == QLatin1String("outlinerButtonRow")) QVERIFY(row->isEnabled());
    }
    QCOMPARE(menuRows, (QStringList{QStringLiteral("fitMenuButtonRow"), QStringLiteral("findButtonRow"), QStringLiteral("outlinerButtonRow"),
                                 QStringLiteral("ganttButtonRow"), QStringLiteral("presentButtonRow")}));
    {
        auto *frame = mapMenu->property("background").value<QQuickItem *>();
        const qreal arrowRight = arrow->mapToScene(QPointF((arrow->width() + arrow->property("visualWidth").toReal()) / 2, 0)).x();
        const qreal arrowBottom = arrow->mapToScene(QPointF(0, (arrow->height() + 32) / 2)).y();
        QVERIFY(qAbs(frame->mapToScene(QPointF(frame->width(), 0)).x() - arrowRight) < 0.5);
        QVERIFY(qAbs(frame->mapToScene(QPointF()).y() - (arrowBottom + 4)) < 0.5);
    }
    QVERIFY(findRow && QMetaObject::invokeMethod(findRow, "clicked"));
    QTRY_VERIFY(!mapMenu->property("opened").toBool());
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
void MobileTest::sidePanelObjectPages() {
    const QString sticker = StickerLibrary().collections().constFirst().toMap().value(QStringLiteral("items")).toList()
                                .constFirst().toMap().value(QStringLiteral("path")).toString();
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Objects.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MapCanvas canvas;
    canvas.setSize(QSizeF(400, 800));
    canvas.setMap(&map);
    MobileStyle style;
    style.setMap(&map);
    MindMapModel *model = map.document()->model();
    const QString firstId = childTitled(model->root(), QStringLiteral("First"))->id;
    const QString secondId = childTitled(model->root(), QStringLiteral("Second"))->id;
    const auto tapOn = [&](const QPointF &mapPoint) {
        const QPointF at = canvas.itemPoint(mapPoint);
        canvas.tapAt(at.x(), at.y());
    };
    const auto value = [&](MobileStyle::Property property) { return style.reading(property).value(QStringLiteral("value")); };

    // A topic: the topics' page; its picture picked: the picture's page (MainWindow::refreshStylePage).
    map.select(firstId);
    QCOMPARE(style.target(), int(MobileStyle::Topics));
    QVERIFY(map.insertSticker(sticker));
    canvas.zoomStep(0);
    canvas.reveal(map.selectedTopic()->bounds);
    {
        const MapSnapshot::Topic *topic = map.selectedTopic();
        tapOn(topic->geometry->imageRect().translated(topic->position).center());
    }
    QVERIFY(map.pictureSelected());
    QCOMPARE(style.target(), int(MobileStyle::Picture));
    QVERIFY(style.pictureIsSticker());
    QCOMPARE(value(MobileStyle::PictureOutline).toBool(), true);  // a new sticker has its outline

    // Border, thickness, shadow, opacity: one step each, the picture stays picked.
    const MindNode *first = model->nodeById(firstId);
    QVERIFY(style.choose(MobileStyle::PictureBorder, int(ImageStyle::Border::Dashed)));
    QCOMPARE(first->imageStyle.border, ImageStyle::Border::Dashed);
    QVERIFY(map.pictureSelected());
    QVERIFY(style.choose(MobileStyle::PictureBorderWidth, 40.0));  // clamped to the desktop's range
    QCOMPARE(first->imageStyle.borderWidth, NodeStyle::kMaxBorderWidthPx);
    QVERIFY(style.chooseColor(MobileStyle::PictureBorderColor, QColor(Qt::red)));
    QCOMPARE(first->imageStyle.borderColor, QColor(Qt::red));
    // FEAT-129: the topic shapes, with the shapes' icons.
    QCOMPARE(style.choices(MobileStyle::PictureShape), style.choices(MobileStyle::Shape));
    QCOMPARE(style.iconKind(MobileStyle::PictureShape), int(MobileStyle::ShapeIcon));
    QVERIFY(style.choose(MobileStyle::PictureShape, int(NodeShape::Stella)));
    QCOMPARE(first->imageStyle.shape, NodeShape::Stella);
    QCOMPARE(value(MobileStyle::PictureShape).toInt(), int(NodeShape::Stella));
    // FEAT-131: the effects, text only, "No effect" first.
    QCOMPARE(style.choices(MobileStyle::PictureEffect).size(), 5);
    QCOMPARE(style.iconKind(MobileStyle::PictureEffect), -1);
    MobileOutline outline;
    outline.setMap(&map);
    const auto outlineKey = [&] { return outline.data(outline.index(outline.rowOf(firstId)), MobileOutline::PictureKeyRole).toString(); };
    const QString plainKey = outlineKey();
    QVERIFY(style.choose(MobileStyle::PictureEffect, int(ImageEffect::Sepia)));
    QCOMPARE(first->imageStyle.effect, ImageEffect::Sepia);
    QVERIFY(outlineKey() != plainKey);  // the phone outliner reloads the picture
    QCOMPARE(value(MobileStyle::PictureEffect).toInt(), int(ImageEffect::Sepia));
    QVERIFY(style.choose(MobileStyle::PictureShadow, true));
    QVERIFY(style.choose(MobileStyle::PictureOpacity, 50.0));
    QCOMPARE(first->imageStyle.opacity, 50);
    QVERIFY(!style.choose(MobileStyle::PictureOpacity, 50.0));  // unchanged: no step
    QVERIFY(map.undo());
    first = model->nodeById(firstId);
    QCOMPARE(first->imageStyle.opacity, 100);
    QVERIFY(first->imageStyle.shadow);

    // Size with the proportions kept, then not; its reset; the turn swaps the size.
    map.select(firstId);
    {
        const MapSnapshot::Topic *topic = map.selectedTopic();
        tapOn(topic->geometry->imageRect().translated(topic->position).center());
    }
    QVERIFY(map.pictureSelected());
    const QSizeF inserted = model->nodeById(firstId)->imageSize;
    QVERIFY(style.choosePictureWidth(inserted.width() * 2, true));
    QCOMPARE(model->nodeById(firstId)->imageSize, QSizeF(inserted.width() * 2, std::round(inserted.height() * 2)));
    QVERIFY(style.choosePictureHeight(30, false));
    QCOMPARE(model->nodeById(firstId)->imageSize, QSizeF(inserted.width() * 2, 30));
    QCOMPARE(style.pictureSize().value(QStringLiteral("height")).toDouble(), 30.0);
    QVERIFY(style.resetPictureSize());
    QCOMPARE(model->nodeById(firstId)->imageSize, NodeGeometry::insertionImageSize(model->nodeById(firstId)->imagePath));
    QVERIFY(style.choosePictureHeight(30, false));
    const QSizeF before = model->nodeById(firstId)->imageSize;
    QVERIFY(style.rotatePicture());
    QCOMPARE(model->nodeById(firstId)->imageStyle.rotation, 90);
    QCOMPARE(model->nodeById(firstId)->imageSize, before.transposed());
    // Reset style: border, shadow and opacity go; the sticker's outline and the turn stay.
    QVERIFY(style.resetPictureStyle());
    first = model->nodeById(firstId);
    QCOMPARE(first->imageStyle.border, ImageStyle::Border::None);
    QVERIFY(!first->imageStyle.shadow);
    QVERIFY(first->imageStyle.outline);
    QCOMPARE(first->imageStyle.rotation, 90);
    QVERIFY(map.pictureSelected());

    // A relationship: its page, each change one step, the line stays selected.
    map.select(firstId);
    QVERIFY(!map.document()->addRelation(firstId, secondId).isEmpty());
    QVERIFY(map.switchSheet(1));
    QVERIFY(map.switchSheet(0));
    canvas.reveal(map.snapshot()->relations().front().geometry.path.boundingRect());
    tapOn(map.snapshot()->relations().front().geometry.path.pointAtPercent(0.5));
    QVERIFY(!map.selectedRelationId().isEmpty());
    QCOMPARE(style.target(), int(MobileStyle::Relation));
    QCOMPARE(style.topicCount(), 0);
    QCOMPARE(value(MobileStyle::RelationPattern).toInt(), int(Qt::DashLine));
    QCOMPARE(value(MobileStyle::RelationFontSize).toDouble(), Relationships::kDefaultFontSize);
    QCOMPARE(style.reading(MobileStyle::RelationLineColor).value(QStringLiteral("manual")).toBool(), false);
    QVERIFY(style.choose(MobileStyle::RelationShape, int(Relationship::Shape::Elbow)));
    QCOMPARE(model->relations.front().shape, Relationship::Shape::Elbow);
    QVERIFY(style.choose(MobileStyle::RelationEnd, int(BranchEnd::None)));
    QVERIFY(style.chooseColor(MobileStyle::RelationTextColor, QColor(Qt::blue)));
    QCOMPARE(model->relations.front().textColor, std::optional<QColor>(QColor(Qt::blue)));
    QVERIFY(!map.selectedRelationId().isEmpty());
    QVERIFY(map.undo());
    QVERIFY(!model->relations.front().textColor.has_value());
    QCOMPARE(model->relations.front().endMark, std::optional<BranchEnd>(BranchEnd::None));

    // A boundary: its page; a wider line lays the map out in the same step; it stays selected.
    map.select(secondId);
    QVERIFY(map.addBoundary());
    QCOMPARE(style.target(), int(MobileStyle::BoundaryTarget));
    const QString boundaryId = map.selectedBoundaryId();
    QVERIFY(style.choose(MobileStyle::BoundaryShape, int(Boundary::Shape::Rectangle)));
    QVERIFY(style.choose(MobileStyle::BoundaryWidth, NodeStyle::kNamedBranchWidths.last()));
    QCOMPARE(model->boundaries.front().lineWidth, std::optional<qreal>(NodeStyle::kNamedBranchWidths.last()));
    QVERIFY(style.chooseColor(MobileStyle::BoundaryFillColor, QColor(Qt::green)));
    QCOMPARE(map.selectedBoundaryId(), boundaryId);
    QVERIFY(map.undo());
    QVERIFY(!model->boundaries.front().fillColor.has_value());
    QCOMPARE(model->boundaries.front().shape, Boundary::Shape::Rectangle);
    // FEAT-130: the eight shapes, "None", the title's font, size, colour and bold.
    QCOMPARE(style.choices(MobileStyle::BoundaryShape).size(), 11);  // FEAT-134: Tag, Rounded left, Frame
    QCOMPARE(style.iconKind(MobileStyle::BoundaryShape), int(MobileStyle::BoundaryShapeIcon));
    QVERIFY(style.choose(MobileStyle::BoundaryShape, int(Boundary::Shape::Scallops)));
    QCOMPARE(model->boundaries.front().shape, Boundary::Shape::Scallops);
    QVERIFY(style.choose(MobileStyle::BoundaryPattern, int(Qt::NoPen)));
    QCOMPARE(model->boundaries.front().linePattern, std::optional<int>(Qt::NoPen));
    QVERIFY(style.choose(MobileStyle::BoundaryTitleSize, 20.0));
    QCOMPARE(model->boundaries.front().titleSize, std::optional<qreal>(20));
    QVERIFY(style.choose(MobileStyle::BoundaryTitleBold, true));
    QVERIFY(model->boundaries.front().titleBold);
    QCOMPARE(style.reading(MobileStyle::BoundaryTitleBold).value(QStringLiteral("value")).toBool(), true);
    QVERIFY(style.chooseColor(MobileStyle::BoundaryTitleColor, QColor(Qt::red)));
    QCOMPARE(model->boundaries.front().titleColor, std::optional<QColor>(QColor(Qt::red)));
    QVERIFY(style.choose(MobileStyle::BoundaryTitleFont, QStringLiteral("Arial")));
    QCOMPARE(model->boundaries.front().titleFont, QStringLiteral("Arial"));
    QCOMPARE(map.selectedBoundaryId(), boundaryId);
    // A tap on an ellipse's side, outside its stroke rect, selects it.
    QVERIFY(style.choose(MobileStyle::BoundaryShape, int(Boundary::Shape::Ellipse)));
    const MapSnapshot::BoundaryShape *oval = map.snapshot()->boundaryOf(boundaryId);
    QVERIFY(oval);
    const QPointF lobe(oval->stroke.left() - 6, oval->stroke.center().y());
    QVERIFY(map.snapshot()->boundaryAt(lobe, 0) == oval);
    QVERIFY(!map.snapshot()->boundaryAt(oval->stroke.topLeft() - QPointF(6, 6), 0));
    // FEAT-134: the topics' fill styles; a pattern draws strokes instead of the area.
    QCOMPARE(style.iconKind(MobileStyle::BoundaryFillStyle), int(MobileStyle::FillStyleIcon));
    QCOMPARE(style.choices(MobileStyle::BoundaryFillStyle).size(), StyleChoices::fillStyles().size());
    QVERIFY(style.choose(MobileStyle::BoundaryFillStyle, int(NodeFillStyle::TratteggioDiagonale)));
    QCOMPARE(model->boundaries.front().fillStyle, NodeFillStyle::TratteggioDiagonale);
    QCOMPARE(style.reading(MobileStyle::BoundaryFillStyle).value(QStringLiteral("value")).toInt(), int(NodeFillStyle::TratteggioDiagonale));
    const MapSnapshot::BoundaryShape *hatched = map.snapshot()->boundaryOf(boundaryId);
    QVERIFY(hatched && hatched->fill.area.isEmpty() && !hatched->fill.pattern.lines.isEmpty());
    QVERIFY(style.choose(MobileStyle::BoundaryShape, int(Boundary::Shape::Frame)));
    QCOMPARE(model->boundaries.front().shape, Boundary::Shape::Frame);
    QCOMPARE(map.selectedBoundaryId(), boundaryId);
}

void MobileTest::sidePanelMapPage() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("MapPage.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MobileStyle style;
    style.setMap(&map);
    MindMapModel *model = map.document()->model();
    const auto sheet = [&](const char *key) { return style.sheet().value(QLatin1String(key)); };

    // Map page with nothing selected: a variant of the colour theme, one Undo step.
    QCOMPARE(style.topicCount(), 0);
    QCOMPARE(sheet("variantCount").toInt(), NodeStyle::kVariantCount);
    const int before = model->paletteVariant;
    QVERIFY(style.chooseVariant(1));
    QCOMPARE(model->paletteVariant, 1);
    QCOMPARE(sheet("variant").toInt(), 1);
    QVERIFY(style.reading(MobileStyle::SheetBackground).value(QStringLiteral("manual")).toBool());
    QVERIFY(map.undo());
    QCOMPARE(model->paletteVariant, before);

    // The branch line width with its tapered versions; the background; the line colour.
    const QVariantList widths = style.sheetWidths();
    QCOMPARE(widths.size(), 1 + 2 * NodeStyle::kNamedBranchWidths.size());
    QVERIFY(style.chooseSheet(QStringLiteral("width"), -NodeStyle::kNamedBranchWidths[1]));
    QCOMPARE(model->sheetStyle.branchWidth, NodeStyle::kNamedBranchWidths[1]);
    QVERIFY(model->sheetStyle.tapered);
    QCOMPARE(sheet("width").toDouble(), -NodeStyle::kNamedBranchWidths[1]);
    QVERIFY(style.chooseColor(MobileStyle::SheetBackground, QColor(Qt::red)));
    QCOMPARE(model->backdrop().color, QColor(Qt::red));
    QVERIFY(style.chooseSheet(QStringLiteral("coloredBranches"), false));
    QVERIFY(style.chooseColor(MobileStyle::SheetLineColor, QColor(Qt::blue)));
    QCOMPARE(model->sheetStyle.lineColor, QColor(Qt::blue));
    QVERIFY(!style.chooseSheet(QStringLiteral("coloredBranches"), false));  // unchanged: no step
    QVERIFY(map.undo());
    QCOMPARE(model->sheetStyle.lineColor, NodeStyle::kDefaultBranchLineColor);
    QVERIFY(!model->sheetStyle.coloredBranches);

    // A colour theme from the Classic group, starred into the Favourites and out again.
    const QVariantList classic = style.palettes(2);
    QVERIFY(!classic.isEmpty());
    const QString classicId = classic.first().toMap().value(QStringLiteral("id")).toString();
    QCOMPARE(style.paletteGroup(classicId), 2);
    QVERIFY(style.choosePalette(classicId));
    QCOMPARE(model->paletteId, classicId);
    QCOMPARE(model->activePalette, classic.first().toMap().value(QStringLiteral("colors")).toStringList());
    QVERIFY(!style.choosePalette(classicId));  // the same: no step
    style.setFavoritePalette(classicId, true);
    QCOMPARE(style.palettes(0).first().toMap().value(QStringLiteral("id")).toString(), classicId);
    style.setFavoritePalette(classicId, false);
    QVERIFY(style.palettes(0).isEmpty());

    // The whole map's numbering is the central topic's.
    QVariantMap numbering = style.numbering(true);
    QCOMPARE(numbering.value(QStringLiteral("empty")).toBool(), false);
    numbering.insert(QStringLiteral("format"), style.numberingPresets(QStringLiteral(".")).first());
    QVERIFY(style.chooseNumbering(numbering, true));
    QVERIFY(model->root()->style.numbering.has_value());

    // Show all notes: the phone measures and draws the note in the topic.
    MindNode *first = const_cast<MindNode *>(childTitled(model->root(), QStringLiteral("First")));
    QVERIFY(map.document()->setNote(first, QStringLiteral("A note"), QString()));
    QVERIFY(style.chooseSheet(QStringLiteral("showNotes"), true));
    QVERIFY(!map.snapshot()->topicOf(model->nodeById(first->id))->geometry->notePreviewRect().isEmpty());
    QVERIFY(style.chooseSheet(QStringLiteral("showNotes"), false));
    QVERIFY(map.snapshot()->topicOf(model->nodeById(first->id))->geometry->notePreviewRect().isEmpty());

    // Free branch positions: each main branch keeps its side; Balance map off with it.
    QVERIFY(sheet("balanceEnabled").toBool());
    QVERIFY(style.chooseSheet(QStringLiteral("freeBranches"), true));
    for (const auto &child : model->root()->children) QVERIFY(child->side != MindNode::Side::Auto);
    QVERIFY(!sheet("balanceEnabled").toBool());
    QVERIFY(style.chooseSheet(QStringLiteral("freeBranches"), false));
    QVERIFY(style.chooseBalanced(!model->balanced));

    // B3, the theme editor: six colours and a free name make an own theme on the sheet, one step;
    // a name already taken is refused.
    const QStringList six{QStringLiteral("#112233"), QStringLiteral("#223344"), QStringLiteral("#334455"),
                          QStringLiteral("#445566"), QStringLiteral("#556677"), QStringLiteral("#667788")};
    QVERIFY(!style.createPalette(QStringLiteral("Mine"), six.mid(0, 5) + QStringList{QString()}));  // a colour missing
    const QString defaultName = style.nextPaletteName();
    QVERIFY(style.createPalette(QString(), six));  // empty name: the next "Custom N"
    QCOMPARE(model->paletteName, defaultName);
    QCOMPARE(model->activePalette, six);
    QVERIFY(style.isPaletteNameTaken(defaultName));
    QVERIFY(!style.createPalette(defaultName, six));
    QCOMPARE(style.palettes(3).size(), 1);
    QVERIFY(map.undo());
    QVERIFY(model->activePalette != six);
    QVERIFY(style.removePalette(style.palettes(3).first().toMap().value(QStringLiteral("id")).toString()));
    QVERIFY(style.palettes(3).isEmpty());

    // B2, the map type: the card shows the sheet's type; a variant of another type changes the
    // sheet's type and style in one step.
    const QVariantList types = style.mapTypes();
    QVERIFY(types.size() > 1);
    QCOMPARE(style.mapTypeName(style.mapVariant()), StyleChoices::structureName(MapStructure::Type::MindMap));
    const QString orgVariant = [&] {
        for (const QVariant &type : types)
            if (type.toMap().value(QStringLiteral("type")).toString() == MapStructure::typeKey(MapStructure::Type::OrgChart))
                return type.toMap().value(QStringLiteral("variants")).toStringList().first();
        return QString();
    }();
    QVERIFY(!orgVariant.isEmpty());
    QVERIFY(style.chooseMapVariant(orgVariant));
    QCOMPARE(model->sheetStyle.variant, orgVariant);
    QCOMPARE(MapStructure::typeFromKey(model->root()->structure), MapStructure::Type::OrgChart);
    QCOMPARE(style.mapVariant(), orgVariant);
    QVERIFY(!style.chooseMapVariant(orgVariant));  // the same: no step
    QVERIFY(map.undo());
    QCOMPARE(MapStructure::typeFromKey(model->root()->structure), MapStructure::Type::MindMap);

    // Reset map style: nothing left to reset afterwards.
    QVERIFY(sheet("canReset").toBool());
    QVERIFY(style.resetMapStyle());
    QVERIFY(!sheet("canReset").toBool());
}

// FEAT-120 stage A: the Pitch page (the desktop's PitchPage rows, one Undo step each) and the
// presentation (taps, swipes, rotation, Back).
void MobileTest::pitchOnThePhone() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Pitch.mentis"));
    QCOMPARE(writeSearchMap(path), QString());
    {
        MobileMap map;
        QVERIFY(map.open(path));
        MobilePitch pitch;
        pitch.setMap(&map);
        MindMapModel *model = map.document()->model();
        MindNode *root = model->root();
        const MindNode *plan = childTitled(root, QStringLiteral("Plan"));
        const MindNode *costs = childTitled(root, QStringLiteral("Costs"));
        QVERIFY(plan && costs);

        // Nothing selected: the central topic's rows; Auto says what it resolves to.
        QVariantMap state = pitch.state();
        QVERIFY(state.value(QStringLiteral("topic")).toBool());
        QCOMPARE(state.value(QStringLiteral("layout")).toInt(), int(Pitch::Layout::Bracket));
        QCOMPARE(state.value(QStringLiteral("topicSlideNames")).toStringList(), PitchChoices::visibilityNames(true));
        // One step: Grid on the central topic.
        QVERIFY(pitch.chooseTopic(MobilePitch::Layout, int(Pitch::Layout::Grid)));
        QCOMPARE(root->pitchLayout, Pitch::layoutKey(Pitch::Layout::Grid));
        QVERIFY(!pitch.chooseTopic(MobilePitch::Layout, int(Pitch::Layout::Grid)));  // the same: no step
        QVERIFY(map.undo());
        QCOMPARE(model->root()->pitchLayout, QString());
        // A topic: its rows; Delivery and Topic slide.
        map.select(costs->id);
        QVERIFY(pitch.chooseTopic(MobilePitch::Delivery, int(Pitch::Delivery::All)));
        QVERIFY(pitch.chooseTopic(MobilePitch::TopicSlide, int(Pitch::Visibility::Hidden)));
        QCOMPARE(model->nodeById(costs->id)->pitchDelivery, Pitch::deliveryKey(Pitch::Delivery::All));
        QCOMPARE(pitch.state().value(QStringLiteral("topicSlide")).toInt(), int(Pitch::Visibility::Hidden));
        QVERIFY(map.undo());
        QVERIFY(map.undo());
        QCOMPARE(model->nodeById(costs->id)->pitchDelivery, QString());
        // Several topics: the rows are disabled and change nothing.
        map.startMultiSelect();
        map.toggleInSelection(plan->id);
        QCOMPARE(map.selectedIds().size(), 2);
        QVERIFY(!pitch.state().value(QStringLiteral("topic")).toBool());
        QVERIFY(!pitch.chooseTopic(MobilePitch::Layout, int(Pitch::Layout::Grid)));
        map.stopMultiSelect();
        // The sheet: Change theme in the desktop's two groups; a theme and a ratio, one step each.
        const QVariantList themes = pitch.themes();
        QCOMPARE(themes.size(), Pitch::kThemeCount);
        for (int i = 0; i < themes.size(); ++i)
            QCOMPARE(themes[i].toMap().value(QStringLiteral("illustrated")).toBool(), i >= Pitch::kBasicThemeCount);
        QVERIFY(pitch.chooseTheme(int(Pitch::Theme::Night)));
        QCOMPARE(model->pitchTheme, Pitch::themeKey(Pitch::Theme::Night));
        QCOMPARE(pitch.state().value(QStringLiteral("theme")).toInt(), int(Pitch::Theme::Night));
        QVERIFY(pitch.chooseAspect(int(Pitch::Aspect::Classic)));
        QCOMPARE(model->pitchAspect, Pitch::aspectKey(Pitch::Aspect::Classic));
        QVERIFY(map.undo());
        QVERIFY(map.undo());
        QCOMPARE(map.document()->model()->pitchTheme, QString());
        map.close();
    }

    QQmlApplicationEngine engine;
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
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    const auto item = [&](const char *name) { return window->findChild<QQuickItem *>(QLatin1String(name)); };
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *root, const QString &name) -> QQuickItem * {
        if (root->objectName() == name) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = visual(child, name)) return found;
        return nullptr;
    };
    MindMapModel *model = map->document()->model();
    window->resize(360, 720);

    // The Pitch segment shows the Pitch page; a layout cell is one step.
    QVERIFY(QMetaObject::invokeMethod(item("panelButton"), "clicked"));
    QQuickItem *panel = nullptr;
    QTRY_VERIFY((panel = item("sidePanel")) && panel->isVisible());
    QQuickItem *pitchSegment = visual(panel, QStringLiteral("pitchSegment"));
    QVERIFY(pitchSegment && pitchSegment->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(pitchSegment, "clicked"));
    QTRY_VERIFY(item("pitchPanelPage")->isVisible());
    QVERIFY(item("pitchPreview")->height() >= 90);
    QQuickItem *grid = visual(item("pitchPanelPage"), QStringLiteral("pitchLayout7"));
    QVERIFY(grid && grid->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(grid, "clicked"));
    QCOMPARE(model->root()->pitchLayout, Pitch::layoutKey(Pitch::Layout::Grid));
    QTRY_VERIFY(grid->property("current").toBool());
    QVERIFY(map->undo());
    QTRY_VERIFY(!grid->property("current").toBool());
    // Change theme: thirteen cards; one chosen closes the sheet.
    QVERIFY(QMetaObject::invokeMethod(item("pitchThemeButton"), "clicked"));
    auto *themeSheet = window->findChild<QObject *>(QStringLiteral("pitchThemeSheet"));
    QTRY_VERIFY(themeSheet->property("opened").toBool());
    QQuickItem *night = nullptr;
    QQuickItem *top = window->contentItem()->parentItem() ? window->contentItem()->parentItem() : window->contentItem();
    QTRY_VERIFY((night = visual(top, QStringLiteral("pitchThemeCard%1").arg(int(Pitch::Theme::Night)))));
    QVERIFY(QMetaObject::invokeMethod(night, "clicked"));
    QTRY_VERIFY(!themeSheet->property("opened").toBool());
    QCOMPARE(model->pitchTheme, Pitch::themeKey(Pitch::Theme::Night));
    QVERIFY(QMetaObject::invokeMethod(item("panelButton"), "clicked"));
    QTRY_VERIFY(!panel->isVisible());

    // Present: full screen from the cover. Cover, the central topic's list (3 items one at a time),
    // Plan's topic slide, its list, Costs and Money.
    QQuickItem *present = item("presentButton");
    QVERIFY(present && present->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(present, "clicked"));
    auto *view = window->findChild<QObject *>(QStringLiteral("presentationView"));
    QTRY_VERIFY(view->property("opened").toBool());
    auto *slides = window->findChild<PitchPresentation *>();
    QVERIFY(slides && slides->running());
    QCOMPARE(slides->slideCount(), 6);
    QCOMPARE(slides->slide(), 0);
    QTRY_COMPARE(slides->width(), 360.0);
    // A tap on the right two thirds goes forward, on the left third back.
    QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(300, 400));
    QCOMPARE(slides->slide(), 1);
    QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(60, 400));
    QCOMPARE(slides->slide(), 0);
    // A swipe left goes forward, a swipe right back; a step at a time on a list.
    const auto swipe = [&](int fromX, int toX) {
        QTest::mousePress(window, Qt::LeftButton, {}, QPoint(fromX, 400));
        QTest::mouseMove(window, QPoint((fromX + toX) / 2, 400));
        QTest::mouseRelease(window, Qt::LeftButton, {}, QPoint(toX, 400));
    };
    swipe(300, 100);
    QCOMPARE(slides->slide(), 1);
    QCOMPARE(slides->shown(), 0);
    swipe(300, 100);
    QCOMPARE(slides->shown(), 1);
    swipe(100, 300);
    QCOMPARE(slides->shown(), 0);
    // Auto takes the item's shape (on Android always landscape), at the same step.
    QCOMPARE(slides->slideRect().size(), QSizeF(360, 720));
    window->resize(720, 360);
    QTRY_COMPARE(slides->slideRect().size(), QSizeF(720, 360));
    QCOMPARE(slides->slide(), 1);
    // A hardware keyboard steps with the desktop's keys.
    QTest::keyClick(window, Qt::Key_Right);
    QCOMPARE(slides->shown(), 1);
    QTest::keyClick(window, Qt::Key_Left);
    QCOMPARE(slides->shown(), 0);
    QTest::keyClick(window, Qt::Key_End);
    QCOMPARE(slides->slide(), 5);
    QTest::keyClick(window, Qt::Key_Home);
    QCOMPARE(slides->slide(), 0);
    QTest::keyClick(window, Qt::Key_PageDown);
    QCOMPARE(slides->slide(), 1);
    // To Plan's topic slide, then Back: the map selects Plan.
    for (int i = 0; i < 4; ++i) slides->next();
    QCOMPARE(slides->slide(), 2);
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!view->property("opened").toBool());
    QVERIFY(!slides->running());
    QCOMPARE(map->selectedId(), childTitled(model->root(), QStringLiteral("Plan"))->id);

    // No slides: the map says so and nothing opens.
    QVERIFY(map->document()->setPitchTopic(model->root(), Document::PitchField::TopicSlide, Pitch::visibilityKey(Pitch::Visibility::Hidden)));
    for (const auto &child : model->root()->children)
        QVERIFY(map->document()->setPitchTopic(child.get(), Document::PitchField::TopicSlide, Pitch::visibilityKey(Pitch::Visibility::Hidden)));
    for (MindNode *node : {model->root(), const_cast<MindNode *>(childTitled(model->root(), QStringLiteral("Plan")))})
        QVERIFY(map->document()->setPitchTopic(node, Document::PitchField::ListSlide, Pitch::visibilityKey(Pitch::Visibility::Hidden)));
    QVERIFY(QMetaObject::invokeMethod(present, "clicked"));
    QVERIFY(!view->property("opened").toBool());
    QVERIFY(!map->message().isEmpty());
}

void MobileTest::pitchTemplatesOnThePhone() {
    QDir(PitchTemplates::folder()).removeRecursively();
    const auto clearLibrary = qScopeGuard([] { QDir(PitchTemplates::folder()).removeRecursively(); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Templates.mentis"));
    QCOMPARE(writeSearchMap(path), QString());
    const QString picturePath = source.filePath(QStringLiteral("logo.png"));
    {
        QImage picture(1200, 600, QImage::Format_ARGB32);
        picture.fill(QColor(0, 120, 200, 128));  // transparency: kept as PNG
        QVERIFY(picture.save(picturePath));
    }
    UrlCatcher system;  // Export's Share shows the folder off Android
    QDesktopServices::setUrlHandler(QStringLiteral("file"), &system, "open");
    const auto unsetHandler = qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("file")); });
    {
        MobileMap map;
        QVERIFY(map.open(path));
        MobilePitch pitch;
        pitch.setMap(&map);
        MindMapModel *model = map.document()->model();
        QVERIFY(pitch.templates().isEmpty());
        QVERIFY(!pitch.editTemplate());  // no template on the sheet

        // New: a draft on the sheet's theme; the preview shows it; nothing on the sheet yet.
        QVERIFY(pitch.chooseTheme(int(Pitch::Theme::Night)));
        QVERIFY(pitch.newTemplate());
        QVERIFY(pitch.editing());
        QVariantMap draft = pitch.draft();
        QCOMPARE(draft.value(QStringLiteral("base")).toInt(), int(Pitch::Theme::Night));
        QVERIFY(!draft.value(QStringLiteral("inLibrary")).toBool());
        QCOMPARE(draft.value(QStringLiteral("colors")).toList().size(), PitchChoices::kTemplateColorCount);
        QVERIFY(!draft.value(QStringLiteral("colors")).toList().at(0).toMap().value(QStringLiteral("own")).toBool());
        QVERIFY(pitch.setDraft(MobilePitch::Name, QStringLiteral("  School  ")));
        QVERIFY(pitch.setDraftColor(0, QColor(QStringLiteral("#123456"))));
        QCOMPARE(pitch.previewLook().background, QColor(QStringLiteral("#123456")));
        QCOMPARE(pitch.sheetLook().background, Pitch::look(*model, Pitch::Theme::Night).background);
        QVERIFY(pitch.setDraft(MobilePitch::Motif, int(Pitch::Motif::Frame) + 1));
        QVERIFY(pitch.setDraft(MobilePitch::LogoSize, 15));
        QVERIFY(!pitch.setDraft(MobilePitch::LogoSize, 12));  // not offered
        QVERIFY(pitch.setDraft(MobilePitch::LogoCorner, int(Pitch::Corner::TopLeft)));
        QVERIFY(pitch.setDraft(MobilePitch::Footer, QStringLiteral("Class 3B")));
        QVERIFY(pitch.setDraft(MobilePitch::Numbers, true));
        QCOMPARE(pitch.draftChoices(MobilePitch::Motif).size(), Pitch::kMotifCount + 1);
        QCOMPARE(pitch.draftChoices(MobilePitch::TitleFont).first().toMap().value(QStringLiteral("value")).toString(), QString());
        // A picture: the logo reduced to 512 px, PNG.
        QVERIFY(pitch.setDraftPicture(2, QUrl::fromLocalFile(picturePath)));
        QImage logo;
        QVERIFY(logo.loadFromData(pitch.draftResult().logo, "PNG"));
        QCOMPARE(std::max(logo.width(), logo.height()), Pitch::kLogoMaxSide);
        QVERIFY(pitch.draft().value(QStringLiteral("pictures")).toList().at(2).toMap().value(QStringLiteral("chosen")).toBool());
        QVERIFY(!pitch.setDraftPicture(1, QUrl::fromLocalFile(source.filePath(QStringLiteral("missing.png")))));
        QVERIFY(!map.lastError().isEmpty());

        // Save: in the library and on the sheet, one Undo step back to Night.
        QVERIFY(pitch.saveDraft());
        QVERIFY(!pitch.editing());
        const QList<Pitch::Template> library = PitchTemplates::all();
        QCOMPARE(library.size(), 1);
        QCOMPARE(library.front().name, QStringLiteral("School"));
        QCOMPARE(library.front().footer, QStringLiteral("Class 3B"));
        QVERIFY(!model->pitchTemplate.isEmpty());
        QCOMPARE(pitch.state().value(QStringLiteral("templateId")).toString(), library.front().id);
        QCOMPARE(pitch.sheetLook().background, QColor(QStringLiteral("#123456")));
        QVERIFY(map.undo());
        QVERIFY(model->pitchTemplate.isEmpty());
        QCOMPARE(model->pitchTheme, Pitch::themeKey(Pitch::Theme::Night));
        // Choose it from the Templates group: one step again.
        const QVariantList cards = pitch.templates();
        QCOMPARE(cards.size(), 1);
        QVERIFY(pitch.chooseTemplate(library.front().id));
        QVERIFY(!model->pitchTemplate.isEmpty());

        // Edit: the sheet's template, in the library (Delete offered); a name left empty keeps the old.
        QVERIFY(pitch.editTemplate());
        QVERIFY(pitch.draft().value(QStringLiteral("inLibrary")).toBool());
        QVERIFY(pitch.setDraft(MobilePitch::Name, QString()));
        QCOMPARE(pitch.draftResult().name, QStringLiteral("School"));
        pitch.resetDraftColors();
        QVERIFY(!pitch.draftResult().background.isValid());
        pitch.cancelDraft();  // Cancel: nothing saved
        QVERIFY(PitchTemplates::all().front().background.isValid());

        // Export: a valid file handed to Share; Import of it into an empty library.
        QVERIFY(pitch.editTemplate());
        QVERIFY(pitch.exportDraft());
        QCOMPARE(system.urls.size(), 1);
        const QString exported = QDir(system.urls.front().toLocalFile()).filePath(QStringLiteral("School.mentis-theme"));
        QVERIFY(PitchTemplates::readFile(exported).has_value());
        // Delete: gone from the library, the map keeps its copy (shown as "this map").
        QVERIFY(pitch.deleteDraft());
        QVERIFY(PitchTemplates::all().isEmpty());
        QVERIFY(!model->pitchTemplate.isEmpty());
        const QVariantList onlyHere = pitch.templates();
        QCOMPARE(onlyHere.size(), 1);
        QVERIFY(onlyHere.front().toMap().value(QStringLiteral("onlyHere")).toBool());
        QVERIFY(!pitch.chooseTemplate(library.front().id));  // the map's own copy stays
        QVERIFY(pitch.importTemplate(QUrl::fromLocalFile(exported)));
        QCOMPARE(PitchTemplates::all().size(), 1);
        // A file that is not a template is refused with the desktop's message.
        QVERIFY(!pitch.importTemplate(QUrl::fromLocalFile(picturePath)));
        QVERIFY(map.lastError().contains(QCoreApplication::translate("PitchPage", "The template could not be imported: %1").section(QLatin1Char(':'), 0, 0)));
        // Another sheet shown: the draft is dropped.
        QVERIFY(pitch.newTemplate());
        emit map.snapshotChanged();
        QVERIFY(!pitch.editing());
        map.close();
    }

    // The page: Templates in Change theme, New template opens the fields in place of the rows,
    // Save closes them and shows Edit template.
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
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    const auto item = [&](const char *name) { return window->findChild<QQuickItem *>(QLatin1String(name)); };
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *root, const QString &name) -> QQuickItem * {
        if (root->objectName() == name) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = visual(child, name)) return found;
        return nullptr;
    };
    window->resize(360, 720);
    QVERIFY(QMetaObject::invokeMethod(item("panelButton"), "clicked"));
    QQuickItem *panel = nullptr;
    QTRY_VERIFY((panel = item("sidePanel")) && panel->isVisible());
    QVERIFY(QMetaObject::invokeMethod(visual(panel, QStringLiteral("pitchSegment")), "clicked"));
    QQuickItem *page = item("pitchPanelPage");
    QTRY_VERIFY(page->isVisible());
    QVERIFY(QMetaObject::invokeMethod(item("pitchThemeButton"), "clicked"));
    auto *themeSheet = window->findChild<QObject *>(QStringLiteral("pitchThemeSheet"));
    QTRY_VERIFY(themeSheet->property("opened").toBool());
    QQuickItem *top = window->contentItem()->parentItem() ? window->contentItem()->parentItem() : window->contentItem();
    const QString id = PitchTemplates::all().front().id;
    QTRY_VERIFY(visual(top, QStringLiteral("pitchTemplateCard-%1").arg(id)));
    QVERIFY(visual(top, QStringLiteral("pitchImportTemplate")));
    QQuickItem *create = visual(top, QStringLiteral("pitchNewTemplateCard"));
    QVERIFY(create);
    QVERIFY(QMetaObject::invokeMethod(create, "clicked"));
    QTRY_VERIFY(!themeSheet->property("opened").toBool());
    QQuickItem *fields = visual(page, QStringLiteral("pitchTemplateFields"));
    QTRY_VERIFY(fields && fields->isVisible());
    QVERIFY(!item("pitchThemeButton")->isVisible());
    QVERIFY(visual(fields, QStringLiteral("pitchTemplateColor8")));
    QQuickItem *deleteButton = visual(fields, QStringLiteral("pitchTemplateDelete"));
    QVERIFY(deleteButton && !deleteButton->isVisible());  // a new template is not in the library
    QVERIFY(QMetaObject::invokeMethod(visual(fields, QStringLiteral("pitchTemplateSave")), "clicked"));
    QTRY_VERIFY(!fields->isVisible());
    QCOMPARE(PitchTemplates::all().size(), 2);
    QTRY_VERIFY(item("pitchEditTemplateButton") && item("pitchEditTemplateButton")->isVisible());
    QVERIFY(QMetaObject::invokeMethod(item("pitchEditTemplateButton"), "clicked"));
    QTRY_VERIFY(fields->isVisible());
    QTRY_VERIFY(deleteButton->isVisible());
    QVERIFY(QMetaObject::invokeMethod(visual(fields, QStringLiteral("pitchTemplateCancel")), "clicked"));
    QTRY_VERIFY(!fields->isVisible());
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).toString()));
}

void MobileTest::exportFromTheHome() {
    QTemporaryDir storage;
    const QString path = QDir(storage.path()).filePath(QStringLiteral("Export.mentis"));
    QCOMPARE(writeSearchMap(path), QString());
    UrlCatcher system;  // Share shows the folder off Android
    QDesktopServices::setUrlHandler(QStringLiteral("file"), &system, "open");
    const auto unsetHandler = qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("file")); });
    {
        MapLibrary library(storage.path(), nullptr);
        MapExport exporter;
        exporter.setLibrary(&library);
        QVERIFY(!exporter.run(MapExport::Word, 0));  // nothing prepared
        QVERIFY(exporter.prepare(QUrl::fromLocalFile(path)));
        QCOMPARE(exporter.sheets().size(), 2);
        // The map is locked while it is exported.
        {
            Document other;
            QVERIFY(!other.open(path, false).ok);
        }
        const auto readAll = [](const QString &file) {
            QFile input(file);
            return input.open(QIODevice::ReadOnly) ? input.readAll() : QByteArray();
        };
        // Every format: the file written in the shared folder and handed to Share.
        const struct { MapExport::Format format; int sheet; const char *suffix; QByteArray magic; } cases[] = {
            {MapExport::Word, 0, "docx", "PK"},           {MapExport::Excel, 0, "xlsx", "PK"},
            {MapExport::PitchPowerPoint, 1, "pptx", "PK"}, {MapExport::PitchPdf, 0, "pdf", "%PDF"},
            {MapExport::Markdown, 0, "zip", "PK"}};
        for (const auto &c : cases) {
            system.urls.clear();
            QVERIFY2(exporter.run(c.format, c.sheet), qPrintable(exporter.lastError()));
            const QFileInfo file(exporter.lastFile());
            QCOMPARE(file.suffix(), QLatin1String(c.suffix));
            QCOMPARE(file.absolutePath(), QDir(MapExport::exportFolder()).absolutePath());
            QVERIFY(readAll(file.filePath()).startsWith(c.magic));
            QCOMPARE(system.urls.size(), 1);
            // One export at a time in the folder.
            QCOMPARE(QDir(MapExport::exportFolder()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).size(), 1);
        }
        // The sheet's name names the file of one sheet, the document's the file of every sheet.
        QVERIFY(exporter.run(MapExport::PitchPowerPoint, 1));
        QVERIFY(QFileInfo(exporter.lastFile()).completeBaseName().endsWith(exporter.sheets().at(1)));
        QVERIFY(exporter.run(MapExport::Word, 1));
        QVERIFY(QFileInfo(exporter.lastFile()).completeBaseName().endsWith(QStringLiteral("Export")));
        // The Markdown zip holds the .md at its root.
        QVERIFY(exporter.run(MapExport::Markdown, 0));
        QVERIFY(readAll(exporter.lastFile()).contains(".md"));
        QVERIFY(!exporter.run(MapExport::Markdown, 5));  // no such sheet

        // C2, Image: a PNG of the sheet drawn by the phone (its titles and lines on the backdrop),
        // at the scale chosen; an SVG with the titles as text; every sheet as one .zip.
        const QVariantMap frame{{QStringLiteral("style"), int(ExportFrame::Style::Solid)}, {QStringLiteral("thickness"), 6},
                                {QStringLiteral("color"), QStringLiteral("#ff0000")}};
        QVERIFY2(exporter.runImage({{QStringLiteral("sheet"), 0}, {QStringLiteral("scale"), 1}}), qPrintable(exporter.lastError()));
        QImage one(exporter.lastFile());
        QVERIFY(!one.isNull());
        QVERIFY(exporter.runImage({{QStringLiteral("sheet"), 0}, {QStringLiteral("scale"), 2}, {QStringLiteral("frame"), frame}}));
        QImage two(exporter.lastFile());
        QVERIFY(two.width() > 2 * one.width());  // twice, plus the frame on the edge
        QCOMPARE(two.pixelColor(2, two.height() / 2), QColor(Qt::red));
        const QColor backdrop = one.pixelColor(one.width() / 2, 2);
        int inked = 0;
        for (int y = 0; y < one.height(); y += 2)
            for (int x = 0; x < one.width(); x += 2) inked += one.pixelColor(x, y) != backdrop;
        QVERIFY2(inked > 200, qPrintable(QString::number(inked)));
        QVERIFY(exporter.runImage({{QStringLiteral("sheet"), 0}, {QStringLiteral("svg"), true}}));
        QCOMPARE(QFileInfo(exporter.lastFile()).suffix(), QStringLiteral("svg"));
        const QByteArray svg = readAll(exporter.lastFile());
        QVERIFY(svg.contains("<path") && svg.contains("Costs"));
        QVERIFY(exporter.runImage({{QStringLiteral("sheet"), -1}}));
        QCOMPARE(QFileInfo(exporter.lastFile()).suffix(), QStringLiteral("zip"));
        QCOMPARE(readAll(exporter.lastFile()).count(".png"), 4);  // two entries, each named twice (local and central)
        // PDF: the map on A4 landscape fitted, framed and monochrome; every sheet; Pitch.
        QVERIFY2(exporter.runPdf({{QStringLiteral("sheet"), 0}, {QStringLiteral("pageSize"), int(MapPages::PageSize::A4)},
                                  {QStringLiteral("landscape"), true}, {QStringLiteral("monochrome"), true}, {QStringLiteral("frame"), frame}}),
                 qPrintable(exporter.lastError()));
        QVERIFY(readAll(exporter.lastFile()).startsWith("%PDF"));
        QVERIFY(exporter.runPdf({{QStringLiteral("sheet"), -1}}));
        QVERIFY(readAll(exporter.lastFile()).startsWith("%PDF"));
        QVERIFY(exporter.runPdf({{QStringLiteral("pitch"), true}, {QStringLiteral("sheet"), 0}}));
        QVERIFY(readAll(exporter.lastFile()).startsWith("%PDF"));
        QVERIFY(!exporter.runPdf({{QStringLiteral("pitch"), true}, {QStringLiteral("sheet"), -1}}));  // one sheet's slides
        exporter.finish();
        QVERIFY(!exporter.prepared());
        Document again;  // the lock is released
        QVERIFY(again.open(path, false).ok);
        again.close();
    }

    // The page: the sheet's dialog from the Home, Pitch then PowerPoint, the sheet, Share.
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QObject *dialog = window->findChild<QObject *>(QStringLiteral("mapExportSheet"));
    QVERIFY(dialog);
    system.urls.clear();
    QVERIFY(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QQuickItem *top = window->contentItem()->parentItem() ? window->contentItem()->parentItem() : window->contentItem();
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *root, const QString &name) -> QQuickItem * {
        if (root->objectName() == name) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = visual(child, name)) return found;
        return nullptr;
    };
    // C2: Image… opens its options over Export; PNG shares and closes both.
    QQuickItem *image = visual(top, QStringLiteral("exportImage"));
    QVERIFY(image && image->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(image, "clicked"));
    QObject *options = window->findChild<QObject *>(QStringLiteral("exportOptionsSheet"));
    QTRY_VERIFY(options && options->property("opened").toBool());
    QVERIFY(dialog->property("opened").toBool());
    QQuickItem *png = nullptr;
    QTRY_VERIFY((png = visual(top, QStringLiteral("exportRun"))));
    QVERIFY(QMetaObject::invokeMethod(png, "clicked"));
    QTRY_VERIFY(!options->property("opened").toBool());
    QTRY_VERIFY(!dialog->property("opened").toBool());
    QCOMPARE(system.urls.size(), 1);
    QVERIFY2(options->property("error").toString().isEmpty(), qPrintable(options->property("error").toString()));
    system.urls.clear();
    QVERIFY(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(visual(top, QStringLiteral("exportPitch")), "clicked"));
    QVERIFY(QMetaObject::invokeMethod(visual(top, QStringLiteral("exportPitchPptx")), "clicked"));
    QCOMPARE(dialog->property("step").toString(), QStringLiteral("sheets"));
    QQuickItem *second = nullptr;
    QTRY_VERIFY((second = visual(top, QStringLiteral("exportSheet1"))));
    QVERIFY(QMetaObject::invokeMethod(second, "clicked"));
    QTRY_VERIFY(!dialog->property("opened").toBool());
    QCOMPARE(system.urls.size(), 1);
    QVERIFY(QFileInfo::exists(path));  // the picked map stays; the library's copy of it is removed
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).toString()));
}

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
        if (button && !button->property("visible").toBool()) return tapTopicTool(qobject_cast<QQuickItem *>(button));  // FEAT-118
        return button && QMetaObject::invokeMethod(button, "clicked");
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
    QVERIFY(click("labelsDialogClose"));
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
    QObject *panel = window->findChild<QObject *>(QStringLiteral("linkDialog"));
    QObject *field = window->findChild<QObject *>(QStringLiteral("findTopicField"));
    QObject *backSwitch = window->findChild<QObject *>(QStringLiteral("alsoBackSwitch"));
    QObject *remove = window->findChild<QObject *>(QStringLiteral("linkRemoveButton"));
    QVERIFY(panel && field && backSwitch && remove);
    const auto click = [&](const char *name) {
        QObject *button = window->findChild<QObject *>(QLatin1String(name));
        if (button && !button->property("visible").toBool()) return tapTopicTool(qobject_cast<QQuickItem *>(button));  // FEAT-118
        return button && QMetaObject::invokeMethod(button, "clicked");
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

    // FEAT-117: the topic bar's Link button opens on Web page (no link yet); the Topic tab offers
    // every topic but the topic itself.
    map->select(startId);
    QVERIFY(click("linkButton"));
    QTRY_VERIFY(panel->property("opened").toBool());
    QVERIFY(!panel->property("topicTab").toBool());
    QVERIFY(QMetaObject::invokeMethod(panel, "showTab", Q_ARG(QVariant, true)));  // the Topic segment's tap
    QVERIFY(panel->property("topicTab").toBool());
    QVERIFY(backSwitch->property("visible").toBool());
    QVERIFY(!remove->property("visible").toBool());
    QVERIFY(!window->findChild<QObject *>(QStringLiteral("webLinkInsertButton"))->property("visible").toBool());
    search(QStringLiteral("e"));
    QVERIFY(!resultIds().contains(startId));
    QVERIFY(resultIds().contains(nearbyId));
    // A target in the same sheet: one link, one Undo step.
    QVERIFY(QMetaObject::invokeMethod(panel, "link", Q_ARG(QVariant, 0), Q_ARG(QVariant, nearbyId)));
    QTRY_VERIFY(!panel->property("visible").toBool());
    MindNode *start = topicTitled(QStringLiteral("Start here"));
    QCOMPARE(start->topicLink, nearbyId);
    QVERIFY(topicTitled(QStringLiteral("Nearby goal"))->topicLink.isEmpty());
    QCOMPARE(map->selectedId(), startId);
    QVERIFY(!map->snapshot()->topicOf(start)->geometry->linkIconRect(true).isEmpty());
    QVERIFY(map->undo());
    QVERIFY(topicTitled(QStringLiteral("Start here"))->topicLink.isEmpty());
    QVERIFY(map->redo());

    // Linked again to a topic in the other sheet, with the back link: one Undo step for both. With
    // only a topic link the dialog opens on Topic.
    map->select(startId);
    QVERIFY(click("linkButton"));
    QTRY_VERIFY(panel->property("opened").toBool());
    QVERIFY(panel->property("topicTab").toBool());
    QVERIFY(remove->property("visible").toBool());
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
    QVERIFY(QMetaObject::invokeMethod(panel, "link", Q_ARG(QVariant, farSheet), Q_ARG(QVariant, farId)));
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
    // Remove on the Topic tab takes it off in one Undo step.
    QVERIFY(click("linkButton"));
    QTRY_VERIFY(panel->property("opened").toBool());
    QVERIFY(panel->property("topicTab").toBool());
    QVERIFY(click("linkRemoveButton"));
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

// FEAT-052 point 8 on the phone: two main topics under one boundary, one dragged to the other side
// of the central topic: each keeps a boundary of its own (user 2026-11-13).
void MobileTest::boundarySplitsAcrossSides() {
    QTemporaryDir storage;
    const QString path = storage.filePath(QStringLiteral("Split.mentis"));
    QCOMPARE(writeTwoSheetMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MindMapModel *model = map.document()->model();
    const MindNode *root = model->root();
    const QString firstId = childTitled(root, QStringLiteral("First"))->id;
    const QString secondId = childTitled(root, QStringLiteral("Second"))->id;
    map.select(firstId);
    map.startMultiSelect();
    map.toggleInSelection(secondId);
    QVERIFY(map.addBoundary());
    QCOMPARE(model->boundaries.size(), 1);
    const MapLayout *layout = map.snapshot()->layout();
    QCOMPARE(layout->sideOf(model->nodeById(secondId)), 1);
    const QRectF centre = map.snapshot()->topicOf(model->root())->bounds;
    const QPointF left(centre.left() - 120, map.snapshot()->topicOf(model->nodeById(secondId))->bounds.center().y());
    const ReorderTarget target = map.moveTarget(secondId, left);
    QCOMPARE(target.kind, ReorderTarget::Kind::Insert);
    QCOMPARE(target.side, -1);
    QVERIFY(map.moveTopic(secondId, left));
    QCOMPARE(map.snapshot()->layout()->sideOf(model->nodeById(secondId)), -1);
    QCOMPARE(model->boundaries.size(), 2);
    QCOMPARE(model->boundaries.at(0).topics, QStringList{firstId});
    QCOMPARE(model->boundaries.at(1).topics, QStringList{secondId});
    QVERIFY(map.undo());  // one step with the move
    QCOMPARE(model->boundaries.size(), 1);
    QCOMPARE(model->boundaries.front().topics, (QStringList{firstId, secondId}));
    QVERIFY(map.close());
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
        return tapTopicTool(button);
    };

    map->select(first->id);
    QTRY_VERIFY(onTopicBar(item("boundaryButton")));
    QCOMPARE(item("boundaryButton")->property("text").toString(), QStringLiteral("Add a boundary around the topic"));
    QVERIFY(click("boundaryButton"));
    QCOMPARE(model->boundaries.size(), 1);
    QCOMPARE(map->selectedBoundaryId(), model->boundaries.front().id);
    // The boundary's bar: Rename and Delete only.
    QTRY_VERIFY(item("renameButton")->isVisible());
    QVERIFY(item("deleteButton")->isVisible() && item("deleteButton")->isEnabled());
    for (const char *name : {"selectMoreButton", "boundaryButton", "relationButton", "noteButton", "copyTopicsButton"})
        QVERIFY2(!onTopicBar(item(name)), name);
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
// FEAT-028: a tree table is drawn as cells, as the desktop's NodeItem::TableCell (the phone drew the
// topics' own shapes, without cells, borders or tints: the user's photo, 2026-11-14).
void MobileTest::treeTableCells() {
    Document document;
    document.startUntitled();
    MindMapModel *model = document.model();
    MindNode *root = model->root();
    QVERIFY(document.setStructure(root, MapStructure::typeKey(MapStructure::Type::TreeTable), QString()));
    MindNode *first = document.addChild(root, QStringLiteral("First"));
    document.addChild(root, QStringLiteral("Second"));
    MindNode *deep = document.addChild(first, QStringLiteral("Deep"));
    const MapSnapshot snapshot(model);
    // MindMapScene's solidFill: the topic's fill, or its branch colour without one.
    const auto solid = [](const MindNode *node) { return node->fill.alpha() > 0 ? node->fill : node->branchColor; };
    const auto cellOf = [&](const MindNode *node) {
        const MapSnapshot::Topic *topic = snapshot.topicOf(node);
        return topic ? topic->cell : std::nullopt;
    };
    for (const MindNode *node : {static_cast<const MindNode *>(root), static_cast<const MindNode *>(first), static_cast<const MindNode *>(deep)}) {
        const auto cell = cellOf(node);
        QVERIFY2(cell, qPrintable(node->title));
        // The layout's cell, in map px; the borders in the header's colour.
        QCOMPARE(cell->rect, snapshot.layout()->tableCells().value(node->id).rect.translated(snapshot.topicOf(node)->position));
        QCOMPARE(cell->border, solid(root));
        QVERIFY(snapshot.bounds().contains(cell->rect));
    }
    // The header and the main rows filled as their topics; below them the main colour 80% to white.
    QCOMPARE(cellOf(root)->fill, solid(root));
    const QColor main = solid(first);
    QCOMPARE(cellOf(first)->fill, main);
    const QColor tint = cellOf(deep)->fill;
    QVERIFY(std::abs(tint.redF() - (main.redF() + (1 - main.redF()) * 0.8)) < 1e-3);
    QVERIFY(std::abs(tint.blueF() - (main.blueF() + (1 - main.blueF()) * 0.8)) < 1e-3);
    // A leaf's cell reaches the table's right edge: a tap there, past the topic, selects it.
    const QRectF deepCell = cellOf(deep)->rect;
    QCOMPARE(deepCell.right(), cellOf(root)->rect.right());
    const QPointF pastTopic(deepCell.right() - 2, deepCell.center().y());
    QVERIFY(!snapshot.topicOf(deep)->bounds.contains(pastTopic));
    QCOMPARE(snapshot.topicAt(pastTopic)->node, deep);
    // A topic outside a table has no cell.
    QVERIFY(document.setStructure(root, MapStructure::typeKey(MapStructure::Type::MindMap), QString()));
    const MapSnapshot plain(model);
    QVERIFY(!plain.topicOf(first)->cell);
}

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
        return tapTopicTool(button);
    };

    map->select(first->id);
    QTRY_VERIFY(onTopicBar(item("summaryButton")));
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
        // The title field is not one letter per line (bug 2026-11-13: it took the plain layout's
        // width, 0 for a formula title): it grows up to the topic's width. Also with $$...$$.
        const NodeGeometry &titleGeometry = *map.snapshot()->topicOf(first)->geometry;
        const qreal fieldLimit = titleGeometry.maxTextNodeWidth() - 2 * titleGeometry.padX();
        QCOMPARE(canvas.editorWidth(), fieldLimit);
        QVERIFY(map.rename(QStringLiteral("$$x+y$$")));
        QVERIFY(map.snapshot()->topicOf(first)->geometry->formula());
        QCOMPARE(canvas.editorWidth(), fieldLimit);
        QVERIFY(map.rename(QStringLiteral("Area $\\pi r^2$")));
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
        return tapTopicTool(button);
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
    QTRY_VERIFY(onTopicBar(item("formulaButton")));
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
    // FEAT-133: the braces are closed while typing; "}" steps over the closing one, Backspace on an
    // empty pair removes both.
    QVERIFY(field->setProperty("text", QString()));
    field->forceActiveFocus();
    for (const char key : {'\\', 's', 'q', 'r', 't', '{'}) QTest::keyClick(window, key);
    QTRY_COMPARE(field->property("text").toString(), QStringLiteral("\\sqrt{}"));
    QCOMPARE(field->property("cursorPosition").toInt(), 6);
    for (const char key : {'x', '}'}) QTest::keyClick(window, key);
    QTRY_COMPARE(field->property("text").toString(), QStringLiteral("\\sqrt{x}"));
    QCOMPARE(field->property("cursorPosition").toInt(), 8);
    QTest::keyClick(window, '{');
    QTRY_COMPARE(field->property("text").toString(), QStringLiteral("\\sqrt{x}{}"));
    QTest::keyClick(window, Qt::Key_Backspace);
    QTRY_COMPARE(field->property("text").toString(), QStringLiteral("\\sqrt{x}"));
    QCOMPARE(field->property("cursorPosition").toInt(), 8);
    // FEAT-132: Symbols and formulas opens the list over the modal; a tap on an entry puts it at the
    // selection (inside the first {}) and closes only the list.
    QVERIFY(field->setProperty("text", QStringLiteral("x^2")));
    QVERIFY(QMetaObject::invokeMethod(field, "selectAll"));
    QVERIFY(click("formulaListButton"));
    QObject *latexList = window->findChild<QObject *>(QStringLiteral("latexListDialog"));
    QVERIFY(latexList);
    QTRY_VERIFY(latexList->property("opened").toBool());
    QQuickItem *search = item("latexListSearch");
    QQuickItem *grid = item("latexListGrid");
    QVERIFY(search && grid);
    QVERIFY(grid->property("count").toInt() > 200);
    QVERIFY(search->setProperty("text", QStringLiteral("zzzz")));
    QTRY_COMPARE(grid->property("count").toInt(), 0);
    QVERIFY(item("latexListEmpty")->isVisible());
    QVERIFY(search->setProperty("text", QStringLiteral("radice quadrata")));  // Italian keywords
    QTRY_COMPARE(grid->property("count").toInt(), 1);
    QTest::qWait(50);
    QTest::mouseClick(window, Qt::LeftButton, {}, grid->mapToScene(QPointF(20, 20)).toPoint());
    QTRY_VERIFY(!latexList->property("visible").toBool());
    QVERIFY(dialog->property("opened").toBool());
    QCOMPARE(field->property("text").toString(), QStringLiteral("\\sqrt{x^2}"));
    QCOMPARE(field->property("cursorPosition").toInt(), 9);
    if (const QString folder = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR"); !folder.isEmpty()) {  // to look at it
        QVERIFY(click("formulaListButton"));
        QTRY_VERIFY(latexList->property("opened").toBool());
        QVERIFY(search->setProperty("text", QString()));
        QTest::qWait(300);
        window->grabWindow().save(QDir(folder).filePath(QStringLiteral("mobile-latex-list.png")));
        QVERIFY(QMetaObject::invokeMethod(latexList, "close"));
        QTRY_VERIFY(!latexList->property("visible").toBool());
    }
    QVERIFY(field->setProperty("text", QStringLiteral("\\frac{a}{b}")));
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
    for (const char *name : {"imageButton", "copyPictureButton", "renameButton", "noteButton"}) QVERIFY2(!onTopicBar(item(name)), name);
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

    // The title field (user's report 2026-11-13): each formula a picture in the line, the one the
    // cursor is in or touches its code; while typed, the topic lays that one out as text.
    QObject *page = window->findChild<QObject *>(QStringLiteral("mapPage"));
    QQuickItem *titleField = item("titleEditor");
    const MindNode *named = model->nodeById(firstId);
    const QChar picture = QChar::ObjectReplacementCharacter;
    map->select(firstId);
    QVERIFY(map->rename(QStringLiteral("Area $x^2$ ok")));
    QVERIFY(QMetaObject::invokeMethod(page, "beginEdit", Q_ARG(QVariant, true)));  // the cursor at the end
    QTRY_VERIFY(titleField->isVisible());
    QCOMPARE(titleField->property("text").toString(), QStringLiteral("Area %1 ok").arg(picture));
    QCOMPARE(named->title, QStringLiteral("Area $x^2$ ok"));
    QVERIFY(titleField->setProperty("cursorPosition", 5));  // before the picture: its code
    QTRY_COMPARE(titleField->property("text").toString(), QStringLiteral("Area $x^2$ ok"));
    QCOMPARE(titleField->property("cursorPosition").toInt(), 5);
    QTRY_COMPARE(named->title, QStringLiteral("Area \\$x^2\\$ ok"));
    QVERIFY(!map->snapshot()->topicOf(named)->geometry->formula());
    QVERIFY(titleField->setProperty("cursorPosition", 10));  // after the closing $: still its code
    QTest::qWait(20);
    QCOMPARE(titleField->property("text").toString(), QStringLiteral("Area $x^2$ ok"));
    QVERIFY(titleField->setProperty("cursorPosition", 0));  // away: a picture again
    QTRY_COMPARE(titleField->property("text").toString(), QStringLiteral("Area %1 ok").arg(picture));
    QCOMPARE(titleField->property("cursorPosition").toInt(), 0);
    QTRY_COMPARE(named->title, QStringLiteral("Area $x^2$ ok"));
    // A formula typed at the end: code while the cursor touches it, a picture after a space.
    QVERIFY(titleField->setProperty("cursorPosition", titleField->property("length").toInt()));
    for (const char key : {' ', '$', 'y', '$'}) QTest::keyClick(window, key);
    QTRY_COMPARE(titleField->property("text").toString(), QStringLiteral("Area %1 ok $y$").arg(picture));
    QTest::keyClick(window, ' ');
    QTRY_COMPARE(titleField->property("text").toString(), QStringLiteral("Area %1 ok %1 ").arg(picture));
    QCOMPARE(titleField->property("cursorPosition").toInt(), titleField->property("length").toInt());
    // The picture is drawn in the field (the scene leaves the title out meanwhile).
    const QImage fieldImage = window->grabWindow().copy(titleField->mapRectToScene(titleField->boundingRect()).toRect());
    int dark = 0;
    for (int y = 0; y < fieldImage.height(); ++y)
        for (int x = 0; x < fieldImage.width(); ++x) dark += qGray(fieldImage.pixel(x, y)) < 100;
    QVERIFY2(dark > 20, qPrintable(QString::number(dark)));
    QVERIFY(QMetaObject::invokeMethod(page, "endEdit"));
    QCOMPARE(named->title, QStringLiteral("Area $x^2$ ok $y$ "));
    QVERIFY(errors.isEmpty());
}

// FEAT-118: on a phone the adding tools that do not fit go under the "+" at the end of the bar; its
// menu lists them in order, a row does what the button does, and the menu closes when the selection
// or the room changes. The multiple selection has its own five; a picture's bar has no "+".
void MobileTest::topicBarOverflowOnThePage() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Overflow.mentis"));
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
    const MindNode *first = childTitled(map->document()->model()->root(), QStringLiteral("First"));
    QVERIFY(first);
    const auto item = [&](const char *name) { return window->findChild<QQuickItem *>(QLatin1String(name)); };
    QQuickItem *bar = item("topicBar");
    QQuickItem *plus = item("addToolsButton");
    QObject *menu = window->findChild<QObject *>(QStringLiteral("addMenu"));
    QVERIFY(bar && plus && menu);
    const auto rows = [&] {
        QList<QQuickItem *> found;
        for (QQuickItem *row : menu->property("rows").value<QQuickItem *>()->childItems())
            if (row->objectName().endsWith(QLatin1String("Row"))) found.append(row);
        return found;
    };
    const auto rowObjects = [&] {
        QStringList names;
        for (QQuickItem *row : rows()) names.append(row->objectName());
        return names;
    };
    const auto rowOf = [&](const QString &name) -> QQuickItem * {
        for (QQuickItem *row : rows()) if (row->objectName() == name + QLatin1String("Row")) return row;
        return nullptr;
    };
    const auto toolOf = [&](QQuickItem *row) { return row->property("modelData").value<QObject *>(); };
    const auto openMenu = [&] {
        return QMetaObject::invokeMethod(plus, "clicked") && QTest::qWaitFor([&] { return menu->property("opened").toBool(); });
    };

    // One topic on the 412 px page: the fixed part, then the "+"; its menu holds the eleven in order,
    // 4 px above the "+" and inside the page.
    map->select(first->id);
    QTRY_VERIFY(plus->isVisible());
    QCOMPARE(bar->height(), 48.0);
    QCOMPARE(plus->property("text").toString(), QStringLiteral("Add to the topic"));
    QVERIFY(openMenu());
    QCOMPARE(rowObjects(), (QStringList{QStringLiteral("relationButtonRow"), QStringLiteral("boundaryButtonRow"), QStringLiteral("summaryButtonRow"),
                                        QStringLiteral("linkButtonRow"), QStringLiteral("imageButtonRow"), QStringLiteral("formulaButtonRow"),
                                        QStringLiteral("noteButtonRow"), QStringLiteral("labelsButtonRow"), QStringLiteral("attachFilesButtonRow"),
                                        QStringLiteral("checkboxButtonRow"), QStringLiteral("topicStickerButtonRow")}));
    QCOMPARE(toolOf(rowOf(QStringLiteral("linkButton")))->property("menuText").toString(), QStringLiteral("Link…"));
    {
        auto *menuParent = qobject_cast<QQuickItem *>(menu->parent());
        QVERIFY(menuParent);
        const QRectF menuRect(menu->property("x").toReal(), menu->property("y").toReal(), menu->property("width").toReal(),
                              menu->property("height").toReal());
        const QPointF plusTop = plus->mapToItem(menuParent, QPointF(0, (48 - 32) / 2.0));
        QCOMPARE(menuRect.bottom(), plusTop.y() - 4);
        QVERIFY(menuRect.left() >= 16 && menuRect.right() <= menuParent->width() - 16);
        QVERIFY(menuRect.top() >= 4);
    }
    // A row closes the menu, then does what the button does: Note opens the note modal.
    QVERIFY(QMetaObject::invokeMethod(rowOf(QStringLiteral("noteButton")), "clicked"));
    QVERIFY(!menu->property("visible").toBool());
    QObject *noteDialog = window->findChild<QObject *>(QStringLiteral("noteDialog"));
    QVERIFY(noteDialog);
    QTRY_VERIFY(noteDialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(noteDialog, "close"));
    QTRY_VERIFY(!noteDialog->property("visible").toBool());
    // Relationship armed from the menu: its row and the "+" are checked.
    QVERIFY(openMenu());
    QVERIFY(QMetaObject::invokeMethod(rowOf(QStringLiteral("relationButton")), "clicked"));
    QVERIFY(map->relationTool());
    QVERIFY(plus->property("checked").toBool());
    QVERIFY(openMenu());
    QVERIFY(toolOf(rowOf(QStringLiteral("relationButton")))->property("checked").toBool());
    QVERIFY(QMetaObject::invokeMethod(menu, "close"));
    map->stopRelation();
    QVERIFY(!plus->property("checked").toBool());
    // A change of the selection closes it; so does a change of the room (a rotation).
    QVERIFY(openMenu());
    map->select(map->document()->model()->root()->id);
    QTRY_VERIFY(!menu->property("visible").toBool());
    QVERIFY(openMenu());
    const QVariant pageWidth = bar->property("maximumWidth");
    QVERIFY(bar->setProperty("maximumWidth", 800));
    QTRY_VERIFY(!menu->property("visible").toBool());
    QVERIFY(!plus->isVisible());  // everything fits
    QVERIFY(bar->setProperty("maximumWidth", pageWidth));
    QTRY_VERIFY(plus->isVisible());
    // With the menu open a tap on the "+" only closes it: the modal menu takes the press outside,
    // so the "+" does not open it again. A click without a press (TalkBack) also closes it.
    {
        const QPoint at = plus->mapToScene(QPointF(plus->width() / 2, plus->height() / 2)).toPoint();
        QVERIFY(openMenu());
        QTest::mouseClick(window, Qt::LeftButton, {}, at);
        QTRY_VERIFY(!menu->property("visible").toBool());
        QTest::qWait(50);
        QVERIFY(!menu->property("visible").toBool());
        // A tap outside on the map closes the menu and leaves the selection.
        const QString selected = map->selectedId();
        QVERIFY(openMenu());
        QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(window->width() / 2, 120));
        QTRY_VERIFY(!menu->property("visible").toBool());
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);  // a tap reaches the map after the double tap time
        QCOMPARE(map->selectedId(), selected);
        QVERIFY(openMenu());
        QVERIFY(QMetaObject::invokeMethod(plus, "clicked"));
        QVERIFY(!menu->property("visible").toBool());
        QVERIFY(openMenu());
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
    }

    // The multiple selection on a narrow page: Paste style in the fixed part, the "+" lists its five,
    // disabled while the selection is empty.
    QVERIFY(bar->setProperty("maximumWidth", 300));
    map->select(QString());
    map->startMultiSelect();
    QTRY_VERIFY(plus->isVisible());
    QVERIFY(item("pasteStyleButton")->isVisible());
    QCOMPARE(item("pasteStyleButton")->x(), item("pasteTopicsButton")->x() + 34);
    QCOMPARE(plus->x(), item("pasteStyleButton")->x() + 34);
    QVERIFY(openMenu());
    QCOMPARE(rowObjects(), (QStringList{QStringLiteral("boundaryButtonRow"), QStringLiteral("summaryButtonRow"), QStringLiteral("imageButtonRow"),
                                        QStringLiteral("checkboxButtonRow"), QStringLiteral("topicStickerButtonRow")}));
    for (QQuickItem *row : rows()) QVERIFY2(!row->isEnabled(), qPrintable(row->objectName()));
    QVERIFY(QMetaObject::invokeMethod(menu, "close"));
    map->stopMultiSelect();
    QVERIFY(bar->setProperty("maximumWidth", pageWidth));

    // A picked picture: its own bar, no "+".
    map->select(first->id);
    const QString sticker = StickerLibrary().collections().constFirst().toMap().value(QStringLiteral("items")).toList()
                                .constFirst().toMap().value(QStringLiteral("path")).toString();
    QVERIFY(map->insertSticker(sticker));
    const MapSnapshot::Topic *topic = map->snapshot()->topicOf(first);
    map->selectAt(topic->geometry->imageRect().translated(topic->position).center());
    QVERIFY(map->pictureSelected());
    QTRY_VERIFY(item("copyPictureButton")->isVisible());
    QVERIFY(!plus->isVisible());
    QVERIFY(item("imageButton")->isVisible());
    QVERIFY2(errors.isEmpty(), qPrintable(errors.isEmpty() ? QString() : errors.constFirst().toString()));
}

namespace {
// FEAT-121: a central topic with a branch of two, a topic and a floating topic.
QString writeOutlineMap(const QString &path) {
    Document document;
    document.startUntitled();
    MindNode *root = document.model()->root();
    MindNode *plan = document.addChild(root, QStringLiteral("Plan"));
    document.addChild(plan, QStringLiteral("Budget"));
    document.addChild(plan, QStringLiteral("Team"));
    document.addChild(root, QStringLiteral("Costs"));
    document.addFloatingTopic(QPointF(600, 400), QStringLiteral("Loose"));
    { const MapSnapshot laidOut(document.model()); }
    const Document::Result saved = document.save(path, BmapStorage::SaveReason::Explicit);
    document.close();
    return saved.ok ? QString() : saved.error;
}

QStringList outlineTitles(const MobileOutline &outline) {
    QStringList titles;
    for (int row = 0; row < outline.rowCount(); ++row) {
        const QModelIndex index = outline.index(row);
        titles.append(QString(outline.data(index, MobileOutline::DepthRole).toInt(), QLatin1Char('-')) +
                      outline.data(index, MobileOutline::TitleRole).toString());
    }
    return titles;
}

bool outlineFlag(const MobileOutline &outline, const QString &id, MobileOutline::Role role) {
    const int row = outline.rowOf(id);
    return row >= 0 && outline.data(outline.index(row), role).toBool();
}
}  // namespace

void MobileTest::outlinerOnThePhone() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Outline.mentis"));
    QCOMPARE(writeOutlineMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MobileOutline outline;
    outline.setMap(&map);
    MindMapModel *model = map.document()->model();
    const auto id = [&](const char *title) {
        std::function<QString(const MindNode *)> find = [&](const MindNode *node) -> QString {
            if (node->title == QLatin1String(title)) return node->id;
            for (const auto &child : node->children)
                if (const QString found = find(child.get()); !found.isEmpty()) return found;
            return {};
        };
        return find(map.document()->model()->root());
    };

    // The rows in tree order under the central topic, the floating topic last after its line.
    QCOMPARE(outline.rootTitle(), model->root()->title);
    QCOMPARE(outlineTitles(outline), (QStringList{"Plan", "-Budget", "-Team", "Costs", "Loose"}));
    QVERIFY(outlineFlag(outline, id("Loose"), MobileOutline::FloatingStartRole));
    QVERIFY(!outlineFlag(outline, id("Costs"), MobileOutline::FloatingStartRole));
    QVERIFY(!outlineFlag(outline, id("Plan"), MobileOutline::CanIndentRole));
    QVERIFY(outlineFlag(outline, id("Costs"), MobileOutline::CanIndentRole));
    QVERIFY(outlineFlag(outline, id("Budget"), MobileOutline::CanOutdentRole));
    QVERIFY(!outlineFlag(outline, id("Plan"), MobileOutline::CanOutdentRole));
    QVERIFY(!outlineFlag(outline, id("Budget"), MobileOutline::CanMoveUpRole));
    QVERIFY(outlineFlag(outline, id("Budget"), MobileOutline::CanMoveDownRole));
    QVERIFY(!outlineFlag(outline, id("Costs"), MobileOutline::CanMoveDownRole));  // the floating topic stays last

    // Folding is the outline's: the map's branch stays open, no step; the badge counts the hidden.
    const int stepsBefore = map.canUndo();
    outline.toggleFolded(id("Plan"));
    QCOMPARE(outlineTitles(outline), (QStringList{"Plan", "Costs", "Loose"}));
    QCOMPARE(outline.data(outline.index(0), MobileOutline::HiddenCountRole).toInt(), 2);
    QVERIFY(!model->nodeById(id("Plan"))->collapsed);
    QCOMPARE(int(map.canUndo()), stepsBefore);
    // Selecting a hidden topic opens the branch over it (the desktop's expand).
    map.select(id("Team"));
    QCOMPARE(outlineTitles(outline), (QStringList{"Plan", "-Budget", "-Team", "Costs", "Loose"}));

    // Indent, Outdent, Move up and Duplicate: one step each, the topic still selected.
    map.select(id("Costs"));
    QVERIFY(map.indentSelected());
    QCOMPARE(outlineTitles(outline), (QStringList{"Plan", "-Budget", "-Team", "-Costs", "Loose"}));
    QCOMPARE(map.selectedId(), id("Costs"));
    QVERIFY(map.undo());
    QCOMPARE(outlineTitles(outline), (QStringList{"Plan", "-Budget", "-Team", "Costs", "Loose"}));
    map.select(id("Team"));
    QVERIFY(map.moveInOutline(-1));
    QCOMPARE(outlineTitles(outline), (QStringList{"Plan", "-Team", "-Budget", "Costs", "Loose"}));
    QVERIFY(!map.moveInOutline(-1));  // first of its level
    QVERIFY(map.outdentSelected());
    QCOMPARE(outlineTitles(outline), (QStringList{"Plan", "-Budget", "Team", "Costs", "Loose"}));
    QVERIFY(map.undo());
    QVERIFY(map.undo());
    QCOMPARE(outlineTitles(outline), (QStringList{"Plan", "-Budget", "-Team", "Costs", "Loose"}));
    map.select(id("Costs"));
    QVERIFY(!map.moveInOutline(1));
    QVERIFY(map.duplicateSelected());
    QCOMPARE(outlineTitles(outline), (QStringList{"Plan", "-Budget", "-Team", "Costs", "Costs", "Loose"}));
    QVERIFY(map.selectedId() != id("Costs"));
    QVERIFY(map.undo());

    // A2, the desktop's drop: inside (the last child), before, after; one step each, the topic
    // selected; never into its own branch, nor the central topic, nor where it already is.
    const QStringList tree{"Plan", "-Budget", "-Team", "Costs", "Loose"};
    QVERIFY(map.dropInOutline(id("Costs"), id("Plan"), 0));
    QCOMPARE(outlineTitles(outline), (QStringList{"Plan", "-Budget", "-Team", "-Costs", "Loose"}));
    QCOMPARE(map.selectedId(), id("Costs"));
    QVERIFY(map.undo());
    QCOMPARE(outlineTitles(outline), tree);
    QVERIFY(map.dropInOutline(id("Costs"), id("Plan"), -1));
    QCOMPARE(outlineTitles(outline), (QStringList{"Costs", "Plan", "-Budget", "-Team", "Loose"}));
    QVERIFY(map.undo());
    QVERIFY(map.dropInOutline(id("Budget"), id("Team"), 1));
    QCOMPARE(outlineTitles(outline), (QStringList{"Plan", "-Team", "-Budget", "Costs", "Loose"}));
    QVERIFY(map.undo());
    QCOMPARE(outlineTitles(outline), tree);
    QVERIFY(!map.dropInOutline(id("Plan"), id("Budget"), 0));
    QVERIFY(!map.dropInOutline(id("Team"), id("Budget"), 1));
    QVERIFY(!map.dropInOutline(model->root()->id, id("Costs"), 0));
    QCOMPARE(outlineTitles(outline), tree);

    // A title typed in a row's field with a bold word: the desktop's formats in titleHtml, the
    // map's other properties kept; the row draws it formatted.
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nTextEdit { textFormat: TextEdit.RichText }", QUrl());
    std::unique_ptr<QObject> edit(component.create());
    QVERIFY2(edit, qPrintable(component.errorString()));
    auto *field = edit->property("textDocument").value<QQuickTextDocument *>();
    QVERIFY(field);
    const QString budget = id("Budget");
    map.select(budget);
    outline.loadTitle(field, budget);
    QCOMPARE(field->textDocument()->toPlainText(), QStringLiteral("Budget"));
    QVERIFY(!outline.applyTitle(field, budget, QString(), 0));  // not edited
    map.setEditingTitle(true);
    QTextCursor cursor(field->textDocument());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral(" 2027"));
    outline.applyFormat(field, budget, 7, 11, OutlineFormats::kInlineBold, true);
    QCOMPARE(outline.formatState(field, budget, 7, 11) & OutlineFormats::kInlineBold, int(OutlineFormats::kInlineBold));
    QVERIFY(outline.applyTitle(field, budget, QStringLiteral("!"), 11));  // the word being composed too
    map.setEditingTitle(false);
    const MindNode *typed = model->nodeById(budget);
    QCOMPARE(typed->title, QStringLiteral("Budget 2027!"));
    QVERIFY(!typed->titleHtml.isEmpty());
    QVERIFY(!outline.data(outline.index(outline.rowOf(budget)), MobileOutline::HtmlRole).toString().isEmpty());
    cursor.insertText(QStringLiteral("!"));  // the keyboard commits the word composed
    QVERIFY(!outline.applyTitle(field, budget, QString(), 0));  // already the title
    QVERIFY(!field->textDocument()->isModified());  // a queued call writes nothing again
    // The review's P1: the bold typed at a second level stays bold after an Outdent makes the row
    // a first level one (bold there is Black), also when the typing goes on in the same field.
    const auto boldChars = [&](const MindNode *node) {
        int count = 0;
        for (const QTextLayout::FormatRange &range : OutlineFormats::titleInlineRanges(node->title, node->titleHtml,
                 OutlineFormats::storedBoldWeights(node).bold))
            if (range.format.fontWeight() >= QFont::Bold) count += range.length;
        return count;
    };
    const int bold = boldChars(model->nodeById(budget));
    QCOMPARE(bold, 5);  // "2027!": the composed word continues the bold before it
    QVERIFY(map.outdentSelected());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral("?"));
    map.setEditingTitle(true);
    QVERIFY(outline.applyTitle(field, budget, QString(), 0));
    map.setEditingTitle(false);
    QCOMPARE(model->nodeById(budget)->title, QStringLiteral("Budget 2027!?"));
    QCOMPARE(boldChars(model->nodeById(budget)), bold + 1);  // nothing lost, the "?" continues it
    QVERIFY(map.undo());
    QVERIFY(map.undo());
    QVERIFY(map.undo());  // the typing is one step
    QCOMPARE(model->nodeById(budget)->title, QStringLiteral("Budget"));
    // The central topic's title is not left empty (the desktop keeps it).
    map.select(outline.rootId());
    outline.loadTitle(field, outline.rootId());
    field->textDocument()->setPlainText(QString());
    QVERIFY(!outline.applyTitle(field, outline.rootId(), QString(), 0));

    // A2, FEAT-055 in the main title: Markdown of two topics or more is the whole sheet, one step;
    // one topic is pasted as text.
    const QString rootTitle = model->root()->title;
    QGuiApplication::clipboard()->setText(QStringLiteral("one line"));
    QVERIFY(!map.pasteAsMap());
    QGuiApplication::clipboard()->setText(QStringLiteral("- Alpha\n  - Beta\n- Gamma"));
    QVERIFY(map.pasteAsMap());
    QCOMPARE(model->root()->title, QStringLiteral("Alpha"));
    QCOMPARE(outline.rootTitle(), QStringLiteral("Alpha"));
    QCOMPARE(outlineTitles(outline), (QStringList{"Beta", "Gamma"}));
    QCOMPARE(map.selectedId(), model->root()->id);
    QVERIFY(map.undo());
    QCOMPARE(model->root()->title, rootTitle);
    QCOMPARE(outlineTitles(outline), tree);

    // D: an entered branch is the outline, depths from it, the way back from the central topic; no
    // step. Its children do not outdent out of it; the weights stay the tree's.
    const int stepsAtFocus = map.canUndo();
    QVERIFY(!outline.focused());
    QVERIFY(outline.crumbs().isEmpty());
    QCOMPARE(outline.headId(), outline.rootId());
    outline.focusOn(id("Plan"));
    QVERIFY(outline.focused());
    QCOMPARE(outline.headId(), id("Plan"));
    QVERIFY(outline.headHeavy());
    QCOMPARE(outlineTitles(outline), (QStringList{"Budget", "Team"}));
    QCOMPARE(outline.crumbs().size(), 1);
    QCOMPARE(outline.crumbs().constFirst().toMap().value(QStringLiteral("id")).toString(), outline.rootId());
    QVERIFY(!outlineFlag(outline, id("Budget"), MobileOutline::CanOutdentRole));
    QVERIFY(!outlineFlag(outline, id("Budget"), MobileOutline::HeavyRole));
    QCOMPARE(map.canUndo(), stepsAtFocus);
    // Deeper: Budget (under the first level) has the normal weights; up a level, then none.
    map.select(id("Budget"));
    QVERIFY(map.addChild());
    const QString item = map.selectedId();
    outline.focusOn(id("Budget"));
    QVERIFY(!outline.headHeavy());
    QCOMPARE(outline.crumbs().size(), 2);
    QCOMPARE(outline.rowOf(item), 0);
    QVERIFY(outline.focusUp());
    QCOMPARE(outline.headId(), id("Plan"));
    QVERIFY(outline.focusUp());
    QVERIFY(!outline.focused());
    QVERIFY(!outline.focusUp());
    // A selection outside the branch: the nearest topic holding it (here the whole sheet).
    outline.focusOn(id("Budget"));
    map.select(id("Team"));
    QCOMPARE(outline.headId(), id("Plan"));
    map.select(id("Costs"));
    QVERIFY(!outline.focused());
    // The entered topic removed: its nearest ancestor; Undo does not enter it again.
    outline.focusOn(id("Budget"));
    map.select(id("Budget"));
    QVERIFY(map.removeSelected());
    QCOMPARE(outline.headId(), id("Plan"));
    QVERIFY(map.undo());
    QCOMPARE(outline.headId(), id("Plan"));
    QVERIFY(map.undo());  // the added row
    // The central topic: the whole sheet.
    outline.focusOn(outline.rootId());
    QVERIFY(!outline.focused());
    QCOMPARE(outlineTitles(outline), tree);

    // D: a row's contents under its title while compact, asked from its picture's icon; no step.
    QVERIFY(!outlineFlag(outline, id("Costs"), MobileOutline::ExpandedRole));
    outline.toggleContents(id("Costs"));
    QVERIFY(outlineFlag(outline, id("Costs"), MobileOutline::ExpandedRole));
    outline.toggleContents(id("Costs"));
    QVERIFY(!outlineFlag(outline, id("Costs"), MobileOutline::ExpandedRole));
    map.close();
}

void MobileTest::outlinerOnThePage() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Outline.mentis"));
    QCOMPARE(writeOutlineMap(path), QString());
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
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    const auto item = [&](const QString &name) { return window->findChild<QQuickItem *>(name); };
    // ListView delegates have no QObject parent: found through the visual tree.
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *root, const QString &name) -> QQuickItem * {
        if (root->objectName() == name) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = visual(child, name)) return found;
        return nullptr;
    };
    const auto rowItem = [&](const QString &id) { return visual(window->contentItem(), QStringLiteral("outlineRow_") + id); };
    MindMapModel *model = map->document()->model();
    const MindNode *plan = childTitled(model->root(), QStringLiteral("Plan"));
    QVERIFY(plan);
    const QString planId = plan->id;

    // The map menu's Outliner shows the outline in place of the map; its row then reads Mind map.
    // The side panel closes and its button goes (user's request 2026-11-17).
    QQuickItem *outliner = item(QStringLiteral("outliner"));
    QQuickItem *toggle = item(QStringLiteral("outlinerButton"));
    QQuickItem *panelButton = item(QStringLiteral("panelButton"));
    QQuickItem *mapPage = item(QStringLiteral("mapPage"));
    QVERIFY(outliner && toggle && panelButton && mapPage);
    QVERIFY(!outliner->isVisible());
    QVERIFY(toggle->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(panelButton, "clicked"));
    QVERIFY(mapPage->property("panelOpen").toBool());
    QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
    QTRY_VERIFY(outliner->isVisible());
    QVERIFY(!canvas->isVisible());
    QVERIFY(!mapPage->property("panelOpen").toBool());
    QVERIFY(!panelButton->isVisible());
    QCOMPARE(toggle->property("text").toString(), QStringLiteral("Mind map"));
    QQuickItem *planRow = nullptr;
    QTRY_VERIFY((planRow = rowItem(planId)));

    // A first tap on the title selects the row (its ⋮ shown), a second one types in it; the
    // keyboard bar shows; Enter confirms and closes.
    QQuickItem *keyboardBar = item(QStringLiteral("outlineKeyboardBar"));
    QVERIFY(keyboardBar && !keyboardBar->isVisible());
    QQuickItem *field = planRow->property("field").value<QQuickItem *>();
    QVERIFY(field);
    const QPointF onTitle = field->mapToScene(QPointF(4, field->height() / 2));
    map->select(QString());
    QTest::mouseClick(window, Qt::LeftButton, {}, onTitle.toPoint());
    QTRY_COMPARE(map->selectedId(), planId);
    QCOMPARE(outliner->property("editingId").toString(), QString());
    QVERIFY(!keyboardBar->isVisible());
    QTest::mouseClick(window, Qt::LeftButton, {}, onTitle.toPoint(), 400);
    QTRY_COMPARE(outliner->property("editingId").toString(), planId);
    QCOMPARE(map->selectedId(), planId);
    QVERIFY(keyboardBar->isVisible());
    QVERIFY(field->hasActiveFocus());
    QTest::keyClick(window, Qt::Key_End);
    QTest::keyClick(window, ' ');
    QTest::keyClick(window, 'A');
    QTRY_COMPARE(model->nodeById(planId)->title, QStringLiteral("Plan A"));
    QVERIFY(!item(QStringLiteral("outlineIndentButton"))->isEnabled());  // the first row
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(outliner->property("editingId").toString(), QString());
    QVERIFY(!keyboardBar->isVisible());
    QVERIFY(map->undo());
    QCOMPARE(model->nodeById(planId)->title, QStringLiteral("Plan"));

    // FEAT-116: a formula in a title is its code while the cursor touches it, a picture otherwise;
    // the title is saved as plain text, its code.
    QTest::mouseClick(window, Qt::LeftButton, {}, onTitle.toPoint());
    QTRY_COMPARE(outliner->property("editingId").toString(), planId);
    QTest::keyClick(window, Qt::Key_End);
    for (const char c : QByteArrayLiteral(" $x$")) QTest::keyClick(window, c);
    QTRY_COMPARE(model->nodeById(planId)->title, QStringLiteral("Plan $x$"));
    QVERIFY(model->nodeById(planId)->titleHtml.isEmpty());
    const QTextDocument *planText = field->property("textDocument").value<QQuickTextDocument *>()->textDocument();
    QVERIFY(!planText->toPlainText().contains(QChar::ObjectReplacementCharacter));  // touched: its code
    QTest::keyClick(window, Qt::Key_Home);
    QTRY_VERIFY(planText->toPlainText().contains(QChar::ObjectReplacementCharacter));  // away: its picture
    QCOMPARE(model->nodeById(planId)->title, QStringLiteral("Plan $x$"));
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(outliner->property("editingId").toString(), QString());
    QTRY_VERIFY(planText->toPlainText().contains(QChar::ObjectReplacementCharacter));  // the row shows it
    QVERIFY(map->undo());
    QCOMPARE(model->nodeById(planId)->title, QStringLiteral("Plan"));
    QTRY_COMPARE(planText->toPlainText(), QStringLiteral("Plan"));

    // A hardware keyboard with the map's keys (user 2026-11-17). Space types at the end of the
    // selected row; Enter confirms, a second one adds the row below in editing, Tab a child; F2
    // types with the title selected; the arrows walk the rows; Ctrl+/ folds; Alt+Down moves.
    auto *outline = outliner->property("outlineModel").value<MobileOutline *>();
    QVERIFY(outline);
    const auto editingId = [&] { return outliner->property("editingId").toString(); };
    map->select(planId);
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(editingId(), planId);
    QCOMPARE(field->property("cursorPosition").toInt(), 4);
    QCOMPARE(field->property("selectedText").toString(), QString());
    QTest::keyClick(window, 'X');
    QTRY_COMPARE(model->nodeById(planId)->title, QStringLiteral("PlanX"));
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(editingId(), QString());
    QCOMPARE(map->selectedId(), planId);
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_VERIFY(!editingId().isEmpty() && editingId() != planId);
    const QString below = editingId();
    QCOMPARE(model->nodeById(below)->parent, model->root());
    QCOMPARE(outline->rowOf(below), 3);  // after Plan's branch
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(editingId(), QString());
    // Tab indents the row under the one before, Shift+Tab outdents (user 2026-11-18).
    const auto &rootRows = model->root()->children;
    const auto belowRow = std::find_if(rootRows.begin(), rootRows.end(), [&](const auto &row) { return row->id == below; });
    QVERIFY(belowRow != rootRows.begin() && belowRow != rootRows.end());
    const QString previous = (*(belowRow - 1))->id;
    QTest::keyClick(window, Qt::Key_Tab);
    QCOMPARE(model->nodeById(below)->parent->id, previous);
    QCOMPARE(model->nodeById(previous)->children.back()->id, below);
    QCOMPARE(editingId(), QString());
    QTest::keyClick(window, Qt::Key_Backtab, Qt::ShiftModifier);
    QCOMPARE(model->nodeById(below)->parent, model->root());
    // The same keys while the title is typed: one level, the typing goes on, no tab typed.
    map->select(below);
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(editingId(), below);
    const QString typedTitle = model->nodeById(below)->title;
    QTest::keyClick(window, Qt::Key_Tab);
    QTRY_COMPARE(model->nodeById(below)->parent->id, previous);
    QTRY_COMPARE(editingId(), below);
    QTest::keyClick(window, Qt::Key_Backtab, Qt::ShiftModifier);
    QTRY_COMPARE(model->nodeById(below)->parent, model->root());
    QTRY_COMPARE(editingId(), below);
    QCOMPARE(model->nodeById(below)->title, typedTitle);
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(editingId(), QString());
    QVERIFY(map->undo());
    QVERIFY(map->undo());
    map->select(model->root()->children.front()->id);
    QTest::keyClick(window, Qt::Key_Tab);  // the first row: nothing
    QCOMPARE(model->nodeById(map->selectedId())->parent, model->root());
    map->select(below);
    QVERIFY(map->undo());
    QVERIFY(map->undo());
    QVERIFY(map->undo());
    QVERIFY(map->undo());
    QTRY_COMPARE(model->nodeById(planId)->title, QStringLiteral("Plan"));
    QVERIFY(!model->nodeById(below));
    map->select(childTitled(model->root(), QStringLiteral("Costs"))->id);
    QTest::keyClick(window, Qt::Key_F2);
    QTRY_COMPARE(editingId(), map->selectedId());
    QCOMPARE(outliner->property("editField").value<QQuickItem *>()->property("selectedText").toString(), QStringLiteral("Costs"));
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(editingId(), QString());
    // FEAT-002 (user 2026-11-22): a printable key on the selected row replaces its whole title, written
    // as it is typed; Enter keeps it and the next key types again.
    {
        const QString costs = map->selectedId();
        QTest::keyClick(window, 'W');
        QTRY_COMPARE(editingId(), costs);
        QTest::keyClick(window, '2');
        QTRY_COMPARE(model->nodeById(costs)->title, QStringLiteral("W2"));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(editingId(), QString());
        QTest::keyClick(window, 'k');
        QTRY_COMPARE(editingId(), costs);
        QTRY_COMPARE(model->nodeById(costs)->title, QStringLiteral("k"));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(editingId(), QString());
        QVERIFY(map->undo());
        QVERIFY(map->undo());
        QTRY_COMPARE(model->nodeById(costs)->title, QStringLiteral("Costs"));
    }
    const QString budgetId = childTitled(model->nodeById(planId), QStringLiteral("Budget"))->id;
    map->select(planId);
    QTest::keyClick(window, Qt::Key_Down);
    QCOMPARE(map->selectedId(), budgetId);
    QTest::keyClick(window, Qt::Key_Left);
    QCOMPARE(map->selectedId(), planId);
    QTest::keyClick(window, Qt::Key_Up);
    QCOMPARE(map->selectedId(), model->root()->id);
    QTest::keyClick(window, Qt::Key_Down);
    QCOMPARE(map->selectedId(), planId);
    QTest::keyClick(window, Qt::Key_Right);
    QCOMPARE(map->selectedId(), budgetId);
    map->select(planId);
    QTest::keyClick(window, Qt::Key_Slash, Qt::ControlModifier);
    QTRY_VERIFY(outlineFlag(*outline, planId, MobileOutline::FoldedRole));
    QTest::keyClick(window, Qt::Key_Right);  // opens it and goes in
    QTRY_VERIFY(!outlineFlag(*outline, planId, MobileOutline::FoldedRole));
    QCOMPARE(map->selectedId(), budgetId);
    QVERIFY(!model->nodeById(planId)->collapsed);  // the outline's folding only
    map->select(planId);
    QTest::keyClick(window, Qt::Key_Down, Qt::AltModifier);
    QTRY_COMPARE(outline->rowOf(planId), 1);
    QVERIFY(map->undo());
    QTRY_COMPARE(outline->rowOf(planId), 0);
    QTRY_VERIFY((planRow = rowItem(planId)));  // the rows were made again

    // A tap on the branch's triangle folds it in the outline only (D: below 600 dp left of the bullet).
    QCOMPARE(planRow->property("rowLeft").toReal(), 16 + 12.0);
    const QPointF onTriangle = planRow->mapToScene(QPointF(planRow->property("rowLeft").toReal() - 10, planRow->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, {}, onTriangle.toPoint());
    QQuickItem *list = item(QStringLiteral("outlinerList"));
    QTRY_COMPARE(list->property("count").toInt(), 3);
    QVERIFY(!model->nodeById(planId)->collapsed);
    QTest::mouseClick(window, Qt::LeftButton, {}, onTriangle.toPoint());
    QTRY_COMPARE(list->property("count").toInt(), 5);

    // Indent from the keyboard bar on Costs: one step, the typing goes on in the same row.
    const QString costsId = childTitled(model->root(), QStringLiteral("Costs"))->id;
    QQuickItem *costsRow = nullptr;
    QTRY_VERIFY((costsRow = rowItem(costsId)));
    QQuickItem *costsField = costsRow->property("field").value<QQuickItem *>();
    QTest::mouseClick(window, Qt::LeftButton, {}, costsField->mapToScene(QPointF(4, costsField->height() / 2)).toPoint());
    QTRY_COMPARE(map->selectedId(), costsId);
    QTest::mouseClick(window, Qt::LeftButton, {}, costsField->mapToScene(QPointF(4, costsField->height() / 2)).toPoint(), 400);
    QTRY_COMPARE(outliner->property("editingId").toString(), costsId);
    QQuickItem *indent = item(QStringLiteral("outlineIndentButton"));
    QTRY_VERIFY(indent->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(indent, "clicked"));
    QTRY_COMPARE(model->nodeById(costsId)->parent->id, planId);
    QTRY_COMPARE(outliner->property("editingId").toString(), costsId);
    QVERIFY(QMetaObject::invokeMethod(outliner, "endEdit", Q_ARG(QVariant, QVariant())));
    QVERIFY(map->undo());
    QTRY_COMPARE(model->nodeById(costsId)->parent, model->root());

    // Move up from the bar changes the rows' order (a reset): the typing goes on in Costs.
    QTRY_VERIFY((costsRow = rowItem(costsId)));
    costsField = costsRow->property("field").value<QQuickItem *>();
    QTest::mouseClick(window, Qt::LeftButton, {}, costsField->mapToScene(QPointF(4, costsField->height() / 2)).toPoint());
    QTRY_COMPARE(outliner->property("editingId").toString(), costsId);
    QQuickItem *moveUp = item(QStringLiteral("outlineMoveUpButton"));
    QTRY_VERIFY(moveUp->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(moveUp, "clicked"));
    QTRY_COMPARE(model->root()->children.front()->id, costsId);
    QTRY_COMPARE(outliner->property("editingId").toString(), costsId);
    QTRY_VERIFY(rowItem(costsId) && rowItem(costsId)->property("field").value<QQuickItem *>()->hasActiveFocus());
    QVERIFY(QMetaObject::invokeMethod(outliner, "endEdit", Q_ARG(QVariant, QVariant())));
    QVERIFY(map->undo());
    QTRY_COMPARE(model->root()->children.front()->id, planId);

    // The selected row's ⋮: the desktop's context menu; Duplicate, one step.
    map->select(costsId);
    QQuickItem *rowMenu = nullptr;
    QTRY_VERIFY((rowMenu = visual(rowItem(costsId), QStringLiteral("outlineRowMenu"))) && rowMenu->isVisible());
    QVERIFY(QMetaObject::invokeMethod(rowMenu, "clicked"));
    QObject *menu = window->findChild<QObject *>(QStringLiteral("outlineMenu"));
    QTRY_VERIFY(menu && menu->property("visible").toBool());
    QStringList rows;
    for (QQuickItem *row : menu->property("rows").value<QQuickItem *>()->childItems())
        if (row->objectName().endsWith(QLatin1String("Row"))) rows.append(row->objectName());
    QCOMPARE(rows, (QStringList{"outlineRowSelectMoreRow", "outlineRowInsertRow", "outlineRowCopyRow", "outlineRowCutRow", "outlineRowPasteRow",
                                "outlineRowDuplicateRow", "outlineRowIndentRow", "outlineRowOutdentRow", "outlineRowDeleteRow",
                                "outlineRowClearRow", "outlineRowCopyStyleRow", "outlineRowPasteStyleRow"}));
    QQuickItem *duplicateRow = nullptr;
    for (QQuickItem *row : menu->property("rows").value<QQuickItem *>()->childItems())
        if (row->objectName() == QLatin1String("outlineRowDuplicateRow")) duplicateRow = row;
    QVERIFY(QMetaObject::invokeMethod(duplicateRow, "clicked"));
    QTRY_COMPARE(list->property("count").toInt(), 6);
    QVERIFY(map->undo());
    QTRY_COMPARE(list->property("count").toInt(), 5);

    // A2: a long press lifts Costs; on the middle of Plan's row it drops inside, as its last child.
    QTRY_VERIFY((costsRow = rowItem(costsId)) && (planRow = rowItem(planId)));
    const QPoint onCosts = costsRow->mapToScene(QPointF(costsRow->width() / 2, costsRow->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, {}, onCosts);
    QTRY_VERIFY(outliner->property("dragging").toBool());
    QVERIFY(item(QStringLiteral("outlineDragGhost"))->isVisible());
    QCOMPARE(map->selectedId(), costsId);
    const QPoint onPlan = planRow->mapToScene(QPointF(planRow->width() / 2, planRow->height() / 2)).toPoint();
    QTest::mouseMove(window, onPlan);
    QTRY_COMPARE(outliner->property("dropId").toString(), planId);
    QCOMPARE(outliner->property("dropWhere").toInt(), 0);
    QTest::mouseRelease(window, Qt::LeftButton, {}, onPlan);
    QTRY_COMPARE(model->nodeById(costsId)->parent->id, planId);
    QVERIFY(!outliner->property("dragging").toBool());
    QVERIFY(map->undo());
    QTRY_COMPARE(model->nodeById(costsId)->parent, model->root());
    // Plan over its own Budget: no drop, nothing moves.
    QTRY_VERIFY((planRow = rowItem(planId)));
    QVERIFY(model->nodeById(budgetId));
    QQuickItem *budgetRow = nullptr;
    QTRY_VERIFY((budgetRow = rowItem(budgetId)));
    QTest::mousePress(window, Qt::LeftButton, {}, planRow->mapToScene(QPointF(planRow->width() / 2, planRow->height() / 2)).toPoint());
    QTRY_VERIFY(outliner->property("dragging").toBool());
    const QPoint onBudget = budgetRow->mapToScene(QPointF(budgetRow->width() / 2, budgetRow->height() / 2)).toPoint();
    QTest::mouseMove(window, onBudget);
    QTRY_COMPARE(outliner->property("dropId").toString(), QString());
    QTest::mouseRelease(window, Qt::LeftButton, {}, onBudget);
    QTRY_VERIFY(!outliner->property("dragging").toBool());
    QCOMPARE(model->nodeById(budgetId)->parent->id, planId);
    QCOMPARE(model->root()->children.front()->id, planId);

    // FEAT-055: Markdown pasted in the main title is the whole sheet; the typing goes on there.
    const QString rootTitle = model->root()->title;
    QQuickItem *rootField = item(QStringLiteral("outlineRootTitle"));
    QVERIFY(rootField);
    QGuiApplication::clipboard()->setText(QStringLiteral("- Alpha\n  - Beta\n- Gamma"));
    QTest::mouseClick(window, Qt::LeftButton, {}, rootField->mapToScene(QPointF(4, rootField->height() / 2)).toPoint());
    QTRY_COMPARE(outliner->property("editingId").toString(), model->root()->id);
    QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE(model->root()->title, QStringLiteral("Alpha"));
    QTRY_COMPARE(outliner->property("editingId").toString(), model->root()->id);
    QTRY_VERIFY(rootField->hasActiveFocus());
    QCOMPARE(rootField->property("length").toInt(), 5);
    QCOMPARE(list->property("count").toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(outliner, "endEdit", Q_ARG(QVariant, QVariant())));
    QVERIFY(map->undo());
    QTRY_COMPARE(model->root()->title, rootTitle);
    QTRY_COMPARE(list->property("count").toInt(), 5);

    // D: a tap on Plan's bullet enters its branch, the way back over its title; a tap on the main
    // title there shows the whole sheet; Back goes up a level before the map.
    QTRY_VERIFY((planRow = rowItem(planId)));
    const auto onBullet = [&] {
        return planRow->mapToScene(QPointF(planRow->property("rowLeft").toReal() + 10, planRow->property("titleHeight").toReal() / 2)).toPoint();
    };
    QTest::mouseClick(window, Qt::LeftButton, {}, onBullet());
    QTRY_COMPARE(list->property("count").toInt(), 2);
    QCOMPARE(map->selectedId(), planId);
    QTRY_COMPARE(item(QStringLiteral("outlineRootTitle"))->property("text").toString().contains(QStringLiteral("Plan")), true);
    QQuickItem *crumbs = visual(window->contentItem(), QStringLiteral("outlineCrumbs"));
    QVERIFY(crumbs && crumbs->isVisible());
    QTest::mouseClick(window, Qt::LeftButton, {}, crumbs->mapToScene(QPointF(8, crumbs->height() / 2)).toPoint());
    QTRY_COMPARE(list->property("count").toInt(), 5);
    QTRY_VERIFY(!crumbs->isVisible());
    QCOMPARE(map->selectedId(), planId);  // the topic left stays selected
    QTRY_VERIFY((planRow = rowItem(planId)));
    QTest::mouseClick(window, Qt::LeftButton, {}, onBullet());
    QTRY_COMPARE(list->property("count").toInt(), 2);
    QTest::keyClick(window, Qt::Key_Back);
    QTRY_COMPARE(list->property("count").toInt(), 5);
    QVERIFY(outliner->isVisible());

    // Android's Back goes back to the map.
    QTest::keyClick(window, Qt::Key_Back);
    QTRY_VERIFY(!outliner->isVisible());
    QVERIFY(canvas->isVisible());
    QCOMPARE(toggle->property("text").toString(), QStringLiteral("Outliner"));
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).toString()));
}

// FEAT-121 B: the ⋮ menu's Select more, taps adding and removing rows, the bar's Delete, Copy and
// Cut on every selected row in one step, and the ways out of the mode.
void MobileTest::outlinerMultipleSelection() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Outline.mentis"));
    QCOMPARE(writeOutlineMap(path), QString());
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
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    const auto item = [&](const QString &name) { return window->findChild<QQuickItem *>(name); };
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *root, const QString &name) -> QQuickItem * {
        if (root->objectName() == name) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = visual(child, name)) return found;
        return nullptr;
    };
    const auto rowItem = [&](const QString &id) { return visual(window->contentItem(), QStringLiteral("outlineRow_") + id); };
    const auto tapTitle = [&](const QString &id) {
        QQuickItem *row = rowItem(id);
        QVERIFY(row);
        QQuickItem *field = row->property("field").value<QQuickItem *>();
        QTest::mouseClick(window, Qt::LeftButton, {}, field->mapToScene(QPointF(4, field->height() / 2)).toPoint());
    };
    MindMapModel *model = map->document()->model();
    const QString planId = childTitled(model->root(), QStringLiteral("Plan"))->id;
    const QString costsId = childTitled(model->root(), QStringLiteral("Costs"))->id;
    const QString rootId = model->root()->id;

    QQuickItem *outliner = item(QStringLiteral("outliner"));
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("outlinerButton")), "clicked"));
    QTRY_VERIFY(outliner->isVisible());
    QQuickItem *list = item(QStringLiteral("outlinerList"));
    QTRY_COMPARE(list->property("count").toInt(), 5);
    QQuickItem *bar = item(QStringLiteral("outlineSelectionBar"));
    QVERIFY(bar && !bar->isVisible());

    // The ⋮ menu's first row starts the mode with the row in it; the bar shows.
    const auto startFromMenu = [&](const QString &id) {
        map->select(id);
        QQuickItem *rowMenu = nullptr;
        QTRY_VERIFY((rowMenu = visual(rowItem(id), QStringLiteral("outlineRowMenu"))) && rowMenu->isVisible());
        QVERIFY(QMetaObject::invokeMethod(rowMenu, "clicked"));
        QObject *menu = window->findChild<QObject *>(QStringLiteral("outlineMenu"));
        QTRY_VERIFY(menu && menu->property("visible").toBool());
        QQuickItem *selectMore = nullptr;
        for (QQuickItem *row : menu->property("rows").value<QQuickItem *>()->childItems())
            if (row->objectName() == QLatin1String("outlineRowSelectMoreRow")) selectMore = row;
        QVERIFY(selectMore);
        QVERIFY(QMetaObject::invokeMethod(selectMore, "clicked"));
        QTRY_VERIFY(map->multiSelect());
        QCOMPARE(map->selectedIds(), QStringList{id});
    };
    startFromMenu(planId);
    QTRY_VERIFY(bar->isVisible());
    QVERIFY(!visual(rowItem(planId), QStringLiteral("outlineRowMenu"))->isVisible());

    // A tap on a title adds the row (both tinted, no typing), a second one removes it.
    tapTitle(costsId);
    QTRY_COMPARE(map->selectedIds(), (QStringList{planId, costsId}));
    QCOMPARE(outliner->property("editingId").toString(), QString());
    QVERIFY(rowItem(planId)->property("inSelection").toBool());
    QVERIFY(rowItem(costsId)->property("inSelection").toBool());
    tapTitle(costsId);
    QTRY_COMPARE(map->selectedIds(), QStringList{planId});
    QVERIFY(!rowItem(costsId)->property("inSelection").toBool());
    tapTitle(costsId);
    QTRY_COMPARE(map->selectedIds(), (QStringList{planId, costsId}));
    QCOMPARE(list->property("count").toInt(), 5);  // a tap on a branch's row does not fold it

    // Cut: both branches on the clipboard and removed in one step; the mode ends.
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("outlineSelectionCut")), "clicked"));
    QTRY_COMPARE(list->property("count").toInt(), 1);
    QVERIFY(!map->multiSelect());
    QVERIFY(!bar->isVisible());
    const QString cut = QGuiApplication::clipboard()->text();
    QVERIFY(cut.contains(QStringLiteral("Plan")) && cut.contains(QStringLiteral("Budget")) && cut.contains(QStringLiteral("Costs")));
    QVERIFY(map->undo());
    QTRY_COMPARE(list->property("count").toInt(), 5);
    QVERIFY(model->nodeById(planId) && model->nodeById(costsId));

    // With the central topic in the selection Cut copies and removes only the others.
    map->select(rootId);
    map->startMultiSelect();
    QTRY_VERIFY(bar->isVisible());
    QVERIFY(!item(QStringLiteral("outlineSelectionCut"))->isEnabled());  // the central topic alone
    QTRY_VERIFY(rowItem(costsId));
    tapTitle(costsId);
    QTRY_COMPARE(map->selectedIds(), (QStringList{rootId, costsId}));
    QTRY_VERIFY(item(QStringLiteral("outlineSelectionCut"))->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("outlineSelectionCut")), "clicked"));
    QTRY_VERIFY(!model->nodeById(costsId));
    QVERIFY(!QGuiApplication::clipboard()->text().contains(QStringLiteral("Plan")));
    QCOMPARE(model->root()->id, rootId);
    QVERIFY(map->undo());
    QTRY_COMPARE(list->property("count").toInt(), 5);

    // Copy keeps the mode and the rows; Delete removes both in one step.
    startFromMenu(planId);
    QTRY_VERIFY(rowItem(costsId));
    tapTitle(costsId);
    QTRY_COMPARE(map->selectedIds(), (QStringList{planId, costsId}));
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("outlineSelectionCopy")), "clicked"));
    QVERIFY(map->multiSelect());
    QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral("Costs")));
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("outlineSelectionDelete")), "clicked"));
    QTRY_COMPARE(list->property("count").toInt(), 1);
    QVERIFY(!map->multiSelect());
    QVERIFY(map->undo());
    QTRY_COMPARE(list->property("count").toInt(), 5);

    // The ways out: the bar's Select more, a tap on the main title (no typing), Back (the outline stays).
    startFromMenu(planId);
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("outlineSelectMoreButton")), "clicked"));
    QTRY_VERIFY(!map->multiSelect());
    QCOMPARE(map->selectedId(), planId);
    startFromMenu(planId);
    QQuickItem *rootField = item(QStringLiteral("outlineRootTitle"));
    QTest::mouseClick(window, Qt::LeftButton, {}, rootField->mapToScene(QPointF(4, rootField->height() / 2)).toPoint());
    QTRY_VERIFY(!map->multiSelect());
    QCOMPARE(outliner->property("editingId").toString(), QString());
    startFromMenu(planId);
    QTest::keyClick(window, Qt::Key_Back);
    QTRY_VERIFY(!map->multiSelect());
    QVERIFY(outliner->isVisible());
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).toString()));
}

void MobileTest::outlinerContents() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Outline.mentis"));
    QCOMPARE(writeOutlineMap(path), QString());
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
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *root, const QString &name) -> QQuickItem * {
        if (root->objectName() == name) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = visual(child, name)) return found;
        return nullptr;
    };
    // The page's popups by their QML type (they have no object name).
    const auto popup = [&](const char *type) -> QObject * {
        for (QObject *object : window->findChildren<QObject *>())
            if (QString::fromLatin1(object->metaObject()->className()).startsWith(QLatin1String(type))) return object;
        return nullptr;
    };
    MindMapModel *model = map->document()->model();
    const MindNode *budgetTopic = childTitled(childTitled(model->root(), QStringLiteral("Plan")), QStringLiteral("Budget"));
    QVERIFY(budgetTopic);
    const QString budgetId = budgetTopic->id;

    // Budget with every content: a marker, a checkbox, a web link, a formula as its picture, a note
    // and two labels.
    map->select(budgetId);
    QVERIFY(map->setMarker(QStringLiteral("priority:1")));
    QVERIFY(map->toggleCheckbox());
    QVERIFY(map->setWebLink(QStringLiteral("example.com")));
    QVERIFY(map->setFormula(QStringLiteral("x^2")));
    QVERIFY(map->document()->setNote(model->nodeById(budgetId), QStringLiteral("Remember this"), QString()));
    QVERIFY(map->setLabels(QStringLiteral("alpha, beta")));  // its edit shows the note too
    const MindNode *budget = model->nodeById(budgetId);

    QQuickItem *toggle = window->findChild<QQuickItem *>(QStringLiteral("outlinerButton"));
    QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
    auto *outline = window->findChild<MobileOutline *>(QStringLiteral("outlineModel"));
    QVERIFY(outline);
    const QModelIndex index = outline->index(outline->rowOf(budgetId));
    QCOMPARE(outline->data(index, MobileOutline::MarkersRole).toStringList(), QStringList{QStringLiteral("priority:1")});
    QCOMPARE(outline->data(index, MobileOutline::TaskRole).toInt(), 1);
    QVERIFY(outline->data(index, MobileOutline::WebLinkRole).toBool());
    QVERIFY(!outline->data(index, MobileOutline::TopicLinkRole).toBool());
    QCOMPARE(outline->data(index, MobileOutline::FilesRole).toInt(), 0);
    QCOMPARE(outline->data(index, MobileOutline::PictureWidthRole).toReal(), budget->imageSize.width());
    QVERIFY(!outline->data(index, MobileOutline::NoteKeyRole).toString().isEmpty());
    QCOMPARE(outline->data(index, MobileOutline::LabelsRole).toStringList(), (QStringList{"alpha", "beta"}));

    QQuickItem *row = nullptr;
    QTRY_VERIFY((row = visual(window->contentItem(), QStringLiteral("outlineRow_") + budgetId)));
    // The desktop's places but the phone's 20 px bullet (D): a marker 22, the checkbox 24, the title's 15.
    QCOMPARE(row->property("titleLeft").toReal(), row->property("rowLeft").toReal() + 20 + 22 + 24 + 15);
    QCOMPARE(row->property("rowLeft").toReal(), 16 + 12 + 20.0);
    // D: compact below 600 dp: picture, note and labels as icons after the title, before the web link.
    QCOMPARE(row->property("trailing").toStringList(), (QStringList{"picture", "note", "labels", "web"}));
    QVERIFY(!visual(row, QStringLiteral("outlineNote"))->isVisible());
    QVERIFY(!visual(row, QStringLiteral("outlineLabels"))->isVisible());
    QVERIFY(!visual(row, QStringLiteral("outlinePicture"))->isVisible());
    {
        const qreal y = row->property("titleHeight").toReal() / 2;
        QQuickItem *icons = visual(row, QStringLiteral("outlineTrailingIcons"));
        QTRY_VERIFY(icons->width() > 0);  // laid out at the next frame
        QTest::mouseClick(window, Qt::LeftButton, {}, icons->mapToScene(QPointF(12, y - icons->y())).toPoint(), 400);
        QTRY_VERIFY(outline->data(index, MobileOutline::ExpandedRole).toBool());
        QTRY_VERIFY(row->property("contentsShown").toBool());
        QTRY_VERIFY(visual(row, QStringLiteral("outlinePicture"))->isVisible());  // the picture's icon shows them
        QVERIFY(visual(row, QStringLiteral("outlineNote"))->isVisible());
        outline->toggleContents(budgetId);
        QTRY_VERIFY(!visual(row, QStringLiteral("outlinePicture"))->isVisible());
    }
    // The map menu's Show contents (only in the outline below 600 dp) shows them on every row.
    const auto showContents = qScopeGuard([&] { outline->setShowContents(false); });
    QQuickItem *contentsRow = window->findChild<QQuickItem *>(QStringLiteral("outlineContentsButton"));
    QVERIFY(contentsRow && !contentsRow->property("checked").toBool());
    QVERIFY(QMetaObject::invokeMethod(contentsRow, "clicked"));
    QVERIFY(outline->showContents() && contentsRow->property("checked").toBool());
    QTRY_COMPARE(row->property("trailing").toStringList(), QStringList{"web"});
    QQuickItem *picture = visual(row, QStringLiteral("outlinePicture"));
    QVERIFY(picture && picture->isVisible());
    QTRY_VERIFY(picture->property("loaded").toBool());
    QQuickItem *note = visual(row, QStringLiteral("outlineNote"));
    QQuickItem *labels = visual(row, QStringLiteral("outlineLabels"));
    QVERIFY(note && note->isVisible() && labels && labels->isVisible());
    // Picture, note box, labels in that order under the title, once the loaded picture is laid out
    // (the column moves the note on its next polish: checked at once, it raced, 2026-11-16).
    QTRY_VERIFY(picture->mapToScene(QPointF()).y() < note->mapToScene(QPointF()).y());
    QTRY_VERIFY(note->mapToScene(QPointF()).y() < labels->mapToScene(QPointF()).y());
    QVERIFY(visual(row, QStringLiteral("outlineTrailingIcons"))->isVisible());

    const qreal titleY = row->property("titleHeight").toReal() / 2;
    const auto tap = [&](QQuickItem *on, QPointF at) {
        QTest::mouseClick(window, Qt::LeftButton, {}, on->mapToScene(at).toPoint(), 400);
    };
    // The checkbox: done, one step.
    map->select(QString());
    tap(row, QPointF(row->property("taskLeft").toReal() + 12, titleY));
    QTRY_COMPARE(model->nodeById(budgetId)->task, MindNode::Task::Done);
    QCOMPARE(map->selectedId(), budgetId);
    QTRY_COMPARE(outline->data(index, MobileOutline::TaskRole).toInt(), 2);
    QVERIFY(map->undo());
    QCOMPARE(model->nodeById(budgetId)->task, MindNode::Task::Open);
    // The marker: the map's marker menu.
    QObject *markerMenu = popup("MarkerMenu");
    QVERIFY(markerMenu);
    tap(row, QPointF(row->property("markersLeft").toReal() + 11, titleY));
    QTRY_VERIFY(markerMenu->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(markerMenu, "close"));
    QTRY_VERIFY(!markerMenu->property("visible").toBool());
    // The note: the Note modal; the labels: the Labels modal.
    QObject *noteDialog = popup("NoteDialog");
    QVERIFY(noteDialog);
    tap(note, QPointF(20, note->height() / 2));
    QTRY_VERIFY(noteDialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(noteDialog, "close"));
    QTRY_VERIFY(!noteDialog->property("visible").toBool());
    QObject *labelsDialog = popup("LabelsDialog");
    QVERIFY(labelsDialog);
    tap(labels, QPointF(10, 10));
    QTRY_VERIFY(labelsDialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(labelsDialog, "close"));
    QTRY_VERIFY(!labelsDialog->property("visible").toBool());

    // The picture: a tap picks it, its bottom right handle resizes it with its proportions, one step.
    map->select(QString());
    tap(picture, QPointF(picture->width() / 2, picture->height() / 2));
    QTRY_VERIFY(map->pictureSelected());
    QCOMPARE(map->selectedId(), budgetId);
    QQuickItem *handle = nullptr;
    QTRY_VERIFY((handle = visual(row, QStringLiteral("outlinePictureHandle2"))));
    const QSizeF before = model->nodeById(budgetId)->imageSize;
    const QPoint from = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, {}, from);
    QTest::mouseMove(window, from + QPoint(10, 10));
    QTest::mouseMove(window, from + QPoint(20, 20));
    QTest::mouseRelease(window, Qt::LeftButton, {}, from + QPoint(20, 20));
    QTRY_VERIFY(model->nodeById(budgetId)->imageSize.width() > before.width());
    const QSizeF after = model->nodeById(budgetId)->imageSize;
    QVERIFY(qAbs(after.width() / after.height() - before.width() / before.height()) < 0.02);
    QCOMPARE(after.width(), std::round(after.width()));  // rounded map px, as the map's handles
    QVERIFY(map->pictureSelected());
    QVERIFY(map->undo());
    QCOMPARE(model->nodeById(budgetId)->imageSize, before);
    // A tap elsewhere on the row lets the picture go: Delete then acts on the topic.
    QVERIFY(map->pictureSelected());
    tap(note, QPointF(20, note->height() / 2));
    QTRY_VERIFY(!map->pictureSelected());
    QCOMPARE(map->selectedId(), budgetId);
    QTRY_VERIFY(noteDialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(noteDialog, "close"));
    QTRY_VERIFY(!noteDialog->property("visible").toBool());

    // A4: the outline's zoom, 50% to 300%, shared by the outlines while the app runs; every
    // measure but the sides grows with it.
    const auto resetZoom = qScopeGuard([&] { outline->setZoomPercent(100); });
    MobileOutline other;
    outline->setZoomPercent(20);
    QCOMPARE(outline->zoomPercent(), 50);
    outline->setZoomPercent(900);
    QCOMPARE(other.zoomPercent(), 300);
    outline->setZoomPercent(200);
    QTRY_COMPARE(row->property("rowLeft").toReal(), 16 + 2 * (12 + 20.0));  // the sides stay, a level doubles
    QCOMPARE(row->property("titleLeft").toReal(), 16 + 2 * (12 + 20 + 20 + 22 + 24 + 15.0));
    QCOMPARE(row->property("field").value<QQuickItem *>()->property("font").value<QFont>().pixelSize(), 36);
    QTRY_COMPARE(picture->width(), std::min(budget->imageSize.width() * 2, row->property("belowWidth").toReal()));
    QCOMPARE(window->findChild<QQuickItem *>(QStringLiteral("outlineRootTitle"))->property("font").value<QFont>().pixelSize(), 56);
    outline->setZoomPercent(100);
    // Two fingers moving apart over the list zoom in.
    QQuickItem *list = window->findChild<QQuickItem *>(QStringLiteral("outlinerList"));
    QPointingDevice *touch = QTest::createTouchDevice();
    const QPoint centre = list->mapToScene(QPointF(list->width() / 2, list->height() / 2)).toPoint();
    QTest::touchEvent(window, touch).press(0, centre - QPoint(40, 0)).press(1, centre + QPoint(40, 0));
    for (int step = 1; step <= 10; ++step)
        QTest::touchEvent(window, touch).move(0, centre - QPoint(40 + 8 * step, 0)).move(1, centre + QPoint(40 + 8 * step, 0));
    QTest::touchEvent(window, touch).release(0, centre - QPoint(120, 0)).release(1, centre + QPoint(120, 0));
    QTRY_VERIFY(outline->zoomPercent() > 150);
    QVERIFY(model->nodeById(budgetId)->imageSize == before);  // the map is unchanged

    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).toString()));

    // D: from 600 dp (tablets) the desktop's layout stays: 36 px levels, contents under the titles.
    // The resize is checked without the QML warnings: the closed modals (ModalDialog) report a
    // binding loop on implicitHeight when the window's width changes, a problem of their own.
    outline->setZoomPercent(100);
    outline->setShowContents(false);
    const QSize portrait = window->size();
    window->resize(800, portrait.height());
    const auto restore = qScopeGuard([&] { window->resize(portrait); });
    QTRY_COMPARE(row->property("rowLeft").toReal(), 16 + 36.0);
    QCOMPARE(row->property("titleLeft").toReal(), 16 + 36 + 24 + 22 + 24 + 15.0);
    QCOMPARE(row->property("trailing").toStringList(), QStringList{"web"});
    QVERIFY(visual(row, QStringLiteral("outlineNote"))->isVisible());
}

namespace {

const QDate kGanttMonday(2026, 11, 2);

// Plan (Budget from Monday, 3 days; Team from Thursday, 2 days), Costs on Friday, Idea without a
// date.
QString writeGanttMap(const QString &path) {
    Document document;
    document.startUntitled();
    MindNode *root = document.model()->root();
    MindNode *plan = document.addChild(root, QStringLiteral("Plan"));
    const QString budget = document.addChild(plan, QStringLiteral("Budget"))->id;
    const QString team = document.addChild(plan, QStringLiteral("Team"))->id;
    const QString costs = document.addChild(root, QStringLiteral("Costs"))->id;
    document.addChild(root, QStringLiteral("Idea"));
    document.flushPendingStep();
    document.editGantt([&](GanttPlan &gantt) {
        gantt.setStart(budget, kGanttMonday);
        gantt.setDuration(budget, 3);
        gantt.setStart(team, kGanttMonday.addDays(3));
        gantt.setDuration(team, 2);
        gantt.setStart(costs, kGanttMonday.addDays(4));
        return true;
    });
    { const MapSnapshot laidOut(document.model()); }
    const Document::Result saved = document.save(path, BmapStorage::SaveReason::Explicit);
    document.close();
    return saved.ok ? QString() : saved.error;
}

QStringList ganttNames(const MobileGantt &gantt, const MobileMap &map) {
    QStringList names;
    for (int row = 0; row < gantt.rowCount(); ++row) names.append(map.document()->model()->nodeById(gantt.rowId(row))->title);
    return names;
}

}  // namespace

void MobileTest::ganttOnThePhone() {
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MobileGantt gantt;
    gantt.setSize(QSizeF(400, 600));
    QSignalSpy scrolls(&gantt, &MobileGantt::scrollRequested);
    gantt.setMap(&map);
    QCOMPARE(gantt.rowCount(), 0);  // nothing read while the map is shown
    gantt.setActive(true);

    // The rows in tree order, the Name column 40% wide; the opening day near the left edge.
    QCOMPARE(ganttNames(gantt, map), (QStringList{"Plan", "Budget", "Team", "Costs", "Idea"}));
    QCOMPARE(gantt.nameWidth(), 160.0);
    QVERIFY(!gantt.empty());
    QVERIFY(!scrolls.isEmpty());
    gantt.setContentX(scrolls.last().first().toReal());
    const QString budget = gantt.rowId(1), costs = gantt.rowId(3), idea = gantt.rowId(4);
    const QRectF bar = gantt.barRect(budget);
    QVERIFY(bar.left() > gantt.tableWidth());
    QCOMPARE(bar.width(), 3.0 * 28);
    QCOMPARE(bar.top(), double(MobileGantt::kHeaderHeight + MobileGantt::kRowHeight + 4));
    QCOMPARE(gantt.barRect(costs).width(), 28.0);
    QVERIFY(gantt.barRect(idea).isEmpty());                // undated: no bar

    // A tap on a row's name or bar selects its topic; on the summary's arrow it folds (no step).
    gantt.tapAt(gantt.nameWidth() - 10, bar.center().y());
    QCOMPARE(map.selectedId(), budget);
    map.select(QString());
    gantt.tapAt(bar.center().x(), bar.center().y());
    QCOMPARE(map.selectedId(), budget);
    const bool undo = map.canUndo();
    gantt.tapAt(20, MobileGantt::kHeaderHeight + MobileGantt::kRowHeight / 2.0);
    QVERIFY(gantt.folded(gantt.rowId(0)));
    QCOMPARE(ganttNames(gantt, map), (QStringList{"Plan", "Costs", "Idea"}));
    QCOMPARE(map.canUndo(), undo);
    gantt.tapAt(20, MobileGantt::kHeaderHeight + MobileGantt::kRowHeight / 2.0);
    QCOMPARE(gantt.rowCount(), 5);

    // Scales and zoom: the day width changes, the date under the fingers stays.
    gantt.setScale(1);
    QCOMPARE(gantt.barRect(budget).width(), 3.0 * 12);
    gantt.setScale(0);
    const double dayBefore = (bar.center().x() - gantt.tableWidth() + gantt.contentX()) / 28;
    gantt.zoomTo(200, bar.center().x());
    QCOMPARE(gantt.zoomPercent(), 200);
    gantt.setContentX(scrolls.last().first().toReal());
    QCOMPARE(gantt.barRect(budget).width(), 3.0 * 56);
    QVERIFY(std::abs((bar.center().x() - gantt.tableWidth() + gantt.contentX()) / 56 - dayBefore) < 0.05);
    gantt.zoomTo(1000, 0);
    QCOMPARE(gantt.zoomPercent(), 300);
    gantt.zoomTo(100, 0);  // the zoom is shared while the app runs: back for the other tests
    gantt.scrollToToday();
    gantt.setContentX(scrolls.last().first().toReal());

    // A painted frame: the bar in the default task colour.
    QImage image(400, 600, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        gantt.paint(&painter);
    }
    const QRectF shown = gantt.barRect(budget);
    QVERIFY(shown.left() > gantt.tableWidth() && shown.right() < 400);
    QCOMPARE(image.pixelColor(shown.center().toPoint() + QPoint(0, -4)), QColor(0xf9, 0xbd, 0x89));

    // A map with the central topic alone: the empty plan; Add task makes a main topic and selects it.
    map.close();
    const QString emptyPath = source.filePath(QStringLiteral("Empty.mentis"));
    {
        Document document;
        document.startUntitled();
        QVERIFY(document.save(emptyPath, BmapStorage::SaveReason::Explicit).ok);
        document.close();
    }
    MobileMap blank;
    QVERIFY(blank.open(emptyPath));
    gantt.setMap(&blank);
    QVERIFY(gantt.empty());
    const QString added = gantt.addTask();
    QVERIFY(!added.isEmpty());
    QCOMPARE(blank.selectedId(), added);
    QVERIFY(!gantt.empty());
    QCOMPARE(gantt.rowCount(), 1);
    blank.close();
}

void MobileTest::ganttOnThePage() {
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    // Rotating reports the modal dialogs' known implicitHeight binding loops (also before 2026-11-15).
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            if (!warning.toString().contains(QLatin1String("ModalDialog: Binding loop"))) errors.append(warning);
    });
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
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    const auto item = [&](const QString &name) { return window->findChild<QQuickItem *>(name); };

    // The map menu's Gantt shows the Gantt in place of the map; its row then reads Mind map.
    QQuickItem *view = item(QStringLiteral("gantt"));
    QQuickItem *toggle = item(QStringLiteral("ganttButton"));
    QQuickItem *outlinerToggle = item(QStringLiteral("outlinerButton"));
    QVERIFY(view && toggle && outlinerToggle);
    QVERIFY(!view->isVisible());
    QVERIFY(toggle->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
    QTRY_VERIFY(view->isVisible());
    QVERIFY(!canvas->isVisible());
    QCOMPARE(toggle->property("text").toString(), QStringLiteral("Mind map"));
    auto *gantt = window->findChild<MobileGantt *>(QStringLiteral("ganttChart"));
    QVERIFY(gantt);
    QTRY_COMPARE(gantt->rowCount(), 5);
    QVERIFY(gantt->y() > 0);  // under the top groups
    QCOMPARE(gantt->nameWidth(), std::round(gantt->width() * 0.4));
    {
        // User's request 2026-11-22: Name's grip on the header at its right edge; kept in the settings.
        QQuickItem *grip = item(QStringLiteral("ganttNameGrip"));
        QVERIFY(grip);
        QTRY_VERIFY(grip->isVisible());
        const qreal before = gantt->nameWidth();
        QCOMPARE(grip->x() + grip->width() / 2, gantt->x() + gantt->nameRight());
        const QPoint from = grip->mapToScene(QPointF(grip->width() / 2, grip->height() / 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, {}, from);
        for (int step = 1; step <= 10; ++step) QTest::mouseMove(window, from + QPoint(5 * step, 0));
        QTest::mouseRelease(window, Qt::LeftButton, {}, from + QPoint(50, 0));
        QTRY_COMPARE(gantt->nameWidth(), before + 50);
        QCOMPARE(QSettings().value(QStringLiteral("gantt/mobileNameShare")).toReal(), gantt->nameWidth() / gantt->width());
        gantt->setNameWidth(10, false);  // never under 96 px
        QCOMPARE(gantt->nameWidth(), qreal(MobileGantt::kMinName));
        gantt->setNameWidth(5000, false);  // the chart keeps its 48 px
        QCOMPARE(gantt->tableWidth(), gantt->width() - MobileGantt::kMinChart);
        gantt->setNameWidth(gantt->width() * MobileGantt::kDefaultNameShare, true);
        QSettings().remove(QStringLiteral("gantt/mobileNameShare"));
        QCOMPARE(gantt->nameWidth(), before);
    }

    // User's request 2026-11-15: no Fit with the Gantt, in the group or the map menu; in landscape the
    // side panel opens at the left, as the desktop's task panel, the chart and the sheet group beside it.
    const QSize portrait = window->size();
    QQuickItem *fit = item(QStringLiteral("fitButton"));
    QQuickItem *sheetGroup = item(QStringLiteral("sheetGroup"));
    QVERIFY(fit && sheetGroup);
    QVERIFY(!fit->isVisible());
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("mapMenuButton")), "clicked"));
    QObject *mapMenu = window->findChild<QObject *>(QStringLiteral("mapMenu"));
    QTRY_VERIFY(mapMenu->property("opened").toBool());
    QStringList menuRows;
    for (QQuickItem *row : mapMenu->property("rows").value<QQuickItem *>()->childItems()) menuRows << row->objectName();
    QVERIFY(!menuRows.contains(QStringLiteral("fitMenuButtonRow")));
    QVERIFY(menuRows.contains(QStringLiteral("ganttButtonRow")));
    QMetaObject::invokeMethod(mapMenu, "close");
    QTRY_VERIFY(!mapMenu->property("opened").toBool());
    window->resize(800, 360);
    QTRY_COMPARE(window->width(), 800);
    QVERIFY(!fit->isVisible());
    const qreal sheetX = sheetGroup->x();
    // The panel opens at the left: the icon has its narrow part at the left (user's request 2026-11-17).
    QCOMPARE(QQmlProperty::read(item(QStringLiteral("panelButton")), QStringLiteral("icon.source")).toUrl(),
             QUrl(QStringLiteral("qrc:/assets/hugeicons/panel-left.svg")));
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("panelButton")), "clicked"));
    QQuickItem *panel = nullptr;
    QTRY_VERIFY((panel = item(QStringLiteral("sidePanel"))) && panel->isVisible());
    QCOMPARE(panel->x(), 0.0);
    QTRY_COMPARE(gantt->x(), panel->width());
    QCOMPARE(sheetGroup->x(), sheetX + panel->width());
    QQuickItem *history = item(QStringLiteral("historyGroup"));
    QVERIFY(sheetGroup->x() + sheetGroup->width() <= history->x());
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("panelButton")), "clicked"));
    QTRY_VERIFY(!panel->isVisible());
    QCOMPARE(gantt->x(), 0.0);
    window->resize(portrait);
    QTRY_COMPARE(window->size(), portrait);

    // The scale buttons; the Flickable follows the opening day's scroll.
    // Repeater delegates have no QObject parent: found through the visual tree.
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *root, const QString &name) -> QQuickItem * {
        if (root->objectName() == name) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = visual(child, name)) return found;
        return nullptr;
    };
    QQuickItem *weeks = visual(window->contentItem(), QStringLiteral("ganttScale1"));
    QVERIFY(weeks);
    QVERIFY(QMetaObject::invokeMethod(weeks, "clicked"));
    QCOMPARE(gantt->scale(), 1);
    QVERIFY(weeks->property("checked").toBool());
    QQuickItem *flick = item(QStringLiteral("ganttFlick"));
    QVERIFY(flick);
    QCOMPARE(flick->property("contentX").toReal(), gantt->contentX());

    // The Critical path button, pressed while the path is shown, in the desktop's setting.
    QQuickItem *critical = visual(window->contentItem(), QStringLiteral("ganttCritical"));
    QVERIFY(critical);
    QVERIFY(gantt->criticalShown());
    QVERIFY(critical->property("checked").toBool());
    QVERIFY(QMetaObject::invokeMethod(critical, "clicked"));
    QVERIFY(!gantt->criticalShown());
    QVERIFY(!critical->property("checked").toBool());
    QCOMPARE(QSettings().value(QStringLiteral("gantt/critical")).toBool(), false);
    QVERIFY(QMetaObject::invokeMethod(critical, "clicked"));
    QVERIFY(gantt->criticalShown());
    QCOMPARE(QSettings().value(QStringLiteral("gantt/critical")).toBool(), true);

    // A tap on a bar selects its topic.
    const QString budget = gantt->rowId(1);
    const QRectF bar = gantt->barRect(budget);
    QVERIFY(!bar.isEmpty());
    map->select(QString());
    QTest::mouseClick(window, Qt::LeftButton, {}, gantt->mapToScene(bar.center()).toPoint());
    QTRY_COMPARE(map->selectedId(), budget);

    // FEAT-122 B1: with a task selected the row bar takes the scale group's place; its ⋮ opens the
    // row menu; nothing selected brings the scale group back.
    QQuickItem *rowBar = item(QStringLiteral("ganttRowBar"));
    QQuickItem *scaleGroup = item(QStringLiteral("ganttScaleGroup"));
    QVERIFY(rowBar && scaleGroup);
    QTRY_VERIFY(rowBar->isVisible());
    QVERIFY(!scaleGroup->isVisible());
    QVERIFY(item(QStringLiteral("ganttAddSubtask"))->isEnabled());
    QVERIFY(!item(QStringLiteral("ganttChain"))->isEnabled());  // one task without links
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("ganttRowMenu")), "clicked"));
    auto *rowMenu = window->findChild<QObject *>(QStringLiteral("ganttMenu"));
    QVERIFY(rowMenu);
    QTRY_VERIFY(rowMenu->property("visible").toBool());
    QCOMPARE(rowMenu->property("tools").toList().size(), 10);  // ... Go to today, Resources (C1), Baseline (D), Columns… (F)
    QMetaObject::invokeMethod(rowMenu, "close");
    QTRY_VERIFY(!rowMenu->property("visible").toBool());
    // FEAT-122 F: the row menu's Columns… and a tap on the table's header open the Columns modal.
    auto *columnsDialog = window->findChild<QObject *>(QStringLiteral("columnsDialog"));
    QVERIFY(columnsDialog);
    QVERIFY(QMetaObject::invokeMethod(window->findChild<QObject *>(QStringLiteral("ganttColumnsRow")), "clicked"));
    QTRY_VERIFY(columnsDialog->property("opened").toBool());
    QMetaObject::invokeMethod(columnsDialog, "close");
    QTRY_VERIFY(!columnsDialog->property("opened").toBool());
    gantt->tapAt(20, 10);
    QTRY_VERIFY(columnsDialog->property("opened").toBool());
    QMetaObject::invokeMethod(columnsDialog, "close");
    QTRY_VERIFY(!columnsDialog->property("opened").toBool());
    // A hardware keyboard: Tab puts the task under the one before, Shift+Tab back (user 2026-11-18);
    // nothing on the first task of its level.
    const QString team = gantt->rowId(2);
    const MindMapModel *ganttModel = map->document()->model();
    const QString budgetParent = ganttModel->nodeById(budget)->parent->id;
    map->select(budget);
    QTest::keyClick(window, Qt::Key_Tab);
    QCOMPARE(ganttModel->nodeById(budget)->parent->id, budgetParent);
    map->select(team);
    QTRY_VERIFY([&] { QTest::keyClick(window, Qt::Key_Tab); return ganttModel->nodeById(team)->parent->id == budget; }());
    QCOMPARE(map->selectedId(), team);
    QTest::keyClick(window, Qt::Key_Backtab, Qt::ShiftModifier);
    QCOMPARE(ganttModel->nodeById(team)->parent->id, budgetParent);
    QVERIFY(map->undo());
    QVERIFY(map->undo());
    map->select(QString());
    QTRY_VERIFY(scaleGroup->isVisible());
    QVERIFY(!rowBar->isVisible());

    // The Outliner row switches view; Back returns to the map.
    QVERIFY(QMetaObject::invokeMethod(outlinerToggle, "clicked"));
    QTRY_VERIFY(!view->isVisible());
    QVERIFY(item(QStringLiteral("outliner"))->isVisible());
    QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
    QTRY_VERIFY(view->isVisible());
    QVERIFY(!item(QStringLiteral("outliner"))->isVisible());
    QTest::keyClick(window, Qt::Key_Back);
    QTRY_VERIFY(!view->isVisible());
    QVERIFY(canvas->isVisible());
    QCOMPARE(toggle->property("text").toString(), QStringLiteral("Gantt"));
    gantt->setScale(0);
    for (const QQmlError &error : std::as_const(errors)) qWarning() << error.toString();
    QVERIFY(errors.isEmpty());
}

void MobileTest::ganttRowActions() {
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MobileGantt gantt;
    gantt.setSize(QSizeF(400, 600));
    gantt.setMap(&map);
    gantt.setActive(true);
    const QStringList start{QStringLiteral("Plan"), QStringLiteral("Budget"), QStringLiteral("Team"), QStringLiteral("Costs"), QStringLiteral("Idea")};
    QCOMPARE(ganttNames(gantt, map), start);
    const QString budget = gantt.rowId(1), team = gantt.rowId(2), costs = gantt.rowId(3), idea = gantt.rowId(4);
    const auto action = [&](const char *name) { return gantt.actions().value(QLatin1String(name)); };
    QCOMPARE(action("tasks").toInt(), 0);

    // Add task: a sibling after the selected one, selected; Undo removes it in one step.
    map.selectTopic(budget);
    QCOMPARE(action("tasks").toInt(), 1);
    QVERIFY(!action("canIndent").toBool());  // the first child
    QVERIFY(action("canOutdent").toBool());
    const QString added = gantt.addRow(false);
    QVERIFY(!added.isEmpty());
    QCOMPARE(map.selectedId(), added);
    QCOMPARE(gantt.rowId(2), added);
    QVERIFY(map.undo());
    QCOMPARE(ganttNames(gantt, map), start);

    // Add subtask under a folded summary opens it.
    gantt.tapAt(20, MobileGantt::kHeaderHeight + 10);  // Plan's fold arrow
    QVERIFY(gantt.folded(gantt.rowId(0)));
    map.selectTopic(gantt.rowId(0));
    const QString sub = gantt.addRow(true);
    QVERIFY(!sub.isEmpty());
    QVERIFY(!gantt.folded(gantt.rowId(0)));
    QCOMPARE(gantt.rowId(3), sub);
    QVERIFY(map.undo());

    // Level down and up: Team under Budget, then back.
    map.selectTopic(team);
    QVERIFY(action("canIndent").toBool());
    QVERIFY(gantt.indentSelected());
    QCOMPARE(map.document()->model()->nodeById(team)->parent->id, budget);
    QCOMPARE(map.selectedId(), team);
    QVERIFY(gantt.outdentSelected());
    QCOMPARE(map.document()->model()->nodeById(team)->parent->id, gantt.rowId(0));
    QVERIFY(map.undo());
    QVERIFY(map.undo());
    QCOMPARE(ganttNames(gantt, map), start);

    // Link in a chain: the tasks in the order tapped (Costs, then Idea), one step; then Remove links.
    map.selectTopic(costs);
    QVERIFY(!action("canChain").toBool());
    map.startMultiSelect();
    gantt.tapAt(300, MobileGantt::kHeaderHeight + 4 * MobileGantt::kRowHeight + 10);  // Idea's row
    QCOMPARE(map.selectedIds(), (QStringList{costs, idea}));
    QVERIFY(action("canChain").toBool());
    QVERIFY(!action("chainUnlinks").toBool());
    QVERIFY(!action("canIndent").toBool());  // one task at a time
    QVERIFY(gantt.chainSelected());
    GanttPlan plan = map.document()->model()->gantt();
    QCOMPARE(plan.dependencies().size(), 1);
    QCOMPARE(plan.dependencies().first().from, costs);
    QVERIFY(action("chainUnlinks").toBool());
    QVERIFY(gantt.chainSelected());
    plan = map.document()->model()->gantt();
    QVERIFY(plan.dependencies().isEmpty());
    QVERIFY(map.undo());
    QCOMPARE(map.document()->model()->gantt().dependencies().size(), 1);
    QVERIFY(map.undo());
    // A tap under the last row clears the selection and the mode.
    gantt.tapAt(300, gantt.height() - 4);
    QVERIFY(!map.multiSelect());
    QVERIFY(map.selectedIds().isEmpty());

    // Colour: the topic's branch colour, then the default again.
    map.selectTopic(budget);
    QVERIFY(!action("hasColor").toBool());
    QVERIFY(gantt.setColor(QColor(0x12, 0x34, 0x56)));
    QVERIFY(action("hasColor").toBool());
    QCOMPARE(action("color").value<QColor>(), QColor(0x12, 0x34, 0x56));
    QCOMPARE(map.document()->model()->gantt().task(budget)->color, QColor(0x12, 0x34, 0x56));
    QVERIFY(gantt.clearColor());
    QVERIFY(!action("hasColor").toBool());
    QVERIFY(!gantt.clearColor());

    // Name on the bar, and Remove date.
    QVERIFY(!action("nameOnBar").toBool());
    QVERIFY(gantt.setNameOnBar(true));
    QVERIFY(action("nameOnBar").toBool());
    QVERIFY(action("canRemoveDate").toBool());
    QVERIFY(gantt.removeDates());
    QVERIFY(!map.document()->model()->gantt().task(budget)->start.isValid());
    QVERIFY(!action("canRemoveDate").toBool());
    QVERIFY(map.undo());
    QVERIFY(map.document()->model()->gantt().task(budget)->start.isValid());
}

void MobileTest::ganttTaskPanel() {
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MobileGantt gantt;  // for the rows' ids
    gantt.setSize(QSizeF(400, 600));
    gantt.setMap(&map);
    gantt.setActive(true);
    const QString plan = gantt.rowId(0), budget = gantt.rowId(1), team = gantt.rowId(2), costs = gantt.rowId(3), idea = gantt.rowId(4);
    MobileGanttTask task;
    task.setMap(&map);
    QVERIFY(task.fields().isEmpty());  // nothing read while the page is not shown
    task.setActive(true);
    const auto field = [&](const char *name) { return task.fields().value(QLatin1String(name)); };
    const auto taskOf = [&](const QString &id) { return map.document()->model()->gantt().task(id); };
    const auto iso = [](const QDate &date) { return date.toString(Qt::ISODate); };
    QVERIFY(!field("task").toBool());

    // One task: its values, as the desktop's panel.
    map.selectTopic(budget);
    QVERIFY(field("task").toBool());
    QCOMPARE(field("title").toString(), QStringLiteral("Task"));
    QCOMPARE(field("name").toString(), QStringLiteral("Budget"));
    QCOMPARE(field("start").toString(), iso(kGanttMonday));
    QCOMPARE(field("duration").toInt(), 3);
    QCOMPARE(field("end").toString(), iso(kGanttMonday.addDays(2)));
    QVERIFY(field("lengthShown").toBool() && field("linksShown").toBool() && field("clearShown").toBool());
    QVERIFY(!field("addShown").toBool());

    // Each edit one Undo step.
    const auto rename = [&](const QString &id, const QString &name) {
        QTextDocument typed;
        task.loadName(&typed, id);
        typed.setPlainText(name);
        return task.renameFrom(typed, id);
    };
    QVERIFY(rename(budget, QStringLiteral("  Money  ")));
    QCOMPARE(field("name").toString(), QStringLiteral("Money"));
    QVERIFY(!rename(budget, QStringLiteral("   ")));
    QVERIFY(map.undo());
    QCOMPARE(field("name").toString(), QStringLiteral("Budget"));
    // FEAT-124: the name keeps its formats; one typed in it ("**", bold) is written into the title.
    {
        QTextDocument typed;
        task.loadName(&typed, budget);
        QTextCursor cursor(&typed);
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(QStringLiteral(" plan"));
        cursor.movePosition(QTextCursor::StartOfWord);
        cursor.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
        QTextCharFormat bold;
        bold.setFontWeight(task.nameBoldWeight(budget));
        cursor.mergeCharFormat(bold);
        QVERIFY(task.renameFrom(typed, budget));
        const MindNode *topic = map.document()->model()->nodeById(budget);
        QCOMPARE(topic->title, QStringLiteral("Budget plan"));
        QTextDocument saved;
        saved.setHtml(topic->titleHtml);
        QTextCursor at(&saved);
        at.setPosition(9);
        QVERIFY(at.charFormat().fontWeight() >= QFont::Bold);
        at.setPosition(3);
        QVERIFY(at.charFormat().fontWeight() < QFont::Bold);
        // Read back with its bold; renamed again, the bold stays.
        QTextDocument again;
        task.loadName(&again, budget);
        QTextCursor read(&again);
        read.setPosition(9);
        QVERIFY(read.charFormat().fontWeight() >= QFont::Bold);
        QVERIFY(!task.renameFrom(again, budget));  // unchanged: no step
        QTextCursor(&again).insertText(QStringLiteral("New "));
        QVERIFY(task.renameFrom(again, budget));
        QCOMPARE(topic->title, QStringLiteral("New Budget plan"));
        saved.setHtml(topic->titleHtml);
        at.setPosition(13);
        QVERIFY(at.charFormat().fontWeight() >= QFont::Bold);
        QVERIFY(map.undo());
        QVERIFY(map.undo());
        QCOMPARE(field("name").toString(), QStringLiteral("Budget"));
    }
    {
        QTextDocument typed;
        MindNode *topic = map.document()->model()->nodeById(budget);
        // A title with a formula takes no format: only the formats changed, nothing is written.
        QVERIFY(map.document()->setFormattedTitle(topic, QStringLiteral("Area $x$ m"), QString()));
        map.document()->flushPendingStep();
        task.loadName(&typed, budget);
        QVERIFY(!task.nameFormattable(budget));
        QTextCursor all(&typed);
        all.select(QTextCursor::Document);
        QTextCharFormat bold;
        bold.setFontWeight(QFont::Black);
        all.mergeCharFormat(bold);
        QVERIFY(!task.renameFrom(typed, budget));
        QCOMPARE(topic->title, QStringLiteral("Area $x$ m"));
        QVERIFY(map.undo());
    }
    QVERIFY(task.setDuration(5));
    QVERIFY(task.stepDuration(-1));
    QCOMPARE(taskOf(budget)->duration, 4);
    QVERIFY(map.undo());
    QCOMPARE(taskOf(budget)->duration, 5);
    QVERIFY(map.undo());
    QVERIFY(task.setEnd(iso(kGanttMonday.addDays(4))));
    QCOMPARE(taskOf(budget)->duration, 5);
    QVERIFY(map.undo());
    QVERIFY(task.setStart(iso(kGanttMonday.addDays(7))));
    QCOMPARE(taskOf(budget)->start, kGanttMonday.addDays(7));
    QVERIFY(task.clearStart());
    QVERIFY(field("addShown").toBool());
    QVERIFY(!field("startShown").toBool());
    QVERIFY(task.addStart());  // today
    QCOMPARE(taskOf(budget)->start, kGanttMonday);
    QVERIFY(task.setProgress(40));
    QVERIFY(task.stepProgress(10));
    QCOMPARE(field("progress").toInt(), 50);
    QVERIFY(task.setPriority(4));
    QCOMPARE(field("priority").toInt(), 4);
    QCOMPARE(task.priorityNames().value(4), GanttCommands::priorityName(4));
    QVERIFY(task.setNameOnBar(true));
    QVERIFY(field("nameOnBar").toBool());
    QVERIFY(task.setMilestone(true));
    QVERIFY(field("milestone").toBool());
    QVERIFY(!field("lengthShown").toBool());
    QCOMPARE(field("progressStep").toInt(), GanttPlan::kMaxProgress);
    QVERIFY(map.undo());
    QVERIFY(!field("milestone").toBool());

    // Links: a predecessor added, its type and lag changed, removed.
    map.selectTopic(team);
    const auto candidateIds = [&](bool predecessors) {
        QStringList ids;
        for (const QVariant &row : task.linkCandidates(predecessors)) ids.append(row.toMap().value(QStringLiteral("id")).toString());
        return ids;
    };
    QCOMPARE(candidateIds(true), (QStringList{budget, costs, idea}));  // not the summary nor itself
    QVERIFY(task.addLink(true, budget));
    QCOMPARE(task.predecessors().size(), 1);
    QVariantMap link = task.predecessors().first().toMap();
    QCOMPARE(link.value(QStringLiteral("id")).toString(), budget);
    QCOMPARE(link.value(QStringLiteral("parent")).toString(), QStringLiteral("Plan"));
    QVERIFY(!candidateIds(true).contains(budget));
    QVERIFY(task.setLinkType(true, budget, 1));
    QVERIFY(task.setLinkLag(true, budget, -2));
    link = task.predecessors().first().toMap();
    QCOMPARE(link.value(QStringLiteral("type")).toInt(), 1);
    QCOMPARE(link.value(QStringLiteral("lag")).toInt(), -2);
    QCOMPARE(map.document()->model()->gantt().dependencies().first().type, GanttDependency::Type::StartStart);
    QVERIFY(task.removeLink(true, budget));
    QVERIFY(task.predecessors().isEmpty());
    QVERIFY(map.undo());
    QCOMPARE(task.predecessors().size(), 1);

    // A summary: only its name and the name on the bar.
    map.selectTopic(plan);
    QVERIFY(!field("datesShown").toBool() && !field("milestoneShown").toBool() && !field("linksShown").toBool());
    QVERIFY(!field("detailsShown").toBool());

    // Several tasks: the values they share, the edits on all of them.
    map.selectTopic(costs);
    map.startMultiSelect();
    map.toggleInSelection(idea);
    QVERIFY(field("several").toBool());
    QCOMPARE(field("count").toInt(), 2);
    QCOMPARE(field("title").toString(), QCoreApplication::translate("GanttTaskPanel", "%n task(s)", nullptr, 2));
    QVERIFY(!field("nameEnabled").toBool());
    QVERIFY(!field("linksShown").toBool());
    QVERIFY(task.setDuration(6));
    QCOMPARE(taskOf(costs)->duration, 6);
    QCOMPARE(taskOf(idea)->duration, 6);
    QCOMPARE(field("duration").toInt(), 6);

    // Delete: the topics go, the page shows no task; Undo brings them back.
    QVERIFY(task.removeTasks());
    QVERIFY(!map.document()->model()->nodeById(costs));
    QVERIFY(!field("task").toBool());
    QVERIFY(map.undo());
    QVERIFY(map.document()->model()->nodeById(costs));

    // The date picker's month: six weeks from the locale's first day.
    const QVariantMap month = task.calendar(iso(kGanttMonday));
    QCOMPARE(month.value(QStringLiteral("days")).toList().size(), 42);
    QCOMPARE(month.value(QStringLiteral("weekdays")).toStringList().size(), 7);
    QVERIFY(month.value(QStringLiteral("title")).toString().endsWith(QStringLiteral("2026")));
    QCOMPARE(task.addMonths(iso(kGanttMonday), 1), iso(kGanttMonday.addMonths(1)));
}

// FEAT-002 (user 2026-11-22): a printable key on the Gantt's selected task opens the Task page with
// that key as its whole name; Enter keeps it, Esc the old one, and the next key types again.
void MobileTest::ganttTypingRenamesTask() {
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
    QQmlApplicationEngine engine;
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
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(window));
    const auto item = [&](const QString &name) { return window->findChild<QQuickItem *>(name); };
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("ganttButton")), "clicked"));
    auto *gantt = window->findChild<MobileGantt *>(QStringLiteral("ganttChart"));
    QTRY_VERIFY(gantt && gantt->rowCount() == 5);
    const QString budget = gantt->rowId(1);
    map->selectTopic(budget);
    const auto title = [&] { return map->document()->model()->nodeById(budget)->title; };
    const QString original = title();

    QVERIFY(!item(QStringLiteral("ganttTaskPage")));  // the panel is closed
    QTest::keyClick(window, 'q');
    QQuickItem *name = nullptr;
    QTRY_VERIFY((name = item(QStringLiteral("ganttTaskName"))) && name->hasActiveFocus());
    QVERIFY(item(QStringLiteral("ganttTaskPage"))->isVisible());
    QTest::keyClick(window, '7');
    QTRY_COMPARE(name->property("text").toString(), QStringLiteral("q7"));
    // User 2026-11-23: the row and the bar show the Name as it is typed; the topic keeps its own.
    QCOMPARE(gantt->shownName(budget), QStringLiteral("q7"));
    QCOMPARE(title(), original);
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(title(), QStringLiteral("q7"));
    QCOMPARE(gantt->shownName(budget), QStringLiteral("q7"));
    QVERIFY(!name->hasActiveFocus());
    QTest::keyClick(window, 'z');  // the page has the keys again
    QTRY_VERIFY(name->hasActiveFocus());
    QTRY_COMPARE(name->property("text").toString(), QStringLiteral("z"));
    QCOMPARE(gantt->shownName(budget), QStringLiteral("z"));
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!name->hasActiveFocus());
    QCOMPARE(title(), QStringLiteral("q7"));
    QCOMPARE(gantt->shownName(budget), QStringLiteral("q7"));  // Esc: the old name back on the row
    QTRY_COMPARE(name->property("text").toString(), QStringLiteral("q7"));
    // User 2026-11-23: Enter on the selected task adds the next one with its Name open, as the
    // desktop's table; typing replaces it, Enter keeps it and the next Enter adds another.
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(gantt->rowCount(), 6);
    const QString added = map->selectedId();
    QVERIFY(added != budget);
    QCOMPARE(map->document()->model()->nodeById(added)->parent, map->document()->model()->nodeById(budget)->parent);
    QTRY_VERIFY(name->hasActiveFocus());
    for (const char key : {'N', 'e', 'w'}) QTest::keyClick(window, key);
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(map->document()->model()->nodeById(added)->title, QStringLiteral("New"));
    QVERIFY(!name->hasActiveFocus());
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(gantt->rowCount(), 7);
    QTRY_VERIFY(map->selectedId() != added && name->hasActiveFocus());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!name->hasActiveFocus());
    QVERIFY(map->undo());
    QVERIFY(map->undo());
    QVERIFY(map->undo());
    QTRY_COMPARE(gantt->rowCount(), 5);
    QVERIFY(map->undo());
    QTRY_COMPARE(title(), QStringLiteral("Budget"));
}

void MobileTest::ganttTaskPage() {
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
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
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    const auto item = [&](const QString &name) { return window->findChild<QQuickItem *>(name); };
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("ganttButton")), "clicked"));
    auto *gantt = window->findChild<MobileGantt *>(QStringLiteral("ganttChart"));
    QTRY_VERIFY(gantt && gantt->rowCount() == 5);
    const QString budget = gantt->rowId(1);
    map->selectTopic(budget);
    const qreal fullHeight = gantt->height();

    // The panel's button opens the Task page (Task | Gantt while the Gantt is shown).
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("panelButton")), "clicked"));
    QQuickItem *page = nullptr;
    QTRY_VERIFY((page = item(QStringLiteral("ganttTaskPage"))) && page->isVisible());
    QVERIFY(!item(QStringLiteral("ganttRowBar"))->isVisible());  // under the bottom panel
    QVERIFY(gantt->height() < fullHeight);  // the rows keep the part above the panel
    auto *segments = item(QStringLiteral("panelSegments"));
    QCOMPARE(segments->property("segments").toList().size(), 2);  // Task | Gantt (C2)
    auto *name = item(QStringLiteral("ganttTaskName"));
    QVERIFY(name);
    QTRY_COMPARE(name->property("text").toString(), QStringLiteral("Budget"));

    // A name being typed is confirmed on its task when another row is tapped.
    QMetaObject::invokeMethod(name, "forceActiveFocus");
    QTRY_VERIFY(name->hasActiveFocus());
    name->setProperty("text", QStringLiteral("Money"));
    map->selectTopic(gantt->rowId(2));
    QTRY_COMPARE(map->document()->model()->nodeById(budget)->title, QStringLiteral("Money"));
    QTRY_COMPARE(name->property("text").toString(), QStringLiteral("Team"));
    QVERIFY(map->undo());
    map->selectTopic(budget);
    QTRY_COMPARE(name->property("text").toString(), QStringLiteral("Budget"));
    // FEAT-124: "**x**" typed in the name is bold, written when the field is left; Ctrl+I on a
    // selection there formats only it.
    QMetaObject::invokeMethod(name, "forceActiveFocus");
    QTRY_VERIFY(name->hasActiveFocus());
    name->setProperty("cursorPosition", 6);
    for (const char c : QByteArrayLiteral(" **x**")) {
        QTest::keyClick(window, c);
        QCoreApplication::processEvents();
    }
    QTRY_COMPARE(name->property("text").toString(), QStringLiteral("Budget x"));
    name->setProperty("cursorPosition", 0);
    QMetaObject::invokeMethod(name, "select", Q_ARG(int, 0), Q_ARG(int, 3));
    QTest::keyClick(window, Qt::Key_I, Qt::ControlModifier);
    QMetaObject::invokeMethod(name, "deselect");
    name->setProperty("focus", false);
    {
        const MindNode *renamed = map->document()->model()->nodeById(budget);
        QTRY_COMPARE(renamed->title, QStringLiteral("Budget x"));
        QTextDocument saved;
        saved.setHtml(renamed->titleHtml);
        QTextCursor at(&saved);
        at.setPosition(8);
        QVERIFY(at.charFormat().fontWeight() >= QFont::Bold);
        at.setPosition(2);
        QVERIFY(at.charFormat().fontItalic());
        at.setPosition(5);
        QVERIFY(!at.charFormat().fontItalic());
    }
    QVERIFY(map->undo());
    QTRY_COMPARE(name->property("text").toString(), QStringLiteral("Budget"));

    // Start opens the date picker; a day sets it.
    QQuickItem *start = item(QStringLiteral("ganttTaskStartField"));
    QVERIFY(start && start->isVisible());
    QVERIFY(QMetaObject::invokeMethod(start, "clicked"));
    auto *picker = window->findChild<QObject *>(QStringLiteral("datePickerSheet"));
    QVERIFY(picker);
    QTRY_VERIFY(picker->property("opened").toBool());
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *root, const QString &wanted) -> QQuickItem * {
        if (root->objectName() == wanted) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = visual(child, wanted)) return found;
        return nullptr;
    };
    QQuickItem *overlay = window->contentItem()->parentItem() ? window->contentItem()->parentItem() : window->contentItem();
    const QDate wanted = kGanttMonday.addDays(9);
    QQuickItem *day = visual(overlay, QStringLiteral("day_") + wanted.toString(Qt::ISODate));
    QVERIFY(day);
    QVERIFY(QMetaObject::invokeMethod(day, "clicked"));
    QTRY_VERIFY(!picker->property("visible").toBool());
    QCOMPARE(map->document()->model()->gantt().task(budget)->start, wanted);

    // Delete asks first.
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("ganttTaskDelete")), "clicked"));
    auto *dialog = window->findChild<QObject *>(QStringLiteral("ganttDeleteDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
    QTRY_VERIFY(!map->document()->model()->nodeById(budget));
    QTRY_VERIFY(item(QStringLiteral("ganttNoTask"))->isVisible());
    map->undo();
    QTest::keyClick(window, Qt::Key_Back);  // closes the panel
    QTRY_COMPARE(gantt->height(), fullHeight);
    for (const QQmlError &error : std::as_const(errors)) qWarning() << error.toString();
    QVERIFY(errors.isEmpty());
}

void MobileTest::ganttChartEdits() {
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MobileGantt gantt;
    gantt.setSize(QSizeF(400, 600));
    gantt.setMap(&map);
    gantt.setActive(true);
    const QString plan = gantt.rowId(0), budget = gantt.rowId(1), costs = gantt.rowId(3);
    const auto taskOf = [&](const QString &id) { return map.document()->model()->gantt().task(id); };
    const auto drag = [&](const QPointF &from, const QPointF &to) {
        if (!gantt.pressAt(from.x(), from.y())) return false;
        gantt.dragTo((from.x() + to.x()) / 2, (from.y() + to.y()) / 2);
        gantt.dragTo(to.x(), to.y());
        return gantt.releaseAt(to.x(), to.y());
    };

    // Handles: the one task selected, not a summary nor in the multiple selection.
    QVERIFY(gantt.handlesId().isEmpty());
    map.selectTopic(plan);
    QVERIFY(gantt.handlesId().isEmpty());
    map.selectTopic(budget);
    QCOMPARE(gantt.handlesId(), budget);
    const qreal day = gantt.barRect(budget).width() / 3;  // Budget lasts 3 days, every day worked
    QVERIFY(!gantt.pressAt(gantt.nameWidth() / 2, gantt.handlePoint(QStringLiteral("bar")).y()));  // the names
    QVERIFY(!gantt.pressAt(gantt.width() - 4, gantt.height() - 4));  // elsewhere: the drag scrolls

    // A tap on the bar changes nothing.
    const QPointF bar = gantt.handlePoint(QStringLiteral("bar"));
    QVERIFY(gantt.pressAt(bar.x(), bar.y()));
    QVERIFY(!gantt.releaseAt(bar.x() + 2, bar.y()));
    QCOMPARE(taskOf(budget)->start, kGanttMonday);

    // Move two days, one step.
    QVERIFY(drag(bar, bar + QPointF(2 * day, 0)));
    QCOMPARE(map.document()->model()->gantt().schedule().tasks.value(budget).start, kGanttMonday.addDays(2));
    QVERIFY(map.undo());
    QCOMPARE(map.document()->model()->gantt().schedule().tasks.value(budget).start, kGanttMonday);

    // The end grip: two days longer.
    const QPointF grip = gantt.handlePoint(QStringLiteral("grip"));
    QVERIFY(drag(grip, grip + QPointF(2 * day, 0)));
    QCOMPARE(taskOf(budget)->duration, 5);
    QVERIFY(map.undo());

    // The progress badge: to the bar's middle.
    const QPointF badge = gantt.handlePoint(QStringLiteral("progress"));
    const QRectF barRect = gantt.barRect(budget);
    QVERIFY(drag(badge, QPointF(barRect.center().x(), badge.y())));
    QCOMPARE(taskOf(budget)->progress, 50);
    QVERIFY(map.undo());

    // The end circle onto Costs' bar: Budget comes first.
    const QPointF end = gantt.handlePoint(QStringLiteral("end"));
    QVERIFY(drag(end, gantt.barRect(costs).center()));
    GanttPlan edited = map.document()->model()->gantt();
    QCOMPARE(edited.dependencies().size(), 1);
    QCOMPARE(edited.dependencies().first().from, budget);
    QCOMPARE(edited.dependencies().first().to, costs);
    QCOMPARE(gantt.handlesId(), budget);  // the handles stay

    // A tap on the arrow: its menu's link; type and lag, then Delete.
    QSignalSpy tapped(&gantt, &MobileGantt::dependencyTapped);
    const QPointF arrow = gantt.arrowPoint(0);
    QVERIFY(!arrow.isNull());
    gantt.tapAt(arrow.x(), arrow.y());
    QCOMPARE(tapped.size(), 1);
    const QVariantMap link = tapped.first().first().toMap();
    QCOMPARE(link.value(QStringLiteral("from")).toString(), budget);
    QCOMPARE(map.selectedId(), budget);  // the arrow does not select
    QVERIFY(gantt.setDependency(budget, costs, int(GanttDependency::Type::FinishFinish), 2));
    edited = map.document()->model()->gantt();
    QCOMPARE(edited.dependencies().first().type, GanttDependency::Type::FinishFinish);
    QCOMPARE(edited.dependencies().first().lag, 2);
    QCOMPARE(gantt.dependencyTypes().size(), 4);
    QVERIFY(gantt.removeDependency(budget, costs));
    QVERIFY(map.document()->model()->gantt().dependencies().isEmpty());
    QVERIFY(map.undo());
    QCOMPARE(map.document()->model()->gantt().dependencies().size(), 1);

    // In the multiple selection no handles.
    map.startMultiSelect();
    map.toggleInSelection(costs);
    QVERIFY(gantt.handlesId().isEmpty());
}

void MobileTest::ganttDoubleTapDates() {
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MobileGantt gantt;
    gantt.setSize(QSizeF(400, 600));
    gantt.setMap(&map);
    gantt.setActive(true);
    const QString plan = gantt.rowId(0), budget = gantt.rowId(1), idea = gantt.rowId(4);
    const auto taskOf = [&](const QString &id) { return map.document()->model()->gantt().task(id); };
    const QRectF monday = gantt.barRect(budget);
    const qreal day = monday.width() / 3;  // Budget: 3 days from Monday
    const auto dayX = [&](int days) { return monday.left() + (days + 0.5) * day; };
    const qreal ideaY = MobileGantt::kHeaderHeight + 4.5 * MobileGantt::kRowHeight;

    // A press without a tap before it, or a single tap on a dated row: nothing.
    QVERIFY(!gantt.pressAt(dayX(1), ideaY));
    gantt.tapAt(dayX(1), MobileGantt::kHeaderHeight + 1.5 * MobileGantt::kRowHeight);
    gantt.pressAt(dayX(1), MobileGantt::kHeaderHeight + 1.5 * MobileGantt::kRowHeight);  // the bar's handles
    QVERIFY(gantt.datingId().isEmpty());
    gantt.cancelGesture();
    // A summary takes its subtasks' dates.
    gantt.tapAt(dayX(1), MobileGantt::kHeaderHeight + 0.5 * MobileGantt::kRowHeight);
    QVERIFY(!gantt.pressAt(dayX(1), MobileGantt::kHeaderHeight + 0.5 * MobileGantt::kRowHeight));
    QCOMPARE(map.selectedId(), plan);
    // The second tap in another row: nothing.
    gantt.tapAt(dayX(1), ideaY);
    QVERIFY(!gantt.pressAt(dayX(1), ideaY - MobileGantt::kRowHeight));

    // A double tap on Idea: the day tapped, its duration kept, one step.
    gantt.tapAt(dayX(1), ideaY);
    QCOMPARE(map.selectedId(), idea);
    QVERIFY(gantt.pressAt(dayX(1), ideaY));
    QCOMPARE(gantt.datingId(), idea);
    QVERIFY(gantt.releaseAt(dayX(1) + 2, ideaY));
    QVERIFY(gantt.datingId().isEmpty());
    QCOMPARE(taskOf(idea)->start, kGanttMonday.addDays(1));
    QCOMPARE(taskOf(idea)->duration, 1);
    QVERIFY(!gantt.barRect(idea).isEmpty());
    QVERIFY(map.undo());
    QVERIFY(!taskOf(idea)->start.isValid());

    // The finger kept down and moved sideways: Tuesday to Thursday, 3 days.
    gantt.tapAt(dayX(1), ideaY);
    QVERIFY(gantt.pressAt(dayX(1), ideaY));
    gantt.dragTo(dayX(2), ideaY + 6);
    gantt.dragTo(dayX(3), ideaY + 6);
    QVERIFY(gantt.releaseAt(dayX(3), ideaY));
    QCOMPARE(taskOf(idea)->start, kGanttMonday.addDays(1));
    QCOMPARE(taskOf(idea)->duration, 3);
    QVERIFY(map.undo());
    QVERIFY(!taskOf(idea)->start.isValid());
    QCOMPARE(taskOf(idea)->duration, 1);

    // Dragged back: the start is the day under the finger.
    gantt.tapAt(dayX(3), ideaY);
    QVERIFY(gantt.pressAt(dayX(3), ideaY));
    QVERIFY(gantt.releaseAt(dayX(2), ideaY));
    QCOMPARE(taskOf(idea)->start, kGanttMonday.addDays(2));
    QCOMPARE(taskOf(idea)->duration, 2);
    QVERIFY(map.undo());

    // Too late for a double tap.
    gantt.tapAt(dayX(1), ideaY);
    QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
    QVERIFY(!gantt.pressAt(dayX(1), ideaY));

    // Not in Select more tasks.
    map.startMultiSelect();
    gantt.tapAt(dayX(1), ideaY);
    QVERIFY(!gantt.pressAt(dayX(1), ideaY));
}

void MobileTest::ganttChartDrag() {
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
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
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    QVERIFY(QMetaObject::invokeMethod(window->findChild<QQuickItem *>(QStringLiteral("ganttButton")), "clicked"));
    auto *gantt = window->findChild<MobileGantt *>(QStringLiteral("ganttChart"));
    QTRY_VERIFY(gantt && gantt->rowCount() == 5);
    QQuickItem *flick = window->findChild<QQuickItem *>(QStringLiteral("ganttFlick"));
    const QString budget = gantt->rowId(1), costs = gantt->rowId(3);
    const auto scene = [&](const QPointF &point) { return gantt->mapToScene(point).toPoint(); };

    // A tap selects the bar; a drag on it then moves it, the chart does not scroll.
    const QPoint bar = scene(gantt->barRect(budget).center());
    QTest::mouseClick(window, Qt::LeftButton, {}, bar);
    QTRY_COMPARE(gantt->handlesId(), budget);
    const qreal contentX = flick->property("contentX").toReal();
    const int day = int(gantt->barRect(budget).width() / 3);
    QTest::mousePress(window, Qt::LeftButton, {}, bar);
    for (int step = 1; step <= 4; ++step) QTest::mouseMove(window, bar + QPoint(step * day / 2, 0));
    QTest::mouseRelease(window, Qt::LeftButton, {}, bar + QPoint(2 * day, 0));
    QTRY_COMPARE(map->document()->model()->gantt().schedule().tasks.value(budget).start, kGanttMonday.addDays(2));
    QCOMPARE(flick->property("contentX").toReal(), contentX);
    QVERIFY(map->undo());

    // A sideways drag on the frozen table leaves the days still; on the chart it scrolls them.
    const auto swipe = [&](const QPoint &from) {
        QTest::mousePress(window, Qt::LeftButton, {}, from);
        for (int step = 1; step <= 8; ++step) QTest::mouseMove(window, from - QPoint(step * 20, 0), 16);
        QTest::mouseRelease(window, Qt::LeftButton, {}, from - QPoint(160, 0), 16);
    };
    const qreal below = gantt->barRect(costs).bottom() + 40;
    swipe(scene(QPointF(gantt->tableWidth() - 4, below)));
    QTest::qWait(300);
    QCOMPARE(flick->property("contentX").toReal(), contentX);
    swipe(scene(QPointF(gantt->width() - 10, below)));
    QTRY_VERIFY(flick->property("contentX").toReal() != contentX);
    QTRY_VERIFY(!flick->property("moving").toBool());
    flick->setProperty("contentX", contentX);

    // A tap on another bar selects it (the handles' layer lets it through).
    QTest::mouseClick(window, Qt::LeftButton, {}, scene(gantt->barRect(costs).center()));
    QTRY_COMPARE(map->selectedId(), costs);

    // A tap on an arrow opens its menu: the four types, Lag… and Delete dependency.
    MobileGanttTask links;  // the Task page's Successors "+ Add"
    links.setMap(map);
    links.setActive(true);
    map->selectTopic(budget);
    QVERIFY(links.addLink(false, costs));
    map->select(QString());
    QTRY_VERIFY(!gantt->arrowPoint(0).isNull());
    QTest::mouseClick(window, Qt::LeftButton, {}, scene(gantt->arrowPoint(0)));
    auto *menu = window->findChild<QObject *>(QStringLiteral("ganttArrowMenu"));
    QVERIFY(menu);
    QTRY_VERIFY(menu->property("visible").toBool());
    const QVariantList tools = menu->property("tools").toList();
    QCOMPARE(tools.size(), 6);
    QVERIFY(tools.first().value<QObject *>()->property("checked").toBool());  // Finish to start
    QMetaObject::invokeMethod(menu, "close");
    for (const QQmlError &error : std::as_const(errors)) qWarning() << error.toString();
    QVERIFY(errors.isEmpty());
}

void MobileTest::ganttRowDrag() {
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
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
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    QVERIFY(QMetaObject::invokeMethod(window->findChild<QQuickItem *>(QStringLiteral("ganttButton")), "clicked"));
    auto *gantt = window->findChild<MobileGantt *>(QStringLiteral("ganttChart"));
    QTRY_VERIFY(gantt && gantt->rowCount() == 5);
    MindMapModel *model = map->document()->model();
    const QString plan = gantt->rowId(0), budget = gantt->rowId(1), costs = gantt->rowId(3), idea = gantt->rowId(4);
    // A point on row `id` of the table (its Name column), `part` of the row from its top.
    const auto onRow = [&](const QString &id, qreal part) {
        const qreal y = MobileGantt::kHeaderHeight + (gantt->rowOf(id) + part) * MobileGantt::kRowHeight;
        return gantt->mapToScene(QPointF(gantt->nameWidth() - 8, y)).toPoint();
    };
    const auto lift = [&](const QString &id) {
        QTest::mousePress(window, Qt::LeftButton, {}, onRow(id, 0.5));
        QTRY_COMPARE(gantt->liftedId(), id);
    };

    // A tap on the table still selects the row, once.
    QTest::mouseClick(window, Qt::LeftButton, {}, onRow(budget, 0.5));
    QTRY_COMPARE(map->selectedId(), budget);
    QVERIFY(gantt->liftedId().isEmpty());

    // A long press lifts Costs; over the middle of Plan it drops inside, as its last subtask.
    lift(costs);
    QCOMPARE(map->selectedId(), costs);
    QTest::mouseMove(window, onRow(plan, 0.5));
    QTRY_COMPARE(gantt->dropId(), plan);
    QCOMPARE(gantt->dropPlace(), 0);
    QTest::mouseRelease(window, Qt::LeftButton, {}, onRow(plan, 0.5));
    QTRY_COMPARE(model->nodeById(costs)->parent->id, plan);
    QCOMPARE(model->nodeById(plan)->children.back()->id, costs);
    QVERIFY(gantt->liftedId().isEmpty());
    QCOMPARE(map->selectedId(), costs);
    QVERIFY(map->undo());  // one step
    QTRY_COMPARE(model->nodeById(costs)->parent, model->root());

    // Idea over Plan's top: before it, the first main topic.
    QTRY_COMPARE(gantt->rowId(4), idea);
    lift(idea);
    QTest::mouseMove(window, onRow(plan, 0.15));
    QTRY_COMPARE(gantt->dropId(), plan);
    QCOMPARE(gantt->dropPlace(), -1);
    QTest::mouseRelease(window, Qt::LeftButton, {}, onRow(plan, 0.15));
    QTRY_COMPARE(model->root()->children.front()->id, idea);
    QVERIFY(map->undo());
    QTRY_COMPARE(model->root()->children.back()->id, idea);

    // Plan over its own Budget: no drop, nothing moves.
    QTRY_COMPARE(gantt->rowId(0), plan);
    lift(plan);
    QTest::mouseMove(window, onRow(budget, 0.5));
    QTRY_VERIFY(gantt->dropId().isEmpty());
    QTest::mouseRelease(window, Qt::LeftButton, {}, onRow(budget, 0.5));
    QTRY_VERIFY(gantt->liftedId().isEmpty());
    QCOMPARE(model->nodeById(budget)->parent->id, plan);
    QCOMPARE(model->root()->children.front()->id, plan);
    for (const QQmlError &error : std::as_const(errors)) qWarning() << error.toString();
    QVERIFY(errors.isEmpty());
}

void MobileTest::ganttResources() {
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    QSettings().remove(QStringLiteral("gantt/currencies"));
    const auto clean = qScopeGuard([] { QSettings().remove(QStringLiteral("gantt/currencies")); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    Document *document = map.document();

    // The modal's draft: nothing changes the document before Save.
    MobileResources resources;
    resources.setMap(&map);
    QVERIFY(resources.load());
    QVERIFY(resources.people().isEmpty() && resources.valid());
    QCOMPARE(resources.currencies().size(), 4);
    QVERIFY(resources.saturday() && resources.sunday());
    QCOMPARE(resources.dayMinutes(), 8 * 60);
    resources.addPerson();
    QCOMPARE(resources.people().size(), 1);
    QVERIFY(resources.valid());  // an empty row is dropped, not marked
    resources.setText(0, QStringLiteral("firstName"), QStringLiteral("Anna"));
    QVERIFY(!resources.valid());
    QVariantMap wrong = resources.problems().value(0).toMap();
    QVERIFY(!wrong.value("firstName").toBool() && wrong.value("lastName").toBool() && wrong.value("role").toBool());
    resources.setText(0, QStringLiteral("lastName"), QStringLiteral("Rossi"));
    resources.setText(0, QStringLiteral("role"), QStringLiteral("Developer"));
    resources.setText(0, QStringLiteral("email"), QStringLiteral("anna"));
    QVERIFY(resources.problems().value(0).toMap().value("email").toBool());
    QVERIFY(!resources.valid());
    QVERIFY(!resources.save());
    resources.setText(0, QStringLiteral("email"), QStringLiteral("anna@example.com"));
    resources.setText(0, QStringLiteral("cost"), QLocale().toString(45.5, 'f', 2));
    QVERIFY(resources.valid());
    resources.addPerson();
    resources.setText(1, QStringLiteral("firstName"), QStringLiteral("Marco"));
    resources.setText(1, QStringLiteral("lastName"), QStringLiteral("Bianchi"));
    resources.setText(1, QStringLiteral("role"), QStringLiteral("PM"));
    resources.movePerson(1, 0);
    QCOMPARE(resources.people().value(0).toMap().value("firstName").toString(), QStringLiteral("Marco"));
    // FEAT-128: each person's currency; a new person takes the last one's.
    QCOMPARE(resources.rowCurrencies(), (QStringList{QStringLiteral("EUR €"), QStringLiteral("EUR €")}));
    resources.chooseCurrency(0, QStringLiteral("CHF"));
    QCOMPARE(resources.rowSymbols(), (QStringList{QStringLiteral("CHF"), QStringLiteral("€")}));
    resources.chooseCurrency(1, QStringLiteral("CHF"));
    resources.addPerson();  // left empty: dropped on Save
    QCOMPARE(resources.rowCurrencies().value(2), QStringLiteral("CHF"));
    resources.setSaturday(false);
    resources.setSunday(false);
    resources.setDayMinutes(7 * 60 + 30);
    QVERIFY(document->resources().people.isEmpty());

    // Save: one Undo step with the sheet's working week.
    QVERIFY(resources.save());
    QCOMPARE(document->resources().people.size(), 2);
    QCOMPARE(document->resources().people.at(0).firstName, QStringLiteral("Marco"));
    QCOMPARE(document->resources().people.at(1).hourlyCostCents, 4550);
    QCOMPARE(document->resources().people.at(0).currency.text(), QStringLiteral("CHF"));
    QCOMPARE(document->resources().people.at(1).currency.text(), QStringLiteral("CHF"));
    GanttCalendar calendar = document->model()->gantt().calendar();
    QVERIFY(!calendar.workingDays.contains(Qt::Saturday) && !calendar.workingDays.contains(Qt::Sunday));
    QCOMPARE(calendar.minutesPerDay, 7 * 60 + 30);
    QVERIFY(map.undo());
    QVERIFY(document->resources().people.isEmpty());
    QVERIFY(document->model()->gantt().calendar().workingDays.contains(Qt::Saturday));
    QVERIFY(map.redo());
    QVERIFY(resources.load());  // reopened: the saved list
    QCOMPARE(resources.people().size(), 2);
    QCOMPARE(resources.people().value(1).toMap().value("cost").toString(), QLocale().toString(45.5, 'f', 2));
    QVERIFY(!resources.save());  // nothing changed: no step
    // The day's length stays between a quarter hour and 24 h.
    resources.setDayMinutes(0);
    QCOMPARE(resources.dayMinutes(), GanttCalendar::kMinutesStep);
    resources.setDayMinutes(30 * 60);
    QCOMPARE(resources.dayMinutes(), 24 * 60);
    resources.setDayMinutes(8 * 60 + 10);  // to the nearest quarter hour
    QCOMPARE(resources.dayMinutes(), 8 * 60 + 15);

    // A currency added: saved on this device at once and chosen; listed or saved ones are refused.
    QCOMPARE(MobileResources::currencyCheck(QString(), QString()), 0);
    QCOMPARE(MobileResources::currencyCheck(QStringLiteral("USD"), QString()), 1);
    QCOMPARE(MobileResources::currencyCheck(QStringLiteral("RON"), QStringLiteral("lei")), 2);
    QVERIFY(!resources.addCurrency(5, QStringLiteral("RON"), QStringLiteral("lei")));  // no such person
    QVERIFY(resources.addCurrency(0, QStringLiteral("RON"), QStringLiteral("lei")));
    QCOMPARE(resources.rowCurrencies().value(0), QStringLiteral("RON lei"));
    QCOMPARE(resources.rowCurrencies().value(1), QStringLiteral("CHF"));
    QCOMPARE(GanttResources::savedCurrencies(), QStringList{QStringLiteral("RON lei")});
    QCOMPARE(MobileResources::currencyCheck(QStringLiteral("RON"), QStringLiteral("lei")), 1);
    const auto saved = [&](const QString &text) {
        for (const QVariant &choice : resources.currencies())
            if (choice.toMap().value("value").toString() == text) return choice.toMap().value("saved").toBool();
        return false;
    };
    QVERIFY(saved(QStringLiteral("RON lei")));
    // Removed while chosen: the person keeps it, offered without ×.
    resources.removeSavedCurrency(QStringLiteral("RON lei"));
    QVERIFY(GanttResources::savedCurrencies().isEmpty());
    QCOMPARE(resources.rowCurrencies().value(0), QStringLiteral("RON lei"));
    QVERIFY(!saved(QStringLiteral("RON lei")));
    QCOMPARE(resources.currencies().size(), 5);  // the four listed and RON lei

    // The Task page's people (FEAT-081, FEAT-083).
    MobileGantt gantt;
    gantt.setSize(QSizeF(400, 600));
    gantt.setMap(&map);
    gantt.setActive(true);
    const QString plan = gantt.rowId(0), budget = gantt.rowId(1), team = gantt.rowId(2);
    const QString marco = document->resources().people.at(0).id, anna = document->resources().people.at(1).id;
    MobileGanttTask task;
    task.setMap(&map);
    task.setActive(true);
    map.selectTopic(plan);  // a summary has no people
    QVERIFY(!task.fields().value("peopleShown").toBool());
    map.selectTopic(budget);
    QVERIFY(task.fields().value("peopleShown").toBool() && task.fields().value("hasResources").toBool());
    QVERIFY(task.people().isEmpty());
    QCOMPARE(task.personCandidates().size(), 2);
    QCOMPARE(task.personCandidates().value(1).toMap().value("name").toString(), QStringLiteral("Anna Rossi — Developer"));
    QVERIFY(task.assignPerson(anna));
    QCOMPARE(document->model()->nodeById(budget)->ganttPeople, QStringList{anna});
    QCOMPARE(task.people().size(), 1);
    QCOMPARE(task.people().value(0).toMap().value("percent").toInt(), 100);
    QVERIFY(task.setPersonPercent(anna, 50));
    QVERIFY(task.stepPersonPercent(anna, 5));
    QCOMPARE(document->model()->nodeById(budget)->percentOf(anna), 55);
    QVERIFY(map.undo());
    QCOMPARE(document->model()->nodeById(budget)->percentOf(anna), 50);
    QVERIFY(task.assignPerson(marco));
    QVERIFY(task.fields().value("everyoneAssigned").toBool());
    QVERIFY(task.personCandidates().isEmpty());
    // People in the Resources list's order: Marco first.
    QCOMPARE(task.people().value(0).toMap().value("id").toString(), marco);

    // Two tasks: Anna on one only is partial, her share differs; assigning adds her to both.
    map.startMultiSelect();
    map.toggleInSelection(team);
    QCOMPARE(task.fields().value("count").toInt(), 2);
    QCOMPARE(task.people().size(), 2);
    QVERIFY(task.people().value(1).toMap().value("partial").toBool());
    QVERIFY(task.fields().value("everyoneAssigned").toBool() == false);
    QCOMPARE(task.personCandidates().size(), 2);
    QVERIFY(task.assignPerson(anna));
    QCOMPARE(document->model()->nodeById(team)->ganttPeople, QStringList{anna});
    QCOMPARE(task.people().value(1).toMap().value("percent").toInt(), -1);  // 50 and 100
    QVERIFY(!task.stepPersonPercent(anna, 5));
    QVERIFY(task.setPersonPercent(anna, 20));
    QCOMPARE(document->model()->nodeById(team)->percentOf(anna), 20);
    QCOMPARE(document->model()->nodeById(budget)->percentOf(anna), 20);
    QVERIFY(task.unassignPerson(anna));
    QVERIFY(document->model()->nodeById(team)->ganttPeople.isEmpty());
    QCOMPARE(document->model()->nodeById(budget)->ganttPeople, QStringList{marco});
    map.stopMultiSelect();
}

void MobileTest::ganttResourcesDialog() {
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    QSettings().remove(QStringLiteral("gantt/currencies"));
    const auto clean = qScopeGuard([] { QSettings().remove(QStringLiteral("gantt/currencies")); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
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
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    const auto item = [&](const QString &name) { return window->findChild<QQuickItem *>(name); };
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("ganttButton")), "clicked"));
    auto *gantt = window->findChild<MobileGantt *>(QStringLiteral("ganttChart"));
    QTRY_VERIFY(gantt && gantt->rowCount() == 5);
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *root, const QString &wanted) -> QQuickItem * {
        if (root->objectName() == wanted) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = visual(child, wanted)) return found;
        return nullptr;
    };
    QQuickItem *overlay = window->contentItem()->parentItem() ? window->contentItem()->parentItem() : window->contentItem();
    const auto typeText = [&](const QString &text) {
        for (const QChar c : text) QTest::keyClick(window, c.toLatin1());
    };

    // The bottom group's Resources button opens the modal with the document's (empty) list.
    QQuickItem *button = item(QStringLiteral("ganttResources"));
    QVERIFY(button && button->isVisible());
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    auto *dialog = window->findChild<QObject *>(QStringLiteral("resourcesDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto *model = dialog->findChild<MobileResources *>(QStringLiteral("resourcesModel"));
    QVERIFY(model);
    QQuickItem *add = visual(overlay, QStringLiteral("resourcesAddPerson"));
    QVERIFY(add);
    QVERIFY(QMetaObject::invokeMethod(add, "clicked"));
    QTRY_VERIFY(visual(overlay, QStringLiteral("resourcesPerson0")));
    QQuickItem *card = visual(overlay, QStringLiteral("resourcesPerson0"));
    QQuickItem *first = visual(card, QStringLiteral("resources_firstName"));
    QVERIFY(first);
    QTRY_VERIFY(first->hasActiveFocus());  // the new person's first name, typed at once
    typeText(QStringLiteral("Anna"));
    QCOMPARE(model->people().size(), 1);
    QQuickItem *save = visual(overlay, QStringLiteral("resourcesSave"));
    QTRY_VERIFY(!save->isEnabled());  // last name and role are required
    QQuickItem *last = visual(card, QStringLiteral("resources_lastName"));
    QMetaObject::invokeMethod(last, "forceActiveFocus");
    typeText(QStringLiteral("Rossi"));
    QQuickItem *role = visual(card, QStringLiteral("resources_role"));
    QMetaObject::invokeMethod(role, "forceActiveFocus");
    typeText(QStringLiteral("Developer"));
    QTRY_VERIFY(save->isEnabled());

    // "+ Add currency…" opens the Add currency modal; Add saves and chooses it.
    QQuickItem *currency = visual(overlay, QStringLiteral("resourcesCurrency"));
    QVERIFY(currency);
    QVERIFY(QMetaObject::invokeMethod(currency, "picked", Q_ARG(QVariant, QVariant(QStringLiteral("+")))));
    auto *addCurrency = window->findChild<QObject *>(QStringLiteral("addCurrencyDialog"));
    QVERIFY(addCurrency);
    QTRY_VERIFY(addCurrency->property("opened").toBool());
    QQuickItem *addButton = visual(overlay, QStringLiteral("addCurrencyAdd"));
    QVERIFY(addButton && !addButton->isEnabled());
    typeText(QStringLiteral("USD"));
    QTRY_VERIFY(visual(overlay, QStringLiteral("addCurrencyNote"))->opacity() > 0);  // already listed
    QVERIFY(!addButton->isEnabled());
    QQuickItem *code = visual(overlay, QStringLiteral("addCurrencyCodeInput"));
    code->setProperty("text", QStringLiteral("RON"));
    visual(overlay, QStringLiteral("addCurrencySymbolInput"))->setProperty("text", QStringLiteral("lei"));
    QTRY_VERIFY(addButton->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(addButton, "clicked"));
    QTRY_VERIFY(!addCurrency->property("visible").toBool());
    QCOMPARE(model->rowCurrencies().value(0), QStringLiteral("RON lei"));  // the card's person
    QCOMPARE(currency->property("value").toString(), QStringLiteral("RON lei"));
    // The saved currency's × in the list removes it from this device.
    QVERIFY(QMetaObject::invokeMethod(currency, "clicked"));
    QQuickItem *remove = nullptr;
    QTRY_VERIFY((remove = visual(overlay, QStringLiteral("choiceRemoveRON lei"))) && remove->isVisible());
    QVERIFY(QMetaObject::invokeMethod(remove, "clicked"));
    QTRY_VERIFY(GanttResources::savedCurrencies().isEmpty());
    QCOMPARE(model->rowCurrencies().value(0), QStringLiteral("RON lei"));  // kept by the person
    auto *choices = window->findChild<QObject *>(QStringLiteral("styleChoiceMenu"));
    if (choices && choices->property("opened").toBool()) QMetaObject::invokeMethod(choices, "close");

    // Save: one step; the Task page offers Anna.
    QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(map->document()->resources().people.size(), 1);
    QCOMPARE(map->document()->resources().people.at(0).displayName(), QStringLiteral("Anna Rossi"));
    map->selectTopic(gantt->rowId(1));
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("panelButton")), "clicked"));
    QQuickItem *section = nullptr;
    QTRY_VERIFY((section = item(QStringLiteral("ganttPeopleSection"))) && section->isVisible());
    auto *task = window->findChild<MobileGanttTask *>();
    QVERIFY(task);
    QVERIFY(task->assignPerson(map->document()->resources().people.at(0).id));
    QTRY_VERIFY(visual(section, QStringLiteral("ganttPerson_") + map->document()->resources().people.at(0).id));

    // Opened again, each card is folded to its header with the name; a tap on it unfolds the card.
    QVERIFY(QMetaObject::invokeMethod(dialog, "openResources"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QTRY_VERIFY((card = visual(overlay, QStringLiteral("resourcesPerson0"))));
    first = visual(card, QStringLiteral("resources_firstName"));
    QVERIFY(first && !first->isVisible());
    QCOMPARE(visual(card, QStringLiteral("resourcesName"))->property("text").toString(), QStringLiteral("Anna Rossi"));
    QVERIFY(QMetaObject::invokeMethod(visual(card, QStringLiteral("resourcesToggle")), "clicked"));
    QTRY_VERIFY(first->isVisible());
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QVERIFY(errors.isEmpty());
}

void MobileTest::ganttResourceCharts() {
    // FEAT-127: the phone's charts give the desktop's figures (GanttPeopleCharts on the same sheet);
    // the unfolded card is the person; the toggle hides them and is kept.
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    QSettings().remove(QStringLiteral("gantt/resourceCharts"));
    const auto clean = qScopeGuard([] { QSettings().remove(QStringLiteral("gantt/resourceCharts")); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
    QQmlApplicationEngine engine;
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
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    // Two people on the sheet's tasks.
    Document *document = map->document();
    GanttResources resources;
    resources.people = {{"p1", "Anna", "Rossi", {}, "Dev", 6000}, {"p2", "Marco", "Bianchi", {}, "Design", 3000}};
    QVERIFY(document->setResources(resources, document->model()->gantt().calendar()));
    MindNode *budget = nullptr;
    MindNode *team = nullptr;
    for (const auto &plan : document->model()->root()->children)
        for (const auto &child : plan->children) {
            if (child->title == QLatin1String("Budget")) budget = child.get();
            if (child->title == QLatin1String("Team")) team = child.get();
        }
    QVERIFY(budget && team);
    QVERIFY(document->setTaskInfo({budget}, std::nullopt, {"p1", "p2"}));
    QVERIFY(document->setTaskInfo({team}, std::nullopt, {"p1"}));
    document->flushPendingStep();

    const auto item = [&](const QString &name) { return window->findChild<QQuickItem *>(name); };
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("ganttButton")), "clicked"));
    QTRY_VERIFY(window->findChild<MobileGantt *>(QStringLiteral("ganttChart")));
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *root, const QString &wanted) -> QQuickItem * {
        if (root->objectName() == wanted) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = visual(child, wanted)) return found;
        return nullptr;
    };
    QQuickItem *overlay = window->contentItem()->parentItem() ? window->contentItem()->parentItem() : window->contentItem();
    QQuickItem *button = item(QStringLiteral("ganttResources"));
    QTRY_VERIFY(button && button->isVisible());
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    auto *dialog = window->findChild<QObject *>(QStringLiteral("resourcesDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto *model = dialog->findChild<MobileResources *>(QStringLiteral("resourcesModel"));
    QVERIFY(model);
    QVERIFY(model->chartsShown());

    // The desktop's figures for the same sheet.
    const GanttPlan &plan = document->model()->gantt();
    const GanttPeopleCharts desktop = GanttPeopleCharts::compute(plan, document->model()->ganttAssignments(), resources.people,
                                                                 GanttCanvas::Scale(model->scale()));
    const GanttCosts exported = GanttCosts::compute(plan, plan.schedule(), document->model()->ganttAssignments(), resources.people);
    QCOMPARE(desktop.cost, exported.total.cost);
    QVariantMap charts = model->charts();
    QCOMPARE(charts.value("empty").toBool(), false);
    QCOMPARE(charts.value("cost").toString(), desktop.costText());
    QCOMPARE(charts.value("personDays").toString(), desktop.personDays());
    QCOMPARE(charts.value("days").toList().size(), 2);
    QCOMPARE(charts.value("roleBars").toList().size(), 1);
    QCOMPARE(charts.value("roleBars").toList().value(0).toMap().value("roles").toList().size(), 2);
    QVERIFY(!charts.value("person").isValid() || charts.value("person").isNull());
    QQuickItem *under = visual(overlay, QStringLiteral("resourcesChartsUnder"));
    QVERIFY(under && under->isVisible());  // a phone: under the people
    QQuickItem *summary = visual(under, QStringLiteral("resourcesChartsSummary"));
    QVERIFY(summary && summary->isVisible());
    QQuickItem *cost = visual(under, QStringLiteral("resourcesChartsCostValue"));
    QVERIFY(cost);
    QCOMPARE(cost->property("text").toString(), charts.value("cost").toString());

    // Unfolding Anna's card shows her, with the desktop's line.
    QQuickItem *card = nullptr;
    QTRY_VERIFY((card = visual(overlay, QStringLiteral("resourcesPerson0"))));
    QVERIFY(QMetaObject::invokeMethod(visual(card, QStringLiteral("resourcesToggle")), "clicked"));
    QTRY_COMPARE(model->chartRow(), 0);
    charts = model->charts();
    const QVariantMap person = charts.value("person").toMap();
    QCOMPARE(person.value("name").toString(), QString("Anna Rossi"));
    QCOMPARE(person.value("line").toString(), desktop.personLine(desktop.people.at(0)));
    QCOMPARE(person.value("progressLine").toString(), desktop.progressLine(desktop.people.at(0)));
    QCOMPARE(person.value("load").toList().size(), desktop.people.at(0).load.size());
    QTRY_VERIFY(visual(under, QStringLiteral("resourcesChartsPerson"))->isVisible());
    // Folded again: the summary.
    QVERIFY(QMetaObject::invokeMethod(visual(card, QStringLiteral("resourcesToggle")), "clicked"));
    QTRY_COMPARE(model->chartRow(), -1);

    // Live: a cost typed in the card.
    model->setText(1, QStringLiteral("cost"), QStringLiteral("60"));
    QVERIFY(model->charts().value("cost").toString() != charts.value("cost").toString());
    // FEAT-128: a person paid in dollars: a cost line and a role bar per currency.
    model->chooseCurrency(1, QStringLiteral("USD $"));
    QCOMPARE(model->charts().value("cost").toString(), model->chartFigures().costText());
    QVERIFY(model->charts().value("cost").toString().contains(QLatin1Char('\n')));
    QTRY_COMPARE(cost->property("lineCount").toInt(), 2);
    QCOMPARE(model->charts().value("roleBars").toList().size(), 2);
    QCOMPARE(model->charts().value("roleBars").toList().value(0).toMap().value("title").toString(), QStringLiteral("EUR €"));

    // Hidden by the title's button, kept in the app setting.
    QQuickItem *toggle = visual(overlay, QStringLiteral("resourcesChartsToggle"));
    QVERIFY(toggle && toggle->isVisible());
    QVERIFY(QMetaObject::invokeMethod(toggle, "toggle"));
    QVERIFY(QMetaObject::invokeMethod(toggle, "toggled"));
    QTRY_VERIFY(!model->chartsShown());
    QTRY_VERIFY(!under->isVisible());
    QCOMPARE(QSettings().value(QStringLiteral("gantt/resourceCharts")).toBool(), false);
    MobileResources fresh;
    QVERIFY(!fresh.chartsShown());
}

void MobileTest::ganttStylePage() {
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    Document *document = map.document();
    const auto plan = [&] { return document->model()->gantt(); };

    // The sheet's style (the Gantt page): one Undo step per choice.
    MobileGanttStyle style;
    style.setMap(&map);
    QVERIFY(style.values().isEmpty());  // nothing read while the page is not shown
    style.setActive(true);
    QCOMPARE(style.values().value("barShape").toInt(), int(GanttPlan::BarShape::Pill));
    QCOMPARE(style.choices(MobileGanttStyle::BarShape).size(), 8);
    QCOMPARE(style.choices(MobileGanttStyle::BarShape).value(1).toMap().value("name").toString(), QStringLiteral("Rounded rectangle"));
    QCOMPARE(style.choices(MobileGanttStyle::SummaryShape).size(), 9);
    QCOMPARE(style.choices(MobileGanttStyle::Border).last().toMap().value("value").toInt(), int(GanttPlan::BorderStyle::None));
    QCOMPARE(style.iconKind(MobileGanttStyle::Fill), int(MobileStyle::GanttIcons) + int(MobileGanttStyle::Fill));
    QVERIFY(style.choose(MobileGanttStyle::BarShape, int(GanttPlan::BarShape::Hexagon)));
    QCOMPARE(plan().barShape(), GanttPlan::BarShape::Hexagon);
    QCOMPARE(style.values().value("barShape").toInt(), int(GanttPlan::BarShape::Hexagon));
    QVERIFY(!style.choose(MobileGanttStyle::BarShape, 99));  // not a choice
    QVERIFY(style.choose(MobileGanttStyle::Fill, int(NodeFillStyle::Nessuno), int(GanttPlan::BarKind::Milestone)));
    QCOMPARE(plan().fillStyle(GanttPlan::BarKind::Milestone), NodeFillStyle::Nessuno);
    QCOMPARE(plan().fillStyle(GanttPlan::BarKind::Task), NodeFillStyle::Pieno);
    QVERIFY(style.choose(MobileGanttStyle::Border, int(GanttPlan::BorderStyle::Dashed), int(GanttPlan::BarKind::Summary)));
    QCOMPARE(plan().borderStyle(GanttPlan::BarKind::Summary), GanttPlan::BorderStyle::Dashed);
    QVERIFY(style.choose(MobileGanttStyle::LinkEnd, int(BranchEnd::Dot)));
    QVERIFY(style.choose(MobileGanttStyle::LinkWidth, int(GanttPlan::LinkWidth::Bold)));
    QCOMPARE(plan().linkEnd(), BranchEnd::Dot);
    QVERIFY(map.undo());
    QCOMPARE(plan().linkWidth(), GanttPlan::LinkWidth::Thin);
    QCOMPARE(plan().linkEnd(), BranchEnd::Dot);
    QVERIFY(!style.values().value("colorChosen").toList().value(0).toBool());
    QVERIFY(style.setColor(int(GanttPlan::BarKind::Task), QColor(10, 20, 30, 100)));
    QCOMPARE(plan().defaultColor(GanttPlan::BarKind::Task), QColor(10, 20, 30));  // opaque
    QVERIFY(style.values().value("colorChosen").toList().value(0).toBool());
    QCOMPARE(style.values().value("color").toList().value(0).value<QColor>(), QColor(10, 20, 30));
    QVERIFY(style.clearColor(int(GanttPlan::BarKind::Task)));
    QVERIFY(!plan().defaultColor(GanttPlan::BarKind::Task).isValid());
    // The sheet's border colour (user, 2026-11-22): automatic until chosen, opaque, × back.
    QVERIFY(!style.values().value("borderColorChosen").toList().value(0).toBool());
    QCOMPARE(style.values().value("borderColor").toList().value(0).value<QColor>(),
             GanttCanvas::defaultOutlineColor(plan(), GanttPlan::BarKind::Task));
    QVERIFY(style.setBorderColor(int(GanttPlan::BarKind::Task), QColor(1, 2, 3, 100)));
    QCOMPARE(plan().defaultBorderColor(GanttPlan::BarKind::Task), QColor(1, 2, 3));
    QVERIFY(style.values().value("borderColorChosen").toList().value(0).toBool());
    QVERIFY(!plan().defaultBorderColor(GanttPlan::BarKind::Milestone).isValid());
    QVERIFY(map.undo());
    QVERIFY(!plan().defaultBorderColor(GanttPlan::BarKind::Task).isValid());
    QVERIFY(map.redo());
    QVERIFY(style.clearBorderColor(int(GanttPlan::BarKind::Task)));
    QVERIFY(!style.clearBorderColor(int(GanttPlan::BarKind::Task)));
    QVERIFY(!style.setBorderColor(7, QColor(1, 2, 3)));
    // The relations' colour: the view's grey until chosen, opaque, × back.
    QVERIFY(!style.values().value("linkColorChosen").toBool());
    QCOMPARE(style.values().value("linkColor").value<QColor>(), GanttCanvas::linkColor(plan()));
    QVERIFY(style.setLinkColor(QColor(4, 5, 6, 100)));
    QCOMPARE(plan().linkColor(), QColor(4, 5, 6));
    QVERIFY(style.values().value("linkColorChosen").toBool());
    QVERIFY(map.undo());
    QVERIFY(!plan().linkColor().isValid());
    QVERIFY(map.redo());
    QVERIFY(style.clearLinkColor());
    QVERIFY(!style.clearLinkColor());

    // The task's own style (the Task page's Style section).
    MobileGantt gantt;
    gantt.setSize(QSizeF(400, 600));
    gantt.setMap(&map);
    gantt.setActive(true);
    const QString planId = gantt.rowId(0), budget = gantt.rowId(1), team = gantt.rowId(2);
    MobileGanttTask task;
    task.setMap(&map);
    task.setActive(true);
    const auto field = [&](const char *name) { return task.fields().value(QLatin1String(name)); };
    map.selectTopic(budget);
    QCOMPARE(field("styleKind").toInt(), int(GanttPlan::BarKind::Task));
    QCOMPARE(field("ownShape").toInt(), int(GanttPlan::BarShape::Hexagon));  // the sheet's
    QVERIFY(!field("ownShapeSet").toBool());
    QVERIFY(task.setOwnStyle(0, int(GanttPlan::BarShape::Arrow)));
    QCOMPARE(plan().barShape(*plan().task(budget)), GanttPlan::BarShape::Arrow);
    QVERIFY(field("ownShapeSet").toBool());
    QVERIFY(task.setOwnStyle(1, int(NodeFillStyle::TratteggioDiagonale)));
    QVERIFY(task.setOwnStyle(2, int(GanttPlan::BorderStyle::Dotted)));
    QVERIFY(field("ownFillSet").toBool() && field("ownBorderSet").toBool());
    // The sheet's shape no longer changes it.
    QVERIFY(style.choose(MobileGanttStyle::BarShape, int(GanttPlan::BarShape::Rectangle)));
    QCOMPARE(plan().barShape(*plan().task(budget)), GanttPlan::BarShape::Arrow);
    QVERIFY(task.clearOwnStyle(0));
    QCOMPARE(plan().barShape(*plan().task(budget)), GanttPlan::BarShape::Rectangle);
    QVERIFY(map.undo());
    QCOMPARE(plan().barShape(*plan().task(budget)), GanttPlan::BarShape::Arrow);
    // Colour: the topic's branch colour, × back to the default.
    QVERIFY(!field("colorManual").toBool());
    QVERIFY(task.setColor(QColor(200, 0, 0)));
    QVERIFY(field("colorManual").toBool());
    QCOMPARE(field("color").value<QColor>(), QColor(200, 0, 0));
    QVERIFY(task.clearColor());
    QVERIFY(!field("colorManual").toBool());
    // Border colour: the task's own over the sheet's, × back to the sheet's.
    QVERIFY(style.setBorderColor(int(GanttPlan::BarKind::Task), QColor(0, 0, 200)));
    QCOMPARE(field("borderColor").value<QColor>(), QColor(0, 0, 200));
    QVERIFY(!field("borderColorSet").toBool());
    QVERIFY(task.setBorderColor(QColor(0, 150, 0)));
    QVERIFY(field("borderColorSet").toBool());
    QCOMPARE(field("borderColor").value<QColor>(), QColor(0, 150, 0));
    QCOMPARE(plan().task(budget)->borderColor, QColor(0, 150, 0));
    QCOMPARE(document->model()->nodeById(budget)->ganttBorderColor, QColor(0, 150, 0));
    QVERIFY(task.clearBorderColor());
    QVERIFY(!field("borderColorSet").toBool());
    QCOMPARE(field("borderColor").value<QColor>(), QColor(0, 0, 200));
    QVERIFY(style.clearBorderColor(int(GanttPlan::BarKind::Task)));
    // A summary and a task: no shape (kinds differ); fill and border for both, shared or mixed.
    map.startMultiSelect();
    map.toggleInSelection(planId);
    QCOMPARE(field("styleKind").toInt(), -1);
    QCOMPARE(field("ownFill").toInt(), -1);
    QVERIFY(!task.setOwnStyle(0, int(GanttPlan::BarShape::Pill)));
    QVERIFY(task.setOwnStyle(1, int(NodeFillStyle::Pieno)));
    QCOMPARE(field("ownFill").toInt(), int(NodeFillStyle::Pieno));
    QCOMPARE(plan().task(planId)->fill, GanttPlan::styleKey(NodeFillStyle::Pieno));
    map.stopMultiSelect();
    Q_UNUSED(team);
}

void MobileTest::ganttStylePanel() {
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
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
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    // Repeater delegates have no QObject parent: found through the visual tree.
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *root, const QString &wanted) -> QQuickItem * {
        if (root->objectName() == wanted) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = visual(child, wanted)) return found;
        return nullptr;
    };
    const auto item = [&](const QString &name) {
        QQuickItem *found = window->findChild<QQuickItem *>(name);
        return found ? found : visual(window->contentItem(), name);
    };
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("ganttButton")), "clicked"));
    auto *gantt = window->findChild<MobileGantt *>(QStringLiteral("ganttChart"));
    QTRY_VERIFY(gantt && gantt->rowCount() == 5);
    const QString budget = gantt->rowId(1);
    map->selectTopic(budget);
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("panelButton")), "clicked"));
    QQuickItem *taskPage = nullptr;
    QTRY_VERIFY((taskPage = item(QStringLiteral("ganttTaskPage"))) && taskPage->isVisible());

    // Task | Gantt: the Gantt segment shows the Gantt page.
    auto *segments = item(QStringLiteral("panelSegments"));
    QCOMPARE(segments->property("segments").toList().size(), 2);
    QVERIFY(QMetaObject::invokeMethod(segments, "chosen", Q_ARG(int, 1)));
    QQuickItem *stylePage = nullptr;
    QTRY_VERIFY((stylePage = item(QStringLiteral("ganttStylePage"))) && stylePage->isVisible());
    QVERIFY(!taskPage->isVisible());
    QQuickItem *shape = item(QStringLiteral("ganttSheetShape0"));
    QVERIFY(shape);
    QTRY_COMPARE(shape->property("value").toInt(), int(GanttPlan::BarShape::Pill));
    QVERIFY(QMetaObject::invokeMethod(shape, "picked", Q_ARG(QVariant, int(GanttPlan::BarShape::Leaf))));
    QTRY_COMPARE(map->document()->model()->gantt().barShape(), GanttPlan::BarShape::Leaf);
    QTRY_COMPARE(shape->property("value").toInt(), int(GanttPlan::BarShape::Leaf));
    QVERIFY(stylePage->isVisible());  // the edit keeps the page
    QQuickItem *end = item(QStringLiteral("ganttSheet_linkEnd"));
    QVERIFY(end && end->property("iconKind").toInt() >= int(MobileStyle::GanttIcons));
    QQuickItem *borderColor = item(QStringLiteral("ganttSheetBorderColor0"));
    QVERIFY(borderColor && borderColor->isVisible());
    QCOMPARE(borderColor->property("resettable").toBool(), false);
    QQuickItem *linkColor = item(QStringLiteral("ganttSheetLinkColor"));
    QVERIFY(linkColor && linkColor->isVisible());
    QCOMPARE(linkColor->property("resettable").toBool(), false);

    // A tap on another task shows it on the Task page.
    map->selectTopic(gantt->rowId(2));
    QTRY_VERIFY(taskPage->isVisible());
    QTRY_VERIFY(!stylePage->isVisible());
    // The Style section: the task's own fill, × back to the sheet's.
    QQuickItem *fill = item(QStringLiteral("ganttOwnFillSelect"));
    QVERIFY(fill && fill->isVisible());
    QVERIFY(QMetaObject::invokeMethod(fill, "picked", Q_ARG(QVariant, int(NodeFillStyle::Nessuno))));
    QQuickItem *reset = item(QStringLiteral("ganttOwnFillReset"));
    QTRY_VERIFY(reset->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(reset, "clicked"));
    QTRY_VERIFY(!reset->isEnabled());
    QVERIFY(map->document()->model()->gantt().task(gantt->rowId(2))->fill.isEmpty());
    QQuickItem *ownBorderColor = item(QStringLiteral("ganttOwnBorderColor"));
    QVERIFY(ownBorderColor && ownBorderColor->isVisible());
    QVERIFY(errors.isEmpty());
}

void MobileTest::ganttBaselines() {
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MobileGantt gantt;
    gantt.setSize(QSizeF(400, 600));
    gantt.setMap(&map);
    gantt.setActive(true);
    const QString budget = gantt.rowId(1), team = gantt.rowId(2), costs = gantt.rowId(3);
    MobileGanttTask task;  // the Task page's model: its edits, then the comparison of its fields
    task.setMap(&map);
    task.setActive(true);
    QVERIFY(gantt.baselines().isEmpty());
    QCOMPARE(gantt.nextBaselineName(), QStringLiteral("Baseline 1"));

    // A snapshot is one Undo step; taking it does not compare (the desktop's).
    const QString first = gantt.addBaseline(gantt.nextBaselineName());
    QVERIFY(!first.isEmpty());
    QCOMPARE(gantt.baselines().size(), 1);
    QCOMPARE(gantt.nextBaselineName(), QStringLiteral("Baseline 2"));
    QVERIFY(gantt.comparedBaseline().isEmpty());
    QVERIFY(gantt.baselineMarkRect(budget).isEmpty());
    QVERIFY(gantt.addBaseline(QStringLiteral("   ")).isEmpty());  // an empty name is refused
    QVERIFY(map.undo());
    QVERIFY(gantt.baselines().isEmpty());
    QVERIFY(map.redo());
    QCOMPARE(gantt.baselines().size(), 1);

    // Comparing: the thin bars under the bars; Budget longer is red, a new task has the "New" pill.
    gantt.compareBaseline(first);
    QCOMPARE(gantt.comparedBaseline(), first);
    QCOMPARE(gantt.comparedName(), QStringLiteral("Baseline 1"));
    QVERIFY(!gantt.baselineMarkRect(budget).isEmpty());
    QVERIFY(gantt.baselineMarkRect(budget).top() > gantt.barRect(budget).bottom());
    map.selectTopic(budget);
    QVERIFY(task.setDuration(5));
    QCOMPARE(gantt.baselineMarkColor(budget), GanttCanvas::criticalColor());
    map.selectTopic(costs);
    const QString added = gantt.addRow(false);
    QVERIFY(gantt.isNew(added));
    QVERIFY(!gantt.isNew(budget));
    QVERIFY(gantt.removedTasks().isEmpty());

    // A task removed since is listed by name.
    map.selectTopic(team);
    map.removeSelected();
    QCOMPARE(gantt.removedTasks(), QStringList{QStringLiteral("Team")});

    // The Task page: Budget's End and Duration later and longer, the snapshot's dates under them.
    task.setBaseline(gantt.comparedBaseline());
    map.selectTopic(budget);
    QVariantMap fields = task.fields();
    QVERIFY(fields.value(QStringLiteral("baselineShown")).toBool());
    QCOMPARE(fields.value(QStringLiteral("startChange")).toInt(), 0);
    QVERIFY(fields.value(QStringLiteral("endChange")).toInt() > 0);
    QCOMPARE(fields.value(QStringLiteral("durationChange")).toInt(), 2);
    QVERIFY(fields.value(QStringLiteral("baselineText")).toString().startsWith(QStringLiteral("Baseline 1: ")));
    map.selectTopic(added);
    QVERIFY(task.fields().value(QStringLiteral("isNew")).toBool());
    task.setBaseline({});
    QVERIFY(!task.fields().value(QStringLiteral("baselineShown")).toBool());

    // Rename: one step, an empty name refused; the compared one keeps comparing.
    QVERIFY(gantt.renameBaseline(first, QStringLiteral("Kick-off")));
    QCOMPARE(gantt.comparedName(), QStringLiteral("Kick-off"));
    QVERIFY(!gantt.renameBaseline(first, QStringLiteral(" ")));
    QVERIFY(map.undo());
    QCOMPARE(gantt.comparedName(), QStringLiteral("Baseline 1"));

    // Removing the compared snapshot stops comparing; its Undo does not bring the comparison back.
    QVERIFY(gantt.removeBaseline(first));
    QVERIFY(gantt.comparedBaseline().isEmpty());
    QVERIFY(gantt.baselineMarkRect(budget).isEmpty());
    QVERIFY(map.undo());
    QCOMPARE(gantt.baselines().size(), 1);
    QVERIFY(gantt.comparedBaseline().isEmpty());

    // Another sheet stops it too.
    gantt.compareBaseline(first);
    QCOMPARE(gantt.comparedBaseline(), first);
    map.addSheet();
    QVERIFY(gantt.comparedBaseline().isEmpty());
    QVERIFY(map.switchSheet(0));
    QVERIFY(gantt.comparedBaseline().isEmpty());
    QCOMPARE(gantt.baselines().size(), 1);  // saved with the sheet
}

void MobileTest::ganttBaselinesDialog() {
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
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
    auto *stack = window->findChild<QQuickItem *>(QStringLiteral("stack"));
    QTRY_VERIFY(stack && !stack->property("busy").toBool());
    const auto item = [&](const QString &name) { return window->findChild<QQuickItem *>(name); };
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *root, const QString &wanted) -> QQuickItem * {
        if (root->objectName() == wanted) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = visual(child, wanted)) return found;
        return nullptr;
    };
    QQuickItem *overlay = window->contentItem()->parentItem() ? window->contentItem()->parentItem() : window->contentItem();
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("ganttButton")), "clicked"));
    auto *gantt = window->findChild<MobileGantt *>(QStringLiteral("ganttChart"));
    QTRY_VERIFY(gantt && gantt->rowCount() == 5);
    const QString budget = gantt->rowId(1);

    // The bottom group's Baseline opens the modal with the default name; Save takes a snapshot.
    QQuickItem *button = item(QStringLiteral("ganttBaseline"));
    QVERIFY(button && button->isVisible());
    QVERIFY(!button->property("checked").toBool());
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    auto *dialog = window->findChild<QObject *>(QStringLiteral("baselinesDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    QQuickItem *name = visual(overlay, QStringLiteral("baselineName"));
    QVERIFY(name);
    QCOMPARE(name->property("text").toString(), QStringLiteral("Baseline 1"));
    QVERIFY(visual(overlay, QStringLiteral("baselineEmpty"))->isVisible());
    QVERIFY(QMetaObject::invokeMethod(visual(overlay, QStringLiteral("baselineSave")), "clicked"));
    QTRY_COMPARE(gantt->baselines().size(), 1);
    QTRY_COMPARE(name->property("text").toString(), QStringLiteral("Baseline 2"));

    // A tap on the row compares: the button pressed, the legend shown; a second tap stops.
    QQuickItem *row = nullptr;
    QTRY_VERIFY((row = visual(overlay, QStringLiteral("baselineRow0"))));
    QVERIFY(QMetaObject::invokeMethod(row, "clicked"));
    QTRY_VERIFY(!gantt->comparedBaseline().isEmpty());
    QVERIFY(button->property("checked").toBool());
    QVERIFY(visual(overlay, QStringLiteral("baselineLegend"))->isVisible());
    QVERIFY(QMetaObject::invokeMethod(row, "clicked"));
    QTRY_VERIFY(gantt->comparedBaseline().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(row, "clicked"));
    QTRY_VERIFY(!gantt->comparedBaseline().isEmpty());

    // ✎ renames through its modal.
    QVERIFY(QMetaObject::invokeMethod(visual(overlay, QStringLiteral("baselineRename0")), "clicked"));
    auto *rename = window->findChild<QObject *>(QStringLiteral("baselineRenameDialog"));
    QTRY_VERIFY(rename && rename->property("opened").toBool());
    visual(overlay, QStringLiteral("baselineRenameField"))->setProperty("text", QStringLiteral("Kick-off"));
    QVERIFY(QMetaObject::invokeMethod(rename, "accept"));
    QTRY_COMPARE(gantt->comparedName(), QStringLiteral("Kick-off"));
    QVERIFY(QMetaObject::invokeMethod(visual(overlay, QStringLiteral("baselinesDone")), "clicked"));
    QTRY_VERIFY(!dialog->property("visible").toBool());

    // The Task page colours a longer duration red, the snapshot's dates under the dates.
    map->selectTopic(budget);
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("panelButton")), "clicked"));
    MobileGanttTask *task = nullptr;
    QTRY_VERIFY((task = window->findChild<MobileGanttTask *>()) && task->active());
    QVERIFY(task->setDuration(5));
    QQuickItem *caption = nullptr;
    QTRY_VERIFY((caption = item(QStringLiteral("ganttTaskBaseline"))) && caption->isVisible());
    QVERIFY(caption->property("text").toString().startsWith(QStringLiteral("Kick-off: ")));
    QQuickItem *duration = item(QStringLiteral("ganttTaskDuration"));
    QCOMPARE(duration->property("valueColor").value<QColor>(), QColor(0xe5, 0x48, 0x4d));
    QTest::keyClick(window, Qt::Key_Back);  // closes the panel

    // Removing the compared snapshot stops comparing (the row menu's Baseline opens the modal too).
    QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("ganttBaselineRow")), "clicked"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(visual(overlay, QStringLiteral("baselineRemove0")), "clicked"));
    QTRY_VERIFY(gantt->baselines().isEmpty());
    QVERIFY(gantt->comparedBaseline().isEmpty());
    QTRY_VERIFY(visual(overlay, QStringLiteral("baselineEmpty"))->isVisible());
    for (const QQmlError &error : std::as_const(errors)) qWarning() << error.toString();
    QVERIFY(errors.isEmpty());
}

void MobileTest::ganttExports() {
    QSettings().remove(QStringLiteral("gantt/columns"));  // the desktop's default columns: Name, Start, Duration
    const auto columns = qScopeGuard([] { QSettings().setValue(QStringLiteral("gantt/columns"), QStringList()); });
    QTemporaryDir storage;
    const QString path = QDir(storage.path()).filePath(QStringLiteral("Plan.mentis"));
    QCOMPARE(writeSearchMap(path), QString());
    {  // a third sheet with only its central topic: no Gantt
        Document document;
        QVERIFY(document.open(path, false).ok);
        document.addSheet();
        QVERIFY(document.save(path, BmapStorage::SaveReason::Explicit).ok);
        document.close();
    }
    UrlCatcher system;
    QDesktopServices::setUrlHandler(QStringLiteral("file"), &system, "open");
    const auto unsetHandler = qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("file")); });
    const auto readAll = [](const QString &file) {
        QFile input(file);
        return input.open(QIODevice::ReadOnly) ? input.readAll() : QByteArray();
    };
    {
        MapLibrary library(storage.path(), nullptr);
        MapExport exporter;
        exporter.setLibrary(&library);
        QVERIFY(exporter.prepare(QUrl::fromLocalFile(path)));
        QCOMPARE(exporter.sheets().size(), 3);
        QVERIFY(exporter.hasGantt(0) && exporter.hasGantt(1));
        QVERIFY(!exporter.hasGantt(2));
        QVERIFY(!exporter.hasGantt(7));

        // The columns: the desktop's default ones (not numbered: no No.).
        using Field = GanttExportSource::Field;
        const QVariantMap png{{QStringLiteral("gantt"), true}, {QStringLiteral("sheet"), 0}, {QStringLiteral("scale"), 1}};
        QVERIFY2(exporter.runImage(png), qPrintable(exporter.lastError()));
        const QImage one(exporter.lastFile());
        QVERIFY(!one.isNull());
        QVariantMap twice = png;
        twice.insert(QStringLiteral("scale"), 2);
        QVERIFY(exporter.runImage(twice));
        const QImage two(exporter.lastFile());
        QVERIFY(std::abs(two.width() - 2 * one.width()) <= 2);  // rounded once per scale
        // Gantt and costs: the cost tables under the chart, a taller picture.
        QVariantMap costs = png;
        costs.insert(QStringLiteral("content"), int(GanttPages::Content::GanttAndCosts));
        QVERIFY(exporter.runImage(costs));
        QVERIFY(QImage(exporter.lastFile()).height() > one.height());
        QVariantMap svg = png;
        svg.insert(QStringLiteral("svg"), true);
        QVERIFY(exporter.runImage(svg));
        const QByteArray svgText = readAll(exporter.lastFile());
        QVERIFY(svgText.contains("Plan"));
        QVERIFY(!svgText.contains("Budget 2027"));  // sheet 0's tasks only
        QVERIFY(svgText.contains("Duration"));  // the default columns' header
        QVERIFY(!exporter.runImage({{QStringLiteral("gantt"), true}, {QStringLiteral("sheet"), -1}}));  // one sheet's Gantt

        // PDF: pages of the chart, landscape A4 by default, the costs, framed and monochrome.
        QVERIFY2(exporter.runPdf({{QStringLiteral("gantt"), true}, {QStringLiteral("sheet"), 1}, {QStringLiteral("pageSize"), int(MapPages::PageSize::A4)}}),
                 qPrintable(exporter.lastError()));
        QVERIFY(readAll(exporter.lastFile()).startsWith("%PDF"));
        QVERIFY(QFileInfo(exporter.lastFile()).completeBaseName().endsWith(exporter.sheets().at(1)));
        QVERIFY(exporter.runPdf({{QStringLiteral("gantt"), true}, {QStringLiteral("sheet"), 0}, {QStringLiteral("content"), int(GanttPages::Content::Costs)},
                                 {QStringLiteral("monochrome"), true}, {QStringLiteral("frame"), QVariantMap{{QStringLiteral("style"), 1}}}}));
        QVERIFY(readAll(exporter.lastFile()).startsWith("%PDF"));
        // The note: nothing to say for a short plan in days on A4.
        QCOMPARE(exporter.ganttNote({{QStringLiteral("sheet"), 0}, {QStringLiteral("pageSize"), int(MapPages::PageSize::A4)}}), QString());
        QCOMPARE(exporter.ganttNote({{QStringLiteral("sheet"), 2}}), QString());

        // Word, Excel, Markdown of one sheet's Gantt.
        system.urls.clear();
        QVERIFY2(exporter.runGanttFile(MapExport::Word, 0, 0, 0), qPrintable(exporter.lastError()));
        QCOMPARE(QFileInfo(exporter.lastFile()).suffix(), QStringLiteral("docx"));
        QVERIFY(readAll(exporter.lastFile()).startsWith("PK"));
        QVERIFY(exporter.runGanttFile(MapExport::Excel, 0, int(GanttPages::Content::GanttAndCosts), 1));
        QCOMPARE(QFileInfo(exporter.lastFile()).suffix(), QStringLiteral("xlsx"));
        QVERIFY(readAll(exporter.lastFile()).startsWith("PK"));
        QVERIFY(exporter.runGanttFile(MapExport::Markdown, 1, 0, 0));
        QCOMPARE(QFileInfo(exporter.lastFile()).suffix(), QStringLiteral("md"));
        const QString markdown = QString::fromUtf8(readAll(exporter.lastFile()));
        QVERIFY2(markdown.contains(QStringLiteral("Budget 2027")) && markdown.contains(QStringLiteral("Duration")), qPrintable(markdown));
        QCOMPARE(system.urls.size(), 3);
        QVERIFY(!exporter.runGanttFile(MapExport::PitchPdf, 0, 0, 0));
        QVERIFY(!exporter.runGanttFile(MapExport::Word, 5, 0, 0));
        exporter.finish();

        // The source of the default columns, from a document.
        Document document;
        QVERIFY(document.open(path, false).ok);
        const QVector<Field> fields = GanttExportSource::defaultFields(document);
        QCOMPARE(fields, (QVector<Field>{Field::Name, Field::Start, Field::Duration}));
        const GanttExportSource source = GanttExportSource::fromDocument(document, fields);
        QCOMPARE(source.columns.size(), 3);
        QCOMPARE(source.rows.size(), 4);  // Plan, its folded subtask, Costs, Money
        QCOMPARE(source.rows.at(0).cells.at(0), QStringLiteral("Plan"));
        QVERIFY(source.rows.at(0).summary);
        QCOMPARE(source.rows.at(1).depth, 1);
        QCOMPARE(source.rows.at(1).cells.at(0), QStringLiteral("Budget <draft>"));
        document.close();
    }

    // The page: PDF… with the Gantt tab, then Word… asking Mind map or Gantt, then the sheet.
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QObject *dialog = window->findChild<QObject *>(QStringLiteral("mapExportSheet"));
    QVERIFY(dialog);
    QQuickItem *top = window->contentItem()->parentItem() ? window->contentItem()->parentItem() : window->contentItem();
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *root, const QString &name) -> QQuickItem * {
        if (root->objectName() == name) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = visual(child, name)) return found;
        return nullptr;
    };
    system.urls.clear();
    QVERIFY(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(visual(top, QStringLiteral("exportPdf")), "clicked"));
    QObject *options = window->findChild<QObject *>(QStringLiteral("exportOptionsSheet"));
    QTRY_VERIFY(options && options->property("opened").toBool());
    QVERIFY(options->property("ganttAvailable").toBool());
    QQuickItem *kind = visual(top, QStringLiteral("exportKind"));
    QVERIFY(kind);
    QVERIFY(QMetaObject::invokeMethod(kind, "chosen", Q_ARG(int, 2)));
    QVERIFY(options->property("gantt").toBool());
    QVERIFY(options->property("ganttLandscape").toBool());
    QVERIFY(visual(top, QStringLiteral("exportGanttContent"))->isVisible());
    QVERIFY(!visual(top, QStringLiteral("exportBackground"))->isVisible());
    // A sheet without tasks turns the tab off.
    QVERIFY(options->setProperty("sheetIndex", 2));
    QVERIFY(QMetaObject::invokeMethod(options, "checkGantt"));
    QVERIFY(!options->property("gantt").toBool() && !options->property("ganttAvailable").toBool());
    QVERIFY(options->setProperty("sheetIndex", 0));
    QVERIFY(QMetaObject::invokeMethod(options, "checkGantt"));
    QVERIFY(QMetaObject::invokeMethod(kind, "chosen", Q_ARG(int, 2)));
    QVERIFY(QMetaObject::invokeMethod(visual(top, QStringLiteral("exportRun")), "clicked"));
    QTRY_VERIFY(!dialog->property("opened").toBool());
    QVERIFY2(options->property("error").toString().isEmpty(), qPrintable(options->property("error").toString()));
    QCOMPARE(system.urls.size(), 1);

    system.urls.clear();
    QVERIFY(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(visual(top, QStringLiteral("exportWord")), "clicked"));
    QCOMPARE(dialog->property("step").toString(), QStringLiteral("office"));
    QCOMPARE(dialog->property("ganttSheets").toList().size(), 2);
    QVERIFY(QMetaObject::invokeMethod(visual(top, QStringLiteral("exportOfficeKind")), "chosen", Q_ARG(int, 1)));
    QVERIFY(QMetaObject::invokeMethod(visual(top, QStringLiteral("exportOfficeRun")), "clicked"));
    QCOMPARE(dialog->property("step").toString(), QStringLiteral("sheets"));
    QTRY_VERIFY(visual(top, QStringLiteral("exportSheet1")));
    QVERIFY(!visual(top, QStringLiteral("exportSheet2")));  // no Gantt there
    QVERIFY(QMetaObject::invokeMethod(visual(top, QStringLiteral("exportSheet1")), "clicked"));
    QTRY_VERIFY(!dialog->property("opened").toBool());
    QCOMPARE(system.urls.size(), 1);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).toString()));
}

void MobileTest::outlinerExports() {
    QTemporaryDir storage;
    const QString path = QDir(storage.path()).filePath(QStringLiteral("Outline.mentis"));
    QCOMPARE(writeSearchMap(path), QString());
    UrlCatcher system;
    QDesktopServices::setUrlHandler(QStringLiteral("file"), &system, "open");
    const auto unsetHandler = qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("file")); });
    const auto readAll = [](const QString &file) {
        QFile input(file);
        return input.open(QIODevice::ReadOnly) ? input.readAll() : QByteArray();
    };
    // The page size of a PDF's first page, in points.
    const auto mediaBox = [](const QByteArray &pdf) {
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral(R"(/MediaBox \[0 0 ([0-9.]+) ([0-9.]+)\])")).match(QString::fromLatin1(pdf));
        return m.hasMatch() ? QSizeF(m.captured(1).toDouble(), m.captured(2).toDouble()) : QSizeF();
    };
    {
        MapLibrary library(storage.path(), nullptr);
        MapExport exporter;
        exporter.setLibrary(&library);
        QVERIFY(exporter.prepare(QUrl::fromLocalFile(path)));
        // The map at Fit to map: a page of the map's size.
        QVERIFY2(exporter.runPdf({{QStringLiteral("sheet"), 0}}), qPrintable(exporter.lastError()));
        const QSizeF mapPage = mediaBox(readAll(exporter.lastFile()));
        QVERIFY(mapPage.isValid());
        // The outline: on A4 even with Fit to map (the desktop's), landscape as asked, every sheet,
        // monochrome and framed.
        system.urls.clear();
        QVERIFY2(exporter.runPdf({{QStringLiteral("outliner"), true}, {QStringLiteral("sheet"), 0}}), qPrintable(exporter.lastError()));
        const QSizeF a4 = mediaBox(readAll(exporter.lastFile()));
        QVERIFY2(std::abs(a4.width() - 595.3) < 1 && std::abs(a4.height() - 841.9) < 1, qPrintable(QStringLiteral("%1 x %2").arg(a4.width()).arg(a4.height())));
        QVERIFY(a4 != mapPage);
        QCOMPARE(system.urls.size(), 1);
        QVERIFY(exporter.runPdf({{QStringLiteral("outliner"), true}, {QStringLiteral("sheet"), 0},
                                 {QStringLiteral("pageSize"), int(MapPages::PageSize::A4)}, {QStringLiteral("landscape"), true}}));
        QVERIFY(mediaBox(readAll(exporter.lastFile())).width() > 800);
        QVERIFY(exporter.runPdf({{QStringLiteral("outliner"), true}, {QStringLiteral("sheet"), -1}, {QStringLiteral("monochrome"), true},
                                 {QStringLiteral("frame"), QVariantMap{{QStringLiteral("style"), int(ExportFrame::Style::Solid)}}}}));
        const QByteArray every = readAll(exporter.lastFile());
        QVERIFY(every.startsWith("%PDF"));
        QVERIFY(every.count("/Type /Page\n") + every.count("/Type /Page ") + every.count("/Type /Page/") >= 2);  // a page per sheet at least
        exporter.finish();
    }

    // The page: PDF…'s Outliner tab, the page size off Fit to map, no Scale or Background, Share.
    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) { errors.append(warnings); });
    engine.loadFromModule("Mentis.Mobile", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QObject *dialog = window->findChild<QObject *>(QStringLiteral("mapExportSheet"));
    QVERIFY(dialog);
    QQuickItem *top = window->contentItem()->parentItem() ? window->contentItem()->parentItem() : window->contentItem();
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visual = [&](QQuickItem *root, const QString &name) -> QQuickItem * {
        if (root->objectName() == name) return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = visual(child, name)) return found;
        return nullptr;
    };
    system.urls.clear();
    QVERIFY(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(visual(top, QStringLiteral("exportPdf")), "clicked"));
    QObject *options = window->findChild<QObject *>(QStringLiteral("exportOptionsSheet"));
    QTRY_VERIFY(options && options->property("opened").toBool());
    QCOMPARE(options->property("pageSize").toInt(), 0);  // Fit to map
    QQuickItem *kind = visual(top, QStringLiteral("exportKind"));
    QVERIFY(kind);
    QVERIFY(QMetaObject::invokeMethod(kind, "chosen", Q_ARG(int, 1)));
    QVERIFY(options->property("outline").toBool());
    QCOMPARE(options->property("pageSize").toInt(), int(MapPages::PageSize::A4));
    const QVariantList sizes = visual(top, QStringLiteral("exportPageSize"))->property("choices").toList();
    QCOMPARE(sizes.size(), 5);  // no Fit to map
    QVERIFY(!visual(top, QStringLiteral("exportScale"))->isEnabled());
    QQuickItem *background = visual(top, QStringLiteral("exportBackground"));
    QVERIFY(background->isVisible() && !background->isEnabled());
    QVERIFY(visual(top, QStringLiteral("exportScope"))->isVisible());
    QVERIFY(QMetaObject::invokeMethod(visual(top, QStringLiteral("exportRun")), "clicked"));
    QTRY_VERIFY(!dialog->property("opened").toBool());
    QVERIFY2(options->property("error").toString().isEmpty(), qPrintable(options->property("error").toString()));
    QCOMPARE(system.urls.size(), 1);
    // Back to Mind map: Fit to map offered again, the A4 kept (as the desktop's).
    QVERIFY(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(path)))));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(visual(top, QStringLiteral("exportPdf")), "clicked"));
    QTRY_VERIFY(options->property("opened").toBool());
    QVERIFY(!options->property("outline").toBool());  // each opening starts on Mind map
    QCOMPARE(visual(top, QStringLiteral("exportPageSize"))->property("choices").toList().size(), 6);
    QVERIFY(visual(top, QStringLiteral("exportBackground"))->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(options, "close"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).toString()));
}

void MobileTest::ganttColumns() {
    GanttToday::setForTests(kGanttMonday);
    const auto today = qScopeGuard([] { GanttToday::setForTests({}); });
    const auto clean = [] {
        for (const char *key : {"gantt/columns", "gantt/columnOrder", "gantt/numberHidden", "gantt/columnWidths"}) QSettings().remove(QLatin1String(key));
    };
    clean();
    const auto cleanup = qScopeGuard([&clean] {
        clean();
        QSettings().setValue(QStringLiteral("gantt/columns"), QStringList());  // as initTestCase
    });
    using Field = GanttColumns::Field;
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("Gantt.mentis"));
    QCOMPARE(writeGanttMap(path), QString());
    MobileMap map;
    QVERIFY(map.open(path));
    MobileGantt gantt;
    gantt.setSize(QSizeF(400, 600));
    gantt.setMap(&map);
    gantt.setActive(true);

    // The desktop's defaults: Name, Start, Duration frozen, the chart beside them; the cells' texts
    // are the desktop table's.
    QCOMPARE(gantt.shownFields(), (QVariantList{int(Field::Name), int(Field::Start), int(Field::Duration)}));
    QCOMPARE(gantt.nameWidth(), 160.0);
    QCOMPARE(gantt.tableWidth(), 160.0 + 96 + 72);
    QCOMPARE(gantt.columnLeft(int(Field::Duration)), 160.0 + 96);
    const QString budget = gantt.rowId(1);
    QCOMPARE(gantt.cellText(1, int(Field::Start)), QLocale().toString(kGanttMonday, QLocale::ShortFormat));
    QCOMPARE(gantt.cellText(1, int(Field::Duration)), QStringLiteral("3"));
    QVERIFY(gantt.barRect(budget).left() > gantt.tableWidth());
    gantt.tapAt(gantt.nameWidth() + 20, gantt.barRect(budget).center().y());  // a cell selects its row too
    QCOMPARE(map.selectedId(), budget);

    // No. applies only to a numbered map: its row is disabled, showing it does nothing.
    const QVariantList choices = gantt.columns();
    QCOMPARE(choices.size(), 7);
    QCOMPARE(choices.first().toMap().value(QStringLiteral("field")).toInt(), int(Field::Number));
    QVERIFY(!choices.first().toMap().value(QStringLiteral("enabled")).toBool());
    gantt.setColumnShown(int(Field::Number), true);
    QVERIFY(!gantt.shownFields().contains(int(Field::Number)));

    // Shown, moved, hidden: the app's settings, as the desktop's; no Undo step.
    const bool undo = map.canUndo();
    QSignalSpy changed(&gantt, &MobileGantt::columnsChanged);
    gantt.setColumnShown(int(Field::Progress), true);
    QVERIFY(!changed.isEmpty());
    QCOMPARE(gantt.shownFields(), (QVariantList{int(Field::Name), int(Field::Start), int(Field::Duration), int(Field::Progress)}));
    QCOMPARE(QSettings().value(QStringLiteral("gantt/columns")).toStringList(), (QStringList{"start", "duration", "progress"}));
    QCOMPARE(gantt.cellText(1, int(Field::Progress)), QStringLiteral("0%"));
    // Too wide: Name narrows so the chart keeps 48 px.
    gantt.setSize(QSizeF(440, 600));
    QCOMPARE(gantt.tableWidth(), 440.0 - MobileGantt::kMinChart);
    QCOMPARE(gantt.nameWidth(), 440.0 - MobileGantt::kMinChart - 96 - 72 - 96);
    gantt.setSize(QSizeF(400, 600));
    // A column's width dragged on its grip (user's request 2026-11-23): the desktop's setting, at
    // least 48 px, never so wide that Name goes under 96 px or the chart under 48.
    gantt.setColumnWidth(int(Field::Duration), 60, false);
    QCOMPARE(gantt.columnWidth(int(Field::Duration)), 60.0);
    QVERIFY(!QSettings().contains(QStringLiteral("gantt/columnWidths")));
    gantt.setColumnWidth(int(Field::Duration), 10, true);
    QCOMPARE(gantt.columnWidth(int(Field::Duration)), qreal(GanttColumns::kMinWidth));
    QCOMPARE(GanttColumns::width(Field::Duration), GanttColumns::kMinWidth);
    gantt.setColumnWidth(int(Field::Duration), 5000, true);
    QCOMPARE(gantt.columnWidth(int(Field::Duration)), 400.0 - MobileGantt::kMinChart - MobileGantt::kMinName - 96 - 96);
    QCOMPARE(gantt.nameWidth(), qreal(MobileGantt::kMinName));
    gantt.setColumnWidth(int(Field::Duration), 72, true);
    gantt.moveColumn(3, 0);  // % (fourth in the order) first
    QCOMPARE(gantt.shownFields(), (QVariantList{int(Field::Name), int(Field::Progress), int(Field::Start), int(Field::Duration)}));
    QCOMPARE(QSettings().value(QStringLiteral("gantt/columnOrder")).toStringList().value(0), QStringLiteral("progress"));
    QCOMPARE(GanttColumns::order().first(), Field::Progress);
    gantt.setColumnShown(int(Field::Start), false);
    gantt.setColumnShown(int(Field::Duration), false);
    gantt.setColumnShown(int(Field::Progress), false);
    QCOMPARE(gantt.shownFields(), (QVariantList{int(Field::Name)}));
    QCOMPARE(gantt.tableWidth(), 160.0);
    QCOMPARE(map.canUndo(), undo);

    // A tap on the table's header asks for the Columns modal; on the chart's it does not.
    QSignalSpy header(&gantt, &MobileGantt::headerTapped);
    gantt.tapAt(20, 10);
    QCOMPARE(header.size(), 1);
    gantt.tapAt(300, 10);
    QCOMPARE(header.size(), 1);

    // A numbered map (FEAT-024, the numbers computed on the topics): No. first, before Name, as wide as
    // its numbers; hidden by the user it stays hidden. The exports take the shown columns.
    for (const auto &child : map.document()->model()->root()->children) child->number = QStringLiteral("12");
    gantt.setActive(false);
    gantt.setActive(true);
    QVERIFY(gantt.columns().first().toMap().value(QStringLiteral("enabled")).toBool());
    QCOMPARE(gantt.shownFields(), (QVariantList{int(Field::Number), int(Field::Name)}));
    QCOMPARE(gantt.cellText(0, int(Field::Number)), QStringLiteral("12"));
    QVERIFY(gantt.columnLeft(int(Field::Name)) > 0);
    QCOMPARE(gantt.tableWidth(), gantt.columnLeft(int(Field::Name)) + gantt.nameWidth());
    gantt.setColumnShown(int(Field::Number), false);
    QCOMPARE(gantt.shownFields(), (QVariantList{int(Field::Name)}));
    QVERIFY(QSettings().value(QStringLiteral("gantt/numberHidden")).toBool());
    gantt.setColumnShown(int(Field::Priority), true);
    const GanttExportSource exported = GanttExportSource::fromDocument(*map.document(), GanttColumns::shownFields(true));
    QCOMPARE(exported.columns.size(), 2);
    QCOMPARE(exported.columns.at(1).title, GanttColumns::title(Field::Priority));
    map.close();
}

QTEST_MAIN(MobileTest)
#include "mobile_test.moc"
