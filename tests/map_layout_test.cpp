// Disposizione della mappa con nodi spostati a mano (B7). BUG-013: trascinando un nodo i suoi
// discendenti si spostano con lui, anche durante il trascinamento, e i figli aggiunti dopo nascono
// accanto al nodo spostato. Vero MainWindow, piattaforma offscreen e impostazioni separate.
#include "document.h"
#include "mainwindow.h"
#include "mapvariants.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "performance.h"

#include <QAction>
#include <QApplication>
#include <QPaintEvent>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTest>

#include <functional>
#include <memory>


class MapLayoutTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void branchMovesWithItsDescendants();
    void newChildAppearsNextToMovedParent();
    void undoRestoresTheWholeBranch();
    void structureChangeRepaintsOldShapes();
    void twoChildrenMeetHalfwayOnTheParentsLine();

private:
    struct Branch {
        MindNode *parent = nullptr;
        MindNode *first = nullptr;
        MindNode *second = nullptr;
        MindNode *grandchild = nullptr;
        QList<MindNode *> all() const { return {parent, first, second, grandchild}; }
    };
    // Ramo principale con due figli e un nipote, aggiunto alla mappa e disposto.
    Branch addBranch();
    NodeItem *itemFor(const MindNode *node) const;
    // Trascina il nodo di delta (coordinate della scena) con il mouse, in piu' passi; se
    // midDragCheck e' dato, lo chiama a meta' del trascinamento, con il tasto ancora premuto.
    void dragNode(MindNode *node, QPointF delta, const std::function<void()> &midDragCheck = {});
    QAction *action(const QString &shortcut) const;

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    MindMapView *m_view = nullptr;
};

void MapLayoutTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisMapLayoutTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);
    m_window = std::make_unique<MainWindow>();
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    auto *pages = qobject_cast<QStackedWidget *>(m_window->centralWidget());
    QVERIFY(pages);
    pages->setCurrentIndex(1);  // editor
    m_model = m_window->findChild<MindMapModel *>();
    m_view = m_window->findChild<MindMapView *>();
    QVERIFY(m_model && m_view);
    m_scene = qobject_cast<MindMapScene *>(m_view->scene());
    QVERIFY(m_scene);
}

void MapLayoutTest::cleanupTestCase() { m_window.reset(); }

void MapLayoutTest::init() {
    // Queste prove riguardano lo spostamento a mano: con il bilanciamento acceso il trascinamento
    // cambia ordine o genitore (FEAT-015).
    m_window->setBalanced(false);
    m_scene->clearSelection();
    m_view->setZoomPercent(100);
    QApplication::processEvents();
    m_window->findChild<Document *>()->flushPendingStep();  // closes the history step at once
}

MapLayoutTest::Branch MapLayoutTest::addBranch() {
    Branch branch;
    branch.parent = m_model->addChild(m_model->root(), QStringLiteral("Ramo"));
    branch.first = m_model->addChild(branch.parent, QStringLiteral("Figlio 1"));
    branch.second = m_model->addChild(branch.parent, QStringLiteral("Figlio 2"));
    branch.grandchild = m_model->addChild(branch.first, QStringLiteral("Nipote"));
    m_scene->autoLayout();
    QApplication::processEvents();
    m_window->findChild<Document *>()->flushPendingStep();  // closes the history step at once
    return branch;
}

NodeItem *MapLayoutTest::itemFor(const MindNode *node) const {
    for (auto *item : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node) return nodeItem;
    return nullptr;
}

void MapLayoutTest::dragNode(MindNode *node, QPointF delta, const std::function<void()> &midDragCheck) {
    NodeItem *item = itemFor(node);
    m_view->ensureVisible(item, 0, 0);
    // Si preme nel riquadro ma fuori dalla zona dei bordi (ridimensionamento) e lontano dal testo.
    const QPoint start = m_view->mapFromScene(item->mapToScene(QPointF(0, item->boundingRect().top() + 6)));
    const QPoint end = start + QPoint(qRound(delta.x() * m_view->transform().m11()), qRound(delta.y() * m_view->transform().m22()));
    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, start);
    for (int step = 1; step <= 4; ++step) {
        QTest::mouseMove(m_view->viewport(), start + (end - start) * step / 4);
        if (step == 2 && midDragCheck) midDragCheck();
    }
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, end);
    QApplication::processEvents();
    m_window->findChild<Document *>()->flushPendingStep();  // closes the history step at once
}

QAction *MapLayoutTest::action(const QString &shortcut) const {
    for (auto *candidate : m_window->actions())
        if (candidate->shortcuts().contains(QKeySequence(shortcut))) return candidate;
    for (auto *candidate : m_window->findChildren<QAction *>())
        if (candidate->shortcuts().contains(QKeySequence(shortcut))) return candidate;
    return nullptr;
}

// User's photos 2026-10-26: with two children the parent's centre sat halfway between the children's
// centres, so where the parent's line point (its underline) is lower than the children's, the
// junction was off the middle of the two lines. Now the line points are halfway.
void MapLayoutTest::twoChildrenMeetHalfwayOnTheParentsLine() {
    MindNode *parent = m_model->addChild(m_model->root(), QStringLiteral("Due figli"));
    MindNode *first = m_model->addChild(parent, QStringLiteral("Sotto-argomento 1"));
    MindNode *second = m_model->addChild(parent, QStringLiteral("Sotto-argomento 2"));
    // As in the photos: underlined topics, the parent's text bigger, so its underline is further below
    // its centre than the children's below theirs.
    QVERIFY(m_model->setStyle({parent, first, second}, StyleProperty::Shape, int(NodeShape::LineaSotto)));
    QVERIFY(m_model->setStyle({parent}, StyleProperty::FontSize, 28));
    m_scene->autoLayout();
    QApplication::processEvents();
    const bool right = m_scene->sideOf(parent) >= 0;
    const auto lineY = [this](const MindNode *node, bool towardsRight) {
        const NodeItem *item = itemFor(node);
        return item->pos().y() + item->branchAnchor(towardsRight).y();
    };
    QVERIFY(itemFor(parent)->branchAnchor(right).y() > itemFor(first)->branchAnchor(!right).y() + 2);
    const qreal parentY = lineY(parent, right);
    const qreal middle = (lineY(first, !right) + lineY(second, !right)) / 2;
    QVERIFY2(std::abs(parentY - middle) < 0.6, qPrintable(QStringLiteral("%1 vs %2").arg(parentY).arg(middle)));
    // The two children's points are not the same height: the check is not trivially true.
    QVERIFY(std::abs(lineY(first, !right) - lineY(second, !right)) > 10);
}

void MapLayoutTest::branchMovesWithItsDescendants() {
    const Branch branch = addBranch();
    QHash<MindNode *, QPointF> before;
    for (MindNode *node : branch.all()) before.insert(node, node->position);
    // A meta' del trascinamento i figli hanno gia' seguito il genitore (foto di riferimento 02).
    bool followedDuringDrag = false;
    dragNode(branch.parent, QPointF(40, -160), [&] {
        const QPointF parentDelta = itemFor(branch.parent)->pos() - before.value(branch.parent);
        followedDuringDrag = parentDelta.manhattanLength() > 10
                             && itemFor(branch.grandchild)->pos() - before.value(branch.grandchild) == parentDelta;
    });
    QVERIFY(followedDuringDrag);
    const QPointF delta = branch.parent->position - before.value(branch.parent);
    QVERIFY(delta.manhattanLength() > 100);
    for (MindNode *node : branch.all()) QCOMPARE(node->position - before.value(node), delta);
    // Lo spostamento si salva solo sul nodo trascinato; i discendenti lo ereditano.
    QCOMPARE(branch.parent->manualOffset, delta);
    QCOMPARE(branch.first->manualOffset, QPointF());
    // Una ridisposizione (per esempio dopo un cambio di titolo altrove) non li separa.
    m_scene->autoLayout();
    for (MindNode *node : branch.all()) QCOMPARE(node->position - before.value(node), delta);
}

void MapLayoutTest::newChildAppearsNextToMovedParent() {
    const Branch branch = addBranch();
    dragNode(branch.parent, QPointF(0, -200));
    MindNode *third = m_model->addChild(branch.parent, QStringLiteral("Figlio 3"));
    m_scene->autoLayout();
    QApplication::processEvents();
    // Come i fratelli: stessa colonna, sotto l'ultimo, e il gruppo dei figli resta centrato sul
    // genitore (foto di riferimento 03).
    // Same column = same edge towards the parent: on the left side the right edges (macOS gives
    // text fractional widths, so "Figlio 3" and "Figlio 2" differ there by a fraction of a pixel).
    const bool leftSide = third->position.x() < branch.parent->position.x();
    const QRectF thirdBox = itemFor(third)->sceneBoundingRect();
    const QRectF secondBox = itemFor(branch.second)->sceneBoundingRect();
    QVERIFY(qAbs(leftSide ? thirdBox.right() - secondBox.right() : thirdBox.left() - secondBox.left()) < 0.01);
    QVERIFY(third->position.y() > branch.second->position.y());
    QVERIFY(third->position.y() - branch.second->position.y() < 120);
    const qreal groupCentre = (branch.grandchild->position.y() + third->position.y()) / 2;
    QVERIFY(qAbs(groupCentre - branch.parent->position.y()) < 1);
}

void MapLayoutTest::undoRestoresTheWholeBranch() {
    const Branch branch = addBranch();
    QHash<MindNode *, QPointF> before;
    for (MindNode *node : branch.all()) before.insert(node, node->position);
    dragNode(branch.parent, QPointF(0, -180));
    QAction *undo = action(QStringLiteral("Ctrl+Z"));
    QVERIFY(undo);
    undo->trigger();
    QApplication::processEvents();
    // L'annulla puo' ricostruire i nodi: si cercano di nuovo per titolo nel ramo ripristinato.
    const MindNode *parent = m_model->root()->children.back().get();
    QCOMPARE(parent->title, QStringLiteral("Ramo"));
    QCOMPARE(parent->position, before.value(branch.parent));
    QCOMPARE(parent->children.at(0)->position, before.value(branch.first));
    QCOMPARE(parent->children.at(0)->children.at(0)->position, before.value(branch.grandchild));
    QCOMPARE(parent->manualOffset, QPointF());
}

// Switching the map type repaints everything the old map covered (bug 2026-10-24: a piece of the old oval
// central topic stayed on screen until the next click).
void MapLayoutTest::structureChangeRepaintsOldShapes() {
    addBranch();
    struct PaintRecorder final : QObject {
        QRegion painted;
        bool eventFilter(QObject *, QEvent *event) override {
            if (event->type() == QEvent::Paint) painted |= static_cast<QPaintEvent *>(event)->region();
            return false;
        }
    };
    // As MainWindow::chooseSheetVariant; the oval central topic first, then brace lines, timeline, fishbone.
    const QStringList variants{"mindmap-11", "logic-4", "brace-1", "mindmap-1", "timeline-3", "fishbone-2", "tree-4", "logic-4"};
    for (const QString &variant : variants) {
        QRegion before;
        for (const QGraphicsItem *item : m_scene->items())
            if (item->isVisible()) before |= m_view->mapFromScene(item->sceneBoundingRect()).boundingRect();
        before &= m_view->viewport()->rect();
        PaintRecorder recorder;
        m_view->viewport()->installEventFilter(&recorder);
        MapVariants::apply(*m_model, variant, false);
        m_scene->autoLayout();
        QApplication::processEvents();
        m_view->viewport()->removeEventFilter(&recorder);
        QVERIFY2(before.subtracted(recorder.painted).isEmpty(), qPrintable(variant));
    }
}

QTEST_MAIN(MapLayoutTest)
#include "map_layout_test.moc"
