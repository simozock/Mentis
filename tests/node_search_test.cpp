// FEAT-061 / FEAT-072 C: search core and the left navigation panel (Outline, Notes, Tags).
#include "document.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "navigationpanel.h"
#include "nodesearch.h"
#include "performance.h"
#include "slowtests.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QMenu>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>

#include <algorithm>
#include <memory>

class NodeSearchTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void entriesFollowTheTreeOrder();
    void occurrencesHonourCaseAndWholeWords();
    void fieldsCanBeFilteredOut();
    void resultLineShowsWhereItWasFound();  // FEAT-113: shared with the phone
    void findOpensThePanelAndShowsTheTree();
    void typingShowsOnlyMatchesHighlighted();
    void clickSelectsTheTopicAndEscCloses();
    void notesAndTagsTabs();
    void currentFileFindsTopicsInOtherSheets();
    void findAndReplaceTitles();
    void tagsRenameAndDeleteOnEverySheet();
    void fiveThousandTopicsOnTheLightProfile();

private:
    QAction *action(const QString &text) const;
    QStringList rows(QTreeWidget *list) const;
    void search(const QString &text);

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    NavigationPanel *m_panel = nullptr;
    QDockWidget *m_dock = nullptr;
};

void NodeSearchTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisNodeSearchTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);
    m_window = std::make_unique<MainWindow>();
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    qobject_cast<QStackedWidget *>(m_window->centralWidget())->setCurrentIndex(1);
    m_model = m_window->findChild<MindMapModel *>();
    m_scene = qobject_cast<MindMapScene *>(m_window->findChild<MindMapView *>()->scene());
    m_panel = m_window->findChild<NavigationPanel *>();
    m_dock = m_window->findChild<QDockWidget *>(QStringLiteral("NavigationDock"));
    QVERIFY(m_model && m_scene && m_panel && m_dock);
}

void NodeSearchTest::cleanupTestCase() { m_window.reset(); }

void NodeSearchTest::init() {
    // Central "Plan" with "Budget review" (note, label) and "Team" > "Budget owner".
    m_model->clear(QStringLiteral("Plan"));
    MindNode *budget = m_model->addChild(m_model->root(), QStringLiteral("Budget review"));
    budget->notes = QStringLiteral("Check the yearly costs\nwith finance");
    budget->labels = {QStringLiteral("money")};
    MindNode *team = m_model->addChild(m_model->root(), QStringLiteral("Team"));
    MindNode *owner = m_model->addChild(team, QStringLiteral("Budget owner"));
    owner->labels = {QStringLiteral("money"), QStringLiteral("people")};
    owner->webLink = QStringLiteral("https://example.com/roles");
    m_model->refreshStyles();
    m_scene->autoLayout();
    m_window->findChild<Document *>()->startUntitled();
    m_dock->hide();
}

QAction *NodeSearchTest::action(const QString &text) const {
    for (QAction *candidate : m_window->findChildren<QAction *>())
        if (candidate->text() == text) return candidate;
    return nullptr;
}

QStringList NodeSearchTest::rows(QTreeWidget *list) const {
    QStringList result;
    for (QTreeWidgetItemIterator it(list); *it; ++it) result.append((*it)->text(0));
    return result;
}

void NodeSearchTest::search(const QString &text) {
    m_panel->findField()->setText(text);
    m_panel->refresh();  // without waiting for the typing delay
}

void NodeSearchTest::entriesFollowTheTreeOrder() {
    const QList<NodeSearch::Entry> entries = NodeSearch::entries(QStringLiteral("s"), m_model->toFlatSnapshot());
    QCOMPARE(entries.size(), 4);
    QCOMPARE(entries[0].title, QStringLiteral("Plan"));
    QCOMPARE(entries[0].depth, 0);
    QCOMPARE(entries[1].title, QStringLiteral("Budget review"));
    QCOMPARE(entries[1].labels, QStringList{QStringLiteral("money")});
    QCOMPARE(entries[2].title, QStringLiteral("Team"));
    QCOMPARE(entries[3].title, QStringLiteral("Budget owner"));
    QCOMPARE(entries[3].depth, 2);
    QCOMPARE(entries[3].webLink, QStringLiteral("https://example.com/roles"));
}

void NodeSearchTest::occurrencesHonourCaseAndWholeWords() {
    NodeSearch::Options options;
    using Ranges = QList<std::pair<int, int>>;
    QCOMPARE(NodeSearch::occurrences(QStringLiteral("Budget budget"), QStringLiteral("budget"), options), (Ranges{{0, 6}, {7, 6}}));
    options.matchCase = true;
    QCOMPARE(NodeSearch::occurrences(QStringLiteral("Budget budget"), QStringLiteral("budget"), options), (Ranges{{7, 6}}));
    options = {};
    options.wholeWords = true;
    QCOMPARE(NodeSearch::occurrences(QStringLiteral("Budgets and budget"), QStringLiteral("budget"), options), (Ranges{{12, 6}}));
    QCOMPARE(NodeSearch::occurrences(QStringLiteral("perché più"), QStringLiteral("più"), options), (Ranges{{7, 3}}));  // accents count as letters
    QVERIFY(NodeSearch::occurrences(QStringLiteral("anything"), QStringLiteral("  "), options).isEmpty());
    // As \b: underscores and digits are word characters, punctuation is not.
    QCOMPARE(NodeSearch::occurrences(QStringLiteral("my_budget budget2 (budget)"), QStringLiteral("budget"), options), (Ranges{{19, 6}}));
    QCOMPARE(NodeSearch::occurrences(QStringLiteral("aaa aa"), QStringLiteral("aa"), options), (Ranges{{4, 2}}));
}

void NodeSearchTest::fieldsCanBeFilteredOut() {
    const QList<NodeSearch::Entry> entries = NodeSearch::entries(QString(), m_model->toFlatSnapshot());
    QCOMPARE(entries.size(), 4);
    NodeSearch::Options options;
    QCOMPARE(NodeSearch::matchingFields(entries[1], QStringLiteral("finance"), options), int(NodeSearch::Note));
    QCOMPARE(NodeSearch::matchingFields(entries[3], QStringLiteral("people"), options), int(NodeSearch::Label));
    QCOMPARE(NodeSearch::matchingFields(entries[3], QStringLiteral("roles"), options), int(NodeSearch::Link));
    options.fields = NodeSearch::Title;
    QCOMPARE(NodeSearch::matchingFields(entries[1], QStringLiteral("finance"), options), 0);
    QCOMPARE(NodeSearch::matchingFields(entries[1], QStringLiteral("review"), options), int(NodeSearch::Title));
}

void NodeSearchTest::resultLineShowsWhereItWasFound() {
    const QList<NodeSearch::Entry> entries = NodeSearch::entries(QString(), m_model->toFlatSnapshot());
    const NodeSearch::Options options;
    using Ranges = QList<std::pair<int, int>>;
    // In the title: the title alone, its parts highlighted.
    NodeSearch::Shown line = NodeSearch::resultLine(entries[1], QStringLiteral("budget"), options, NodeSearch::Title);
    QCOMPARE(line.text, QStringLiteral("Budget review"));
    QCOMPARE(line.ranges, (Ranges{{0, 6}}));
    // In a link only: the title, " · " and the piece of the link.
    line = NodeSearch::resultLine(entries[3], QStringLiteral("roles"), options, NodeSearch::Link);
    QCOMPARE(line.text, QStringLiteral("Budget owner · https://example.com/roles"));
    QCOMPARE(line.ranges, (Ranges{{int(line.text.indexOf(QStringLiteral("roles"))), 5}}));
    // A long text: the words around the first match, after an ellipsis, on one line.
    const NodeSearch::Shown piece = NodeSearch::snippet(QString(60, QLatin1Char('a')) + QStringLiteral("\nfound here"), QStringLiteral("found"), options);
    QVERIFY(piece.text.startsWith(QStringLiteral("…")));
    QVERIFY(!piece.text.contains(QLatin1Char('\n')));
    QCOMPARE(piece.text.mid(piece.ranges.value(0).first, 5), QStringLiteral("found"));
}

void NodeSearchTest::findOpensThePanelAndShowsTheTree() {
    QAction *find = action(QStringLiteral("Find"));
    QVERIFY(find);
    QCOMPARE(find->shortcut(), QKeySequence(QKeySequence::Find));
    // The real key on the map (the action must be on the window: the menu bar is hidden).
    auto *view = m_window->findChild<MindMapView *>();
    view->setFocus();
    QTest::keyClick(view, Qt::Key_F, Qt::ControlModifier);
    QApplication::processEvents();
    QVERIFY(m_dock->isVisible());
    QVERIFY(m_panel->findField()->hasFocus());
    // Without text: the current map as a tree, central topic first with the sheet name.
    const QStringList shown = rows(m_panel->outlineList());
    QCOMPARE(shown.size(), 4);
    QVERIFY2(shown[0].endsWith(QStringLiteral("]Plan")), qPrintable(shown[0]));
    QVERIFY(shown[1].endsWith(QStringLiteral("Budget review")));
    QVERIFY(shown[3].endsWith(QStringLiteral("Budget owner")));
}

void NodeSearchTest::typingShowsOnlyMatchesHighlighted() {
    action(QStringLiteral("Find"))->trigger();
    search(QStringLiteral("budget"));
    QCOMPARE(rows(m_panel->outlineList()), (QStringList{QStringLiteral("Budget review"), QStringLiteral("Budget owner")}));
    const QList<QVariant> ranges = m_panel->outlineList()->topLevelItem(0)->data(0, Qt::UserRole + 2).toList();
    QCOMPARE(ranges.size(), 1);
    QCOMPARE(ranges.front().toPoint(), QPoint(0, 6));
    // Found in a note: the title, then the piece of the note with the match.
    search(QStringLiteral("finance"));
    const QStringList found = rows(m_panel->outlineList());
    QCOMPARE(found.size(), 1);
    QVERIFY2(found[0].startsWith(QStringLiteral("Budget review · ")) && found[0].contains(QStringLiteral("finance")), qPrintable(found[0]));
    // Without the Note field the note is not searched.
    for (QAction *option : m_panel->optionsButton()->menu()->actions())
        if (option->text() == QStringLiteral("Note")) option->setChecked(false);
    search(QStringLiteral("finance"));
    QVERIFY(rows(m_panel->outlineList()).isEmpty());
    for (QAction *option : m_panel->optionsButton()->menu()->actions())
        if (option->text() == QStringLiteral("Note")) option->setChecked(true);
}

void NodeSearchTest::clickSelectsTheTopicAndEscCloses() {
    action(QStringLiteral("Find"))->trigger();
    search(QStringLiteral("owner"));
    QTreeWidget *list = m_panel->outlineList();
    QCOMPARE(list->topLevelItemCount(), 1);
    emit list->itemClicked(list->topLevelItem(0), 0);
    QTRY_VERIFY(m_scene->selectedNode() && m_scene->selectedNode()->title == QStringLiteral("Budget owner"));
    // Enter in the field selects the first match.
    m_scene->clearSelection();
    search(QStringLiteral("review"));
    QTest::keyClick(m_panel->findField(), Qt::Key_Return);
    QTRY_VERIFY(m_scene->selectedNode() && m_scene->selectedNode()->title == QStringLiteral("Budget review"));
    m_panel->findField()->setFocus();
    QTest::keyClick(m_panel->findField(), Qt::Key_Escape);
    QVERIFY(!m_dock->isVisible());
}

void NodeSearchTest::notesAndTagsTabs() {
    action(QStringLiteral("Find"))->trigger();
    m_panel->showTab(NavigationPanel::Tab::Notes);
    QStringList notes = rows(m_panel->notesList());
    QCOMPARE(notes.size(), 1);
    QVERIFY(notes[0].startsWith(QStringLiteral("Budget review · Check the yearly costs")));
    m_panel->notesField()->setText(QStringLiteral("nothing like this"));
    m_panel->refresh();
    QVERIFY(rows(m_panel->notesList()).isEmpty());
    m_panel->notesField()->clear();
    m_panel->showTab(NavigationPanel::Tab::Tags);
    QListWidget *tags = m_panel->tagsList();
    QCOMPARE(tags->count(), 2);
    QCOMPARE(tags->item(0)->text(), QStringLiteral("money  (2)"));
    QCOMPARE(tags->item(1)->text(), QStringLiteral("people  (1)"));
}

void NodeSearchTest::currentFileFindsTopicsInOtherSheets() {
    auto *document = m_window->findChild<Document *>();
    document->addSheet([](MindMapModel &sheet) { sheet.addChild(sheet.root(), QStringLiteral("Budget forecast")); });
    QCOMPARE(document->currentSheet(), 1);
    QVERIFY(document->switchSheet(0).ok);
    QApplication::processEvents();
    action(QStringLiteral("Find"))->trigger();
    search(QStringLiteral("forecast"));
    QVERIFY(rows(m_panel->outlineList()).isEmpty());  // current map only
    for (QAction *option : m_panel->optionsButton()->menu()->actions())
        if (option->text() == QStringLiteral("Current file")) option->setChecked(true);
    search(QStringLiteral("forecast"));
    QTreeWidget *list = m_panel->outlineList();
    QCOMPARE(list->topLevelItemCount(), 1);  // the sheet as a group
    QTreeWidgetItem *match = list->topLevelItem(0)->child(0);
    QVERIFY(match);
    QCOMPARE(match->text(0), QStringLiteral("Budget forecast"));
    emit list->itemClicked(match, 0);
    QTRY_COMPARE(document->currentSheet(), 1);
    QTRY_VERIFY(m_scene->selectedNode() && m_scene->selectedNode()->title == QStringLiteral("Budget forecast"));
    for (QAction *option : m_panel->optionsButton()->menu()->actions())
        if (option->text() == QStringLiteral("Current map")) option->setChecked(true);
}

// Find and replace: Replace changes the selected result, Replace all every title found,
// also in other sheets with "Current file"; each is one step.
void NodeSearchTest::findAndReplaceTitles() {
    auto *document = m_window->findChild<Document *>();
    action(QStringLiteral("Find"))->trigger();
    QVERIFY(!m_panel->replaceField()->isVisible());
    for (QAction *option : m_panel->optionsButton()->menu()->actions())
        if (option->text() == QStringLiteral("Find and replace")) option->setChecked(true);
    QVERIFY(m_panel->replaceField()->isVisible());
    search(QStringLiteral("Budget"));
    m_panel->replaceField()->setText(QStringLiteral("Cost"));
    QVERIFY(!m_panel->replaceButton()->isEnabled() && m_panel->replaceAllButton()->isEnabled());
    QTreeWidget *list = m_panel->outlineList();
    list->setCurrentItem(list->topLevelItem(0));
    QVERIFY(m_panel->replaceButton()->isEnabled());
    m_panel->replaceButton()->click();
    MindNode *review = m_model->root()->children.at(0).get();
    MindNode *owner = m_model->root()->children.at(1)->children.at(0).get();
    QCOMPARE(review->title, QStringLiteral("Cost review"));
    QCOMPARE(owner->title, QStringLiteral("Budget owner"));
    QCOMPARE(rows(list), QStringList{QStringLiteral("Budget owner")});
    // Another sheet, searched with "Current file": Replace all changes both in one step.
    document->addSheet([](MindMapModel &sheet) { sheet.addChild(sheet.root(), QStringLiteral("Budget forecast")); });
    QVERIFY(document->switchSheet(0).ok);
    QApplication::processEvents();
    owner = m_model->root()->children.at(1)->children.at(0).get();
    for (QAction *option : m_panel->optionsButton()->menu()->actions())
        if (option->text() == QStringLiteral("Current file")) option->setChecked(true);
    search(QStringLiteral("Budget"));
    m_panel->replaceAllButton()->click();
    QCOMPARE(owner->title, QStringLiteral("Cost owner"));
    bool otherChanged = false;
    for (const QJsonObject &node : document->sheetContent(1)) otherChanged = otherChanged || node.value(QStringLiteral("title")).toString() == QStringLiteral("Cost forecast");
    QVERIFY(otherChanged);
    action(QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
    QCOMPARE(m_model->root()->children.at(1)->children.at(0)->title, QStringLiteral("Budget owner"));
    bool otherBack = false;
    for (const QJsonObject &node : document->sheetContent(1)) otherBack = otherBack || node.value(QStringLiteral("title")).toString() == QStringLiteral("Budget forecast");
    QVERIFY(otherBack);
    QCOMPARE(m_model->root()->children.at(0)->title, QStringLiteral("Cost review"));  // the earlier step stays
    for (QAction *option : m_panel->optionsButton()->menu()->actions()) {
        if (option->text() == QStringLiteral("Current map")) option->setChecked(true);
        if (option->text() == QStringLiteral("Find")) option->setChecked(true);
    }
    QVERIFY(!m_panel->replaceField()->isVisible());
}

// FEAT-060 in the Tags tab: F2 renames in the row (every sheet, one step), Esc cancels, renaming onto
// another tag merges them, Delete removes the tag everywhere.
void NodeSearchTest::tagsRenameAndDeleteOnEverySheet() {
    auto *document = m_window->findChild<Document *>();
    document->addSheet([](MindMapModel &sheet) { sheet.addChild(sheet.root(), QStringLiteral("Forecast"))->labels = {QStringLiteral("Money")}; });
    QVERIFY(document->switchSheet(0).ok);
    QApplication::processEvents();
    const auto otherLabels = [document] {
        QStringList labels;
        for (const QJsonObject &node : document->sheetContent(1))
            for (const auto label : node.value(QStringLiteral("labels")).toArray()) labels.append(label.toString());
        return labels;
    };
    MindNode *review = m_model->root()->children.at(0).get();
    MindNode *owner = m_model->root()->children.at(1)->children.at(0).get();
    action(QStringLiteral("Find"))->trigger();
    m_panel->showTab(NavigationPanel::Tab::Tags);
    QListWidget *tags = m_panel->tagsList();
    const auto rename = [&](int row, const QString &text, Qt::Key confirm) {
        tags->setCurrentRow(row);
        tags->setFocus();
        QTest::keyClick(tags, Qt::Key_F2);
        QLineEdit *field = nullptr;  // a closed editor waits for deletion: the visible one
        for (QLineEdit *candidate : tags->viewport()->findChildren<QLineEdit *>()) if (candidate->isVisible()) field = candidate;
        QVERIFY(field);
        QCOMPARE(field->text(), tags->item(row)->data(Qt::UserRole).toString());
        field->selectAll();
        QTest::keyClicks(field, text);
        QTest::keyClick(field, confirm);
        QApplication::processEvents();
        QApplication::processEvents();
    };
    rename(0, QStringLiteral("cost"), Qt::Key_Escape);
    QCOMPARE(review->labels, QStringList{QStringLiteral("money")});
    rename(0, QStringLiteral("cost"), Qt::Key_Return);
    QCOMPARE(review->labels, QStringList{QStringLiteral("cost")});
    QCOMPARE(owner->labels, (QStringList{QStringLiteral("cost"), QStringLiteral("people")}));
    QCOMPARE(otherLabels(), QStringList{QStringLiteral("cost")});  // "Money" is the same tag
    QCOMPARE(tags->item(0)->text(), QStringLiteral("cost  (2)"));
    action(QStringLiteral("Undo"))->trigger();  // one step for both sheets
    QApplication::processEvents();
    review = m_model->root()->children.at(0).get();
    owner = m_model->root()->children.at(1)->children.at(0).get();
    QCOMPARE(owner->labels, (QStringList{QStringLiteral("money"), QStringLiteral("people")}));
    QCOMPARE(otherLabels(), QStringList{QStringLiteral("Money")});
    // "people" onto "Money": the owner keeps one, spelled as typed.
    m_panel->refresh();
    QCOMPARE(tags->item(1)->text(), QStringLiteral("people  (1)"));
    rename(1, QStringLiteral("Money"), Qt::Key_Return);
    QCOMPARE(owner->labels, QStringList{QStringLiteral("Money")});
    QCOMPARE(review->labels, QStringList{QStringLiteral("money")});
    QCOMPARE(tags->count(), 1);  // "money" and "Money" are one row
    QCOMPARE(tags->item(0)->text(), QStringLiteral("money  (2)"));
    // Delete: gone from every topic of every sheet; the window's Delete does not remove a topic.
    const int topics = int(m_model->toFlatSnapshot().size());
    tags->setCurrentRow(0);
    tags->setFocus();
    QTest::keyClick(tags, Qt::Key_Delete);
    QApplication::processEvents();
    QVERIFY(review->labels.isEmpty() && owner->labels.isEmpty());
    QVERIFY(otherLabels().isEmpty());
    QCOMPARE(int(m_model->toFlatSnapshot().size()), topics);
    QCOMPARE(tags->count(), 0);
}
// Card target: results while typing in under 50 ms with 5,000 topics on the Light profile (set in initTestCase).
void NodeSearchTest::fiveThousandTopicsOnTheLightProfile() {
    MENTIS_SLOW_TEST();
    m_model->clear(QStringLiteral("Plan"));
    for (int branch = 0; branch < 50; ++branch) {
        MindNode *main = m_model->addChild(m_model->root(), QStringLiteral("Chapter %1").arg(branch));
        for (int leaf = 0; leaf < 99; ++leaf) {
            MindNode *topic = m_model->addChild(main, QStringLiteral("Topic %1 of chapter %2").arg(leaf).arg(branch));
            if (leaf % 3 == 0) topic->notes = QStringLiteral("Notes about the subject number %1, with some longer text to read").arg(leaf);
            if (leaf % 5 == 0) topic->labels = {QStringLiteral("exam"), QStringLiteral("week %1").arg(leaf % 7)};
        }
    }
    m_model->refreshStyles();
    m_window->findChild<Document *>()->startUntitled();
    action(QStringLiteral("Find"))->trigger();
    // Middle of three runs, so a busy machine does not decide the result.
    const auto timed = [this](const QString &query) {
        QList<qint64> runs;
        for (int run = 0; run < 3; ++run) {
            QElapsedTimer timer;
            timer.start();
            search(query);
            runs.append(timer.elapsed());
        }
        std::sort(runs.begin(), runs.end());
        qInfo("%s: %lld ms, %d rows", qPrintable(query.isEmpty() ? QStringLiteral("(tree)") : query), runs[1], int(rows(m_panel->outlineList()).size()));
        return runs[1];
    };
    const auto setWholeWords = [this](bool on) {
        for (QAction *option : m_panel->optionsButton()->menu()->actions())
            if (option->text() == QStringLiteral("Whole words")) option->setChecked(on);
    };
    timed(QString());  // the tree, also shown when the field is emptied
    const qint64 rare = timed(QStringLiteral("subject number 42,"));
    const qint64 none = timed(QStringLiteral("nothing like this"));
    const qint64 every = timed(QStringLiteral("topic"));
    setWholeWords(true);
    const qint64 words = timed(QStringLiteral("chapter"));
    setWholeWords(false);
    QVERIFY2(rare < 50 && none < 50 && every < 50 && words < 50, "a search typed in the field must answer in under 50 ms");
}

QTEST_MAIN(NodeSearchTest)
#include "node_search_test.moc"
