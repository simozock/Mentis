// FEAT-017 (docs/features/FEAT-017-pannello-contestuale.md): pannello laterale a tre pagine con il
// vero MainWindow, piattaforma offscreen e impostazioni separate da quelle dell'applicazione.
#include "floatingui.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "performance.h"
#include "segmentedcontrol.h"
#include "sidepanel.h"
#include "stylecontrols.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QGraphicsItem>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTest>
#include <QDialog>
#include <QTimer>
#include <QToolButton>

#include <memory>

namespace {
constexpr int kStyle = 0, kPitch = 1, kMap = 2;
}

class SidePanelTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void selectorOrderAndSingleSurface();
    void noSelectionOpensMapWithStyleDisabled();
    void singleSelectionOpensStyle();
    void rootNodeOpensStyle();
    void manualChoiceUntilSelectionChanges();
    void multipleSelectionKeepsStyleControlsEnabled();
    void pitchShowsTheSettings();
    void reopeningRecomputesContext();
    void pagesAreCreatedOnce();
    void pageChangesDoNotModifyDocument();
    void keyboardNavigation();
    void panelDoesNotStealFocusFromMap();
    void sheetSwitchKeepsHistoryUntouched();
    void standardProfileAnimatesOpening();
    void outlinerHidesPanelAndCommand();
    void closingWithSelectionDoesNotCrash();
    void openingKeepsSelectionBesidePanel_data();
    void openingKeepsSelectionBesidePanel();
    void newChildAtTheEdgeIsShown();

private:
    void setPanelOpen(bool open);
    void placeAtRightEdge(MindNode *node);
    QRect nodeInView(MindNode *node) const;
    QToolButton *segment(const QString &label) const;
    QAction *undoAction() const;

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    QDockWidget *m_dock = nullptr;
    SidePanel *m_panel = nullptr;
    QToolButton *m_toggle = nullptr;
    QComboBox *m_styleCombo = nullptr;  // FEAT-019: select dello stile del ramo nella pagina Stile
    ColorField *m_fillField = nullptr;
    MindNode *m_first = nullptr;
    MindNode *m_second = nullptr;
};

void SidePanelTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisSidePanelTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);  // apertura immediata; Standard in un test a parte
    m_window = std::make_unique<MainWindow>();
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    // The side panel is built once the window has drawn its first frame.
    QTRY_VERIFY(m_window->findChild<QDockWidget *>(QStringLiteral("InspectorDock")));
    auto *pages = qobject_cast<QStackedWidget *>(m_window->centralWidget());
    QVERIFY(pages);
    pages->setCurrentIndex(1);  // editor
    m_model = m_window->findChild<MindMapModel *>();
    auto *view = m_window->findChild<MindMapView *>();
    QVERIFY(m_model && view);
    m_scene = qobject_cast<MindMapScene *>(view->scene());
    m_dock = m_window->findChild<QDockWidget *>(QStringLiteral("InspectorDock"));
    m_panel = m_window->findChild<SidePanel *>(QStringLiteral("InspectorSidePanel"));
    // By name: since FEAT-080 the Gantt's panel button has the same tooltip.
    m_toggle = m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"));
    QVERIFY(m_scene && m_dock && m_panel && m_toggle);
    auto *stylePage = m_panel->page(kStyle)->findChild<StylePage *>();
    QVERIFY(stylePage);
    m_styleCombo = stylePage->branchStyleField();
    m_fillField = stylePage->fillField();
    m_first = m_model->addChild(m_model->root(), QStringLiteral("Primo"));
    m_second = m_model->addChild(m_model->root(), QStringLiteral("Secondo"));
    QApplication::processEvents();
}

void SidePanelTest::cleanupTestCase() { m_window.reset(); }

void SidePanelTest::init() {
    m_scene->clearSelection();
    setPanelOpen(false);
}

void SidePanelTest::setPanelOpen(bool open) {
    if (m_dock->isVisible() != open) m_toggle->click();
    QApplication::processEvents();
    QCOMPARE(m_dock->isVisible(), open);
}

QToolButton *SidePanelTest::segment(const QString &label) const {
    for (auto *button : m_panel->selector()->findChildren<QToolButton *>())
        if (button->text() == label) return button;
    return nullptr;
}

// The node's right side 8 px from the view's right edge, as when the user scrolled it there.
void SidePanelTest::placeAtRightEdge(MindNode *node) {
    // outlinerHidesPanelAndCommand leaves the outliner shown: back to the map.
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Mind map")) button->click();
    QApplication::processEvents();
    auto *view = m_window->findChild<MindMapView *>();
    m_scene->setSceneRect(m_scene->sceneRect().adjusted(-2000, -2000, 2000, 2000));
    const int shift = nodeInView(node).right() - (view->viewport()->width() - 8);
    view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->value() + shift);
    QVERIFY(view->viewport()->rect().contains(nodeInView(node)));
}

QRect SidePanelTest::nodeInView(MindNode *node) const {
    auto *view = m_window->findChild<MindMapView *>();
    return view->mapFromScene(m_scene->nodeSceneRect(node)).boundingRect();
}

QAction *SidePanelTest::undoAction() const {
    for (auto *action : m_window->actions())
        if (action->shortcut() == QKeySequence::Undo || action->shortcut() == QKeySequence(QStringLiteral("Ctrl+Z"))) return action;
    return nullptr;
}

void SidePanelTest::selectorOrderAndSingleSurface() {
    setPanelOpen(true);
    QCOMPARE(m_panel->count(), 3);
    const auto segments = m_panel->selector()->findChildren<QToolButton *>();
    QCOMPARE(segments.size(), 3);
    QCOMPARE(segments[0]->text(), QStringLiteral("Style"));
    QCOMPARE(segments[1]->text(), QStringLiteral("Pitch"));
    QCOMPARE(segments[2]->text(), QStringLiteral("Map"));
    QCOMPARE(m_panel->findChildren<QFrame *>(QStringLiteral("SidePanelSurface")).size(), 1);
    // Un solo segmento attivo.
    int checked = 0;
    for (auto *button : segments) checked += button->isChecked() ? 1 : 0;
    QCOMPARE(checked, 1);
}

void SidePanelTest::noSelectionOpensMapWithStyleDisabled() {
    setPanelOpen(true);
    QCOMPARE(m_panel->currentPage(), kMap);
    QCOMPARE(m_panel->selector()->currentIndex(), kMap);
    QTest::mouseClick(segment(QStringLiteral("Style")), Qt::LeftButton);
    QCOMPARE(m_panel->currentPage(), kStyle);
    QVERIFY(!m_styleCombo->isEnabled());
}

void SidePanelTest::singleSelectionOpensStyle() {
    setPanelOpen(true);
    m_scene->selectNode(m_first);
    QCOMPARE(m_panel->currentPage(), kStyle);
    QVERIFY(m_styleCombo->isEnabled());
}

void SidePanelTest::rootNodeOpensStyle() {
    setPanelOpen(true);
    m_scene->selectNode(m_model->root());
    QCOMPARE(m_panel->currentPage(), kStyle);
}

void SidePanelTest::manualChoiceUntilSelectionChanges() {
    setPanelOpen(true);
    m_scene->selectNode(m_first);
    QTest::mouseClick(segment(QStringLiteral("Map")), Qt::LeftButton);
    QCOMPARE(m_panel->currentPage(), kMap);
    // La scelta manuale non cambia la selezione.
    QCOMPARE(m_scene->selectedNodes(), QList<MindNode *>{m_first});
    m_scene->selectNode(m_second);
    QCOMPARE(m_panel->currentPage(), kStyle);
    QTest::mouseClick(segment(QStringLiteral("Pitch")), Qt::LeftButton);
    QCOMPARE(m_panel->currentPage(), kPitch);
    m_scene->clearSelection();
    QCOMPARE(m_panel->currentPage(), kMap);
}

void SidePanelTest::multipleSelectionKeepsStyleControlsEnabled() {
    // FEAT-019: con piu' nodi la pagina Stile resta attiva (prima, con FEAT-017, si disattivava).
    setPanelOpen(true);
    m_scene->selectNode(m_first);
    QVERIFY(m_styleCombo->isEnabled());
    for (QGraphicsItem *item : m_scene->items())
        if (auto *node = qgraphicsitem_cast<NodeItem *>(item); node && node->node()->parent) item->setSelected(true);
    QVERIFY(m_scene->selectedNodes().size() > 1);
    QCOMPARE(m_panel->currentPage(), kStyle);
    QVERIFY(m_styleCombo->isEnabled());
    QVERIFY(m_fillField->isEnabled());
    // BUG-006: anche con il nodo centrale nella selezione i controlli del Ramo restano attivi
    // (valgono per le linee che partono dai nodi).
    for (QGraphicsItem *item : m_scene->items())
        if (item->flags() & QGraphicsItem::ItemIsSelectable) item->setSelected(true);
    QVERIFY(m_styleCombo->isEnabled());
    QVERIFY(m_fillField->isEnabled());
}

// FEAT-063 stage A: the Pitch tab is no longer a placeholder: preview, theme and aspect ratio.
void SidePanelTest::pitchShowsTheSettings() {
    setPanelOpen(true);
    QTest::mouseClick(segment(QStringLiteral("Pitch")), Qt::LeftButton);
    QWidget *pitch = m_panel->page(kPitch);
    QCOMPARE(pitch->objectName(), QStringLiteral("InspectorPitchPage"));
    QVERIFY(pitch->findChild<QWidget *>(QStringLiteral("PitchPreview")));
    QVERIFY(pitch->findChild<QWidget *>(QStringLiteral("PitchTheme")));
    QVERIFY(pitch->findChild<QWidget *>(QStringLiteral("PitchAspect")));
    for (auto *label : pitch->findChildren<QLabel *>()) QVERIFY(label->text() != QStringLiteral("Coming in a future version"));
}

void SidePanelTest::reopeningRecomputesContext() {
    m_scene->selectNode(m_first);
    setPanelOpen(true);
    QCOMPARE(m_panel->currentPage(), kStyle);
    QTest::mouseClick(segment(QStringLiteral("Map")), Qt::LeftButton);
    setPanelOpen(false);
    setPanelOpen(true);
    QCOMPARE(m_panel->currentPage(), kStyle);  // la scelta manuale non resta
    m_scene->clearSelection();
    setPanelOpen(false);
    setPanelOpen(true);
    QCOMPARE(m_panel->currentPage(), kMap);
}

void SidePanelTest::pagesAreCreatedOnce() {
    setPanelOpen(true);
    const QList<QWidget *> pages{m_panel->page(0), m_panel->page(1), m_panel->page(2)};
    const qsizetype widgets = m_panel->findChildren<QWidget *>().size();
    for (int i = 0; i < 100; ++i) {
        m_scene->selectNode(i % 2 ? m_first : m_second);
        m_panel->selector()->findChildren<QToolButton *>().at(i % 3)->click();
        m_scene->clearSelection();
    }
    QCOMPARE((QList<QWidget *>{m_panel->page(0), m_panel->page(1), m_panel->page(2)}), pages);
    QCOMPARE(m_panel->findChildren<QWidget *>().size(), widgets);
}

void SidePanelTest::pageChangesDoNotModifyDocument() {
    setPanelOpen(true);
    QApplication::processEvents();
    QTest::qWait(400);  // eventuali passi di storico ancora in attesa del timer
    QAction *undo = undoAction();
    QVERIFY(undo);
    const bool modified = m_window->isWindowModified();
    const bool undoEnabled = undo->isEnabled();
    const QString firstTitle = m_first->title;
    for (int i = 0; i < 20; ++i) {
        m_scene->selectNode(i % 2 ? m_first : m_model->root());
        m_panel->selector()->findChildren<QToolButton *>().at(i % 3)->click();
        m_scene->clearSelection();
        setPanelOpen(false);
        setPanelOpen(true);
    }
    QTest::qWait(400);
    QCOMPARE(m_window->isWindowModified(), modified);
    QCOMPARE(undo->isEnabled(), undoEnabled);
    QCOMPARE(m_first->title, firstTitle);
}

void SidePanelTest::keyboardNavigation() {
    setPanelOpen(true);
    SegmentedControl *selector = m_panel->selector();
    QCOMPARE(selector->focusPolicy(), Qt::TabFocus);
    selector->setFocus();
    QTest::keyClick(selector, Qt::Key_Home);
    QCOMPARE(m_panel->currentPage(), kStyle);
    QTest::keyClick(selector, Qt::Key_Right);
    QCOMPARE(m_panel->currentPage(), kPitch);
    QTest::keyClick(selector, Qt::Key_End);
    QCOMPARE(m_panel->currentPage(), kMap);
    QTest::keyClick(selector, Qt::Key_Left);
    QCOMPARE(m_panel->currentPage(), kPitch);
}

void SidePanelTest::panelDoesNotStealFocusFromMap() {
    auto *view = m_window->findChild<MindMapView *>();
    m_window->activateWindow();
    view->setFocus();
    QApplication::processEvents();
    QWidget *before = QApplication::focusWidget();
    QVERIFY(before == view || view->isAncestorOf(before));
    setPanelOpen(true);
    QCOMPARE(QApplication::focusWidget(), before);
    QTest::mouseClick(segment(QStringLiteral("Pitch")), Qt::LeftButton);
    QCOMPARE(m_panel->currentPage(), kPitch);
    QCOMPARE(QApplication::focusWidget(), before);
}

void SidePanelTest::sheetSwitchKeepsHistoryUntouched() {
    setPanelOpen(true);
    // Nuovo foglio: il documento cambia (elenco fogli), ma il cambio foglio in se' non e' un passo.
    QToolButton *addSheet = nullptr;
    for (auto *button : m_window->findChildren<QToolButton *>())
        if (button->toolTip() == QStringLiteral("Add sheet")) addSheet = button;
    QVERIFY(addSheet);
    // FEAT-026 fase D: "+ foglio" apre la galleria; si conferma la prima variante.
    QTimer::singleShot(0, [] { if (auto *gallery = qobject_cast<QDialog *>(QApplication::activeModalWidget())) gallery->accept(); });
    addSheet->click();
    QApplication::processEvents();
    QTest::qWait(400);
    auto *sheets = m_window->findChild<QComboBox *>(QStringLiteral("sheetSelect"));
    QVERIFY(sheets && sheets->count() == 2);
    QAction *undo = undoAction();
    const bool undoEnabled = undo->isEnabled();
    emit sheets->activated(0);
    QApplication::processEvents();
    QCOMPARE(sheets->currentIndex(), 0);
    QCOMPARE(m_panel->currentPage(), kStyle);  // il foglio si apre con il nodo centrale selezionato
    m_scene->clearSelection();
    QCOMPARE(m_panel->currentPage(), kMap);
    emit sheets->activated(1);
    QApplication::processEvents();
    QTest::qWait(400);
    QCOMPARE(undo->isEnabled(), undoEnabled);
    emit sheets->activated(0);
    QApplication::processEvents();
    // Il test usa nodi del primo foglio: dopo il cambio il modello e' stato ricaricato.
    m_first = m_model->root()->children.at(0).get();
    m_second = m_model->root()->children.at(1).get();
}

void SidePanelTest::standardProfileAnimatesOpening() {
    Performance::setMode(Performance::Mode::Standard);
    m_toggle->click();
    QVERIFY(m_dock->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(m_dock->width() >= 256, 3000);  // panel floor (FEAT-072; was 280)
    QCOMPARE(m_panel->currentPage(), kMap);
    m_toggle->click();
    QTRY_VERIFY(!m_dock->isVisible());
    Performance::setMode(Performance::Mode::Light);
}

void SidePanelTest::outlinerHidesPanelAndCommand() {
    setPanelOpen(true);
    QPushButton *outliner = nullptr;
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Outliner")) outliner = button;
    QVERIFY(outliner);
    outliner->click();
    QApplication::processEvents();
    QVERIFY(!m_dock->isVisible());
    QVERIFY(!m_toggle->isVisible());  // il comando non e' disponibile nell'Outliner
}

// Regressione: chiudendo la finestra con un nodo selezionato la scena, distrutta dopo i widget,
// notificava un cambio di selezione al pannello gia' distrutto (crash osservato con gdb).
void SidePanelTest::closingWithSelectionDoesNotCrash() {
    for (int round = 0; round < 3; ++round) {
        auto window = std::make_unique<MainWindow>();
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        // The side panel is built once the window has drawn its first frame.
        QTRY_VERIFY(window->findChild<QDockWidget *>(QStringLiteral("InspectorDock")));
        qobject_cast<QStackedWidget *>(window->centralWidget())->setCurrentIndex(1);
        auto *model = window->findChild<MindMapModel *>();
        auto *scene = qobject_cast<MindMapScene *>(window->findChild<MindMapView *>()->scene());
        MindNode *node = model->addChild(model->root(), QStringLiteral("Nodo"));
        window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))->click();  // the map's, not the Gantt's
        scene->selectNode(node);
        QApplication::processEvents();
        window.reset();  // senza la correzione: accesso a widget distrutti
    }
}

// User, 2026-11-05: a selected node near the right edge stays visible beside the opening panel,
// as if the map moved with it, with either profile.
void SidePanelTest::openingKeepsSelectionBesidePanel_data() {
    QTest::addColumn<bool>("animated");
    QTest::newRow("light") << false;
    QTest::newRow("standard") << true;
}

void SidePanelTest::openingKeepsSelectionBesidePanel() {
    QFETCH(bool, animated);
    Performance::setMode(animated ? Performance::Mode::Standard : Performance::Mode::Light);
    m_scene->selectNode(m_first);
    placeAtRightEdge(m_first);
    m_toggle->click();
    QTRY_VERIFY_WITH_TIMEOUT(m_dock->width() >= 256, 3000);
    QApplication::processEvents();
    auto *view = m_window->findChild<MindMapView *>();
    QVERIFY2(view->viewport()->rect().contains(nodeInView(m_first)), "the node is hidden by the panel");
    m_toggle->click();
    QTRY_VERIFY(!m_dock->isVisible());
    Performance::setMode(Performance::Mode::Light);
}

// User, 2026-11-05: a child added to a node at the edge of the view is scrolled into sight.
void SidePanelTest::newChildAtTheEdgeIsShown() {
    m_scene->selectNode(m_first);
    placeAtRightEdge(m_first);
    QAction *addChild = nullptr;
    for (auto *action : m_window->findChildren<QAction *>())
        if (action->text() == QStringLiteral("Add child")) addChild = action;
    QVERIFY(addChild);
    addChild->trigger();
    QApplication::processEvents();
    MindNode *created = m_scene->selectedNode();
    QVERIFY(created && created->parent == m_first);
    auto *view = m_window->findChild<MindMapView *>();
    QVERIFY2(view->viewport()->rect().contains(nodeInView(created)), "the new child is beyond the edge");
    m_model->removeNode(created);
}

QTEST_MAIN(SidePanelTest)
#include "side_panel_test.moc"
