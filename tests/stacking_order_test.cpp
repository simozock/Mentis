// FEAT-057: stacking order of floating topics and freely placed main branches (commands, disabled
// cases, z of whole branches in the scene, Undo/Redo, save and reopen).
#include "bmapstorage.h"
#include "document.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "nodeitem.h"
#include "performance.h"

#include <QGraphicsPathItem>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class StackingOrderTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void commandsMoveTheRank();
    void mainBranchesNeedFreePositions();
    void wholeBranchIsDrawnAbove();
    void orderIsUndoableAndSaved();

private:
    QTemporaryDir m_dir;
};

namespace {

QStringList stackTitles(const MindMapModel *model) {
    QStringList titles;
    for (const MindNode *node : model->stackingOrder()) titles.append(node->title);
    return titles;
}

NodeItem *itemFor(const MindMapScene &scene, const MindNode *node) {
    for (QGraphicsItem *item : scene.items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node) return nodeItem;
    return nullptr;
}

}  // namespace

void StackingOrderTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisStackingOrderTest"));
    Performance::setMode(Performance::Mode::Standard);
    QVERIFY(m_dir.isValid());
}

void StackingOrderTest::commandsMoveTheRank() {
    Document document;
    document.startUntitled();
    MindNode *a = document.addFloatingTopic({0, 0}, QStringLiteral("A"));
    MindNode *b = document.addFloatingTopic({10, 0}, QStringLiteral("B"));
    MindNode *c = document.addFloatingTopic({20, 0}, QStringLiteral("C"));
    document.flushPendingStep();
    using Arrange = Document::Arrange;
    // Without a choice the child order stacks them: later on top.
    QCOMPARE(stackTitles(document.model()), (QStringList{"A", "B", "C"}));
    QVERIFY(document.arrange(a, Arrange::ToFront));
    QCOMPARE(stackTitles(document.model()), (QStringList{"B", "C", "A"}));
    QVERIFY(!document.arrange(a, Arrange::Forward));  // already on top
    QVERIFY(document.arrange(a, Arrange::Backward));
    QCOMPARE(stackTitles(document.model()), (QStringList{"B", "A", "C"}));
    QVERIFY(document.arrange(c, Arrange::ToBack));
    QCOMPARE(stackTitles(document.model()), (QStringList{"C", "B", "A"}));
    QVERIFY(!document.arrange(c, Arrange::ToBack));
    QVERIFY(document.arrange(c, Arrange::Forward));
    QCOMPARE(stackTitles(document.model()), (QStringList{"B", "C", "A"}));
    QVERIFY(b && !document.canArrange(document.model()->root()));
}

void StackingOrderTest::mainBranchesNeedFreePositions() {
    Document document;
    document.startUntitled();
    MindNode *main1 = document.addChild(nullptr, QStringLiteral("M1"));
    document.addChild(nullptr, QStringLiteral("M2"));
    MindNode *sub = document.addChild(main1, QStringLiteral("S"));
    document.flushPendingStep();
    QVERIFY(!document.canArrange(main1));
    QVERIFY(!document.arrange(main1, Document::Arrange::ToFront));
    QVERIFY(!document.canArrange(sub));
    document.model()->sheetStyle.freeBranches = true;
    QVERIFY(document.canArrange(main1));
    QVERIFY(!document.canArrange(sub));  // inside a branch: never
    // A lone floating topic has nothing to overlap.
    Document lone;
    lone.startUntitled();
    MindNode *floating = lone.addFloatingTopic({0, 0}, QStringLiteral("F"));
    QVERIFY(!lone.canArrange(floating));
}

void StackingOrderTest::wholeBranchIsDrawnAbove() {
    Document document;
    document.startUntitled();
    MindNode *low = document.addFloatingTopic({0, 0}, QStringLiteral("Low"));
    MindNode *lowChild = document.addChild(low, QStringLiteral("Low child"));
    MindNode *high = document.addFloatingTopic({10, 0}, QStringLiteral("High"));
    document.flushPendingStep();
    MindMapScene scene(document.model());
    const auto z = [&scene](const MindNode *node) { return itemFor(scene, node)->zValue(); };
    QVERIFY(z(high) > z(lowChild));
    QVERIFY(document.arrange(low, Document::Arrange::ToFront));
    scene.refreshStacking();
    QVERIFY(z(low) > z(high));
    QVERIFY(z(lowChild) > z(high));
    QCOMPARE(z(lowChild), z(low));
    QVERIFY(z(document.model()->root()) < z(high));
    QVERIFY(z(low) < 1);  // under relationships and the topic being edited
    // Lines stay under every topic.
    for (QGraphicsItem *item : scene.items())
        if (item->type() == QGraphicsPathItem::Type) QVERIFY(item->zValue() < 0);
    // A topic added later joins its branch's place.
    MindNode *added = document.addChild(low, QStringLiteral("Added"));
    QCOMPARE(z(added), z(low));
    // A topic moved to another branch takes that branch's place.
    QVERIFY(document.moveNode(added, high, 0, 0));
    QCOMPARE(z(added), z(high));
}

void StackingOrderTest::orderIsUndoableAndSaved() {
    const QString file = m_dir.filePath(QStringLiteral("stack.mentis"));
    {
        Document document;
        document.startUntitled();
        MindNode *a = document.addFloatingTopic({0, 0}, QStringLiteral("A"));
        document.addFloatingTopic({10, 0}, QStringLiteral("B"));
        document.flushPendingStep();
        QVERIFY(document.arrange(a, Document::Arrange::ToFront));
        document.flushPendingStep();
        QCOMPARE(stackTitles(document.model()), (QStringList{"B", "A"}));
        QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
        QCOMPARE(stackTitles(document.model()), (QStringList{"A", "B"}));
        QCOMPARE(document.redo().outcome, Document::StepOutcome::Applied);
        QCOMPARE(stackTitles(document.model()), (QStringList{"B", "A"}));
        QVERIFY(document.save(file, BmapStorage::SaveReason::Explicit).ok);
        document.close();
    }
    Document reopened;
    reopened.startUntitled();
    QVERIFY(reopened.open(file, false).ok);
    QCOMPARE(stackTitles(reopened.model()), (QStringList{"B", "A"}));
    reopened.close();
}

QTEST_MAIN(StackingOrderTest)
#include "stacking_order_test.moc"
