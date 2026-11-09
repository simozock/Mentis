// FEAT-059 (docs/features/FEAT-059-note-del-nodo.md): note del nodo con il vero MainWindow: riquadro
// accanto al nodo con i formati base, icona nel posto a destra, un passo per sessione, "Mostra tutte
// le note", outliner, file e Markdown.
#include "document.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "noteeditor.h"
#include "performance.h"
#include "richresources.h"
#include "stylecontrols.h"
#include "toggleswitch.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QImage>
#include <QMimeData>
#include <QTemporaryDir>
#include <QUrl>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTest>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolButton>
#include <QTreeView>

#include <memory>

class NotesTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void menuOpensTheEditorNextToTheNode();
    void formatsAreSavedInOneStep();
    void iconOpensTheNoteAndSitsBeforeTheLinks();
    void emptyNoteRemovesTheIcon();
    void showAllNotesMakesRoomBelowTheNode();
    void outlinerShowsTheNoteBelowTheRow();
    void fileAndMarkdownKeepTheFormats();
    void externalImagesNeverLoad();
    void deeplyNestedTablesBecomeParagraphs();
    void pastedImagesAreEmbedded();
    void snapshotForVisualReview();

private:
    MindNode *addNode(const QString &title);
    NodeItem *itemFor(const MindNode *node) const;
    QAction *actionWithText(const QString &text) const;
    void flushHistory();
    void undo();

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    MindMapView *m_view = nullptr;
    NoteEditor *m_editor = nullptr;
};

void NotesTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisNotesTest"));
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
    m_editor = m_window->noteEditor();
    QVERIFY(m_model && m_view && m_scene && m_editor);
}

void NotesTest::cleanupTestCase() { m_window.reset(); }

void NotesTest::init() {
    // FEAT-012 (passo 4a): qui si provano misure e distanze verticali fra rami vicini, non i due lati
    // (tests/balanced_layout_test.cpp): tutti i rami a destra, la direzione "destra" della Mappa mentale.
    if (m_model->root()->structureDirection != QLatin1String("right")) {
        m_model->root()->structure = QStringLiteral("mindmap");
        m_model->root()->structureDirection = QStringLiteral("right");
        m_scene->autoLayout();
        // Passo a se': un Ctrl+Z della prova non deve toglierlo insieme alla sua modifica.
        m_window->findChild<Document *>()->flushPendingStep();
    }
    if (m_editor->isOpen()) m_editor->commitAndClose();
    m_scene->clearSelection();
    m_view->setZoomPercent(100);
    flushHistory();
}

void NotesTest::flushHistory() {
    QApplication::processEvents();
    // The open history step is closed at once instead of waiting for its 150 ms timer.
    m_window->findChild<Document *>()->flushPendingStep();
    QApplication::processEvents();
}

void NotesTest::undo() {
    for (auto *action : m_window->actions())
        if (action->shortcuts().contains(QKeySequence(QKeySequence::Undo))) { action->trigger(); break; }
    QApplication::processEvents();
}

MindNode *NotesTest::addNode(const QString &title) {
    MindNode *node = m_model->addChild(m_model->root(), title);
    m_scene->autoLayout();
    flushHistory();
    return node;
}

NodeItem *NotesTest::itemFor(const MindNode *node) const {
    for (auto *item : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node) return nodeItem;
    return nullptr;
}

QAction *NotesTest::actionWithText(const QString &text) const {
    for (auto *menu : m_window->findChildren<QMenu *>())
        for (auto *action : menu->actions())
            if (action->text().section(QLatin1Char('\t'), 0, 0) == text) return action;  // FEAT-041 stage B: the key after a tab
    return nullptr;
}

void NotesTest::menuOpensTheEditorNextToTheNode() {
    MindNode *node = addNode(QStringLiteral("Con nota"));
    m_scene->selectNode(node);
    QApplication::processEvents();
    QAction *note = actionWithText(QStringLiteral("Note"));
    QVERIFY(note);
    note->trigger();
    QApplication::processEvents();
    QVERIFY(m_editor->isOpen());
    QCOMPARE(m_editor->nodeId(), node->id);
    // Accanto al nodo: a destra (o a sinistra se non c'e' spazio), mai sopra il nodo.
    const QRect nodeRect = m_view->mapFromScene(m_scene->nodeSceneRect(node)).boundingRect();
    const QRect inWindow(m_view->viewport()->mapTo(m_window.get(), nodeRect.topLeft()), nodeRect.size());
    QVERIFY(!m_editor->geometry().intersects(inWindow.adjusted(4, 4, -4, -4)));
    m_editor->commitAndClose();  // vuota: niente nota
    QVERIFY(node->notes.isEmpty());
}

void NotesTest::formatsAreSavedInOneStep() {
    MindNode *node = addNode(QStringLiteral("Formati"));
    m_scene->selectNode(node);
    actionWithText(QStringLiteral("Note"))->trigger();
    QApplication::processEvents();
    QTextEdit *text = m_editor->textEdit();
    QTest::keyClicks(text, QStringLiteral("Importante"));
    text->selectAll();
    m_editor->formatButton(QStringLiteral("bold"))->click();
    text->moveCursor(QTextCursor::End);
    QTest::keyClick(text, Qt::Key_Return);
    m_editor->formatButton(QStringLiteral("bullets"))->click();
    QTest::keyClicks(text, QStringLiteral("primo punto"));
    QTest::keyClick(text, Qt::Key_Escape);  // chiude e salva
    QApplication::processEvents();
    QVERIFY(!m_editor->isOpen());
    QCOMPARE(node->notes, QStringLiteral("Importante\nprimo punto"));
    QVERIFY(node->notesHtml.contains(QStringLiteral("font-weight")));
    QVERIFY(node->notesHtml.contains(QStringLiteral("<ul")));
    QVERIFY(!itemFor(node)->noteRect().isEmpty());
    // Riaperta: stessi formati.
    emit m_scene->noteActivated(node);
    QApplication::processEvents();
    QVERIFY(m_editor->textEdit()->toHtml().contains(QStringLiteral("<ul")));
    m_editor->commitAndClose();
    flushHistory();
    undo();  // una sessione = un passo
    QVERIFY(node->notes.isEmpty());
    QVERIFY(itemFor(node)->noteRect().isEmpty());
}

void NotesTest::iconOpensTheNoteAndSitsBeforeTheLinks() {
    MindNode *node = addNode(QStringLiteral("Nota e collegamento"));
    node->notes = QStringLiteral("testo");
    node->webLink = QStringLiteral("https://example.com");
    m_scene->updateNode(node);
    m_scene->autoLayout();
    QApplication::processEvents();
    NodeItem *item = itemFor(node);
    const QRectF note = item->noteRect();
    const QRectF link = item->linkIconRect(NodeItem::LinkKind::Web);
    QVERIFY(!note.isEmpty() && !link.isEmpty());
    QCOMPARE(note.right() + NodeItem::kLinkIconGapPx, link.left());  // nota, poi collegamento
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, m_view->mapFromScene(item->mapToScene(note.center())));
    QApplication::processEvents();
    QVERIFY(m_editor->isOpen());
    QCOMPARE(m_editor->textEdit()->toPlainText(), QStringLiteral("testo"));
    // Collegamento sul testo selezionato.
    m_editor->textEdit()->selectAll();
    m_editor->applyLink(QStringLiteral("example.org"));
    QVERIFY(m_editor->textEdit()->toHtml().contains(QStringLiteral("https://example.org")));
    QVERIFY(!item->isEditing());  // il titolo non si apre
    // Clic fuori dal riquadro: chiude.
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, QPoint(10, 300));
    QApplication::processEvents();
    QVERIFY(!m_editor->isOpen());
}

void NotesTest::emptyNoteRemovesTheIcon() {
    MindNode *node = addNode(QStringLiteral("Da svuotare"));
    node->notes = QStringLiteral("vecchia");
    m_scene->updateNode(node);
    m_scene->autoLayout();
    flushHistory();
    emit m_scene->noteActivated(node);
    QApplication::processEvents();
    m_editor->textEdit()->clear();
    m_editor->commitAndClose();
    QApplication::processEvents();
    QVERIFY(node->notes.isEmpty() && node->notesHtml.isEmpty());
    QVERIFY(itemFor(node)->noteRect().isEmpty());
}

void NotesTest::showAllNotesMakesRoomBelowTheNode() {
    MindNode *node = addNode(QStringLiteral("Visibile"));
    node->notes = QStringLiteral("riga uno\nriga due");
    m_scene->updateNode(node);
    m_scene->autoLayout();
    flushHistory();
    const QPointF before = node->position;
    NodeItem *item = itemFor(node);
    QVERIFY(item->notePreviewRect().isEmpty());
    auto *panel = m_window->findChild<SheetStylePanel *>();
    QVERIFY(panel);
    panel->showNotesSwitch()->click();
    QApplication::processEvents();
    QVERIFY(m_model->sheetStyle.showNotes);
    QVERIFY(!item->notePreviewRect().isEmpty());
    QVERIFY(item->noteRect().isEmpty());  // nota mostrata dentro il nodo al posto dell'icona
    QVERIFY(item->notePreviewRect().bottom() < item->boundingRect().bottom());
    // Un clic sulla nota riapre il riquadro di scrittura.
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, m_view->mapFromScene(item->mapToScene(item->notePreviewRect().center())));
    QApplication::processEvents();
    QVERIFY(m_editor->isOpen());
    m_editor->commitAndClose();
    // La nota ha posto: il nodo dopo scende (2026-10-19, dopo la prova: senza spazio copriva il vicino).
    MindNode *next = m_model->addChild(m_model->root(), QStringLiteral("Dopo"));
    m_scene->autoLayout();
    QApplication::processEvents();
    const QRectF noteInScene = item->mapRectToScene(item->notePreviewRect());
    QVERIFY(m_scene->nodeSceneRect(next).top() > noteInScene.bottom());
    Q_UNUSED(before);
    panel->showNotesSwitch()->click();
    QApplication::processEvents();
    QVERIFY(item->notePreviewRect().isEmpty());
}

void NotesTest::outlinerShowsTheNoteBelowTheRow() {
    MindNode *plain = addNode(QStringLiteral("Senza nota"));
    MindNode *noted = addNode(QStringLiteral("Con nota sotto"));
    noted->notes = QStringLiteral("prima riga\nseconda riga\nterza riga");
    m_scene->updateNode(noted);
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
    tree->doItemsLayout();
    QApplication::processEvents();
    const int plainHeight = tree->visualRect(m_model->indexFromNode(plain)).height();
    const int notedHeight = tree->visualRect(m_model->indexFromNode(noted)).height();
    QVERIFY(notedHeight > plainHeight);  // tutta la nota sotto il titolo (tre righe)
    // Clic sul riquadro grigio sotto il titolo: si scrive li', con la barra flottante.
    const QRect row = tree->visualRect(m_model->indexFromNode(noted));
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, QPoint(row.center().x(), row.bottom() - 20));
    QApplication::processEvents();
    NoteEditor *inlineEditor = m_window->outlineNoteEditor();
    QVERIFY(inlineEditor->isOpen());
    QCOMPARE(inlineEditor->mode(), NoteEditor::Mode::Inline);
    QVERIFY(inlineEditor->formatButton(QStringLiteral("link")));
    inlineEditor->textEdit()->moveCursor(QTextCursor::End);
    for (int i = 0; i < 3; ++i) QTest::keyClick(inlineEditor->textEdit(), Qt::Key_Return);
    QTest::keyClicks(inlineEditor->textEdit(), QStringLiteral("quarta riga"));
    QApplication::processEvents();
    QTest::qWait(50);
    QVERIFY(tree->visualRect(m_model->indexFromNode(noted)).height() > notedHeight);  // la riga si allunga
    QTest::keyClick(inlineEditor->textEdit(), Qt::Key_Escape);
    QApplication::processEvents();
    QVERIFY(noted->notes.endsWith(QStringLiteral("quarta riga")));
    outliner->click();
    QApplication::processEvents();
}

void NotesTest::fileAndMarkdownKeepTheFormats() {
    MindMapModel model;
    MindNode *node = model.addChild(model.root(), QStringLiteral("Voce"));
    QTextDocument document;
    document.setHtml(QStringLiteral("<p><b>Grassetto</b> e <i>corsivo</i> con <a href=\"https://example.com\">sito</a></p><ul><li>uno</li><li>due</li></ul>"));
    node->notes = document.toPlainText();
    node->notesHtml = document.toHtml();
    MindMapModel reopened;
    QVERIFY(reopened.fromJson(model.toJson()));
    QCOMPARE(reopened.root()->children.front()->notesHtml, node->notesHtml);
    const QString markdown = model.toMarkdown(false);
    QVERIFY2(markdown.contains(QStringLiteral("**Grassetto**")), qPrintable(markdown));
    QVERIFY(markdown.contains(QStringLiteral("• uno")));
    QVERIFY(markdown.contains(QStringLiteral("[sito](https://example.com)")));
    QVERIFY(!markdown.contains(QStringLiteral("- uno")));  // non diventa un ramo figlio
    MindMapModel imported;
    QVERIFY(imported.fromMarkdown(markdown));
    const MindNode *back = imported.root()->children.front().get();
    QCOMPARE(back->children.size(), size_t(0));
    QVERIFY(back->notes.contains(QStringLiteral("Grassetto")));
    QVERIFY(back->notesHtml.contains(QStringLiteral("<ul")));
    QVERIFY(back->notesHtml.contains(QStringLiteral("font-weight")));
    QVERIFY(back->notesHtml.contains(QStringLiteral("https://example.com")));
}

// Foto per il confronto a vista (solo con MENTIS_SNAPSHOT_DIR, piattaforma con i caratteri).
// BUG-044: rich text from a document (titles, notes) keeps no image, background or style sheet
// Qt would read from a file or a network share; embedded images stay.
void NotesTest::externalImagesNeverLoad() {
    QTemporaryDir dir;
    QImage red(300, 200, QImage::Format_RGB32);
    red.fill(Qt::red);
    const QString png = dir.filePath(QStringLiteral("probe.png"));
    QVERIFY(red.save(png));
    QFile css(dir.filePath(QStringLiteral("wide.css")));
    QVERIFY(css.open(QIODevice::WriteOnly));
    css.write("p { margin-left: 400px; }");
    css.close();
    const QString cssUrl = QUrl::fromLocalFile(css.fileName()).toString();
    const QString share = QStringLiteral("//127.0.0.1/") + png.left(1) + QStringLiteral("$") + png.mid(2);
    const auto width = [](const QString &html) {
        QTextDocument document;
        document.setHtml(RichResources::withoutExternal(html));
        return document.idealWidth();
    };
    const auto image = [](const QString &source) { return QStringLiteral("<p>x<img src=\"%1\"></p>").arg(source); };
    for (const QString &source : {QUrl::fromLocalFile(png).toString(), png, share, QStringLiteral("file:") + share})
        QVERIFY2(width(image(source)) < 100, qPrintable(source));
    for (const QString &background : {QStringLiteral("<table background=\"%1\"><tr><td>x</td></tr></table>"),
                                      QStringLiteral("<table><tr><td style=\"background-image: url(%1)\">x</td></tr></table>"),
                                      QStringLiteral("<p style=\"background-image: url('%1')\">x</p>")})
        QVERIFY2(!RichResources::withoutExternal(background.arg(png)).contains(QStringLiteral("probe.png")), qPrintable(background));
    QVERIFY(width(QStringLiteral("<html><head><link rel=\"stylesheet\" type=\"text/css\" href=\"%1\"></head><body><p>x</p></body></html>").arg(cssUrl)) < 100);
    QVERIFY(width(QStringLiteral("<html><head><style>@import url(%1);</style></head><body><p>x</p></body></html>").arg(cssUrl)) < 100);
    QVERIFY(width(image(RichResources::dataUrl(red))) >= 300);  // embedded: kept
    const QString plain = QStringLiteral("<p>Solo <b>testo</b></p>");
    QCOMPARE(RichResources::withoutExternal(plain), plain);  // nothing to clean: unchanged
    // From a file: the title is cleaned when the map is read.
    MindMapModel model;
    QJsonObject child{{QStringLiteral("id"), QStringLiteral("n1")}, {QStringLiteral("title"), QStringLiteral("x")},
                      {QStringLiteral("titleHtml"), image(share)}};
    QJsonObject root{{QStringLiteral("id"), QStringLiteral("r")}, {QStringLiteral("title"), QStringLiteral("Radice")},
                     {QStringLiteral("children"), QJsonArray{child}}};
    QString error;
    QVERIFY2(model.fromJson(QJsonObject{{QStringLiteral("root"), root}}, &error), qPrintable(error));
    QVERIFY(!model.root()->children.front()->titleHtml.contains(QStringLiteral("127.0.0.1")));
}

// BUG-048 (found by fuzzing): Qt's layout time grows tenfold every four levels of nested tables;
// 20 levels froze the app. Past kMaxTableNesting they become paragraphs, quickly, text kept.
void NotesTest::deeplyNestedTablesBecomeParagraphs() {
    const QString shallow = QStringLiteral("<table><tr><td>").repeated(RichResources::kMaxTableNesting) + QStringLiteral("dentro");
    QCOMPARE(RichResources::safeToParse(shallow), shallow);  // within the limit: unchanged
    for (const QString &opening : {QStringLiteral("<table><tr><td>"), QStringLiteral("<TABLE><tr><td<body>")}) {
        const QString html = opening.repeated(40) + QStringLiteral("dentro") + QStringLiteral("</td></tr></table>").repeated(40);
        QElapsedTimer timer;
        timer.start();
        const QString clean = RichResources::withoutExternal(html);
        QTextDocument document;
        document.setHtml(clean);
        document.idealWidth();
        QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(!clean.contains(QStringLiteral("table"), Qt::CaseInsensitive));
        QVERIFY(document.toPlainText().contains(QStringLiteral("dentro")));
    }
}

void NotesTest::pastedImagesAreEmbedded() {
    QTemporaryDir dir;
    QImage green(120, 80, QImage::Format_RGB32);
    green.fill(Qt::green);
    const QString png = dir.filePath(QStringLiteral("dal-disco.png"));
    QVERIFY(green.save(png));
    QImage blue(60, 40, QImage::Format_RGB32);
    blue.fill(Qt::blue);
    MindNode *node = addNode(QStringLiteral("Immagini incollate"));
    m_scene->selectNode(node);
    actionWithText(QStringLiteral("Note"))->trigger();
    QApplication::processEvents();
    QTextEdit *text = m_editor->textEdit();
    const auto paste = [text](const QString &html, const QImage &image) {
        auto *mime = new QMimeData;
        if (!html.isEmpty()) mime->setHtml(html);
        if (!image.isNull()) mime->setImageData(image);
        QApplication::clipboard()->setMimeData(mime);
        text->paste();
    };
    // A local file and one web image with the browser's picture: both embedded.
    paste(QStringLiteral("<p>a<img src=\"%1\">b<img src=\"https://example.invalid/x.png\"></p>").arg(QUrl::fromLocalFile(png).toString()), blue);
    QString html = text->toHtml();
    QCOMPARE(html.count(QStringLiteral("data:image/")), 2);
    QVERIFY(!html.contains(QStringLiteral("dal-disco.png")));
    QVERIFY(!html.contains(QStringLiteral("example.invalid")));
    // Two web images without a picture: nothing to embed, both removed.
    text->clear();
    paste(QStringLiteral("<p>c<img src=\"https://example.invalid/1.png\"><img src=\"https://example.invalid/2.png\">d</p>"), QImage());
    QCOMPARE(text->toPlainText(), QStringLiteral("cd"));
    // A picture alone (a screenshot): embedded.
    text->clear();
    paste(QString(), blue);
    QCOMPARE(text->toHtml().count(QStringLiteral("data:image/")), 1);
    QTest::keyClick(text, Qt::Key_Escape);  // closes and saves
    QApplication::processEvents();
    QVERIFY(node->notesHtml.contains(QStringLiteral("data:image/")));
}

void NotesTest::snapshotForVisualReview() {
    const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR");
    if (dir.isEmpty()) QSKIP("MENTIS_SNAPSHOT_DIR non impostata");
    m_model->clear(QStringLiteral("Esame di storia"));
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("Rivoluzione francese"));
    MindNode *b = m_model->addChild(m_model->root(), QStringLiteral("Napoleone"));
    QTextDocument note;
    note.setHtml(QStringLiteral("<p><b>Date chiave</b></p><ul><li>1789 presa della Bastiglia</li><li>1793 Terrore</li></ul>"));
    a->notes = note.toPlainText();
    a->notesHtml = note.toHtml();
    b->notes = QStringLiteral("Da ripassare con le mappe delle campagne.");
    b->webLink = QStringLiteral("https://example.com");
    for (MindNode *node : {a, b}) m_scene->updateNode(node);
    m_scene->autoLayout();
    m_view->centerOn(itemFor(a));
    QApplication::processEvents();
    emit m_scene->noteActivated(a);
    QTest::qWait(300);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/notes-editor.png")));
    m_editor->commitAndClose();
    m_window->findChild<SheetStylePanel *>()->showNotesSwitch()->click();
    QTest::qWait(300);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/notes-shown.png")));
    m_window->findChild<SheetStylePanel *>()->showNotesSwitch()->click();
    QPushButton *outliner = nullptr;
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Outliner")) outliner = button;
    outliner->click();
    QTest::qWait(300);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/notes-outliner.png")));
    emit m_scene->noteActivated(a);  // nell'outliner: scrittura sul posto
    QTest::qWait(300);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/notes-outliner-edit.png")));
    m_window->outlineNoteEditor()->commitAndClose();
    outliner->click();
}

QTEST_MAIN(NotesTest)
#include "notes_test.moc"
