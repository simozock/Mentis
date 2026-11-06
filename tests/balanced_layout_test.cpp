// FEAT-012 (passo 4a): Mappa mentale su due lati con il bilanciamento automatico; FEAT-026 fase B
// (tipo e direzione salvati); FEAT-034 (il nodo si allarga dal lato lontano dal genitore). Vero
// MainWindow, piattaforma offscreen.
#include "mainwindow.h"
#include "mapstructure.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "nodestyle.h"
#include "performance.h"
#include "document.h"
#include "toggleswitch.h"
#include "branchpath.h"
#include "structurepicker.h"
#include "stylecontrols.h"
#include "mapvariants.h"
#include "variantgallery.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QToolButton>
#include <QJsonArray>
#include <QJsonObject>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTest>
#include <QDialog>
#include <QTimer>
#include <QScrollBar>
#include <QWheelEvent>
#include <QPainter>

#include <memory>

class BalancedLayoutTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void balancedSidesRules();
    void oneBranchGoesRight();
    void fourBranchesClockwise();
    void bigBranchPushesTheOthersLeft();
    void leftBranchesMirrorTheRight();
    void noOverlapsOnBothSides();
    void edgesEnterFromTheParentSide();
    void structureAndSidesAreSaved();
    void oldFileWithManualMovesKeepsItsSides();
    void nodeGrowsAwayFromItsParent();
    void longerTitleKeepsChildrenClearAndUndoes();
    void balanceSwitchOffAndOn();
    void balanceSwitchFollowsTheSheet();
    void newSheetIsLaidOut();
    void firstLevelAroundTheCentre();
    void dragReordersWithBalance();
    void distancesBetweenBranches();
    void branchShapes();
    void dragTargets();
    void twoMainsStayRight();
    void floatingTopics();
    void placeFloatingTopicWithTheMouse();
    void branchesFollowTheDirection();
    void collapseBranches();
    void structuresOfTheSheetAndOfABranch();
    void structureCombinations();
    void arrowKeysMoveBetweenNodes();
    void compactMapAndFreeBranches();
    void mapPageNumbering();
    void justifyLevels();
    void branchEndMarksOnTheMap();
    void deleteKeepsANeighbourSelected();
    void wheelScrollsVerticallyAndShiftHorizontally();
    void taperedOnlyFromTheCentre();
    void variantsSetTheLookOfTheSheet();
    void snapshotForVisualReview();

private:
    // Mappa nuova: centrale e rami principali con il numero di figli dato per ognuno.
    QList<MindNode *> newMap(const QList<int> &childrenPerBranch);
    NodeItem *itemFor(const MindNode *node) const;
    QRectF box(const MindNode *node) const;
    void trigger(QKeySequence::StandardKey key);
    void step();  // chiude il passo di storico in corso
    // Trascina il nodo con il mouse fino al punto della scena (in piu' passi), poi rilascia.
    void dragTo(const MindNode *node, const QPointF &scenePos, bool cancelWithEscape = false);

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    MindMapView *m_view = nullptr;
};

void BalancedLayoutTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisBalancedLayoutTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);
    m_window = std::make_unique<MainWindow>();
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    qobject_cast<QStackedWidget *>(m_window->centralWidget())->setCurrentIndex(1);
    m_model = m_window->findChild<MindMapModel *>();
    m_view = m_window->findChild<MindMapView *>();
    m_scene = qobject_cast<MindMapScene *>(m_view->scene());
    QVERIFY(m_model && m_scene);
}

void BalancedLayoutTest::cleanupTestCase() { m_window.reset(); }

QList<MindNode *> BalancedLayoutTest::newMap(const QList<int> &childrenPerBranch) {
    QJsonArray branches;
    for (int i = 0; i < childrenPerBranch.size(); ++i) {
        QJsonArray children;
        for (int c = 0; c < childrenPerBranch[i]; ++c)
            children.append(QJsonObject{{"id", QStringLiteral("b%1c%2").arg(i + 1).arg(c + 1)}, {"title", QStringLiteral("Sotto-argomento %1").arg(c + 1)}});
        branches.append(QJsonObject{{"id", QStringLiteral("b%1").arg(i + 1)}, {"title", QStringLiteral("Argomento principale %1").arg(i + 1)}, {"children", children}});
    }
    const QJsonObject map{{"root", QJsonObject{{"id", "root"}, {"title", "Argomento centrale"}, {"children", branches}}}, {"balanced", true}};
    if (!m_model->fromJson(map)) return {};
    m_scene->autoLayout();
    QApplication::processEvents();
    QList<MindNode *> mains;
    for (const auto &child : m_model->root()->children) mains.append(child.get());
    return mains;
}

NodeItem *BalancedLayoutTest::itemFor(const MindNode *node) const {
    for (auto *item : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node) return nodeItem;
    return nullptr;
}

QRectF BalancedLayoutTest::box(const MindNode *node) const {
    const NodeItem *item = itemFor(node);
    return item ? item->mapRectToScene(item->shapeRect()) : QRectF();
}

void BalancedLayoutTest::balancedSidesRules() {
    using MapStructure::balancedSides;
    QCOMPARE(balancedSides({10}, {0}), QList<int>{1});                        // uno solo: a destra
    QCOMPARE(balancedSides({10, 10}, {0, 0}), QList<int>({1, 1}));            // due: tutti e due a destra
    QCOMPARE(balancedSides({1, 30}, {0, 0}), QList<int>({1, 1}));             // anche se uno e' grande
    QCOMPARE(balancedSides({10, 10, 10}, {0, 0, 0}), QList<int>({1, 1, -1}));  // a parita' piu' a destra
    QCOMPARE(balancedSides({10, 10, 10, 10}, {0, 0, 0, 0}), QList<int>({1, 1, -1, -1}));
    // Foto di riferimento 2026-10-21 (pesi = righe): il ramo 1 con 5, 11, 12 figli lascia il 2 a destra,
    // con 13 lo manda a sinistra.
    QCOMPARE(balancedSides({5, 1, 1, 1}, {0, 0, 0, 0}), QList<int>({1, 1, -1, -1}));
    QCOMPARE(balancedSides({12, 1, 1, 1}, {0, 0, 0, 0}), QList<int>({1, 1, -1, -1}));
    QCOMPARE(balancedSides({13, 1, 1, 1}, {0, 0, 0, 0}), QList<int>({1, -1, -1, -1}));
    QCOMPARE(balancedSides({14, 14, 1, 1}, {0, 0, 0, 0}), QList<int>({1, -1, -1, -1}));    // foto 12
    QCOMPARE(balancedSides({14, 20, 1, 1}, {0, 0, 0, 0}), QList<int>({1, -1, -1, -1}));    // foto 13
    QCOMPARE(balancedSides({14, 18, 1, 13}, {0, 0, 0, 0}), QList<int>({1, 1, -1, -1}));    // foto 14
    QCOMPARE(balancedSides({14, 18, 14, 13}, {0, 0, 0, 0}), QList<int>({1, 1, -1, -1}));   // foto 15
    // User's reference photo 2026-10-23: 13 subtopics and five lone main topics, two on the right.
    QCOMPARE(balancedSides({13, 1, 1, 1, 1, 1}, QList<int>(6, 0)), QList<int>({1, 1, -1, -1, -1, -1}));
    // I lati scelti restano; gli automatici bilanciano attorno a loro, sempre nell'ordine.
    QCOMPARE(balancedSides({10, 10, 10, 10}, {-1, 0, 0, 0}), QList<int>({-1, 1, 1, -1}));
    QCOMPARE(balancedSides({}, {}), QList<int>());
    // Reference capture 2026-10-22: 16 main topics, the first two with 7 and 5 subtopics: 8 per side
    // even if the right side is much taller (18 rows against 8).
    QList<double> sixteen(16, 1.0);
    sixteen[0] = 7;
    sixteen[1] = 5;
    QList<int> eightEight(16, -1);
    for (int i = 0; i < 8; ++i) eightEight[i] = 1;
    QCOMPARE(balancedSides(sixteen, QList<int>(16, 0)), eightEight);
    QCOMPARE(MapStructure::typeFromKey(QStringLiteral("org")), MapStructure::Type::OrgChart);
    QCOMPARE(MapStructure::directionFromKey(QStringLiteral("xyz"), MapStructure::Type::OrgChart), MapStructure::Direction::Down);
}

void BalancedLayoutTest::oneBranchGoesRight() {
    const QList<MindNode *> mains = newMap({2});
    QVERIFY(box(mains[0]).left() > box(m_model->root()).right());
    QCOMPARE(m_scene->sideOf(mains[0]), 1);
    QCOMPARE(m_model->root()->position, QPointF(0, 0));
}

// Quattro rami uguali: due a destra (1 sopra 2), due a sinistra in senso orario (3 sotto, 4 sopra).
void BalancedLayoutTest::fourBranchesClockwise() {
    const QList<MindNode *> m = newMap({1, 1, 1, 1});
    const qreal centre = m_model->root()->position.x();
    QVERIFY(m[0]->position.x() > centre && m[1]->position.x() > centre);
    QVERIFY(m[2]->position.x() < centre && m[3]->position.x() < centre);
    QVERIFY(m[0]->position.y() < m[1]->position.y());
    QVERIFY(m[3]->position.y() < m[2]->position.y());
    // Ogni lato centrato sul centrale.
    QVERIFY(std::abs((m[0]->position.y() + m[1]->position.y()) / 2) < 1.0);
    QVERIFY(std::abs((m[2]->position.y() + m[3]->position.y()) / 2) < 1.0);
}

// Foto 03 di FEAT-012: un ramo con 13 figli resta solo a destra, gli altri tre vanno a sinistra.
void BalancedLayoutTest::bigBranchPushesTheOthersLeft() {
    const QList<MindNode *> m = newMap({13, 0, 0, 0});
    QCOMPARE(m_scene->sideOf(m[0]), 1);
    for (int i = 1; i < 4; ++i) QCOMPARE(m_scene->sideOf(m[i]), -1);
    QVERIFY(m[3]->position.y() < m[2]->position.y() && m[2]->position.y() < m[1]->position.y());
}

// A sinistra i figli stanno a sinistra del genitore, a 80 px dal suo lato, con il lato destro
// rivolto verso di lui (specchio della destra).
void BalancedLayoutTest::leftBranchesMirrorTheRight() {
    const QList<MindNode *> m = newMap({3, 3, 3});
    QCOMPARE(m_scene->sideOf(m[2]), -1);
    const QRectF parent = box(m[2]);
    for (const auto &child : m[2]->children) {
        QCOMPARE(m_scene->sideOf(child.get()), -1);
        QVERIFY(box(child.get()).right() < parent.left());
    }
    const QRectF right = box(m[0]);
    for (const auto &child : m[0]->children) QVERIFY(box(child.get()).left() > right.right());
    // Stessa distanza dal genitore sui due lati.
    const qreal gapRight = box(m[0]->children[0].get()).left() - right.right();
    const qreal gapLeft = parent.left() - box(m[2]->children[0].get()).right();
    QVERIFY2(std::abs(gapRight - gapLeft) < 2.0, qPrintable(QStringLiteral("%1 vs %2").arg(gapRight).arg(gapLeft)));
}

void BalancedLayoutTest::noOverlapsOnBothSides() {
    newMap({5, 2, 7, 1, 3, 0});
    QList<QRectF> boxes;
    for (auto *item : m_scene->items())
        if (auto *node = qgraphicsitem_cast<NodeItem *>(item)) boxes.append(node->mapRectToScene(node->shapeRect()));
    QVERIFY(boxes.size() > 20);
    for (int i = 0; i < boxes.size(); ++i)
        for (int j = i + 1; j < boxes.size(); ++j)
            QVERIFY2(!boxes[i].intersects(boxes[j]), qPrintable(QStringLiteral("%1 e %2").arg(i).arg(j)));
}

// Il ramo esce dal lato del genitore verso il figlio ed entra dal lato opposto del figlio.
void BalancedLayoutTest::edgesEnterFromTheParentSide() {
    const QList<MindNode *> m = newMap({1, 1});
    for (MindNode *main : m) {
        QGraphicsPathItem *edge = nullptr;
        const QPointF centre = itemFor(main)->pos();
        for (auto *item : m_scene->items())
            if (auto *path = qgraphicsitem_cast<QGraphicsPathItem *>(item); path && std::floor(path->zValue()) == -1) {
                const QPainterPath shape = path->path();
                if (QLineF(shape.pointAtPercent(1), centre).length() < box(main).width()) edge = path;
            }
        QVERIFY(edge);
        const QPointF end = edge->path().pointAtPercent(1);
        const QRectF target = box(main);
        if (m_scene->sideOf(main) > 0) QVERIFY(std::abs(end.x() - target.left()) < 3);
        else QVERIFY(std::abs(end.x() - target.right()) < 3);
    }
}

void BalancedLayoutTest::structureAndSidesAreSaved() {
    const QList<MindNode *> m = newMap({1, 1});
    m_model->root()->structure = MapStructure::typeKey(MapStructure::Type::LogicChart);
    m_model->root()->structureDirection = QStringLiteral("left");
    m[0]->side = MindNode::Side::Left;
    m_model->balanced = false;
    const QJsonObject saved = m_model->toJson();
    QVERIFY(m_model->fromJson(saved));
    QCOMPARE(m_model->root()->structure, QStringLiteral("logic"));
    QCOMPARE(MapStructure::directionOf(m_model->root()->children[1].get()), MapStructure::Direction::Left);
    QCOMPARE(m_model->root()->children[0]->side, MindNode::Side::Left);
    QCOMPARE(m_model->balanced, false);
    QCOMPARE(saved.value(QStringLiteral("balanced")).toBool(true), false);
}

// File di prima (senza "balanced"): con spostamenti a mano si apre con il bilanciamento spento e i
// rami a destra come allora; un ramo nuovo va dal lato con meno rami e ci resta.
void BalancedLayoutTest::oldFileWithManualMovesKeepsItsSides() {
    const QJsonObject old{{"root", QJsonObject{{"id", "root"}, {"title", "Vecchia"}, {"children", QJsonArray{
        QJsonObject{{"id", "a"}, {"title", "A"}, {"manualDx", 30.0}, {"manualDy", 0.0}},
        QJsonObject{{"id", "b"}, {"title", "B"}}}}}}};
    QVERIFY(m_model->fromJson(old));
    QCOMPARE(m_model->balanced, false);
    for (const auto &child : m_model->root()->children) QCOMPARE(child->side, MindNode::Side::Right);
    MindNode *fresh = m_model->addChild(m_model->root(), QStringLiteral("Nuovo"));
    m_scene->autoLayout();
    QCOMPARE(fresh->side, MindNode::Side::Left);
    QVERIFY(fresh->position.x() < m_model->root()->position.x());
    // Senza spostamenti a mano: bilanciato.
    const QJsonObject clean{{"root", QJsonObject{{"id", "root"}, {"title", "Pulita"}, {"children", QJsonArray{QJsonObject{{"id", "a"}, {"title", "A"}}}}}}};
    QVERIFY(m_model->fromJson(clean));
    QCOMPARE(m_model->balanced, true);
}

// FEAT-034: mentre si scrive il lato verso il genitore resta fermo (a destra quello sinistro, a
// sinistra quello destro); il centrale si allarga dai due lati.
void BalancedLayoutTest::nodeGrowsAwayFromItsParent() {
    const QList<MindNode *> m = newMap({0, 0, 0});
    QCOMPARE(m_scene->sideOf(m[2]), -1);
    for (MindNode *node : {m[0], m[2], m_model->root()}) {
        NodeItem *item = itemFor(node);
        const QRectF before = box(node);
        item->beginEditing();
        QApplication::processEvents();
        QTest::keyClicks(m_view->viewport(), QStringLiteral("un titolo molto piu' lungo di prima"));
        QApplication::processEvents();
        const QRectF typing = box(node);
        QVERIFY(typing.width() > before.width() + 20);  // fino alla larghezza massima, poi va a capo
        const int side = node->parent ? m_scene->sideOf(node) : 0;
        if (side > 0) QVERIFY2(std::abs(typing.left() - before.left()) < 1.0, "a destra: lato sinistro fermo");
        else if (side < 0) QVERIFY2(std::abs(typing.right() - before.right()) < 1.0, "a sinistra: lato destro fermo");
        else QVERIFY(std::abs(typing.center().x() - before.center().x()) < 1.0);
        QTest::keyClick(m_view->viewport(), Qt::Key_Return);
        QApplication::processEvents();
        // Dopo la conferma (nuovo layout) nessun salto: lo stesso lato resta dov'era.
        const QRectF after = box(node);
        if (side > 0) QVERIFY(std::abs(after.left() - before.left()) < 1.0);
        else if (side < 0) QVERIFY(std::abs(after.right() - before.right()) < 1.0);
    }
}

// BUG-039 (user's photos 2026-11-04): a longer title covered the topic's child while typing, and
// Ctrl+Z gave back the old size but left the topic where the longer title had put it.
void BalancedLayoutTest::longerTitleKeepsChildrenClearAndUndoes() {
    const QJsonObject grandchild{{"id", "gc"}, {"title", "Sotto-argomento 1"}};
    const QJsonObject child{{"id", "c"}, {"title", "Sotto-argomento 2"}, {"children", QJsonArray{grandchild}}};
    const QJsonObject branch{{"id", "b"}, {"title", "Argomento principale 1"}, {"children", QJsonArray{child}}};
    const QJsonObject map{{"root", QJsonObject{{"id", "root"}, {"title", "Argomento centrale"}, {"children", QJsonArray{branch}}}}, {"balanced", true}};
    QVERIFY(m_model->fromJson(map));
    m_scene->autoLayout();
    step();
    MindNode *c = m_model->root()->children[0]->children[0].get();
    MindNode *gc = c->children[0].get();
    QCOMPARE(m_scene->sideOf(c), 1);
    const QRectF childBefore = box(c), grandchildBefore = box(gc);
    const qreal gap = grandchildBefore.left() - childBefore.right();
    QVERIFY(gap > 0);
    NodeItem *item = itemFor(c);
    item->beginEditingAtEnd();
    QApplication::processEvents();
    QTest::keyClicks(m_view->viewport(), QStringLiteral("asss"));
    QTest::qWait(300);  // longer than the history's wait (150 ms): typing slowly
    QTest::keyClicks(m_view->viewport(), QStringLiteral("ssss"));
    QApplication::processEvents();
    QVERIFY(box(c).width() > childBefore.width() + 20);
    QVERIFY2(std::abs(box(gc).left() - box(c).right() - gap) < 1.0, "while typing the child keeps its distance");
    QTest::keyClick(m_view->viewport(), Qt::Key_Return);
    step();
    QVERIFY(std::abs(box(c).left() - childBefore.left()) < 1.0);
    QVERIFY(std::abs(box(gc).left() - box(c).right() - gap) < 1.0);
    trigger(QKeySequence::Undo);
    QCOMPARE(c->title, QStringLiteral("Sotto-argomento 2"));
    QVERIFY2(std::abs(box(c).left() - childBefore.left()) < 1.0, "undone: the topic is back in its place");
    QVERIFY(std::abs(box(c).width() - childBefore.width()) < 1.0);
    QVERIFY(std::abs(box(gc).left() - grandchildBefore.left()) < 1.0);
    // Esc drops the text and the layout of the preview with it.
    item->beginEditingAtEnd();
    QApplication::processEvents();
    QTest::keyClicks(m_view->viewport(), QStringLiteral("asssssss"));
    QApplication::processEvents();
    QVERIFY(box(gc).left() > grandchildBefore.left() + 20);
    QTest::keyClick(m_view->viewport(), Qt::Key_Escape);
    step();
    QVERIFY(std::abs(box(c).left() - childBefore.left()) < 1.0);
    QVERIFY(std::abs(box(gc).left() - grandchildBefore.left()) < 1.0);
    QCOMPARE(gc->position, itemFor(gc)->pos());
}

void BalancedLayoutTest::trigger(QKeySequence::StandardKey key) {
    for (auto *action : m_window->actions())
        if (action->shortcuts().contains(QKeySequence(key))) { action->trigger(); break; }
    QApplication::processEvents();
}

void BalancedLayoutTest::step() {
    QApplication::processEvents();
    m_window->findChild<Document *>()->flushPendingStep();
}

// FEAT-013: "Bilancia mappa" nella pagina Mappa. Spento: la disposizione resta e ogni ramo tiene
// il suo lato; acceso: spostamenti a mano tolti e mappa bilanciata. Un passo ciascuno; Ctrl+Z e
// Ctrl+Y aggiornano anche lo switch.
void BalancedLayoutTest::balanceSwitchOffAndOn() {
    const QList<MindNode *> m = newMap({1, 1, 1, 1});
    step();
    auto *toggle = m_window->findChild<ToggleSwitch *>(QStringLiteral("BalanceMapSwitch"));
    QVERIFY(toggle);
    emit m_model->structureChanged();  // la pagina rilegge il foglio
    QApplication::processEvents();
    QVERIFY(toggle->isEnabled());
    QVERIFY(toggle->isChecked());
    QList<QPointF> before;
    for (MindNode *node : m) before.append(node->position);
    toggle->click();
    QApplication::processEvents();
    QCOMPARE(m_model->balanced, false);
    QVERIFY(!toggle->isChecked());
    for (int i = 0; i < m.size(); ++i) QCOMPARE(m[i]->position, before[i]);  // nulla si sposta
    QCOMPARE(m[0]->side, MindNode::Side::Right);
    QCOMPARE(m[3]->side, MindNode::Side::Left);
    // Spento: uno spostamento a mano resta (passo a se').
    m[0]->manualOffset = QPointF(40, 0);
    m_scene->autoLayout();
    QCOMPARE(m[0]->position, before[0] + QPointF(40, 0));
    m_window->findChild<Document *>()->markChanged();
    step();
    // Acceso: spostamenti tolti, lati di nuovo automatici, mappa bilanciata.
    toggle->click();
    QApplication::processEvents();
    QCOMPARE(m_model->balanced, true);
    QVERIFY(toggle->isChecked());
    QCOMPARE(m[0]->manualOffset, QPointF());
    QCOMPARE(m[0]->side, MindNode::Side::Auto);
    QCOMPARE(m[0]->position, before[0]);
    step();
    trigger(QKeySequence::Undo);  // un passo: di nuovo spento, con lo spostamento
    QCOMPARE(m_model->balanced, false);
    QVERIFY(!toggle->isChecked());
    QCOMPARE(m[0]->manualOffset, QPointF(40, 0));
    QCOMPARE(m[0]->position, before[0] + QPointF(40, 0));
    trigger(QKeySequence::Redo);
    QCOMPARE(m_model->balanced, true);
    QVERIFY(toggle->isChecked());
    QCOMPARE(m[0]->position, before[0]);
    // Direzione a un solo lato: lo switch c'e' ma e' spento e disattivato.
    m_model->root()->structure = MapStructure::typeKey(MapStructure::Type::MindMap);
    m_model->root()->structureDirection = QStringLiteral("right");
    emit m_model->structureChanged();
    QApplication::processEvents();
    const bool enabled = toggle->isEnabled(), checked = toggle->isChecked();
    m_model->root()->structure.clear();
    m_model->root()->structureDirection.clear();
    emit m_model->structureChanged();
    QApplication::processEvents();
    QVERIFY(!enabled);
    QVERIFY(!checked);
    QVERIFY(toggle->isEnabled());
}

// Ogni foglio ha il suo valore; cambiare foglio aggiorna lo switch senza passi di storico.
void BalancedLayoutTest::balanceSwitchFollowsTheSheet() {
    newMap({1, 1});
    step();
    auto *toggle = m_window->findChild<ToggleSwitch *>(QStringLiteral("BalanceMapSwitch"));
    emit m_model->structureChanged();
    QApplication::processEvents();
    if (!toggle->isChecked()) toggle->click();
    toggle->click();  // primo foglio: spento
    QApplication::processEvents();
    QCOMPARE(m_model->balanced, false);
    step();
    QToolButton *addSheet = nullptr;
    for (auto *button : m_window->findChildren<QToolButton *>())
        if (button->toolTip() == QStringLiteral("Add sheet")) addSheet = button;
    QVERIFY(addSheet);
    // FEAT-026 fase D: "+ foglio" apre la galleria; si conferma la prima variante.
    QTimer::singleShot(0, [] { if (auto *gallery = qobject_cast<QDialog *>(QApplication::activeModalWidget())) gallery->accept(); });
    addSheet->click();
    QApplication::processEvents();
    QCOMPARE(m_model->balanced, true);  // foglio nuovo: attivo
    QVERIFY(toggle->isChecked());
    QComboBox *sheets = nullptr;
    for (auto *combo : m_window->findChildren<QComboBox *>())
        if (!combo->isEditable() && combo->count() >= 2 && combo->itemText(0).startsWith(QStringLiteral("Sheet"))) sheets = combo;
    QVERIFY(sheets);
    const bool undoBefore = m_window->findChild<Document *>()->canUndo();
    sheets->setCurrentIndex(0);
    emit sheets->activated(0);  // come la scelta dell'utente
    QApplication::processEvents();
    QCOMPARE(m_model->balanced, false);
    QVERIFY(!toggle->isChecked());
    QCOMPARE(m_window->findChild<Document *>()->canUndo(), undoBefore);
    sheets->setCurrentIndex(1);
    emit sheets->activated(1);
    QApplication::processEvents();
    QVERIFY(toggle->isChecked());
}

// Bug: a new sheet showed its four topics stacked until the next edit laid the map out.
void BalancedLayoutTest::newSheetIsLaidOut() {
    newMap({1});
    step();
    QToolButton *addSheet = nullptr;
    for (auto *button : m_window->findChildren<QToolButton *>())
        if (button->toolTip() == QStringLiteral("Add sheet")) addSheet = button;
    QVERIFY(addSheet);
    auto *document = m_window->findChild<Document *>();
    const bool undoBefore = document->canUndo();
    addSheet->click();
    QApplication::processEvents();
    step();
    const auto &topics = m_model->root()->children;
    QCOMPARE(int(topics.size()), 4);
    QList<QRectF> boxes{box(m_model->root())};
    for (const auto &topic : topics) boxes.append(box(topic.get()));
    for (int i = 0; i < boxes.size(); ++i)
        for (int j = i + 1; j < boxes.size(); ++j) QVERIFY2(!boxes[i].intersects(boxes[j]), qPrintable(QString("%1 %2").arg(i).arg(j)));
    QCOMPARE(document->canUndo(), undoBefore);  // the layout is the sheet's start, not a step
}

// Foto per il confronto a vista (solo con MENTIS_SNAPSHOT_DIR, piattaforma con i caratteri).
void BalancedLayoutTest::dragTo(const MindNode *node, const QPointF &scenePos, bool cancelWithEscape) {
    const QRectF from = box(node);
    m_view->ensureVisible(from, 0, 0);
    QApplication::processEvents();
    // Si preme sopra il testo, lontano dai bordi che allargano il nodo.
    const QPoint start = m_view->mapFromScene(QPointF(from.center().x(), from.top() + 4));
    const QPoint end = m_view->mapFromScene(scenePos);
    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, start);
    for (int i = 1; i <= 4; ++i) {
        const QPoint at = start + (end - start) * i / 4;
        QMouseEvent move(QEvent::MouseMove, QPointF(at), m_view->viewport()->mapToGlobal(QPointF(at)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(m_view->viewport(), &move);
    }
    if (cancelWithEscape) QTest::keyClick(m_view, Qt::Key_Escape);
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, end);
    QApplication::processEvents();
}

// Foto di riferimento dell'utente (2026-10-21): conta la posizione dei rami principali, non l'ingombro dei
// figli. Con piu' rami su un lato il primo e l'ultimo stanno alla stessa distanza dal centrale e non
// lo affiancano; un ramo da solo su un lato sta sopra il centrale.
void BalancedLayoutTest::firstLevelAroundTheCentre() {
    const QList<MindNode *> m = newMap({13, 0, 0, 0});  // foto 10: il ramo 1 da solo a destra
    const QRectF centre = box(m_model->root());
    QCOMPARE(m_scene->sideOf(m[0]), 1);
    QVERIFY2(box(m[0]).bottom() <= centre.top() + 0.5, "ramo da solo a destra: sopra il centrale");
    QVERIFY(centre.top() - box(m[0]).bottom() < 12);  // appena sopra, non lontano
    // A sinistra tre rami: quello di mezzo all'altezza del centrale, gli altri due simmetrici.
    QVERIFY(std::abs(m[2]->position.y() - m_model->root()->position.y()) < 1.0);
    QVERIFY(std::abs((m[1]->position.y() + m[3]->position.y()) / 2 - m_model->root()->position.y()) < 1.0);
    // Two close main topics (no children) on a side move apart; as measured live (FEAT-072 A2) they may
    // reach into the central box by half its vertical padding, not more.
    const QList<MindNode *> four = newMap({0, 0, 0, 0});
    const QRectF root = box(m_model->root());
    const qreal overlap = NodeStyle::kRootTextPadding.y() / 2;
    QVERIFY(box(four[0]).bottom() <= root.top() + overlap + 0.5);
    QVERIFY(box(four[1]).top() >= root.bottom() - overlap - 0.5);
    // Here the sibling gap already sets them apart: centres 141 px with Atkinson Hyperlegible, whose lines are
    // taller (132 with Atkinson Hyperlegible Next, 130 in the reference).
    QVERIFY(std::abs(four[1]->position.y() - four[0]->position.y() - 141) < 3);
    QVERIFY(box(four[3]).bottom() <= root.top() + overlap + 0.5);
    QVERIFY(box(four[2]).top() >= root.bottom() - overlap - 0.5);
    // Un ramo grande: la meta' fra il primo e l'ultimo ramo e' all'altezza del centrale, anche se i
    // figli del primo salgono molto piu' di quanto scendano quelli del secondo.
    const QList<MindNode *> big = newMap({1, 1, 8, 4});
    for (const int sign : {1, -1}) {
        QList<MindNode *> side;
        for (MindNode *node : big) if (m_scene->sideOf(node) == sign) side.append(node);
        if (side.size() < 2) continue;
        qreal top = side.front()->position.y(), bottom = top;
        for (MindNode *node : side) { top = std::min(top, node->position.y()); bottom = std::max(bottom, node->position.y()); }
        QVERIFY(std::abs((top + bottom) / 2 - m_model->root()->position.y()) < 1.0);
    }
}

// Foto di riferimento dell'utente (2026-10-21, confermate): 54 px dal centrale ai rami principali (live,
// FEAT-072; prima 80), 28 px
// da un ramo ai suoi figli (misurato dal vivo, 2026-10-21; prima 32), 6 px fra i figli (fra le forme visibili; misurato dal vivo, prima 8), 32 fra i rami principali.
// FEAT-072 (measured live 2026-10-22): below the main topics a rounded bracket by default; a shape chosen
// on a topic applies to its own children only and is saved; fans put the children 54 px out, not 28.
void BalancedLayoutTest::branchShapes() {
    using namespace BranchPath;
    // Bracket: stub of 12 px, bar, rounded corner only when asked, horizontal into the child.
    const QPainterPath rounded = bracket({0, 0}, {28, -40}, true);
    QCOMPARE(QPointF(rounded.elementAt(1)), QPointF(12, 0));
    QCOMPARE(QPointF(rounded.elementAt(rounded.elementCount() - 1)), QPointF(28, -40));
    QVERIFY(rounded.elementCount() > bracket({0, 0}, {28, -40}, false).elementCount());  // the curve of the corner
    QCOMPARE(bracket({0, 0}, {28, 0}, true).elementCount(), 2);                          // level: straight
    const QPainterPath straightFan = fan({0, 0}, {54, -40}, static_cast<int>(BranchShape::Straight));
    QCOMPARE(QPointF(straightFan.elementAt(1)), QPointF(12, 0));                          // common point
    const QList<MindNode *> m = newMap({3, 0, 0, 0});
    QCOMPARE(m[0]->branchShape, BranchShape::Auto);
    QVERIFY(m_model->setStyle({m[0]}, StyleProperty::BranchShape, static_cast<int>(BranchShape::Straight)));
    QApplication::processEvents();  // the scene relayouts by itself when a shape changes
    QCOMPARE(m[0]->children[0]->branchShape, BranchShape::Auto);  // not inherited
    const QRectF parent = box(m[0]);
    for (const auto &child : m[0]->children)
        QVERIFY2(std::abs(box(child.get()).left() - parent.right() - 54) < 1.0, qPrintable(QString::number(box(child.get()).left() - parent.right())));
    const QJsonObject saved = m_model->toJson();
    const QJsonObject first = saved["root"].toObject()["children"].toArray().at(0).toObject();
    QCOMPARE(first["branchShape"].toString(), QStringLiteral("straight"));
    QVERIFY(!first["children"].toArray().at(0).toObject().contains(QStringLiteral("branchShape")));  // only the chosen one
    QVERIFY(m_model->setStyle({m[0]}, StyleProperty::BranchShape, static_cast<int>(BranchShape::Auto)));  // back to the default
    QCOMPARE(m[0]->branchShape, BranchShape::Auto);
    QApplication::processEvents();
    // The parent sits at the height of the middle child (straight line), and a large first
    // subtree does not pull it off a child: it snaps to the nearest one.
    QVERIFY(std::abs(m[0]->position.y() - m[0]->children[1]->position.y()) < 0.5);
    for (int i = 0; i < 4; ++i) m_model->addChild(m[0]->children[0].get(), QStringLiteral("Nipote %1").arg(i));
    m_scene->autoLayout();
    bool onAChild = false;
    for (const auto &child : m[0]->children) onAChild = onAChild || std::abs(child->position.y() - m[0]->position.y()) < 0.5;
    QVERIFY(onAChild);
}

void BalancedLayoutTest::distancesBetweenBranches() {
    const QList<MindNode *> m = newMap({5, 0, 0, 0, 2});
    const QRectF root = box(m_model->root());
    QCOMPARE(m_scene->sideOf(m[0]), 1);
    QVERIFY(std::abs(box(m[0]).left() - root.right() - 54) < 1.0);
    const QRectF parent = box(m[0]);
    for (int i = 0; i < 5; ++i) {
        const QRectF child = box(m[0]->children[i].get());
        QVERIFY2(std::abs(child.left() - parent.right() - 28) < 1.0, qPrintable(QString::number(child.left() - parent.right())));
        if (i > 0) {
            const qreal gap = child.top() - box(m[0]->children[i - 1].get()).bottom();
            QVERIFY2(std::abs(gap - 6) < 1.0, qPrintable(QString::number(gap)));  // measured live (FEAT-072; was 8)
        }
    }
    // Fra due rami principali sullo stesso lato restano 32 px fra le forme delle foglie vicine.
    QList<MindNode *> left;
    for (MindNode *node : m) if (m_scene->sideOf(node) < 0) left.append(node);
    QVERIFY(left.size() >= 2);
    for (int i = 1; i < left.size(); ++i) {
        const QRectF upper = box(left[i]), lower = box(left[i - 1]);  // a sinistra dal basso in alto
        QVERIFY(lower.top() - upper.bottom() >= 32 - 1.0);
    }
    // Un ramo da solo a sinistra (tre rami) sta sotto il centrale: senso orario (foto di riferimento).
    const QList<MindNode *> three = newMap({0, 0, 0});
    QCOMPARE(m_scene->sideOf(three[2]), -1);
    QVERIFY(box(three[2]).top() >= box(m_model->root()).bottom() - 0.5);
}

// FEAT-015: con il bilanciamento il trascinamento cambia ordine o genitore, non la posizione. Un
// passo di storico; Esc e lo sfondo non cambiano nulla.
void BalancedLayoutTest::dragReordersWithBalance() {
    newMap({3, 0, 0});
    m_window->findChild<Document *>()->markChanged();
    step();
    MindNode *branch = m_model->root()->children[0].get();
    MindNode *first = branch->children[0].get();
    MindNode *third = branch->children[2].get();
    // Fra il genitore e i figli, poco sopra il primo: prima di lui.
    dragTo(third, QPointF(box(branch).right() + 16, box(first).top() - 2));
    step();
    QCOMPARE(branch->children[0].get(), third);
    QCOMPARE(third->manualOffset, QPointF());
    QVERIFY(third->position.y() < first->position.y());
    QCOMPARE(itemFor(third)->opacity(), 1.0);
    trigger(QKeySequence::Undo);
    branch = m_model->root()->children[0].get();
    QCOMPARE(branch->children[2]->title, QStringLiteral("Sotto-argomento 3"));
    // Al centro dell'altro ramo principale: ne diventa figlio, dal suo lato.
    MindNode *moved = branch->children[0].get();
    MindNode *other = m_model->root()->children[2].get();  // a sinistra
    dragTo(moved, box(other).center());
    step();
    QCOMPARE(moved->parent, other);
    QCOMPARE(m_scene->sideOf(moved), m_scene->sideOf(other));
    QVERIFY(m_scene->sideOf(moved) < 0);
    QVERIFY(box(moved).right() < box(other).left());
    // Con Esc nulla cambia e il nodo resta al suo posto (sullo sfondo diventa mobile: floatingTopics).
    const QPointF before = moved->position;
    dragTo(moved, box(branch).center(), true);
    QCOMPARE(moved->parent, other);
    QCOMPARE(moved->position, before);
    // Il centrale non si sposta.
    dragTo(m_model->root(), QPointF(300, 300));
    QCOMPARE(m_model->root()->position, QPointF(0, 0));
}

// FEAT-015 (foto dell'utente 2026-10-21): sopra un nodo = suo figlio in fondo; oltre il
// suo lato esterno = suo figlio fra i fratelli all'altezza del mouse; il segnaposto sta dove
// comparira' il nodo. A sinistra i rami principali vanno dal basso in alto.
void BalancedLayoutTest::dragTargets() {
    const QList<MindNode *> m = newMap({0, 0, 0, 0});
    using Kind = MindMapScene::ReorderTarget::Kind;
    const QRectF root = box(m_model->root());
    // Sopra il ramo 2: suo figlio.
    auto target = m_scene->reorderTargetAt(m[0], box(m[1]).center());
    QCOMPARE(target.kind, Kind::Child);
    QCOMPARE(target.parent, m[1]);
    QVERIFY(target.slot.left() > box(m[1]).right());
    // A destra del ramo 2, entro 200 px: suo figlio; il segnaposto a 28 px dal ramo.
    target = m_scene->reorderTargetAt(m[0], box(m[1]).center() + QPointF(box(m[1]).width() / 2 + 50, 0));
    QCOMPARE(target.kind, Kind::Insert);
    QCOMPARE(target.parent, m[1]);
    QVERIFY(std::abs(target.slot.left() - box(m[1]).right() - 28) < 1.0);
    // Fra il centrale e i rami di destra, all'altezza fra il ramo 1 e il 2: il ramo 4 (a sinistra)
    // va fra loro, a destra.
    target = m_scene->reorderTargetAt(m[3], QPointF(root.right() + 40, (box(m[0]).bottom() + box(m[1]).top()) / 2));
    QCOMPARE(target.kind, Kind::Insert);
    QCOMPARE(target.parent, m_model->root());
    QCOMPARE(target.row, 1);
    QCOMPARE(target.side, 1);
    QVERIFY(target.slot.top() > box(m[0]).bottom() - 1 && target.slot.bottom() < box(m[1]).top() + 1);
    // A sinistra, sopra il ramo 4 (il piu' alto): dopo di lui nell'ordine, lato sinistro.
    target = m_scene->reorderTargetAt(m[0], QPointF(root.left() - 40, box(m[3]).top() - 4));
    QCOMPARE(target.kind, Kind::Insert);
    QCOMPARE(target.row, 4);
    QCOMPARE(target.side, -1);
    QVERIFY(target.slot.bottom() < box(m[3]).top() + 1);
    // Al suo posto: niente.
    QCOMPARE(m_scene->reorderTargetAt(m[0], box(m[0]).center()).kind, Kind::None);
    QCOMPARE(m_scene->reorderTargetAt(m[0], QPointF(5000, 5000)).kind, Kind::Float);  // sfondo: argomento mobile
    // Senza bilanciamento il trascinamento resta libero.
    QVERIFY(m_scene->isReorderDrag(m[0]));
    m_model->balanced = false;
    QVERIFY(!m_scene->isReorderDrag(m[0]));
    m_model->balanced = true;
}

// Bug segnalato dall'utente (2026-10-21, foto): portando il ramo 3 sotto il 2 e il 4 sotto il 3 restano
// due rami principali; il 2 passava a sinistra con tutto il ramo. Restano tutti e due a destra.
void BalancedLayoutTest::twoMainsStayRight() {
    const QList<MindNode *> m = newMap({0, 0, 0, 0});
    m_window->reorderNode(m[2], m[1], 0, 0);
    m_window->reorderNode(m[3], m[2], 0, 0);
    QCOMPARE(m_model->root()->children.size(), size_t(2));
    QCOMPARE(m_scene->sideOf(m[0]), 1);
    QCOMPARE(m_scene->sideOf(m[1]), 1);
    QCOMPARE(m_scene->sideOf(m[3]), 1);
    QVERIFY(box(m[3]).left() > box(m[2]).right());
}

// Richiesta dell'utente 2026-10-21: un nodo lasciato sullo sfondo diventa un argomento
// mobile con il suo ramo; i figli si dispongono alla sua destra; i rami nuovi vanno prima dei mobili.
void BalancedLayoutTest::floatingTopics() {
    const QList<MindNode *> m = newMap({1, 0, 0});
    m_window->findChild<Document *>()->markChanged();
    step();
    dragTo(m[0], box(m_model->root()).center() + QPointF(0, 260));
    step();
    QVERIFY(m[0]->floating);
    QCOMPARE(m_model->root()->children.back().get(), m[0]);
    const MindNode *child = m[0]->children[0].get();
    QVERIFY(std::abs(box(child).left() - box(m[0]).right() - 28) < 1.0);
    QVERIFY(std::abs(child->position.y() - m[0]->position.y()) < 1.0);
    QVERIFY(m[0]->position.y() > box(m_model->root()).bottom() + 120);
    MindNode *fresh = m_model->addChild(m_model->root());
    QCOMPARE(m_model->root()->children.back().get(), m[0]);
    QCOMPARE(fresh->title, MindMapModel::defaultTitle(m_model->root(), 3));
    trigger(QKeySequence::Undo);  // un passo: torna ramo principale
    QVERIFY(!m_model->root()->children[0]->floating);
}

// + > Argomento mobile (richiesta dell'utente 2026-10-21): l'argomento segue il cursore e un
// clic lo fissa; Esc annulla senza modifiche ne' passi.
void BalancedLayoutTest::placeFloatingTopicWithTheMouse() {
    newMap({0, 0});
    Document *document = m_window->findChild<Document *>();
    document->markChanged();
    step();
    const size_t before = m_model->root()->children.size();
    m_window->startFloatingPlacement();
    QVERIFY(m_window->isPlacingFloating());
    QCOMPARE(m_model->root()->children.size(), before);  // l'anteprima non e' nel modello
    QTest::keyClick(m_view, Qt::Key_Escape);
    QVERIFY(!m_window->isPlacingFloating());
    QCOMPARE(m_model->root()->children.size(), before);
    m_window->startFloatingPlacement();
    const QPoint at = m_view->mapFromScene(box(m_model->root()).center() + QPointF(0, 200));
    QMouseEvent move(QEvent::MouseMove, QPointF(at), m_view->viewport()->mapToGlobal(QPointF(at)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(m_view->viewport(), &move);
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, at);
    QVERIFY(!m_window->isPlacingFloating());
    QCOMPARE(m_model->root()->children.size(), before + 1);
    MindNode *placed = m_model->root()->children.back().get();
    QVERIFY(placed->floating);
    QVERIFY(QLineF(placed->position, m_view->mapToScene(at)).length() < 1.0);
    trigger(QKeySequence::Undo);  // un passo
    QCOMPARE(m_model->root()->children.size(), before);
}

// FEAT-014: exitPoint (used by angled, square and straight lines) and fromRoot (curves);
// il figlio riceve il ramo dal lato rivolto al centrale, anche spostato a mano.
void BalancedLayoutTest::branchesFollowTheDirection() {
    using namespace BranchPath;
    const QRectF rect(-50, -20, 100, 40);
    QCOMPARE(exitPoint(rect, {200, 0}), QPointF(50, 0));
    QCOMPARE(exitPoint(rect, {0, -300}), QPointF(0, -20));
    QCOMPARE(exitPoint(rect, {100, -100}), QPointF(20, -20));
    QCOMPARE(exitPoint(rect, {10, 5}), QPointF(0, 0));  // dentro: il centro
    // FEAT-072 A7: from exitPoint, a quadratic curve with the control at the child's height, 20% of
    // the horizontal distance from the start (measured live 2026-10-22).
    const QPainterPath up = fromRoot(rect, {200, -300});
    const QPointF start = exitPoint(rect, {200, -300});
    QCOMPARE(QPointF(up.elementAt(0)), start);
    QCOMPARE(up.elementAt(1).type, QPainterPath::CurveToElement);  // quadTo is stored as a cubic
    QCOMPARE(up.elementAt(up.elementCount() - 1).y, -300.0);
    // The cubic form of the quadratic: first control = start + 2/3 (control - start).
    const QPointF control(start.x() + 0.2 * (200 - start.x()), -300);
    QVERIFY(QLineF(QPointF(up.elementAt(1)), start + (control - start) * 2.0 / 3.0).length() < 0.01);
    // Level with the start: straight.
    QCOMPARE(fromRoot(rect, {300, 0}).elementCount(), 2);
    // Spostato a mano nella meta' sinistra, sopra il centrale: il ramo entra dal lato destro.
    const QList<MindNode *> m = newMap({0, 0});
    m_window->setBalanced(false);
    m[0]->manualOffset = QPointF(-600, -200);
    m_scene->autoLayout();
    QApplication::processEvents();
    QGraphicsPathItem *edge = nullptr;
    for (auto *item : m_scene->items())
        if (auto *path = qgraphicsitem_cast<QGraphicsPathItem *>(item); path && std::floor(path->zValue()) == -1 &&
            QLineF(path->path().pointAtPercent(1), itemFor(m[0])->pos()).length() < box(m[0]).width()) edge = path;
    QVERIFY(edge);
    QVERIFY(std::abs(edge->path().pointAtPercent(1).x() - box(m[0]).right()) < 3);
    const QRectF root = box(m_model->root());
    QVERIFY(root.adjusted(-1, -1, 1, 1).contains(edge->path().pointAtPercent(0)));
    QVERIFY(!root.adjusted(1, 1, -1, -1).contains(edge->path().pointAtPercent(0)));  // sul bordo
    m_window->setBalanced(true);
}

// FEAT-016: il controllo vuoto compare sul nodo scelto con figli; il clic nasconde i discendenti
// nella mappa (non li cancella) e il controllo ne mostra il numero, sempre visibile; un passo; la
// mappa si ridispone; il centrale non si chiude; lo stato si salva.
void BalancedLayoutTest::collapseBranches() {
    const QList<MindNode *> m = newMap({3, 2, 0});
    m_model->addChild(m[0]->children[0].get(), QStringLiteral("Nipote"));
    m_scene->autoLayout();
    m_window->findChild<Document *>()->markChanged();
    step();
    NodeItem *item = itemFor(m[0]);
    QVERIFY(item->collapseRect(1).isEmpty());  // aperto e non scelto: niente
    m_scene->selectNode(m[0]);
    QApplication::processEvents();
    const QRectF control = item->collapseRect(1);
    QVERIFY(!control.isEmpty());
    // Open branch: small circle centred on the branch junction, 12 px out (FEAT-072).
    QCOMPARE(control.width(), NodeItem::kCollapseOpenSizePx);
    QVERIFY(std::abs(item->mapRectToScene(control).center().x() - box(m[0]).right() - NodeItem::kCollapseOpenOffsetPx) < 1.0);
    const qreal secondBefore = m[1]->position.y();
    m_view->ensureVisible(item->mapRectToScene(control));
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, m_view->mapFromScene(item->mapToScene(control.center())));
    step();
    QVERIFY(m[0]->collapsed);
    QCOMPARE(m[0]->children.size(), size_t(3));  // nel modello ci sono ancora
    for (const auto &child : m[0]->children) QVERIFY(!itemFor(child.get())->isVisible());
    QVERIFY(!itemFor(m[0]->children[0]->children[0].get())->isVisible());
    // Ridisposta: il ramo chiuso occupa una riga (con il centrale alto come nel riferimento, 2026-10-21, la
    // distanza minima dal centrale puo' tenere il ramo seguente dov'era).
    QVERIFY(m[1]->position.y() <= secondBefore);
    m_scene->clearSelection();
    QApplication::processEvents();
    QVERIFY(!item->collapseRect(1).isEmpty());  // con il numero resta visibile
    const QJsonObject saved = m_model->toJson();
    trigger(QKeySequence::Undo);  // un passo
    QVERIFY(!m_model->root()->children[0]->collapsed);
    QVERIFY(m_model->fromJson(saved));
    QVERIFY(m_model->root()->children[0]->collapsed);
    // Il centrale non si chiude (decisione dell'utente 2026-10-21).
    m_scene->selectNode(m_model->root());
    QApplication::processEvents();
    QVERIFY(itemFor(m_model->root())->collapseRect(-1).isEmpty() && itemFor(m_model->root())->collapseRect(1).isEmpty());
    QVERIFY(!m_window->findChild<Document *>()->setCollapsed(m_model->root(), true));
}

// FEAT-026 fase C (foto di riferimento dell'utente 2026-10-21): organigramma = figli in riga sotto il
// genitore; grafico ad albero = figli in colonna sotto il genitore, a destra del tronco; anche per un
// solo ramo. Nessuna sovrapposizione; scelta dal pannello in un passo.
void BalancedLayoutTest::structuresOfTheSheetAndOfABranch() {
    const auto noOverlaps = [this] {
        QList<QRectF> boxes;
        for (auto *item : m_scene->items())
            if (auto *node = qgraphicsitem_cast<NodeItem *>(item); node && node->isVisible()) boxes.append(node->mapRectToScene(node->shapeRect()));
        for (int i = 0; i < boxes.size(); ++i)
            for (int j = i + 1; j < boxes.size(); ++j) if (boxes[i].intersects(boxes[j])) return false;
        return true;
    };
    QList<MindNode *> m = newMap({2, 3, 1});
    m_model->root()->structure = QStringLiteral("org");
    m_scene->autoLayout();
    const QRectF root = box(m_model->root());
    for (MindNode *main : m) {
        QVERIFY(std::abs(box(main).top() - root.bottom() - 100) < 1.0);  // riga sotto il centrale (FEAT-072: 100)
        for (const auto &child : main->children) QVERIFY(std::abs(box(child.get()).top() - box(main).bottom() - 40) < 1.0);
    }
    QVERIFY(box(m[0]).right() < box(m[1]).left() && box(m[1]).right() < box(m[2]).left());
    QVERIFY(noOverlaps());
    m = newMap({2, 3, 1});
    m_model->root()->structure = QStringLiteral("tree");
    m_scene->autoLayout();
    const QRectF treeRoot = box(m_model->root());
    for (int i = 0; i < m.size(); ++i) {
        QVERIFY(std::abs(box(m[i]).left() - treeRoot.center().x() - 50) < 1.0);  // FEAT-072: 50
        if (i > 0) QVERIFY(box(m[i]).top() > box(m[i - 1]->children.empty() ? m[i - 1] : m[i - 1]->children.back().get()).bottom());
    }
    QVERIFY(noOverlaps());
    // Un ramo ad albero dentro la mappa mentale.
    m = newMap({3, 1, 1});
    m[0]->structure = QStringLiteral("tree");
    m_scene->autoLayout();
    for (const auto &child : m[0]->children) {
        QVERIFY(box(child.get()).top() > box(m[0]).bottom());
        QVERIFY(std::abs(box(child.get()).left() - box(m[0]).center().x() - 50) < 1.0);
    }
    QVERIFY(noOverlaps());
    // From the panel: sheet type in one step, from the central topic's Structure section (FEAT-072 B13).
    m = newMap({1, 1});
    m_window->findChild<Document *>()->markChanged();
    step();
    m_scene->selectNode(m_model->root());
    QApplication::processEvents();
    auto *picker = m_window->findChild<StructurePicker *>(QStringLiteral("RootStructurePicker"));
    QVERIFY(picker);
    // Come il menu a griglia: activated con l'indice, senza cambiare prima la voce corrente.
    QComboBox *type = picker->typeSelect();
    emit type->activated(type->findData(QStringLiteral("org")));
    QCOMPARE(m_model->root()->structure, QStringLiteral("org"));
    QCOMPARE(m_model->root()->structureDirection, QStringLiteral("down"));
    QVERIFY(box(m[0]).top() > box(m_model->root()).bottom());
    trigger(QKeySequence::Undo);
    QVERIFY(m_model->root()->structure.isEmpty());
    // Bug 2026-10-21: dalla pagina Stile, un ramo scelto diventa un albero e i figli si dispongono.
    m = newMap({3, 1});
    m_scene->selectNode(m[0]);
    QApplication::processEvents();
    auto *branchType = m_window->findChild<StylePage *>()->findChild<QComboBox *>(QStringLiteral("StructureTypeSelect"));
    QVERIFY(branchType);
    emit branchType->activated(branchType->findData(QStringLiteral("tree")));
    QCOMPARE(m[0]->structure, QStringLiteral("tree"));
    for (const auto &child : m[0]->children) QVERIFY(box(child.get()).top() > box(m[0]).bottom());
    QCOMPARE(branchType->currentData().toString(), QStringLiteral("tree"));
    emit branchType->activated(branchType->findData(QString()));  // Come il genitore
    QVERIFY(m[0]->structure.isEmpty());
    for (const auto &child : m[0]->children) QVERIFY(box(child.get()).left() > box(m[0]).right());
}

// Bug segnalato dall'utente (2026-10-21, foto): in un organigramma un ramo con un altro tipo scendeva
// sotto gli altri e i suoi figli ricevevano il ramo dal lato sbagliato. Ogni combinazione di tipo del
// foglio e del ramo: nessuna sovrapposizione, i nodi di una riga di organigramma in linea, ogni ramo
// entra nel figlio dal lato rivolto al genitore.
void BalancedLayoutTest::structureCombinations() {
    const QStringList types{QString(), QStringLiteral("logic"), QStringLiteral("org"), QStringLiteral("tree"), QStringLiteral("mindmap")};
    for (const QString &sheet : types.mid(0, 4)) {
        for (const QString &branch : types) {
            const QList<MindNode *> m = newMap({3, 2, 2});
            m_model->addChild(m[0]->children[0].get(), QStringLiteral("Nipote"));
            m_model->root()->structure = sheet;
            m[0]->structure = branch;
            m_scene->autoLayout();
            const QString label = QStringLiteral("foglio '%1', ramo '%2'").arg(sheet, branch);
            QList<std::pair<const MindNode *, QRectF>> boxes;
            std::function<void(const MindNode *)> collect = [&](const MindNode *node) {
                boxes.append({node, box(node)});
                for (const auto &child : node->children) collect(child.get());
            };
            collect(m_model->root());
            for (int i = 0; i < boxes.size(); ++i)
                for (int j = i + 1; j < boxes.size(); ++j)
                    QVERIFY2(!boxes[i].second.intersects(boxes[j].second),
                             qPrintable(label + QStringLiteral(": %1 su %2").arg(boxes[i].first->title, boxes[j].first->title)));
            if (sheet == QLatin1String("org"))
                for (const MindNode *main : m) QVERIFY2(std::abs(box(main).top() - box(m[1]).top()) < 1.0, qPrintable(label + QStringLiteral(": riga")));
            for (const auto &[node, rect] : std::as_const(boxes)) {
                if (!node->parent) continue;
                const QPainterPath path = m_scene->edgePath(node);
                QVERIFY2(!path.isEmpty(), qPrintable(label + QStringLiteral(": ramo di ") + node->title));
                const QPointF end = path.pointAtPercent(1);
                const QRectF parent = box(node->parent);
                const MapStructure::Type type = MapStructure::typeOf(node->parent);
                QString side;
                if (type == MapStructure::Type::OrgChart) side = std::abs(end.y() - (rect.center().y() > parent.center().y() ? rect.top() : rect.bottom())) < 3 ? QString() : QStringLiteral("sopra/sotto");
                else side = std::abs(end.x() - (rect.center().x() > parent.center().x() ? rect.left() : rect.right())) < 3 ? QString() : QStringLiteral("lato");
                QVERIFY2(side.isEmpty(), qPrintable(label + QStringLiteral(": ingresso di %1 (%2)").arg(node->title, side)));
            }
        }
    }
}

// Richiesta dell'utente 2026-10-21: con le frecce si passa da un nodo all'altro. Verso l'esterno il
// figlio piu' vicino, verso il centro il genitore; dal centrale a destra il primo ramo di destra, a
// sinistra il primo di sinistra (il 3); sopra e sotto lo stesso livello, anche in un altro ramo.
void BalancedLayoutTest::arrowKeysMoveBetweenNodes() {
    const QList<MindNode *> m = newMap({2, 2, 1, 0});
    MindNode *root = m_model->root();
    QCOMPARE(m_scene->neighbour(root, Qt::Key_Right), m[0]);
    QCOMPARE(m_scene->neighbour(root, Qt::Key_Left), m[2]);
    QCOMPARE(m_scene->neighbour(root, Qt::Key_Up), nullptr);
    QVERIFY(m[0]->children.size() == 2);
    MindNode *a2 = m[0]->children[1].get();
    MindNode *b1 = m[1]->children[0].get();
    QVERIFY(m_scene->neighbour(m[0], Qt::Key_Right)->parent == m[0]);
    QCOMPARE(m_scene->neighbour(a2, Qt::Key_Left), m[0]);
    QCOMPARE(m_scene->neighbour(m[0], Qt::Key_Left), root);
    QCOMPARE(m_scene->neighbour(a2, Qt::Key_Down), b1);   // dal piu' basso di un ramo al piu' alto del seguente
    QCOMPARE(m_scene->neighbour(b1, Qt::Key_Up), a2);
    QCOMPARE(m_scene->neighbour(m[0], Qt::Key_Down), m[1]);
    QCOMPARE(m_scene->neighbour(m[2], Qt::Key_Right), root);  // a sinistra il genitore e' a destra
    QCOMPARE(m_scene->neighbour(m[2], Qt::Key_Left), m[2]->children[0].get());
    QCOMPARE(m_scene->neighbour(m[3], Qt::Key_Down), m[2]);   // a sinistra: il 4 sopra il 3
    // Dalla tastiera, sulla vista.
    m_scene->selectNode(m[0]);
    m_view->setFocus();
    QTest::keyClick(m_view, Qt::Key_Down);
    QCOMPARE(m_scene->selectedNode(), m[1]);
    QTest::keyClick(m_view, Qt::Key_Left);
    QCOMPARE(m_scene->selectedNode(), root);
    // Organigramma: in basso i figli, ai lati i fratelli.
    const QList<MindNode *> o = newMap({1, 1});
    m_model->root()->structure = QStringLiteral("org");
    m_scene->autoLayout();
    QCOMPARE(m_scene->neighbour(m_model->root(), Qt::Key_Down), o[0]);
    QCOMPARE(m_scene->neighbour(o[0], Qt::Key_Right), o[1]);
    QCOMPARE(m_scene->neighbour(o[0], Qt::Key_Up), m_model->root());
    QCOMPARE(m_scene->neighbour(o[0], Qt::Key_Down), o[0]->children[0].get());
}

// FEAT-024 (user's request 2026-10-28): the Map page's Numbering is the central topic's, so it
// numbers the whole map; the Style page of the central topic shows the same choice.
void BalancedLayoutTest::mapPageNumbering() {
    QList<MindNode *> m = newMap({2, 0});
    NumberingFields *fields = m_window->findChild<SheetStylePanel *>()->numberingFields();
    QVERIFY(!fields->formatField()->isVisibleTo(fields));  // None
    emit fields->presetField()->activated(fields->presetField()->findText(QStringLiteral("1.2.3")));
    QVERIFY(m_model->root()->style.numbering);
    QCOMPARE(m[0]->number, QStringLiteral("1"));
    QCOMPARE(m[1]->number, QStringLiteral("2"));
    QVERIFY(fields->formatField()->isVisibleTo(fields));
    QCOMPARE(fields->formatField()->text(), QStringLiteral("1.2.3"));
    emit fields->presetField()->activated(fields->presetField()->findText(QStringLiteral("None")));
    QVERIFY(!m_model->root()->style.numbering);
    QVERIFY(m[0]->number.isEmpty());
}

// FEAT-043 parte layout (richiesta dell'utente 2026-10-21): Mappa compatta riduce le distanze; con
// Rami in posizione libera il trascinamento e' libero anche con il bilanciamento e i lati restano;
// con Ramo colorato spento le linee hanno il colore del testo del centrale.
void BalancedLayoutTest::compactMapAndFreeBranches() {
    QList<MindNode *> m = newMap({3, 0});
    const qreal normalGap = box(m[0]->children[0].get()).left() - box(m[0]).right();
    NodeStyle::SheetStyle style = m_model->sheetStyle;
    style.compact = true;
    emit m_window->findChild<SheetStylePanel *>()->sheetStyleChosen(style);
    const qreal compactGap = box(m[0]->children[0].get()).left() - box(m[0]).right();
    QVERIFY2(compactGap < normalGap, qPrintable(QStringLiteral("%1 / %2").arg(compactGap).arg(normalGap)));
    QCOMPARE(m_model->toJson().value(QStringLiteral("compactMap")).toBool(), true);
    style.compact = false;
    style.freeBranches = true;
    emit m_window->findChild<SheetStylePanel *>()->sheetStyleChosen(style);
    QVERIFY(!m_scene->isReorderDrag(m[0]));
    QCOMPARE(m[0]->side, MindNode::Side::Right);
    QCOMPARE(m[1]->side, MindNode::Side::Right);  // due rami: tutti e due a destra, e restano
    auto *balance = m_window->findChild<ToggleSwitch *>(QStringLiteral("BalanceMapSwitch"));
    QVERIFY(!balance->isEnabled());
    m[0]->manualOffset = QPointF(30, 40);
    style.freeBranches = false;
    emit m_window->findChild<SheetStylePanel *>()->sheetStyleChosen(style);
    QCOMPARE(m[0]->manualOffset, QPointF());
    QCOMPARE(m[0]->side, MindNode::Side::Auto);
    QVERIFY(m_scene->isReorderDrag(m[0]));
    // Ramo colorato spento: il colore del testo del centrale.
    style.coloredBranches = false;
    emit m_window->findChild<SheetStylePanel *>()->sheetStyleChosen(style);
    QCOMPARE(m[0]->lineColor, m_model->root()->text);
    QCOMPARE(m[0]->children[0]->lineColor, m_model->root()->text);
    style.coloredBranches = true;
    emit m_window->findChild<SheetStylePanel *>()->sheetStyleChosen(style);
}

// FEAT-043 Justify: same width per level across the sheet, hand widths widened, floating topics
// and Org chart branches left out, everything back when it is off.
void BalancedLayoutTest::justifyLevels() {
    QList<MindNode *> m = newMap({2, 1, 1});
    m[0]->title = QStringLiteral("A much longer main topic title");
    m[1]->customWidth = 90;
    m[0]->children[1]->title = QStringLiteral("Wide subtopic under the first branch");
    m[2]->structure = QStringLiteral("org");
    MindNode *orgChild = m[2]->children[0].get();
    m_scene->autoLayout();
    const qreal shortMain = box(m[1]).width();
    const qreal orgChildWidth = box(orgChild).width();
    const qreal subWidth = box(m[1]->children[0].get()).width();
    NodeStyle::SheetStyle style = m_model->sheetStyle;
    style.justify = true;
    emit m_window->findChild<SheetStylePanel *>()->sheetStyleChosen(style);
    QVERIFY(m_window->findChild<ToggleSwitch *>(QStringLiteral("JustifySwitch"))->isChecked());
    const qreal mainWidth = box(m[0]).width();
    QVERIFY2(mainWidth > shortMain, qPrintable(QStringLiteral("%1 / %2").arg(mainWidth).arg(shortMain)));
    for (MindNode *main : m) QCOMPARE(box(main).width(), mainWidth);  // the Org chart node too
    const qreal widestSub = box(m[0]->children[1].get()).width();
    QVERIFY(widestSub > subWidth);
    QCOMPARE(box(m[0]->children[0].get()).width(), widestSub);
    QCOMPARE(box(m[1]->children[0].get()).width(), widestSub);
    QCOMPARE(box(orgChild).width(), orgChildWidth);  // laid out by the Org chart: not justified
    QCOMPARE(m[1]->customWidth, 90.0);  // the hand width is kept, only widened on screen
    QCOMPARE(m_model->toJson().value(QStringLiteral("justifyTopics")).toBool(), true);
    // A title edited while justified: the level follows the new widest node.
    m[0]->title = QStringLiteral("Short");
    m_scene->autoLayout();
    QVERIFY(box(m[0]).width() < mainWidth);
    QCOMPARE(box(m[1]).width(), box(m[0]).width());
    style.justify = false;
    emit m_window->findChild<SheetStylePanel *>()->sheetStyleChosen(style);
    QCOMPARE(box(m[1]->children[0].get()).width(), subWidth);
    QCOMPARE(m_model->toJson().contains(QStringLiteral("justifyTopics")), false);
}

// FEAT-023: the marks are drawn with the line (they widen its area) and disappear with line style None.
void BalancedLayoutTest::branchEndMarksOnTheMap() {
    QList<MindNode *> m = newMap({1});
    MindNode *child = m[0]->children[0].get();
    const auto edgeArea = [this, child] {
        for (QGraphicsItem *item : m_scene->items())
            if (auto *path = dynamic_cast<QGraphicsPathItem *>(item); path && path->path() == m_scene->edgePath(child))
                return path->boundingRect();
        return QRectF();
    };
    QVERIFY(m_model->setStyle({m[0]}, StyleProperty::BranchWidth, 8.0));
    QVERIFY(m_model->setStyle({m[0]}, StyleProperty::BranchEnd, static_cast<int>(BranchEnd::FilledArrow)));
    const QRectF marked = edgeArea();
    const QPainterPath line = m_scene->edgePath(child);
    const qreal angle = line.angleAtPercent(1) * M_PI / 180.0;
    const QRectF mark = BranchPath::endMark(BranchEnd::FilledArrow, line.pointAtPercent(1),
                                            QPointF(std::cos(angle), -std::sin(angle)), 8).boundingRect();
    QVERIFY2(marked.contains(mark), qPrintable(QStringLiteral("%1 %2").arg(marked.height()).arg(mark.height())));
    QVERIFY(m_model->setStyle({m[0]}, StyleProperty::BranchEnd, static_cast<int>(BranchEnd::None)));
    const QRectF plain = edgeArea();  // same line, no mark
    QVERIFY(marked.contains(plain) && marked != plain);
    QVERIFY(m_model->setStyle({m[0]}, StyleProperty::BranchEnd, static_cast<int>(BranchEnd::FilledArrow)));
    QVERIFY(m_model->setStyle({m[0]}, StyleProperty::BranchStyle, static_cast<int>(Qt::NoPen)));
    QVERIFY(!edgeArea().contains(mark));  // no line, no mark
}

// Richiesta dell'utente 2026-10-21: cancellato un nodo resta attivo il fratello seguente, se no il
// precedente, se no il genitore.
void BalancedLayoutTest::deleteKeepsANeighbourSelected() {
    const QList<MindNode *> m = newMap({3, 1});
    QAction *remove = nullptr;
    for (auto *action : m_window->findChildren<QAction *>())
        if (action->shortcuts().contains(QKeySequence(QKeySequence::Delete))) remove = action;
    QVERIFY(remove);
    MindNode *first = m[0]->children[0].get(), *second = m[0]->children[1].get(), *third = m[0]->children[2].get();
    m_scene->selectNode(second);
    remove->trigger();
    QCOMPARE(m_scene->selectedNode(), third);   // il seguente
    remove->trigger();
    QCOMPARE(m_scene->selectedNode(), first);   // non c'e' piu' un seguente: il precedente
    remove->trigger();
    QCOMPARE(m_scene->selectedNode(), m[0]);    // nessun fratello: il genitore
    QCOMPARE(m_scene->selectedNodes().size(), qsizetype(1));
}

// Richiesta dell'utente 2026-10-21: la rotellina scorre in verticale, con Maiusc in orizzontale.
void BalancedLayoutTest::wheelScrollsVerticallyAndShiftHorizontally() {
    newMap({20, 20, 20, 20});
    m_view->setZoomPercent(200);
    m_view->centerOn(0, 0);
    QApplication::processEvents();
    const auto wheel = [this](Qt::KeyboardModifiers modifiers) {
        const QPointF at(m_view->viewport()->width() / 2.0, m_view->viewport()->height() / 2.0);
        QWheelEvent event(at, m_view->viewport()->mapToGlobal(at), QPoint(), QPoint(0, -120), Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
        QApplication::sendEvent(m_view->viewport(), &event);
    };
    const int x = m_view->horizontalScrollBar()->value(), y = m_view->verticalScrollBar()->value();
    wheel(Qt::NoModifier);
    QCOMPARE(m_view->horizontalScrollBar()->value(), x);
    QVERIFY(m_view->verticalScrollBar()->value() > y);
    const int y2 = m_view->verticalScrollBar()->value();
    wheel(Qt::ShiftModifier);
    QVERIFY(m_view->horizontalScrollBar()->value() > x);
    QCOMPARE(m_view->verticalScrollBar()->value(), y2);
    m_view->setZoomPercent(100);
}

// Foto di riferimento dell'utente (2026-10-21): con l'affusolato solo i rami dal centrale ai principali sono
// aree piene; gli altri restano linee dello spessore scelto.
void BalancedLayoutTest::taperedOnlyFromTheCentre() {
    const QList<MindNode *> m = newMap({2, 0});
    NodeStyle::SheetStyle style = m_model->sheetStyle;
    style.branchWidth = 3;
    style.tapered = true;
    m_model->setSheetStyle(style);
    m_scene->autoLayout();
    const auto edgeOf = [this](const MindNode *child) -> QGraphicsPathItem * {
        for (auto *item : m_scene->items())
            if (auto *path = qgraphicsitem_cast<QGraphicsPathItem *>(item); path && std::floor(path->zValue()) == -1 && path->path() == m_scene->edgePath(child)) return path;
        return nullptr;
    };
    QGraphicsPathItem *main = edgeOf(m[0]);
    QGraphicsPathItem *sub = edgeOf(m[0]->children[0].get());
    QVERIFY(main && sub);
    QCOMPARE(main->pen().style(), Qt::NoPen);   // area piena
    QVERIFY(main->brush().style() != Qt::NoBrush);
    QCOMPARE(sub->pen().style(), Qt::SolidLine);  // linea normale
    QCOMPARE(sub->pen().widthF(), 3.0);
    style.tapered = false;
    style.branchWidth = 0;
    m_model->setSheetStyle(style);
}

void BalancedLayoutTest::variantsSetTheLookOfTheSheet() {
    // FEAT-026 fase D: ogni variante ha un tipo della galleria e un id unico.
    QSet<QString> ids;
    for (const MapVariants::Variant &variant : MapVariants::all()) {
        QVERIFY(MapVariants::galleryTypes().contains(variant.type));
        QVERIFY(!ids.contains(variant.id));
        ids.insert(variant.id);
    }
    QCOMPARE(MapVariants::ofType(MapStructure::Type::MindMap).size(), 12);
    QCOMPARE(MapVariants::ofType(MapStructure::Type::LogicChart).size(), 9);
    QCOMPARE(MapVariants::ofType(MapStructure::Type::OrgChart).size(), 6);
    QCOMPARE(MapVariants::ofType(MapStructure::Type::TreeChart).size(), 6);
    // Senza variante: l'aspetto di sempre, centrale centrato.
    QList<MindNode *> m = newMap({1, 0});
    MindNode *root = m_model->root();
    QCOMPARE(root->textAlign, TextAlign::Centro);
    QCOMPARE(m[0]->shape, NodeShape::RettangoloArrotondato);
    // foto di riferimento 10: centrale scuro, principali con la linea sotto, linee ad angolo retto, spessore 1.
    MapVariants::apply(*m_model, QStringLiteral("mindmap-5"), false);
    m_scene->autoLayout();
    QCOMPARE(m_model->sheetStyle.variant, QStringLiteral("mindmap-5"));
    QCOMPARE(m_model->sheetStyle.branchWidth, 1.0);
    QCOMPARE(root->fillStyle, NodeFillStyle::Pieno);
    QVERIFY(NodeStyle::relativeLuminance(root->text) > 0.5);  // testo chiaro sul pieno scuro
    QCOMPARE(m[0]->shape, NodeShape::LineaSotto);
    QCOMPARE(m[0]->borderWidth, 4.0);
    QCOMPARE(m[0]->children[0]->shape, NodeShape::LineaSotto);
    const QPainterPath sub = m_scene->edgePath(m[0]->children[0].get());
    // FEAT-072 (measured live 2026-10-23): the only child's underline is at its parent's height, so
    // the line is straight (before: down then across).
    QCOMPARE(sub.elementAt(0).y, sub.elementAt(sub.elementCount() - 1).y);
    // Una scelta fatta a mano vince sulla variante.
    m_model->setStyle({m[1]}, StyleProperty::Shape, int(NodeShape::Esagono));
    QCOMPARE(m[1]->shape, NodeShape::Esagono);
    // Salvata nel foglio.
    MindMapModel reopened;
    QVERIFY(reopened.fromJson(m_model->toJson()));
    QCOMPARE(reopened.sheetStyle.variant, QStringLiteral("mindmap-5"));
    QCOMPARE(reopened.root()->children[0]->shape, NodeShape::LineaSotto);
    // Tipo con la variante: organigramma.
    MapVariants::apply(*m_model, QStringLiteral("org-2"), false);
    QCOMPARE(MapStructure::typeOf(root), MapStructure::Type::OrgChart);
    QCOMPARE(root->shape, NodeShape::Rettangolo);
    QVERIFY(root->border.alpha() > 0);
    // A mano libera: le linee lo sono senza scelta sul nodo.
    MapVariants::apply(*m_model, QStringLiteral("mindmap-7"), false);
    QCOMPARE(root->branchStyle, NodeStyle::kFreehandSolidLine);
    // Miniature: disegnate e della misura della galleria.
    const QPixmap thumbnail = VariantGallery::thumbnail(QStringLiteral("logic-4"), false);
    QCOMPARE(thumbnail.deviceIndependentSize().toSize(), QSize(VariantGallery::kThumbnailWidthPx, VariantGallery::kThumbnailHeightPx));
    newMap({0});
    NodeStyle::SheetStyle plain;
    m_model->setSheetStyle(plain);
}

void BalancedLayoutTest::snapshotForVisualReview() {
    const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR");
    if (dir.isEmpty()) QSKIP("MENTIS_SNAPSHOT_DIR non impostata");
    const auto shoot = [this, &dir](const QList<int> &branches, const QString &name) {
        newMap(branches);
        m_view->centerOn(itemFor(m_model->root()));
        QTest::qWait(400);
        QVERIFY(m_window->grab().save(dir + QLatin1Char('/') + name));
    };
    shoot({0, 0, 0, 0}, QStringLiteral("balanced-four.png"));
    shoot({13, 0, 0, 0}, QStringLiteral("balanced-big-branch.png"));
    shoot({3, 2, 4, 1, 2}, QStringLiteral("balanced-mixed.png"));
    shoot({12, 0, 0, 0}, QStringLiteral("balanced-ref-12.png"));
    shoot({5, 0, 0, 0}, QStringLiteral("balanced-ref-5.png"));
    shoot({13, 0, 10, 0}, QStringLiteral("balanced-ref-13-10.png"));
    shoot({13, 0, 11, 9}, QStringLiteral("balanced-ref-13-11-9.png"));
    // FEAT-015: a meta' trascinamento, segnaposto accanto al sotto-argomento (foto di riferimento 1).
    {
        const QList<MindNode *> m = newMap({1, 0, 0, 0});
        m_view->centerOn(box(m[0]).center());  // lontano dai bordi: niente scorrimento automatico
        QTest::qWait(200);
        const QRectF child = box(m[0]->children[0].get());
        const QPoint start = m_view->mapFromScene(QPointF(box(m[1]).center().x(), box(m[1]).top() + 4));
        const QPoint end = m_view->mapFromScene(child.center() + QPointF(child.width() / 2 + 40, 0));
        QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, start);
        for (int i = 1; i <= 4; ++i) {
            const QPoint at = start + (end - start) * i / 4;
            QMouseEvent move(QEvent::MouseMove, QPointF(at), m_view->viewport()->mapToGlobal(QPointF(at)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(m_view->viewport(), &move);
        }
        QTest::qWait(100);
        QVERIFY(m_window->grab().save(dir + QStringLiteral("/reorder-drag.png")));
        QTest::keyClick(m_view, Qt::Key_Escape);
        QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, end);
    }
    // FEAT-016: controllo vuoto sul nodo scelto, poi ramo chiuso con il numero.
    {
        const QList<MindNode *> m = newMap({3, 0, 0, 0});
        m_view->setZoomPercent(200);
        m_view->centerOn(box(m[0]).center());
        m_scene->selectNode(m[0]);
        QTest::qWait(200);
        QVERIFY(m_window->grab().save(dir + QStringLiteral("/collapse-open.png")));
        m_window->findChild<Document *>()->setCollapsed(m[0], true);
        m_scene->autoLayout();
        m_scene->clearSelection();
        m_view->centerOn(box(m[0]).center());
        QTest::qWait(200);
        QVERIFY(m_window->grab().save(dir + QStringLiteral("/collapse-closed.png")));
        m_view->setZoomPercent(100);
    }
    // FEAT-026 fase C: tipi del foglio e di un ramo, come le foto di riferimento dell'utente.
    const auto structureShot = [&](const QString &type, const QString &direction, bool branchOnly, const QString &name) {
        const QList<MindNode *> m = newMap({3, 3, 2, 3});
        m_model->addChild(m[0]->children[0].get(), QStringLiteral("Nipote"));
        MindNode *target = branchOnly ? m[0] : m_model->root();
        target->structure = type;
        target->structureDirection = direction;
        m_scene->autoLayout();
        m_view->setZoomPercent(60);
        m_view->centerOn(itemFor(target)->sceneBoundingRect().center() + (branchOnly ? QPointF(0, 0) : QPointF(0, 150)));
        QTest::qWait(200);
        QVERIFY(m_window->grab().save(dir + QLatin1Char('/') + name));
        m_view->setZoomPercent(100);
    };
    structureShot(QStringLiteral("logic"), QStringLiteral("right"), false, QStringLiteral("structure-logic.png"));
    structureShot(QStringLiteral("org"), QStringLiteral("down"), false, QStringLiteral("structure-org.png"));
    structureShot(QStringLiteral("tree"), QStringLiteral("right"), false, QStringLiteral("structure-tree.png"));
    structureShot(QStringLiteral("org"), QStringLiteral("down"), true, QStringLiteral("structure-branch-org.png"));
    {   // Organigramma con un ramo a grafico logico (foto 3 dell'utente, 2026-10-21).
        const QList<MindNode *> m = newMap({3, 0, 0, 0});
        m_model->root()->structure = QStringLiteral("org");
        m[0]->structure = QStringLiteral("logic");
        m_scene->autoLayout();
        m_view->setZoomPercent(80);
        m_view->centerOn(box(m_model->root()).center() + QPointF(0, 120));
        QTest::qWait(200);
        QVERIFY(m_window->grab().save(dir + QStringLiteral("/structure-org-logic.png")));
        m_view->setZoomPercent(100);
    }
    structureShot(QStringLiteral("tree"), QStringLiteral("right"), true, QStringLiteral("structure-branch-tree.png"));
    // FEAT-026: sezione Struttura della pagina Stile e menu a griglia aperto (foto dell'utente).
    {
        const QList<MindNode *> m = newMap({2, 1});
        m_scene->selectNode(m[0]);
        if (!m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))->isChecked())
            m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))->click();
        QTest::qWait(400);
        auto *select = m_window->findChild<StylePage *>()->findChild<QComboBox *>(QStringLiteral("StructureTypeSelect"));
        QVERIFY(select);
        select->showPopup();
        QTest::qWait(200);
        QPixmap shot = m_window->grab();
        if (QWidget *popup = QApplication::activePopupWidget()) {
            QPainter painter(&shot);
            painter.drawPixmap(m_window->mapFromGlobal(popup->mapToGlobal(QPoint(0, 0))), popup->grab());
            popup->close();
        }
        QVERIFY(shot.save(dir + QStringLiteral("/structure-panel.png")));
        m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))->click();
        QTest::qWait(200);
    }
    // FEAT-023: rami affusolati (Medio) e linee a mano libera.
    {
        const QList<MindNode *> m = newMap({3, 3, 0, 0});
        NodeStyle::SheetStyle style = m_model->sheetStyle;
        style.branchWidth = 3;
        style.tapered = true;
        m_model->setSheetStyle(style);
        m_scene->autoLayout();
        m_view->centerOn(itemFor(m_model->root()));
        QTest::qWait(200);
        QVERIFY(m_window->grab().save(dir + QStringLiteral("/branch-tapered.png")));
        style.tapered = false;
        style.branchWidth = 0;
        m_model->setSheetStyle(style);
        m_model->setStyle({m_model->root(), m[0]}, StyleProperty::BranchStyle, int(NodeStyle::kFreehandSolidLine));
        m_model->setStyle({m[1]}, StyleProperty::BranchStyle, int(NodeStyle::kFreehandDashLine));
        m_scene->autoLayout();
        QTest::qWait(200);
        QVERIFY(m_window->grab().save(dir + QStringLiteral("/branch-freehand.png")));
        // FEAT-026 fase D: alcune varianti a grandezza vera e la galleria.
        for (const QString &id : {QStringLiteral("mindmap-2"), QStringLiteral("mindmap-3"), QStringLiteral("mindmap-4"),
                                  QStringLiteral("mindmap-5"), QStringLiteral("mindmap-7"), QStringLiteral("logic-4")}) {
            const QList<MindNode *> v = newMap({3, 0, 0, 2, 0});
            MapVariants::apply(*m_model, id, false);
            m_scene->autoLayout();
            m_view->centerOn(itemFor(m_model->root()));
            QTest::qWait(200);
            QVERIFY(m_window->grab().save(dir + QStringLiteral("/variant-%1.png").arg(id)));
        }
        // FEAT-026 phase E: the brace map variants on the reference test map (a branch with four subtopics, one with one).
        for (const QString &id : {QStringLiteral("brace-1"), QStringLiteral("brace-2"), QStringLiteral("brace-3")}) {
            newMap({4, 1, 0, 0});
            MapVariants::apply(*m_model, id, false);
            m_scene->autoLayout();
            m_view->centerOn(itemFor(m_model->root()));
            QTest::qWait(200);
            QVERIFY(m_window->grab().save(dir + QStringLiteral("/variant-%1.png").arg(id)));
        }
        // FEAT-026 phase E: the timeline variants on the reference test map (variant 1 without, the others with
        // two grandchildren, one long, and a subtopic on main topics 3 and 4).
        for (const QString &id : {QStringLiteral("timeline-1"), QStringLiteral("timeline-1"), QStringLiteral("timeline-2"),
                                  QStringLiteral("timeline-3")}) {
            static int shot = 0;
            const QList<MindNode *> m = newMap(shot == 0 ? QList<int>{4, 1, 0, 0} : QList<int>{4, 1, 1, 1});
            if (shot > 0) {
                m_model->addChild(m[0]->children[0].get(), QStringLiteral("Sotto-argomento 1"));
                m_model->addChild(m[0]->children[3].get(), QStringLiteral("Sotto-argomento quattro molto molto lungo per la prova"));
            }
            MapVariants::apply(*m_model, id, false);
            m_scene->autoLayout();
            m_view->centerOn(box(m[1]).center());
            QTest::qWait(200);
            QVERIFY(m_window->grab().save(dir + QStringLiteral("/variant-%1%2.png").arg(id, shot == 0 ? QStringLiteral("-plain") : QString())));
            ++shot;
        }
        // FEAT-026 phase E: the fishbone variants on the reference test map (a grandchild under the first subtopic).
        for (const QString &id : {QStringLiteral("fishbone-1"), QStringLiteral("fishbone-2"), QStringLiteral("fishbone-3")}) {
            const QList<MindNode *> m = newMap({4, 1, 1, 1});
            m_model->addChild(m[0]->children[0].get(), QStringLiteral("Sotto-argomento 1"));
            MapVariants::apply(*m_model, id, false);
            m_scene->autoLayout();
            m_view->centerOn(box(m[0]).center() + QPointF(150, 150));
            QTest::qWait(200);
            QVERIFY(m_window->grab().save(dir + QStringLiteral("/variant-%1.png").arg(id)));
        }
        m_model->setSheetStyle(NodeStyle::SheetStyle());
        newMap({2, 0});
        {
            VariantGallery gallery(m_window.get(), QString(), QStringLiteral("Create"));
            gallery.show();
            QTest::qWait(300);
            QVERIFY(gallery.grab().save(dir + QStringLiteral("/variant-gallery.png")));
        }
    }
    // FEAT-013: "Bilancia mappa" in cima alla pagina Mappa.
    m_scene->clearSelection();
    m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))->click();
    QTest::qWait(400);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/balance-switch.png")));
}

QTEST_MAIN(BalancedLayoutTest)
#include "balanced_layout_test.moc"
