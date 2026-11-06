// FEAT-036 (docs/features/FEAT-036-varianti-tema-colore.md): varianti del tema colore con il vero
// MainWindow: regole dei colori sullo sfondo, miniature, colore sfondo libero, cambio di palette,
// outliner scuro, file, tema predefinito delle mappe nuove e foglio nuovo dal foglio attivo.
#include "custompalettes.h"
#include "floatingui.h"
#include "document.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodestyle.h"
#include "performance.h"
#include "sidepanel.h"
#include "stylecontrols.h"
#include "toggleswitch.h"

#include <QDockWidget>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QJsonObject>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTest>
#include <QDialog>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>

#include <memory>

class ThemeVariantsTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void colorsFollowTheBackground();
    void tileChoosesTheVariantInOneStep();
    void freeBackgroundAndPaletteChange();
    void outlinerTurnsDarkOnDarkBackgrounds();
    void floatingGroupsTakeTheToneOfTheBackground();
    void fileKeepsVariantAndBackground();
    void newMapUsesTheDefaultAndNewSheetCopiesTheActiveOne();
    void sheetFontAndWidthAreTheBaseOfTheNodes();
    void coloredBranchOffPaintsTheLinesWithOneColor();
    void snapshotForVisualReview();

private:
    void flushHistory();
    void undo();
    QAction *actionWithShortcut(const QKeySequence &shortcut) const;

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    VariantPicker *m_variants = nullptr;
    ColorField *m_background = nullptr;
    PaletteSelect *m_paletteSelect = nullptr;
};

void ThemeVariantsTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisThemeVariantsTest"));
    QSettings().clear();
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
    m_scene = qobject_cast<MindMapScene *>(m_window->findChild<MindMapView *>()->scene());
    m_variants = m_window->findChild<VariantPicker *>();
    m_background = m_window->findChild<ColorField *>(QStringLiteral("BackgroundColorField"));
    m_paletteSelect = m_window->findChild<PaletteSelect *>();
    QVERIFY(m_model && m_scene && m_variants && m_background && m_paletteSelect);
}

void ThemeVariantsTest::cleanupTestCase() { m_window.reset(); }

void ThemeVariantsTest::init() {
    m_model->setPalette(NodeStyle::kDefaultPaletteId, NodeStyle::findPalette(NodeStyle::kDefaultPaletteId)->colors);
    flushHistory();
}

void ThemeVariantsTest::flushHistory() {
    QApplication::processEvents();
    // The open history step is closed at once instead of waiting for its 150 ms timer.
    m_window->findChild<Document *>()->flushPendingStep();
    QApplication::processEvents();
}

QAction *ThemeVariantsTest::actionWithShortcut(const QKeySequence &shortcut) const {
    for (auto *action : m_window->actions())
        if (action->shortcuts().contains(shortcut)) return action;
    return nullptr;
}

void ThemeVariantsTest::undo() {
    actionWithShortcut(QKeySequence(QKeySequence::Undo))->trigger();
    QApplication::processEvents();
}

void ThemeVariantsTest::colorsFollowTheBackground() {
    const QStringList palette{"#ffffff", "#f2f2f2", "#f22816", "#f2b807", "#233ed9", "#0d0d0d"};  // Energia
    // Sfondo predefinito: regola di FEAT-031 (niente colori quasi bianchi).
    QCOMPARE(NodeStyle::branchColors(palette, NodeStyle::Backdrop()).size(), 4);
    // Variante 6 (sfondo quasi nero): dal colore dopo lo sfondo, girando, solo i contrastati.
    const NodeStyle::Backdrop dark = NodeStyle::backdropFor(QStringLiteral("energia"), palette, 5, std::nullopt);
    QCOMPARE(dark.color, QColor("#0d0d0d"));
    const QList<QColor> onDark = NodeStyle::branchColors(palette, dark);
    QVERIFY(!onDark.isEmpty());
    QCOMPARE(onDark.front(), QColor("#ffffff"));  // il successivo di c6 e' c1
    QVERIFY(!onDark.contains(QColor("#0d0d0d")));
    for (const QColor &color : onDark) QVERIFY(NodeStyle::apcaContrast(color, dark.color) >= NodeStyle::kMinBranchContrast);
    // Nessun colore abbastanza contrastato: un solo colore.
    const QStringList pale{"#fff0f3", "#ffccd5", "#ffb3c1", "#ffe5ea", "#ffd6dd", "#ffc2cc"};
    QCOMPARE(NodeStyle::branchColors(pale, NodeStyle::backdropFor(QStringLiteral("rose"), pale, 0, std::nullopt)).size(), 1);
    // Testo dei nodi sullo sfondo scuro: chiaro; Depenna con il riempimento dello sfondo.
    auto root = std::make_unique<MindNode>();
    auto main = std::make_unique<MindNode>();
    main->parent = root.get();
    auto child = std::make_unique<MindNode>();
    child->parent = main.get();
    auto grandchild = std::make_unique<MindNode>();
    grandchild->parent = child.get();
    grandchild->style.preset = NodePreset::Depenna;
    MindNode *third = grandchild.get();
    child->children.push_back(std::move(grandchild));
    main->children.push_back(std::move(child));
    root->children.push_back(std::move(main));
    NodeStyle::resolveTree(root.get(), palette, dark);
    QCOMPARE(root->text, QColor(Qt::white));
    QCOMPARE(third->fill, dark.color);
    QCOMPARE(third->text, QColor(Qt::white));
    QCOMPARE(NodeStyle::annotationColor(QColor("#7c8796"), dark), QColor(Qt::white));
    QCOMPARE(NodeStyle::annotationColor(QColor("#7c8796"), NodeStyle::Backdrop()), QColor("#7c8796"));
    // Palette tonale su sfondo chiaro: un solo colore, il piu' saturo che si legge (Kimono, rosa -> blu).
    const QStringList kimono = NodeStyle::findPalette(QStringLiteral("kimono"))->colors;
    QVERIFY(NodeStyle::isTonalPalette(kimono));
    const NodeStyle::Backdrop pink = NodeStyle::backdropFor(QStringLiteral("kimono"), kimono, 1, std::nullopt);
    QCOMPARE(pink.color, QColor(kimono[1]));
    QCOMPARE(pink.branches, QList<QColor>{QColor("#4a51d9")});
    // Palette colorata: sfondo bianco; variante 1 multicolore, le altre un colore c1-c5.
    const QStringList alba = NodeStyle::findPalette(QStringLiteral("alba"))->colors;
    QVERIFY(!NodeStyle::isTonalPalette(alba));
    QVERIFY(NodeStyle::backdropFor(QStringLiteral("alba"), alba, 0, std::nullopt).isDefault());
    const NodeStyle::Backdrop albaThird = NodeStyle::backdropFor(QStringLiteral("alba"), alba, 2, std::nullopt);
    QCOMPARE(albaThird.color, NodeStyle::kMapBackground);
    QCOMPARE(albaThird.branches, QList<QColor>{QColor(alba[1])});
    // Eccezioni dalle foto di riferimento.
    const QStringList magnifico = NodeStyle::findPalette(QStringLiteral("magnifico"))->colors;
    const NodeStyle::Backdrop red = NodeStyle::backdropFor(QStringLiteral("magnifico"), magnifico, 0, std::nullopt);
    QCOMPARE(red.color, QColor(magnifico[0]));
    QCOMPARE(red.branches, QList<QColor>{QColor(Qt::white)});
    const QStringList corallo = NodeStyle::findPalette(QStringLiteral("corallo"))->colors;
    const NodeStyle::Backdrop night = NodeStyle::backdropFor(QStringLiteral("corallo"), corallo, 2, std::nullopt);
    QVERIFY(NodeStyle::relativeLuminance(night.color) < 0.02);
    QCOMPARE(night.branches, QList<QColor>{QColor(corallo[2])});
}

void ThemeVariantsTest::tileChoosesTheVariantInOneStep() {
    // Alba e' una palette colorata: sfondo bianco, variante 5 = tutti i rami del colore c4.
    MindNode *main = m_model->addChild(m_model->root(), QStringLiteral("Ramo"));
    QToolButton *tile = m_variants->tile(4);
    QVERIFY(tile && tile->isEnabled());
    tile->click();
    QApplication::processEvents();
    QCOMPARE(m_model->paletteVariant, 4);
    QCOMPARE(m_scene->backgroundBrush().color(), NodeStyle::kMapBackground);
    QCOMPARE(main->branchColor, QColor(m_model->activePalette[3]));
    QCOMPARE(m_variants->variant(), 4);
    // Kimono e' tonale: la variante 6 (blu notte) fa da sfondo.
    const NodeStyle::Palette *kimono = NodeStyle::findPalette(QStringLiteral("kimono"));
    m_model->setPalette(kimono->id, kimono->colors);
    m_variants->tile(5)->click();
    QApplication::processEvents();
    QCOMPARE(m_scene->backgroundBrush().color(), QColor(kimono->colors[5]));
    QCOMPARE(m_background->color(), QColor(kimono->colors[5]));
    undo();
    QApplication::processEvents();
    QCOMPARE(m_scene->backgroundBrush().color(), NodeStyle::kMapBackground);
    undo();  // torna Alba con la variante 5
    undo();
    flushHistory();
    undo();
    QCOMPARE(m_model->paletteVariant, -1);
    QCOMPARE(m_scene->backgroundBrush().color(), NodeStyle::kMapBackground);
    QCOMPARE(m_variants->variant(), -1);
}

void ThemeVariantsTest::freeBackgroundAndPaletteChange() {
    MindNode *main = m_model->addChild(m_model->root(), QStringLiteral("Ramo"));
    m_model->setBackdrop(2);
    QApplication::processEvents();
    const QColor branchBefore = main->branchColor;
    emit m_background->colorChosen(QColor("#123456"));
    QApplication::processEvents();
    // Richiesta 2026-10-19: lo sfondo libero cambia solo sfondo e testo, i rami restano.
    QCOMPARE(m_model->paletteVariant, 2);
    QCOMPARE(m_model->background, std::optional<QColor>(QColor("#123456")));
    QCOMPARE(m_scene->backgroundBrush().color(), QColor("#123456"));
    QCOMPARE(main->branchColor, branchBefore);
    QCOMPARE(m_model->root()->text, QColor(Qt::white));  // centrale senza riempimento: chiaro sul blu notte
    m_model->setBackdrop(1);  // una miniatura toglie lo sfondo libero
    QVERIFY(!m_model->background);
    emit m_paletteSelect->paletteChosen(QStringLiteral("oceano"));
    QApplication::processEvents();
    QCOMPARE(m_model->paletteId, QStringLiteral("oceano"));
    QCOMPARE(m_model->paletteVariant, -1);  // nuova palette: sfondo bianco
    QCOMPARE(m_scene->backgroundBrush().color(), NodeStyle::kMapBackground);
}

void ThemeVariantsTest::outlinerTurnsDarkOnDarkBackgrounds() {
    auto *tree = m_window->findChild<QTreeView *>(QStringLiteral("OutlineTree"));  // not the navigation lists
    QVERIFY(tree);
    QVERIFY(tree->styleSheet().contains(QStringLiteral("color:#202630")));
    m_model->setBackground(QColor("#101820"));
    QApplication::processEvents();
    QVERIFY(tree->styleSheet().contains(QStringLiteral("color:#ffffff")));
    m_model->setBackground(QColor("#fff4cf"));  // chiaro: outliner bianco
    QApplication::processEvents();
    QVERIFY(tree->styleSheet().contains(QStringLiteral("color:#202630")));
}

// Richiesta dell'utente 2026-10-19 (foto di riferimento): gruppi flottanti nel tono dello sfondo.
void ThemeVariantsTest::floatingGroupsTakeTheToneOfTheBackground() {
    QCOMPARE(FloatingUi::toneFor(Qt::white).fill, QColor(255, 255, 255, 214));  // come prima
    // Sul bianco iniziale il bottone acceso ha l'icona chiara (2026-10-19: restava scura e spariva).
    const auto lightPixelsOf = [](const QIcon &icon, QIcon::State state) {
        const QImage image = icon.pixmap(20, 20, QIcon::Normal, state).toImage();
        int count = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (qAlpha(image.pixel(x, y)) > 128 && QColor(image.pixel(x, y)).lightness() > 200) ++count;
        return count;
    };
    auto *toggle = m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"));
    QVERIFY(toggle && !FloatingUi::tone().dark);
    QVERIFY(lightPixelsOf(toggle->icon(), QIcon::On) > 0);
    QCOMPARE(lightPixelsOf(toggle->icon(), QIcon::Off), 0);
    const FloatingUi::Tone light = FloatingUi::toneFor(QColor("#8ab4f8"));
    QVERIFY(!light.dark);
    QVERIFY(light.fill.blue() > light.fill.red());  // tinta dello sfondo, schiarita
    QVERIFY(light.fill.lightness() > QColor("#8ab4f8").lightness());
    const FloatingUi::Tone dark = FloatingUi::toneFor(QColor("#1b1a5c"));
    QVERIFY(dark.dark);
    QVERIFY(dark.fill.lightness() > QColor("#1b1a5c").lightness() && dark.fill.lightness() < 110);
    QVERIFY(dark.text.lightness() > 200);
    QCOMPARE(dark.checkedIcon, QColor("#1f2937"));  // bottone acceso invertito
    // Nella finestra: icone chiare sullo sfondo scuro, il bottone del pannello acceso quando e' aperto.
    m_model->setBackground(QColor("#1b1a5c"));
    QApplication::processEvents();
    QVERIFY(FloatingUi::tone().dark);
    auto *panel = m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"));
    QVERIFY(panel && panel->isCheckable());
    const QImage icon = panel->icon().pixmap(20, 20, QIcon::Normal, QIcon::Off).toImage();
    int lightPixels = 0;
    for (int y = 0; y < icon.height(); ++y)
        for (int x = 0; x < icon.width(); ++x)
            if (qAlpha(icon.pixel(x, y)) > 128 && QColor(icon.pixel(x, y)).lightness() > 200) ++lightPixels;
    QVERIFY(lightPixels > 0);
    panel->click();
    QTest::qWait(500);
    QVERIFY(panel->isChecked());
    panel->click();
    QTest::qWait(100);
    QVERIFY(!panel->isChecked());
    m_model->setBackground(Qt::white);
    QApplication::processEvents();
    QVERIFY(!FloatingUi::tone().dark);
}

void ThemeVariantsTest::fileKeepsVariantAndBackground() {
    MindMapModel model;
    QVERIFY(!model.toJson().contains(QStringLiteral("paletteVariant")));
    QVERIFY(!model.toJson().contains(QStringLiteral("background")));
    QVERIFY(model.setBackdrop(3));
    MindMapModel reopened;
    QVERIFY(reopened.fromJson(model.toJson()));
    QCOMPARE(reopened.paletteVariant, 3);
    QVERIFY(model.setBackground(QColor("#abcdef")));
    QVERIFY(reopened.fromJson(model.toJson()));
    QCOMPARE(reopened.paletteVariant, 3);  // lo sfondo libero si somma alla variante
    QCOMPARE(reopened.background, std::optional<QColor>(QColor("#abcdef")));
    QVERIFY(!model.setBackdrop(9));  // variante inesistente
}

void ThemeVariantsTest::newMapUsesTheDefaultAndNewSheetCopiesTheActiveOne() {
    CustomPalettes::setDefaultTheme(QStringLiteral("oceano"), 2);
    // Nuova mappa: eventuale "Non salvare", poi la galleria.
    auto *timer = new QTimer(this);
    int ticks = 0;
    connect(timer, &QTimer::timeout, this, [timer, &ticks] {
        if (++ticks > 200) { timer->stop(); return; }
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal) return;
        for (auto *button : modal->findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("Don't save")) { button->click(); return; }
        // FEAT-026 fase D: la galleria al posto della scelta del modello; si conferma la prima variante.
        if (modal->objectName() == QStringLiteral("VariantGallery")) { static_cast<QDialog *>(modal)->accept(); timer->stop(); return; }
    });
    timer->start(20);
    QAction *newMap = nullptr;
    for (auto *action : m_window->findChildren<QAction *>())
        if (action->text() == QStringLiteral("New")) newMap = action;
    QVERIFY(newMap);
    newMap->trigger();
    QApplication::processEvents();
    timer->deleteLater();
    QCOMPARE(m_model->paletteId, QStringLiteral("oceano"));
    QCOMPARE(m_model->paletteVariant, 2);
    // Nuovo foglio: tema del foglio attivo (qui uno sfondo libero), non quello predefinito.
    m_model->setBackground(QColor("#334455"));
    flushHistory();
    QToolButton *addSheet = nullptr;
    for (auto *button : m_window->findChildren<QToolButton *>())
        if (button->toolTip() == QStringLiteral("Add sheet")) addSheet = button;
    QVERIFY(addSheet);
    // FEAT-026 fase D: "+ foglio" apre la galleria; si conferma la prima variante.
    QTimer::singleShot(0, [] { if (auto *gallery = qobject_cast<QDialog *>(QApplication::activeModalWidget())) gallery->accept(); });
    addSheet->click();
    flushHistory();
    QCOMPARE(m_model->paletteId, QStringLiteral("oceano"));
    QCOMPARE(m_model->background, std::optional<QColor>(QColor("#334455")));
    QCOMPARE(m_scene->backgroundBrush().color(), QColor("#334455"));
    CustomPalettes::setDefaultTheme(NodeStyle::kDefaultPaletteId, -1);
}

// Foto per il confronto a vista: solo con MENTIS_SNAPSHOT_DIR, da lanciare con una
// piattaforma che ha i caratteri (es. windows). Mappa con la variante 5 di Magnifico e pannello aperto.
void ThemeVariantsTest::snapshotForVisualReview() {
    const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR");
    if (dir.isEmpty()) QSKIP("MENTIS_SNAPSHOT_DIR non impostata");
    const NodeStyle::Palette *magnifico = NodeStyle::findPalette(QStringLiteral("magnifico"));
    QVERIFY(magnifico);
    m_model->setPalette(magnifico->id, magnifico->colors);
    for (const QString &title : {QStringLiteral("Uno"), QStringLiteral("Due"), QStringLiteral("Tre")}) {
        MindNode *main = m_model->addChild(m_model->root(), title);
        m_model->addChild(m_model->addChild(main, title + QStringLiteral(" a")), title + QStringLiteral(" b"));
    }
    m_scene->autoLayout();
    m_model->setBackdrop(4);
    m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))->click();  // the map's, not the Gantt's
    QTest::qWait(400);
    m_scene->clearSelection();
    if (auto *panel = m_window->findChild<SidePanel *>()) panel->setCurrentPage(2);  // pagina Mappa
    QApplication::processEvents();
    QTest::qWait(100);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/variants-map.png")));
    // Miniature di alcune palette, da confrontare con le foto di riferimento.
    for (const QString &id : {QStringLiteral("alba"), QStringLiteral("kimono"), QStringLiteral("codice"), QStringLiteral("energia"),
                              QStringLiteral("magnifico"), QStringLiteral("corallo"), QStringLiteral("serie"), QStringLiteral("costanza"),
                              QStringLiteral("arcobaleno"), QStringLiteral("te-verde")}) {
        const NodeStyle::Palette *palette = NodeStyle::findPalette(id);
        if (!palette) continue;
        m_model->setPalette(palette->id, palette->colors);
        QApplication::processEvents();
        m_variants->grab().save(dir + QStringLiteral("/variants-%1.png").arg(id));
    }
    m_model->setPalette(NodeStyle::kDefaultPaletteId, NodeStyle::findPalette(NodeStyle::kDefaultPaletteId)->colors);
    m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))->click();  // the map's, not the Gantt's
    m_model->setBackground(QColor("#cc4488"));
    QTest::qWait(400);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/variants-closed.png")));
    // Gruppi flottanti sugli sfondi delle foto di riferimento (2026-10-19).
    for (const QString &color : {QStringLiteral("#1b1a5c"), QStringLiteral("#8ab4f8"), QStringLiteral("#ffa8a8"), QStringLiteral("#046663")}) {
        m_model->setBackground(QColor(color));
        QTest::qWait(200);
        QVERIFY(m_window->grab().save(dir + QStringLiteral("/groups-%1.png").arg(color.mid(1))));
    }
    m_model->setBackground(QColor("#046663"));
    m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))->click();  // the map's, not the Gantt's
    QTest::qWait(500);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/groups-panel-dark.png")));
    m_model->setBackground(Qt::white);
    QTest::qWait(200);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/groups-panel-light.png")));
    m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))->click();  // the map's, not the Gantt's
    // Popup con una palette preferita (stella gialla).
    PalettePicker::setFavorite(QStringLiteral("alba"), true);
    QTimer::singleShot(200, this, [dir] {
        if (PalettePicker *picker = PalettePicker::openPicker()) {
            picker->showGroup(1);
            picker->grab().save(dir + QStringLiteral("/variants-picker.png"));
            picker->close();
        }
    });
    m_paletteSelect->click();
    PalettePicker::setFavorite(QStringLiteral("alba"), false);
}

// FEAT-043 parte stile: font e spessore del foglio come base dei nodi senza valori scelti a mano.
void ThemeVariantsTest::sheetFontAndWidthAreTheBaseOfTheNodes() {
    MindNode *plain = m_model->addChild(m_model->root(), QStringLiteral("Senza"));
    MindNode *chosen = m_model->addChild(m_model->root(), QStringLiteral("Con"));
    m_model->setStyle({chosen}, StyleProperty::FontFamily, QStringLiteral("Arial"));
    m_model->setStyle({chosen}, StyleProperty::BranchWidth, 6.0);
    flushHistory();
    auto *panel = m_window->findChild<SheetStylePanel *>();
    QVERIFY(panel);
    const int courier = panel->fontField()->findData(QStringLiteral("Courier New"));
    const QString family = courier >= 0 ? QStringLiteral("Courier New") : panel->fontField()->itemData(1).toString();
    emit panel->fontField()->activated(panel->fontField()->findData(family));
    QApplication::processEvents();
    QCOMPARE(m_model->sheetStyle.fontFamily, family);
    QCOMPARE(plain->fontFamily, family);
    QCOMPARE(chosen->fontFamily, QStringLiteral("Arial"));  // scelto a mano: vince
    emit panel->widthField()->activated(panel->widthField()->findData(5.0));  // Grassetto
    QApplication::processEvents();
    QCOMPARE(plain->branchWidth, 5.0);
    QCOMPARE(chosen->branchWidth, 6.0);
    QVERIFY(!m_model->sheetStyle.tapered);
    // FEAT-023: le voci affusolate tengono lo spessore e accendono l'affusolato (un passo a se').
    flushHistory();
    emit panel->widthField()->activated(panel->widthField()->findData(-3.0));  // Medio - Affusolato
    QApplication::processEvents();
    QCOMPARE(plain->branchWidth, 3.0);
    QVERIFY(m_model->sheetStyle.tapered);
    QCOMPARE(m_model->toJson().value(QStringLiteral("sheetBranchTapered")).toBool(), true);
    flushHistory();
    undo();
    QVERIFY(!m_model->sheetStyle.tapered);
    QCOMPARE(plain->branchWidth, 5.0);
    flushHistory();
    undo();  // un passo per scelta
    QCOMPARE(plain->branchWidth, 2.0);
    QCOMPARE(plain->fontFamily, family);
    undo();
    QVERIFY(plain->fontFamily.isEmpty());
    // File: solo i valori diversi dal predefinito.
    MindMapModel model;
    QVERIFY(!model.toJson().contains(QStringLiteral("sheetFont")));
    NodeStyle::SheetStyle style;
    style.fontFamily = QStringLiteral("Georgia");
    style.branchWidth = 3;
    style.coloredBranches = false;
    style.lineColor = QColor("#112233");
    QVERIFY(model.setSheetStyle(style));
    MindMapModel reopened;
    QVERIFY(reopened.fromJson(model.toJson()));
    QCOMPARE(reopened.sheetStyle, style);
}

// FEAT-043: Ramo colorato spento = linee del Colore del foglio; nodi e rami scelti a mano invariati.
void ThemeVariantsTest::coloredBranchOffPaintsTheLinesWithOneColor() {
    MindNode *main = m_model->addChild(m_model->root(), QStringLiteral("Principale"));
    MindNode *child = m_model->addChild(main, QStringLiteral("Figlio"));
    MindNode *manual = m_model->addChild(m_model->root(), QStringLiteral("A mano"));
    MindNode *manualChild = m_model->addChild(manual, QStringLiteral("Figlio a mano"));
    m_model->setStyle({manual}, StyleProperty::BranchColor, QColor("#00aa00"));
    flushHistory();
    const QColor fillBefore = main->fill;
    QCOMPARE(main->lineColor, main->branchColor);  // acceso: come oggi
    auto *panel = m_window->findChild<SheetStylePanel *>();
    QVERIFY(!panel->lineColorField()->isEnabled());
    panel->coloredSwitch()->click();
    QApplication::processEvents();
    QVERIFY(!m_model->sheetStyle.coloredBranches);
    QCOMPARE(main->lineColor, m_model->root()->text);  // il colore del testo del centrale
    QCOMPARE(child->lineColor, m_model->root()->text);
    QCOMPARE(main->fill, fillBefore);  // i nodi non cambiano
    QCOMPARE(manual->lineColor, QColor("#00aa00"));
    QCOMPARE(manualChild->lineColor, QColor("#00aa00"));
    QVERIFY(panel->lineColorField()->isEnabled());
    emit panel->lineColorField()->colorChosen(QColor("#ff0000"));
    QApplication::processEvents();
    QCOMPARE(main->lineColor, QColor("#ff0000"));
    flushHistory();
    undo();
    QCOMPARE(main->lineColor, m_model->root()->text);  // il colore del testo del centrale
    undo();
    QCOMPARE(main->lineColor, main->branchColor);
}

QTEST_MAIN(ThemeVariantsTest)
#include "theme_variants_test.moc"
