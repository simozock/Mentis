// BUG-001 (docs/bugs/BUG-001-testo-scompare-selezione-outliner.md): selezionando nell'Outliner il
// testo non deve uscire dalla vista del campo di modifica, in nessuna direzione di selezione.
// Il test usa il vero MainWindow (campi e delegate sono interni a mainwindow.cpp) con la
// piattaforma offscreen e impostazioni separate da quelle dell'applicazione.
#include "bmapstorage.h"
#include "mainwindow.h"
#include "mapfolder.h"
#include "mindmapmodel.h"

#include <QAction>
#include <QApplication>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>

#include <memory>

namespace {
// Conta le pressioni del mouse che risalgono dal campo all'outliner. Dalla seconda in poi le
// ferma: senza il blocco un campo che ignora il clic genera una ricorsione con forwardMouse.
class PressCounter final : public QObject {
public:
    int presses = 0;

protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() != QEvent::MouseButtonPress) return false;
        return ++presses > 1;
    }
};

// Pixel scuri (testo) nell'area visibile del campo. Il campo è trasparente: si cattura l'area
// dal widget genitore, dove è composto sopra la riga o la pagina.
int inkPixels(QPlainTextEdit *edit) {
    QWidget *host = edit->parentWidget();
    const QRect area(edit->viewport()->mapTo(host, QPoint()), edit->viewport()->size());
    const QImage image = host->grab(area).toImage();
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qGray(image.pixel(x, y)) < 110) ++count;
    return count;
}

// Selezione costruita direttamente: ancora alla fine del testo, posizione all'inizio.
void selectRightToLeft(QPlainTextEdit *edit) {
    QTextCursor cursor(edit->document());
    cursor.setPosition(edit->document()->characterCount() - 1);
    cursor.setPosition(0, QTextCursor::KeepAnchor);
    edit->setTextCursor(cursor);
}
}

class OutlinerSelectionTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void itemSelection_data();
    void itemSelection();
    void rootTitleSelection();
    void itemGrowsWhileTyping();
    void ignoredPressDoesNotRecurse();
    void rootTitleWholeAfterOpeningFile();

private:
    QPlainTextEdit *openItemEditor(const QString &title);
    void checkSelectionStaysVisible(QPlainTextEdit *edit, const QString &text, bool rightToLeft);

    std::unique_ptr<MainWindow> m_window;
    QTreeView *m_tree = nullptr;
    MindMapModel *m_model = nullptr;
};

void OutlinerSelectionTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisOutlinerSelectionTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    m_window = std::make_unique<MainWindow>();
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    // Pagina dell'editor e modalità Outliner, come dall'interfaccia.
    auto *pages = qobject_cast<QStackedWidget *>(m_window->centralWidget());
    QVERIFY(pages);
    pages->setCurrentIndex(1);
    QPushButton *outlinerButton = nullptr;
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Outliner")) outlinerButton = button;
    QVERIFY(outlinerButton);
    outlinerButton->click();
    for (auto *tree : m_window->findChildren<QTreeView *>())
        if (tree->isVisible() && tree->model()) m_tree = tree;
    QVERIFY(m_tree);
    m_model = qobject_cast<MindMapModel *>(m_tree->model());
    QVERIFY(m_model);
    QApplication::processEvents();
}

void OutlinerSelectionTest::cleanupTestCase() { m_window.reset(); }

QPlainTextEdit *OutlinerSelectionTest::openItemEditor(const QString &title) {
    // Una voce di primo livello con una figlia: si prova la figlia (peso normale).
    while (!m_model->root()->children.empty()) m_model->removeNode(m_model->root()->children.front().get());
    MindNode *parent = m_model->addChild(m_model->root(), QStringLiteral("Primo livello"));
    m_model->addChild(parent, title);
    m_tree->expandAll();
    QApplication::processEvents();
    const QModelIndex index = m_model->index(0, 0, m_model->index(0, 0));
    m_tree->setCurrentIndex(index);
    m_tree->edit(index);
    QApplication::processEvents();
    for (auto *edit : m_tree->viewport()->findChildren<QPlainTextEdit *>())
        if (edit->isVisible()) return edit;
    return nullptr;
}

void OutlinerSelectionTest::checkSelectionStaysVisible(QPlainTextEdit *edit, const QString &text, bool rightToLeft) {
    QScrollBar *vertical = edit->verticalScrollBar();
    // Il campo mostra sempre tutto il testo: non deve poter scorrere.
    QCOMPARE(vertical->maximum(), 0);

    // Trascinamento reale da un bordo all'altro e oltre il bordo, come quando si cerca di
    // prendere il primo (o l'ultimo) carattere.
    QWidget *viewport = edit->viewport();
    const int y = qMin(viewport->height() / 2, 8);
    const int left = 2, right = viewport->width() - 2;
    QTest::mousePress(viewport, Qt::LeftButton, {}, QPoint(rightToLeft ? right : left, y));
    for (int step = 1; step <= 10; ++step) {
        const int x = rightToLeft ? right - (right - left) * step / 10 : left + (right - left) * step / 10;
        QTest::mouseMove(viewport, QPoint(x, y));
    }
    QTest::mouseMove(viewport, QPoint(rightToLeft ? -6 : viewport->width() + 6, y));
    QTest::mouseRelease(viewport, Qt::LeftButton, {}, QPoint(rightToLeft ? left : right, y));
    QApplication::processEvents();
    QVERIFY(edit->textCursor().hasSelection());
    QCOMPARE(vertical->value(), 0);

    // Selezione con ancora a destra (o a sinistra) costruita direttamente.
    if (rightToLeft) {
        selectRightToLeft(edit);
    } else {
        QTextCursor cursor(edit->document());
        cursor.setPosition(edit->document()->characterCount() - 1, QTextCursor::KeepAnchor);
        edit->setTextCursor(cursor);
    }
    QApplication::processEvents();
    const QTextCursor selection = edit->textCursor();
    const int inkBefore = inkPixels(edit);
    QVERIFY(inkBefore > 0);

    // Passo dello scorrimento automatico di QPlainTextEdit quando, trascinando, il puntatore è
    // fuori dal campo e più in basso del suo bordo (QPlainTextEdit::timerEvent).
    vertical->triggerAction(QAbstractSlider::SliderSingleStepAdd);
    QApplication::processEvents();
    QCOMPARE(vertical->value(), 0);
    QCOMPARE(inkPixels(edit), inkBefore);
    QCOMPARE(edit->toPlainText(), text);
    QCOMPARE(edit->textCursor().anchor(), selection.anchor());
    QCOMPARE(edit->textCursor().position(), selection.position());

    // Un clic nel campo resta al campo: nessuna pressione deve risalire all'outliner.
    PressCounter counter;
    QWidget *host = edit->parentWidget();
    host->installEventFilter(&counter);
    QTest::mousePress(viewport, Qt::LeftButton, {}, QPoint(left + 20, y));
    host->removeEventFilter(&counter);
    QTest::mouseRelease(viewport, Qt::LeftButton, {}, QPoint(left + 20, y));
    QCOMPARE(counter.presses, 0);
    QCOMPARE(edit->toPlainText(), text);
}

void OutlinerSelectionTest::itemSelection_data() {
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("rightToLeft");
    const QString oneLine = QStringLiteral("Pianificazione del lavoro");
    // Abbastanza lungo da andare a capo nella colonna dell'outliner (larghezza della finestra 1400).
    const QString wrapped = QStringLiteral("Testo lungo che va a capo su piu' righe nella colonna centrale "
                                           "dell'outliner perche' la riga non basta a contenerlo tutto");
    QTest::newRow("una riga, destra-sinistra") << oneLine << true;
    QTest::newRow("una riga, sinistra-destra") << oneLine << false;
    QTest::newRow("piu' righe, destra-sinistra") << wrapped << true;
    QTest::newRow("piu' righe, sinistra-destra") << wrapped << false;
}

void OutlinerSelectionTest::itemSelection() {
    QFETCH(QString, text);
    QFETCH(bool, rightToLeft);
    QPlainTextEdit *edit = openItemEditor(text);
    QVERIFY(edit);
    checkSelectionStaysVisible(edit, text, rightToLeft);
    m_tree->setFocus();  // chiude il campo confermando
    QApplication::processEvents();
}

void OutlinerSelectionTest::rootTitleSelection() {
    QPlainTextEdit *root = nullptr;
    for (auto *edit : m_window->findChildren<QPlainTextEdit *>())
        if (edit->isVisible() && edit->parentWidget() != m_tree->viewport()) root = edit;
    QVERIFY(root);
    root->setFocus();
    const QString text = root->toPlainText();
    QScrollBar *vertical = root->verticalScrollBar();
    QCOMPARE(vertical->maximum(), 0);
    selectRightToLeft(root);
    QApplication::processEvents();
    const int inkBefore = inkPixels(root);
    QVERIFY(inkBefore > 0);
    vertical->triggerAction(QAbstractSlider::SliderSingleStepAdd);
    QApplication::processEvents();
    QCOMPARE(vertical->value(), 0);
    QCOMPARE(inkPixels(root), inkBefore);
    QCOMPARE(root->toPlainText(), text);
}

// Rischio della correzione: mentre si va a capo il campo è per un istante più basso del testo
// (prima QPlainTextEdit scorreva e updateEditorGeometry lo riportava in cima). La riga nuova deve
// risultare visibile dopo il ricalcolo dell'altezza della riga, senza scorrimento.
void OutlinerSelectionTest::itemGrowsWhileTyping() {
    QPlainTextEdit *edit = openItemEditor(QStringLiteral("Prima riga"));
    QVERIFY(edit);
    edit->moveCursor(QTextCursor::End);
    QTest::keyClick(edit, Qt::Key_Return, Qt::ShiftModifier);
    QTest::keyClicks(edit, QStringLiteral("Seconda riga"));
    QApplication::processEvents();
    QCOMPARE(edit->toPlainText(), QStringLiteral("Prima riga\nSeconda riga"));
    QCOMPARE(edit->verticalScrollBar()->value(), 0);
    const QRect first = edit->cursorRect(QTextCursor(edit->document()));
    const QRect last = edit->cursorRect(edit->textCursor());
    QVERIFY(edit->viewport()->rect().contains(first));
    QVERIFY(edit->viewport()->rect().contains(last));
    m_tree->setFocus();
    QApplication::processEvents();
}

// BUG-002: se il campo ignora una pressione del mouse, questa risale all'outliner, che la
// reinoltrava al campo (OutlineTreeView::forwardMouse) all'infinito fino a esaurire lo stack.
// Il campo che ignora il clic è simulato con un filtro; un secondo filtro sull'outliner ferma il
// ciclo dopo 50 passaggi, così prima della correzione il test fallisce invece di andare in crash.
void OutlinerSelectionTest::ignoredPressDoesNotRecurse() {
    class IgnorePresses final : public QObject {
    protected:
        bool eventFilter(QObject *, QEvent *event) override {
            if (event->type() != QEvent::MouseButtonPress) return false;
            event->ignore();  // come QPlainTextEdit quando sotto il puntatore non trova testo
            return true;
        }
    };
    class StopRecursion final : public QObject {
    public:
        int presses = 0;
    protected:
        bool eventFilter(QObject *, QEvent *event) override {
            if (event->type() != QEvent::MouseButtonPress || ++presses < 50) return false;
            event->accept();
            return true;
        }
    };
    QPlainTextEdit *edit = openItemEditor(QStringLiteral("Pianificazione del lavoro"));
    QVERIFY(edit);
    IgnorePresses ignore;
    StopRecursion stop;
    edit->viewport()->installEventFilter(&ignore);
    m_tree->viewport()->installEventFilter(&stop);
    QTest::mousePress(edit->viewport(), Qt::LeftButton, {}, QPoint(20, 8));
    m_tree->viewport()->removeEventFilter(&stop);
    edit->viewport()->removeEventFilter(&ignore);
    QTest::mouseRelease(edit->viewport(), Qt::LeftButton, {}, QPoint(20, 8));
    // Il clic risale una volta dal campo e una volta dalla copia inoltrata, poi si ferma.
    QVERIFY2(stop.presses <= 2, qPrintable(QStringLiteral("pressioni ricevute dall'outliner: %1").arg(stop.presses)));
    m_tree->setFocus();
    QApplication::processEvents();
}

// Bug 2026-11-03 (user's picture): opening a saved file and then the outliner, the main title
// showed only the lower half of its letters.
void OutlinerSelectionTest::rootTitleWholeAfterOpeningFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("vecchio.mentis"));
    MindMapModel saved;
    saved.root()->title = QStringLiteral("ciao");
    saved.addChild(saved.root(), QStringLiteral("Argomento principale 1"));
    saved.addChild(saved.addChild(saved.root(), QStringLiteral("Argomento principale 2")), QStringLiteral("Argomento principale 3"));
    QString error;
    QVERIFY2(BmapStorage::save(path, saved, BmapStorage::SaveReason::Explicit, &error), qPrintable(error));

    // A window of its own: the one of the other tests has changes and would ask to save them.
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.setOpenFilePicker([path] { return path; });
    MapFolder::setConfiguredPath(dir.path());  // FEAT-105: opened in place, never copied into the user's Documents
    QAction *open = nullptr;
    for (auto *candidate : window.findChildren<QAction *>())
        if (candidate->text() == QStringLiteral("Open…")) open = candidate;
    QVERIFY(open);
    open->trigger();
    QApplication::processEvents();
    QPushButton *outlinerButton = nullptr;
    for (auto *button : window.findChildren<QPushButton *>())
        if (button->isVisible() && button->text() == QStringLiteral("Outliner")) outlinerButton = button;
    QVERIFY(outlinerButton);
    outlinerButton->click();
    QApplication::processEvents();

    QPlainTextEdit *root = nullptr;
    for (auto *edit : window.findChildren<QPlainTextEdit *>())
        if (edit->isVisible() && edit->toPlainText() == QStringLiteral("ciao")) root = edit;
    QVERIFY(root);
    QTextCursor end(root->document());
    end.movePosition(QTextCursor::End);
    const QRect first = root->cursorRect(QTextCursor(root->document()));
    const QRect last = root->cursorRect(end);
    QVERIFY2(root->viewport()->rect().contains(first),
             qPrintable(QStringLiteral("viewport %1 px, first line from %2 to %3").arg(root->viewport()->height()).arg(first.top()).arg(first.bottom())));
    QVERIFY(root->viewport()->rect().contains(last));
}

QTEST_MAIN(OutlinerSelectionTest)
#include "outliner_selection_test.moc"
