// FEAT-096 stage 4: the core (MentisDomain) lays a map out by itself, without Qt Widgets. This test links
// MentisDomain only, so it runs under QGuiApplication: the build fails if the core needs a widget
// again. Every map of tests/layout_fixtures.h (but Justify, which measures the items again) is built
// without summaries, boundaries and formulas, measured with NodeGeometry and laid out with MapLayout;
// the positions must be those the desktop scene gives (recorded by tests/layout_regression_test.cpp under
// "plain-<map>" in tests/data/layout_regression/<kernel>-qt<version>.json; another platform or Qt skips).
#include "layout_fixtures.h"
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

QTEST_MAIN(DomainLayoutTest)
#include "domain_layout_test.moc"
