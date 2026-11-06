// FEAT-056: what each "Select" command selects (model) and what the scene selects (drawn only).
#include "document.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "performance.h"
#include "selectioncommands.h"

#include <QStandardPaths>
#include <QTest>

using Kind = SelectionCommands::Kind;

class SelectionCommandsTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void subtopicsAreAllDescendants();
    void siblingsAndUnion();
    void sameLevelKeepsHierarchiesApart();
    void elementKinds();
    void collapsedBranchesAreSkipped();
    void sceneSelectsTheDrawnOnes();

private:
    QStringList titles(Kind kind, const QList<MindNode *> &selected) const;
    std::unique_ptr<Document> m_document;
    MindNode *m_a = nullptr, *m_a1 = nullptr, *m_a11 = nullptr, *m_a2 = nullptr, *m_b = nullptr, *m_b1 = nullptr;
    MindNode *m_f = nullptr, *m_f1 = nullptr;
};

QStringList SelectionCommandsTest::titles(Kind kind, const QList<MindNode *> &selected) const {
    QStringList result;
    for (const MindNode *node : SelectionCommands::targets(kind, *m_document->model(), selected).nodes) result.append(node->title);
    result.sort();
    return result;
}

void SelectionCommandsTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisSelectionCommandsTest"));
    Performance::setMode(Performance::Mode::Standard);
}

// Central > A (A1 > A11, A2), B (B1); floating F (F1).
void SelectionCommandsTest::init() {
    m_document = std::make_unique<Document>();
    m_document->startUntitled();
    m_document->model()->root()->title = QStringLiteral("C");
    m_a = m_document->addChild(nullptr, QStringLiteral("A"));
    m_a1 = m_document->addChild(m_a, QStringLiteral("A1"));
    m_a11 = m_document->addChild(m_a1, QStringLiteral("A11"));
    m_a2 = m_document->addChild(m_a, QStringLiteral("A2"));
    m_b = m_document->addChild(nullptr, QStringLiteral("B"));
    m_b1 = m_document->addChild(m_b, QStringLiteral("B1"));
    m_f = m_document->addFloatingTopic({500, 500}, QStringLiteral("F"));
    m_f1 = m_document->addChild(m_f, QStringLiteral("F1"));
    m_document->flushPendingStep();
}

void SelectionCommandsTest::cleanup() { m_document.reset(); }

void SelectionCommandsTest::subtopicsAreAllDescendants() {
    QCOMPARE(titles(Kind::Subtopics, {m_a}), (QStringList{"A1", "A11", "A2"}));
    // From the central topic: the map, not the floating topics.
    QCOMPARE(titles(Kind::Subtopics, {m_document->model()->root()}), (QStringList{"A", "A1", "A11", "A2", "B", "B1"}));
    QVERIFY(titles(Kind::Subtopics, {m_a11}).isEmpty());
}

void SelectionCommandsTest::siblingsAndUnion() {
    QCOMPARE(titles(Kind::Siblings, {m_a1}), (QStringList{"A1", "A2"}));
    QCOMPARE(titles(Kind::Siblings, {m_a}), (QStringList{"A", "B"}));  // main branches, not floating
    QCOMPARE(titles(Kind::Siblings, {m_f}), (QStringList{"F"}));
    QCOMPARE(titles(Kind::Siblings, {m_a1, m_b1}), (QStringList{"A1", "A2", "B1"}));
}

void SelectionCommandsTest::sameLevelKeepsHierarchiesApart() {
    QCOMPARE(titles(Kind::SameLevel, {m_a1}), (QStringList{"A1", "A2", "B1"}));
    QCOMPARE(titles(Kind::SameLevel, {m_f1}), (QStringList{"F1"}));
    QCOMPARE(titles(Kind::SameLevel, {m_a11, m_f}), (QStringList{"A11", "F"}));
}

void SelectionCommandsTest::elementKinds() {
    QCOMPARE(titles(Kind::Floating, {}), (QStringList{"F"}));
    m_b1->formula = QStringLiteral("x^2");
    m_a2->imagePath = QStringLiteral("picture.png");
    QCOMPARE(titles(Kind::Equations, {}), (QStringList{"B1"}));
    QCOMPARE(titles(Kind::Images, {}), (QStringList{"A2"}));
    const QString relation = m_document->addRelation(m_a->id, m_b->id);
    const QStringList boundaries = m_document->addBoundaries({m_a1});
    const QList<MindNode *> summaries = m_document->addSummaries({m_b1});
    const MindMapModel &model = *m_document->model();
    QCOMPARE(SelectionCommands::targets(Kind::Relationships, model, {}).relations, QStringList{relation});
    QCOMPARE(SelectionCommands::targets(Kind::Boundaries, model, {}).boundaries, boundaries);
    QCOMPARE(summaries.size(), 1);
    QCOMPARE(SelectionCommands::targets(Kind::Summaries, model, {}).nodes, summaries);
}

void SelectionCommandsTest::collapsedBranchesAreSkipped() {
    QVERIFY(m_document->setCollapsed(m_a1, true));
    QCOMPARE(titles(Kind::Subtopics, {m_a}), (QStringList{"A1", "A2"}));
    QCOMPARE(titles(Kind::SameLevel, {m_b}), (QStringList{"A", "B"}));
    m_a11->formula = QStringLiteral("y");
    QVERIFY(titles(Kind::Equations, {}).isEmpty());
}

void SelectionCommandsTest::sceneSelectsTheDrawnOnes() {
    MindMapScene scene(m_document->model());
    scene.selectNode(m_a);
    const SelectionCommands::Targets none = SelectionCommands::targets(Kind::Relationships, *m_document->model(), {});
    QCOMPARE(scene.selectTargets(none, false), 0);  // greyed out
    const SelectionCommands::Targets targets = SelectionCommands::targets(Kind::SameLevel, *m_document->model(), {m_a1});
    QCOMPARE(scene.selectTargets(targets, false), 3);
    QCOMPARE(scene.selectedNodes(), QList<MindNode *>{m_a});  // nothing changed yet
    QCOMPARE(scene.selectTargets(targets), 3);
    QList<MindNode *> selected = scene.selectedNodes();
    std::sort(selected.begin(), selected.end());
    QList<MindNode *> expected{m_a1, m_a2, m_b1};
    std::sort(expected.begin(), expected.end());
    QCOMPARE(selected, expected);
}

QTEST_MAIN(SelectionCommandsTest)
#include "selection_commands_test.moc"
