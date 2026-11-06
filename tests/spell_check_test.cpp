// FEAT-044 stage A with the real MainWindow and a fake checker (the user's dictionary is never
// touched): underline in the map title and in the Outliner, menu items, suggestion, Learn and
// Ignore, switch, formulas, saved title; then the Windows checker itself, skipped without a language.
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "performance.h"
#include "spellchecker.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QContextMenuEvent>
#include <QElapsedTimer>
#include <QGraphicsTextItem>
#include <QLocale>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>
#include <QTimer>
#include <QTreeView>

#include <memory>

namespace {

// Misspelled = any word not in the known set. Learn and Ignore add to it, as the system does.
class FakeBackend final : public SpellCheck::Backend {
public:
    QSet<QString> known{QStringLiteral("ciao"), QStringLiteral("mondo"), QStringLiteral("bello"), QStringLiteral("x")};
    QStringList learned;
    QStringList ignored;

    QList<SpellCheck::Range> misspelled(const QString &text) override {
        QList<SpellCheck::Range> ranges;
        static const QRegularExpression words(QStringLiteral("\\p{L}+"));
        for (auto it = words.globalMatch(text); it.hasNext();) {
            const auto match = it.next();
            if (!known.contains(match.captured().toLower())) ranges.append({int(match.capturedStart()), int(match.capturedLength())});
        }
        return ranges;
    }
    QStringList suggestions(const QString &word, int max) override {
        if (word == QStringLiteral("qwzx")) return {};
        return QStringList{QStringLiteral("mondo"), QStringLiteral("monde"), QStringLiteral("mondi"), QStringLiteral("moda"),
                           QStringLiteral("mondano"), QStringLiteral("mondare")}.mid(0, max);
    }
    void learn(const QString &word) override { learned.append(word); known.insert(word.toLower()); }
    void ignore(const QString &word) override { ignored.append(word); known.insert(word.toLower()); }
};

// Underlined ranges of the first paragraph of document, as "start:length".
QStringList underlines(const QTextDocument *document) {
    QStringList result;
    for (const QTextLayout::FormatRange &range : document->firstBlock().layout()->formats())
        if (range.format.underlineStyle() == QTextCharFormat::WaveUnderline)
            result.append(QStringLiteral("%1:%2").arg(range.start).arg(range.length));
    return result;
}

}  // namespace

class SpellCheckTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void mapTitleIsUnderlinedWhileEditing();
    void suggestionReplacesTheWord();
    void learnAndIgnore();
    void noSuggestions();
    void formulaIsNotChecked();
    void switchOff();
    void noDictionary();
    void outlinerField();
    void windowsChecker();

private:
    MindNode *addNode(const QString &title);
    NodeItem *itemFor(const MindNode *node) const;
    QGraphicsTextItem *editTitle(MindNode *node);
    // Right-click on the character at position of the map editor; inspect sees the open menu.
    void openMapMenu(QGraphicsTextItem *editor, int position, const std::function<void(QMenu *)> &inspect);
    static void choose(QMenu *menu, const QString &text);
    static QStringList texts(QMenu *menu);

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    MindMapView *m_view = nullptr;
    FakeBackend *m_backend = nullptr;
};

void SpellCheckTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisSpellCheckTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);  // FEAT-044: active in the Light profile too
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

void SpellCheckTest::cleanupTestCase() {
    m_window.reset();
    SpellCheck::setBackendForTesting(nullptr);
}

void SpellCheckTest::init() {
    SpellCheck::setEnabled(true);
    auto backend = std::make_unique<FakeBackend>();
    m_backend = backend.get();
    SpellCheck::setBackendForTesting(std::move(backend));
    m_scene->clearSelection();
    QApplication::processEvents();
}

MindNode *SpellCheckTest::addNode(const QString &title) {
    MindNode *node = m_model->addChild(m_model->root(), title);
    m_scene->autoLayout();
    QApplication::processEvents();
    return node;
}

NodeItem *SpellCheckTest::itemFor(const MindNode *node) const {
    for (auto *item : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node) return nodeItem;
    return nullptr;
}

QGraphicsTextItem *SpellCheckTest::editTitle(MindNode *node) {
    m_scene->selectNode(node);
    NodeItem *item = itemFor(node);
    m_view->setFocus();
    item->beginEditing();
    QApplication::processEvents();  // the highlighter runs on the next turn of the event loop
    return const_cast<QGraphicsTextItem *>(item->titleEditor());
}

void SpellCheckTest::openMapMenu(QGraphicsTextItem *editor, int position, const std::function<void(QMenu *)> &inspect) {
    const QTextBlock block = editor->document()->findBlock(position);
    const QTextLine line = block.layout()->lineForTextPosition(position - block.position());
    const QPointF local(line.cursorToX(position - block.position() + 0) + 2, line.y() + line.height() / 2);
    const QPoint inView = m_view->mapFromScene(editor->mapToScene(local + block.layout()->position()));
    bool seen = false;
    // The wait ends with this function: it captures its locals (a late one crashed the next test, macOS CI).
    QObject scope;
    QTimer::singleShot(50, &scope, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        QVERIFY(menu);
        seen = true;
        inspect(menu);
        if (menu->isVisible()) menu->close();
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, inView, m_view->viewport()->mapToGlobal(inView));
    QApplication::sendEvent(m_view->viewport(), &event);
    QApplication::processEvents();
    QVERIFY(seen);
}

QStringList SpellCheckTest::texts(QMenu *menu) {
    QStringList result;
    for (QAction *action : menu->actions()) result.append(action->isSeparator() ? QStringLiteral("|") : action->text());
    return result;
}

void SpellCheckTest::choose(QMenu *menu, const QString &text) {
    for (QAction *action : menu->actions())
        if (action->text() == text) {
            menu->setActiveAction(action);
            QTest::keyClick(menu, Qt::Key_Return);
            return;
        }
    QFAIL(qPrintable(QStringLiteral("no item ") + text));
}

void SpellCheckTest::mapTitleIsUnderlinedWhileEditing() {
    MindNode *node = addNode(QStringLiteral("ciao mondoo"));
    QGraphicsTextItem *editor = editTitle(node);
    QVERIFY(editor);
    QCOMPARE(underlines(editor->document()), QStringList{QStringLiteral("5:6")});
    // Menu on the misspelled word: 5 suggestions, Learn, Ignore, then FEAT-042's items.
    openMapMenu(editor, 7, [](QMenu *menu) {
        const QStringList expected{QStringLiteral("mondo"), QStringLiteral("monde"), QStringLiteral("mondi"), QStringLiteral("moda"),
                                   QStringLiteral("mondano"), QStringLiteral("|"), QStringLiteral("Learn spelling"), QStringLiteral("Ignore"),
                                   QStringLiteral("|"), QStringLiteral("Cut"), QStringLiteral("Copy"), QStringLiteral("Paste"),
                                   QStringLiteral("|"), QStringLiteral("Select all")};
        QCOMPARE(texts(menu), expected);
    });
    // On a correct word: only FEAT-042's items.
    openMapMenu(editor, 1, [](QMenu *menu) { QCOMPARE(texts(menu).first(), QStringLiteral("Cut")); });
    // The underline is a layout format: the saved title has none.
    itemFor(node)->commitEditing();
    QApplication::processEvents();
    QCOMPARE(node->title, QStringLiteral("ciao mondoo"));
    QVERIFY2(!node->titleHtml.contains(QStringLiteral("underline")), qPrintable(node->titleHtml));
}

void SpellCheckTest::suggestionReplacesTheWord() {
    MindNode *node = addNode(QStringLiteral("ciao mondoo bello"));
    QGraphicsTextItem *editor = editTitle(node);
    openMapMenu(editor, 6, [](QMenu *menu) { choose(menu, QStringLiteral("mondo")); });
    QCOMPARE(editor->toPlainText(), QStringLiteral("ciao mondo bello"));
    QApplication::processEvents();
    QVERIFY(underlines(editor->document()).isEmpty());
    // An edit of the field like typing: Ctrl+Z gives the word back, Ctrl+Y the suggestion again.
    QTest::keyClick(m_view->viewport(), Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(editor->toPlainText(), QStringLiteral("ciao mondoo bello"));
    QTest::keyClick(m_view->viewport(), Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(editor->toPlainText(), QStringLiteral("ciao mondo bello"));
    itemFor(node)->commitEditing();
    QApplication::processEvents();
    QCOMPARE(node->title, QStringLiteral("ciao mondo bello"));
}

void SpellCheckTest::learnAndIgnore() {
    MindNode *node = addNode(QStringLiteral("abcd efgh"));
    QGraphicsTextItem *editor = editTitle(node);
    QCOMPARE(underlines(editor->document()).size(), 2);
    openMapMenu(editor, 1, [](QMenu *menu) { choose(menu, QStringLiteral("Learn spelling")); });
    QCOMPARE(m_backend->learned, QStringList{QStringLiteral("abcd")});
    QCOMPARE(underlines(editor->document()), QStringList{QStringLiteral("5:4")});
    openMapMenu(editor, 6, [](QMenu *menu) { choose(menu, QStringLiteral("Ignore")); });
    QCOMPARE(m_backend->ignored, QStringList{QStringLiteral("efgh")});
    QVERIFY(underlines(editor->document()).isEmpty());
    itemFor(node)->commitEditing();
    QApplication::processEvents();
    QCOMPARE(node->title, QStringLiteral("abcd efgh"));  // Learn and Ignore do not edit the text
}

void SpellCheckTest::noSuggestions() {
    MindNode *node = addNode(QStringLiteral("qwzx"));
    QGraphicsTextItem *editor = editTitle(node);
    openMapMenu(editor, 1, [](QMenu *menu) {
        QAction *first = menu->actions().first();
        QCOMPARE(first->text(), QStringLiteral("No suggestions"));
        QVERIFY(!first->isEnabled());
    });
    itemFor(node)->commitEditing();
}

void SpellCheckTest::formulaIsNotChecked() {
    MindNode *node = addNode(QStringLiteral("x"));
    QGraphicsTextItem *editor = editTitle(node);
    editor->setPlainText(QStringLiteral("ciao $\\frac{ab}{cd}$ mondoo"));
    QApplication::processEvents();
    QCOMPARE(underlines(editor->document()), QStringList{QStringLiteral("21:6")});
    QCOMPARE(SpellCheck::check(QStringLiteral("\\(qq\\) zz")).size(), 1);
    itemFor(node)->commitEditing();
}

void SpellCheckTest::switchOff() {
    QSettings().remove(QStringLiteral("editor/spellCheck"));
    QVERIFY(!SpellCheck::enabled());  // off by default (2026-10-26)
    SpellCheck::setEnabled(false);
    MindNode *node = addNode(QStringLiteral("ciao mondoo"));
    QGraphicsTextItem *editor = editTitle(node);
    QVERIFY(!SpellCheck::isAttached(editor->document()));
    QVERIFY(underlines(editor->document()).isEmpty());
    openMapMenu(editor, 7, [](QMenu *menu) { QCOMPARE(texts(menu).first(), QStringLiteral("Cut")); });
    itemFor(node)->commitEditing();
}

void SpellCheckTest::noDictionary() {
    SpellCheck::setBackendForTesting(nullptr);
    MindNode *node = addNode(QStringLiteral("ciao mondoo"));
    QGraphicsTextItem *editor = editTitle(node);
    QVERIFY(!SpellCheck::isAttached(editor->document()));
    openMapMenu(editor, 7, [](QMenu *menu) { QCOMPARE(texts(menu).first(), QStringLiteral("Cut")); });
    itemFor(node)->commitEditing();
}

void SpellCheckTest::outlinerField() {
    MindNode *node = addNode(QStringLiteral("ciao mondoo"));
    QPushButton *toggle = nullptr;
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Outliner")) toggle = button;
    QVERIFY(toggle);
    toggle->click();
    QApplication::processEvents();
    QTreeView *tree = nullptr;
    for (auto *view : m_window->findChildren<QTreeView *>())
        if (view->isVisible() && view->model()) tree = view;
    QVERIFY(tree);
    const QModelIndex index = m_model->indexFromNode(node);
    tree->setCurrentIndex(index);
    tree->edit(index);
    QApplication::processEvents();
    auto *field = qobject_cast<QPlainTextEdit *>(tree->indexWidget(index));
    QVERIFY(field);
    field->setFocus();
    QApplication::processEvents();
    // The field is checked on FocusIn, which setFocus sends only in the active window: on macOS CI the
    // window was not, and activating it there crashed. The event is sent as the system would.
    if (!field->hasFocus()) {
        QFocusEvent focusIn(QEvent::FocusIn, Qt::OtherFocusReason);
        QApplication::sendEvent(field, &focusIn);
        QApplication::processEvents();  // the highlighter's first pass is queued
    }
    QVERIFY(SpellCheck::isAttached(field->document()));
    QCOMPARE(underlines(field->document()), QStringList{QStringLiteral("5:6")});
    // Right-click on "mondoo": choosing a suggestion replaces it in the field.
    QTextCursor cursor(field->document());
    cursor.setPosition(7);
    const QPoint at = field->cursorRect(cursor).center();
    bool seen = false;
    QObject scope;  // as in openMapMenu
    QTimer::singleShot(50, &scope, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        QVERIFY(menu);
        seen = true;
        QCOMPARE(texts(menu).first(), QStringLiteral("mondo"));
        choose(menu, QStringLiteral("mondo"));
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, at, field->viewport()->mapToGlobal(at));
    QApplication::sendEvent(field->viewport(), &event);
    QApplication::processEvents();
    QVERIFY(seen);
    QCOMPARE(field->toPlainText(), QStringLiteral("ciao mondo"));
    QTest::keyClick(field, Qt::Key_Return);
    QApplication::processEvents();
    QCOMPARE(node->title, QStringLiteral("ciao mondo"));
    toggle = nullptr;
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Mind map")) toggle = button;
    if (toggle) toggle->click();
    QApplication::processEvents();
}

// The real system checker, read-only (never Learn or Ignore): skipped when Windows has no English.
void SpellCheckTest::windowsChecker() {
#ifndef Q_OS_WIN
    QSKIP("FEAT-044 stage A: Windows only");
#else
    QElapsedTimer timer;
    timer.start();
    std::unique_ptr<SpellCheck::Backend> checker = SpellCheck::createSystemBackend(QStringLiteral("en-US"));
    const qint64 createMs = timer.elapsed();
    if (!checker) QSKIP("no English spell checker in this Windows");
    timer.restart();
    const QList<SpellCheck::Range> ranges = checker->misspelled(QStringLiteral("hello wrld, nice day"));
    const qint64 checkUs = timer.nsecsElapsed() / 1000;
    QCOMPARE(ranges.size(), 1);
    QCOMPARE(ranges.first().start, 6);
    QCOMPARE(ranges.first().length, 4);
    const QStringList suggestions = checker->suggestions(QStringLiteral("wrld"), 5);
    QVERIFY(suggestions.size() <= 5);
    QVERIFY2(suggestions.contains(QStringLiteral("world")), qPrintable(suggestions.join(',')));
    QVERIFY(checker->misspelled(QStringLiteral("hello world")).isEmpty());
    // What a keystroke costs once the checker is warm: one paragraph checked again.
    timer.restart();
    for (int i = 0; i < 20; ++i) checker->misspelled(QStringLiteral("hello wrld, nice day %1").arg(i));
    const qint64 typingUs = timer.nsecsElapsed() / 1000 / 20;
    qInfo("FEAT-044: checker created in %lld ms, first check %lld us, then %lld us per check; system language %s supported: %s",
          createMs, checkUs, typingUs, qPrintable(QLocale::system().name()),
          SpellCheck::createSystemBackend(QLocale::system().name().replace('_', '-')) ? "yes" : "no");
#endif
}

QTEST_MAIN(SpellCheckTest)
#include "spell_check_test.moc"
