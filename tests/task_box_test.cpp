// FEAT-048 (docs/features/FEAT-048-attivita-con-casella.md): casella di spunta nel posto a sinistra
// del nodo (docs/architecture/node-content-contract.md), con il vero MainWindow: menu + Cose da fare,
// clic sulla mappa e nell'outliner, Rimuovi casella, file e Markdown.
#include "document.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "nodestyle.h"
#include "performance.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTest>
#include <QTimer>
#include <QTreeView>

#include <memory>

class TaskBoxTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void menuAddsTheBoxOnTheLeftInOneStep();
    void menuTogglesTheBox();
    void clickTogglesWithoutEditingOrMoving();
    void boxStaysLeftAndLinksRight();
    void removeFromTheBoxMenu();
    void outlinerShowsAndTogglesTheBox();
    void fileAndMarkdownKeepTheBox();
    void snapshotForVisualReview();

private:
    NodeItem *itemFor(const MindNode *node) const;
    QPoint viewPoint(const NodeItem *item, const QPointF &local) const;
    QAction *actionWithText(const QString &text) const;
    void flushHistory();
    void undo();

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    MindMapView *m_view = nullptr;
};

void TaskBoxTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisTaskBoxTest"));
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
    QVERIFY(m_model && m_view && m_scene);
}

void TaskBoxTest::cleanupTestCase() { m_window.reset(); }

void TaskBoxTest::init() {
    m_scene->clearSelection();
    m_view->setZoomPercent(100);
    flushHistory();
}

void TaskBoxTest::flushHistory() {
    QApplication::processEvents();
    // The open history step is closed at once instead of waiting for its 150 ms timer.
    m_window->findChild<Document *>()->flushPendingStep();
    QApplication::processEvents();
}

void TaskBoxTest::undo() {
    for (auto *action : m_window->actions())
        if (action->shortcuts().contains(QKeySequence(QKeySequence::Undo))) { action->trigger(); break; }
    QApplication::processEvents();
}

NodeItem *TaskBoxTest::itemFor(const MindNode *node) const {
    for (auto *item : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node) return nodeItem;
    return nullptr;
}

QPoint TaskBoxTest::viewPoint(const NodeItem *item, const QPointF &local) const {
    return m_view->mapFromScene(item->mapToScene(local));
}

QAction *TaskBoxTest::actionWithText(const QString &text) const {
    for (auto *menu : m_window->findChildren<QMenu *>())
        for (auto *action : menu->actions())
            if (action->text().section(QLatin1Char('\t'), 0, 0) == text) return action;  // FEAT-041 stage B: the key after a tab
    return nullptr;
}

void TaskBoxTest::menuAddsTheBoxOnTheLeftInOneStep() {
    MindNode *node = m_model->addChild(m_model->root(), QStringLiteral("Comprare il pane"));
    m_scene->autoLayout();
    m_scene->selectNode(node);
    flushHistory();
    NodeItem *item = itemFor(node);
    const qreal widthBefore = item->textBoxWidth();
    QAction *checkbox = actionWithText(QStringLiteral("Checkbox"));
    QVERIFY(checkbox);
    checkbox->trigger();
    QApplication::processEvents();
    QCOMPARE(node->task, MindNode::Task::Open);
    QCOMPARE(item->textBoxWidth(), widthBefore + NodeItem::kLinkTextGapPx + NodeItem::kLinkButtonPx);  // 30 px in piu'
    const QRectF box = item->taskRect();
    QVERIFY(!box.isEmpty());
    QCOMPARE(box.left(), -item->textBoxWidth() / 2 + NodeStyle::kMainTextPadding.x());  // al padding dal bordo sinistro del contenuto
    flushHistory();
    undo();
    QCOMPARE(node->task, MindNode::Task::None);
    QVERIFY(item->taskRect().isEmpty());
}

void TaskBoxTest::menuTogglesTheBox() {
    MindNode *first = m_model->addChild(m_model->root(), QStringLiteral("Uno"));
    MindNode *second = m_model->addChild(m_model->root(), QStringLiteral("Due"));
    m_scene->autoLayout();
    m_scene->selectNode(first);
    flushHistory();
    QAction *checkbox = actionWithText(QStringLiteral("Checkbox"));
    QVERIFY(checkbox);
    checkbox->trigger();
    QApplication::processEvents();
    QCOMPARE(first->task, MindNode::Task::Open);
    flushHistory();
    checkbox->trigger();  // already there: removed, one step
    QApplication::processEvents();
    QCOMPARE(first->task, MindNode::Task::None);
    flushHistory();
    undo();
    QCOMPARE(first->task, MindNode::Task::Open);
    // Mixed selection: the topics without a box get it, the others keep theirs.
    m_scene->selectNode(first);
    itemFor(second)->setSelected(true);
    QCOMPARE(m_scene->selectedNodes().size(), 2);
    checkbox->trigger();
    QApplication::processEvents();
    QCOMPARE(first->task, MindNode::Task::Open);
    QCOMPARE(second->task, MindNode::Task::Open);
    checkbox->trigger();  // every topic has one: removed from all
    QApplication::processEvents();
    QCOMPARE(first->task, MindNode::Task::None);
    QCOMPARE(second->task, MindNode::Task::None);
}

void TaskBoxTest::clickTogglesWithoutEditingOrMoving() {
    MindNode *node = m_model->addChild(m_model->root(), QStringLiteral("Studiare"));
    node->task = MindNode::Task::Open;
    m_scene->updateNode(node);
    m_scene->autoLayout();
    flushHistory();
    NodeItem *item = itemFor(node);
    const QPointF position = node->position;
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, viewPoint(item, item->taskRect().center()));
    QApplication::processEvents();
    QCOMPARE(node->task, MindNode::Task::Done);
    QCOMPARE(node->title, QStringLiteral("Studiare"));  // il testo non cambia
    QVERIFY(!item->isEditing());
    QCOMPARE(node->position, position);
    QTest::qWait(QApplication::doubleClickInterval() + 50);
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, viewPoint(item, item->taskRect().center()));
    QApplication::processEvents();
    QCOMPARE(node->task, MindNode::Task::Open);
    flushHistory();
    undo();
    QCOMPARE(node->task, MindNode::Task::Done);
}

void TaskBoxTest::boxStaysLeftAndLinksRight() {
    MindNode *node = m_model->addChild(m_model->root(), QStringLiteral("Con collegamento"));
    node->task = MindNode::Task::Open;
    node->webLink = QStringLiteral("https://example.com");
    m_scene->updateNode(node);
    m_scene->autoLayout();
    QApplication::processEvents();
    NodeItem *item = itemFor(node);
    const QRectF box = item->taskRect();
    const QRectF link = item->linkIconRect(NodeItem::LinkKind::Web);
    QVERIFY(!box.isEmpty() && !link.isEmpty());
    QVERIFY(box.right() < link.left());
    QVERIFY(box.center().x() < 0 && link.center().x() > 0);
    QCOMPARE(box.center().y(), link.center().y());  // stessa riga, centrati sul contenuto
}

void TaskBoxTest::removeFromTheBoxMenu() {
    MindNode *node = m_model->addChild(m_model->root(), QStringLiteral("Da togliere"));
    node->task = MindNode::Task::Done;
    m_scene->updateNode(node);
    m_scene->autoLayout();
    flushHistory();
    QTimer::singleShot(100, this, [] {
        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()); menu && !menu->actions().isEmpty()) {
            menu->setActiveAction(menu->actions().front());
            QTest::keyClick(menu, Qt::Key_Return);
        }
    });
    emit m_scene->taskMenuRequested(node, QCursor::pos());
    QApplication::processEvents();
    QCOMPARE(node->task, MindNode::Task::None);
}

void TaskBoxTest::outlinerShowsAndTogglesTheBox() {
    MindNode *node = m_model->addChild(m_model->root(), QStringLiteral("Voce con casella"));
    node->task = MindNode::Task::Open;
    m_scene->updateNode(node);
    flushHistory();
    QPushButton *outliner = nullptr;
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Outliner")) outliner = button;
    QVERIFY(outliner);
    outliner->click();
    QApplication::processEvents();
    QTreeView *tree = nullptr;
    for (auto *candidate : m_window->findChildren<QTreeView *>())
        if (candidate->isVisible() && candidate->model()) tree = candidate;
    QVERIFY(tree);
    const QModelIndex index = m_model->indexFromNode(node);
    tree->scrollTo(index);
    QApplication::processEvents();
    const QRect row = tree->visualRect(index);
    // Maniglia (22) e marcatore (24), poi la casella larga 24: centro a 46 + 12 px.
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, QPoint(row.left() + 58, row.center().y()));
    QApplication::processEvents();
    QCOMPARE(node->task, MindNode::Task::Done);
    QVERIFY(!tree->indexWidget(index));  // nessuna modifica del titolo aperta
    outliner->click();  // di nuovo sulla mappa
    QApplication::processEvents();
}

void TaskBoxTest::fileAndMarkdownKeepTheBox() {
    MindMapModel model;
    MindNode *open = model.addChild(model.root(), QStringLiteral("Aperta"));
    MindNode *done = model.addChild(model.root(), QStringLiteral("Fatta"));
    model.addChild(model.root(), QStringLiteral("Senza"));
    open->task = MindNode::Task::Open;
    done->task = MindNode::Task::Done;
    MindMapModel reopened;
    QVERIFY(reopened.fromJson(model.toJson()));
    QCOMPARE(reopened.root()->children[0]->task, MindNode::Task::Open);
    QCOMPARE(reopened.root()->children[1]->task, MindNode::Task::Done);
    QCOMPARE(reopened.root()->children[2]->task, MindNode::Task::None);
    const QString markdown = model.toMarkdown(false);
    QVERIFY(markdown.contains(QStringLiteral("- [ ] Aperta")));
    QVERIFY(markdown.contains(QStringLiteral("- [x] Fatta")));
    QVERIFY(markdown.contains(QStringLiteral("- Senza")));
    MindMapModel imported;
    QVERIFY(imported.fromMarkdown(QStringLiteral("- Lista\n  - [ ] Pane\n  - [X] Latte\n  - Uova\n")));
    const auto &items = imported.root()->children;
    QCOMPARE(items.size(), size_t(3));
    QCOMPARE(items[0]->title, QStringLiteral("Pane"));
    QCOMPARE(items[0]->task, MindNode::Task::Open);
    QCOMPARE(items[1]->task, MindNode::Task::Done);
    QCOMPARE(items[2]->task, MindNode::Task::None);
}

// Foto per il confronto a vista (solo con MENTIS_SNAPSHOT_DIR, piattaforma con i caratteri).
void TaskBoxTest::snapshotForVisualReview() {
    const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR");
    if (dir.isEmpty()) QSKIP("MENTIS_SNAPSHOT_DIR non impostata");
    m_model->clear(QStringLiteral("Piano di studio"));
    MindNode *main = m_model->addChild(m_model->root(), QStringLiteral("Cose da fare"));
    MindNode *a = m_model->addChild(main, QStringLiteral("Ripassare capitolo 3"));
    MindNode *b = m_model->addChild(main, QStringLiteral("Esercizi"));
    MindNode *c = m_model->addChild(main, QStringLiteral("Leggere l'articolo"));
    a->task = MindNode::Task::Done;
    b->task = MindNode::Task::Open;
    c->task = MindNode::Task::Open;
    c->webLink = QStringLiteral("https://example.com");
    main->task = MindNode::Task::Open;
    for (MindNode *node : {main, a, b, c}) m_scene->updateNode(node);
    m_scene->autoLayout();
    m_view->centerOn(itemFor(main));
    QTest::qWait(300);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/tasks-map.png")));
    QPushButton *outliner = nullptr;
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Outliner")) outliner = button;
    outliner->click();
    QTest::qWait(300);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/tasks-outliner.png")));
    outliner->click();
}

QTEST_MAIN(TaskBoxTest)
#include "task_box_test.moc"
