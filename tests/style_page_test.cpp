// FEAT-019 parte C (docs/features/FEAT-019-fondamenta-stile-nodo.md): pagina Stile attiva con uno o
// piu' nodi. Vero MainWindow, piattaforma offscreen e impostazioni separate da quelle dell'app.
#include "slowtests.h"
#include "floatingui.h"
#include "document.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "performance.h"
#include "sidepanel.h"
#include "stylecontrols.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QPainter>
#include <QGraphicsPathItem>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QLayout>
#include <QSet>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTest>
#include <QDialog>
#include <QTimer>
#include <QToolButton>

#include <functional>
#include <memory>

class StylePageTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void mixedValuesShowDashAndOpeningChangesNothing();
    void colorAppliesToAllSelectedNodesInOneStep();
    void escapeInPickerChangesNothing();
    void colorOnTransparentFillIsOpaque();
    void branchWidthAppliesAndShowsOrigin();
    void branchWidthRejectsInvalidValues();
    void branchStyleAppliesToAllSelectedNodes();
    void branchEndSelects();
    void borderAndBranchColorsAreSeparate();
    void palettePickerShowsThreeGroups();
    void paletteChoiceIsOneStepAndBecomesFavorite();
    void escapeInPalettePickerChangesNothing();  // e Invio applica
    void sheetsKeepTheirPalette();
    void panelIsNeverNarrowerThanItsContent();
    void presetAppliesToSelectionInOneStep();
    void fillAndBorderTypesShowMixedAndApply();
    void borderWidthField();
    void shapeAppliesAndRelaysTheMap();
    void branchBoundsContainTheStroke();
    void gridMenusShowColumnsAndNone();
    void gridMenuScrollsWhenTallerThanScreen();
    void textSectionAppliesToAllSelectedNodes();
    void textButtonsSitInTheMiddleOfTheirBar();
    void alignmentPlacesTextInsideTheNode();
    void gridMenuChoosesWithClickKeysAndEsc();
    void doubleUnderlineFollowsBorderWidth();
    void widthFieldTypesArrowsAndFit();
    void widthArrowsChangeEachNodeWhenMixed();
    void disabledRowsDimTheirLabel();
    void styleCardFontAndApplyToRelated();  // FEAT-072 B9, B12, B15
    void applyingToThousandNodes();
    void changingPaletteOnThousandNodes();
    void presetOnThousandNodes();
    void shapeOnThousandNodes();
    void sketchOnThousandNodes();
    void fontSizeOnThousandNodes();
    void widthOnThousandNodes();

private:
    QList<MindNode *> addSelectedNodes(int count);
    QList<MindNode *> addNodes(int count);
    void select(const QList<MindNode *> &nodes);
    QAction *action(const QString &shortcut) const;
    // Apre il select "Tema colore" e, dentro il ciclo d'attesa del menu, esegue interact.
    void runPalettePicker(const std::function<void(PalettePicker *)> &interact);
    PaletteSelect *m_paletteSelect = nullptr;
    void flushHistory();
    // FEAT-038: apre il menu a griglia della select e lo restituisce (nullptr se non si apre).
    QWidget *openGridMenu(QComboBox *select);
    static QList<QToolButton *> gridTiles(QWidget *popup);
    // FEAT-022: distanza (px) del testo disegnato dai lati sinistro e destro del riquadro del nodo.
    std::pair<int, int> textMargins(MindNode *node);
    // Ultimi 1000 rami creati da applyingToThousandNodes.
    QList<MindNode *> lastThousandNodes() const;
    // Disegna al 100% l'area dei nodi, a riquadri grandi come una finestra; tempo in ms.
    double drawingMs(const QList<MindNode *> &nodes);

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    StylePage *m_page = nullptr;
};

void StylePageTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisStylePageTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);
    m_window = std::make_unique<MainWindow>();
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    // The side panel is built once the window has drawn its first frame.
    QTRY_VERIFY(m_window->findChild<QDockWidget *>(QStringLiteral("InspectorDock")));
    auto *pages = qobject_cast<QStackedWidget *>(m_window->centralWidget());
    QVERIFY(pages);
    pages->setCurrentIndex(1);
    m_model = m_window->findChild<MindMapModel *>();
    auto *view = m_window->findChild<MindMapView *>();
    QVERIFY(m_model && view);
    m_scene = qobject_cast<MindMapScene *>(view->scene());
    m_page = m_window->findChild<StylePage *>();
    QVERIFY(m_scene && m_page);
    m_paletteSelect = m_window->findChild<PaletteSelect *>();
    QVERIFY(m_paletteSelect);
    QSettings().remove(QStringLiteral("palettes/favorites"));
    // Pannello aperto, come lo usa l'utente.
    m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))->click();  // the map's, not the Gantt's
    QApplication::processEvents();
    QVERIFY(m_window->findChild<QDockWidget *>(QStringLiteral("InspectorDock"))->isVisible());
}

void StylePageTest::cleanupTestCase() { m_window.reset(); }

void StylePageTest::init() {
    // FEAT-012 (passo 4a): qui si provano misure e distanze verticali fra rami vicini, non i due lati
    // (tests/balanced_layout_test.cpp): tutti i rami a destra, la direzione "destra" della Mappa mentale.
    if (m_model->root()->structureDirection != QLatin1String("right")) {
        m_model->root()->structure = QStringLiteral("mindmap");
        m_model->root()->structureDirection = QStringLiteral("right");
        m_scene->autoLayout();
        // Passo a se': un Ctrl+Z della prova non deve toglierlo insieme alla sua modifica.
        m_window->findChild<Document *>()->flushPendingStep();
    }
    m_scene->clearSelection();
    flushHistory();
}

void StylePageTest::flushHistory() {
    QApplication::processEvents();
    // The open history step is closed at once instead of waiting for its 150 ms timer.
    m_window->findChild<Document *>()->flushPendingStep();
    QApplication::processEvents();
}

QList<MindNode *> StylePageTest::addNodes(int count) {
    QList<MindNode *> nodes;
    for (int i = 0; i < count; ++i) nodes.append(m_model->addChild(m_model->root(), QStringLiteral("Nodo %1").arg(i)));
    m_scene->autoLayout();
    flushHistory();
    return nodes;
}

void StylePageTest::select(const QList<MindNode *> &nodes) {
    m_scene->clearSelection();
    for (auto *item : m_scene->items())
        if (auto *node = qgraphicsitem_cast<NodeItem *>(item); node && nodes.contains(node->node())) node->setSelected(true);
    flushHistory();
}

QList<MindNode *> StylePageTest::addSelectedNodes(int count) {
    const QList<MindNode *> nodes = addNodes(count);
    select(nodes);
    return nodes;
}

QAction *StylePageTest::action(const QString &shortcut) const {
    for (auto *candidate : m_window->actions())
        if (candidate->shortcuts().contains(QKeySequence(shortcut))) return candidate;
    return nullptr;
}

void StylePageTest::mixedValuesShowDashAndOpeningChangesNothing() {
    const QList<MindNode *> nodes = addNodes(2);
    const auto before = m_model->toFlatSnapshot();
    select(nodes);
    QVERIFY(nodes[0]->fill != nodes[1]->fill);  // due colori diversi della palette
    QVERIFY(m_page->fillField()->isEnabled());
    QVERIFY(m_page->fillField()->isMixed());
    QVERIFY(!m_page->borderField()->isMixed() == (nodes[0]->border == nodes[1]->border));
    QCOMPARE(m_page->branchWidthField()->currentData().toDouble(), 2.0);  // Sottile su entrambi
    QCOMPARE(m_page->branchWidthField()->property("manual").toBool(), false);  // ricavato: grigio
    // Selezionare i nodi e mostrare i loro valori nel pannello non scrive nulla nella mappa.
    QCOMPARE(m_model->toFlatSnapshot(), before);
}

void StylePageTest::colorAppliesToAllSelectedNodesInOneStep() {
    const QList<MindNode *> nodes = addSelectedNodes(3);
    QList<QColor> before;
    for (MindNode *node : nodes) before.append(node->fill);
    QTimer::singleShot(0, this, [] {
        if (ColorPicker *picker = ColorPicker::openPicker()) {
            for (auto *button : picker->findChildren<QToolButton *>())
                if (button->toolTip() == QStringLiteral("#2CD551")) { button->click(); return; }
        }
    });
    m_page->fillField()->click();
    QApplication::processEvents();
    for (MindNode *node : nodes) {
        QCOMPARE(node->fill, QColor("#2cd551"));
        QVERIFY(NodeStyle::hasManualValue(node, StyleProperty::FillColor));
    }
    QVERIFY(!m_page->fillField()->isMixed());
    QVERIFY(m_page->fillField()->isManual());
    // Un solo Ctrl+Z riporta ogni nodo al suo colore di prima; Ctrl+Y li riapplica tutti.
    QAction *undo = action(QStringLiteral("Ctrl+Z"));
    QAction *redo = action(QStringLiteral("Ctrl+Y"));
    QVERIFY(undo && redo);
    undo->trigger();
    QApplication::processEvents();
    const auto &children = m_model->root()->children;
    for (int i = 0; i < nodes.size(); ++i) {
        const MindNode *node = children[children.size() - nodes.size() + static_cast<size_t>(i)].get();
        QCOMPARE(node->fill, before[i]);
        QVERIFY(!NodeStyle::hasManualValue(node, StyleProperty::FillColor));
    }
    redo->trigger();
    QApplication::processEvents();
    for (int i = 0; i < nodes.size(); ++i)
        QCOMPARE(children[children.size() - nodes.size() + static_cast<size_t>(i)]->fill, QColor("#2cd551"));
}

void StylePageTest::escapeInPickerChangesNothing() {
    const QList<MindNode *> nodes = addSelectedNodes(2);
    const auto before = m_model->toFlatSnapshot();
    QTimer::singleShot(0, this, [] {
        if (ColorPicker *picker = ColorPicker::openPicker()) QTest::keyClick(picker, Qt::Key_Escape);
    });
    m_page->textField()->click();
    QApplication::processEvents();
    QCOMPARE(m_model->toFlatSnapshot(), before);
}

void StylePageTest::colorOnTransparentFillIsOpaque() {
    // BUG-011: dal terzo livello il riempimento e' trasparente; il colore scelto deve vedersi.
    MindNode *main = m_model->addChild(m_model->root(), QStringLiteral("Principale"));
    MindNode *second = m_model->addChild(main, QStringLiteral("Secondo"));
    MindNode *third = m_model->addChild(second, QStringLiteral("Terzo"));
    m_scene->autoLayout();
    select({third});
    QCOMPARE(third->fill.alpha(), 0);
    QTimer::singleShot(0, this, [] {
        if (ColorPicker *picker = ColorPicker::openPicker())
            for (auto *button : picker->findChildren<QToolButton *>())
                if (button->toolTip() == QStringLiteral("#2CD551")) { button->click(); return; }
    });
    m_page->fillField()->click();
    QApplication::processEvents();
    QCOMPARE(third->fill, QColor("#2cd551"));
    QCOMPARE(third->fill.alpha(), 255);
}

void StylePageTest::branchWidthAppliesAndShowsOrigin() {
    const QList<MindNode *> nodes = addSelectedNodes(2);
    // FEAT-023: spessori con i nomi (Extra sottile 1, Sottile 2, Medio 3, Grassetto 5, Extra spesso 8).
    QComboBox *field = m_page->branchWidthField();
    QCOMPARE(field->count(), 5);
    emit field->activated(field->findData(5.0));  // Grassetto
    QApplication::processEvents();
    for (MindNode *node : nodes) QCOMPARE(node->branchWidth, 5.0);
    QCOMPARE(field->currentData().toDouble(), 5.0);
    QCOMPARE(field->property("manual").toBool(), true);  // scelto a mano: chiaro
}

void StylePageTest::branchWidthRejectsInvalidValues() {
    const QList<MindNode *> nodes = addSelectedNodes(2);
    const auto before = m_model->toFlatSnapshot();
    // Uno spessore in px fuori dall'elenco (file di prima) si mostra come numero; uno fuori dai limiti
    // non si accetta.
    nodes[0]->style.branchWidth = 4.0;
    m_model->refreshStyles();
    select({nodes[0]});
    QComboBox *field = m_page->branchWidthField();
    QCOMPARE(field->currentText(), QStringLiteral("4 px"));
    QVERIFY(!m_model->setStyle({nodes[1]}, StyleProperty::BranchWidth, 40.0));
    Q_UNUSED(before);
}

void StylePageTest::branchStyleAppliesToAllSelectedNodes() {
    const QList<MindNode *> nodes = addSelectedNodes(2);
    QComboBox *combo = m_page->branchStyleField();
    const int dashed = combo->findData(static_cast<int>(Qt::DashLine));
    QVERIFY(dashed >= 0);
    emit combo->activated(dashed);
    QApplication::processEvents();
    for (MindNode *node : nodes) QCOMPARE(node->branchStyle, Qt::DashLine);
    QCOMPARE(combo->currentIndex(), dashed);
    // FEAT-038: every pattern is a preview icon, its name only in the tooltip.
    for (int i = 0; i < combo->count(); ++i) {
        QVERIFY(!combo->itemIcon(i).isNull());
        QVERIFY(!combo->itemData(i, Qt::ToolTipRole).toString().isEmpty());
    }
    // FEAT-023: nessuna linea e linee a mano libera.
    for (const int style : {int(Qt::NoPen), int(NodeStyle::kFreehandSolidLine), int(NodeStyle::kFreehandDashLine)}) {
        QVERIFY(combo->findData(style) >= 0);
        emit combo->activated(combo->findData(style));
        QApplication::processEvents();
        for (MindNode *node : nodes) QCOMPARE(int(node->branchStyle), style);
    }
}

void StylePageTest::branchEndSelects() {
    // FEAT-023: Start / End selects with six marks; mixed values show "—".
    const QList<MindNode *> nodes = addSelectedNodes(2);
    QComboBox *start = m_page->branchStartField();
    QComboBox *end = m_page->branchEndField();
    QCOMPARE(end->count(), 6);
    QCOMPARE(end->currentData().toInt(), static_cast<int>(BranchEnd::None));
    emit end->activated(end->findData(static_cast<int>(BranchEnd::Diamond)));
    QApplication::processEvents();
    for (MindNode *node : nodes) QCOMPARE(node->branchEnd, BranchEnd::Diamond);
    QCOMPARE(start->currentData().toInt(), static_cast<int>(BranchEnd::None));
    QVERIFY(m_model->setStyle({nodes[0]}, StyleProperty::BranchEnd, static_cast<int>(BranchEnd::Square)));
    QApplication::processEvents();
    QCOMPARE(end->currentIndex(), -1);
}

void StylePageTest::borderAndBranchColorsAreSeparate() {
    // BUG-005: il campo Bordo cambia solo il bordo; il campo Colore del Ramo solo il ramo.
    const QList<MindNode *> nodes = addSelectedNodes(1);
    const QColor branchBefore = nodes[0]->branchColor;
    emit m_page->borderField()->colorChosen(QColor("#0000ff"));
    QApplication::processEvents();
    QCOMPARE(nodes[0]->border, QColor("#0000ff"));
    QCOMPARE(nodes[0]->branchColor, branchBefore);
    emit m_page->branchColorField()->colorChosen(QColor("#ff0000"));
    QApplication::processEvents();
    QCOMPARE(nodes[0]->branchColor, QColor("#ff0000"));
    QCOMPARE(nodes[0]->border, QColor("#0000ff"));
    QVERIFY(m_page->branchColorField()->isManual());
}

void StylePageTest::runPalettePicker(const std::function<void(PalettePicker *)> &interact) {
    bool opened = false;
    QTimer::singleShot(0, this, [&] {
        PalettePicker *picker = PalettePicker::openPicker();
        opened = picker != nullptr;
        if (!picker) return;
        interact(picker);
        if (PalettePicker::openPicker()) PalettePicker::openPicker()->close();  // fine della prova
    });
    m_paletteSelect->click();
    QApplication::processEvents();
    QVERIFY2(opened, "il menu delle palette non si e' aperto");
}

namespace {
QList<QToolButton *> paletteTiles(QWidget *page) {
    QList<QToolButton *> tiles;
    for (auto *button : page->findChildren<QToolButton *>())
        if (!button->toolTip().isEmpty()) tiles.append(button);
    return tiles;
}

// Primo colore che la palette da' ai rami: i quasi bianchi si saltano (FEAT-031).
QColor firstBranchColor(const NodeStyle::Palette *palette) {
    for (const QString &color : palette->colors)
        if (!NodeStyle::isNearWhite(QColor(color))) return QColor(color);
    return QColor(palette->colors.front());
}

QToolButton *paletteTile(PalettePicker *picker, const QString &name) {
    for (auto *button : picker->findChildren<QToolButton *>())
        if (button->toolTip() == name) return button;
    return nullptr;
}
}  // namespace

void StylePageTest::palettePickerShowsThreeGroups() {
    QCOMPARE(m_paletteSelect->paletteId(), QStringLiteral("alba"));
    int colorful = 0, classic = 0, favorites = -1, openedOn = -1;
    runPalettePicker([&](PalettePicker *picker) {
        auto *pages = picker->findChild<QStackedWidget *>();
        openedOn = picker->currentGroup();
        favorites = paletteTiles(pages->widget(0)).size();
        colorful = paletteTiles(pages->widget(1)).size();
        classic = paletteTiles(pages->widget(2)).size();
    });
    QCOMPARE(openedOn, 1);  // Alba e' nel gruppo Colorato
    QCOMPARE(favorites, 0);  // nessuna palette usata in questa prova
    QCOMPARE(colorful, 34);
    QCOMPARE(classic, 9);
}

void StylePageTest::paletteChoiceIsOneStepAndBecomesFavorite() {
    const QList<MindNode *> nodes = addNodes(2);
    const QStringList before = m_model->activePalette;
    runPalettePicker([](PalettePicker *picker) {
        picker->showGroup(2);
        paletteTile(picker, QStringLiteral("Zen"))->click();
    });
    QApplication::processEvents();
    QCOMPARE(m_model->paletteId, QStringLiteral("zen"));
    QCOMPARE(m_paletteSelect->paletteId(), QStringLiteral("zen"));
    const NodeStyle::Palette *zen = NodeStyle::findPalette(QStringLiteral("zen"));
    QCOMPARE(m_model->root()->children.front()->branchColor, firstBranchColor(zen));
    QVERIFY(PalettePicker::favoriteIds().isEmpty());  // sceglierla non la rende preferita
    // La stella in alto a sinistra la aggiunge alle preferite (2026-10-19).
    int favorites = 0;
    runPalettePicker([&favorites](PalettePicker *picker) {
        QCOMPARE(picker->currentGroup(), 2);  // si apre sul gruppo della palette attiva
        QToolButton *zen = paletteTile(picker, QStringLiteral("Zen"));
        QTest::mouseClick(zen, Qt::LeftButton, {}, QPoint(10, 13));
        QApplication::processEvents();
        favorites = paletteTiles(picker->findChild<QStackedWidget *>()->widget(0)).size();
    });
    QCOMPARE(PalettePicker::favoriteIds(), QStringList{QStringLiteral("zen")});
    QCOMPARE(favorites, 1);
    QCOMPARE(m_model->paletteId, QStringLiteral("zen"));
    // Un solo Ctrl+Z torna alla palette di prima.
    flushHistory();
    action(QStringLiteral("Ctrl+Z"))->trigger();
    QApplication::processEvents();
    QCOMPARE(m_model->paletteId, QStringLiteral("alba"));
    QCOMPARE(m_model->activePalette, before);
    QCOMPARE(m_paletteSelect->paletteId(), QStringLiteral("alba"));
    Q_UNUSED(nodes);
}

void StylePageTest::escapeInPalettePickerChangesNothing() {
    const auto before = m_model->toFlatSnapshot();
    runPalettePicker([](PalettePicker *picker) {
        QTest::keyClick(picker, Qt::Key_Escape);
        QVERIFY(!PalettePicker::openPicker());
    });
    QCOMPARE(m_model->toFlatSnapshot(), before);
    // Invio applica la palette che ha il fuoco; Ctrl+Z la toglie.
    runPalettePicker([](PalettePicker *picker) {
        QToolButton *tile = paletteTile(picker, QStringLiteral("Mint"));
        tile->setFocus();
        QTest::keyClick(picker, Qt::Key_Return);
    });
    QCOMPARE(m_model->paletteId, QStringLiteral("menta"));
    flushHistory();
    action(QStringLiteral("Ctrl+Z"))->trigger();
    QApplication::processEvents();
    QCOMPARE(m_model->toFlatSnapshot(), before);
}

void StylePageTest::sheetsKeepTheirPalette() {
    runPalettePicker([](PalettePicker *picker) { paletteTile(picker, QStringLiteral("Ocean"))->click(); });
    flushHistory();
    QCOMPARE(m_model->paletteId, QStringLiteral("oceano"));
    QToolButton *addSheet = nullptr;
    for (auto *button : m_window->findChildren<QToolButton *>())
        if (button->toolTip() == QStringLiteral("Add sheet")) addSheet = button;
    QVERIFY(addSheet);
    // FEAT-026 fase D: "+ foglio" apre la galleria; si conferma la prima variante.
    QTimer::singleShot(0, [] { if (auto *gallery = qobject_cast<QDialog *>(QApplication::activeModalWidget())) gallery->accept(); });
    addSheet->click();
    flushHistory();
    // FEAT-036: il nuovo foglio prende il tema del foglio attivo; poi ognuno tiene il suo.
    QCOMPARE(m_model->paletteId, QStringLiteral("oceano"));
    runPalettePicker([](PalettePicker *picker) { paletteTile(picker, QStringLiteral("Dawn"))->click(); });
    flushHistory();
    QCOMPARE(m_model->paletteId, NodeStyle::kDefaultPaletteId);
    QCOMPARE(m_paletteSelect->paletteId(), NodeStyle::kDefaultPaletteId);
    auto *sheets = m_window->findChild<QComboBox *>(QStringLiteral("sheetSelect"));
    QVERIFY(sheets && sheets->count() == 2);
    emit sheets->activated(0);
    flushHistory();
    QCOMPARE(m_model->paletteId, QStringLiteral("oceano"));
    QCOMPARE(m_paletteSelect->paletteId(), QStringLiteral("oceano"));
    emit sheets->activated(1);
    flushHistory();
    QCOMPARE(m_model->paletteId, NodeStyle::kDefaultPaletteId);
    emit sheets->activated(0);
    flushHistory();
}

void StylePageTest::panelIsNeverNarrowerThanItsContent() {
    // BUG-010: stretto al minimo, il pannello mostra per intero la select della pagina Mappa.
    auto *dock = m_window->findChild<QDockWidget *>(QStringLiteral("InspectorDock"));
    auto *panel = m_window->findChild<SidePanel *>(QStringLiteral("InspectorSidePanel"));
    QVERIFY(dock && panel);
    m_scene->clearSelection();
    panel->setCurrentPage(2);  // Mappa
    m_window->resizeDocks({dock}, {50}, Qt::Horizontal);
    QApplication::processEvents();
    QVERIFY(dock->width() >= 256);  // panel floor (FEAT-072; was 280)
    // FEAT-072 B1: fixed width, the separator cannot widen it.
    const int width = dock->width();
    m_window->resizeDocks({dock}, {width + 200}, Qt::Horizontal);
    QApplication::processEvents();
    QCOMPARE(dock->width(), width);
    const QRect select(m_paletteSelect->mapTo(dock, QPoint(0, 0)), m_paletteSelect->size());
    QVERIFY2(dock->rect().contains(select), qPrintable(QStringLiteral("select %1..%2, pannello %3")
             .arg(select.left()).arg(select.right()).arg(dock->width())));
    // Un controllo largo aggiunto dopo l'apertura alza il minimo del pannello.
    QWidget *mapPage = panel->page(2);
    const int before = dock->minimumWidth();
    auto *wide = new QWidget(mapPage);
    wide->setMinimumWidth(before + 60);
    mapPage->layout()->addWidget(wide);
    wide->show();
    QApplication::processEvents();
    QVERIFY(dock->minimumWidth() >= before + 60);
    m_window->resizeDocks({dock}, {50}, Qt::Horizontal);
    QApplication::processEvents();
    QVERIFY(dock->width() >= before + 60);
    delete wide;
    QApplication::processEvents();
}

void StylePageTest::presetAppliesToSelectionInOneStep() {
    // FEAT-020: stile predefinito su due nodi di livelli diversi, un passo Undo/Redo.
    MindNode *main = m_model->addChild(m_model->root(), QStringLiteral("Principale"));
    MindNode *child = m_model->addChild(main, QStringLiteral("Figlio"));
    m_scene->autoLayout();
    select({main, child});
    QComboBox *preset = m_page->presetField();
    QVERIFY(preset->isEnabled());
    QCOMPARE(preset->count(), 4);  // Molto importante, Importante, Depenna, Predefinito
    QCOMPARE(preset->itemData(0, Qt::ToolTipRole).toString(), QStringLiteral("Very important"));
    QCOMPARE(preset->currentData().toInt(), static_cast<int>(NodePreset::Predefinito));
    QCOMPARE(preset->property("manual").toBool(), false);
    emit preset->activated(preset->findData(static_cast<int>(NodePreset::Depenna)));
    QApplication::processEvents();
    for (MindNode *node : {main, child}) {
        QCOMPARE(node->preset, NodePreset::Depenna);
        QVERIFY(node->strikeOut);
        QCOMPARE(node->border, node->branchColor);
    }
    QCOMPARE(preset->currentData().toInt(), static_cast<int>(NodePreset::Depenna));
    QCOMPARE(preset->property("manual").toBool(), true);
    flushHistory();
    action(QStringLiteral("Ctrl+Z"))->trigger();
    QApplication::processEvents();
    const MindNode *restoredMain = m_model->root()->children.back().get();
    QCOMPARE(restoredMain->preset, NodePreset::Predefinito);
    QVERIFY(!restoredMain->children.front()->strikeOut);
    QVERIFY(!NodeStyle::hasManualValue(restoredMain, StyleProperty::Preset));
}

void StylePageTest::fillAndBorderTypesShowMixedAndApply() {
    MindNode *main = m_model->addChild(m_model->root(), QStringLiteral("Principale"));
    MindNode *third = m_model->addChild(m_model->addChild(main, QStringLiteral("Secondo")), QStringLiteral("Terzo"));
    m_scene->autoLayout();
    select({main, third});
    QCOMPARE(m_page->fillStyleField()->currentIndex(), -1);  // pieno e nessuno
    QCOMPARE(m_page->borderStyleField()->currentData().toInt(), static_cast<int>(NodeBorderStyle::Nessuno));
    QComboBox *border = m_page->borderStyleField();
    emit border->activated(border->findData(static_cast<int>(NodeBorderStyle::Tratteggiato)));
    QApplication::processEvents();
    for (MindNode *node : {main, third}) {
        QCOMPARE(node->borderStyle, NodeBorderStyle::Tratteggiato);
        QCOMPARE(node->border, node->branchColor);
    }
    QComboBox *fill = m_page->fillStyleField();
    emit fill->activated(fill->findData(static_cast<int>(NodeFillStyle::Nessuno)));
    QApplication::processEvents();
    QCOMPARE(main->fill.alpha(), 0);
    QCOMPARE(fill->currentData().toInt(), static_cast<int>(NodeFillStyle::Nessuno));
    QCOMPARE(fill->itemData(fill->currentIndex(), Qt::ToolTipRole).toString(), QStringLiteral("No fill"));
}

void StylePageTest::borderWidthField() {
    const QList<MindNode *> nodes = addSelectedNodes(2);
    QLineEdit *width = m_page->borderWidthField();
    QCOMPARE(width->text(), QStringLiteral("2"));
    QCOMPARE(width->property("manual").toBool(), false);
    width->setFocus();
    width->setText(QStringLiteral("5"));
    QTest::keyClick(width, Qt::Key_Return);
    QApplication::processEvents();
    for (MindNode *node : nodes) QCOMPARE(node->borderWidth, 5.0);
    QCOMPARE(width->text(), QStringLiteral("5"));
    QTest::keyClick(width, Qt::Key_Up);
    QTest::keyClick(width, Qt::Key_Up);  // massimo 6
    QApplication::processEvents();
    for (MindNode *node : nodes) QCOMPARE(node->borderWidth, 6.0);
    width->setText(QStringLiteral("9"));
    QTest::keyClick(width, Qt::Key_Return);  // oltre il massimo: il massimo
    QApplication::processEvents();
    QCOMPARE(width->text(), QStringLiteral("6"));
    for (MindNode *node : nodes) QCOMPARE(node->borderWidth, 6.0);
}

void StylePageTest::shapeAppliesAndRelaysTheMap() {
    // FEAT-020 parte B: forma su piu' nodi in un passo; la mappa si ridispone perche' il nodo
    // cresce; Ctrl+Z riporta forma e posizioni.
    const QList<MindNode *> nodes = addNodes(3);
    select({nodes[0], nodes[1]});
    const qreal gapBefore = nodes[2]->position.y() - nodes[1]->position.y();
    QComboBox *shape = m_page->shapeField();
    QCOMPARE(shape->currentData().toInt(), static_cast<int>(NodeShape::RettangoloArrotondato));
    QCOMPARE(shape->count(), static_cast<int>(NodeShape::TrapezioRovesciato) + 1);
    emit shape->activated(shape->findData(static_cast<int>(NodeShape::Cerchio)));
    QApplication::processEvents();
    QCOMPARE(nodes[0]->shape, NodeShape::Cerchio);
    QCOMPARE(nodes[1]->shape, NodeShape::Cerchio);
    QCOMPARE(nodes[2]->shape, NodeShape::RettangoloArrotondato);
    QVERIFY(nodes[2]->position.y() - nodes[1]->position.y() > gapBefore);  // il cerchio e' piu' alto
    select({nodes[1], nodes[2]});
    QCOMPARE(shape->currentIndex(), -1);  // forme diverse: "—"
    flushHistory();
    action(QStringLiteral("Ctrl+Z"))->trigger();
    QApplication::processEvents();
    const auto &children = m_model->root()->children;
    const MindNode *first = children[children.size() - 3].get();
    const MindNode *second = children[children.size() - 2].get();
    const MindNode *third = children[children.size() - 1].get();
    QCOMPARE(first->shape, NodeShape::RettangoloArrotondato);
    QCOMPARE(third->position.y() - second->position.y(), gapBefore);
}

void StylePageTest::branchBoundsContainTheStroke() {
    // Il riquadro dei rami si ricava dai punti di controllo della curva (MindMapScene, EdgeItem) e
    // non dal contorno del tratto: deve comunque contenere tutto il tratto disegnato, anche con lo
    // spessore massimo e con i rami che salgono e scendono.
    MindNode *parent = addNodes(1).front();
    for (int i = 0; i < 4; ++i) m_model->addChild(parent, QStringLiteral("Figlio %1").arg(i));
    QVERIFY(m_model->setStyle({parent}, StyleProperty::BranchWidth, NodeStyle::kMaxBranchWidthPx));
    m_scene->autoLayout();
    QApplication::processEvents();
    int checked = 0;
    for (const QGraphicsItem *item : m_scene->items()) {
        const auto *edge = qgraphicsitem_cast<const QGraphicsPathItem *>(item);
        if (!edge) continue;
        QVERIFY(edge->boundingRect().contains(edge->shape().boundingRect()));
        if (edge->pen().widthF() == NodeStyle::kMaxBranchWidthPx) ++checked;
    }
    QCOMPARE(checked, 4);
}

void StylePageTest::doubleUnderlineFollowsBorderWidth() {
    // BUG-012: con "Doppia linea sotto" la seconda linea scende di uno spessore del bordo. Da 1 a
    // 2 px il nodo si allunga e la mappa si ridispone (prima la misura non veniva aggiornata).
    const QList<MindNode *> nodes = addNodes(2);
    QVERIFY(m_model->setStyle({nodes[0]}, StyleProperty::Shape, static_cast<int>(NodeShape::DoppiaLineaSotto)));
    QVERIFY(m_model->setStyle({nodes[0]}, StyleProperty::BorderWidth, 1.0));
    QApplication::processEvents();
    const qreal gapBefore = nodes[1]->position.y() - nodes[0]->position.y();
    QVERIFY(m_model->setStyle({nodes[0]}, StyleProperty::BorderWidth, 2.0));
    QApplication::processEvents();
    QVERIFY(nodes[1]->position.y() - nodes[0]->position.y() > gapBefore);
}

QWidget *StylePageTest::openGridMenu(QComboBox *select) {
    select->showPopup();
    QApplication::processEvents();
    QWidget *popup = QApplication::activePopupWidget();
    return popup && popup->objectName() == QStringLiteral("GridMenuPopup") ? popup : nullptr;
}

QList<QToolButton *> StylePageTest::gridTiles(QWidget *popup) {
    return popup->findChildren<QToolButton *>();  // inside the popup's scroll area
}

void StylePageTest::gridMenuScrollsWhenTallerThanScreen() {
    // FEAT-080 (user, 2026-11-01): a grid menu taller than the screen stays on it and scrolls.
    QComboBox select;
    for (int i = 0; i < 60; ++i) addIconSelectItem(&select, i, QString::number(i), [](QPainter &, const QRectF &) {});
    const QRect area = QGuiApplication::primaryScreen()->availableGeometry();
    select.setGeometry(area.center().x() - 50, area.center().y(), 100, 28);
    select.show();
    openIconGridMenu(&select, 2, {Qt::white, Qt::gray, Qt::black, Qt::lightGray});
    QApplication::processEvents();
    QWidget *popup = QApplication::activePopupWidget();
    QVERIFY(popup && popup->objectName() == QStringLiteral("GridMenuPopup"));
    QVERIFY2(area.contains(popup->geometry()), "the menu goes past the screen");
    auto *scroll = popup->findChild<QScrollArea *>();
    QVERIFY(scroll && scroll->verticalScrollBar()->isVisible());
    QCOMPARE(scroll->verticalScrollBar()->value(), 0);
    // Keyboard: the focused tile scrolls into view.
    for (int i = 0; i < 29; ++i) QTest::keyClick(popup, Qt::Key_Down);
    QVERIFY(scroll->verticalScrollBar()->value() > 0);
    QCOMPARE(scroll->verticalScrollBar()->value(), scroll->verticalScrollBar()->maximum());
    popup->close();
    // A short menu keeps its size, with no bar.
    QComboBox small;
    for (int i = 0; i < 4; ++i) addIconSelectItem(&small, i, QString::number(i), [](QPainter &, const QRectF &) {});
    small.setGeometry(area.center().x() - 50, area.top() + 20, 100, 28);
    small.show();
    openIconGridMenu(&small, 2, {Qt::white, Qt::gray, Qt::black, Qt::lightGray});
    QApplication::processEvents();
    popup = QApplication::activePopupWidget();
    QVERIFY(popup);
    QVERIFY(!popup->findChild<QScrollArea *>()->verticalScrollBar()->isVisible());
    popup->close();
}

void StylePageTest::gridMenusShowColumnsAndNone() {
    // FEAT-038: riempimento e bordo a due colonne con "Nessuno" largo in fondo; forma a tre
    // colonne con il titolo "Base" e senza bottone largo.
    const QList<MindNode *> nodes = addSelectedNodes(1);
    Q_UNUSED(nodes);
    struct Expected { QComboBox *select; int columns; int tiles; QString none; };
    for (const Expected &expected : {Expected{m_page->fillStyleField(), 2, 6, QStringLiteral("No fill")},
                                     Expected{m_page->borderStyleField(), 2, 4, QStringLiteral("No border")},
                                     Expected{m_page->shapeField(), 3, 47, QString()}}) {
        QWidget *popup = openGridMenu(expected.select);
        QVERIFY(popup);
        const QList<QToolButton *> tiles = gridTiles(popup);
        QCOMPARE(tiles.size(), expected.tiles + (expected.none.isEmpty() ? 0 : 1));
        QSet<int> columns;
        int iconWidth = 0;
        QToolButton *none = nullptr;
        for (QToolButton *tile : tiles) {
            if (tile->toolTip() == expected.none) { none = tile; continue; }
            columns.insert(tile->x());
            iconWidth = tile->width();
        }
        QCOMPARE(columns.size(), expected.columns);
        if (!expected.none.isEmpty()) {
            QVERIFY(none);
            QVERIFY(none->width() > expected.columns * iconWidth);  // largo quanto la griglia
            for (QToolButton *tile : tiles) QVERIFY(tile == none || tile->y() < none->y());  // in fondo
        }
        const QList<QLabel *> titles = popup->findChildren<QLabel *>();
        QCOMPARE(titles.size(), expected.none.isEmpty() ? 4 : 0);
        if (!titles.isEmpty()) {
            // FEAT-030: Basic (12), Advanced from the heart, Punctual from the angle brackets
            // with the square brackets third; each group starts a new row in the first column.
            QCOMPARE(titles[0]->text(), QStringLiteral("Basic"));
            QCOMPARE(titles[1]->text(), QStringLiteral("Advanced"));
            QCOMPARE(titles[2]->text(), QStringLiteral("Punctual"));
            QCOMPARE(titles[3]->text(), QStringLiteral("Flowchart"));
            for (const auto &[first, title] : {std::pair{12, titles[1]}, std::pair{25, titles[2]}, std::pair{32, titles[3]}}) {
                QCOMPARE(tiles[first]->x(), tiles[0]->x());
                QVERIFY(tiles[first]->y() > title->y() && title->y() > tiles[first - 1]->y());
            }
            QCOMPARE(tiles[12]->toolTip(), QStringLiteral("Heart"));
            QCOMPARE(tiles[25]->toolTip(), QStringLiteral("Angle brackets"));
            QCOMPARE(tiles[27]->toolTip(), QStringLiteral("Square brackets"));
            QCOMPARE(tiles[32]->toolTip(), QStringLiteral("Decision"));
        }
        QTest::keyClick(popup, Qt::Key_Escape);
        QApplication::processEvents();
        QVERIFY(!QApplication::activePopupWidget());
    }
}

void StylePageTest::gridMenuChoosesWithClickKeysAndEsc() {
    const QList<MindNode *> nodes = addSelectedNodes(2);
    QComboBox *fill = m_page->fillStyleField();
    // Clic su un'icona: stile su tutti i nodi selezionati, menu chiuso.
    QWidget *popup = openGridMenu(fill);
    QVERIFY(popup);
    QToolButton *brush = nullptr;
    for (QToolButton *tile : gridTiles(popup)) if (tile->toolTip() == QStringLiteral("Diagonal strokes")) brush = tile;
    QVERIFY(brush);
    QTest::mouseClick(brush, Qt::LeftButton);
    QApplication::processEvents();
    QVERIFY(!QApplication::activePopupWidget());
    for (MindNode *node : nodes) QCOMPARE(node->fillStyle, NodeFillStyle::PennellateDiagonali);
    // Esc: nessuna modifica.
    const auto before = m_model->toFlatSnapshot();
    popup = openGridMenu(fill);
    QVERIFY(popup);
    QTest::keyClick(popup, Qt::Key_Escape);
    QApplication::processEvents();
    QCOMPARE(m_model->toFlatSnapshot(), before);
    // Tastiera: il fuoco parte dalla voce attuale; destra e Invio scelgono la successiva, giu'
    // dall'ultima riga arriva a "Nessun riempimento".
    popup = openGridMenu(fill);
    QVERIFY(popup);
    QTest::keyClick(popup, Qt::Key_Right);
    QTest::keyClick(popup, Qt::Key_Return);
    QApplication::processEvents();
    for (MindNode *node : nodes) QCOMPARE(node->fillStyle, NodeFillStyle::TratteggioDiagonale);
    popup = openGridMenu(fill);
    QVERIFY(popup);
    for (int i = 0; i < 3; ++i) QTest::keyClick(popup, Qt::Key_Down);
    QTest::keyClick(popup, Qt::Key_Return);
    QApplication::processEvents();
    for (MindNode *node : nodes) QCOMPARE(node->fillStyle, NodeFillStyle::Nessuno);
}

void StylePageTest::textSectionAppliesToAllSelectedNodes() {
    // FEAT-022: dimensione scritta e grassetto su due nodi, un passo di storico ciascuno; il nodo
    // si allarga e la mappa si ridispone; valori diversi = "—".
    const QList<MindNode *> nodes = addSelectedNodes(2);
    NodeItem *item = nullptr;
    for (auto *candidate : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(candidate); nodeItem && nodeItem->node() == nodes[0]) item = nodeItem;
    QVERIFY(item);
    const qreal widthBefore = item->anchorHalfWidth();
    const qreal gapBefore = nodes[1]->position.y() - nodes[0]->position.y();
    QComboBox *size = m_page->fontSizeField();
    QCOMPARE(size->currentText(), QStringLiteral("18"));  // main topic: 18 px, shown in px
    size->lineEdit()->setFocus();
    size->setEditText(QStringLiteral("24"));
    emit size->lineEdit()->editingFinished();
    QApplication::processEvents();
    for (MindNode *node : nodes) QCOMPARE(node->fontSize, 18.0);  // 24 px = 18 pt
    QVERIFY(item->anchorHalfWidth() > widthBefore);
    QVERIFY(nodes[1]->position.y() - nodes[0]->position.y() > gapBefore);
    // Fuori intervallo: nessuna modifica, il campo torna a 24.
    size->setEditText(QStringLiteral("200"));
    emit size->lineEdit()->editingFinished();
    QApplication::processEvents();
    QCOMPARE(size->currentText(), QStringLiteral("24"));
    for (MindNode *node : nodes) QCOMPARE(node->fontSize, 18.0);
    flushHistory();
    QVERIFY(!m_page->boldButton()->isChecked());
    m_page->boldButton()->click();
    QApplication::processEvents();
    for (MindNode *node : nodes) QCOMPARE(node->fontWeight, 700);
    QVERIFY(m_page->boldButton()->isChecked());
    m_page->alignButton(TextAlign::Centro)->click();
    QApplication::processEvents();
    for (MindNode *node : nodes) QCOMPARE(node->textAlign, TextAlign::Centro);
    QVERIFY(m_page->alignButton(TextAlign::Centro)->isChecked() && !m_page->alignButton(TextAlign::Sinistra)->isChecked());
    // Ctrl+Z annulla solo l'allineamento, poi solo il grassetto.
    flushHistory();
    action(QStringLiteral("Ctrl+Z"))->trigger();
    QApplication::processEvents();
    const auto &children = m_model->root()->children;
    const MindNode *first = children[children.size() - 2].get();
    QCOMPARE(first->textAlign, TextAlign::Sinistra);
    QCOMPARE(first->fontWeight, 700);
    // Mixed values (18 pt and the central topic 22.5 pt): "—".
    select({children[children.size() - 2].get(), m_model->root()});
    QVERIFY(m_page->fontSizeField()->currentText().isEmpty());
    QVERIFY(!m_page->boldButton()->isChecked());  // 700 and the central's 800: mixed, off
}

void StylePageTest::textButtonsSitInTheMiddleOfTheirBar() {
    // Segnalazione dell'utente (foto 2026-10-16): i bottoni toccavano il lato basso del riquadro.
    addSelectedNodes(1);
    for (QToolButton *button : {m_page->boldButton(), m_page->alignButton(TextAlign::Destra)}) {
        const QWidget *bar = button->parentWidget();
        const QRect inside = bar->contentsRect();
        const QRect geometry = button->geometry();
        QVERIFY(inside.contains(geometry));
        const int above = geometry.top() - inside.top();
        const int below = inside.bottom() - geometry.bottom();
        QCOMPARE(above, below);
        QVERIFY(above >= 1);
    }
}

std::pair<int, int> StylePageTest::textMargins(MindNode *node) {
    NodeItem *item = nullptr;
    for (auto *candidate : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(candidate); nodeItem && nodeItem->node() == node) item = nodeItem;
    if (!item) return {-1, -1};
    const QRectF box = item->mapRectToScene(item->shapeRect()).adjusted(2, 2, -2, -2);  // dentro il bordo arrotondato
    QImage image(box.size().toSize(), QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    m_scene->render(&painter, QRectF(QPointF(0, 0), box.size()), box);
    painter.end();
    // Colonne con pixel vicini al colore del testo (il riempimento e' chiaro e diverso).
    const QColor text = node->text;
    int first = -1, last = -1;
    for (int x = 0; x < image.width(); ++x)
        for (int y = 0; y < image.height(); ++y) {
            const QColor pixel = image.pixelColor(x, y);
            if (qAbs(pixel.red() - text.red()) + qAbs(pixel.green() - text.green()) + qAbs(pixel.blue() - text.blue()) < 120) {
                if (first < 0) first = x;
                last = x;
                break;
            }
        }
    if (first < 0) return {-1, -1};
    return {first + 2, image.width() - 1 - last + 2};
}

void StylePageTest::alignmentPlacesTextInsideTheNode() {
    // Segnalazione dell'utente: allargando il nodo l'allineamento non cambiava nulla. Nodo largo
    // 300 px a mano con un titolo corto: il testo deve stare a sinistra, al centro o a destra.
    MindNode *node = addNodes(1).front();
    node->title = QStringLiteral("Ciao");
    node->customWidth = 300;
    QVERIFY(m_model->setStyle({node}, StyleProperty::FillColor, QColor("#fff4cf")));
    QVERIFY(m_model->setStyle({node}, StyleProperty::TextColor, QColor("#172033")));
    m_scene->autoLayout();
    QApplication::processEvents();
    const auto [leftOnLeft, rightOnLeft] = textMargins(node);
    // Padding dei rami principali (20 px), con 2 px di tolleranza per il disegno delle lettere.
    QVERIFY2(leftOnLeft >= 18 && leftOnLeft <= 22, qPrintable(QString::number(leftOnLeft)));
    QVERIFY(rightOnLeft > 200);
    QVERIFY(m_model->setStyle({node}, StyleProperty::TextAlign, static_cast<int>(TextAlign::Centro)));
    QApplication::processEvents();
    const auto [leftOnCentre, rightOnCentre] = textMargins(node);
    QVERIFY2(qAbs(leftOnCentre - rightOnCentre) <= 3, qPrintable(QStringLiteral("%1 %2").arg(leftOnCentre).arg(rightOnCentre)));
    QVERIFY(m_model->setStyle({node}, StyleProperty::TextAlign, static_cast<int>(TextAlign::Destra)));
    QApplication::processEvents();
    const auto [leftOnRight, rightOnRight] = textMargins(node);
    QVERIFY2(rightOnRight >= 18 && rightOnRight <= 22, qPrintable(QString::number(rightOnRight)));
    QVERIFY(leftOnRight > 200);
    // Giustificato su piu' righe: la prima riga arriva fino al margine destro.
    node->title = QStringLiteral("Giustificato con un testo abbastanza lungo da andare a capo almeno una volta");
    m_scene->updateNode(node);
    QVERIFY(m_model->setStyle({node}, StyleProperty::TextAlign, static_cast<int>(TextAlign::Sinistra)));
    QApplication::processEvents();
    const int rightLeftAligned = textMargins(node).second;
    QVERIFY(m_model->setStyle({node}, StyleProperty::TextAlign, static_cast<int>(TextAlign::Giustificato)));
    QApplication::processEvents();
    const auto [leftJustified, rightJustified] = textMargins(node);
    QVERIFY2(rightJustified >= 18 && rightJustified <= 23, qPrintable(QStringLiteral("%1 (a sinistra %2)").arg(rightJustified).arg(rightLeftAligned)));
    QVERIFY2(leftJustified >= 18 && leftJustified <= 22, qPrintable(QString::number(leftJustified)));
}

void StylePageTest::widthFieldTypesArrowsAndFit() {
    // FEAT-021: numero grigio (automatica) o chiaro (a mano), valori scritti fra minimo e 3000 px,
    // interi, frecce e Maiusc+frecce, Esc, "Adatta"; un passo di storico per conferma.
    const QList<MindNode *> nodes = addSelectedNodes(1);
    MindNode *node = nodes.front();
    QLineEdit *width = m_page->widthField();
    QToolButton *fit = m_page->fitButton();
    const qreal automatic = std::round(m_scene->nodeWidth(node));
    QVERIFY(automatic > 0);
    QCOMPARE(width->text(), QString::number(automatic));
    QCOMPARE(width->property("manual").toBool(), false);
    QVERIFY(!fit->isEnabled());  // gia' automatica
    width->setFocus();
    QTest::keyClick(width, Qt::Key_Return);  // Invio senza scrivere: resta automatica
    QApplication::processEvents();
    QCOMPARE(node->customWidth, 0.0);

    auto type = [&](const QString &text) {
        width->setText(text);
        QTest::keyClick(width, Qt::Key_Return);
        QApplication::processEvents();
    };
    type(QStringLiteral("300"));
    QCOMPARE(node->customWidth, 300.0);
    QCOMPARE(std::round(m_scene->nodeWidth(node)), 300.0);
    QCOMPARE(width->text(), QStringLiteral("300"));
    QCOMPARE(width->property("manual").toBool(), true);
    QVERIFY(fit->isEnabled());
    flushHistory();
    action(QStringLiteral("Ctrl+Z"))->trigger();  // un passo: torna automatica e grigia
    QApplication::processEvents();
    QCOMPARE(node->customWidth, 0.0);
    QCOMPARE(width->text(), QString::number(automatic));
    QCOMPARE(width->property("manual").toBool(), false);
    action(QStringLiteral("Ctrl+Y"))->trigger();
    QApplication::processEvents();
    QCOMPARE(node->customWidth, 300.0);
    QCOMPARE(width->text(), QStringLiteral("300"));

    type(QStringLiteral("250,6"));  // intero
    QCOMPARE(node->customWidth, 251.0);
    const qreal minimum = m_scene->manualWidthFor(node, 0);
    QVERIFY(minimum > 0 && minimum < automatic);
    type(QStringLiteral("10"));  // sotto il minimo: il minimo, a mano
    QCOMPARE(node->customWidth, minimum);
    QCOMPARE(width->property("manual").toBool(), true);
    type(QStringLiteral("5000"));  // oltre il massimo: 3000
    QCOMPARE(node->customWidth, kMaxManualNodeWidthPx);
    type(QString());  // vuoto o non valido: torna al valore di prima, nessuna modifica
    QCOMPARE(node->customWidth, kMaxManualNodeWidthPx);
    QCOMPARE(width->text(), QStringLiteral("3000"));

    type(QStringLiteral("200"));
    QTest::keyClick(width, Qt::Key_Up);
    QApplication::processEvents();
    QCOMPARE(node->customWidth, 201.0);
    QTest::keyClick(width, Qt::Key_Down, Qt::ShiftModifier);
    QApplication::processEvents();
    QCOMPARE(node->customWidth, 191.0);
    QCOMPARE(width->text(), QStringLiteral("191"));
    width->setText(QStringLiteral("400"));
    QTest::keyClick(width, Qt::Key_Escape);  // Esc: rimette il valore senza scegliere
    QApplication::processEvents();
    QCOMPARE(width->text(), QStringLiteral("191"));
    QCOMPARE(node->customWidth, 191.0);

    fit->click();  // "Adatta": automatica, grigia, bottone disattivato
    QApplication::processEvents();
    QCOMPARE(node->customWidth, 0.0);
    QCOMPARE(width->text(), QString::number(automatic));
    QCOMPARE(width->property("manual").toBool(), false);
    QVERIFY(!fit->isEnabled());

    // Maiusc+frecce anche negli altri campi in px, entro il loro intervallo (spessore 1-6).
    QLineEdit *border = m_page->borderWidthField();
    border->setFocus();
    QTest::keyClick(border, Qt::Key_Up, Qt::ShiftModifier);
    QApplication::processEvents();
    QCOMPARE(node->borderWidth, NodeStyle::kMaxBorderWidthPx);
}

// docs/design/ui-foundations.md, "Controlli disattivati": a row whose fields are all disabled
// shows its label disabled too, so the whole row reads as unavailable.
void StylePageTest::disabledRowsDimTheirLabel() {
    Document document;
    document.startUntitled();
    MindNode *a = document.addChild(nullptr, QStringLiteral("A"));
    StylePage page;
    const auto label = [&page](const QString &text) -> QLabel * {
        for (QLabel *candidate : page.findChildren<QLabel *>())
            if (candidate->text() == text) return candidate;
        return nullptr;
    };
    QLabel *fill = label(QStringLiteral("Fill"));
    QVERIFY(fill);
    page.setNodes({}, {});
    QVERIFY(!fill->isEnabled());
    page.setNodes({a}, {});
    QVERIFY(fill->isEnabled());
}

void StylePageTest::widthArrowsChangeEachNodeWhenMixed() {
    // FEAT-021: larghezze diverse = "—"; le frecce cambiano ogni nodo dalla sua larghezza, entro il
    // suo minimo; un numero scritto vale per tutti; "Adatta" li rende tutti automatici. Ogni
    // conferma e' un solo passo di storico.
    const QList<MindNode *> nodes = addNodes(3);
    QVERIFY(m_model->setTitle(m_model->indexFromNode(nodes[2]), QStringLiteral("Un titolo piuttosto lungo"), QString()));
    QVERIFY(m_model->setCustomWidths({{nodes[0], 120}, {nodes[1], 200}}));
    QApplication::processEvents();
    select(nodes);
    QLineEdit *width = m_page->widthField();
    QCOMPARE(width->text(), QString());  // valori diversi: segnaposto "—"
    QCOMPARE(width->placeholderText(), QStringLiteral("—"));
    QVERIFY(m_page->fitButton()->isEnabled());  // almeno uno a mano
    const qreal third = std::round(m_scene->nodeWidth(nodes[2]));
    width->setFocus();
    QTest::keyClick(width, Qt::Key_Up);
    QApplication::processEvents();
    QCOMPARE(nodes[0]->customWidth, 121.0);
    QCOMPARE(nodes[1]->customWidth, 201.0);
    QCOMPARE(nodes[2]->customWidth, third + 1);  // l'automatico diventa manuale dalla sua misura
    QCOMPARE(width->text(), QString());
    flushHistory();
    action(QStringLiteral("Ctrl+Z"))->trigger();  // un solo passo per tutti
    QApplication::processEvents();
    QCOMPARE(nodes[0]->customWidth, 120.0);
    QCOMPARE(nodes[1]->customWidth, 200.0);
    QCOMPARE(nodes[2]->customWidth, 0.0);

    // Un numero sotto i minimi: ognuno si ferma al proprio.
    width->setText(QStringLiteral("1"));
    QTest::keyClick(width, Qt::Key_Return);
    QApplication::processEvents();
    for (MindNode *node : nodes) QCOMPARE(node->customWidth, m_scene->manualWidthFor(node, 0));
    m_page->fitButton()->click();
    QApplication::processEvents();
    for (MindNode *node : nodes) QCOMPARE(node->customWidth, 0.0);
    QVERIFY(!m_page->fitButton()->isEnabled());
}

void StylePageTest::applyingToThousandNodes() {
    MENTIS_SLOW_TEST();
    // Misura per FEAT-019 (obiettivo sotto 100 ms nel profilo Standard, riportato nella scheda).
    Performance::setMode(Performance::Mode::Standard);
    const QList<MindNode *> nodes = addSelectedNodes(1000);
    QElapsedTimer timer;
    timer.start();
    emit m_page->styleChosen(StyleProperty::FillColor, QColor("#4051b5"));
    QApplication::processEvents();
    const qint64 elapsedMs = timer.elapsed();
    Performance::setMode(Performance::Mode::Light);
    for (MindNode *node : nodes) QCOMPARE(node->fill, QColor("#4051b5"));
    qInfo("FEAT-019 colore applicato a 1000 nodi selezionati: %lld ms", elapsedMs);
}

void StylePageTest::changingPaletteOnThousandNodes() {
    MENTIS_SLOW_TEST();
    // Misura per FEAT-031 (obiettivo sotto 100 ms nel profilo Standard, riportato nella scheda): i
    // 1000 nodi della prova precedente piu' quelli gia' presenti.
    Performance::setMode(Performance::Mode::Standard);
    m_scene->clearSelection();
    flushHistory();
    const NodeStyle::Palette *rose = NodeStyle::findPalette(QStringLiteral("rose"));
    QElapsedTimer timer;
    timer.start();
    emit m_paletteSelect->paletteChosen(rose->id);
    QApplication::processEvents();
    const qint64 elapsedMs = timer.elapsed();
    Performance::setMode(Performance::Mode::Light);
    QCOMPARE(m_model->paletteId, rose->id);
    QCOMPARE(m_model->root()->children.front()->branchColor, firstBranchColor(rose));
    qInfo("FEAT-031 cambio palette con %d nodi, profilo Standard: %lld ms", static_cast<int>(m_model->root()->children.size()), elapsedMs);
    flushHistory();
    timer.restart();
    emit m_paletteSelect->paletteChosen(QStringLiteral("oceano"));
    QApplication::processEvents();
    qInfo("FEAT-031 cambio palette, profilo Leggero: %lld ms", timer.elapsed());
    QCOMPARE(m_model->paletteId, QStringLiteral("oceano"));
}

void StylePageTest::presetOnThousandNodes() {
    MENTIS_SLOW_TEST();
    // Misura per FEAT-020 (obiettivo sotto 100 ms nel profilo Standard): stile predefinito sui
    // nodi selezionati dalla prova applyingToThousandNodes (gli ultimi 1000 rami).
    QList<MindNode *> nodes;
    const auto &children = m_model->root()->children;
    for (size_t i = children.size() - 1000; i < children.size(); ++i) nodes.append(children[i].get());
    select(nodes);
    for (const auto mode : {Performance::Mode::Standard, Performance::Mode::Light}) {
        Performance::setMode(mode);
        const NodePreset preset = mode == Performance::Mode::Standard ? NodePreset::Depenna : NodePreset::MoltoImportante;
        QElapsedTimer timer;
        timer.start();
        emit m_page->styleChosen(StyleProperty::Preset, static_cast<int>(preset));
        QApplication::processEvents();
        const qint64 elapsedMs = timer.elapsed();
        for (MindNode *node : nodes) QCOMPARE(node->preset, preset);
        qInfo("FEAT-020 stile predefinito su 1000 nodi, profilo %s: %lld ms",
              mode == Performance::Mode::Standard ? "Standard" : "Leggero", elapsedMs);
    }
    Performance::setMode(Performance::Mode::Light);
}

void StylePageTest::shapeOnThousandNodes() {
    MENTIS_SLOW_TEST();
    // Misura per FEAT-020 parte B: forma su 1000 nodi selezionati, con la mappa che si ridispone.
    const QList<MindNode *> nodes = lastThousandNodes();
    select(nodes);
    for (const auto mode : {Performance::Mode::Standard, Performance::Mode::Light}) {
        Performance::setMode(mode);
        const NodeShape shape = mode == Performance::Mode::Standard ? NodeShape::Ellisse : NodeShape::Esagono;
        QElapsedTimer timer;
        timer.start();
        emit m_page->styleChosen(StyleProperty::Shape, static_cast<int>(shape));
        QApplication::processEvents();
        const qint64 elapsedMs = timer.elapsed();
        for (MindNode *node : nodes) QCOMPARE(node->shape, shape);
        qInfo("FEAT-020 forma su 1000 nodi con ridisposizione, profilo %s: %lld ms",
              mode == Performance::Mode::Standard ? "Standard" : "Leggero", elapsedMs);
    }
    Performance::setMode(Performance::Mode::Light);
}

QList<MindNode *> StylePageTest::lastThousandNodes() const {
    QList<MindNode *> nodes;
    const auto &children = m_model->root()->children;
    for (size_t i = children.size() - 1000; i < children.size(); ++i) nodes.append(children[i].get());
    return nodes;
}

double StylePageTest::drawingMs(const QList<MindNode *> &nodes) {
    QRectF area;
    for (QGraphicsItem *item : m_scene->items())
        if (auto *node = qgraphicsitem_cast<NodeItem *>(item); node && nodes.contains(node->node())) area |= node->sceneBoundingRect();
    QImage tile(1200, 900, QImage::Format_ARGB32_Premultiplied);
    // Miglior tempo su tre passate: la prima riempie le cache (anche i tracciati a mano libera).
    double best = 0;
    for (int pass = 0; pass < 3; ++pass) {
        QElapsedTimer timer;
        timer.start();
        for (qreal y = area.top(); y < area.bottom(); y += tile.height())
            for (qreal x = area.left(); x < area.right(); x += tile.width()) {
                tile.fill(Qt::white);
                QPainter painter(&tile);
                m_scene->render(&painter, QRectF(tile.rect()), QRectF(x, y, tile.width(), tile.height()));
            }
        const double ms = timer.nsecsElapsed() / 1e6;
        if (pass > 0 && (best == 0 || ms < best)) best = ms;
    }
    return best;
}

void StylePageTest::sketchOnThousandNodes() {
    MENTIS_SLOW_TEST();
    // Misure per FEAT-020 parte C: motivo e bordo a mano libera su 1000 nodi selezionati (il bordo
    // a mano libera allarga l'area dell'anello, quindi la mappa si ridispone) e disegno al 100%.
    const QList<MindNode *> nodes = lastThousandNodes();
    m_scene->clearSelection();
    m_model->clearStyle(nodes, StyleProperty::Shape);  // forma lasciata da shapeOnThousandNodes, se eseguita
    QApplication::processEvents();
    Performance::setMode(Performance::Mode::Standard);
    qInfo("FEAT-020 disegno di 1000 nodi con forma, riempimento e bordo predefiniti: %.1f ms", drawingMs(nodes));
    select(nodes);
    const QList<std::pair<StyleProperty, int>> changes{
        {StyleProperty::FillStyle, static_cast<int>(NodeFillStyle::PennellateDiagonali)},
        {StyleProperty::BorderStyle, static_cast<int>(NodeBorderStyle::ContinuoManoLibera)}};
    for (const auto mode : {Performance::Mode::Standard, Performance::Mode::Light}) {
        Performance::setMode(mode);
        for (const auto &[property, value] : changes) {
            const int chosen = mode == Performance::Mode::Standard ? value : value + 1;  // Leggero: il tipo successivo
            QElapsedTimer timer;
            timer.start();
            emit m_page->styleChosen(property, chosen);
            QApplication::processEvents();
            const qint64 elapsedMs = timer.elapsed();
            for (MindNode *node : nodes)
                QCOMPARE(property == StyleProperty::FillStyle ? static_cast<int>(node->fillStyle) : static_cast<int>(node->borderStyle), chosen);
            qInfo("FEAT-020 %s su 1000 nodi, profilo %s: %lld ms", property == StyleProperty::FillStyle ? "motivo" : "bordo a mano libera",
                  mode == Performance::Mode::Standard ? "Standard" : "Leggero", elapsedMs);
        }
    }
    m_scene->clearSelection();
    QApplication::processEvents();
    qInfo("FEAT-020 disegno di 1000 nodi con tratteggio diagonale e bordo tratteggiato a mano libera: %.1f ms", drawingMs(nodes));
    Performance::setMode(Performance::Mode::Light);
}

void StylePageTest::fontSizeOnThousandNodes() {
    MENTIS_SLOW_TEST();
    // Misura per FEAT-022: dimensione del testo su 1000 nodi selezionati, con la mappa che si
    // ridispone (obiettivo sotto 100 ms nel profilo Standard).
    const QList<MindNode *> nodes = lastThousandNodes();
    select(nodes);
    for (const auto mode : {Performance::Mode::Standard, Performance::Mode::Light}) {
        Performance::setMode(mode);
        const int size = mode == Performance::Mode::Standard ? 14 : 18;
        QElapsedTimer timer;
        timer.start();
        emit m_page->styleChosen(StyleProperty::FontSize, size);
        QApplication::processEvents();
        const qint64 elapsedMs = timer.elapsed();
        for (MindNode *node : nodes) QCOMPARE(node->fontSize, qreal(size));
        qInfo("FEAT-022 dimensione del testo su 1000 nodi, profilo %s: %lld ms",
              mode == Performance::Mode::Standard ? "Standard" : "Leggero", elapsedMs);
    }
    Performance::setMode(Performance::Mode::Light);
}

void StylePageTest::widthOnThousandNodes() {
    MENTIS_SLOW_TEST();
    // Misura per FEAT-021: larghezza scritta su 1000 nodi selezionati, con la mappa che si
    // ridispone (obiettivo sotto 100 ms nel profilo Standard).
    const QList<MindNode *> nodes = lastThousandNodes();
    select(nodes);
    for (const auto mode : {Performance::Mode::Standard, Performance::Mode::Light}) {
        Performance::setMode(mode);
        const int px = mode == Performance::Mode::Standard ? 260 : 300;
        QElapsedTimer timer;
        timer.start();
        emit m_page->widthChosen(px);
        QApplication::processEvents();
        const qint64 elapsedMs = timer.elapsed();
        for (MindNode *node : nodes) QCOMPARE(node->customWidth, qreal(px));
        qInfo("FEAT-021 larghezza su 1000 nodi, profilo %s: %lld ms",
              mode == Performance::Mode::Standard ? "Standard" : "Leggero", elapsedMs);
    }
    Performance::setMode(Performance::Mode::Light);
}

// FEAT-072: default font shown by name (Atkinson Hyperlegible); bottom button: Apply to related on a main topic
// (its siblings get its style, one step; subtopics of another parent are left alone), Reset style on the central topic.
void StylePageTest::styleCardFontAndApplyToRelated() {
    const QList<MindNode *> nodes = addNodes(3);
    QCOMPARE(m_page->fontFamilyField()->itemText(0), QStringLiteral("Atkinson Hyperlegible"));
    // The bundled fonts are offered in the list (FEAT-069).
    for (const char *family : {"Atkinson Hyperlegible Next", "Atkinson Hyperlegible Mono", "OpenDyslexic", "Roboto"})
        QVERIFY2(m_page->fontFamilyField()->findData(QString::fromLatin1(family)) >= 0, family);
    QVERIFY(m_model->setStyle({nodes[0]}, StyleProperty::FillColor, QColor(Qt::red)));
    flushHistory();
    select({nodes[0]});
    QToolButton *button = m_page->styleActionButton();
    QVERIFY(button->isVisible() && button->isEnabled());
    QCOMPARE(button->text(), QStringLiteral("Apply to related"));
    button->click();
    flushHistory();
    for (const MindNode *node : nodes) QCOMPARE(node->fill, QColor(Qt::red));
    action(QStringLiteral("Ctrl+Z"))->trigger();
    flushHistory();
    QVERIFY(nodes[1]->fill != QColor(Qt::red));
    MindNode *cousinA = m_model->addChild(nodes[0], QStringLiteral("A"));
    MindNode *siblingA = m_model->addChild(nodes[0], QStringLiteral("B"));
    MindNode *cousinB = m_model->addChild(nodes[1], QStringLiteral("C"));
    m_scene->autoLayout();
    QVERIFY(m_model->setStyle({cousinA}, StyleProperty::FillColor, QColor(Qt::blue)));
    flushHistory();
    select({cousinA});
    button->click();
    flushHistory();
    QCOMPARE(siblingA->fill, QColor(Qt::blue));
    QVERIFY(cousinB->fill != QColor(Qt::blue));
    // Apply to children: the whole branch of each selected topic, one step; a leaf has nothing to give it to.
    MindNode *grandchild = m_model->addChild(siblingA, QStringLiteral("D"));
    m_scene->autoLayout();
    flushHistory();
    QVERIFY(m_model->setStyle({nodes[1]}, StyleProperty::FillColor, QColor(Qt::green)));
    flushHistory();
    QToolButton *children = m_page->childrenStyleButton();
    select({cousinA});
    QVERIFY(children->isVisible() && !children->isEnabled());
    select({nodes[0], nodes[1]});
    QVERIFY(children->isEnabled());
    children->click();
    flushHistory();
    for (const MindNode *node : {cousinA, siblingA, grandchild}) QCOMPARE(node->fill, QColor(Qt::red));
    QCOMPARE(cousinB->fill, QColor(Qt::green));
    action(QStringLiteral("Ctrl+Z"))->trigger();
    flushHistory();
    QCOMPARE(siblingA->fill, QColor(Qt::blue));
    QVERIFY(grandchild->fill != QColor(Qt::red));
    QVERIFY(cousinB->fill != QColor(Qt::green));
    // Reset style: the manual style of every selected topic removed, one step; the central topic
    // has no Apply to related.
    QToolButton *reset = m_page->resetStyleButton();
    select({cousinA, siblingA});
    QVERIFY(reset->isVisible() && reset->isEnabled());
    reset->click();
    flushHistory();
    QVERIFY(cousinA->style == NodeStyleOverrides() && siblingA->style == NodeStyleOverrides());
    QVERIFY(!reset->isEnabled());
    action(QStringLiteral("Ctrl+Z"))->trigger();
    flushHistory();
    QCOMPARE(siblingA->fill, QColor(Qt::blue));
    select({m_model->root()});
    QVERIFY(!button->isVisible() && reset->isVisible());
}

QTEST_MAIN(StylePageTest)
#include "style_page_test.moc"
