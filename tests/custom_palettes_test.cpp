// FEAT-032 (docs/features/FEAT-032-palette-personalizzate.md): palette personalizzate con il vero
// MainWindow: modale "Crea nuovo tema colore", nomi unici, applicazione al foglio in un passo,
// gruppo Personalizzate, Rimuovi con conferma, palette di un file non installata.
#include "custompalettes.h"
#include "document.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "modaldialog.h"
#include "palettedialog.h"
#include "performance.h"
#include "stylecontrols.h"

#include <QDockWidget>
#include <QAction>
#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <functional>
#include <memory>

class CustomPalettesTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void namesAreUniqueAndDefaultNamesCount();
    void createdPaletteAppliesInOneStep();
    void saveNeedsSixColorsAndAFreeName();
    void cancelChangesNothing();
    void customGroupShowsTheCreatedPalettes();
    void removeAsksAndKeepsTheSheetColors();
    void unknownPaletteFromAFileCanBeAdded();
    void snapshotForVisualReview();

private:
    void onNextModal(const std::function<void(ModalDialog *)> &handle);
    void runPicker(const std::function<void(PalettePicker *)> &interact);
    void undo();
    QToolButton *newPaletteButton() const;

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    PaletteSelect *m_select = nullptr;
};

const QStringList kColors{"#112233", "#445566", "#778899", "#aabbcc", "#ddeeff", "#102030"};

void CustomPalettesTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisCustomPalettesTest"));
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
    m_select = m_window->findChild<PaletteSelect *>();
    QVERIFY(m_model && m_select && newPaletteButton());
}

void CustomPalettesTest::cleanupTestCase() { m_window.reset(); }

void CustomPalettesTest::init() {
    QSettings().remove(QStringLiteral("palettes"));
    m_model->setPalette(NodeStyle::kDefaultPaletteId, NodeStyle::findPalette(NodeStyle::kDefaultPaletteId)->colors);
    QApplication::processEvents();
    m_window->findChild<Document *>()->flushPendingStep();  // chiude il passo di storico
}

QToolButton *CustomPalettesTest::newPaletteButton() const {
    return m_window->findChild<QToolButton *>(QStringLiteral("NewPaletteButton"));
}

void CustomPalettesTest::onNextModal(const std::function<void(ModalDialog *)> &handle) {
    auto *timer = new QTimer(this);
    auto attempts = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, this, [timer, attempts, handle] {
        if (auto *modal = qobject_cast<ModalDialog *>(QApplication::activeModalWidget())) {
            timer->stop();
            timer->deleteLater();
            handle(modal);
        } else if (++*attempts > 100) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start(10);
}

void CustomPalettesTest::runPicker(const std::function<void(PalettePicker *)> &interact) {
    bool opened = false;
    QTimer::singleShot(0, this, [&] {
        PalettePicker *picker = PalettePicker::openPicker();
        opened = picker != nullptr;
        if (!picker) return;
        interact(picker);
        if (PalettePicker::openPicker()) PalettePicker::openPicker()->close();
    });
    m_select->click();
    QApplication::processEvents();
    QVERIFY2(opened, "il menu delle palette non si e' aperto");
}

void CustomPalettesTest::undo() {
    for (auto *action : m_window->actions())
        if (action->shortcuts().contains(QKeySequence(QKeySequence::Undo))) { action->trigger(); break; }
    QApplication::processEvents();
}

void CustomPalettesTest::namesAreUniqueAndDefaultNamesCount() {
    QVERIFY(CustomPalettes::isNameTaken(QStringLiteral("  dawn ")));  // palette di base, maiuscole e spazi
    QCOMPARE(CustomPalettes::nextDefaultName(), QStringLiteral("Custom 1"));
    const QString id = CustomPalettes::add(QString(), kColors);
    QVERIFY(id.startsWith(QStringLiteral("custom-")));
    QCOMPARE(CustomPalettes::find(id)->name, QStringLiteral("Custom 1"));
    QCOMPARE(CustomPalettes::nextDefaultName(), QStringLiteral("Custom 2"));
    QVERIFY(CustomPalettes::isNameTaken(QStringLiteral("custom 1")));
    QVERIFY(CustomPalettes::add(QStringLiteral("Poche"), kColors.mid(0, 5)).isEmpty());  // servono 6 colori
    QVERIFY(CustomPalettes::remove(id));
    QVERIFY(CustomPalettes::all().isEmpty());
}

void CustomPalettesTest::createdPaletteAppliesInOneStep() {
    bool asked = false;
    onNextModal([&](ModalDialog *modal) {
        auto *dialog = qobject_cast<NewPaletteDialog *>(modal);
        if (!dialog) { modal->reject(); return; }
        asked = true;
        QVERIFY(!dialog->acceptButton()->isEnabled());
        for (int i = 0; i < kColors.size(); ++i) dialog->setColor(i, QColor(kColors[i]));
        dialog->nameField()->setText(QStringLiteral("Mare"));
        QVERIFY(dialog->acceptButton()->isEnabled());
        dialog->acceptButton()->click();
    });
    newPaletteButton()->click();
    QApplication::processEvents();
    QVERIFY(asked);
    QCOMPARE(CustomPalettes::all().size(), 1);
    const NodeStyle::Palette created = CustomPalettes::all().front();
    QCOMPARE(created.name, QStringLiteral("Mare"));
    QCOMPARE(created.colors, kColors);
    QCOMPARE(m_model->paletteId, created.id);
    QCOMPARE(m_model->activePalette, kColors);
    QCOMPARE(m_model->paletteName, QStringLiteral("Mare"));
    QCOMPARE(m_select->paletteName(), QStringLiteral("Mare"));
    // Il nome va nel file e nello storico.
    QCOMPARE(m_model->toJson().value(QStringLiteral("paletteName")).toString(), QStringLiteral("Mare"));
    m_window->findChild<Document *>()->flushPendingStep();  // chiude il passo di storico
    undo();  // un passo: torna la palette di prima
    QCOMPARE(m_model->paletteId, NodeStyle::kDefaultPaletteId);
    QVERIFY(m_model->paletteName.isEmpty());
    QCOMPARE(CustomPalettes::all().size(), 1);  // la palette resta fra le proprie
}

void CustomPalettesTest::saveNeedsSixColorsAndAFreeName() {
    NewPaletteDialog dialog(m_window.get());
    for (int i = 0; i < 5; ++i) dialog.setColor(i, QColor(kColors[i]));
    QVERIFY(!dialog.acceptButton()->isEnabled());  // cinque colori
    dialog.setColor(5, QColor(kColors[5]));
    QVERIFY(dialog.acceptButton()->isEnabled());
    QCOMPARE(dialog.paletteName(), QStringLiteral("Custom 1"));  // nome vuoto
    dialog.nameField()->setText(QStringLiteral(" DAWN "));
    QVERIFY(!dialog.acceptButton()->isEnabled());
    QVERIFY(!dialog.errorLabel()->isHidden());
    dialog.nameField()->setText(QStringLiteral("Dark dawn"));
    QVERIFY(dialog.acceptButton()->isEnabled());
    QVERIFY(dialog.errorLabel()->isHidden());
}

void CustomPalettesTest::cancelChangesNothing() {
    for (const bool escape : {false, true}) {
        onNextModal([escape](ModalDialog *modal) {
            if (auto *dialog = qobject_cast<NewPaletteDialog *>(modal))
                for (int i = 0; i < kColors.size(); ++i) dialog->setColor(i, QColor(kColors[i]));
            if (escape) QTest::keyClick(modal, Qt::Key_Escape);
            else modal->cancelButton()->click();
        });
        newPaletteButton()->click();
        QApplication::processEvents();
        QVERIFY(CustomPalettes::all().isEmpty());
        QCOMPARE(m_model->paletteId, NodeStyle::kDefaultPaletteId);
    }
}

void CustomPalettesTest::customGroupShowsTheCreatedPalettes() {
    const QString id = CustomPalettes::add(QStringLiteral("Bosco"), kColors);
    m_model->setPalette(id, kColors, QStringLiteral("Bosco"));
    QApplication::processEvents();
    int group = -1;
    QStringList tips;
    runPicker([&](PalettePicker *picker) {
        group = picker->currentGroup();
        for (auto *tile : picker->findChildren<QToolButton *>()) tips.append(tile->toolTip());
    });
    QCOMPARE(group, 3);  // si apre sul gruppo della palette del foglio
    QVERIFY(tips.contains(QStringLiteral("Bosco")));
}

void CustomPalettesTest::removeAsksAndKeepsTheSheetColors() {
    const QString id = CustomPalettes::add(QStringLiteral("Deserto"), kColors);
    m_model->setPalette(id, kColors, QStringLiteral("Deserto"));
    QApplication::processEvents();
    // Il tasto destro chiude il popup e chiede conferma: qui si prova l'azione con la conferma.
    for (const bool confirm : {false, true}) {
        QString question;
        onNextModal([&question, confirm](ModalDialog *modal) {
            for (auto *label : modal->findChildren<QLabel *>()) question += label->text();
            if (confirm) modal->acceptButton()->click();
            else modal->reject();
        });
        runPicker([id](PalettePicker *picker) {
            for (auto *tile : picker->findChildren<QToolButton *>()) {
                if (tile->toolTip() != QStringLiteral("Deserto")) continue;
                QTimer::singleShot(0, [] {
                    if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()); menu && !menu->actions().isEmpty()) {
                        menu->setActiveAction(menu->actions().front());
                        QTest::keyClick(menu, Qt::Key_Return);  // chiude QMenu::exec con la voce scelta
                    }
                });
                emit tile->customContextMenuRequested(QPoint(4, 4));
                return;
            }
        });
        QApplication::processEvents();
        QTRY_VERIFY_WITH_TIMEOUT(question.contains(QStringLiteral("Deserto")), 2000);
        QCOMPARE(CustomPalettes::find(id).has_value(), !confirm);
    }
    // Il foglio conserva colori e nome; la select dice che non e' installata.
    QCOMPARE(m_model->activePalette, kColors);
    QCOMPARE(m_model->paletteId, id);
    QCOMPARE(m_select->paletteName(), QStringLiteral("Deserto (not installed)"));
}

void CustomPalettesTest::unknownPaletteFromAFileCanBeAdded() {
    QJsonObject json = m_model->toJson();
    QJsonArray colors;
    for (const QString &color : kColors) colors.append(color);
    json.insert(QStringLiteral("palette"), colors);
    json.insert(QStringLiteral("paletteId"), QStringLiteral("custom-altrove"));
    json.insert(QStringLiteral("paletteName"), QStringLiteral("Altrove"));
    MindMapModel reopened;
    QVERIFY(reopened.fromJson(json));
    QCOMPARE(reopened.activePalette, kColors);  // stessi colori anche senza la palette installata
    QCOMPARE(reopened.paletteName, QStringLiteral("Altrove"));
    QVERIFY(m_model->fromJson(json));
    m_select->setPalette(m_model->paletteId, m_model->activePalette, m_model->paletteName);
    QCOMPARE(m_select->paletteName(), QStringLiteral("Altrove (not installed)"));
    runPicker([](PalettePicker *picker) {
        auto *add = picker->findChild<QToolButton *>(QStringLiteral("AddCurrentPalette"));
        QVERIFY(add);
        QVERIFY(add->text().contains(QStringLiteral("Altrove")));
        add->click();
    });
    QApplication::processEvents();
    QVERIFY(CustomPalettes::find(QStringLiteral("custom-altrove")).has_value());
    QCOMPARE(m_select->paletteName(), QStringLiteral("Altrove"));
}

// Foto del modale per il confronto a vista: solo con MENTIS_SNAPSHOT_DIR, da lanciare con una
// piattaforma che ha i caratteri (es. windows).
void CustomPalettesTest::snapshotForVisualReview() {
    const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR");
    if (dir.isEmpty()) QSKIP("MENTIS_SNAPSHOT_DIR non impostata");
    const QStringList colors{"#ff6b6b", "#ffb347", "#4ecdc4", "#556270", "#c7f464", "#9b59b6"};
    onNextModal([&](ModalDialog *modal) {
        auto *dialog = qobject_cast<NewPaletteDialog *>(modal);
        for (int i = 0; i < 4; ++i) dialog->setColor(i, QColor(colors[i]));
        QTest::qWait(200);
        modal->grab().save(dir + QStringLiteral("/new-palette.png"));
        modal->reject();
    });
    newPaletteButton()->click();
    QApplication::processEvents();
}

QTEST_MAIN(CustomPalettesTest)
#include "custom_palettes_test.moc"
