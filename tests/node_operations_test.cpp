// FEAT-066 tappa 2: regressione delle operazioni sui nodi vista dalla finestra, scritta PRIMA di
// spostarle nel Document. Ogni operazione e' un solo passo di storico (disposizione compresa).
// Vero MainWindow, piattaforma offscreen, impostazioni e cartelle di prova separate.
#include "branchpath.h"
#include "document.h"
#include "floatingui.h"
#include "mainwindow.h"
#include "mapstructure.h"
#include "mapvariants.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "modaldialog.h"
#include "nodeitem.h"
#include "nodeshape.h"
#include "performance.h"
#include "sidepanel.h"
#include "stylecontrols.h"
#include "topictools.h"
#include "tour.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTest>
#include <QGraphicsProxyWidget>
#include <QTextCursor>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QLabel>
#include <QStatusBar>
#include <QLineEdit>
#include <QMenu>
#include <QTimer>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QTreeView>

#include <functional>
#include <memory>

namespace {

QStringList titles(const MindNode *node) {
    QStringList result;
    for (const auto &child : node->children) result.append(child->title);
    return result;
}
}  // namespace

class NodeOperationsTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void addChildAndSiblingAreOneStepEach();
    void removingANestedSelectionIsOneStep();
    void outlinerIndentOutdentDuplicate();
    void outlinerCopyPasteBranch();
    void topicMenuOnTheMap();  // FEAT-042
    void topicMenuStyleCollapseAndText();  // FEAT-042
    void moveBranchToANewSheet();  // FEAT-029
    void mainMenuOrder();  // FEAT-072 F1
    void topicToolsFollowTheSelection();  // FEAT-072 F3
    void homeLayout();  // FEAT-072 F5
    void logicChartLayout();  // FEAT-072
    void braceMapLayout();  // FEAT-026 phase E
    void timelineLayout();  // FEAT-026 phase E
    void fishboneLayout();  // FEAT-026 phase E
    void treeTableLayout();  // FEAT-028 stage 1
    void treeTableKeepsItsCellsAcrossTypes();  // FEAT-028 stage 2, user's report 2026-10-25
    void orgAndTreeChartsLayout();  // FEAT-072
    void branchesStayOutsideAdvancedShapes();  // FEAT-030
    void rootBranchesStartOnTheOutline();  // FEAT-030
    void mindMapVariantsLayout();  // FEAT-072
    void formulaIsAContentOfTheNode();
    void relationButtonWithoutSelectionAddsAFloatingTopic();
    // Tappa 3: relazioni, sommari e confini.
    void relationIsCreatedByClickingTheTarget();
    void relationHandleLabelAndDelete();
    void relationSnapshot();
    void escapeCancelsAPendingRelation();
    void summaryLayout();
    void summaryKeepsEverythingAcrossSaveAndUndo();
    void summaryTopicBranch();
    void summarySnapshot();
    void summaryInOtherStructures();
    void tallSummaryMovesTheSiblingsApart();
    void boundaryRoomInBlockStructures();
    void textFormatBarSitsAboveTheTopic();
    void boundaryWrapsTheSelectedNodes();
    void boundaryTitleStyleRoomAndRelation();
    void boundarySnapshot();
    void boundaryTitleConfirmedByEnterOrClick();
    // BUG-016: voci create nell'outliner disposte sulla mappa, non una sopra l'altra.
    void outlinerItemsAreLaidOutOnTheMap();
    void copyAndPasteStyleOnTheMap();  // FEAT-085

private:
    QAction *action(const QString &text) const;
    NodeItem *itemFor(const MindNode *node) const;
    // The open history step is closed at once (Document::flushPendingStep) instead of waiting for its
    // 150 ms timer: the same step, without 250 ms of sleep per call.
    void wait() {
        QApplication::processEvents();
        m_window->findChild<Document *>()->flushPendingStep();
        QApplication::processEvents();
    }
    void showOutliner(bool on);
    QToolButton *toolButton(const QString &tooltipStart) const;
    void clickNode(MindNode *node);
    MindNode *child(int row) const { return m_model->root()->children.at(row).get(); }
    // Come un clic su una voce: la selezione diventa solo quella voce (setCurrentIndex la estende).
    void selectRow(MindNode *node) {
        m_tree->selectionModel()->setCurrentIndex(m_model->indexFromNode(node),
                                                  QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }

    std::unique_ptr<MainWindow> m_window;
    MindMapView *m_view = nullptr;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    QTreeView *m_tree = nullptr;
};

void NodeOperationsTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisNodeOperationsTest"));
    // Clean settings (CI) would bring the tour invitation and the views' tips, which take the keys.
    TourSettings::setTourOffered();
    TourSettings::setTipsEnabled(false);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);
    m_window = std::make_unique<MainWindow>();
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    auto *pages = qobject_cast<QStackedWidget *>(m_window->centralWidget());
    QVERIFY(pages);
    pages->setCurrentIndex(1);
    m_model = m_window->findChild<MindMapModel *>();
    auto *view = m_window->findChild<MindMapView *>();
    QVERIFY(m_model && view);
    m_view = view;
    m_scene = qobject_cast<MindMapScene *>(view->scene());
    QVERIFY(m_scene);
    QVERIFY(action(QStringLiteral("Add child")) && action(QStringLiteral("Add sibling")) && action(QStringLiteral("Remove")));
    QVERIFY(action(QStringLiteral("Undo")));
}

void NodeOperationsTest::cleanupTestCase() { m_window.reset(); }

void NodeOperationsTest::init() {
    showOutliner(false);
    while (!m_model->root()->children.empty()) m_model->removeNode(m_model->root()->children.front().get());
    m_model->relations.clear();
    m_model->summaries.clear();
    m_model->boundaries.clear();
    m_scene->refreshAnnotations();
    wait();
}

QAction *NodeOperationsTest::action(const QString &text) const {
    for (auto *candidate : m_window->findChildren<QAction *>())
        if (candidate->text() == text) return candidate;
    return nullptr;
}

NodeItem *NodeOperationsTest::itemFor(const MindNode *node) const {
    for (auto *item : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node) return nodeItem;
    return nullptr;
}

void NodeOperationsTest::showOutliner(bool on) {
    for (auto *button : m_window->findChildren<QPushButton *>()) {
        if (button->text() == (on ? QStringLiteral("Outliner") : QStringLiteral("Mind map"))) { button->click(); break; }
    }
    QApplication::processEvents();
    m_tree = nullptr;
    if (!on) return;
    for (auto *tree : m_window->findChildren<QTreeView *>())
        if (tree->isVisible() && tree->model() == m_model) m_tree = tree;
}

void NodeOperationsTest::addChildAndSiblingAreOneStepEach() {
    m_scene->selectNode(m_model->root());
    action(QStringLiteral("Add child"))->trigger();
    wait();
    QCOMPARE(m_model->root()->children.size(), size_t(1));
    MindNode *first = child(0);
    QCOMPARE(m_scene->selectedNode(), first);
    action(QStringLiteral("Add sibling"))->trigger();
    wait();
    QCOMPARE(m_model->root()->children.size(), size_t(2));
    QCOMPARE(m_scene->selectedNode(), child(1));
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QCOMPARE(m_model->root()->children.size(), size_t(1));  // solo il fratello, disposizione compresa
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QVERIFY(m_model->root()->children.empty());
}

void NodeOperationsTest::removingANestedSelectionIsOneStep() {
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("A"));
    MindNode *a1 = m_model->addChild(a, QStringLiteral("A1"));
    MindNode *b = m_model->addChild(m_model->root(), QStringLiteral("B"));
    m_scene->autoLayout();
    wait();
    m_scene->clearSelection();
    for (MindNode *node : {a, a1, b}) itemFor(node)->setSelected(true);  // A1 sta dentro A
    QCOMPARE(m_scene->selectedNodes().size(), 3);
    action(QStringLiteral("Remove"))->trigger();
    wait();
    QVERIFY(m_model->root()->children.empty());
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QCOMPARE(titles(m_model->root()), (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
    QCOMPARE(titles(child(0)), QStringList{QStringLiteral("A1")});
}

void NodeOperationsTest::outlinerIndentOutdentDuplicate() {
    m_model->addChild(m_model->root(), QStringLiteral("A"));
    m_model->addChild(m_model->root(), QStringLiteral("B"));
    wait();
    showOutliner(true);
    QVERIFY(m_tree);
    m_tree->setFocus();
    selectRow(child(1));
    QTest::keyClick(m_tree, Qt::Key_Tab);  // Rientro: B sotto A
    wait();
    QCOMPARE(titles(m_model->root()), QStringList{QStringLiteral("A")});
    QCOMPARE(titles(child(0)), QStringList{QStringLiteral("B")});
    QTest::keyClick(m_tree, Qt::Key_Tab, Qt::ShiftModifier);  // Riduci rientro
    wait();
    QCOMPARE(titles(m_model->root()), (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
    selectRow(child(0));
    QTest::keyClick(m_tree, Qt::Key_D, Qt::ControlModifier);  // Duplica
    wait();
    QCOMPARE(titles(m_model->root()), (QStringList{QStringLiteral("A"), QStringLiteral("A"), QStringLiteral("B")}));
    QVERIFY(child(0)->id != child(1)->id);
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QCOMPARE(titles(m_model->root()), (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
}

void NodeOperationsTest::outlinerCopyPasteBranch() {
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("A"));
    m_model->addChild(a, QStringLiteral("A1"));
    m_model->addChild(m_model->root(), QStringLiteral("B"));
    wait();
    showOutliner(true);
    QVERIFY(m_tree);
    m_tree->setFocus();
    selectRow(child(0));
    QTest::keyClick(m_tree, Qt::Key_C, Qt::ControlModifier);
    selectRow(child(1));
    QTest::keyClick(m_tree, Qt::Key_V, Qt::ControlModifier);
    wait();
    // FEAT-042 (2026-11-05): under the selected item, as its last child (before, after it).
    QCOMPARE(titles(m_model->root()), (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
    MindNode *pasted = child(1)->children.front().get();
    QCOMPARE(pasted->title, QStringLiteral("A"));
    QCOMPARE(titles(pasted), QStringList{QStringLiteral("A1")});
    QVERIFY(pasted->id != child(0)->id);
    QCOMPARE(m_tree->currentIndex(), m_model->indexFromNode(pasted));
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QVERIFY(child(1)->children.empty());
    // Several items: copies of all of them, with their children, under the item, in one step.
    selectRow(child(0));
    m_tree->selectionModel()->select(m_model->indexFromNode(child(1)), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QTest::keyClick(m_tree, Qt::Key_C, Qt::ControlModifier);
    MindNode *a1 = child(0)->children.front().get();
    selectRow(a1);
    QTest::keyClick(m_tree, Qt::Key_V, Qt::ControlModifier);
    wait();
    QCOMPARE(titles(a1), (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
    QCOMPARE(titles(a1->children.front().get()), QStringList{QStringLiteral("A1")});
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QVERIFY(child(0)->children.front()->children.empty());
}

// FEAT-042: right-click on a topic selects it and opens its menu; Copy and Paste put the branch
// under the topic; on the central topic Delete is disabled.
void NodeOperationsTest::topicMenuOnTheMap() {
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("A"));
    m_model->addChild(a, QStringLiteral("A1"));
    MindNode *b = m_model->addChild(m_model->root(), QStringLiteral("B"));
    m_scene->autoLayout();
    wait();
    QStringList shown;
    bool deleteEnabled = true;
    const auto choose = [&](const QString &text) {
        QTimer::singleShot(100, this, [&shown, &deleteEnabled, text] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) return;
            shown.clear();
            for (QAction *item : menu->actions()) {
                if (!item->isSeparator()) shown.append(item->text());
                if (item->text() == QStringLiteral("Delete")) deleteEnabled = item->isEnabled();
            }
            for (QAction *item : menu->actions())
                if (!text.isEmpty() && item->text() == text) { menu->setActiveAction(item); QTest::keyClick(menu, Qt::Key_Return); return; }
            menu->close();
        });
    };
    // A real right-click on A goes through the view and the topic.
    choose(QStringLiteral("Copy"));
    const QPoint pos = m_view->mapFromScene(itemFor(a)->sceneBoundingRect().center());
    QContextMenuEvent event(QContextMenuEvent::Mouse, pos, m_view->viewport()->mapToGlobal(pos));
    QApplication::sendEvent(m_view->viewport(), &event);
    wait();
    QCOMPARE(m_scene->selectedNodes(), QList<MindNode *>{a});
    QVERIFY(shown.contains(QStringLiteral("Insert")) && shown.contains(QStringLiteral("Duplicate")));
    choose(QStringLiteral("Paste"));
    emit m_scene->nodeMenuRequested(b, QCursor::pos());
    wait();
    QCOMPARE(titles(b), QStringList{QStringLiteral("A")});
    QCOMPARE(titles(b->children.front().get()), QStringList{QStringLiteral("A1")});
    QCOMPARE(m_scene->selectedNodes(), QList<MindNode *>{b->children.front().get()});
    choose(QString());
    emit m_scene->nodeMenuRequested(m_model->root(), QCursor::pos());
    wait();
    QVERIFY(!deleteEnabled);
}

namespace {
// Next popup menu: triggers the item at path ("Move to sheet/New sheet") and closes the menu.
void chooseInNextMenu(QObject *context, const QStringList &path) {
    QTimer::singleShot(100, context, [path] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) return;
        QMenu *at = menu;
        for (const QString &text : path) {
            QAction *found = nullptr;
            for (QAction *item : at->actions()) if (!item->isSeparator() && item->text() == text) found = item;
            if (!found) break;
            if (found->menu()) { at = found->menu(); continue; }
            menu->close();
            found->trigger();
            return;
        }
        menu->close();
    });
}
}  // namespace

// FEAT-042: Reset style, Collapse all sub-branches / Expand all, and the menu of the text being
// edited on the map (Cut keeps the field open).
void NodeOperationsTest::topicMenuStyleCollapseAndText() {
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("A"));
    MindNode *a1 = m_model->addChild(a, QStringLiteral("A1"));
    m_model->addChild(a1, QStringLiteral("A1a"));
    m_scene->autoLayout();
    wait();
    QVERIFY(m_model->setStyle({a}, StyleProperty::FillColor, QColor(Qt::red)));
    wait();
    QVERIFY(a->style != NodeStyleOverrides());
    chooseInNextMenu(this, {QStringLiteral("Reset style")});
    emit m_scene->nodeMenuRequested(a, QCursor::pos());
    wait();
    QVERIFY(a->style == NodeStyleOverrides());
    chooseInNextMenu(this, {QStringLiteral("Collapse all sub-branches")});
    emit m_scene->nodeMenuRequested(a, QCursor::pos());
    wait();
    QVERIFY(!a->collapsed && a1->collapsed);
    chooseInNextMenu(this, {QStringLiteral("Expand all")});
    emit m_scene->nodeMenuRequested(a, QCursor::pos());
    wait();
    QVERIFY(!a->collapsed && !a1->collapsed);
    // Text menu: right-click on the field being edited, Cut empties it and editing goes on.
    NodeItem *item = itemFor(a);
    m_scene->selectNode(a);
    item->beginEditing();
    QApplication::processEvents();
    QVERIFY(item->titleEditor());
    QStringList shown;
    QTimer::singleShot(100, this, [&shown] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) return;
        for (QAction *action : menu->actions()) if (!action->isSeparator()) shown.append(action->text());
        for (QAction *action : menu->actions())
            if (action->text() == QStringLiteral("Cut")) { menu->setActiveAction(action); QTest::keyClick(menu, Qt::Key_Return); return; }
        menu->close();
    });
    const QPoint pos = m_view->mapFromScene(item->titleEditor()->sceneBoundingRect().center());
    QContextMenuEvent event(QContextMenuEvent::Mouse, pos, m_view->viewport()->mapToGlobal(pos));
    QApplication::sendEvent(m_view->viewport(), &event);
    QApplication::processEvents();
    QCOMPARE(shown, (QStringList{QStringLiteral("Cut"), QStringLiteral("Copy"), QStringLiteral("Paste"), QStringLiteral("Select all")}));
    QVERIFY(item->titleEditor());
    QVERIFY(item->titleEditor()->toPlainText().isEmpty());
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("A"));
    QTest::keyClick(m_view->viewport(), Qt::Key_Escape);
    wait();
    QCOMPARE(a->title, QStringLiteral("A"));
}

// FEAT-029: Move to sheet > New sheet: the branch becomes the central topic of a new sheet named
// after it, with the same ids; one Ctrl+Z puts it back.
void NodeOperationsTest::moveBranchToANewSheet() {
    auto *document = m_window->findChild<Document *>();
    QVERIFY(document);
    const int sheets = int(document->sheets().size());
    const int source = document->currentSheet();
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("Branch"));
    m_model->addChild(a, QStringLiteral("Leaf"));
    m_model->addChild(m_model->root(), QStringLiteral("Stays"));
    m_scene->autoLayout();
    wait();
    const QString id = a->id;
    chooseInNextMenu(this, {QStringLiteral("Move to sheet"), QStringLiteral("New sheet")});
    emit m_scene->nodeMenuRequested(a, QCursor::pos());
    wait();
    QCOMPARE(int(document->sheets().size()), sheets + 1);
    QCOMPARE(document->currentSheet(), sheets);
    QCOMPARE(document->sheets().back().name, QStringLiteral("Branch"));
    QCOMPARE(m_model->root()->id, id);
    QCOMPARE(titles(m_model->root()), QStringList{QStringLiteral("Leaf")});
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QVERIFY(m_model->root()->children.empty());  // the new sheet is blank again
    QCOMPARE(document->switchSheet(source).ok, true);
    wait();
    QCOMPARE(titles(m_model->root()), (QStringList{QStringLiteral("Branch"), QStringLiteral("Stays")}));
    QCOMPARE(m_model->root()->children.front()->id, id);
    // To an existing sheet: a floating topic above that map, and that sheet becomes active.
    const QString target = document->sheets()[sheets].name;
    chooseInNextMenu(this, {QStringLiteral("Move to sheet"), target});
    emit m_scene->nodeMenuRequested(m_model->root()->children.front().get(), QCursor::pos());
    wait();
    QCOMPARE(document->currentSheet(), sheets);
    QCOMPARE(m_model->root()->children.size(), size_t(1));
    const MindNode *moved = m_model->root()->children.front().get();
    QVERIFY(moved->floating && moved->id == id && moved->position.y() < 0);
    QCOMPARE(titles(moved), QStringList{QStringLiteral("Leaf")});
    QCOMPARE(document->switchSheet(source).ok, true);
    wait();
    QCOMPARE(titles(m_model->root()), QStringList{QStringLiteral("Stays")});
}

// FEAT-072 F1: the main menu in the reference order; Edit commands follow the selection.
void NodeOperationsTest::mainMenuOrder() {
    auto *menu = m_window->findChild<QMenu *>(QStringLiteral("MainMenu"));
    QVERIFY(menu);
    QStringList items;
    for (QAction *item : menu->actions()) items.append(item->isSeparator() ? QStringLiteral("|") : item->text());
    QCOMPARE(items, (QStringList{QStringLiteral("Undo"), QStringLiteral("Redo"), QStringLiteral("|"), QStringLiteral("New"),
                                 QStringLiteral("Library"), QStringLiteral("Open…"), QStringLiteral("|"), QStringLiteral("Save"),
                                 QStringLiteral("Save as…"), QStringLiteral("|"), QStringLiteral("Import"), QStringLiteral("Export"), QStringLiteral("|"),
                                 QStringLiteral("Settings…"), QStringLiteral("|"), QStringLiteral("File"), QStringLiteral("Edit"),
                                 QStringLiteral("Select"), QStringLiteral("Insert"), QStringLiteral("Tools"), QStringLiteral("View"), QStringLiteral("Help")}));
    QMenu *edit = nullptr;
    for (QAction *item : menu->actions()) if (item->text() == QStringLiteral("Edit")) edit = item->menu();
    QVERIFY(edit);
    const auto editItem = [edit](const QString &text) {
        for (QAction *item : edit->actions()) if (item->text() == text) return item;
        return static_cast<QAction *>(nullptr);
    };
    m_scene->selectNode(m_model->root());
    emit menu->aboutToShow();
    QVERIFY(!editItem(QStringLiteral("Cut"))->isEnabled());  // not on the central topic
    QVERIFY(editItem(QStringLiteral("Copy"))->isEnabled());
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("A"));
    m_scene->autoLayout();
    wait();
    m_scene->selectNode(a);
    emit menu->aboutToShow();
    QVERIFY(editItem(QStringLiteral("Cut"))->isEnabled());
    editItem(QStringLiteral("Duplicate"))->trigger();
    wait();
    QCOMPARE(titles(m_model->root()), (QStringList{QStringLiteral("A"), QStringLiteral("A")}));
    // FEAT-056: Select on the first level, filled when it opens.
    m_scene->selectNode(a);
    QMenu *select = nullptr;
    for (QAction *item : menu->actions()) if (item->text() == QStringLiteral("Select")) select = item->menu();
    QVERIFY(select);
    emit select->aboutToShow();
    const auto selectItem = [select](const QString &text) {
        for (QAction *item : select->actions()) if (item->text() == text) return item;
        return static_cast<QAction *>(nullptr);
    };
    QVERIFY(!selectItem(QStringLiteral("Floating topics"))->isEnabled());
    selectItem(QStringLiteral("Siblings"))->trigger();
    QCOMPARE(m_scene->selectedNodes().size(), 2);
}

// FEAT-072 F3: bar above the one selected topic, "+" beside (child) and under it (sibling).
void NodeOperationsTest::topicToolsFollowTheSelection() {
    auto *tools = m_window->findChild<TopicTools *>();
    QVERIFY(tools);
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("A"));
    m_scene->autoLayout();
    wait();
    m_scene->selectNode(a);
    wait();
    QVERIFY(tools->bar()->isVisible());
    const QRect box = m_view->mapFromScene(m_scene->nodeShapeSceneRect(a)).boundingRect();
    QVERIFY2(tools->bar()->geometry().bottom() < box.top(), qPrintable(QStringLiteral("bar %1,%2 %3x%4 box %5,%6 %7x%8").arg(tools->bar()->x()).arg(tools->bar()->y()).arg(tools->bar()->width()).arg(tools->bar()->height()).arg(box.x()).arg(box.y()).arg(box.width()).arg(box.height())));
    QVERIFY(std::abs(tools->bar()->geometry().center().x() - box.center().x()) <= 1);
    QVERIFY(tools->childKnob()->isVisible() && tools->siblingKnob()->isVisible());
    QVERIFY(tools->childKnob()->geometry().left() > box.right());           // A is on the right
    QVERIFY(tools->siblingKnob()->geometry().top() > box.bottom());
    // Sticker tool: opens the Markers and stickers panel on its Sticker page.
    QToolButton *stickerTool = nullptr;
    for (auto *button : tools->bar()->findChildren<QToolButton *>())
        if (button->toolTip() == QStringLiteral("Add a marker or a sticker to the topic")) stickerTool = button;
    QVERIFY(stickerTool);
    stickerTool->click();
    QVERIFY(m_window->isIconPanelShown());
    QCOMPARE(m_window->findChild<SidePanel *>(QStringLiteral("IconSidePanel"))->currentPage(), 1);
    tools->childKnob()->click();
    wait();
    QCOMPARE(a->children.size(), size_t(1));
    // With children the "+" stands past the collapse circle, not over it.
    m_scene->selectNode(a);
    wait();
    const QRect circle = m_view->mapFromScene(m_scene->collapseSceneRect(a, 1)).boundingRect();
    QVERIFY(!circle.isEmpty());
    QVERIFY(tools->childKnob()->geometry().left() > circle.right());
    // A map read back (model reset, no layout, like opening a .mentis) still has the circle.
    m_model->fromJson(m_model->toJson());
    a = m_model->root()->children.front().get();
    m_scene->selectNode(a);
    wait();
    QVERIFY(!m_scene->collapseSceneRect(a, 1).isEmpty());
    // Org chart: the circle is under the topic, on the elbow's bar, and the child "+" under the circle.
    m_model->root()->structure = MapStructure::typeKey(MapStructure::Type::OrgChart);
    m_scene->autoLayout();
    m_scene->selectNode(a);
    wait();
    const QRectF shape = m_scene->nodeShapeSceneRect(a);
    const QRectF below = m_scene->collapseSceneRect(a, 1);
    QVERIFY(!below.isEmpty());
    QCOMPARE(below.center(), QPointF(shape.center().x(), shape.bottom() + BranchPath::kElbowRunPx));
    QVERIFY(tools->childKnob()->geometry().top() > m_view->mapFromScene(below).boundingRect().bottom());
    m_model->root()->structure.clear();
    m_scene->autoLayout();
    m_scene->selectNode(m_model->root());
    wait();
    QVERIFY(tools->bar()->isVisible());
    QVERIFY(tools->childKnob()->isVisible() && !tools->siblingKnob()->isVisible());  // no sibling of the central topic
    m_scene->clearSelection();
    QTRY_VERIFY(!tools->bar()->isVisible() && !tools->childKnob()->isVisible());
}

// FEAT-072 (measured live 2026-10-23): logic chart with the central topic on one line (28 px DemiBold,
// at most 330 px wide), main topics 100 px from it on curves from one point, rounded bracket below.
void NodeOperationsTest::braceMapLayout() {
    // The brace map measured live 2026-10-24 (FEAT-026 phase E).
    auto *document = m_window->findChild<Document *>();
    const int sheet = document->currentSheet();
    document->addSheet([](MindMapModel &model) {
        MapVariants::apply(model, QStringLiteral("brace-1"), true);
        for (int i = 0; i < 4; ++i) model.addChild(model.root()->children.front().get(), QStringLiteral("Sotto"));
        model.addChild(model.root()->children.at(1).get(), QStringLiteral("Sotto"));
    });
    m_scene->autoLayout();
    wait();
    const MindNode *root = m_model->root();
    const auto shape = [this](const MindNode *node) { return m_scene->nodeShapeSceneRect(node); };
    const QRectF rootRect = shape(root);
    const MindNode *first = root->children.front().get();
    const MindNode *second = root->children.at(1).get();
    const MindNode *lastMain = root->children.back().get();
    QCOMPARE(qRound(rootRect.height()), 65);  // the central topic, one line
    for (const auto &main : root->children) QCOMPARE(qRound(shape(main.get()).left() - rootRect.right()), 66);
    for (const auto &sub : first->children) QCOMPARE(qRound(shape(sub.get()).left() - shape(first).right()), 43);
    // Halfway between the first and last child's centres, no snap; usual gaps between the shapes.
    QVERIFY(std::abs(rootRect.center().y() - (shape(first).center().y() + shape(lastMain).center().y()) / 2) < 0.6);
    QVERIFY(std::abs(shape(first).center().y() - (shape(first->children.front().get()).center().y()
                                                  + shape(first->children.back().get()).center().y()) / 2) < 0.6);
    QCOMPARE(qRound(shape(first->children.at(1).get()).top() - shape(first->children.at(0).get()).bottom()), 6);
    QCOMPARE(qRound(shape(second).top() - shape(first->children.back().get()).bottom()), 35);
    // One brace: arms from the cusp 12 px out to the first and last centres, 37 px wide; none for the middle.
    const QPainterPath upper = m_scene->edgePath(first);
    const QPointF cusp(rootRect.right() + 12, rootRect.center().y());
    QVERIFY(QLineF(upper.elementAt(0), cusp).length() < 0.6);
    const QPointF upperEnd = upper.elementAt(upper.elementCount() - 1);
    QVERIFY(QLineF(upperEnd, QPointF(cusp.x() + 37, shape(first).center().y())).length() < 0.6);
    QVERIFY(m_scene->edgePath(second).isEmpty());
    QVERIFY(m_scene->edgePath(lastMain).elementAt(m_scene->edgePath(lastMain).elementCount() - 1).y > cusp.y());
    // A single child: both arms, to its top and bottom.
    const MindNode *only = second->children.front().get();
    const QRectF onlyRect = shape(only);
    const QRectF arms = m_scene->edgePath(only).boundingRect();
    QVERIFY(std::abs(arms.top() - onlyRect.top()) < 0.6 && std::abs(arms.bottom() - onlyRect.bottom()) < 0.6);
    QCOMPARE(qRound(arms.right() + 17), qRound(onlyRect.left()));
    QCOMPARE(root->branchWidth, 3.0);
    QCOMPARE(first->branchWidth, 2.0);
    QCOMPARE(root->fontSize, 18.0);  // 24 px Medium, main topics Regular (the reference panel)
    QCOMPARE(root->fontWeight, 500);
    QCOMPARE(first->fontWeight, 400);
    QVERIFY(document->switchSheet(sheet).ok);
    wait();
}

void NodeOperationsTest::timelineLayout() {
    // The timeline measured live 2026-10-24 (FEAT-026 phase E): main topic 1 with four subtopics,
    // 2 with one, then 3 and 4 with one each.
    auto *document = m_window->findChild<Document *>();
    const int sheet = document->currentSheet();
    document->addSheet([](MindMapModel &model) {
        MapVariants::apply(model, QStringLiteral("timeline-1"), true);
        for (int i = 0; i < 4; ++i) model.addChild(model.root()->children.front().get(), QStringLiteral("Sotto"));
        for (int i = 1; i < 4; ++i) model.addChild(model.root()->children.at(i).get(), QStringLiteral("Sotto"));
    });
    m_scene->autoLayout();
    wait();
    const MindNode *root = m_model->root();
    const auto shape = [this](const MindNode *node) { return m_scene->nodeShapeSceneRect(node); };
    const auto main = [root](int i) { return root->children.at(i).get(); };
    // On the axis: 100 px between consecutive topics, all centred on the central topic.
    QCOMPARE(qRound(shape(main(0)).left() - shape(root).right()), 100);
    for (int i = 1; i < 4; ++i) {
        QCOMPARE(qRound(shape(main(i)).left() - shape(main(i - 1)).right()), 100);
        QVERIFY(std::abs(shape(main(i)).center().y() - shape(root).center().y()) < 0.6);
    }
    // Children above the first, below the second, and so on: 25 px from the topic, 27 px beside the stem.
    for (int i = 0; i < 4; ++i) {
        const MindNode *nearest = i % 2 == 0 ? main(i)->children.back().get() : main(i)->children.front().get();
        const qreal gap = i % 2 == 0 ? shape(main(i)).top() - shape(nearest).bottom() : shape(nearest).top() - shape(main(i)).bottom();
        QVERIFY2(std::abs(gap - 25) < 0.6, qPrintable(QString::number(gap)));
        QCOMPARE(qRound(shape(nearest).left() - shape(main(i)).center().x()), 27);
    }
    QCOMPARE(qRound(shape(main(0)->children.at(1).get()).top() - shape(main(0)->children.at(0).get()).bottom()), 6);
    // The line to a main topic comes from the previous one; the stem to the farthest child is square.
    const QPainterPath line = m_scene->edgePath(main(1));
    QVERIFY(QLineF(line.elementAt(0), QPointF(shape(main(0)).right(), shape(main(0)).center().y())).length() < 0.6);
    const QPainterPath stem = m_scene->edgePath(main(0)->children.front().get());
    QVERIFY(QLineF(stem.elementAt(0), QPointF(shape(main(0)).center().x(), shape(main(0)).top())).length() < 0.6);
    QCOMPARE(QPointF(stem.elementAt(1)).y(), shape(main(0)->children.front().get()).center().y());
    // A wide branch pushes the next one on its side: its block stays 100 px away.
    m_model->addChild(main(0)->children.back().get(), QStringLiteral("A long subtopic that reaches past the next main topic"));
    m_scene->autoLayout();
    wait();
    qreal right = shape(main(0)).right();
    const std::function<void(const MindNode *)> reach = [&](const MindNode *at) {
        right = std::max(right, shape(at).right());
        for (const auto &child : at->children) reach(child.get());
    };
    reach(main(0));
    QCOMPARE(qRound(shape(main(2)).left() - right), 100);
    QCOMPARE(qRound(shape(main(1)).left() - shape(main(0)).right()), 100);  // the other side does not count
    QCOMPARE(root->branchWidth, 3.0);
    QCOMPARE(root->fontSize, 21.0);  // 28 px Bold, main topics 18 Medium, subtopics 14 Medium (the reference panel)
    QCOMPARE(root->fontWeight, 700);
    QCOMPARE(main(0)->children.front()->fontWeight, 500);
    // Arrow keys follow the timeline: Left/Right along the axis, Up/Down along each column.
    const auto next = [this](const MindNode *at, Qt::Key key) { return m_scene->neighbour(const_cast<MindNode *>(at), key); };
    const auto checkKeys = [&] {
        QCOMPARE(next(root, Qt::Key_Right), main(0));
        QCOMPARE(next(main(0), Qt::Key_Right), main(1));
        QCOMPARE(next(main(3), Qt::Key_Right), nullptr);
        QCOMPARE(next(main(2), Qt::Key_Left), main(1));
        QCOMPARE(next(main(0), Qt::Key_Left), root);
        const MindNode *above = main(0)->children.back().get();  // nearest the axis
        QCOMPARE(next(main(0), Qt::Key_Up), above);
        QCOMPARE(next(main(0), Qt::Key_Down), nullptr);
        QCOMPARE(next(above, Qt::Key_Up), main(0)->children.at(2).get());
        QCOMPARE(next(above, Qt::Key_Down), main(0));
        QCOMPARE(next(main(0)->children.front().get(), Qt::Key_Up), nullptr);  // not into another column
        QCOMPARE(next(above, Qt::Key_Left), main(0));
        QCOMPARE(next(main(1), Qt::Key_Down), main(1)->children.front().get());
        QCOMPARE(next(main(1)->children.front().get(), Qt::Key_Up), main(1));
        QCOMPARE(next(main(1)->children.front().get(), Qt::Key_Down), nullptr);
    };
    checkKeys();
    // Off the axis (variant 3): topics 25 px above and below the axis, the first 25 px from the central
    // topic, each centre 37 px past the previous topic's right edge.
    MapVariants::apply(*m_model, QStringLiteral("timeline-3"), false);
    m_scene->autoLayout();
    wait();
    QCOMPARE(MapStructure::directionOf(root), MapStructure::Direction::OffAxis);
    const qreal axis = shape(root).center().y();
    QCOMPARE(qRound(axis - shape(main(0)).bottom()), 25);
    QCOMPARE(qRound(shape(main(1)).top() - axis), 25);
    QCOMPARE(qRound(shape(main(0)).left() - shape(root).right()), 25);
    QCOMPARE(qRound(shape(main(3)).center().x() - shape(main(2)).right()), 37);
    // The last topic draws the axis, 40 px past it, under the other dots.
    const QRectF axisLine = m_scene->edgePath(main(3)).boundingRect();
    QCOMPARE(qRound(axisLine.right()), qRound(shape(main(3)).right() + 40));
    QVERIFY(std::abs(axisLine.left() - shape(root).right()) < 0.6);
    checkKeys();
    QVERIFY(document->switchSheet(sheet).ok);
    wait();
}

void NodeOperationsTest::treeTableLayout() {
    // The user's sequence 01-05 (docs/features/attachments/FEAT-028) with the usual commands, and the
    // measures of the reference tree table (live 2026-10-25).
    auto *document = m_window->findChild<Document *>();
    document->addSheet([](MindMapModel &model) { MapVariants::apply(model, QStringLiteral("treetable-1"), false); });
    m_scene->autoLayout();
    wait();
    MindNode *root = m_model->root();
    const auto cell = [this](const MindNode *node) {
        const NodeItem *item = itemFor(node);
        return item && item->tableCell() ? item->mapRectToScene(item->tableCell()->rect) : QRectF();
    };
    const auto shape = [this](const MindNode *node) { return m_scene->nodeShapeSceneRect(node); };
    // 01: the central topic alone is the header.
    QCOMPARE(cell(root), shape(root));
    // 02-03: main topics in rows under the header, across the table, without lines.
    MindNode *main1 = m_model->addChild(root, QStringLiteral("Argomento principale 1"));
    MindNode *main2 = m_model->addChild(root, QStringLiteral("Argomento principale 2"));
    m_scene->autoLayout();
    wait();
    QCOMPARE(cell(main1).top(), cell(root).bottom());
    QCOMPARE(cell(main2).top(), cell(main1).bottom());
    QVERIFY(cell(main1).height() >= 48);
    for (const MindNode *main : {main1, main2}) {
        QCOMPARE(cell(main).left(), cell(root).left());
        QCOMPARE(cell(main).right(), cell(root).right());
        QVERIFY(m_scene->edgePath(main).isEmpty());
        QCOMPARE(shape(main).left(), cell(main).left());  // text on the left
        QCOMPARE(shape(main).center().y(), cell(main).center().y());
    }
    QCOMPARE(shape(root).center().x(), cell(root).center().x());  // the header's text centred
    // A click on a cell selects its topic, on its text or elsewhere in the cell (user's report 2026-10-25).
    for (const QPointF at : {shape(main2).center(), QPointF(cell(main2).right() - 6, cell(main2).center().y())}) {
        m_scene->clearSelection();
        const QPoint point = m_view->mapFromScene(at);
        QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, point);
        wait();
        QVERIFY2(itemFor(main2)->isSelected(), qPrintable(QStringLiteral("%1,%2").arg(at.x()).arg(at.y())));
    }
    // 04-05: children in the next column; a leaf reaches the right edge; a cell as tall as its rows.
    MindNode *sub1 = m_model->addChild(main1, QStringLiteral("Sotto-argomento 1"));
    MindNode *sub21 = m_model->addChild(main2, QStringLiteral("Sotto-argomento 1"));
    MindNode *sub211 = m_model->addChild(sub21, QStringLiteral("Sotto-argomento 1"));
    MindNode *sub22 = m_model->addChild(main2, QStringLiteral("Sotto-argomento 2"));
    m_scene->autoLayout();
    wait();
    const qreal right = cell(root).right();
    QCOMPARE(cell(main1).right(), cell(sub1).left());
    QCOMPARE(cell(main2).right(), cell(sub21).left());
    QCOMPARE(cell(sub21).right(), cell(sub211).left());
    for (const MindNode *leaf : {sub1, sub211, sub22}) QCOMPARE(cell(leaf).right(), right);
    QCOMPARE(cell(main1).width(), cell(main2).width());
    QVERIFY(cell(main1).width() >= std::max(shape(main1).width(), shape(main2).width()));
    QCOMPARE(cell(main2).height(), cell(sub21).height() + cell(sub22).height());
    QCOMPARE(cell(sub22).top(), cell(sub21).bottom());
    // Up and Down go to the cell right above or below, whatever its level (user's request 2026-10-25).
    QCOMPARE(m_scene->neighbour(root, Qt::Key_Down), main1);
    QCOMPARE(m_scene->neighbour(main1, Qt::Key_Up), root);
    QCOMPARE(m_scene->neighbour(main1, Qt::Key_Down), main2);
    QCOMPARE(m_scene->neighbour(sub1, Qt::Key_Down), sub21);
    QCOMPARE(m_scene->neighbour(sub211, Qt::Key_Down), sub22);
    QCOMPARE(m_scene->neighbour(sub22, Qt::Key_Up), sub21);
    QCOMPARE(m_scene->neighbour(sub22, Qt::Key_Down), nullptr);
    QCOMPARE(m_scene->neighbour(main2, Qt::Key_Right), sub21);
    QCOMPARE(m_scene->neighbour(sub211, Qt::Key_Left), sub21);
    QCOMPARE(cell(main2).top(), cell(main1).bottom());
    // Colours: the header's fill as the border, main topics with their own fill, the cells below a
    // tint of it (80% white), a fill set by hand kept.
    const NodeItem::TableCell &mainCell = *itemFor(main2)->tableCell();
    QCOMPARE(mainCell.border, root->fill);
    QCOMPARE(mainCell.fill, main2->fill);
    const auto tint = [](int channel) { return qRound(channel + (255 - channel) * 0.8); };
    const QColor expected(tint(main2->fill.red()), tint(main2->fill.green()), tint(main2->fill.blue()));
    for (const MindNode *below : {sub21, sub211, sub22}) {
        const QColor fill = itemFor(below)->tableCell()->fill;
        QVERIFY2(std::abs(fill.red() - expected.red()) <= 1 && std::abs(fill.green() - expected.green()) <= 1
                     && std::abs(fill.blue() - expected.blue()) <= 1, qPrintable(fill.name()));
    }
    // A picture to compare by eye (only with MENTIS_SNAPSHOT_DIR).
    if (const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR"); !dir.isEmpty()) {
        m_scene->selectNode(sub21);
        m_view->centerOn(cell(root).center());
        wait();
        QVERIFY(m_window->grab().save(dir + QStringLiteral("/tree-table.png")));
    }
    sub22->style.fill = QColor(Qt::yellow);
    m_model->refreshStyles();
    m_scene->refreshAll();
    QCOMPARE(itemFor(sub22)->tableCell()->fill, QColor(Qt::yellow));
    // Stage 2 (measured live 2026-10-25): a logic chart in a cell. The whole branch in one cell to the
    // right edge, 16 px from its edges, the topic on the left; the children are topics with lines.
    QVERIFY(document->setStructure(main2, MapStructure::typeKey(MapStructure::Type::LogicChart), QString()));
    m_scene->autoLayout();
    document->flushPendingStep();
    wait();
    if (const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR"); !dir.isEmpty()) {
        m_scene->clearSelection();
        m_view->centerOn(cell(root).center());
        wait();
        QVERIFY(m_window->grab().save(dir + QStringLiteral("/tree-table-logic.png")));
    }
    const QRectF held = cell(main2);
    QVERIFY(!held.isEmpty());
    QCOMPARE(held.right(), cell(root).right());
    QCOMPARE(shape(main2).left(), held.left());
    QRectF branch;
    for (const MindNode *below : {sub21, sub211, sub22}) {
        QVERIFY(!itemFor(below)->tableCell());
        QVERIFY(!m_scene->edgePath(below).isEmpty());
        branch = branch.united(shape(below));
    }
    QVERIFY(itemFor(main2)->zValue() < -1);  // under the lines
    QVERIFY(held.contains(branch));
    QVERIFY(held.right() - branch.right() >= 16 - 0.5);
    QVERIFY(std::abs((branch.top() - held.top()) - (held.bottom() - branch.bottom())) < 1);
    QVERIFY(document->addBoundaries({sub22}).isEmpty());
    // A table in a cell: the topic is its header, the outer cell around it.
    QVERIFY(document->setStructure(main2, MapStructure::typeKey(MapStructure::Type::TreeTable), QString()));
    m_scene->autoLayout();
    document->flushPendingStep();
    wait();
    if (const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR"); !dir.isEmpty()) {
        m_scene->clearSelection();
        m_view->centerOn(cell(root).center());
        wait();
        QVERIFY(m_window->grab().save(dir + QStringLiteral("/tree-table-nested.png")));
    }
    const NodeItem::TableCell &nested = *itemFor(main2)->tableCell();
    QVERIFY(!nested.frame.isNull());
    const QRectF frame = itemFor(main2)->mapRectToScene(nested.frame);
    QCOMPARE(frame.right(), cell(root).right());
    QCOMPARE(cell(sub21).top(), cell(main2).bottom());
    QCOMPARE(cell(sub22).top(), cell(sub21).bottom());
    QVERIFY(frame.contains(cell(main2).united(cell(sub22))));
    QVERIFY(m_scene->edgePath(main2).isEmpty() && m_scene->edgePath(sub21).isEmpty());
    // Back to cells with Undo, twice.
    action(QStringLiteral("Undo"))->trigger();
    action(QStringLiteral("Undo"))->trigger();
    wait();
    root = m_model->root();  // a step of the history may give new topics
    main1 = root->children.at(0).get();
    main2 = root->children.at(1).get();
    sub1 = main1->children.front().get();
    sub21 = main2->children.at(0).get();
    sub211 = sub21->children.front().get();
    sub22 = main2->children.at(1).get();
    QVERIFY(main2->structure.isEmpty());
    QVERIFY(itemFor(sub21)->tableCell() && itemFor(sub211)->tableCell());
    QVERIFY(itemFor(sub21)->tableCell()->frame.isNull());
    QCOMPARE(cell(main2).right(), cell(sub21).left());
    // No free position in a table: a moved cell goes back to its place; no boundaries on cells.
    const QRectF before = cell(sub1);
    sub1->manualOffset = QPointF(40, 30);
    m_scene->autoLayout();
    wait();
    QCOMPARE(cell(sub1), before);
    QVERIFY(document->addBoundaries({sub1}).isEmpty());
    // A table inside a mind map: the branch is the header, its rows under it, a line only to it.
    document->addSheet([](MindMapModel &model) { MapVariants::apply(model, QStringLiteral("mindmap-1"), true); });
    m_scene->autoLayout();
    wait();
    MindNode *owner = m_model->root()->children.front().get();
    MindNode *row1 = m_model->addChild(owner, QStringLiteral("Riga 1"));
    MindNode *row2 = m_model->addChild(owner, QStringLiteral("Riga 2"));
    m_scene->autoLayout();
    document->flushPendingStep();
    wait();
    QVERIFY(document->setStructure(owner, MapStructure::typeKey(MapStructure::Type::TreeTable), QString()));
    m_scene->autoLayout();
    document->flushPendingStep();
    wait();
    QVERIFY(!m_scene->edgePath(owner).isEmpty());
    QVERIFY(m_scene->edgePath(row1).isEmpty());
    QCOMPARE(cell(row1).top(), cell(owner).bottom());
    QCOMPARE(cell(row2).top(), cell(row1).bottom());
    QVERIFY(!itemFor(m_model->root())->tableCell());
    // The other main topics keep clear of the table.
    const QRectF table = cell(owner).united(cell(row2));
    for (const auto &main : m_model->root()->children)
        if (main.get() != owner) QVERIFY(!shape(main.get()).intersects(table));
    // Undo puts the branch back as a mind map branch, without cells.
    action(QStringLiteral("Undo"))->trigger();
    wait();
    owner = m_model->root()->children.front().get();
    QVERIFY(owner->structure.isEmpty());
    QVERIFY(!itemFor(owner)->tableCell());
}

void NodeOperationsTest::treeTableKeepsItsCellsAcrossTypes() {
    // User's photo 2026-10-25: after changing the type of a topic in a cell, the next main topic's cell
    // covered the one above until the sheet was opened again.
    auto *document = m_window->findChild<Document *>();
    document->addSheet([](MindMapModel &model) {
        MapVariants::apply(model, QStringLiteral("treetable-1"), true);
        MindNode *main2 = model.root()->children.at(1).get();
        MindNode *deep = model.addChild(model.addChild(main2, QStringLiteral("Sotto-argomento 1")), QStringLiteral("Sotto-argomento 1"));
        for (int i = 1; i <= 3; ++i) model.addChild(deep, QStringLiteral("Sotto-argomento %1").arg(i));
    });
    m_scene->autoLayout();
    wait();
    const auto check = [this](const QString &step) {
        QList<std::pair<const MindNode *, QRectF>> cells;
        const std::function<void(const MindNode *)> visit = [&](const MindNode *node) {
            const NodeItem *item = itemFor(node);
            if (item && item->isVisible() && item->tableCell()) {
                const NodeItem::TableCell &cell = *item->tableCell();
                cells.append({node, item->mapRectToScene(cell.frame.isNull() ? cell.rect : cell.frame)});
            }
            for (const auto &child : node->children) visit(child.get());
        };
        visit(m_model->root());
        for (int i = 0; i < cells.size(); ++i)
            for (int j = i + 1; j < cells.size(); ++j) {
                const auto encloses = [this](const MindNode *outer, const MindNode *inner) {  // a table in its cell
                    if (itemFor(outer)->tableCell()->frame.isNull()) return false;
                    for (const MindNode *up = inner->parent; up; up = up->parent) if (up == outer) return true;
                    return false;
                };
                if (encloses(cells[i].first, cells[j].first) || encloses(cells[j].first, cells[i].first)) continue;
                const QRectF overlap = cells[i].second.intersected(cells[j].second);
                QVERIFY2(overlap.width() < 1 || overlap.height() < 1,
                         qPrintable(step + QStringLiteral(": ") + cells[i].first->title + QStringLiteral(" / ") + cells[j].first->title
                                    + QStringLiteral(" %1,%2 %3x%4 / %5,%6 %7x%8").arg(cells[i].second.x()).arg(cells[i].second.y()).arg(cells[i].second.width()).arg(cells[i].second.height())
                                          .arg(cells[j].second.x()).arg(cells[j].second.y()).arg(cells[j].second.width()).arg(cells[j].second.height())));
            }
        // Every topic in its cell (not only the cells with each other).
        for (const auto &[node, rect] : std::as_const(cells))
            QVERIFY2(rect.adjusted(-1, -1, 1, 1).contains(m_scene->nodeShapeSceneRect(node)), qPrintable(step + QStringLiteral(": ") + node->title));
    };
    check(QStringLiteral("start"));
    MindNode *deep = m_model->root()->children.at(1)->children.front()->children.front().get();
    using MapStructure::Type;
    for (const Type type : {Type::LogicChart, Type::Brace, Type::OrgChart, Type::TreeChart, Type::Timeline, Type::Fishbone,
                            Type::TreeTable, Type::LogicChart}) {
        QVERIFY(document->setStructure(deep, MapStructure::typeKey(type), QString()));
        m_scene->autoLayout();
        document->flushPendingStep();
        wait();
        check(MapStructure::typeKey(type));
    }
    QVERIFY(document->setStructure(deep, QString(), QString()));
    m_scene->autoLayout();
    document->flushPendingStep();
    wait();
    check(QStringLiteral("inherited"));
    // The same through the Style page's structure select, as the user does it.
    m_scene->selectNode(deep);
    wait();
    QComboBox *select = nullptr;
    for (QComboBox *box : m_window->findChildren<QComboBox *>(QStringLiteral("StructureTypeSelect")))
        if (box->findData(QString()) >= 0) select = box;  // the branch select (with "Same as parent")
    QVERIFY(select);
    for (const Type type : {Type::LogicChart, Type::Brace, Type::TreeTable, Type::OrgChart, Type::LogicChart}) {
        const int index = select->findData(MapStructure::typeKey(type));
        QVERIFY(index >= 0);
        emit select->activated(index);
        wait();
        deep = m_model->root()->children.at(1)->children.front()->children.front().get();
        QCOMPARE(deep->structure, MapStructure::typeKey(type));
        check(QStringLiteral("select ") + MapStructure::typeKey(type));
    }
    // And back through the history: a step restores the positions without a layout.
    for (int i = 0; i < 6; ++i) {
        action(QStringLiteral("Undo"))->trigger();
        wait();
        check(QStringLiteral("undo %1").arg(i));
    }
    for (int i = 0; i < 6; ++i) {
        action(QStringLiteral("Redo"))->trigger();
        wait();
        check(QStringLiteral("redo %1").arg(i));
    }
    // The next tests work on a mind map sheet, as after treeTableLayout.
    document->addSheet([](MindMapModel &model) { MapVariants::apply(model, QStringLiteral("mindmap-1"), true); });
    m_scene->autoLayout();
    wait();
}

void NodeOperationsTest::fishboneLayout() {
    // The fishbone measured live 2026-10-24 (FEAT-026 phase E, variant 1): main topic 1 with four
    // subtopics (the first with a subtopic of its own), 2, 3 and 4 with one each.
    auto *document = m_window->findChild<Document *>();
    const int sheet = document->currentSheet();
    document->addSheet([](MindMapModel &model) {
        MapVariants::apply(model, QStringLiteral("fishbone-1"), true);
        for (int i = 0; i < 4; ++i) model.addChild(model.root()->children.front().get(), QStringLiteral("Sotto-argomento"));
        for (int i = 1; i < 4; ++i) model.addChild(model.root()->children.at(i).get(), QStringLiteral("Sotto-argomento"));
        model.addChild(model.root()->children.front()->children.front().get(), QStringLiteral("Sotto-argomento"));
    });
    m_scene->autoLayout();
    wait();
    const MindNode *root = m_model->root();
    const auto shape = [this](const MindNode *node) { return m_scene->nodeShapeSceneRect(node); };
    const auto main = [root](int i) { return root->children.at(i).get(); };
    const qreal axis = shape(root).center().y();
    const auto foot = [&](int i) { return QPointF(m_scene->edgePath(main(i)).elementAt(0)); };
    const auto near = [](qreal value, qreal expected) {
        if (std::abs(value - expected) >= 0.6) qWarning() << "got" << value << "expected" << expected;
        return std::abs(value - expected) < 0.6;
    };
    // Bones at 60 degrees from the spine to the middle of the side facing it, above and below in turn.
    for (int i = 0; i < 4; ++i) {
        const bool above = i % 2 == 0;
        QCOMPARE(shape(main(i)).center().y() < axis, above);
        const QPointF end(m_scene->edgePath(main(i)).elementAt(1));
        QVERIFY(near(foot(i).y(), axis));
        QVERIFY(QLineF(end, QPointF(shape(main(i)).center().x(), above ? shape(main(i)).bottom() : shape(main(i)).top())).length() < 0.6);
        QVERIFY2(std::abs((end.x() - foot(i).x()) / std::abs(end.y() - axis) - 0.577) < 0.01, qPrintable(QString::number(i)));
    }
    // Subtopics: centres 65 px from the spine (the nearest), 37 apart, 39 from the topic; ticks of 36 px.
    const auto centreY = [&](const MindNode *node) { return shape(node).center().y(); };
    QVERIFY(near(axis - centreY(main(0)->children.back().get()), 65));
    for (int j = 1; j < 4; ++j) QVERIFY(near(centreY(main(0)->children.at(j).get()) - centreY(main(0)->children.at(j - 1).get()), 37));
    QVERIFY(near(centreY(main(0)->children.front().get()) - shape(main(0)).bottom(), 39));
    QVERIFY(near(shape(main(1)).top() - axis, 104));
    const QPainterPath tick = m_scene->edgePath(main(0)->children.at(2).get());
    QVERIFY(near(QLineF(tick.elementAt(0), tick.elementAt(1)).length(), 36));
    QVERIFY(near(QPointF(tick.elementAt(1)).x(), shape(main(0)->children.at(2).get()).left()));
    // Branches: 52 px from the central topic, 35 from the previous one on their side, feet 35 apart at least.
    const auto branchRect = [&](const MindNode *at) {
        QRectF r = shape(at);
        const std::function<void(const MindNode *)> visit = [&](const MindNode *node) {
            r = r.united(shape(node));
            for (const auto &child : node->children) visit(child.get());
        };
        visit(at);
        return r;
    };
    QVERIFY(near(foot(0).x() - shape(root).right(), 52));
    // The second branch: 52 px from the central topic or its foot 35 px past the first, whichever is farther.
    const qreal headRoom = std::min(foot(1).x(), shape(main(1)).left()) - shape(root).right();
    QVERIFY(headRoom > 51.4 && foot(1).x() - foot(0).x() > 34.4);
    QVERIFY(std::abs(headRoom - 52) < 0.6 || std::abs(foot(1).x() - foot(0).x() - 35) < 0.6);
    QVERIFY(near(std::min(foot(2).x(), shape(main(2)).left()) - branchRect(main(0)).right(), 35));
    QVERIFY(near(foot(3).x() - foot(2).x(), 35));
    // The last main topic draws the spine, from the central topic to the end of the farthest branch.
    const QRectF spine = m_scene->edgePath(main(3)).boundingRect();
    QVERIFY(near(spine.left(), shape(root).right()));
    QVERIFY(near(spine.right(), std::max(branchRect(main(2)).right(), branchRect(main(3)).right())));
    QCOMPARE(root->branchWidth, 3.0);
    QCOMPARE(root->fontSize, 21.0);  // 28 px Bold, main topics 18 Regular, subtopics 14 Regular (the reference panel)
    QCOMPARE(root->fontWeight, 700);
    QCOMPARE(main(0)->fontWeight, 400);
    QCOMPARE(root->shape, NodeShape::ParentesiQuadre);
    // Arrow keys follow the fishbone: along the spine from topic to topic, Up/Down along each column of
    // subtopics; deeper levels as in a logic chart.
    const auto next = [this](const MindNode *at, Qt::Key key) { return m_scene->neighbour(const_cast<MindNode *>(at), key); };
    const auto checkKeys = [&](Qt::Key along, Qt::Key back) {
        const MindNode *top = main(0)->children.front().get();  // nearest its topic, above the spine
        QCOMPARE(next(root, along), main(0));
        QCOMPARE(next(main(0), along), main(1));
        QCOMPARE(next(main(3), along), nullptr);
        QCOMPARE(next(main(1), back), main(0));
        QCOMPARE(next(main(0), back), root);
        QCOMPARE(next(main(0), Qt::Key_Down), top);
        QCOMPARE(next(main(0), Qt::Key_Up), nullptr);
        QCOMPARE(next(top, Qt::Key_Up), main(0));
        QCOMPARE(next(main(0)->children.at(1).get(), Qt::Key_Up), top);
        QCOMPARE(next(main(0)->children.back().get(), Qt::Key_Down), nullptr);  // not across the spine
        QCOMPARE(next(top, along), top->children.front().get());
        QCOMPARE(next(main(0)->children.at(1).get(), back), main(0));
        QCOMPARE(next(main(1), Qt::Key_Up), main(1)->children.front().get());
        QCOMPARE(next(main(1)->children.front().get(), Qt::Key_Down), main(1));
    };
    checkKeys(Qt::Key_Right, Qt::Key_Left);
    // Head on the right: the same, mirrored.
    m_model->root()->structureDirection = QStringLiteral("left");
    m_scene->autoLayout();
    wait();
    QVERIFY(near(shape(root).left() - foot(0).x(), 52));
    QVERIFY(shape(main(0)->children.front().get()).right() < foot(0).x());
    QVERIFY(QPointF(m_scene->edgePath(main(0)).elementAt(1)).x() < foot(0).x());
    checkKeys(Qt::Key_Left, Qt::Key_Right);
    QVERIFY(document->switchSheet(sheet).ok);
    wait();
}

void NodeOperationsTest::logicChartLayout() {
    auto *document = m_window->findChild<Document *>();
    const int sheet = document->currentSheet();
    document->addSheet([](MindMapModel &model) {
        MapVariants::apply(model, QStringLiteral("logic-1"), true);
        model.root()->title = QStringLiteral("Argomento centrale");
        for (int i = 0; i < 3; ++i) model.addChild(model.root()->children.front().get(), QStringLiteral("Sotto"));
    });
    m_scene->autoLayout();
    wait();
    const QRectF root = m_scene->nodeShapeSceneRect(m_model->root());
    QVERIFY(root.width() <= 330.5);
    QVERIFY2(root.height() < 70, qPrintable(QString::number(root.height())));  // one line
    for (const auto &main : m_model->root()->children)
        QCOMPARE(qRound(m_scene->nodeShapeSceneRect(main.get()).left() - root.right()), 100);
    QCOMPARE(m_scene->effectiveBranchShape(m_model->root()), BranchShape::Curve);
    QCOMPARE(m_scene->effectiveBranchShape(m_model->root()->children.front().get()), BranchShape::RoundedBracket);
    // Square lines (user's photos 2026-10-23): below every topic, the central one too, a kStubPx
    // straight stub before the vertical bar, where the open branch's circle sits.
    document->addSheet([](MindMapModel &model) {
        MapVariants::apply(model, QStringLiteral("logic-2"), true);
        for (int i = 0; i < 3; ++i) model.addChild(model.root()->children.front().get(), QStringLiteral("Sotto"));
    });
    m_scene->autoLayout();
    wait();
    const MindNode *main = m_model->root()->children.front().get();
    const QPainterPath toSub = m_scene->edgePath(main->children.front().get());
    const qreal mainRight = m_scene->nodeShapeSceneRect(main).right();
    QCOMPARE(qRound(toSub.elementAt(1).x - mainRight), qRound(BranchPath::kStubPx));
    QCOMPARE(toSub.elementAt(1).y, toSub.elementAt(0).y);
    QCOMPARE(qRound(mainRight + NodeItem::kCollapseOpenOffsetPx), qRound(toSub.elementAt(1).x));
    const QPainterPath toMain = m_scene->edgePath(main);
    QCOMPARE(qRound(toMain.elementAt(1).x - m_scene->nodeShapeSceneRect(m_model->root()).right()), qRound(BranchPath::kStubPx));
    QCOMPARE(toMain.elementAt(1).y, toMain.elementAt(0).y);
    QVERIFY(document->switchSheet(sheet).ok);
    wait();
}

// FEAT-030 (user's photos 2026-10-26): a heart, cloud, star or gem wider above its anchor covered the
// bracket of its subtopics. The lines leave the shape's side first, never crossing the shape.
void NodeOperationsTest::branchesStayOutsideAdvancedShapes() {
    auto *document = m_window->findChild<Document *>();
    const int sheet = document->currentSheet();
    document->addSheet([](MindMapModel &model) {
        MapVariants::apply(model, QStringLiteral("logic-1"), true);
        for (int i = 0; i < 5; ++i) model.addChild(model.root()->children.front().get(), QStringLiteral("Sotto-argomento"));
    });
    MindNode *main = m_model->root()->children.front().get();
    for (const NodeShape shape : {NodeShape::Cuore, NodeShape::Nuvola, NodeShape::Stella, NodeShape::Gemma, NodeShape::Scudo,
                                  NodeShape::Parallelogramma}) {
        QVERIFY(m_model->setStyle({main}, StyleProperty::Shape, static_cast<int>(shape)));
        m_scene->autoLayout();
        wait();
        const QPainterPath outline = NodeShapes::outline(shape, m_scene->nodeShapeSceneRect(main));
        for (const auto &child : main->children) {
            const QPainterPath edge = m_scene->edgePath(child.get());
            const qreal startY = edge.elementAt(0).y;
            for (int step = 0; step <= 200; ++step) {
                const QPointF point = edge.pointAtPercent(step / 200.0);
                if (std::abs(point.y() - startY) < 0.5) continue;  // the straight lead out of the shape
                QVERIFY2(!outline.contains(point), qPrintable(QStringLiteral("%1 at %2,%3").arg(NodeStyle::shapeKey(shape))
                                                                  .arg(point.x()).arg(point.y())));
            }
        }
    }
    QVERIFY(document->switchSheet(sheet).ok);
    wait();
}

// FEAT-030 (user's photos 2026-10-26): from a central topic shaped as a star, heart, cloud... the
// branches started on its box, beside the shape. They start on the outline; on the box for rectangles.
void NodeOperationsTest::rootBranchesStartOnTheOutline() {
    auto *document = m_window->findChild<Document *>();
    const int sheet = document->currentSheet();
    document->addSheet([](MindMapModel &model) {
        MapVariants::apply(model, QStringLiteral("mindmap-1"), true);
        while (model.root()->children.size() < 4) model.addChild(model.root(), QStringLiteral("Argomento principale"));
    });
    MindNode *root = m_model->root();
    for (int s = 0; s <= static_cast<int>(NodeShape::TrapezioRovesciato); ++s) {
        const auto shape = static_cast<NodeShape>(s);
        QVERIFY(m_model->setStyle({root}, StyleProperty::Shape, s));
        m_scene->autoLayout();
        wait();
        const QRectF box = m_scene->nodeShapeSceneRect(root);
        const QPainterPath outline = NodeShapes::outline(shape, box);
        for (const auto &main : root->children) {
            const QPainterPath edge = m_scene->edgePath(main.get());
            QVERIFY(!edge.isEmpty());
            const QPointF start = edge.elementAt(0);
            const QLineF outward(box.center(), start);
            const QPointF inside = outward.pointAt(1 - 1.5 / outward.length()), outside = outward.pointAt(1 + 1.5 / outward.length());
            const QString where = QStringLiteral("%1 at %2,%3").arg(NodeStyle::shapeKey(shape)).arg(start.x()).arg(start.y());
            if (NodeShapes::startsOnBox(shape)) {
                QVERIFY2(qAbs(start.x() - box.left()) < 0.5 || qAbs(start.x() - box.right()) < 0.5 || qAbs(start.y() - box.top()) < 0.5
                             || qAbs(start.y() - box.bottom()) < 0.5, qPrintable(where));
            } else {
                QVERIFY2(outline.contains(inside) && !outline.contains(outside), qPrintable(where));
            }
        }
    }
    QVERIFY(document->switchSheet(sheet).ok);
    wait();
}

// FEAT-072 (measured live 2026-10-23): org chart with the main topics 100 px under the central topic
// (subtopics 40 under theirs); tree chart with the main topics 50 px beside the trunk, the first
// branch 56 px under the central topic, and the subtopics beside their main topic (rounded bracket).
void NodeOperationsTest::orgAndTreeChartsLayout() {
    auto *document = m_window->findChild<Document *>();
    const int sheet = document->currentSheet();
    const auto addChart = [&](const QString &variant) {
        document->addSheet([variant](MindMapModel &model) {
            MapVariants::apply(model, variant, true);
            for (int i = 0; i < 3; ++i) model.addChild(model.root()->children.front().get(), QStringLiteral("Sub"));
        });
        m_scene->autoLayout();
        wait();
    };
    addChart(QStringLiteral("org-1"));
    QRectF root = m_scene->nodeShapeSceneRect(m_model->root());
    MindNode *first = m_model->root()->children.front().get();
    for (const auto &main : m_model->root()->children)
        QCOMPARE(qRound(m_scene->nodeShapeSceneRect(main.get()).top() - root.bottom()), 100);
    QCOMPARE(qRound(m_scene->nodeShapeSceneRect(first->children.front().get()).top() - m_scene->nodeShapeSceneRect(first).bottom()), 40);
    addChart(QStringLiteral("tree-1"));
    root = m_scene->nodeShapeSceneRect(m_model->root());
    first = m_model->root()->children.front().get();
    for (const auto &main : m_model->root()->children)
        QCOMPARE(qRound(m_scene->nodeShapeSceneRect(main.get()).left() - root.center().x()), 50);
    const QRectF firstBox = m_scene->nodeShapeSceneRect(first);
    for (const auto &sub : first->children) QVERIFY(m_scene->nodeShapeSceneRect(sub.get()).left() > firstBox.right());  // beside, not under
    QCOMPARE(m_scene->effectiveBranchShape(first), BranchShape::RoundedBracket);
    QVERIFY(document->switchSheet(sheet).ok);
    wait();
}

// FEAT-072 (measured live 2026-10-23, its six free mind map variants): the hand-drawn variant is the
// sixth of the gallery; underlined subtopics are joined by curves, and the middle child's underline
// is at the height of its parent's (a straight line).
void NodeOperationsTest::mindMapVariantsLayout() {
    QCOMPARE(MapVariants::all().at(5).id, QStringLiteral("mindmap-7"));
    auto *document = m_window->findChild<Document *>();
    const int sheet = document->currentSheet();
    document->addSheet([](MindMapModel &model) {
        MapVariants::apply(model, QStringLiteral("mindmap-2"), true);
        MindNode *main = model.root()->children.front().get();
        for (int i = 0; i < 3; ++i) model.addChild(main, QStringLiteral("Sub"));
    });
    m_scene->autoLayout();
    wait();
    MindNode *main = m_model->root()->children.front().get();
    MindNode *middle = main->children.at(1).get();
    QCOMPARE(m_scene->effectiveBranchShape(main), BranchShape::Curve);
    const auto underlineY = [this](const MindNode *node) { return m_scene->nodeShapeSceneRect(node).bottom(); };
    QVERIFY2(std::abs(underlineY(middle) - underlineY(main)) < 1.5,
             qPrintable(QStringLiteral("%1 %2").arg(underlineY(middle)).arg(underlineY(main))));
    // The font per variant and black main topic text on the underline; the bundled fonts exist.
    QCOMPARE(main->fontFamily, QStringLiteral("Roboto"));
    QCOMPARE(main->text, m_model->root()->text);
    // Underlines as thick as the lines reaching them. Since 2026-10-26 (user's decision) the
    // default curve to a subtopic has the fan's stub first: a short straight line, then the curve.
    QCOMPARE(main->borderWidth, m_model->root()->branchWidth);
    QCOMPARE(middle->borderWidth, main->branchWidth);
    const QPainterPath toFirst = m_scene->edgePath(main->children.front().get());
    QCOMPARE(toFirst.elementAt(1).type, QPainterPath::LineToElement);
    QCOMPARE(toFirst.elementAt(1).y, toFirst.elementAt(0).y);  // the stub is horizontal
    QCOMPARE(toFirst.elementAt(2).type, QPainterPath::CurveToElement);
    // The subtopics sit at the fan's gap (54 px), like a curve chosen by hand.
    QVERIFY(m_scene->nodeShapeSceneRect(middle).left() - m_scene->nodeShapeSceneRect(main).right() > 40);
    // Main topics centred by their underlines; every curve from the central topic leaves its top or
    // bottom side (the upper ones left the side: user's reference photos 2026-10-23).
    const QRectF rootRect = m_scene->nodeShapeSceneRect(m_model->root());
    const auto branchY = [this](int row) { return itemFor(child(row))->pos().y() + itemFor(child(row))->branchAnchor(false).y(); };
    QVERIFY(std::abs(branchY(0) + branchY(1) - 2 * rootRect.center().y()) < 0.5);
    for (int row = 0; row < int(m_model->root()->children.size()); ++row) {
        // The tapered outline comes out from under the central topic, cut by its rounded outline (reference
        // photos 2026-10-23): every point of the cut lies on that outline, none inside it or short of it
        // in the rounded corners. The start is the middle of the cut.
        const QPainterPath edge = m_scene->edgePath(child(row));
        const QPainterPath inner = NodeShapes::outline(m_model->root()->shape, rootRect.adjusted(1, 1, -1, -1));
        const QPainterPath outer = NodeShapes::outline(m_model->root()->shape, rootRect.adjusted(-1, -1, 1, 1));
        QPointF start;
        int onBorder = 0;
        for (const QPolygonF &polygon : edge.toFillPolygons())
            for (const QPointF &point : polygon) {
                QVERIFY(!inner.contains(point));
                if (!rootRect.adjusted(-0.5, -0.5, 0.5, 0.5).contains(point)) continue;
                QVERIFY2(outer.contains(point), qPrintable(QStringLiteral("%1 %2").arg(point.x()).arg(point.y())));
                start += point;
                ++onBorder;
            }
        QVERIFY(onBorder >= 2);
        start /= onBorder;
        // Inside the corner by 0.75 x the topic's distance from that side (reference photos 2026-10-23).
        const qreal distance = std::max(rootRect.top() - branchY(row), branchY(row) - rootRect.bottom());
        const qreal inset = itemFor(child(row))->pos().x() > rootRect.center().x() ? rootRect.right() - start.x() : start.x() - rootRect.left();
        QVERIFY2(std::abs(inset - std::min(0.75 * distance, rootRect.width() / 2)) < 4, qPrintable(QStringLiteral("%1 %2").arg(inset).arg(distance)));
    }
    for (const char *family : {"Roboto", "Montserrat", "Arvo", "Atkinson Hyperlegible", "OpenDyslexic"})
        QVERIFY2(NodeItem::isFontInstalled(QString::fromLatin1(family)), family);
    QVERIFY(document->switchSheet(sheet).ok);
    wait();
}

// FEAT-072 F5: Home with the sidebar, "New map" as the first card, More templates and the search.
void NodeOperationsTest::homeLayout() {
    auto *document = m_window->findChild<Document *>();
    document->startUntitled();  // no changes: going Home asks nothing
    action(QStringLiteral("Library"))->trigger();
    QApplication::processEvents();
    auto *home = m_window->findChild<QWidget *>(QStringLiteral("homePage"));
    QVERIFY(home && home->isVisible());
    QLabel *title = home->findChild<QLabel *>(QStringLiteral("HomeTitle"));
    QCOMPARE(title->text(), QStringLiteral("Recent"));
    const auto visibleCards = [home] {
        QStringList names;
        for (QToolButton *tile : home->findChildren<QToolButton *>())
            if (tile->isVisible() && tile->parentWidget()->objectName() == QStringLiteral("HomeCard")) names.append(tile->toolTip());
        return names;
    };
    QCOMPARE(visibleCards().value(0), QStringLiteral("New map"));
    QToolButton *templates = nullptr;
    for (QToolButton *row : home->findChildren<QToolButton *>(QStringLiteral("HomeRow")))
        if (row->text().trimmed() == QStringLiteral("More templates")) templates = row;
    QVERIFY(templates);
    templates->click();
    QApplication::processEvents();
    QCOMPARE(title->text(), QStringLiteral("More templates"));
    QVERIFY(visibleCards().contains(QStringLiteral("SWOT analysis")));
    // Search: "New map" goes away, only matching recent maps stay.
    document->startUntitled();
    action(QStringLiteral("Library"))->trigger();  // back on Recent
    QApplication::processEvents();
    QCOMPARE(title->text(), QStringLiteral("Recent"));
    home->findChild<QLineEdit *>(QStringLiteral("HomeSearch"))->setText(QStringLiteral("zzz-no-map"));
    QApplication::processEvents();
    QVERIFY(visibleCards().isEmpty());
    home->findChild<QLineEdit *>(QStringLiteral("HomeSearch"))->clear();
    qobject_cast<QStackedWidget *>(m_window->centralWidget())->setCurrentIndex(1);
    QApplication::processEvents();
}

// FEAT-064: la formula e' un contenuto del nodo (modale con anteprima), il titolo resta com'e'.
void NodeOperationsTest::formulaIsAContentOfTheNode() {
    MindNode *node = m_model->addChild(m_model->root(), QStringLiteral("Area"));
    wait();
    m_scene->selectNode(node);
    QAction *formula = action(QStringLiteral("LaTeX formula…"));
    QVERIFY(formula);
    QTimer::singleShot(0, this, [] {
        if (auto *dialog = qobject_cast<ModalDialog *>(QApplication::activeModalWidget())) {
            dialog->findChild<QPlainTextEdit *>()->setPlainText(QStringLiteral("\\pi r^2"));
            dialog->acceptButton()->click();
        }
    });
    formula->trigger();
    wait();
    QCOMPARE(node->title, QStringLiteral("Area"));
    QCOMPARE(node->formula, QStringLiteral("\\pi r^2"));
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QVERIFY(child(0)->formula.isEmpty());
}

void NodeOperationsTest::relationButtonWithoutSelectionAddsAFloatingTopic() {
    m_scene->clearSelection();
    QApplication::processEvents();
    QToolButton *relation = nullptr;
    for (auto *button : m_window->findChildren<QToolButton *>())
        if (button->toolTip().startsWith(QStringLiteral("Create a relationship"))) relation = button;
    QVERIFY(relation);
    relation->click();
    wait();
    QCOMPARE(m_model->root()->children.size(), size_t(1));
    QVERIFY(child(0)->floating);
    QCOMPARE(child(0)->title, QStringLiteral("Floating Topic"));
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QVERIFY(m_model->root()->children.empty());
}

QToolButton *NodeOperationsTest::toolButton(const QString &tooltipStart) const {
    for (auto *button : m_window->findChildren<QToolButton *>())
        if (button->toolTip().startsWith(tooltipStart)) return button;
    return nullptr;
}

void NodeOperationsTest::clickNode(MindNode *node) {
    NodeItem *item = itemFor(node);
    QVERIFY(item);
    m_view->ensureVisible(item, 0, 0);
    // Nel riquadro ma lontano dal testo e dai bordi (che ridimensionano).
    const QPoint point = m_view->mapFromScene(item->mapToScene(QPointF(0, item->boundingRect().top() + 6)));
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, point);
    QApplication::processEvents();
}

void NodeOperationsTest::relationIsCreatedByClickingTheTarget() {
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("A"));
    MindNode *b = m_model->addChild(m_model->root(), QStringLiteral("B"));
    m_scene->autoLayout();
    wait();
    m_scene->selectNode(a);
    toolButton(QStringLiteral("Create a relationship"))->click();
    clickNode(b);
    wait();
    QCOMPARE(m_model->relations.size(), 1);
    QCOMPARE(m_model->relations.front().from, a->id);
    QCOMPARE(m_model->relations.front().to, b->id);
    // FEAT-053: the new relationship is selected, with an empty label; Delete removes it.
    QCOMPARE(m_scene->selectedRelationIds(), QStringList{m_model->relations.front().id});
    QVERIFY(m_model->relations.front().label.isEmpty());
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QVERIFY(m_model->relations.isEmpty());
}

// FEAT-053: dragging a handle bends the line (one step), typing writes the label, Delete removes it.
void NodeOperationsTest::relationHandleLabelAndDelete() {
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("A"));
    MindNode *b = m_model->addChild(m_model->root(), QStringLiteral("B"));
    m_scene->autoLayout();
    wait();
    m_scene->selectNode(a);
    toolButton(QStringLiteral("Create a relationship"))->click();
    clickNode(b);
    wait();
    QCOMPARE(m_model->relations.size(), 1);
    const QString id = m_model->relations.front().id;
    RelationshipItem *item = m_scene->relationItem(id);
    QVERIFY(item && item->isSelected());

    const QPoint handle = m_view->mapFromScene(item->handlePos(1));
    const QPoint target = handle + QPoint(60, 0);
    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, handle);
    QMouseEvent move(QEvent::MouseMove, target, m_view->viewport()->mapToGlobal(target), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(m_view->viewport(), &move);
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, target);
    wait();
    QVERIFY(m_model->relations.front().toControl.has_value());
    QVERIFY(QLineF(item->handlePos(1), m_view->mapToScene(target)).length() < 2);

    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    QVERIFY(item->isSelected());
    m_view->setFocus();
    item->setFocus();
    QTest::keyClicks(m_view, QStringLiteral("why"));
    QVERIFY(item->isEditing());
    QTest::keyClick(m_view, Qt::Key_Return);
    wait();
    QCOMPARE(m_model->relations.front().label, QStringLiteral("why"));

    // The Style page shows the relationship's controls; a choice is one step on it.
    auto *page = m_window->findChild<RelationshipStylePage *>();
    QVERIFY(page && !page->isHidden());  // the side panel itself may be closed
    const int straight = page->shapeField()->findData(static_cast<int>(Relationship::Shape::Straight));
    page->shapeField()->setCurrentIndex(straight);
    emit page->shapeField()->activated(straight);
    wait();
    QCOMPARE(m_model->relations.front().shape, Relationship::Shape::Straight);
    QCOMPARE(page->shapeField()->currentIndex(), straight);
    // Start and end marks, like the branches'.
    QCOMPARE(page->endMarkField()->currentData().toInt(), static_cast<int>(BranchEnd::FilledArrow));
    const int dot = page->startMarkField()->findData(static_cast<int>(BranchEnd::Dot));
    page->startMarkField()->setCurrentIndex(dot);
    emit page->startMarkField()->activated(dot);
    wait();
    QCOMPARE(m_model->relations.front().startMark, std::optional<BranchEnd>(BranchEnd::Dot));

    QTest::keyClick(m_view, Qt::Key_Delete);
    wait();
    QVERIFY(m_model->relations.isEmpty());
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QCOMPARE(m_model->relations.size(), 1);
    QCOMPARE(m_model->relations.front().label, QStringLiteral("why"));
}

// FEAT-053: photos for the comparison with the reference (only with MENTIS_SNAPSHOT_DIR): the default
// relationship from the first to the fourth main topic, deselected and selected.
void NodeOperationsTest::relationSnapshot() {
    const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR");
    if (dir.isEmpty()) QSKIP("MENTIS_SNAPSHOT_DIR not set");
    QList<MindNode *> mains;
    for (int i = 0; i < 4; ++i) mains.append(m_model->addChild(m_model->root()));
    m_scene->autoLayout();
    wait();
    m_scene->selectNode(mains.front());
    toolButton(QStringLiteral("Create a relationship"))->click();
    clickNode(mains.back());
    wait();
    QCOMPARE(m_model->relations.size(), 1);
    const QString id = m_model->relations.front().id;
    m_view->centerOn(m_scene->nodeSceneRect(m_model->root()).center());
    m_scene->clearSelection();
    QTest::qWait(200);
    QVERIFY(m_view->grab().save(dir + QStringLiteral("/relation.png")));
    m_scene->selectRelation(id);
    QTest::qWait(200);
    QVERIFY(m_view->grab().save(dir + QStringLiteral("/relation-selected.png")));
}

void NodeOperationsTest::escapeCancelsAPendingRelation() {
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("A"));
    MindNode *b = m_model->addChild(m_model->root(), QStringLiteral("B"));
    m_scene->autoLayout();
    wait();
    m_scene->selectNode(a);
    toolButton(QStringLiteral("Create a relationship"))->click();
    // Le scorciatoie valgono solo nella finestra attiva.
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    m_view->setFocus();
    QTest::keyClick(m_view, Qt::Key_Escape);
    clickNode(b);
    wait();
    QVERIFY(m_model->relations.isEmpty());
}

// FEAT-052 summaries: one per run of siblings, a real topic beside a bracket; Tab does nothing
// on it, Delete removes the summary only, the central topic and a duplicate are refused.
void NodeOperationsTest::summaryLayout() {
    MindNode *main = m_model->addChild(m_model->root(), QStringLiteral("Main"));
    MindNode *a = m_model->addChild(main, QStringLiteral("A"));
    MindNode *b = m_model->addChild(main, QStringLiteral("B long title"));
    MindNode *c = m_model->addChild(main, QStringLiteral("C"));
    m_scene->autoLayout();
    wait();
    QToolButton *summary = toolButton(QStringLiteral("Add a summary"));
    QVERIFY(summary);
    m_scene->selectNode(m_model->root());
    summary->click();
    wait();
    QVERIFY(m_model->summaries.isEmpty());
    m_scene->clearSelection();
    for (MindNode *node : {a, b}) itemFor(node)->setSelected(true);
    summary->click();
    wait();
    QTest::keyClick(m_view, Qt::Key_Escape);  // leaves the title opened for editing
    wait();
    QCOMPARE(m_model->summaries.size(), 1);
    QCOMPARE(m_model->summaries.front().topics, (QStringList{a->id, b->id}));
    MindNode *topic = m_model->summaries.front().topic.get();
    QCOMPARE(topic->title, QStringLiteral("Summary"));
    QCOMPARE(m_scene->selectedNode(), topic);
    QCOMPARE(m_model->nodeById(topic->id), topic);
    QVERIFY(!m_model->indexFromNode(topic).isValid());  // not in the outliner
    // Place: 36 px beyond the run's branches (8 + 11 + 9 + 8), centred on it.
    QRectF covered;
    for (const MindNode *node : {a, b}) covered = covered.united(itemFor(node)->mapRectToScene(itemFor(node)->shapeRect()));
    const QRectF shape = itemFor(topic)->mapRectToScene(itemFor(topic)->shapeRect());
    QCOMPARE(shape.left(), covered.right() + 36);
    QCOMPARE(shape.center().y(), covered.center().y());
    QCOMPARE(topic->fontSize, Summaries::kTopicFontSizePt);
    // Enter adds no sibling to a summary topic (its subtopics: summaryTopicBranch).
    QTest::keyClick(m_view, Qt::Key_Return);
    wait();
    QVERIFY(topic->children.empty());
    QCOMPARE(int(main->children.size()), 3);
    // The same run again: no second summary.
    m_scene->clearSelection();
    for (MindNode *node : {a, b}) itemFor(node)->setSelected(true);
    summary->click();
    wait();
    QCOMPARE(m_model->summaries.size(), 1);
    // A relationship can end on the summary topic.
    QVERIFY(!m_window->findChild<Document *>()->addRelation(c->id, topic->id).isEmpty());
    m_window->findChild<Document *>()->flushPendingStep();
    // Delete on the topic removes the summary (and the relationship); its topics stay.
    m_scene->selectNode(topic);
    QTest::keyClick(m_view, Qt::Key_Delete);
    wait();
    QVERIFY(m_model->summaries.isEmpty());
    QVERIFY(m_model->relations.isEmpty());
    QCOMPARE(int(main->children.size()), 3);
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QCOMPARE(m_model->summaries.size(), 1);
    QCOMPARE(m_model->relations.size(), 1);
    // Deleting the covered topics removes the summary.
    m_scene->clearSelection();
    for (MindNode *node : {a, b}) itemFor(node)->setSelected(true);
    QTest::keyClick(m_view, Qt::Key_Delete);
    wait();
    QVERIFY(m_model->summaries.isEmpty());
}

// FEAT-052: title and style of the summary topic survive save/reopen and undo; the topic object is
// kept across an undo that only changes it (the scene's item points to it).
void NodeOperationsTest::summaryKeepsEverythingAcrossSaveAndUndo() {
    MindNode *main = m_model->addChild(m_model->root(), QStringLiteral("Main"));
    MindNode *a = m_model->addChild(main, QStringLiteral("A"));
    m_scene->autoLayout();
    wait();
    const QList<MindNode *> created = m_window->findChild<Document *>()->addSummaries({a});
    QCOMPARE(created.size(), 1);
    m_scene->autoLayout();
    m_window->findChild<Document *>()->flushPendingStep();
    MindNode *topic = created.front();
    QVERIFY(m_window->findChild<Document *>()->setTitle(topic, QStringLiteral("Total")));
    m_window->findChild<Document *>()->flushPendingStep();
    QVERIFY(m_model->setStyle({topic}, StyleProperty::FillColor, QColor(Qt::red)));
    m_window->findChild<Document *>()->flushPendingStep();
    wait();
    MindMapModel copy;
    QVERIFY(copy.fromJson(m_model->toJson()));
    QCOMPARE(copy.summaries.size(), 1);
    QCOMPARE(copy.summaries.front().topics, QStringList{a->id});
    QCOMPARE(copy.summaries.front().topic->title, QStringLiteral("Total"));
    QCOMPARE(copy.summaries.front().topic->id, topic->id);
    QCOMPARE(copy.summaries.front().topic->parent, copy.nodeById(main->id));
    QCOMPARE(copy.summaries.front().topic->fill, QColor(Qt::red));
    action(QStringLiteral("Undo"))->trigger();
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QCOMPARE(m_model->summaries.front().topic.get(), topic);
    QCOMPARE(topic->title, QStringLiteral("Summary"));
    QVERIFY(itemFor(topic));
}

// FEAT-052: a summary topic has the child "+" only; its subtopics have both and grow a branch away
// from the bracket, outside the outliner rows; the siblings after the run make room for it; the
// branch is saved, undone and removed with the summary.
void NodeOperationsTest::summaryTopicBranch() {
    auto *tools = m_window->findChild<TopicTools *>();
    QVERIFY(tools);
    auto *document = m_window->findChild<Document *>();
    MindNode *main = m_model->addChild(m_model->root(), QStringLiteral("Main"));
    MindNode *a = m_model->addChild(main, QStringLiteral("A"));
    MindNode *b = m_model->addChild(main, QStringLiteral("B"));
    MindNode *c = m_model->addChild(main, QStringLiteral("C"));
    m_scene->autoLayout();
    wait();
    const QList<MindNode *> created = document->addSummaries({a, b});
    QCOMPARE(created.size(), 1);
    MindNode *topic = created.front();
    m_scene->autoLayout();
    document->flushPendingStep();
    m_scene->selectNode(topic);
    wait();
    QVERIFY(tools->childKnob()->isVisible());
    QVERIFY(!tools->siblingKnob()->isVisible());
    const QRect topicBox = m_view->mapFromScene(m_scene->nodeShapeSceneRect(topic)).boundingRect();
    QVERIFY(tools->childKnob()->geometry().left() > topicBox.right());  // away from the bracket
    const int rows = m_model->rowCount(m_model->indexFromNode(main));
    tools->childKnob()->click();
    wait();
    QCOMPARE(topic->children.size(), size_t(1));
    MindNode *first = topic->children.front().get();
    QCOMPARE(m_scene->selectedNode(), first);
    QVERIFY(!m_model->indexFromNode(first).isValid());  // not in the outliner
    QCOMPARE(m_model->rowCount(m_model->indexFromNode(main)), rows);
    QCOMPARE(m_model->nodeById(first->id), first);
    const QString firstId = first->id;  // `first` is freed with the branch at the end
    // A subtopic has both "+": a sibling under it and a child beside it.
    QVERIFY(tools->childKnob()->isVisible() && tools->siblingKnob()->isVisible());
    tools->siblingKnob()->click();
    wait();
    QCOMPARE(topic->children.size(), size_t(2));
    m_scene->selectNode(first);
    wait();
    tools->childKnob()->click();
    wait();
    QCOMPARE(first->children.size(), size_t(1));
    m_scene->selectNode(topic);
    wait();
    tools->childKnob()->click();
    wait();
    QCOMPARE(topic->children.size(), size_t(3));
    // Layout: a logic chart beside the topic, centred on it; the siblings after the run move clear of it.
    const QRectF topicShape = m_scene->nodeShapeSceneRect(topic);
    QRectF branch;
    QList<const MindNode *> pending{topic};
    while (!pending.isEmpty()) {
        const MindNode *node = pending.takeLast();
        for (const auto &child : node->children) {
            const QRectF shape = m_scene->nodeShapeSceneRect(child.get());
            QVERIFY(shape.left() > topicShape.right());
            QVERIFY(m_scene->edgePath(child.get()).length() > 0);  // its line
            branch = branch.united(shape);
            pending.append(child.get());
        }
    }
    const qreal firstY = m_scene->nodeShapeSceneRect(topic->children.front().get()).center().y();
    const qreal lastY = m_scene->nodeShapeSceneRect(topic->children.back().get()).center().y();
    QVERIFY(std::abs((firstY + lastY) / 2 - topicShape.center().y()) < 1);
    QVERIFY(m_scene->nodeShapeSceneRect(c).top() > branch.bottom());
    // Save and reopen keep the branch.
    MindMapModel copy;
    QVERIFY(copy.fromJson(m_model->toJson()));
    QCOMPARE(copy.summaries.front().topic->children.size(), size_t(3));
    MindNode *copied = copy.nodeById(first->id);
    QVERIFY(copied);
    QCOMPARE(copied->parent, copy.summaries.front().topic.get());
    QCOMPARE(copied->children.size(), size_t(1));
    // Undo takes the last subtopic away, with its item; Delete on a subtopic keeps the summary.
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QCOMPARE(topic->children.size(), size_t(2));
    for (const auto &child : topic->children) QVERIFY(itemFor(child.get()));
    m_scene->selectNode(topic->children.back().get());
    QTest::keyClick(m_view, Qt::Key_Delete);
    wait();
    QCOMPARE(topic->children.size(), size_t(1));
    QCOMPARE(m_model->summaries.size(), 1);
    // Delete on the summary topic removes the summary with its branch.
    m_scene->selectNode(topic);
    QTest::keyClick(m_view, Qt::Key_Delete);
    wait();
    QVERIFY(m_model->summaries.isEmpty());
    QVERIFY(!m_model->nodeById(firstId));
    QCOMPARE(int(main->children.size()), 3);
}

// FEAT-052: siblings get one boundary over their run (the one between included), other parents
// one each, the central topic none; a topic selected with its parent (BUG-024) and a run that
// already has one get none; the new boundary is selected; Delete and Undo are one step.
void NodeOperationsTest::boundaryWrapsTheSelectedNodes() {
    MindNode *main = m_model->addChild(m_model->root(), QStringLiteral("Main"));
    MindNode *a = m_model->addChild(main, QStringLiteral("A"));
    MindNode *b = m_model->addChild(main, QStringLiteral("B"));
    MindNode *c = m_model->addChild(main, QStringLiteral("C"));
    MindNode *a1 = m_model->addChild(a, QStringLiteral("A1"));
    m_scene->autoLayout();
    wait();
    QToolButton *boundary = toolButton(QStringLiteral("Draw a dashed boundary"));
    QVERIFY(boundary);
    m_scene->selectNode(a1);
    boundary->click();
    wait();
    QCOMPARE(m_model->boundaries.size(), 1);
    m_scene->clearSelection();
    for (MindNode *node : {a, c, a1}) itemFor(node)->setSelected(true);
    boundary->click();
    wait();
    QCOMPARE(m_model->boundaries.size(), 2);  // A1 is in A's branch: no second boundary on it
    const int run = 1;
    QCOMPARE(m_model->boundaries[run].topics, (QStringList{a->id, b->id, c->id}));
    QCOMPARE(m_model->boundaries[1 - run].topics, QStringList{a1->id});
    QCOMPARE(m_scene->selectedBoundaryIds(), QStringList{m_model->boundaries[run].id});
    for (MindNode *node : {a, c}) itemFor(node)->setSelected(true);
    boundary->click();  // the same run again
    wait();
    QCOMPARE(m_model->boundaries.size(), 2);
    // The run covers the branch of A: A1 is inside the stroke, 8 px from the shapes.
    const BoundaryItem *item = m_scene->boundaryItem(m_model->boundaries[run].id);
    QVERIFY(item);
    QVERIFY(item->strokeRect().contains(itemFor(a1)->mapRectToScene(itemFor(a1)->shapeRect())));
    QRectF covered;
    for (const MindNode *node : {a, b, c}) covered = covered.united(itemFor(node)->mapRectToScene(itemFor(node)->shapeRect()));
    QCOMPARE(item->strokeRect().left(), covered.left() - 8);
    // The boundary on A1 is inside: the outer one widens to keep 8 px around its outline.
    const QRectF inner = m_scene->boundaryItem(m_model->boundaries[1 - run].id)->strokeRect();
    const qreal around = 8 + Boundaries::kDefaultLineWidth / 2;
    QVERIFY(item->strokeRect().adjusted(around, around, -around, -around).contains(inner));
    m_scene->selectBoundary(item->id());
    QTest::keyClick(m_view, Qt::Key_Delete);
    wait();
    QCOMPARE(m_model->boundaries.size(), 1);
    QCOMPARE(int(main->children.size()), 3);  // its topics stay
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QCOMPARE(m_model->boundaries.size(), 2);
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QCOMPARE(m_model->boundaries.size(), 1);
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QVERIFY(m_model->boundaries.isEmpty());
    m_scene->selectNode(m_model->root());
    boundary->click();
    wait();
    QVERIFY(m_model->boundaries.isEmpty());
}

// FEAT-052: a boundary on a middle sibling moves its neighbours apart by its room (margin and half
// the stroke, plus the title tab); typing writes the title; the Style page and a relationship to it.
void NodeOperationsTest::boundaryTitleStyleRoomAndRelation() {
    MindNode *main = m_model->addChild(m_model->root(), QStringLiteral("Main"));
    MindNode *a = m_model->addChild(main, QStringLiteral("A"));
    MindNode *b = m_model->addChild(main, QStringLiteral("B"));
    MindNode *c = m_model->addChild(main, QStringLiteral("C"));
    m_scene->autoLayout();
    wait();
    const auto shape = [this](const MindNode *node) { return itemFor(node)->mapRectToScene(itemFor(node)->shapeRect()); };
    const qreal gapAbove = shape(b).top() - shape(a).bottom();
    const qreal gapBelow = shape(c).top() - shape(b).bottom();
    m_scene->selectNode(b);
    toolButton(QStringLiteral("Draw a dashed boundary"))->click();
    wait();
    QCOMPARE(m_model->boundaries.size(), 1);
    const QString id = m_model->boundaries.front().id;
    const qreal room = Boundaries::kMarginPx + Boundaries::kDefaultLineWidth / 2;
    QCOMPARE(shape(b).top() - shape(a).bottom(), gapAbove + room);
    QCOMPARE(shape(c).top() - shape(b).bottom(), gapBelow + room);

    BoundaryItem *item = m_scene->boundaryItem(id);
    QVERIFY(item && item->isSelected());
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    m_view->setFocus();
    item->setFocus();
    QTest::keyClicks(m_view, QStringLiteral("Group"));
    QVERIFY(item->isEditing());
    QTest::keyClick(m_view, Qt::Key_Return);
    wait();
    QCOMPARE(m_model->boundaries.front().title, QStringLiteral("Group"));
    QCOMPARE(shape(b).top() - shape(a).bottom(), gapAbove + room + Boundaries::kTitleHeightPx);
    // The tab stands on the top edge, 12 px from the left side.
    QCOMPARE(item->titleRect().bottom(), item->strokeRect().top());
    QCOMPARE(item->titleRect().left(), item->strokeRect().left() + Boundaries::kTitleInsetPx);

    auto *page = m_window->findChild<BoundaryStylePage *>();
    QVERIFY(page && !page->isHidden());
    const int rectangle = page->shapeField()->findData(static_cast<int>(Boundary::Shape::Rectangle));
    page->shapeField()->setCurrentIndex(rectangle);
    emit page->shapeField()->activated(rectangle);
    wait();
    QCOMPARE(m_model->boundaries.front().shape, Boundary::Shape::Rectangle);
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QCOMPARE(m_model->boundaries.front().shape, Boundary::Shape::RoundedRectangle);
    QCOMPARE(m_model->boundaries.front().title, QStringLiteral("Group"));

    // A relationship from the boundary to a topic ends on the boundary's outline.
    m_scene->selectBoundary(id);
    toolButton(QStringLiteral("Create a relationship;"))->click();  // the toolbar's (the topic bar needs a topic)
    clickNode(m_model->root());
    wait();
    QCOMPARE(m_model->relations.size(), 1);
    QCOMPARE(m_model->relations.front().from, id);
    const auto geometry = m_scene->relationGeometry(m_model->relations.front());
    QVERIFY(geometry);
    QVERIFY(m_scene->boundaryItem(id)->outline().boundingRect().adjusted(-1, -1, 1, 1).contains(geometry->start));
}

// BUG-023: a boundary title being written, confirmed with Enter or with a click elsewhere, crashed
// the app (the title editor removed its scene event filter from inside that filter).
void NodeOperationsTest::boundaryTitleConfirmedByEnterOrClick() {
    MindNode *main = m_model->addChild(m_model->root(), QStringLiteral("Main"));
    MindNode *a = m_model->addChild(main, QStringLiteral("A"));
    m_model->addChild(main, QStringLiteral("B"));
    m_scene->autoLayout();
    wait();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    m_view->setZoomPercent(100);
    for (const bool withEnter : {true, false}) {
        m_scene->selectNode(a);
        toolButton(QStringLiteral("Draw a dashed boundary"))->click();
        wait();
        const QString id = m_model->boundaries.last().id;
        BoundaryItem *item = m_scene->boundaryItem(id);
        QVERIFY(item);
        item->beginEditing();  // as double click or "Edit title"
        QVERIFY(item->isEditing());
        QTest::keyClicks(m_view, QStringLiteral("Title"));
        m_view->centerOn(m_scene->nodeSceneRect(m_model->root()).center());
        if (withEnter) QTest::keyClick(m_view, Qt::Key_Return);
        else QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, m_view->mapFromScene(m_scene->nodeSceneRect(m_model->root()).center()));
        wait();
        const Document *document = m_window->findChild<Document *>();
        QVERIFY(document->boundary(id));
        QCOMPARE(document->boundary(id)->title, QStringLiteral("Title"));
        action(QStringLiteral("Undo"))->trigger();
        wait();
        action(QStringLiteral("Undo"))->trigger();
        wait();
        QVERIFY(m_model->boundaries.isEmpty());
    }
}

// FEAT-052: photo for the comparison with the reference (only with MENTIS_SNAPSHOT_DIR), the sheet
// measured live: main topic 1 with three subtopics boxed, the middle one boxed again, main topic 2
// with a titled boundary, main topics 3 and 4 in one boundary.
void NodeOperationsTest::boundarySnapshot() {
    const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR");
    if (dir.isEmpty()) QSKIP("MENTIS_SNAPSHOT_DIR not set");
    QList<MindNode *> mains;
    for (int i = 0; i < 4; ++i) mains.append(m_model->addChild(m_model->root()));
    QList<MindNode *> subs;
    for (int i = 0; i < 3; ++i) subs.append(m_model->addChild(mains.front()));
    m_scene->autoLayout();
    wait();
    auto *document = m_window->findChild<Document *>();
    const auto side = [this](const MindNode *node) { return m_scene->sideOf(node); };
    document->addBoundaries({mains[0]}, side);
    document->addBoundaries({subs[1]}, side);
    const QString titled = document->addBoundaries({mains[1]}, side).front();
    document->addBoundaries({mains[2], mains[3]}, side);
    Boundary withTitle = *document->boundary(titled);
    withTitle.title = QStringLiteral("Titolo");
    document->updateBoundaries({withTitle});
    m_scene->autoLayout();
    document->flushPendingStep();
    m_view->centerOn(m_scene->nodeSceneRect(m_model->root()).center());
    m_scene->clearSelection();
    QTest::qWait(200);
    QVERIFY(m_view->grab().save(dir + QStringLiteral("/boundary.png")));
    m_scene->selectBoundary(titled);
    QTest::qWait(200);
    QVERIFY(m_view->grab().save(dir + QStringLiteral("/boundary-selected.png")));
}

// FEAT-052: photo for the comparison with the reference (only with MENTIS_SNAPSHOT_DIR), the sheet measured
// live: a logic chart with four main topics, a summary over the second and third; then a subtopic
// on the second (the bracket moves past it).
void NodeOperationsTest::summarySnapshot() {
    const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR");
    if (dir.isEmpty()) QSKIP("MENTIS_SNAPSHOT_DIR not set");
    m_model->root()->structure = MapStructure::typeKey(MapStructure::Type::LogicChart);
    QList<MindNode *> mains;
    for (int i = 0; i < 4; ++i) mains.append(m_model->addChild(m_model->root()));
    m_scene->autoLayout();
    wait();
    auto *document = m_window->findChild<Document *>();
    document->addSummaries({mains[1], mains[2]}, [this](const MindNode *node) { return m_scene->sideOf(node); });
    m_scene->autoLayout();
    document->flushPendingStep();
    m_view->centerOn(m_scene->nodeSceneRect(m_model->root()).center());
    m_scene->clearSelection();
    QTest::qWait(200);
    QVERIFY(m_view->grab().save(dir + QStringLiteral("/summary.png")));
    m_model->addChild(mains[1]);
    m_scene->autoLayout();
    QTest::qWait(200);
    QVERIFY(m_view->grab().save(dir + QStringLiteral("/summary-child.png")));
    using MapStructure::Type;
    for (const Type type : {Type::OrgChart, Type::TreeChart, Type::Brace, Type::Timeline, Type::Fishbone}) {
        m_model->root()->structure = MapStructure::typeKey(type);
        m_scene->autoLayout();
        m_view->centerOn(m_scene->nodeSceneRect(m_model->root()).center());
        QTest::qWait(200);
        QVERIFY(m_view->grab().save(dir + QStringLiteral("/summary-%1.png").arg(MapStructure::typeKey(type))));
    }
    m_model->root()->structure.clear();
}

// FEAT-052 (measured live 2026-10-25): in an org chart the bracket stands below the row of siblings and
// the summary topic below it; a tree table refuses summaries and hides those it has.
void NodeOperationsTest::summaryInOtherStructures() {
    MindNode *root = m_model->root();
    root->structure = MapStructure::typeKey(MapStructure::Type::OrgChart);
    QList<MindNode *> mains;
    for (int i = 0; i < 3; ++i) mains.append(m_model->addChild(root));
    m_scene->autoLayout();
    wait();
    auto *document = m_window->findChild<Document *>();
    const QList<MindNode *> created = document->addSummaries({mains[0], mains[1]});
    QCOMPARE(created.size(), 1);
    m_scene->autoLayout();
    document->flushPendingStep();
    wait();
    QRectF covered;
    for (const MindNode *node : {mains[0], mains[1]}) covered = covered.united(itemFor(node)->mapRectToScene(itemFor(node)->shapeRect()));
    NodeItem *topic = itemFor(created.front());
    QVERIFY(topic && topic->isVisible());
    const QRectF shape = topic->mapRectToScene(topic->shapeRect());
    QCOMPARE(shape.top(), covered.bottom() + 36);
    QCOMPARE(shape.center().x(), covered.center().x());
    root->structure = MapStructure::typeKey(MapStructure::Type::TreeTable);
    m_scene->autoLayout();
    wait();
    QVERIFY(!topic->isVisible());
    QVERIFY(document->addSummaries({mains[2]}).isEmpty());
    // Fishbone (user's photo 2026-10-25): a summary over a main topic's subtopics stays clear of the
    // next main topic on the same side of the spine, which moves away from it.
    root->structure = MapStructure::typeKey(MapStructure::Type::Fishbone);
    QList<MindNode *> subs;
    for (int i = 0; i < 4; ++i) subs.append(m_model->addChild(mains[0]));
    for (int i = 0; i < 3; ++i) m_model->addChild(mains[2]);
    m_scene->autoLayout();
    wait();
    const QList<MindNode *> bones = document->addSummaries(subs);
    QCOMPARE(bones.size(), 1);
    m_scene->autoLayout();
    document->flushPendingStep();
    wait();
    const QRectF summaryShape = itemFor(bones.front())->mapRectToScene(itemFor(bones.front())->shapeRect());
    QRectF next = itemFor(mains[2])->mapRectToScene(itemFor(mains[2])->shapeRect());
    for (const auto &child : mains[2]->children) next = next.united(itemFor(child.get())->mapRectToScene(itemFor(child.get())->shapeRect()));
    QVERIFY2(!summaryShape.intersects(next), "the summary covers the next main topic");
    QVERIFY(summaryShape.right() < next.left());
    root->structure.clear();
}

// FEAT-052: a summary topic taller than its run moves the siblings around the run apart (the
// boundary room rule), so it covers none of them.
void NodeOperationsTest::tallSummaryMovesTheSiblingsApart() {
    MindNode *main = m_model->addChild(m_model->root(), QStringLiteral("Main"));
    QList<MindNode *> subs;
    for (int i = 0; i < 3; ++i) subs.append(m_model->addChild(main));
    m_scene->autoLayout();
    wait();
    auto *document = m_window->findChild<Document *>();
    const QList<MindNode *> created = document->addSummaries({subs[1]});
    QCOMPARE(created.size(), 1);
    QVERIFY(m_model->setStyle({created.front()}, StyleProperty::FontSize, 40.0));
    m_scene->autoLayout();
    m_scene->autoLayout();  // the room uses the run's height from the last layout
    document->flushPendingStep();
    wait();
    const QRectF summary = itemFor(created.front())->mapRectToScene(itemFor(created.front())->shapeRect());
    QVERIFY(summary.height() > itemFor(subs[1])->shapeRect().height() + 20);
    const QRectF above = itemFor(subs[0])->mapRectToScene(itemFor(subs[0])->shapeRect());
    const QRectF below = itemFor(subs[2])->mapRectToScene(itemFor(subs[2])->shapeRect());
    QVERIFY(above.bottom() <= summary.top());
    QVERIFY(below.top() >= summary.bottom());
}

// FEAT-052 (user 2026-10-25): in org chart, tree and timeline a boundary on the middle subtopic
// keeps clear of its neighbours, 6 px apart without it (before, a known limit: it covered them).
void NodeOperationsTest::boundaryRoomInBlockStructures() {
    using MapStructure::Type;
    MindNode *root = m_model->root();
    MindNode *main = m_model->addChild(root);
    QList<MindNode *> mains;
    for (int i = 0; i < 3; ++i) mains.append(m_model->addChild(main));
    auto *document = m_window->findChild<Document *>();
    for (const Type type : {Type::OrgChart, Type::TreeChart, Type::Timeline}) {
        root->structure = MapStructure::typeKey(type);
        m_scene->autoLayout();
        wait();
        if (m_model->boundaries.isEmpty()) {
            QCOMPARE(document->addBoundaries({mains[1]}).size(), 1);
            document->flushPendingStep();
        }
        m_scene->autoLayout();
        wait();
        const BoundaryItem *item = m_scene->boundaryItem(m_model->boundaries.front().id);
        QVERIFY(item);
        for (const MindNode *neighbour : {mains[0], mains[2]}) {
            const QRectF shape = itemFor(neighbour)->mapRectToScene(itemFor(neighbour)->shapeRect());
            QVERIFY2(!item->strokeRect().intersects(shape), qPrintable(MapStructure::typeKey(type)));
        }
    }
    root->structure.clear();
}

// User request 2026-10-25: the bar for selected text sits like the selected topic's bar, centred on
// the topic, its frame 8 px above the shape, at any zoom; when the topic grows to more lines it
// moves up with the shape's top.
void NodeOperationsTest::textFormatBarSitsAboveTheTopic() {
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("Some words"));
    m_scene->autoLayout();
    wait();
    const QTransform before = m_view->transform();
    m_view->setTransform(QTransform::fromScale(0.66, 0.66));
    NodeItem *item = itemFor(a);
    m_scene->selectNode(a);
    item->beginEditing();
    QApplication::processEvents();
    QVERIFY(item->titleEditor());
    QTest::keyClick(m_view->viewport(), Qt::Key_A, Qt::ControlModifier);
    QApplication::processEvents();
    QGraphicsProxyWidget *bar = nullptr;
    for (QGraphicsItem *candidate : m_scene->items())
        if (auto *proxy = qgraphicsitem_cast<QGraphicsProxyWidget *>(candidate); proxy && proxy->isVisible() && proxy->parentItem() == item)
            bar = proxy;
    QVERIFY(bar);
    const auto check = [&] {
        const QRectF shown = bar->deviceTransform(m_view->viewportTransform()).mapRect(bar->boundingRect());
        const QRectF shape = m_view->viewportTransform().mapRect(item->mapRectToScene(item->shapeRect()));
        QVERIFY2(std::abs(shown.center().x() - shape.center().x()) <= 1, "centred on the topic");
        QVERIFY2(std::abs(shown.bottom() - (shape.top() - 8)) <= 1.5, "8 px above the shape");
    };
    check();
    // User's report 2026-10-25: opening the font or size list (the focus goes to the format bar)
    // must not bring back the topic bar.
    auto *topicBar = m_view->viewport()->findChild<QWidget *>(QStringLiteral("TopicToolbar"));
    QVERIFY(topicBar);
    QComboBox *fontList = bar->widget()->findChild<QComboBox *>();
    QVERIFY(fontList);
    m_scene->setFocusItem(bar);
    fontList->setFocus();
    QTest::qWait(150);  // TopicTools updates on a short timer
    QVERIFY(item->isEditing());
    QVERIFY2(!topicBar->isVisible(), "the topic bar came back over the format bar");
    // Same height as the topic bar's frame, same icon size (user request 2026-10-25).
    const int topicFrame = topicBar->height() - FloatingGroup::kShadowTop - FloatingGroup::kShadowBottom;
    QCOMPARE(bar->widget()->height(), topicFrame);
    const QList<QToolButton *> formatButtons = bar->widget()->findChildren<QToolButton *>();
    const QList<QToolButton *> topicButtons = topicBar->findChildren<QToolButton *>();
    QVERIFY(!formatButtons.isEmpty() && !topicButtons.isEmpty());
    QCOMPARE(formatButtons.front()->iconSize(), topicButtons.front()->iconSize());
    QCOMPARE(formatButtons.front()->height(), topicButtons.front()->height());
    // The topic grows to several lines while the text stays selected: the bar follows the top.
    const qreal topBefore = item->shapeRect().top();
    QTextCursor end(item->titleEditor()->document());
    end.movePosition(QTextCursor::End);
    end.insertText(QStringLiteral(" and many more words so the title wraps onto several lines of text"));
    QApplication::processEvents();
    QVERIFY(item->shapeRect().top() < topBefore - 10);
    QVERIFY(bar->isVisible());
    check();
    QTest::keyClick(m_view->viewport(), Qt::Key_Escape);
    m_view->setTransform(before);
    wait();
}

// Visible shape on the scene: the bounding box keeps 4 px around it for the selection ring, and
// siblings are 6 px apart (FEAT-072), so the boxes may touch while the shapes do not.
static QRectF shapeRectOf(const MindMapScene *scene, const MindNode *node) {
    return scene->nodeSceneRect(node).adjusted(4, 4, -4, -4);
}

void NodeOperationsTest::outlinerItemsAreLaidOutOnTheMap() {
    const auto shapeRect = [this](const MindNode *node) { return shapeRectOf(m_scene, node); };
    showOutliner(true);
    QVERIFY(m_tree);
    m_tree->setFocus();
    // Tre voci di primo livello con Invio (si conferma ogni titolo con Invio), poi due figlie
    // della prima con Invio e Tab.
    for (int i = 0; i < 3; ++i) {
        if (i > 0) selectRow(child(i - 1));
        QTest::keyClick(m_tree, Qt::Key_Return);
        QApplication::processEvents();
        if (auto *editor = m_tree->focusWidget(); editor && editor != m_tree) QTest::keyClick(editor, Qt::Key_Escape);
        QApplication::processEvents();
        m_tree->setFocus();
    }
    QCOMPARE(m_model->root()->children.size(), size_t(3));
    // Prima verifica: solo rami di primo livello, senza figli, creati con Invio.
    wait();
    showOutliner(false);
    wait();
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j)
            QVERIFY2(!shapeRect(child(i)).intersects(shapeRect(child(j))),
                     qPrintable(QStringLiteral("rami %1 e %2 sovrapposti").arg(i).arg(j)));
    showOutliner(true);
    m_tree->setFocus();
    for (int i = 0; i < 2; ++i) {
        MindNode *last = child(0)->children.empty() ? child(0) : child(0)->children.back().get();
        selectRow(last);
        QTest::keyClick(m_tree, Qt::Key_Return);
        QApplication::processEvents();
        if (auto *editor = m_tree->focusWidget(); editor && editor != m_tree) QTest::keyClick(editor, Qt::Key_Escape);
        m_tree->setFocus();
        if (child(0)->children.empty()) {
            selectRow(child(1));
            QTest::keyClick(m_tree, Qt::Key_Tab);  // la nuova voce diventa figlia della prima
            QApplication::processEvents();
        }
    }
    wait();
    showOutliner(false);
    wait();
    // Nessuna coppia di nodi con lo stesso riquadro: tutti disposti.
    QList<MindNode *> nodes;
    std::function<void(MindNode *)> collect = [&](MindNode *node) {
        nodes.append(node);
        for (const auto &c : node->children) collect(c.get());
    };
    collect(m_model->root());
    QCOMPARE(nodes.size(), 6);
    for (int i = 0; i < nodes.size(); ++i)
        for (int j = i + 1; j < nodes.size(); ++j)
            QVERIFY2(!shapeRect(nodes[i]).intersects(shapeRect(nodes[j])),
                     qPrintable(nodes[i]->title + QStringLiteral(" / ") + nodes[j]->title));
}

// FEAT-085: Ctrl+Alt+C / Ctrl+Alt+V on the selection, one Undo step; the format painter of the
// topic bar pastes once, or until Esc after a double click, without selecting the topics clicked.
void NodeOperationsTest::copyAndPasteStyleOnTheMap() {
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("A"));
    MindNode *b = m_model->addChild(m_model->root(), QStringLiteral("B"));
    MindNode *c = m_model->addChild(m_model->root(), QStringLiteral("C"));
    m_scene->autoLayout();
    QVERIFY(m_model->setStyle({a}, StyleProperty::FillColor, QColor("#112233")));
    wait();
    const QColor own("#112233");
    m_window->activateWindow();
    m_view->setFocus();
    m_scene->selectNode(a);
    QTest::keyClick(m_view, Qt::Key_C, Qt::ControlModifier | Qt::AltModifier);
    m_scene->selectNode(b);
    itemFor(c)->setSelected(true);
    QTest::keyClick(m_view, Qt::Key_V, Qt::ControlModifier | Qt::AltModifier);
    wait();
    QCOMPARE(b->fill, own);
    QCOMPARE(c->fill, own);
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QVERIFY(b->fill != own && c->fill != own);

    auto *tools = m_window->findChild<TopicTools *>();
    QAbstractButton *painter = tools->formatPainterButton();
    m_scene->selectNode(a);
    wait();
    QVERIFY(painter->isVisible());
    QTest::mouseClick(painter, Qt::LeftButton);
    QVERIFY(painter->isChecked());
    const auto clickOn = [this](MindNode *node) {
        QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, m_view->mapFromScene(m_scene->nodeSceneRect(node).center()));
        wait();
    };
    clickOn(b);
    QCOMPARE(b->fill, own);
    QVERIFY(!painter->isChecked());
    QCOMPARE(m_scene->selectedNodes(), QList<MindNode *>({a}));
    // The second click of a double click on that topic does not open its title.
    QTest::mouseDClick(m_view->viewport(), Qt::LeftButton, {}, m_view->mapFromScene(m_scene->nodeSceneRect(b).center()));
    wait();
    QVERIFY(!m_scene->isEditing(b));
    clickOn(c);  // the painter is off: an ordinary click
    QVERIFY(c->fill != own);
    m_scene->selectNode(a);
    wait();
    QTest::mouseClick(painter, Qt::LeftButton);
    QTest::mouseDClick(painter, Qt::LeftButton);
    QVERIFY(painter->isChecked());
    clickOn(c);
    QCOMPARE(c->fill, own);
    QVERIFY(painter->isChecked());
    // A rectangle from an empty place paints every topic it touches; the selection stays on A.
    action(QStringLiteral("Undo"))->trigger();
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QVERIFY(b->fill != own && c->fill != own);
    QRectF both = m_scene->nodeSceneRect(b).united(m_scene->nodeSceneRect(c));
    const QPoint from = m_view->mapFromScene(both.topLeft() - QPointF(30, 30));
    const QPoint to = m_view->mapFromScene(both.bottomRight() + QPointF(5, 5));
    QVERIFY(!m_scene->nodeAt(m_view->mapToScene(from)));
    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, from);
    QMouseEvent move(QEvent::MouseMove, QPointF(to), m_view->viewport()->mapToGlobal(QPointF(to)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(m_view->viewport(), &move);
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, to);
    wait();
    QCOMPARE(b->fill, own);
    QCOMPARE(c->fill, own);
    QVERIFY(painter->isChecked());
    QCOMPARE(m_scene->selectedNodes(), QList<MindNode *>({a}));
    QTest::keyClick(m_view, Qt::Key_Escape);
    QVERIFY(!painter->isChecked());

    // While a title is typed the keys belong to the text (AltGr = Ctrl+Alt on Windows).
    m_scene->selectNode(a);
    action(QStringLiteral("Edit text"))->trigger();
    wait();
    QVERIFY(m_scene->isEditing(a));
    QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_C, Qt::ControlModifier | Qt::AltModifier, QStringLiteral("c"));
    override.ignore();
    QApplication::sendEvent(m_view, &override);
    QVERIFY(override.isAccepted());
    QTest::keyClick(m_view, Qt::Key_Escape);
}

QTEST_MAIN(NodeOperationsTest)
#include "node_operations_test.moc"
