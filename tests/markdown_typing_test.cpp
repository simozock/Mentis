// FEAT-068 tappa 1 con il vero MainWindow: scorciatoie Markdown scritte nel titolo sulla mappa,
// nel titolo dell'outliner e nella nota; casella da "- [ ] "; Ctrl+Z; interruttore; Markdown della nota.
#include "document.h"
#include "mainwindow.h"
#include "markdownshortcuts.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "noteeditor.h"
#include "performance.h"

#include <QAction>
#include <QApplication>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolButton>
#include <QGraphicsProxyWidget>
#include <QTreeView>

#include <memory>

class MarkdownTypingTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void mapTitleBecomesBold();
    void typingOnSelectedTopicReplacesTitle();
    void mapTitleCheckbox();
    void outlinerTitleHighlightAndCheckbox();
    void noteListLinkAndCode();
    void switchOffKeepsTheSymbols();
    void noteHighlightAndCodeInMarkdown();
    void buttonsRemoveWhatMarkdownAdded();
    void snapshotForVisualReview();

private:
    MindNode *addNode(const QString &title);
    NodeItem *itemFor(const MindNode *node) const;
    void typeInMapTitle(MindNode *node, const QString &text);
    QTreeView *showOutliner();
    void flushHistory();

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    MindMapView *m_view = nullptr;
};

void MarkdownTypingTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisMarkdownTypingTest"));
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

void MarkdownTypingTest::cleanupTestCase() { m_window.reset(); }

void MarkdownTypingTest::init() {
    MarkdownShortcuts::setEnabled(true);
    m_scene->clearSelection();
    flushHistory();
}

void MarkdownTypingTest::flushHistory() {
    QApplication::processEvents();
    // The open history step is closed at once instead of waiting for its 150 ms timer.
    m_window->findChild<Document *>()->flushPendingStep();
    QApplication::processEvents();
}

MindNode *MarkdownTypingTest::addNode(const QString &title) {
    MindNode *node = m_model->addChild(m_model->root(), title);
    m_scene->autoLayout();
    flushHistory();
    return node;
}

NodeItem *MarkdownTypingTest::itemFor(const MindNode *node) const {
    for (auto *item : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node) return nodeItem;
    return nullptr;
}

// Apre il titolo con tutto il testo selezionato e scrive text al suo posto.
void MarkdownTypingTest::typeInMapTitle(MindNode *node, const QString &text) {
    m_scene->selectNode(node);
    NodeItem *item = itemFor(node);
    m_view->setFocus();
    item->beginEditing();
    QApplication::processEvents();
    QTest::keyClicks(m_view->viewport(), text);
    QApplication::processEvents();
}

QTreeView *MarkdownTypingTest::showOutliner() {
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Outliner") || button->text() == QStringLiteral("Mind map")) { button->click(); break; }
    QApplication::processEvents();
    for (auto *tree : m_window->findChildren<QTreeView *>())
        if (tree->isVisible() && tree->model()) return tree;
    return nullptr;
}

void MarkdownTypingTest::mapTitleBecomesBold() {
    MindNode *node = addNode(QStringLiteral("Da scrivere"));
    typeInMapTitle(node, QStringLiteral("un **passo** chiave"));
    itemFor(node)->commitEditing();
    QApplication::processEvents();
    QCOMPARE(node->title, QStringLiteral("un passo chiave"));
    QVERIFY2(node->titleHtml.contains(QStringLiteral("font-weight:700")), qPrintable(node->titleHtml));
    // Ctrl+Z subito dopo la trasformazione, a campo aperto: tornano gli asterischi.
    typeInMapTitle(node, QStringLiteral("**x**"));
    QTest::keyClick(m_view->viewport(), Qt::Key_Z, Qt::ControlModifier);
    QApplication::processEvents();
    itemFor(node)->commitEditing();
    QApplication::processEvents();
    QCOMPARE(node->title, QStringLiteral("**x**"));
}

// FEAT-002 (2026-11-04): with the topic selected and the field closed, the first key opens the
// title and replaces it; the following keys go on writing, Markdown shortcuts included.
void MarkdownTypingTest::typingOnSelectedTopicReplacesTitle() {
    MindNode *node = addNode(QStringLiteral("Vecchio titolo"));
    m_scene->selectNode(node);
    m_view->setFocus();
    QApplication::processEvents();
    QVERIFY(!itemFor(node)->isEditing());
    QTest::keyClicks(m_view->viewport(), QStringLiteral("Nuovo **b**"));
    QApplication::processEvents();
    QVERIFY(itemFor(node)->isEditing());
    itemFor(node)->commitEditing();
    QApplication::processEvents();
    QCOMPARE(node->title, QStringLiteral("Nuovo b"));
    QVERIFY2(node->titleHtml.contains(QStringLiteral("font-weight:700")), qPrintable(node->titleHtml));
    // Two topics selected: typing does not open either title.
    MindNode *other = addNode(QStringLiteral("Altro"));
    m_scene->selectNode(node);
    itemFor(other)->setSelected(true);
    QTest::keyClicks(m_view->viewport(), QStringLiteral("x"));
    QApplication::processEvents();
    QVERIFY(!itemFor(node)->isEditing() && !itemFor(other)->isEditing());
    QCOMPARE(other->title, QStringLiteral("Altro"));
}

void MarkdownTypingTest::mapTitleCheckbox() {
    MindNode *node = addNode(QStringLiteral("Vecchio"));
    typeInMapTitle(node, QStringLiteral("- [ ] Ripassare"));
    itemFor(node)->commitEditing();
    QApplication::processEvents();
    QCOMPARE(node->title, QStringLiteral("Ripassare"));
    QCOMPARE(node->task, MindNode::Task::Open);
    QVERIFY(!itemFor(node)->taskRect().isEmpty());
}

void MarkdownTypingTest::outlinerTitleHighlightAndCheckbox() {
    MindNode *node = addNode(QStringLiteral("Voce"));
    QTreeView *tree = showOutliner();
    QVERIFY(tree);
    const QModelIndex index = m_model->indexFromNode(node);
    tree->setCurrentIndex(index);
    tree->edit(index);
    QApplication::processEvents();
    QWidget *field = tree->indexWidget(index);
    QVERIFY(field);
    QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(field, QStringLiteral("[x] Fatto ==bene=="));
    QTest::keyClick(field, Qt::Key_Return);
    QTest::qWait(50);
    QApplication::processEvents();
    QCOMPARE(node->title, QStringLiteral("Fatto bene"));
    QVERIFY2(node->titleHtml.contains(QStringLiteral("#fff3a3"), Qt::CaseInsensitive), qPrintable(node->titleHtml));
    QCOMPARE(node->task, MindNode::Task::Done);
    showOutliner();  // torna alla mappa
}

void MarkdownTypingTest::noteListLinkAndCode() {
    MindNode *node = addNode(QStringLiteral("Con nota"));
    emit m_scene->noteActivated(node);
    QApplication::processEvents();
    NoteEditor *editor = m_window->noteEditor();
    QVERIFY(editor->isOpen());
    QTextEdit *text = editor->textEdit();
    QTest::keyClicks(text, QStringLiteral("## Date"));
    QTest::keyClick(text, Qt::Key_Return);
    QTest::keyClicks(text, QStringLiteral("- vedi [sito](example.com) e `ls`"));
    editor->commitAndClose();
    QApplication::processEvents();
    QCOMPARE(node->notes, QStringLiteral("Date\nvedi sito e ls"));
    QVERIFY(node->notesHtml.contains(QStringLiteral("<ul")));
    QVERIFY(node->notesHtml.contains(QStringLiteral("https://example.com")));
    const QString markdown = m_model->toMarkdown(false);
    QVERIFY2(markdown.contains(QStringLiteral("`ls`")), qPrintable(markdown));
    QVERIFY(markdown.contains(QStringLiteral("[sito](https://example.com)")));
}

void MarkdownTypingTest::switchOffKeepsTheSymbols() {
    MarkdownShortcuts::setEnabled(false);
    MindNode *node = addNode(QStringLiteral("Spento"));
    typeInMapTitle(node, QStringLiteral("**come scritto**"));
    itemFor(node)->commitEditing();
    QApplication::processEvents();
    QCOMPARE(node->title, QStringLiteral("**come scritto**"));
    MarkdownShortcuts::setEnabled(true);
}

void MarkdownTypingTest::noteHighlightAndCodeInMarkdown() {
    MindMapModel model;
    MindNode *node = model.addChild(model.root(), QStringLiteral("Voce"));
    QTextDocument document;
    QTextCursor cursor(&document);
    cursor.insertText(QStringLiteral("prima "));
    cursor.insertText(QStringLiteral("importante"), MarkdownShortcuts::highlightFormat());
    cursor.insertText(QStringLiteral(" e "), QTextCharFormat());
    cursor.insertText(QStringLiteral("cmd"), MarkdownShortcuts::codeFormat());
    node->notes = document.toPlainText();
    node->notesHtml = document.toHtml();
    const QString markdown = model.toMarkdown(false);
    QVERIFY2(markdown.contains(QStringLiteral("==importante==")), qPrintable(markdown));
    QVERIFY(markdown.contains(QStringLiteral("`cmd`")));
    MindMapModel imported;
    QVERIFY(imported.fromMarkdown(markdown));
    const MindNode *back = imported.root()->children.front().get();
    QCOMPARE(back->notes, QStringLiteral("prima importante e cmd"));
    QVERIFY(back->notesHtml.contains(QStringLiteral("#fff3a3"), Qt::CaseInsensitive));
}

// Il Markdown mette il formato, i bottoni lo tolgono (2026-10-19): mappa e nota.
void MarkdownTypingTest::buttonsRemoveWhatMarkdownAdded() {
    MindNode *node = addNode(QStringLiteral("Bottoni"));
    typeInMapTitle(node, QStringLiteral("==tutto=="));
    itemFor(node)->commitEditing();
    QApplication::processEvents();
    QVERIFY(node->titleHtml.contains(QStringLiteral("#fff3a3"), Qt::CaseInsensitive));
    m_scene->selectNode(node);
    itemFor(node)->beginEditing();  // tutto il testo selezionato e barra visibile
    QApplication::processEvents();
    QToolButton *highlight = nullptr;
    for (auto *candidate : m_scene->items())
        if (auto *proxy = dynamic_cast<QGraphicsProxyWidget *>(candidate); proxy && proxy->widget() && proxy->isVisible())
            for (auto *button : proxy->widget()->findChildren<QToolButton *>())
                if (button->toolTip() == QStringLiteral("Highlight")) highlight = button;
    QVERIFY(highlight);
    QVERIFY(highlight->isChecked());
    highlight->click();
    itemFor(node)->commitEditing();
    QApplication::processEvents();
    QVERIFY2(!node->titleHtml.contains(QStringLiteral("#fff3a3"), Qt::CaseInsensitive), qPrintable(node->titleHtml));
    // Nota: bottone Codice.
    emit m_scene->noteActivated(node);
    QApplication::processEvents();
    NoteEditor *editor = m_window->noteEditor();
    QTest::keyClicks(editor->textEdit(), QStringLiteral("`ls`"));
    editor->textEdit()->selectAll();
    QToolButton *code = editor->formatButton(QStringLiteral("code"));
    QVERIFY(code && code->isChecked());
    code->click();
    QVERIFY(!code->isChecked());
    editor->commitAndClose();
    QApplication::processEvents();
    QVERIFY(!MarkdownShortcuts::isCode(QTextCharFormat()));
    QVERIFY2(!node->notesHtml.contains(QStringLiteral("background-color")), qPrintable(node->notesHtml));
}

// Foto per il confronto a vista (solo con MENTIS_SNAPSHOT_DIR, piattaforma con i caratteri).
void MarkdownTypingTest::snapshotForVisualReview() {
    const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR");
    if (dir.isEmpty()) QSKIP("MENTIS_SNAPSHOT_DIR non impostata");
    m_model->clear(QStringLiteral("Esame di storia"));
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("a"));
    MindNode *b = m_model->addChild(m_model->root(), QStringLiteral("b"));
    m_scene->autoLayout();
    QApplication::processEvents();
    typeInMapTitle(a, QStringLiteral("La **Bastiglia** e il ==Terrore=="));
    itemFor(a)->commitEditing();
    typeInMapTitle(b, QStringLiteral("- [ ] Ripassare *Napoleone*"));
    itemFor(b)->commitEditing();
    QApplication::processEvents();
    emit m_scene->noteActivated(a);
    QApplication::processEvents();
    QTextEdit *text = m_window->noteEditor()->textEdit();
    QTest::keyClicks(text, QStringLiteral("## Date chiave"));
    QTest::keyClick(text, Qt::Key_Return);
    QTest::keyClicks(text, QStringLiteral("- 1789 **presa** della Bastiglia"));
    QTest::keyClick(text, Qt::Key_Return);
    QTest::keyClicks(text, QStringLiteral("1793 ==Terrore==, vedi [wiki](it.wikipedia.org) e `cap. 4`"));
    m_view->centerOn(itemFor(m_model->root()));
    QTest::qWait(300);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/markdown-typing.png")));
    m_window->noteEditor()->commitAndClose();
}

QTEST_MAIN(MarkdownTypingTest)
#include "markdown_typing_test.moc"
