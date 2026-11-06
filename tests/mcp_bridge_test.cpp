// FEAT-045 phase 2: an agent works on the map open in a real window. The agent's side (the tools of
// "Mentis --mcp") runs on another thread, as another process would, while the window handles
// events. Calls reach the window, each change is one Undo step, the text being typed is kept, the
// agent's sheet is shown and its topic selected, a map without a name can be read and saved, and
// with agents off there is no channel. open_map opens a map after the user's yes.
#include "document.h"
#include "mainwindow.h"
#include "mcpbridge.h"
#include "mcpmaptools.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "toggleswitch.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QListWidget>
#include <QTest>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <memory>

class McpBridgeTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void offMeansNoChannel();
    void readAndChangeTheOpenMap();
    void typedTextIsKeptAndSheetShown();
    void mapWithoutANameIsSaved();
    void openFileGoesToTheWindow();
    void deletingASheetAsksInTheWindow();
    void agentsOpenTheWindows();
    void openingAMapAsksTheUser();

private:
    // A tool as "Mentis --mcp" runs it, on another thread; the window keeps handling events.
    McpServer::ToolResult call(const QString &tool, const QJsonObject &args);
    void allow(bool enabled);
    Document *document() const { return m_window->findChild<Document *>(); }
    MindMapModel *model() const { return m_window->findChild<MindMapModel *>(); }

    QTemporaryDir m_dir;
    std::unique_ptr<MainWindow> m_window;
    QList<McpServer::Tool> m_tools;
};

void McpBridgeTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);  // the windows' entries in a test folder
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisMcpBridgeTest"));
    QVERIFY(m_dir.isValid());
    allow(true);
    m_window = std::make_unique<MainWindow>();
    m_window->resize(1200, 800);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    qobject_cast<QStackedWidget *>(m_window->centralWidget())->setCurrentIndex(1);
    m_tools = McpMapTools::tools(&McpMapTools::Access::load);
}

void McpBridgeTest::cleanupTestCase() {
    m_window.reset();
    allow(false);
}

void McpBridgeTest::allow(bool enabled) { McpMapTools::Access{enabled, {}}.save(); }

McpServer::ToolResult McpBridgeTest::call(const QString &tool, const QJsonObject &args) {
    const auto found = std::find_if(m_tools.cbegin(), m_tools.cend(), [&tool](const McpServer::Tool &t) { return t.name == tool; });
    if (found == m_tools.cend()) return McpServer::ToolResult::error(QStringLiteral("no tool"));
    QFuture<McpServer::ToolResult> future = QtConcurrent::run([found, args] { return found->call(args); });
    while (!future.isFinished()) QTest::qWait(5);
    return future.result();
}

void McpBridgeTest::offMeansNoChannel() {
    auto *host = m_window->findChild<McpBridge::Host *>();
    QVERIFY(host);
    QVERIFY(host->isListening());
    QCOMPARE(McpBridge::instances().size(), 1);
    allow(false);
    host->setListening(false);  // what the Settings switch does
    QVERIFY(McpBridge::instances().isEmpty());
    QVERIFY(call(QStringLiteral("read_map"), {}).isError);
    allow(true);
    host->setListening(true);
    QCOMPARE(McpBridge::instances().size(), 1);
}

void McpBridgeTest::readAndChangeTheOpenMap() {
    model()->clear(QStringLiteral("Esame"));
    document()->startUntitled();
    const McpServer::ToolResult read = call(QStringLiteral("read_map"), {});
    QVERIFY2(!read.isError, qPrintable(read.text));
    QCOMPARE(read.structured.value("root").toObject().value("title").toString(), QStringLiteral("Esame"));
    QCOMPARE(read.structured.value("path").toString(), QString());  // no name yet
    const QString rootId = read.structured.value("root").toObject().value("id").toString();
    const bool couldUndo = document()->canUndo();
    const McpServer::ToolResult added = call(QStringLiteral("add_topic"), {{"parent_id", rootId}, {"title", "Capitolo 1"}, {"notes", "da studiare"}});
    QVERIFY2(!added.isError, qPrintable(added.text));
    MindNode *chapter = model()->nodeById(added.structured.value("id").toString());
    QVERIFY(chapter);
    QCOMPARE(chapter->title, QStringLiteral("Capitolo 1"));
    QCOMPARE(m_window->findChild<MindMapScene *>()->selectedNodes(), QList<MindNode *>{chapter});
    QVERIFY(m_window->statusBar()->currentMessage().contains(QStringLiteral("Capitolo 1")));
    QVERIFY(document()->canUndo() || !couldUndo);
    // One Undo step takes the agent's change away.
    QCOMPARE(document()->undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(model()->root()->children.empty());
    QVERIFY(call(QStringLiteral("delete_topic"), {{"id", rootId}}).text.contains("central"));
    // While a dialog waits for the user the map does not change.
    QDialog dialog(m_window.get());
    dialog.setModal(true);
    dialog.show();
    QTRY_VERIFY(QApplication::activeModalWidget());
    QVERIFY(call(QStringLiteral("add_topic"), {{"parent_id", rootId}, {"title", "No"}}).text.contains("waiting"));
    dialog.close();
    QVERIFY(model()->root()->children.empty());
}

void McpBridgeTest::typedTextIsKeptAndSheetShown() {
    model()->clear(QStringLiteral("Primo"));
    document()->startUntitled();
    NodeItem *item = nullptr;
    for (QGraphicsItem *candidate : m_window->findChild<MindMapScene *>()->items())
        if (auto *node = qgraphicsitem_cast<NodeItem *>(candidate); node && node->node() == model()->root()) item = node;
    QVERIFY(item);
    item->beginEditing();
    QApplication::processEvents();
    QTest::keyClicks(m_window->findChild<MindMapView *>()->viewport(), QStringLiteral(" scritto"));
    // The agent adds a sheet while the user types: the text is confirmed first.
    const McpServer::ToolResult sheet = call(QStringLiteral("add_sheet"), {{"title", "Ripasso"}});
    QVERIFY2(!sheet.isError, qPrintable(sheet.text));
    QCOMPARE(document()->sheets().size(), 2);
    QCOMPARE(document()->sheets()[0].content.isEmpty(), false);
    QVERIFY(document()->sheetContent(0).value(document()->sheetContent(0).value(QStringLiteral("@map")).value(QStringLiteral("rootId")).toString())
                .value(QStringLiteral("title")).toString().contains(QStringLiteral("scritto")));
    // Working on the first sheet shows it.
    const QString first = document()->sheets()[0].id;
    QVERIFY(!call(QStringLiteral("read_map"), {{"sheet", first}}).isError);
    QCOMPARE(document()->currentSheetId(), first);
    QCOMPARE(m_window->findChild<QComboBox *>(QStringLiteral("sheetSelect"))->currentIndex(), 0);
}

void McpBridgeTest::mapWithoutANameIsSaved() {
    model()->clear(QStringLiteral("Senza nome"));
    document()->startUntitled();
    QVERIFY(call(QStringLiteral("save_open_map"), {}).text.contains("no name"));
    QVERIFY(call(QStringLiteral("save_open_map"), {{"path", QDir(m_dir.path()).filePath("manca/fuori-mcp.mentis")}}).isError);
    const QString path = QDir(m_dir.path()).filePath("salvata.mentis");
    const McpServer::ToolResult saved = call(QStringLiteral("save_open_map"), {{"path", path}});
    QVERIFY2(!saved.isError, qPrintable(saved.text));
    QCOMPARE(QFileInfo(document()->filePath()).canonicalFilePath(), QFileInfo(path).canonicalFilePath());
    QVERIFY(m_window->statusBar()->currentMessage().contains(QStringLiteral("saved")));
}

void McpBridgeTest::openFileGoesToTheWindow() {
    // The map saved above is open in the window: a call with its path changes the window, not the file.
    const QString path = document()->filePath();
    QVERIFY(!path.isEmpty());
    const McpServer::ToolResult read = call(QStringLiteral("read_map"), {{"path", path}});
    QVERIFY2(!read.isError, qPrintable(read.text));
    const QString rootId = read.structured.value("root").toObject().value("id").toString();
    QVERIFY(!call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId}, {"title", "Dalla finestra"}}).isError);
    QCOMPARE(model()->root()->children.back()->title, QStringLiteral("Dalla finestra"));
}

// Phase 3: with the map open, deleting a sheet asks the user in Mentis; confirm: true from the
// agent does not skip the question.
void McpBridgeTest::deletingASheetAsksInTheWindow() {
    document()->addSheet();
    const QString sheet = document()->currentSheetId();
    const int before = int(document()->sheets().size());
    // The user says no.
    QTimer::singleShot(200, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
    const McpServer::ToolResult refused = call(QStringLiteral("delete_sheet"), {{"sheet", sheet}, {"confirm", true}});
    QVERIFY(refused.isError);
    QVERIFY(refused.text.contains("did not confirm"));
    QCOMPARE(int(document()->sheets().size()), before);
    // The user says yes.
    QTimer::singleShot(200, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->accept(); });
    const McpServer::ToolResult deleted = call(QStringLiteral("delete_sheet"), {{"sheet", sheet}});
    QVERIFY2(!deleted.isError, qPrintable(deleted.text));
    QCOMPARE(int(document()->sheets().size()), before - 1);
}

// FEAT-045: open_dialog opens a window of the app for the user, with the agent's choices; the agent
// gets its answer before the user closes it.
void McpBridgeTest::agentsOpenTheWindows() {
    // The window runs its own loop until it closes: a timer looks at it and closes it, then the test
    // goes on with what the timer saw.
    const auto open = [this](const QJsonObject &args, const std::function<void(QDialog *)> &check) {
        bool seen = false;
        std::function<void()> look;
        look = [&] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                check(dialog);
                seen = true;
                dialog->reject();
            } else {
                QTimer::singleShot(20, look);
            }
        };
        QTimer::singleShot(20, look);
        const McpServer::ToolResult opened = call(QStringLiteral("open_dialog"), args);
        if (opened.isError) { qWarning() << opened.text; return false; }
        for (int waited = 0; (!seen || QApplication::activeModalWidget()) && waited < 5000; waited += 10) QTest::qWait(10);
        return seen && !QApplication::activeModalWidget();
    };
    QString shown;
    bool frameOn = false, background = true;
    QVERIFY(open({{"dialog", "export_image"}, {"background", false},
                  {"frame", QJsonObject{{"style", "dashed"}, {"thickness", 6}, {"color", "#16a34a"}}}},
                 [&](QDialog *dialog) {
                     shown = QString::fromLatin1(dialog->metaObject()->className());
                     frameOn = dialog->findChild<ToggleSwitch *>(QStringLiteral("ExportFrame"))->isChecked();
                     background = dialog->findChild<ToggleSwitch *>(QStringLiteral("ExportBackground"))->isChecked();
                 }));
    QCOMPARE(shown, QStringLiteral("ExportDialog"));
    QVERIFY(frameOn);
    QVERIFY(!background);
    QVERIFY(open({{"dialog", "gantt_resources"}}, [&](QDialog *dialog) { shown = QString::fromLatin1(dialog->metaObject()->className()); }));
    QCOMPARE(shown, QStringLiteral("ResourcesDialog"));
    int page = -1;
    QVERIFY(open({{"dialog", "settings"}, {"page", "appearance"}},
                 [&](QDialog *dialog) { page = dialog->findChild<QListWidget *>(QStringLiteral("SettingsTabs"))->currentRow(); }));
    QCOMPARE(page, 1);
    QVERIFY(open({{"dialog", "formula"}, {"id", model()->root()->id}}, [&](QDialog *dialog) { shown = QString::fromLatin1(dialog->metaObject()->className()); }));
    QCOMPARE(shown, QStringLiteral("FormulaDialog"));
    QVERIFY(call(QStringLiteral("open_dialog"), {{"dialog", "nope"}}).isError);
    QVERIFY(call(QStringLiteral("open_dialog"), {{"dialog", "formula"}, {"id", "nessuno"}}).isError);
}

// FEAT-045 (2026-11-06): a map the agent made opens in the window only after the user's yes; the
// window's own changes are saved first; a map already open is only shown.
void McpBridgeTest::openingAMapAsksTheUser() {
    const QString made = QDir(m_dir.path()).filePath("aperta.mentis");
    QVERIFY(!call(QStringLiteral("create_map"), {{"path", made}, {"title", "Nuova"}}).isError);
    const QString before = document()->filePath();
    // The user says no (confirm: true from the agent does not skip the question).
    QTimer::singleShot(200, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
    const McpServer::ToolResult refused = call(QStringLiteral("open_map"), {{"path", made}, {"confirm", true}});
    QVERIFY(refused.isError);
    QCOMPARE(document()->filePath(), before);
    // The user says yes; a question about the window's unsaved changes is answered with Save.
    QTimer accept;
    QObject::connect(&accept, &QTimer::timeout, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->accept(); });
    accept.start(100);
    const McpServer::ToolResult opened = call(QStringLiteral("open_map"), {{"path", made}});
    accept.stop();
    QVERIFY2(!opened.isError, qPrintable(opened.text));
    QCOMPARE(QFileInfo(document()->filePath()).canonicalFilePath(), QFileInfo(made).canonicalFilePath());
    QCOMPARE(model()->root()->title, QStringLiteral("Nuova"));
    // Already open: nothing to ask.
    QVERIFY(!call(QStringLiteral("open_map"), {{"path", made}}).isError);
    QVERIFY(call(QStringLiteral("open_map"), {{"path", QDir(m_dir.path()).filePath("manca.mentis")}}).isError);
}

QTEST_MAIN(McpBridgeTest)
#include "mcp_bridge_test.moc"
