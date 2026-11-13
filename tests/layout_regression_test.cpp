// FEAT-096: the map layout before and after it moves out of MindMapScene and NodeItem. One map per
// structure type and direction (plus one with a structure per branch, and five with content) is laid
// out like the MCP server does (Document + MindMapScene, no window); every topic's position, side and
// size is compared with the values recorded from the code before the move
// (tests/data/layout_regression/<kernel>-qt<version>.json). MENTIS_LAYOUT_RECORD=1 writes that file
// instead of comparing. Text measures depend on the platform's font rendering and on the Qt version,
// so a platform or Qt without a recorded file skips the comparison (the CI builds with another Qt).
// The same file holds, under "plain-<map>", the positions of each map without summaries, boundaries
// and formulas, which tests/domain_layout_test.cpp finds again with MentisDomain alone.
#include "boundaryitem.h"
#include "layout_fixtures.h"
#include "maplayout.h"
#include "mindmapscene.h"
#include "nodeitem.h"
#include "performance.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTest>

#include <cmath>
#include <functional>
#include <memory>
#include <unordered_map>

using LayoutFixtures::Fixture;
using LayoutFixtures::keyOf;

namespace {

constexpr qreal kTolerancePx = 0.01;

QString platformName() { return QSysInfo::kernelType() + QStringLiteral("-qt") + QString::fromLatin1(qVersion()); }

QString goldenPath() { return QStringLiteral(MENTIS_TEST_DATA_DIR "/layout_regression/%1.json").arg(platformName()); }

QJsonObject rect(const QRectF &r) { return {{"x", r.x()}, {"y", r.y()}, {"w", r.width()}, {"h", r.height()}}; }

// FEAT-096 stage 2: the same layout from MapLayout and NodeGeometry alone, without the scene and its
// items; every laid-out topic must stand where the scene put it. Returns the differences.
QStringList layOutWithoutScene(MindMapModel &model) {
    std::unordered_map<const MindNode *, std::unique_ptr<NodeGeometry>> geometries;
    const std::function<void(const MindNode *)> measure = [&](const MindNode *node) {
        // NodeItem's measuring steps, in its order (constructor and syncFromNode).
        auto geometry = std::make_unique<NodeGeometry>(node);
        geometry->setHasImage(!node->imagePath.isEmpty() || !node->formula.isEmpty());
        geometry->updateText();
        geometry->updateNotePreview(QFont());
        geometry->updateLabelLayout();
        geometries[node] = std::move(geometry);
        for (const auto &child : node->children) measure(child.get());
    };
    measure(model.root());
    for (const Summary &summary : std::as_const(model.summaries)) measure(summary.topic.get());
    const auto hidden = [](const MindNode *node) {  // under a collapsed topic, as the scene hides it
        for (const MindNode *at = node; at->parent; at = at->parent)
            if (!at->floating && MapLayout::childrenHidden(at->parent)) return true;
        return false;
    };
    MapLayout layout(&model);
    layout.geometryOf = [&](const MindNode *node) -> const NodeGeometry * {
        const auto it = geometries.find(node);
        return it == geometries.end() ? nullptr : it->second.get();
    };
    layout.nodeById = [&](const QString &id) -> const MindNode * { return model.nodeById(id); };
    layout.branchesCovered = [&](const QStringList &ids) {
        QRectF covered;
        const std::function<void(const MindNode *)> add = [&](const MindNode *node) {
            if (hidden(node)) return;
            const QRectF box = layout.geometryOf(node)->shapeRect().translated(node->position);
            covered = covered.isNull() ? box : covered.united(box);
            for (const auto &child : node->children) if (!child->floating) add(child.get());
        };
        for (const QString &id : ids) if (const MindNode *node = model.nodeById(id)) add(node);
        return covered;
    };
    layout.collectBoundaryRuns();
    layout.computeAutoPositions();
    QStringList differences;
    for (auto it = layout.autoPositions().cbegin(); it != layout.autoPositions().cend(); ++it)
        if (QLineF(it.value(), it.key()->position).length() > kTolerancePx)
            differences.append(QStringLiteral("%1: (%2, %3) without the scene, (%4, %5) with it").arg(keyOf(it.key()))
                                   .arg(it.value().x()).arg(it.value().y()).arg(it.key()->position.x()).arg(it.key()->position.y()));
    if (layout.autoPositions().size() < 10) differences.append(QStringLiteral("only %1 topics laid out").arg(layout.autoPositions().size()));
    return differences;
}

}  // namespace

class LayoutRegressionTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void layoutMatchesTheRecordedValues();

private:
    // Builds the fixture's map, lays it out and returns what is compared.
    QJsonObject layOut(const Fixture &fixture);
    // FEAT-096 stage 4: the map without summaries, boundaries and formulas, laid out by the scene: the
    // shown topics' positions, which tests/domain_layout_test.cpp compares with.
    QJsonObject layOutPlain(const Fixture &fixture);
    QStringList m_withoutScene;  // differences of layOutWithoutScene, by fixture
};

void LayoutRegressionTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisLayoutRegressionTest"));
    Performance::setMode(Performance::Mode::Standard);
}

QJsonObject LayoutRegressionTest::layOut(const Fixture &fixture) {
    Document document;
    MindMapModel &model = *document.model();
    std::unique_ptr<MindMapScene> sceneOwner;
    const LayoutFixtures::Topics topics = LayoutFixtures::build(
        document, fixture, &NodeItem::formulaInsertionSize,
        [&] { sceneOwner = std::make_unique<MindMapScene>(&model); }, [&] { sceneOwner->rebuild(); });
    MindMapScene &scene = *sceneOwner;
    scene.autoLayout();
    document.flushPendingStep();

    const auto side = [&scene](const MindNode *node) { return scene.sideOf(node); };
    if (fixture.type != MapStructure::typeKey(MapStructure::Type::TreeTable)) {
        document.addSummaries({topics.b1, topics.b2}, side);
        scene.autoLayout();
        document.flushPendingStep();
    }
    document.addBoundaries({topics.a1, topics.a2}, side);
    scene.autoLayout();
    document.flushPendingStep();
    // Justify widens the items' measures, which the scene-less run does not do.
    if (fixture.content != Fixture::Content::Justify)
        for (const QString &difference : layOutWithoutScene(model)) m_withoutScene.append(fixture.name + QStringLiteral(": ") + difference);

    QJsonObject topicValues;
    QJsonArray boundaries;
    for (QGraphicsItem *graphic : scene.items()) {
        if (auto *item = qgraphicsitem_cast<NodeItem *>(graphic)) {
            const MindNode *node = item->node();
            topicValues.insert(keyOf(node), QJsonObject{
                {"position", QJsonArray{node->position.x(), node->position.y()}},
                {"item", QJsonArray{item->pos().x(), item->pos().y()}},
                {"side", scene.sideOf(node)},
                {"box", rect(item->boundingRect())},
                {"anchorHalfWidth", item->anchorHalfWidth()},
                {"visible", item->isVisible()}});
        } else if (auto *boundary = qgraphicsitem_cast<BoundaryItem *>(graphic)) {
            boundaries.append(rect(boundary->mapRectToScene(boundary->boundingRect())));
        }
    }
    QJsonObject result{{"topics", topicValues}, {"boundaries", boundaries}, {"scene", rect(scene.itemsBoundingRect())}};
    document.close();
    NodeItem::showAllNotes() = false;
    return result;
}

QJsonObject LayoutRegressionTest::layOutPlain(const Fixture &fixture) {
    Document document;
    LayoutFixtures::build(document, fixture, nullptr);
    MindMapScene scene(document.model());
    scene.autoLayout();
    QJsonObject positions;
    for (QGraphicsItem *graphic : scene.items())
        if (auto *item = qgraphicsitem_cast<NodeItem *>(graphic); item && item->isVisible())
            positions.insert(keyOf(item->node()), QJsonArray{item->node()->position.x(), item->node()->position.y()});
    document.close();
    NodeItem::showAllNotes() = false;
    return positions;
}

namespace {

// Every number of actual equal to expected within kTolerancePx; same keys and array sizes.
void compare(const QJsonValue &expected, const QJsonValue &actual, const QString &where, QStringList &differences) {
    if (differences.size() >= 20) return;
    if (expected.isDouble() && actual.isDouble()) {
        if (std::abs(expected.toDouble() - actual.toDouble()) > kTolerancePx)
            differences.append(QStringLiteral("%1: %2 -> %3").arg(where).arg(expected.toDouble()).arg(actual.toDouble()));
    } else if (expected.isObject() && actual.isObject()) {
        const QJsonObject e = expected.toObject(), a = actual.toObject();
        for (auto it = e.begin(); it != e.end(); ++it) {
            if (!a.contains(it.key())) differences.append(QStringLiteral("%1/%2: missing").arg(where, it.key()));
            else compare(it.value(), a.value(it.key()), where + QLatin1Char('/') + it.key(), differences);
        }
        for (auto it = a.begin(); it != a.end(); ++it)
            if (!e.contains(it.key())) differences.append(QStringLiteral("%1/%2: new").arg(where, it.key()));
    } else if (expected.isArray() && actual.isArray()) {
        const QJsonArray e = expected.toArray(), a = actual.toArray();
        if (e.size() != a.size()) {
            differences.append(QStringLiteral("%1: %2 items -> %3").arg(where).arg(e.size()).arg(a.size()));
            return;
        }
        for (qsizetype i = 0; i < e.size(); ++i) compare(e.at(i), a.at(i), where + QStringLiteral("[%1]").arg(i), differences);
    } else if (expected != actual) {
        differences.append(QStringLiteral("%1: value changed").arg(where));
    }
}

}  // namespace

void LayoutRegressionTest::layoutMatchesTheRecordedValues() {
    QJsonObject actual;
    for (const Fixture &fixture : LayoutFixtures::all()) {
        actual.insert(fixture.name, layOut(fixture));
        // Justify needs the items (it widens their measures): not part of the scene-less comparison.
        if (fixture.content != Fixture::Content::Justify) actual.insert(QStringLiteral("plain-") + fixture.name, layOutPlain(fixture));
    }

    QVERIFY2(m_withoutScene.isEmpty(), qPrintable(m_withoutScene.mid(0, 20).join(QLatin1Char('\n'))));

    QFile file(goldenPath());
    if (qEnvironmentVariableIntValue("MENTIS_LAYOUT_RECORD") == 1) {
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.fileName()));
        file.write(QJsonDocument(actual).toJson(QJsonDocument::Indented));
        QSKIP(qPrintable(QStringLiteral("Recorded %1").arg(file.fileName())));
    }
    if (!file.exists())
        QSKIP(qPrintable(QStringLiteral("No recorded layout for %1 (MENTIS_LAYOUT_RECORD=1 writes it)").arg(platformName())));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
    const QJsonObject expected = QJsonDocument::fromJson(file.readAll()).object();
    QVERIFY(!expected.isEmpty());

    QStringList differences;
    compare(expected, actual, QString(), differences);
    QVERIFY2(differences.isEmpty(), qPrintable(differences.join(QLatin1Char('\n'))));
}

QTEST_MAIN(LayoutRegressionTest)
#include "layout_regression_test.moc"
