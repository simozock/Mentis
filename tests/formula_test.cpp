// FEAT-064 (docs/features/FEAT-064-formule-latex.md): formula LaTeX come contenuto del nodo, con il
// vero MainWindow: modale con anteprima ed errori, blocco sotto il testo, doppio clic, menu (copia del
// codice e dell'immagine, rimozione), formule nelle note, file e Markdown.
#include "formuladialog.h"
#include "formulatext.h"
#include "document.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "performance.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QLabel>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>

#include <memory>

class FormulaTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void insertFromTheMenuWithPreview();
    void invalidCodeShowsTheError();
    void doubleClickEditsTheFormula();
    void menuCopiesAndRemoves();
    void formulasInNotes();
    void fileAndMarkdownKeepTheFormula();
    void formulaTakesThePlaceOfTheImage();
    void titleKeepsInlineMath();
    void snapshotForVisualReview();

private:
    MindNode *addNode(const QString &title);
    NodeItem *itemFor(const MindNode *node) const;
    void onNextFormulaDialog(const std::function<void(FormulaDialog *)> &handle);
    void chooseInNextMenu(const QString &text);
    void flushHistory();
    void undo();

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    MindMapView *m_view = nullptr;
};

void FormulaTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisFormulaTest"));
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

void FormulaTest::cleanupTestCase() { m_window.reset(); }

void FormulaTest::init() {
    m_scene->clearSelection();
    flushHistory();
}

void FormulaTest::flushHistory() {
    QApplication::processEvents();
    // The open history step is closed at once instead of waiting for its 150 ms timer.
    m_window->findChild<Document *>()->flushPendingStep();
    QApplication::processEvents();
}

void FormulaTest::undo() {
    for (auto *action : m_window->actions())
        if (action->shortcuts().contains(QKeySequence(QKeySequence::Undo))) { action->trigger(); break; }
    QApplication::processEvents();
}

MindNode *FormulaTest::addNode(const QString &title) {
    MindNode *node = m_model->addChild(m_model->root(), title);
    m_scene->autoLayout();
    flushHistory();
    return node;
}

NodeItem *FormulaTest::itemFor(const MindNode *node) const {
    for (auto *item : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node) return nodeItem;
    return nullptr;
}

void FormulaTest::onNextFormulaDialog(const std::function<void(FormulaDialog *)> &handle) {
    auto *timer = new QTimer(this);
    auto attempts = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, this, [timer, attempts, handle] {
        if (auto *dialog = qobject_cast<FormulaDialog *>(QApplication::activeModalWidget())) {
            timer->stop();
            timer->deleteLater();
            handle(dialog);
        } else if (++*attempts > 200) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start(10);
}

void FormulaTest::chooseInNextMenu(const QString &text) {
    QTimer::singleShot(200, this, [text] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) return;
        for (QAction *action : menu->actions())
            if (action->text() == text) {
                menu->setActiveAction(action);
                QTest::keyClick(menu, Qt::Key_Return);
                return;
            }
        menu->close();
    });
}

void FormulaTest::insertFromTheMenuWithPreview() {
    MindNode *node = addNode(QStringLiteral("Area del cerchio"));
    NodeItem *item = itemFor(node);
    const qreal heightBefore = item->boundingRect().height();
    m_scene->selectNode(node);
    QApplication::processEvents();
    QAction *insert = nullptr;
    for (auto *menu : m_window->findChildren<QMenu *>())
        for (auto *action : menu->actions())
            if (action->text() == QStringLiteral("LaTeX formula…") && !insert) insert = action;
    QVERIFY(insert);
    bool sawValid = false;
    onNextFormulaDialog([&sawValid](FormulaDialog *dialog) {
        QVERIFY(!dialog->acceptButton()->isEnabled());  // vuota
        QTest::keyClicks(dialog->field(), QStringLiteral("A = \\pi r^2"));
        sawValid = dialog->isValid() && dialog->acceptButton()->isEnabled();
        dialog->acceptButton()->click();
    });
    insert->trigger();
    QApplication::processEvents();
    QVERIFY(sawValid);
    QCOMPARE(node->formula, QStringLiteral("A = \\pi r^2"));
    QCOMPARE(node->title, QStringLiteral("Area del cerchio"));  // il titolo resta testo
    const QRectF formula = item->formulaRect();
    QVERIFY(!formula.isEmpty());
    QVERIFY(formula.center().y() > item->shapeRect().center().y());  // nuova: sotto il testo, dentro il nodo
    QVERIFY(formula.bottom() <= item->shapeRect().bottom());
    QCOMPARE(node->imagePlacement, MindNode::ImagePlacement::Below);
    QCOMPARE(node->formulaBackground, QColor(Qt::white));
    QVERIFY(item->boundingRect().height() > heightBefore);
    flushHistory();
    undo();  // un passo (formula e ridisposizione)
    QVERIFY(node->formula.isEmpty());
    QVERIFY(item->formulaRect().isEmpty());
}

void FormulaTest::invalidCodeShowsTheError() {
    FormulaDialog dialog(m_window.get(), QString());
    QTest::keyClicks(dialog.field(), QStringLiteral("\\frac{a}{b"));
    QVERIFY(!dialog.isValid());
    QVERIFY(!dialog.acceptButton()->isEnabled());
    QVERIFY(!dialog.errorLabel()->text().isEmpty());
    QTest::keyClicks(dialog.field(), QStringLiteral("}"));
    QVERIFY(dialog.isValid());
    QVERIFY(dialog.errorLabel()->text().isEmpty());
}

void FormulaTest::doubleClickEditsTheFormula() {
    MindNode *node = addNode(QStringLiteral("Pitagora"));
    node->formula = QStringLiteral("a^2 + b^2 = c^2");
    m_scene->updateNode(node);
    m_scene->autoLayout();
    QApplication::processEvents();
    NodeItem *item = itemFor(node);
    const QPoint point = m_view->mapFromScene(item->mapToScene(item->formulaRect().center()));
    QString seen;
    onNextFormulaDialog([&seen](FormulaDialog *dialog) {
        seen = dialog->code();
        dialog->field()->setPlainText(QStringLiteral("c = \\sqrt{a^2 + b^2}"));
        dialog->acceptButton()->click();
    });
    QTest::mouseDClick(m_view->viewport(), Qt::LeftButton, {}, point);
    QApplication::processEvents();
    QCOMPARE(seen, QStringLiteral("a^2 + b^2 = c^2"));
    QCOMPARE(node->formula, QStringLiteral("c = \\sqrt{a^2 + b^2}"));
    QVERIFY(!item->isEditing());  // il titolo non si apre
}

void FormulaTest::menuCopiesAndRemoves() {
    MindNode *node = addNode(QStringLiteral("Somma"));
    node->formula = QStringLiteral("\\sum_{i=1}^{n} i");
    m_scene->updateNode(node);
    m_scene->autoLayout();
    QApplication::processEvents();
    NodeItem *item = itemFor(node);
    const QPoint point = m_view->mapFromScene(item->mapToScene(item->formulaRect().center()));
    const auto rightClick = [this, point] {
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, m_view->viewport()->mapToGlobal(point));
        QApplication::sendEvent(m_view->viewport(), &event);
        QApplication::processEvents();
    };
    QGuiApplication::clipboard()->clear();
    chooseInNextMenu(QStringLiteral("Copy LaTeX code"));
    rightClick();
    QCOMPARE(QGuiApplication::clipboard()->text(), node->formula);
    chooseInNextMenu(QStringLiteral("Copy as image"));
    rightClick();
    QVERIFY(!QGuiApplication::clipboard()->image().isNull());
    chooseInNextMenu(QStringLiteral("Remove formula"));
    rightClick();
    QVERIFY(node->formula.isEmpty());
    QVERIFY(item->formulaRect().isEmpty());
}

void FormulaTest::formulasInNotes() {
    QTextDocument document;
    document.setPlainText(QStringLiteral("area $\\pi r^2$ e lato $x$; costa 5$ o 10$"));
    QFont font;
    font.setPixelSize(14);
    QVERIFY(FormulaText::renderInDocument(&document, font, Qt::black));
    int images = 0;
    for (auto it = document.begin().begin(); !it.atEnd(); ++it)
        if (it.fragment().charFormat().isImageFormat()) ++images;
    QCOMPARE(images, 2);
    QVERIFY(document.toPlainText().startsWith(QStringLiteral("area ")));
    QVERIFY(document.toPlainText().endsWith(QStringLiteral("costa 5$ o 10$")));  // i prezzi restano testo
}

void FormulaTest::fileAndMarkdownKeepTheFormula() {
    MindMapModel model;
    MindNode *node = model.addChild(model.root(), QStringLiteral("Energia"));
    node->formula = QStringLiteral("E = mc^2");
    MindMapModel reopened;
    QVERIFY(reopened.fromJson(model.toJson()));
    QCOMPARE(reopened.root()->children.front()->formula, node->formula);
    const QString markdown = model.toMarkdown(false);
    QVERIFY2(markdown.contains(QStringLiteral("$$E = mc^2$$")), qPrintable(markdown));
    MindMapModel imported;
    QVERIFY(imported.fromMarkdown(markdown));
    const MindNode *back = imported.root()->children.front().get();
    QCOMPARE(back->formula, node->formula);
    QVERIFY(back->notes.isEmpty());  // non diventa una nota
}

// Richiesta dell'utente (2026-10-19): la formula sta nel posto dell'immagine (sopra, sotto, a sinistra
// o a destra del testo, trascinandola), con lo sfondo bianco o scelto; immagine e formula si escludono.
void FormulaTest::formulaTakesThePlaceOfTheImage() {
    MindNode *node = addNode(QStringLiteral("Con immagine"));
    QImage picture(60, 40, QImage::Format_RGB32);
    picture.fill(Qt::red);
    const QString png = QDir::temp().filePath(QStringLiteral("mentis-formula-test.png"));
    QVERIFY(picture.save(png));
    m_scene->selectNode(node);
    m_window->setImageFilePicker([png] { return png; });
    m_window->chooseImage();
    QApplication::processEvents();
    QVERIFY(!node->imagePath.isEmpty());
    // La formula prende il posto dell'immagine.
    onNextFormulaDialog([](FormulaDialog *dialog) {
        dialog->field()->setPlainText(QStringLiteral("e^{i\\pi} + 1 = 0"));
        dialog->acceptButton()->click();
    });
    m_window->editFormula(node);
    QApplication::processEvents();
    QVERIFY(node->imagePath.isEmpty());
    QCOMPARE(node->formula, QStringLiteral("e^{i\\pi} + 1 = 0"));
    NodeItem *item = itemFor(node);
    QVERIFY(!item->formulaRect().isEmpty());  // nel posto dell'immagine (formulaRect = imageRect)
    // A sinistra del testo come un'immagine.
    node->imagePlacement = MindNode::ImagePlacement::Left;
    m_scene->updateNode(node);
    QApplication::processEvents();
    QVERIFY(item->formulaRect().right() < item->shapeRect().center().x());
    // Sfondo bianco nel riquadro; un colore scelto e il testo leggibile.
    const QImage white = FormulaText::boxImage(node->formula, NodeItem::titleFont(node), Qt::white, Qt::black, node->imageSize, 1.0);
    QCOMPARE(white.pixelColor(6, white.height() / 2), QColor(Qt::white));
    QCOMPARE(FormulaText::textColorOn(QColor("#1b1a5c"), Qt::black), QColor(Qt::white));
    QCOMPARE(FormulaText::textColorOn(QColor(0, 0, 0, 0), QColor("#ff0000")), QColor("#ff0000"));  // trasparente
    // E l'immagine riprende il posto della formula.
    m_scene->selectNode(node);
    m_window->chooseImage();
    QApplication::processEvents();
    QVERIFY(node->formula.isEmpty());
    QVERIFY(!node->imagePath.isEmpty());
    QFile::remove(png);
}

// Richiesta dell'utente (2026-10-19): il LaTeX fra $ nel titolo resta parte del testo (in riga),
// non diventa il contenuto nel posto dell'immagine.
void FormulaTest::titleKeepsInlineMath() {
    MindNode *node = addNode(QStringLiteral("Area $\\pi r^2$ del cerchio"));
    NodeItem *item = itemFor(node);
    QVERIFY(node->formula.isEmpty());
    QVERIFY(item->formulaRect().isEmpty());
    QVERIFY(FormulaText::containsMath(node->title));
}

// Foto per il confronto a vista (solo con MENTIS_SNAPSHOT_DIR, piattaforma con i caratteri).
void FormulaTest::snapshotForVisualReview() {
    const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR");
    if (dir.isEmpty()) QSKIP("MENTIS_SNAPSHOT_DIR non impostata");
    m_model->clear(QStringLiteral("Fisica"));
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("Energia"));
    MindNode *b = m_model->addChild(m_model->root(), QStringLiteral("Moto uniformemente accelerato"));
    MindNode *c = m_model->addChild(m_model->root(), QStringLiteral("Note"));
    a->formula = QStringLiteral("E = mc^2");
    b->formula = QStringLiteral("x(t) = x_0 + v_0 t + \\frac{1}{2} a t^2");
    c->notes = QStringLiteral("La velocita' e' $v = \\frac{\\Delta x}{\\Delta t}$ in m/s.");
    m_model->addChild(m_model->root(), QStringLiteral("Area $\\pi r^2$ del cerchio"));  // formula nel testo del titolo
    for (MindNode *node : {a, b}) {
        node->imageSize = NodeItem::formulaInsertionSize(node);
        node->imagePlacement = MindNode::ImagePlacement::Below;
    }
    b->imagePlacement = MindNode::ImagePlacement::Left;
    a->formulaBackground = QColor("#fff3a3");
    for (MindNode *node : {a, b, c}) m_scene->updateNode(node);
    m_scene->autoLayout();
    m_view->centerOn(itemFor(m_model->root()));
    QApplication::processEvents();
    QTest::qWait(300);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/formula-map.png")));
    onNextFormulaDialog([dir, this](FormulaDialog *dialog) {
        QTest::qWait(300);
        dialog->grab().save(dir + QStringLiteral("/formula-dialog.png"));
        dialog->field()->setPlainText(QStringLiteral("\\frac{1}{2"));
        QTest::qWait(200);
        dialog->grab().save(dir + QStringLiteral("/formula-dialog-error.png"));
        dialog->reject();
    });
    m_window->editFormula(b);
}

QTEST_MAIN(FormulaTest)
#include "formula_test.moc"
