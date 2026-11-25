// FEAT-096 stage 4: the core (MentisDomain) lays a map out by itself, without Qt Widgets. This test links
// MentisDomain only, so it runs under QGuiApplication: the build fails if the core needs a widget
// again. Every map of tests/layout_fixtures.h (but Justify, which measures the items again) is built
// without summaries, boundaries and formulas, measured with NodeGeometry and laid out with MapLayout;
// the positions must be those the desktop scene gives (recorded by tests/layout_regression_test.cpp under
// "plain-<map>" in tests/data/layout_regression/<kernel>-qt<version>.json; another platform or Qt skips).
#include "layout_fixtures.h"
#include "mapstructure.h"
#include "maplayout.h"
#include "nodegeometry.h"
#include "performance.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTest>

#include <functional>
#include <memory>
#include <unordered_map>

using LayoutFixtures::Fixture;

namespace {

constexpr qreal kTolerancePx = 0.01;

QString goldenPath() {
    return QStringLiteral(MENTIS_TEST_DATA_DIR "/layout_regression/%1-qt%2.json").arg(QSysInfo::kernelType(), QString::fromLatin1(qVersion()));
}

}  // namespace

class DomainLayoutTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void noWidgetsInTheCore();
    void layoutMatchesTheDesktop();
    void textKeyFollowsThePadding();
    void effectiveDirectionsChangeTheLayout();
};

void DomainLayoutTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisDomainLayoutTest"));
    Performance::setMode(Performance::Mode::Standard);
}

void DomainLayoutTest::noWidgetsInTheCore() {
    // QTEST_MAIN makes a QApplication only when Qt Widgets is linked.
    QVERIFY(qobject_cast<QGuiApplication *>(QCoreApplication::instance()));
    QVERIFY(!QCoreApplication::instance()->inherits("QApplication"));
}

void DomainLayoutTest::layoutMatchesTheDesktop() {
    QFile file(goldenPath());
    if (!file.exists()) QSKIP(qPrintable(QStringLiteral("No recorded layout at %1").arg(file.fileName())));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
    const QJsonObject recorded = QJsonDocument::fromJson(file.readAll()).object();

    QStringList differences;
    int maps = 0;
    for (const Fixture &fixture : LayoutFixtures::all()) {
        if (fixture.content == Fixture::Content::Justify) continue;
        const QJsonObject expected = recorded.value(QStringLiteral("plain-") + fixture.name).toObject();
        QVERIFY2(!expected.isEmpty(), qPrintable(fixture.name + QStringLiteral(": not recorded")));
        Document document;
        MindMapModel &model = *document.model();
        LayoutFixtures::build(document, fixture, nullptr);
        // NodeItem's measuring steps, in its order (constructor and syncFromNode).
        std::unordered_map<const MindNode *, std::unique_ptr<NodeGeometry>> geometries;
        const std::function<void(const MindNode *)> measure = [&](const MindNode *node) {
            auto geometry = std::make_unique<NodeGeometry>(node);
            geometry->setHasImage(!node->imagePath.isEmpty() || !node->formula.isEmpty());
            geometry->updateText();
            geometry->updateNotePreview(QFont());
            geometry->updateLabelLayout();
            geometries[node] = std::move(geometry);
            for (const auto &child : node->children) measure(child.get());
        };
        measure(model.root());
        MapLayout layout(&model);
        layout.geometryOf = [&](const MindNode *node) -> const NodeGeometry * {
            const auto it = geometries.find(node);
            return it == geometries.end() ? nullptr : it->second.get();
        };
        layout.nodeById = [&](const QString &id) -> const MindNode * { return model.nodeById(id); };
        layout.collectBoundaryRuns();
        layout.computeAutoPositions();
        const QHash<MindNode *, QPointF> &positions = layout.autoPositions();
        if (positions.size() != expected.size())
            differences.append(QStringLiteral("%1: %2 topics laid out, %3 shown by the desktop").arg(fixture.name).arg(positions.size()).arg(expected.size()));
        for (auto it = positions.cbegin(); it != positions.cend(); ++it) {
            const QString key = LayoutFixtures::keyOf(it.key());
            const QJsonArray at = expected.value(key).toArray();
            if (at.size() != 2) { differences.append(QStringLiteral("%1: %2 not shown by the desktop").arg(fixture.name, key)); continue; }
            const QPointF desktop(at[0].toDouble(), at[1].toDouble());
            if (QLineF(it.value(), desktop).length() > kTolerancePx)
                differences.append(QStringLiteral("%1: %2 at (%3, %4), the desktop at (%5, %6)").arg(fixture.name, key)
                                       .arg(it.value().x()).arg(it.value().y()).arg(desktop.x()).arg(desktop.y()));
        }
        document.close();
        NodeGeometry::showAllNotes() = false;
        ++maps;
    }
    QCOMPARE(maps, 24);
    QVERIFY2(differences.isEmpty(), qPrintable(differences.mid(0, 20).join(QLatin1Char('\n'))));
}

namespace {
// The automatic positions of every topic, each measured as NodeItem does.
QList<QPointF> laidOut(MindMapModel &model) {
    std::unordered_map<const MindNode *, std::unique_ptr<NodeGeometry>> geometries;
    QList<const MindNode *> order;
    const std::function<void(const MindNode *)> measure = [&](const MindNode *node) {
        auto geometry = std::make_unique<NodeGeometry>(node);
        geometry->updateText();
        geometry->updateNotePreview(QFont());
        geometry->updateLabelLayout();
        geometries[node] = std::move(geometry);
        order.append(node);
        for (const auto &child : node->children) measure(child.get());
    };
    measure(model.root());
    MapLayout layout(&model);
    layout.geometryOf = [&](const MindNode *node) -> const NodeGeometry * {
        const auto it = geometries.find(node);
        return it == geometries.end() ? nullptr : it->second.get();
    };
    layout.nodeById = [&](const QString &id) -> const MindNode * { return model.nodeById(id); };
    layout.collectBoundaryRuns();
    layout.computeAutoPositions();
    QList<QPointF> positions;
    for (const MindNode *node : std::as_const(order)) positions.append(layout.autoPositions().value(const_cast<MindNode *>(node)));
    return positions;
}

bool sameLayout(const QList<QPointF> &a, const QList<QPointF> &b) {
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i) if (QLineF(a[i], b[i]).length() > kTolerancePx) return false;
    return true;
}

void addBranch(MindMapModel &model, MindNode *parent, const QString &title) {
    MindNode *branch = model.addChild(parent, title);
    for (int i = 0; i < 3; ++i) {
        MindNode *child = model.addChild(branch, title + QString::number(i));
        for (int j = 0; j < 2; ++j) model.addChild(child, title + QString::number(i) + QString::number(j));
    }
}
}  // namespace

// The pickers offer only the directions that change something (user request 2026-11-24: choosing some
// did nothing): in every place a topic can be, with every sheet and branch type, the offered directions
// give different layouts and every other direction gives one of theirs (all the same when none is offered).
void DomainLayoutTest::effectiveDirectionsChangeTheLayout() {
    using MapStructure::Type;
    const QList<Type> types{Type::MindMap, Type::LogicChart, Type::OrgChart, Type::TreeChart,
                            Type::Brace, Type::Timeline, Type::Fishbone, Type::TreeTable};
    QStringList failures;
    // where: 0 the central topic, 1 a main topic, 2 a topic under a main topic, 3 a floating topic's child.
    for (const Type sheet : types) {
      for (const MapStructure::Direction sheetDirection : MapStructure::directions(sheet)) {
        for (int where = 0; where < 4; ++where) {
            for (const Type type : types) {
                if (where == 0 && (type != sheet || sheetDirection != MapStructure::directions(sheet).front())) continue;
                MindMapModel model;
                MindNode *root = model.root();
                for (int i = 0; i < 4; ++i) addBranch(model, root, QStringLiteral("M%1").arg(i));
                MindNode *floating = model.addChild(root, QStringLiteral("F"));
                floating->floating = true;
                addBranch(model, floating, QStringLiteral("G"));
                root->structure = MapStructure::typeKey(sheet);
                root->structureDirection = MapStructure::directionKey(sheetDirection);
                MindNode *node = where == 0 ? root
                               : where == 1 ? root->children.front().get()
                               : where == 2 ? root->children.front()->children.front().get()
                               : floating->children.front().get();
                if (MapStructure::insideTreeTable(node) && type == Type::MindMap) continue;  // not offered there
                node->structure = MapStructure::typeKey(type);
                const QList<MapStructure::Direction> offered = MapStructure::effectiveDirections(node, type);
                QList<QList<QPointF>> offeredLayouts;
                QList<QList<QPointF>> otherLayouts;
                for (const MapStructure::Direction direction : MapStructure::directions(type)) {
                    node->structureDirection = MapStructure::directionKey(direction);
                    (offered.contains(direction) ? offeredLayouts : otherLayouts).append(laidOut(model));
                }
                const QString at = QStringLiteral("sheet %1 %2, place %3, type %4").arg(MapStructure::typeKey(sheet), MapStructure::directionKey(sheetDirection)).arg(where).arg(MapStructure::typeKey(type));
                for (int i = 0; i < offeredLayouts.size(); ++i)
                    for (int j = i + 1; j < offeredLayouts.size(); ++j)
                        if (sameLayout(offeredLayouts[i], offeredLayouts[j])) failures.append(at + QStringLiteral(": two offered directions lay out the same"));
                const QList<QList<QPointF>> reference = offeredLayouts.isEmpty() ? otherLayouts.mid(0, 1) : offeredLayouts;
                for (const QList<QPointF> &other : std::as_const(otherLayouts))
                    if (std::none_of(reference.cbegin(), reference.cend(), [&](const QList<QPointF> &r) { return sameLayout(r, other); }))
                        failures.append(at + QStringLiteral(": a direction not offered changes the layout"));
            }
        }
      }
    }
    QVERIFY2(failures.isEmpty(), qPrintable(failures.mid(0, 30).join(QLatin1Char('\n'))));
}

QTEST_MAIN(DomainLayoutTest)
// A main topic moved one level down takes the sub topics' padding: its text is laid out again with
// the new padding (the key lacked it, so an item kept the old wrap width until another change).
void DomainLayoutTest::textKeyFollowsThePadding() {
    MindMapModel model;
    MindNode *root = model.root();
    MindNode *a = model.addChild(root, QStringLiteral("Alpha"));
    MindNode *b = model.addChild(root, QStringLiteral("Beta has a title long enough to wrap over more than one line in its box"));
    NodeGeometry geometry(b);
    const NodeGeometry::TextLayoutKey before = geometry.geometryKey().text;
    std::unique_ptr<MindNode> moved = std::move(root->children.back());
    root->children.pop_back();
    moved->parent = a;
    a->children.push_back(std::move(moved));
    QVERIFY(!(geometry.geometryKey().text == before));
}

#include "domain_layout_test.moc"
