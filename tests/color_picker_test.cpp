// FEAT-019 parte B (docs/features/FEAT-019-fondamenta-stile-nodo.md): selettore colori unico, da solo
// e nella barra di formattazione della mappa. Vero MainWindow, piattaforma offscreen e impostazioni
// separate da quelle dell'applicazione.
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "performance.h"
#include "stylecontrols.h"

#include <QApplication>
#include <QGraphicsProxyWidget>
#include <QGraphicsTextItem>
#include <QLineEdit>
#include <QSettings>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTest>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>

#include <functional>
#include <memory>

class ColorPickerTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void gridHasFortyFiveColorsInNineColumns();
    void escapeClosesWithoutChoosing();
    void swatchAppliesClosesAndBecomesRecent();
    void hexEnterAppliesOnlyCompleteCodes();
    void opacityAppliesAndKeepsPickerOpen();
    void opacityCanBeDisabled();
    void transparentCurrentColorGivesOpaqueChoice();
    void customPanelPicksAnyColor();
    void themeColorsAreShown();
    void opensAboveTheAnchor();
    void formattedTitleKeepsOpacity();
    void formatBarUsesTheSamePicker();

private:
    // Apre il selettore e, dentro il suo ciclo d'attesa, esegue interact sul selettore aperto.
    QList<QColor> runPicker(const ColorPicker::Options &options, const std::function<void(ColorPicker *)> &interact,
                            const QRect &anchor = QRect(600, 600, 32, 32));
    std::unique_ptr<MainWindow> m_window;
};

void ColorPickerTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisColorPickerTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);
    m_window = std::make_unique<MainWindow>();
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    auto *pages = qobject_cast<QStackedWidget *>(m_window->centralWidget());
    QVERIFY(pages);
    pages->setCurrentIndex(1);
}

void ColorPickerTest::cleanupTestCase() { m_window.reset(); }

void ColorPickerTest::init() { QSettings().remove(QStringLiteral("colorPicker/recent")); }

QList<QColor> ColorPickerTest::runPicker(const ColorPicker::Options &options, const std::function<void(ColorPicker *)> &interact,
                                         const QRect &anchor) {
    QList<QColor> chosen;
    bool interacted = false;
    QTimer::singleShot(0, this, [&] {
        ColorPicker *picker = ColorPicker::openPicker();
        interacted = true;
        if (!picker) return;
        interact(picker);
        if (ColorPicker::openPicker()) ColorPicker::openPicker()->close();  // fine della prova
    });
    ColorPicker::exec(anchor, ColorPicker::Placement::Above, options, [&chosen](const QColor &color) { chosen.append(color); });
    if (!interacted) qWarning("il selettore non si e' aperto");
    return chosen;
}

namespace {
QList<QToolButton *> swatches(QWidget *picker) {
    QList<QToolButton *> result;
    for (auto *button : picker->findChildren<QToolButton *>())
        if (button->objectName() != QStringLiteral("SectionToggle") && !button->isCheckable()) result.append(button);
    return result;
}
}

void ColorPickerTest::gridHasFortyFiveColorsInNineColumns() {
    QCOMPARE(ColorPicker::gridColors().size(), 45);
    QCOMPARE(ColorPicker::gridColors().front(), QStringLiteral("#ffffff"));
    QCOMPARE(ColorPicker::gridColors().back(), QStringLiteral("#a34075"));
    int count = 0;
    runPicker({}, [&count](ColorPicker *picker) { count = swatches(picker).size(); });
    QCOMPARE(count, 45);  // nessun tema e nessun recente in questa prova
}

void ColorPickerTest::escapeClosesWithoutChoosing() {
    bool closed = false;
    const QList<QColor> chosen = runPicker({}, [&closed](ColorPicker *picker) {
        QTest::keyClick(picker, Qt::Key_Escape);
        closed = !ColorPicker::openPicker();
    });
    QVERIFY(closed);
    QVERIFY(chosen.isEmpty());
    QVERIFY(ColorPicker::recentColors().isEmpty());
}

void ColorPickerTest::swatchAppliesClosesAndBecomesRecent() {
    bool closed = false;
    const QList<QColor> chosen = runPicker({}, [&closed](ColorPicker *picker) {
        swatches(picker).at(11)->click();  // seconda riga, terzo colore
        closed = !ColorPicker::openPicker();
    });
    QCOMPARE(chosen, QList<QColor>{QColor("#8ede99")});
    QVERIFY(closed);
    QCOMPARE(ColorPicker::recentColors().front(), QColor("#8ede99").name(QColor::HexArgb));
}

void ColorPickerTest::hexEnterAppliesOnlyCompleteCodes() {
    const QList<QColor> chosen = runPicker({}, [](ColorPicker *picker) {
        picker->hexField()->setFocus();
        picker->hexField()->setText(QStringLiteral("#12a"));
        QTest::keyClick(picker->hexField(), Qt::Key_Return);  // incompleto: nessun effetto
        picker->hexField()->setText(QStringLiteral("12ab34"));
        QTest::keyClick(picker->hexField(), Qt::Key_Return);
    });
    QCOMPARE(chosen, QList<QColor>{QColor("#12ab34")});
}

void ColorPickerTest::opacityAppliesAndKeepsPickerOpen() {
    bool stillOpen = false;
    ColorPicker::Options options;
    options.current = QColor("#ff5252");
    const QList<QColor> chosen = runPicker(options, [&stillOpen](ColorPicker *picker) {
        picker->opacityField()->setText(QStringLiteral("50"));
        emit picker->opacityField()->editingFinished();
        QCOMPARE(picker->opacityField()->text(), QStringLiteral("50 %"));
        stillOpen = ColorPicker::openPicker() == picker;
    });
    QVERIFY(stillOpen);
    QCOMPARE(chosen.size(), 1);
    QCOMPARE(chosen.front().rgb(), QColor("#ff5252").rgb());
    QCOMPARE(chosen.front().alpha(), 128);
}

void ColorPickerTest::opacityCanBeDisabled() {
    ColorPicker::Options options;
    options.allowOpacity = false;
    bool disabled = false;
    const QList<QColor> chosen = runPicker(options, [&disabled](ColorPicker *picker) {
        disabled = !picker->opacityField()->isEnabled();
        swatches(picker).at(0)->click();
    });
    QVERIFY(disabled);
    QCOMPARE(chosen.front().alpha(), 255);
}

void ColorPickerTest::transparentCurrentColorGivesOpaqueChoice() {
    // BUG-011: riempimento o bordo trasparenti (per esempio dal terzo livello): il colore scelto
    // ha opacita' 100%, non 0%.
    ColorPicker::Options options;
    options.current = QColor(255, 107, 107, 0);
    QString shownOpacity;
    const QList<QColor> chosen = runPicker(options, [&shownOpacity](ColorPicker *picker) {
        shownOpacity = picker->findChild<QLineEdit *>(QStringLiteral("OpacityField"))->text();
        swatches(picker).at(11)->click();
    });
    QCOMPARE(shownOpacity, QStringLiteral("100 %"));
    QCOMPARE(chosen, QList<QColor>{QColor("#8ede99")});
    QCOMPARE(chosen.front().alpha(), 255);
    // Un colore semitrasparente conserva invece la sua opacita'.
    options.current = QColor(255, 107, 107, 64);
    const QList<QColor> half = runPicker(options, [](ColorPicker *picker) { swatches(picker).at(11)->click(); });
    QCOMPARE(half.front().alpha(), 64);
}

void ColorPickerTest::customPanelPicksAnyColor() {
    bool panelShown = false;
    const QList<QColor> chosen = runPicker({}, [&panelShown](ColorPicker *picker) {
        picker->customColorButton()->click();
        panelShown = picker->customColorPanel()->isVisible();
        // Primo figlio del pannello: il quadrato saturazione/luminosita'.
        QWidget *area = picker->customColorPanel()->findChildren<QWidget *>().front();
        QTest::mousePress(area, Qt::LeftButton, {}, QPoint(area->width() - 2, 2));
        QTest::mouseRelease(area, Qt::LeftButton, {}, QPoint(area->width() - 2, 2));
    });
    QVERIFY(panelShown);
    QCOMPARE(chosen.size(), 1);
    QVERIFY(chosen.front().hsvSaturation() > 240);  // angolo in alto a destra: colore pieno
    QVERIFY(chosen.front().value() > 240);
}

void ColorPickerTest::themeColorsAreShown() {
    ColorPicker::rememberRecentColor(QColor("#123456"));
    ColorPicker::Options options;
    options.themeColors = {"#ff6b6b", "#ff9f69", "#97d3b6", "#88e2d7", "#6fd0f9", "#e18bee"};
    int count = 0;
    const QList<QColor> chosen = runPicker(options, [&count](ColorPicker *picker) {
        count = swatches(picker).size();
        swatches(picker).at(45)->click();  // primo colore del tema
    });
    QCOMPARE(count, 45 + 6 + 1);
    QCOMPARE(chosen, QList<QColor>{QColor("#ff6b6b")});
}

void ColorPickerTest::opensAboveTheAnchor() {
    const QRect anchor(600, 700, 32, 32);
    QRect geometry;
    runPicker({}, [&geometry](ColorPicker *picker) { geometry = picker->frameGeometry(); }, anchor);
    QVERIFY(!geometry.isEmpty());
    QVERIFY(geometry.bottom() < anchor.top());
    QCOMPARE(geometry.right(), anchor.right());
}

void ColorPickerTest::formattedTitleKeepsOpacity() {
    // Il titolo formattato (QTextDocument::toHtml) conserva l'opacita' del colore: il campo
    // opacita' puo' restare attivo anche nella barra di formattazione.
    QTextDocument document;
    document.setPlainText(QStringLiteral("Testo"));
    QTextCursor cursor(&document);
    cursor.select(QTextCursor::Document);
    QTextCharFormat format;
    format.setForeground(QColor(255, 82, 82, 128));
    cursor.mergeCharFormat(format);
    QTextDocument reloaded;
    reloaded.setHtml(document.toHtml());
    QTextCursor reader(&reloaded);
    reader.movePosition(QTextCursor::NextCharacter);
    QCOMPARE(reader.charFormat().foreground().color().alpha(), 128);
}

void ColorPickerTest::formatBarUsesTheSamePicker() {
    auto *model = m_window->findChild<MindMapModel *>();
    auto *view = m_window->findChild<MindMapView *>();
    QVERIFY(model && view);
    auto *scene = qobject_cast<MindMapScene *>(view->scene());
    MindNode *node = model->addChild(model->root(), QStringLiteral("Colore dal selettore"));
    QApplication::processEvents();
    NodeItem *item = nullptr;
    for (auto *candidate : scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(candidate); nodeItem && nodeItem->node() == node) item = nodeItem;
    QVERIFY(item);
    scene->selectNode(node);
    item->beginEditing();  // tutto il testo selezionato e barra di formattazione visibile
    QApplication::processEvents();
    QToolButton *colorButton = nullptr;
    for (auto *candidate : scene->items())
        if (auto *proxy = dynamic_cast<QGraphicsProxyWidget *>(candidate); proxy && proxy->widget())
            for (auto *button : proxy->widget()->findChildren<QToolButton *>())
                if (button->toolTip() == QStringLiteral("Text color")) colorButton = button;
    QVERIFY(colorButton);
    bool sawTheme = false;
    QTimer::singleShot(0, this, [&sawTheme] {
        ColorPicker *picker = ColorPicker::openPicker();
        if (!picker) return;
        sawTheme = swatches(picker).size() >= 45 + 6;  // palette predefinita (Alba): 6 colori
        swatches(picker).at(20)->click();  // #2cd551
    });
    colorButton->click();
    QVERIFY(sawTheme);
    QVERIFY(item->isEditing());  // scegliere un colore non chiude la modifica
    item->commitEditing();
    QVERIFY(node->titleHtml.contains(QStringLiteral("#2cd551"), Qt::CaseInsensitive));
}

QTEST_MAIN(ColorPickerTest)
#include "color_picker_test.moc"
