// FEAT-066 tappa 1: regressione di file, fogli, storico e salvataggio automatico vista dalla
// finestra, scritta PRIMA di spostare lo stato del documento fuori da MainWindow. Deve passare
// identica prima e dopo lo spostamento. Vero MainWindow, piattaforma offscreen, impostazioni e
// cartelle di prova separate.
#include "bmapstorage.h"
#include "document.h"
#include "floatingui.h"
#include "ganttview.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "performance.h"
#include "uitheme.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <memory>

namespace {
// Oltre il timer dello storico (150 ms).
constexpr int kHistoryWaitMs = 250;

QStringList childTitles(const MindNode *node) {
    QStringList titles;
    for (const auto &child : node->children) titles.append(child->title);
    return titles;
}
}  // namespace

class DocumentFlowTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void saveAsThenReopenKeepsTheMap();
    void homePreviewShowsTheLastView();
    void explicitSaveClearsModified();
    void autosaveWritesANamedDocument();
    void sheetsKeepTheirContentAndUndoStaysInItsSheet();
    void reopenedSheetsAreReadWhenOpened();
    void deleteSheetAfterConfirmation();
    void renameSheetFromTheMenu();
    void duplicateSheetFromTheMenu();
    void reorderSheetsByDragging();
    void openingAnotherFileKeepsTheCurrentOneIfItFails();
    // Startup (2026-11-04): the side panel and the Gantt view are built after the first frame, or
    // as soon as something needs them; closing hides the window before the closing save.
    void sidePanelAndGanttAreBuiltAfterTheFirstFrame();
    void sidePanelAndGanttAreBuiltWhenFirstNeeded();
    void closingHidesTheWindowBeforeSaving();
    // Misura (solo con MENTIS_TIMING=1): apertura, Ctrl+S e Ctrl+Z su 10.000 nodi, per
    // confrontare i tempi prima e dopo ogni tappa (criterio: nessun peggioramento oltre il 5%).
    void timingOnALargeMap();

private:
    QAction *action(const QString &text) const;
    QComboBox *sheetSelect() const;
    QToolButton *addSheetButton() const;
    void wait(int ms = kHistoryWaitMs) { QApplication::processEvents(); QTest::qWait(ms); }
    QString path(const QString &name) const { return m_dir.filePath(name); }
    // Nuova mappa vuota senza domande (nessuna modifica in sospeso).
    void freshMap();

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    QTemporaryDir m_dir;
    QString m_nextSavePath;
    QString m_nextOpenPath;
};

void DocumentFlowTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);  // copie di lavoro e cache fuori dai dati dell'utente
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisDocumentFlowTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Standard);  // salvataggio automatico dopo 750 ms
    QVERIFY(m_dir.isValid());
    m_window = std::make_unique<MainWindow>();
    m_window->setSaveFilePicker([this] { return m_nextSavePath; });
    m_window->setOpenFilePicker([this] { return m_nextOpenPath; });
    const QString seed = path(QStringLiteral("seme.md"));
    QFile seedFile(seed);
    QVERIFY(seedFile.open(QIODevice::WriteOnly));
    seedFile.write("- Seme\n");
    seedFile.close();
    m_window->setMarkdownFilePicker([seed] { return seed; });
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    auto *pages = qobject_cast<QStackedWidget *>(m_window->centralWidget());
    QVERIFY(pages);
    pages->setCurrentIndex(1);
    m_model = m_window->findChild<MindMapModel *>();
    QVERIFY(m_model);
    QVERIFY(action(QStringLiteral("Save")) && action(QStringLiteral("Save as…")) && action(QStringLiteral("Open…")));
    QVERIFY(action(QStringLiteral("Undo")) && sheetSelect() && addSheetButton());
}

void DocumentFlowTest::cleanupTestCase() { m_window.reset(); }

void DocumentFlowTest::sidePanelAndGanttAreBuiltAfterTheFirstFrame() {
    MainWindow window;
    QVERIFY(!window.findChild<QDockWidget *>(QStringLiteral("InspectorDock")));
    QVERIFY(!window.findChild<GanttView *>());
    window.resize(1400, 900);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(window.findChild<QDockWidget *>(QStringLiteral("InspectorDock")));
    QTRY_VERIFY(window.findChild<GanttView *>());
    QVERIFY(!window.findChild<QDockWidget *>(QStringLiteral("InspectorDock"))->isVisible());  // the Home is shown
    QVERIFY(!window.findChild<GanttView *>()->isVisible());
}

void DocumentFlowTest::sidePanelAndGanttAreBuiltWhenFirstNeeded() {
    MainWindow window;  // never shown, so no first frame
    // A dark map chosen before the Gantt exists: the Gantt built later takes the dark page.
    auto *model = window.findChild<MindMapModel *>();
    QVERIFY(model);
    model->setTheme(model->paletteId, model->activePalette, model->paletteName, -1, QColor("#0d0d0d"));
    auto *panelToggle = window.findChild<QToolButton *>(QStringLiteral("PanelToggle"));
    QVERIFY(panelToggle);
    panelToggle->click();
    QVERIFY(window.findChild<QDockWidget *>(QStringLiteral("InspectorDock")));
    QAbstractButton *ganttButton = nullptr;
    for (QAbstractButton *button : window.findChildren<QAbstractButton *>())
        if (button->text() == QStringLiteral("Gantt")) ganttButton = button;
    QVERIFY(ganttButton);
    ganttButton->click();
    auto *gantt = window.findChild<GanttView *>();
    QVERIFY(gantt);
    QCOMPARE(gantt->pageColor(), UiTheme::color(UiTheme::Token::SurfaceSunken));
}

void DocumentFlowTest::closingHidesTheWindowBeforeSaving() {
    MainWindow window;
    const QString file = path(QStringLiteral("closing.mentis"));
    window.setSaveFilePicker([file] { return file; });
    window.resize(1400, 900);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *document = window.findChild<Document *>();
    QVERIFY(document);
    MindMapModel *model = document->model();
    model->addChild(model->root(), QStringLiteral("Before"));
    wait();
    QAction *saveAs = nullptr;
    for (QAction *candidate : window.findChildren<QAction *>())
        if (candidate->text() == QStringLiteral("Save as…")) saveAs = candidate;
    QVERIFY(saveAs);
    saveAs->trigger();
    wait();
    QVERIFY(QFileInfo::exists(file));
    model->addChild(model->root(), QStringLiteral("Saved at closing"));
    // The closing save asks for the Home preview: by then the window has left the screen.
    bool shownWhileSaving = true;
    document->setPreviewProvider([&] { shownWhileSaving = window.isVisible(); return QByteArray(); });
    QVERIFY(window.close());
    QVERIFY(!shownWhileSaving);
    MindMapModel onDisk;
    QVERIFY(BmapStorage::load(file, onDisk));
    QVERIFY(childTitles(onDisk.root()).contains(QStringLiteral("Saved at closing")));
}

QAction *DocumentFlowTest::action(const QString &text) const {
    for (auto *candidate : m_window->findChildren<QAction *>())
        if (candidate->text() == text) return candidate;
    return nullptr;
}

QComboBox *DocumentFlowTest::sheetSelect() const {
    return m_window->findChild<QComboBox *>(QStringLiteral("sheetSelect"));
}

QToolButton *DocumentFlowTest::addSheetButton() const {
    for (auto *button : m_window->findChildren<QToolButton *>())
        if (button->toolTip() == QStringLiteral("Add sheet")) return button;
    return nullptr;
}

void DocumentFlowTest::freshMap() {
    // Ogni prova parte da un documento nuovo con un solo foglio (Importa Markdown sostituisce il
    // documento), salvato in un file nuovo: nessuna domanda di salvataggio.
    static int counter = 0;
    if (m_window->isWindowModified()) { action(QStringLiteral("Save"))->trigger(); wait(); }
    action(QStringLiteral("Import Markdown…"))->trigger();
    wait();
    QCOMPARE(m_model->root()->title, QStringLiteral("Seme"));
    QCOMPARE(sheetSelect()->count(), 1);
    m_nextSavePath = path(QStringLiteral("base%1.mentis").arg(++counter));
    QVERIFY(action(QStringLiteral("Save as…")));
    action(QStringLiteral("Save as…"))->trigger();
    wait();
    QVERIFY(!m_window->isWindowModified());
}

// The Home's preview: the map on its theme's background (a dark variant is not white), a small Gantt
// when the Gantt was shown, the map again after the outliner (user, 2026-11-02).
void DocumentFlowTest::homePreviewShowsTheLastView() {
    freshMap();
    m_model->addChild(m_model->root(), QStringLiteral("Anteprima"));
    const QColor dark(QStringLiteral("#1e3a2a"));
    m_model->background = dark;
    m_model->refreshStyles();
    m_window->findChild<Document *>()->markChanged();
    wait();
    m_nextSavePath = path(QStringLiteral("anteprima.mentis"));
    action(QStringLiteral("Save as…"))->trigger();
    wait();
    const auto corner = [this] {
        QImage preview;
        preview.loadFromData(BmapStorage::loadPreview(m_nextSavePath), "JPG");

        return preview.isNull() ? QColor() : preview.pixelColor(2, 2);
    };
    const auto near = [](const QColor &a, const QColor &b) {
        return std::abs(a.red() - b.red()) < 12 && std::abs(a.green() - b.green()) < 12 && std::abs(a.blue() - b.blue()) < 12;
    };
    QVERIFY2(near(corner(), dark), qPrintable(corner().name()));  // JPEG: close, not exact
    QAction *gantt = action(QStringLiteral("Gantt chart"));
    QVERIFY(gantt);
    gantt->trigger();
    wait();
    action(QStringLiteral("Save"))->trigger();
    wait();
    QVERIFY2(!near(corner(), dark), qPrintable(corner().name()));  // the Gantt's page
    action(QStringLiteral("Switch between map and outliner"))->trigger();  // from the Gantt to the outliner
    wait();
    action(QStringLiteral("Save"))->trigger();
    wait();
    QVERIFY2(near(corner(), dark), qPrintable(corner().name()));  // the outliner shows the map
    action(QStringLiteral("Switch between map and outliner"))->trigger();
    wait();
}

void DocumentFlowTest::saveAsThenReopenKeepsTheMap() {
    freshMap();
    m_model->addChild(m_model->root(), QStringLiteral("Uno"));
    m_model->addChild(m_model->root(), QStringLiteral("Due"));
    wait();
    QVERIFY(m_window->isWindowModified());
    m_nextSavePath = path(QStringLiteral("mappa.mentis"));
    action(QStringLiteral("Save as…"))->trigger();
    wait();
    QVERIFY(QFileInfo::exists(m_nextSavePath));
    QVERIFY(!m_window->isWindowModified());
    QVERIFY(m_window->windowTitle().contains(QStringLiteral("mappa.mentis")));
    // Il file sul disco ha i nodi.
    MindMapModel onDisk;
    QVERIFY(BmapStorage::load(m_nextSavePath, onDisk));
    QVERIFY(childTitles(onDisk.root()).contains(QStringLiteral("Due")));
    // Riapertura dello stesso file da "Apri…".
    m_nextOpenPath = m_nextSavePath;
    action(QStringLiteral("Open…"))->trigger();
    wait();
    QVERIFY(childTitles(m_model->root()).contains(QStringLiteral("Uno")));
    QVERIFY(!m_window->isWindowModified());
    QVERIFY(!action(QStringLiteral("Undo"))->isEnabled());  // storico nuovo dopo l'apertura
}

void DocumentFlowTest::explicitSaveClearsModified() {
    freshMap();
    m_model->addChild(m_model->root(), QStringLiteral("Salvato con Ctrl+S"));
    wait();
    QVERIFY(m_window->isWindowModified());
    action(QStringLiteral("Save"))->trigger();
    wait();
    QVERIFY(!m_window->isWindowModified());
    MindMapModel onDisk;
    QVERIFY(BmapStorage::load(m_nextSavePath, onDisk));
    QVERIFY(childTitles(onDisk.root()).contains(QStringLiteral("Salvato con Ctrl+S")));
    // Il passo resta annullabile dopo il salvataggio, e annullarlo rende il documento modificato.
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QVERIFY(!childTitles(m_model->root()).contains(QStringLiteral("Salvato con Ctrl+S")));
    QVERIFY(m_window->isWindowModified());
}

void DocumentFlowTest::autosaveWritesANamedDocument() {
    freshMap();
    m_model->addChild(m_model->root(), QStringLiteral("Automatico"));
    wait();
    QVERIFY(m_window->isWindowModified());
    // Salvataggio automatico dopo 750 ms (profilo Standard) sul suo thread.
    QTRY_VERIFY_WITH_TIMEOUT(!m_window->isWindowModified(), 5000);
    MindMapModel onDisk;
    QVERIFY(BmapStorage::load(m_nextSavePath, onDisk));
    QVERIFY(childTitles(onDisk.root()).contains(QStringLiteral("Automatico")));
}

void DocumentFlowTest::sheetsKeepTheirContentAndUndoStaysInItsSheet() {
    freshMap();
    QComboBox *sheets = sheetSelect();
    const int first = sheets->currentIndex();
    m_model->addChild(m_model->root(), QStringLiteral("Nel primo"));
    wait();
    // FEAT-026 fase D: "+ foglio" apre la galleria; si conferma la prima variante.
    QTimer::singleShot(0, [] { if (auto *gallery = qobject_cast<QDialog *>(QApplication::activeModalWidget())) gallery->accept(); });
    addSheetButton()->click();
    wait();
    const int second = sheets->currentIndex();
    QVERIFY(second != first);
    QCOMPARE(sheets->count(), first + 2);
    QCOMPARE(childTitles(m_model->root()).size(), 4);  // FEAT-026 fase D: i quattro rami principali del riferimento
    m_model->addChild(m_model->root(), QStringLiteral("Nel secondo"));
    wait();
    // Tornati al primo foglio, il suo contenuto e' quello di prima.
    emit sheets->activated(first);
    wait();
    QCOMPARE(sheets->currentIndex(), first);
    QVERIFY(childTitles(m_model->root()).contains(QStringLiteral("Nel primo")));
    // L'ultimo passo e' nel secondo foglio: Ctrl+Z qui non cambia nulla e lo dice.
    const QStringList before = childTitles(m_model->root());
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QCOMPARE(childTitles(m_model->root()), before);
    QVERIFY(m_window->statusBar()->currentMessage().contains(QStringLiteral("sheet")));
    // Nel secondo foglio Ctrl+Z annulla il suo passo.
    emit sheets->activated(second);
    wait();
    QVERIFY(childTitles(m_model->root()).contains(QStringLiteral("Nel secondo")));
    action(QStringLiteral("Undo"))->trigger();
    wait();
    QVERIFY(!childTitles(m_model->root()).contains(QStringLiteral("Nel secondo")));
    // Salvataggio: entrambi i fogli nel file.
    action(QStringLiteral("Save"))->trigger();
    wait();
    QCOMPARE(BmapStorage::maps(m_nextSavePath).size(), 2);
}

void DocumentFlowTest::reopenedSheetsAreReadWhenOpened() {
    freshMap();
    m_model->addChild(m_model->root(), QStringLiteral("Primo foglio"));
    wait();
    // FEAT-026 fase D: "+ foglio" apre la galleria; si conferma la prima variante.
    QTimer::singleShot(0, [] { if (auto *gallery = qobject_cast<QDialog *>(QApplication::activeModalWidget())) gallery->accept(); });
    addSheetButton()->click();
    wait();
    m_model->addChild(m_model->root(), QStringLiteral("Secondo foglio"));
    wait();
    action(QStringLiteral("Save"))->trigger();
    wait();
    const QString file = m_nextSavePath;
    // Riaperto: si mostra il primo foglio; il secondo si legge quando lo si apre.
    m_nextOpenPath = file;
    action(QStringLiteral("Open…"))->trigger();
    wait();
    QComboBox *sheets = sheetSelect();
    QCOMPARE(sheets->count(), 2);
    QCOMPARE(sheets->currentIndex(), 0);
    QVERIFY(childTitles(m_model->root()).contains(QStringLiteral("Primo foglio")));
    emit sheets->activated(1);
    wait();
    QCOMPARE(sheets->currentIndex(), 1);
    QVERIFY(childTitles(m_model->root()).contains(QStringLiteral("Secondo foglio")));
    QVERIFY(!m_window->isWindowModified());  // cambiare foglio non e' una modifica
}

// FEAT-004: "Delete sheet" asks first; Cancel keeps the sheet, Delete removes the active one and a
// topic link into it turns grey (FEAT-027).
void DocumentFlowTest::deleteSheetAfterConfirmation() {
    freshMap();
    QAction *remove = action(QStringLiteral("Delete sheet"));
    QVERIFY(remove);
    addSheetButton()->click();
    wait();
    QComboBox *sheets = sheetSelect();
    QCOMPARE(sheets->count(), 2);
    const QString target = m_model->root()->children.front()->id;
    emit sheets->activated(0);
    wait();
    m_model->root()->topicLink = target;
    emit sheets->activated(1);
    wait();
    QTimer::singleShot(0, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
    remove->trigger();
    wait();
    QCOMPARE(sheets->count(), 2);
    QTimer::singleShot(0, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->accept(); });
    remove->trigger();
    wait();
    QCOMPARE(sheets->count(), 1);
    QCOMPARE(m_model->root()->title, QStringLiteral("Seme"));
    QVERIFY(m_window->isWindowModified());
    QVERIFY(m_window->findChild<MindMapScene *>()->isTopicLinkBroken(m_model->root()));
}

// FEAT-004: Edit > "Duplicate sheet" adds "<name> (copy)" after the active sheet and shows it.
void DocumentFlowTest::duplicateSheetFromTheMenu() {
    freshMap();
    QAction *duplicate = action(QStringLiteral("Duplicate sheet"));
    QVERIFY(duplicate);
    QComboBox *sheets = sheetSelect();
    const QString before = sheets->currentText();
    m_model->addChild(m_model->root(), QStringLiteral("Copiato"));
    m_window->findChild<Document *>()->markChanged();
    duplicate->trigger();
    wait();
    QCOMPARE(sheets->count(), 2);
    QCOMPARE(sheets->currentIndex(), 1);
    QCOMPARE(sheets->currentText(), before + QStringLiteral(" (copy)"));
    QCOMPARE(m_model->root()->children.size(), size_t(1));
    QCOMPARE(m_model->root()->children[0]->title, QStringLiteral("Copiato"));
    QVERIFY(m_window->isWindowModified());
}

// FEAT-004: dragging a row of the open sheet list moves the sheet; the list stays open, the active
// sheet stays shown and a plain click still picks a sheet.
void DocumentFlowTest::reorderSheetsByDragging() {
    freshMap();
    addSheetButton()->click();
    addSheetButton()->click();
    wait();
    auto *sheets = dynamic_cast<FloatingSelect *>(sheetSelect());
    QVERIFY(sheets);
    QCOMPARE(sheets->currentIndex(), 2);
    const QString active = sheets->currentText();
    const QString first = sheets->itemText(0);
    sheets->showPopup();
    auto *popup = dynamic_cast<FloatingListPopup *>(QApplication::activePopupWidget());
    QVERIFY(popup);
    auto *list = popup->findChild<QListWidget *>();
    QWidget *viewport = list->viewport();
    const auto send = [viewport](QEvent::Type type, const QPoint &at, Qt::MouseButtons buttons) {
        QMouseEvent event(type, at, viewport->mapToGlobal(at), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          buttons, Qt::NoModifier);
        QApplication::sendEvent(viewport, &event);
    };
    // Row 0 dropped below the last row: the first sheet goes last.
    const QPoint from = list->visualItemRect(list->item(0)).center();
    const QPoint to = list->visualItemRect(list->item(2)).center() + QPoint(0, FloatingListPopup::kRowHeight / 4);
    send(QEvent::MouseButtonPress, from, Qt::LeftButton);
    send(QEvent::MouseMove, from + QPoint(0, 10), Qt::LeftButton);
    send(QEvent::MouseMove, to, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, to, Qt::NoButton);
    QCOMPARE(QApplication::activePopupWidget(), popup);  // still open
    QCOMPARE(list->item(2)->text(), first);
    QCOMPARE(list->currentItem()->text(), active);
    QCOMPARE(sheets->itemText(2), first);
    QCOMPARE(sheets->currentText(), active);
    QCOMPARE(sheets->currentIndex(), 1);
    QVERIFY(m_window->isWindowModified());
    // Esc cancels a drag: nothing moves, the list stays open and the release does not pick the row.
    const QPoint second = list->visualItemRect(list->item(0)).center();
    send(QEvent::MouseButtonPress, second, Qt::LeftButton);
    send(QEvent::MouseMove, to, Qt::LeftButton);
    QTest::keyClick(list, Qt::Key_Escape);
    send(QEvent::MouseButtonRelease, second, Qt::NoButton);
    QCOMPARE(QApplication::activePopupWidget(), popup);
    QCOMPARE(sheets->itemText(2), first);
    QCOMPARE(sheets->currentText(), active);
    // A plain click picks the row and closes the list.
    const QPoint pick = list->visualItemRect(list->item(2)).center();
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, pick);
    wait();
    QVERIFY(!QApplication::activePopupWidget());
    QCOMPARE(sheets->currentText(), first);
}

// FEAT-004: Edit > "Rename sheet…" opens the dialog with the name; Cancel keeps it, Rename changes
// the select and marks the document modified.
void DocumentFlowTest::renameSheetFromTheMenu() {
    freshMap();
    QAction *rename = action(QStringLiteral("Rename sheet…"));
    QVERIFY(rename);
    QComboBox *sheets = sheetSelect();
    const QString before = sheets->currentText();
    QString shown;
    QTimer::singleShot(0, [&shown] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        shown = dialog->findChild<QLineEdit *>()->text();
        dialog->reject();
    });
    rename->trigger();
    wait();
    QCOMPARE(shown, before);
    QCOMPARE(sheets->currentText(), before);
    QTimer::singleShot(0, [] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        dialog->findChild<QLineEdit *>()->setText(QStringLiteral("Ripasso"));
        dialog->accept();
    });
    rename->trigger();
    wait();
    QCOMPARE(sheets->currentText(), QStringLiteral("Ripasso"));
    QVERIFY(m_window->isWindowModified());
}

void DocumentFlowTest::openingAnotherFileKeepsTheCurrentOneIfItFails() {
    freshMap();
    m_model->addChild(m_model->root(), QStringLiteral("Resta qui"));
    wait();
    action(QStringLiteral("Save"))->trigger();
    wait();
    const QString title = m_window->windowTitle();
    // File che non e' un documento Mentis: errore (finestra chiusa dal test) e nulla cambia.
    const QString broken = path(QStringLiteral("rotto.mentis"));
    QFile file(broken);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("non sono un database");
    file.close();
    QTimer::singleShot(200, this, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
    m_nextOpenPath = broken;
    action(QStringLiteral("Open…"))->trigger();
    wait(400);
    QVERIFY(childTitles(m_model->root()).contains(QStringLiteral("Resta qui")));
    QCOMPARE(m_window->windowTitle(), title);
}

void DocumentFlowTest::timingOnALargeMap() {
    if (qEnvironmentVariable("MENTIS_TIMING") != QLatin1String("1")) QSKIP("misura solo con MENTIS_TIMING=1");
    freshMap();
    // Mappa grande scritta direttamente su disco, poi aperta dalla finestra.
    MindMapModel large;
    for (int branch = 0; branch < 100; ++branch) {
        MindNode *parent = large.addChild(large.root(), QStringLiteral("Ramo %1").arg(branch));
        for (int leaf = 0; leaf < 99; ++leaf) large.addChild(parent, QStringLiteral("Foglia %1.%2").arg(branch).arg(leaf));
    }
    const QString file = path(QStringLiteral("grande.mentis"));
    QVERIFY(BmapStorage::save(file, large, BmapStorage::SaveReason::Explicit));
    QList<qint64> openMs, saveMs, undoMs;
    for (int round = 0; round < 3; ++round) {
        m_nextOpenPath = file;
        QElapsedTimer timer;
        timer.start();
        action(QStringLiteral("Open…"))->trigger();
        QApplication::processEvents();
        openMs.append(timer.elapsed());
        m_model->addChild(m_model->root(), QStringLiteral("Modifica"));
        wait();
        timer.restart();
        action(QStringLiteral("Save"))->trigger();
        saveMs.append(timer.elapsed());
        timer.restart();
        action(QStringLiteral("Undo"))->trigger();
        QApplication::processEvents();
        undoMs.append(timer.elapsed());
        action(QStringLiteral("Save"))->trigger();
        wait();
    }
    qInfo("TIMING open_ms=%lld,%lld,%lld save_ms=%lld,%lld,%lld undo_ms=%lld,%lld,%lld", openMs[0], openMs[1], openMs[2],
          saveMs[0], saveMs[1], saveMs[2], undoMs[0], undoMs[1], undoMs[2]);
}

QTEST_MAIN(DocumentFlowTest)
#include "document_flow_test.moc"
