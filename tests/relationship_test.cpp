// FEAT-053: relationships (data, JSON form, geometry, document commands and history); FEAT-052
// boundaries, the other element kind, in the same way.
#include "boundary.h"
#include "document.h"
#include "mindmapmodel.h"
#include "relationship.h"
#include "summary.h"

#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>

class RelationshipTest final : public QObject {
    Q_OBJECT
private slots:
    void jsonKeepsEveryField();
    void jsonSkipsInvalidEntries();
    void endsSitOnTheOutlines();
    void automaticCurveIsAnS();
    void movedHandleIsKeptRelativeToItsElement();
    void straightIgnoresTheHandles();
    void commandsAreOneUndoStepEach();
    void selfAndMissingEndsAreRefused();
    void removingATopicRemovesItsRelationships();
    void boundaryJsonKeepsEveryField();
    void boundaryGeometry();
    void summaryGeometry();
    void boundaryCommandsAreOneUndoStepEach();
};

namespace {

Relationships::End box(const QRectF &rect) {
    QPainterPath outline;
    outline.addRect(rect);
    return {outline, rect.center()};
}

bool onRectBorder(const QPointF &point, const QRectF &rect) {
    const qreal tolerance = 0.01;
    const bool insideX = point.x() >= rect.left() - tolerance && point.x() <= rect.right() + tolerance;
    const bool insideY = point.y() >= rect.top() - tolerance && point.y() <= rect.bottom() + tolerance;
    const bool onVertical = std::abs(point.x() - rect.left()) < tolerance || std::abs(point.x() - rect.right()) < tolerance;
    const bool onHorizontal = std::abs(point.y() - rect.top()) < tolerance || std::abs(point.y() - rect.bottom()) < tolerance;
    return insideX && insideY && (onVertical || onHorizontal);
}

}  // namespace

void RelationshipTest::jsonKeepsEveryField() {
    Relationship relationship;
    relationship.id = QStringLiteral("r1");
    relationship.from = QStringLiteral("a");
    relationship.to = QStringLiteral("b");
    relationship.shape = Relationship::Shape::Elbow;
    relationship.fromControl = QPointF(10, -20);
    relationship.toControl = QPointF(-5, 7.5);
    relationship.label = QStringLiteral("causes");
    relationship.lineColor = QColor(QStringLiteral("#ff0000"));
    relationship.lineWidth = 3;
    relationship.linePattern = Qt::DotLine;
    relationship.startMark = BranchEnd::Dot;
    relationship.endMark = BranchEnd::None;  // stored too: it turns off the default arrow
    relationship.fontFamily = QStringLiteral("Arial");
    relationship.fontSize = 18;
    relationship.textColor = QColor(QStringLiteral("#00ff00"));
    // Through text, like the .mentis file.
    const QByteArray text = QJsonDocument(Relationships::toJson(QList<Relationship>{relationship})).toJson();
    const QList<Relationship> read = Relationships::listFromJson(QJsonDocument::fromJson(text).array());
    QCOMPARE(read.size(), 1);
    QVERIFY(read.front() == relationship);
    // Defaults are not written.
    Relationship plain{QStringLiteral("r2"), QStringLiteral("a"), QStringLiteral("b")};
    QCOMPARE(Relationships::toJson(plain).keys(), (QStringList{"from", "id", "to"}));
}

void RelationshipTest::jsonSkipsInvalidEntries() {
    QJsonArray array{QJsonArray{"a", "b"},  // the pairs of before FEAT-053 are not read
                     QJsonObject{{"id", "x"}, {"from", "a"}},
                     QJsonObject{{"id", "y"}, {"from", "a"}, {"to", "a"}},
                     QJsonObject{{"id", "z"}, {"from", "a"}, {"to", "b"}, {"shape", "zigzag"}, {"lineWidth", -2}, {"linePattern", 9}}};
    const QList<Relationship> read = Relationships::listFromJson(array);
    QCOMPARE(read.size(), 1);
    QCOMPARE(read.front().id, QStringLiteral("z"));
    QCOMPARE(read.front().shape, Relationship::Shape::Curved);
    QVERIFY(!read.front().lineWidth);
    QVERIFY(!read.front().linePattern);
}

void RelationshipTest::endsSitOnTheOutlines() {
    const QRectF top(0, 0, 120, 40), bottom(0, 200, 120, 40);
    for (auto shape : {Relationship::Shape::Curved, Relationship::Shape::Straight, Relationship::Shape::Elbow}) {
        Relationship relationship{QStringLiteral("r"), QStringLiteral("a"), QStringLiteral("b"), shape};
        const auto geometry = Relationships::geometry(relationship, box(top), box(bottom));
        QVERIFY2(onRectBorder(geometry.start, top), qPrintable(Relationships::shapeKey(shape)));
        QVERIFY2(onRectBorder(geometry.end, bottom), qPrintable(Relationships::shapeKey(shape)));
        QCOMPARE(geometry.path.elementAt(0), geometry.start);
        QVERIFY(QLineF(geometry.path.currentPosition(), geometry.end).length() < 0.01);
    }
}

void RelationshipTest::automaticCurveIsAnS() {
    // Measured on the reference: from a topic to one straight below, the first handle goes left and the
    // second right, each about a third of the way.
    const QRectF top(0, 0, 120, 40), bottom(0, 200, 120, 40);
    Relationship relationship{QStringLiteral("r"), QStringLiteral("a"), QStringLiteral("b")};
    const auto geometry = Relationships::geometry(relationship, box(top), box(bottom));
    QVERIFY(geometry.fromControl.x() < top.center().x() - 10);
    QVERIFY(geometry.toControl.x() > bottom.center().x() + 10);
    const qreal span = geometry.end.y() - geometry.start.y();
    QVERIFY(std::abs(geometry.fromControl.y() - geometry.start.y() - span * 0.32) < 2);
    QVERIFY(std::abs(geometry.end.y() - geometry.toControl.y() - span * 0.32) < 2);
}

void RelationshipTest::movedHandleIsKeptRelativeToItsElement() {
    Relationship relationship{QStringLiteral("r"), QStringLiteral("a"), QStringLiteral("b")};
    relationship.fromControl = QPointF(-100, 0);
    const QRectF from(0, 0, 120, 40), to(0, 200, 120, 40);
    const auto before = Relationships::geometry(relationship, box(from), box(to));
    QCOMPARE(before.fromControl, from.center() + QPointF(-100, 0));
    QVERIFY(std::abs(before.start.x() - from.left()) < 0.01);  // the end follows the handle: left side
    const auto moved = Relationships::geometry(relationship, box(from.translated(50, 30)), box(to));
    QCOMPARE(moved.fromControl, before.fromControl + QPointF(50, 30));
}

void RelationshipTest::straightIgnoresTheHandles() {
    Relationship relationship{QStringLiteral("r"), QStringLiteral("a"), QStringLiteral("b"), Relationship::Shape::Straight};
    relationship.fromControl = QPointF(-300, 0);
    const QRectF from(0, 0, 120, 40), to(0, 200, 120, 40);
    const auto geometry = Relationships::geometry(relationship, box(from), box(to));
    QVERIFY(QLineF(geometry.start, QPointF(60, 40)).length() < 0.01);
    QVERIFY(QLineF(geometry.end, QPointF(60, 200)).length() < 0.01);
}

void RelationshipTest::commandsAreOneUndoStepEach() {
    Document document;
    document.startUntitled();
    MindNode *a = document.addChild(nullptr, QStringLiteral("A"));
    MindNode *b = document.addChild(nullptr, QStringLiteral("B"));
    MindNode *c = document.addChild(nullptr, QStringLiteral("C"));
    document.flushPendingStep();
    QSignalSpy annotations(&document, &Document::annotationsChanged);
    const QString id = document.addRelation(a->id, b->id);
    QVERIFY(!id.isEmpty());
    // Handle moved, then an end changed (document level), then label: three steps.
    Relationship changed = *document.relation(id);
    changed.toControl = QPointF(40, 40);
    QVERIFY(document.updateRelations({changed}));
    QVERIFY(!document.updateRelations({changed}));  // nothing differs
    changed.to = c->id;
    QVERIFY(document.updateRelations({changed}));
    changed.label = QStringLiteral("because");
    QVERIFY(document.updateRelations({changed}));
    QCOMPARE(annotations.size(), 4);
    QCOMPARE(document.relation(id)->label, QStringLiteral("because"));
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(document.relation(id)->label.isEmpty());
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QCOMPARE(document.relation(id)->to, b->id);
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(!document.relation(id)->toControl);
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(!document.relation(id));
    QCOMPARE(document.redo().outcome, Document::StepOutcome::Applied);
    QVERIFY(document.removeRelations({id}));
    QVERIFY(!document.removeRelations({id}));
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(document.relation(id));
}

void RelationshipTest::selfAndMissingEndsAreRefused() {
    Document document;
    document.startUntitled();
    MindNode *a = document.addChild(nullptr, QStringLiteral("A"));
    MindNode *b = document.addChild(nullptr, QStringLiteral("B"));
    QVERIFY(document.addRelation(a->id, a->id).isEmpty());
    QVERIFY(document.addRelation(a->id, QStringLiteral("elsewhere")).isEmpty());  // e.g. another sheet
    const QString id = document.addRelation(a->id, b->id);
    Relationship changed = *document.relation(id);
    changed.to = a->id;
    QVERIFY(!document.updateRelations({changed}));
    changed.id = QStringLiteral("unknown");
    changed.to = b->id;
    QVERIFY(!document.updateRelations({changed}));
    QCOMPARE(document.model()->relations.size(), 1);
}

void RelationshipTest::removingATopicRemovesItsRelationships() {
    Document document;
    document.startUntitled();
    MindNode *a = document.addChild(nullptr, QStringLiteral("A"));
    MindNode *b = document.addChild(nullptr, QStringLiteral("B"));
    const QString id = document.addRelation(a->id, b->id);
    QVERIFY(document.removeNodes({b}));
    QVERIFY(!document.relation(id));
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(document.relation(id));
}

void RelationshipTest::boundaryJsonKeepsEveryField() {
    Boundary boundary;
    boundary.id = QStringLiteral("b1");
    boundary.topics = {QStringLiteral("t1"), QStringLiteral("t2")};
    boundary.title = QStringLiteral("Group");
    boundary.shape = Boundary::Shape::Rectangle;
    boundary.lineColor = QColor(QStringLiteral("#ff112233"));
    boundary.lineWidth = 3;
    boundary.linePattern = Qt::DotLine;
    boundary.fillColor = QColor(QStringLiteral("#20445566"));
    const QJsonObject json = Boundaries::toJson(boundary);
    const auto read = Boundaries::fromJson(QJsonDocument::fromJson(QJsonDocument(json).toJson()).object());
    QVERIFY(read);
    QCOMPARE(*read, boundary);
    // Defaults are not written; old id lists and entries without topics are skipped.
    Boundary plain;
    plain.id = QStringLiteral("b2");
    plain.topics = {QStringLiteral("t1")};
    QCOMPARE(Boundaries::toJson(plain).keys(), (QStringList{QStringLiteral("id"), QStringLiteral("topics")}));
    const QList<Boundary> list = Boundaries::listFromJson(QJsonArray{QJsonArray{QStringLiteral("t1")}, QJsonObject{{"id", "x"}}, json});
    QCOMPARE(list.size(), 1);
}

// FEAT-052 summaries: arms 8 px beyond the branches and 2 px past them, line 11 px out, tick 9 px,
// summary topic 8 px further (measured live 2026-10-24); mirrored on the left.
void RelationshipTest::summaryGeometry() {
    const QRectF covered(0, 0, 100, 50);
    using Summaries::Side;
    QCOMPARE(Summaries::bracket(covered, Side::Right).boundingRect(), QRectF(108, -2, 20, 54));
    QCOMPARE(Summaries::topicAnchor(covered, Side::Right), QPointF(136, 25));
    QCOMPARE(Summaries::bracket(covered, Side::Left).boundingRect(), QRectF(-28, -2, 20, 54));
    QCOMPARE(Summaries::topicAnchor(covered, Side::Left), QPointF(-36, 25));
    // Below and above a row (org chart, timeline, fishbone; measured live 2026-10-25): the same, turned.
    QCOMPARE(Summaries::bracket(covered, Side::Down).boundingRect(), QRectF(-2, 58, 104, 20));
    QCOMPARE(Summaries::topicAnchor(covered, Side::Down), QPointF(50, 86));
    QCOMPARE(Summaries::bracket(covered, Side::Up).boundingRect(), QRectF(-2, -28, 104, 20));
    QCOMPARE(Summaries::topicAnchor(covered, Side::Up), QPointF(50, -36));
    // The line's outer edge is 11 px from where the arms start.
    QCOMPARE(Summaries::bracket(covered, Side::Right).elementAt(1).x + Summaries::kLineWidth / 2, 108 + Summaries::kArmPx);
}

void RelationshipTest::boundaryGeometry() {
    const QRectF covered(100, 50, 200, 40);
    const QRectF stroke = Boundaries::strokeRect(covered);
    QCOMPARE(stroke, QRectF(92, 42, 216, 56));
    const QRectF tab = Boundaries::titleRect(stroke, 30);
    QCOMPARE(tab, QRectF(92 + 12, 42 - 18, 30 + 20, 18));
    Boundary boundary;
    QCOMPARE(Boundaries::padBelow(boundary), 8 + Boundaries::kDefaultLineWidth / 2);
    QCOMPARE(Boundaries::padAbove(boundary), Boundaries::padBelow(boundary));
    boundary.title = QStringLiteral("T");
    QCOMPARE(Boundaries::padAbove(boundary), Boundaries::padBelow(boundary) + 18);
    // Rounded corners: the corner point is outside the outline, the middle of an edge on it.
    QVERIFY(!Boundaries::outline(Boundary::Shape::RoundedRectangle, stroke).contains(stroke.topLeft() + QPointF(0.5, 0.5)));
    QVERIFY(Boundaries::outline(Boundary::Shape::Rectangle, stroke).contains(stroke.topLeft() + QPointF(0.5, 0.5)));
}

void RelationshipTest::boundaryCommandsAreOneUndoStepEach() {
    Document document;
    document.startUntitled();
    MindMapModel *model = document.model();
    MindNode *main = model->addChild(model->root(), QStringLiteral("Main"));
    MindNode *other = model->addChild(model->root(), QStringLiteral("Other"));
    MindNode *a = model->addChild(main, QStringLiteral("A"));
    MindNode *b = model->addChild(main, QStringLiteral("B"));
    MindNode *x = model->addChild(other, QStringLiteral("X"));
    document.markChanged();
    document.flushPendingStep();
    QVERIFY(document.addBoundaries({model->root()}).isEmpty());  // the central topic
    const QStringList created = document.addBoundaries({b, x, a});
    document.flushPendingStep();
    QCOMPARE(created.size(), 2);
    QCOMPARE(model->boundaries[0].topics, (QStringList{a->id, b->id}));
    QCOMPARE(model->boundaries[1].topics, QStringList{x->id});
    // BUG-024: the same run again, or topics selected with their parent: no second boundary.
    QVERIFY(document.addBoundaries({a, b}).isEmpty());
    const QStringList withChild = document.addBoundaries({other, x});
    QCOMPARE(withChild, QStringList{model->boundaries.last().id});
    QCOMPARE(model->boundaries.last().topics, QStringList{other->id});
    document.flushPendingStep();
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QCOMPARE(model->boundaries.size(), 2);
    Boundary changed = model->boundaries[0];
    changed.title = QStringLiteral("Group");
    QVERIFY(document.updateBoundaries({changed}));
    document.flushPendingStep();
    QVERIFY(!document.updateBoundaries({changed}));  // nothing differs
    QVERIFY(!document.addRelation(created[0], x->id).isEmpty());
    QVERIFY(document.removeBoundaries({created[0]}));
    document.flushPendingStep();
    QVERIFY(model->relations.isEmpty());  // the relationship on it goes in the same step
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QCOMPARE(model->boundaries.size(), 2);
    QCOMPARE(model->relations.size(), 1);
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(model->relations.isEmpty());
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(model->boundaries[0].title.isEmpty());
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(model->boundaries.isEmpty());
}

QTEST_MAIN(RelationshipTest)
#include "relationship_test.moc"
