// FEAT-045: the tools of mentis-mcp on real .mentis files in a temporary folder: create, read,
// sheets, topics, Markdown, the checks (server off, invalid paths, the app's lock, central
// topic, own branch, limits) and the bug report that sends nothing.
#include "bmapstorage.h"
#include "custompalettes.h"
#include "mcpclients.h"
#include "document.h"
#include "ganttplan.h"
#include "ganttresources.h"
#include "iconlibrary.h"
#include "iconlibrary.h"
#include "mcpmaptools.h"
#include "mapstructure.h"
#include "mindmapmodel.h"
#include "nodestyle.h"
#include "pitchtemplate.h"
#include "relationship.h"

#include <QSettings>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

class McpToolsTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void offOrInvalidPathsDoNothing();
    void createAndReadAMap();
    void topicsAreAddedChangedMovedAndDeleted();
    void sheetsAndMarkdown();
    void renameOnlySheet();
    void listRecentMapsAndFolders();
    void agentAppsGetMentis();
    void mapOpenInTheAppIsLocked();
    void limitsAndBadArguments();
    void reportIssueSendsNothing();
    void styleAndContentsOfTopics();
    void relationshipsSummariesBoundaries();
    void sheetsAndStructure();
    void ganttPlan();
    void exportsPitchAndReplacing();
    void stickerAndMarkerLibrary();
    void coloursAndText();
    void namesCalendarAndReplacing();
    void relationshipEndsCurveAndStyle();
    void sharesCurrenciesAndGanttStyle();
    void picturePlacementAndWidth();
    void pitchTemplates();

private:
    McpServer::ToolResult call(const QString &tool, const QJsonObject &args);
    QString file(const QString &name) const { return QDir(m_allowed.path()).filePath(name); }
    QJsonObject read(const QString &path, const QString &sheet = {});
    static QStringList childTitles(const QJsonObject &topic);

    QTemporaryDir m_allowed;
    QTemporaryDir m_outside;
    McpMapTools::Access m_access;
    QList<McpServer::Tool> m_tools;
};

void McpToolsTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisMcpToolsTest"));
    QVERIFY(m_allowed.isValid() && m_outside.isValid());
    m_access = {true, {}};
    m_tools = McpMapTools::tools([this] { return m_access; });
}

McpServer::ToolResult McpToolsTest::call(const QString &tool, const QJsonObject &args) {
    for (const McpServer::Tool &candidate : std::as_const(m_tools))
        if (candidate.name == tool) return candidate.call(args);
    return McpServer::ToolResult::error(QStringLiteral("no tool ") + tool);
}

QJsonObject McpToolsTest::read(const QString &path, const QString &sheet) {
    QJsonObject args{{"path", path}};
    if (!sheet.isEmpty()) args.insert("sheet", sheet);
    const McpServer::ToolResult result = call(QStringLiteral("read_map"), args);
    if (result.isError) qWarning() << result.text;
    return result.structured;
}

QStringList McpToolsTest::childTitles(const QJsonObject &topic) {
    QStringList titles;
    for (const QJsonValue &child : topic.value("children").toArray()) titles.append(child.toObject().value("title").toString());
    return titles;
}

void McpToolsTest::offOrInvalidPathsDoNothing() {
    m_access.enabled = false;
    const McpServer::ToolResult off = call(QStringLiteral("create_map"), {{"path", file("spento.mentis")}});
    QVERIFY(off.isError);
    QVERIFY(off.text.contains("Settings"));
    QVERIFY(!QFileInfo::exists(file("spento.mentis")));
    QVERIFY(call(QStringLiteral("list_maps"), {}).isError);
    m_access.enabled = true;
    // Any folder (no allowed folders since 2026-11-06), ".." resolved; relative paths, other
    // extensions and missing folders are refused.
    const QString elsewhere = m_allowed.path() + QStringLiteral("/../") + QFileInfo(m_outside.path()).fileName() + QStringLiteral("/via.mentis");
    QVERIFY(!call(QStringLiteral("create_map"), {{"path", elsewhere}}).isError);
    QVERIFY(QFileInfo::exists(QDir(m_outside.path()).filePath("via.mentis")));
    QVERIFY(call(QStringLiteral("create_map"), {{"path", "relativo.mentis"}}).isError);
    QVERIFY(call(QStringLiteral("create_map"), {{"path", file("testo.txt")}}).isError);
    QVERIFY(call(QStringLiteral("create_map"), {{"path", file("manca/nuova.mentis")}}).text.contains("does not exist"));
    // open_map with no Mentis window: only after the user's yes in the chat (confirm: true starts
    // Mentis, not tried here).
    QVERIFY(call(QStringLiteral("open_map"), {{"path", QDir(m_outside.path()).filePath("via.mentis")}}).text.contains("confirm"));
}

void McpToolsTest::createAndReadAMap() {
    const QString path = file("progetto.mentis");
    const McpServer::ToolResult created = call(QStringLiteral("create_map"), {{"path", path}, {"title", "Progetto"}});
    QVERIFY2(!created.isError, qPrintable(created.text));
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(!QFileInfo::exists(path + ".lock"));  // closed after the call
    // An existing map is replaced only with the user's yes (no Mentis window: confirm: true).
    QVERIFY(call(QStringLiteral("create_map"), {{"path", path}}).text.contains("confirmation"));
    QVERIFY(QFileInfo::exists(path));
    const QJsonObject map = read(path);
    QCOMPARE(map.value("sheets").toArray().size(), 1);
    QCOMPARE(map.value("root").toObject().value("title").toString(), QStringLiteral("Progetto"));
    QCOMPARE(map.value("root").toObject().value("id").toString(), created.structured.value("rootId").toString());
    QVERIFY(!map.value("truncated").toBool());
    // With Markdown the outline is the map.
    const QString outline = file("scaletta.mentis");
    QVERIFY(!call(QStringLiteral("create_map"), {{"path", outline}, {"markdown", "# Viaggio\n- Treni\n  - Orari\n- Alberghi\n"}}).isError);
    const QJsonObject root = read(outline).value("root").toObject();
    QCOMPARE(root.value("title").toString(), QStringLiteral("Viaggio"));
    QCOMPARE(childTitles(root), (QStringList{"Treni", "Alberghi"}));
}

void McpToolsTest::topicsAreAddedChangedMovedAndDeleted() {
    const QString path = file("argomenti.mentis");
    const QString rootId = call(QStringLiteral("create_map"), {{"path", path}, {"title", "Centro"}}).structured.value("rootId").toString();
    const auto add = [&](const QString &parent, const QString &title, QJsonObject extra = {}) {
        extra.insert("path", path);
        extra.insert("parent_id", parent);
        extra.insert("title", title);
        const McpServer::ToolResult result = call(QStringLiteral("add_topic"), extra);
        return result.isError ? QString() : result.structured.value("id").toString();
    };
    const QString a = add(rootId, "A");
    const QString b = add(rootId, "B");
    const QString first = add(rootId, "Primo", {{"index", 0}, {"notes", "una nota"}, {"labels", QJsonArray{"x", "y"}}});
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !first.isEmpty());
    QJsonObject root = read(path).value("root").toObject();
    QCOMPARE(childTitles(root), (QStringList{"Primo", "A", "B"}));
    const QJsonObject primo = root.value("children").toArray().first().toObject();
    QCOMPARE(primo.value("notes").toString(), QStringLiteral("una nota"));
    QCOMPARE(primo.value("labels").toArray().size(), 2);

    QVERIFY(!call(QStringLiteral("update_topic"), {{"path", path}, {"id", a}, {"title", "A2"}, {"notes", ""}}).isError);
    QVERIFY(call(QStringLiteral("update_topic"), {{"path", path}, {"id", a}}).isError);  // nothing to change
    // Moves: down in the same parent, under another topic, then into its own branch (refused).
    QVERIFY(!call(QStringLiteral("move_topic"), {{"path", path}, {"id", first}, {"index", 2}}).isError);
    QCOMPARE(childTitles(read(path).value("root").toObject()), (QStringList{"A2", "B", "Primo"}));
    QVERIFY(!call(QStringLiteral("move_topic"), {{"path", path}, {"id", b}, {"parent_id", a}}).isError);
    root = read(path).value("root").toObject();
    QCOMPARE(childTitles(root), (QStringList{"A2", "Primo"}));
    QCOMPARE(childTitles(root.value("children").toArray().first().toObject()), QStringList{"B"});
    QVERIFY(call(QStringLiteral("move_topic"), {{"path", path}, {"id", a}, {"parent_id", b}}).text.contains("own branch"));
    QVERIFY(call(QStringLiteral("move_topic"), {{"path", path}, {"id", rootId}, {"parent_id", a}}).isError);
    QVERIFY(call(QStringLiteral("delete_topic"), {{"path", path}, {"id", rootId}}).text.contains("central"));
    QVERIFY(!call(QStringLiteral("delete_topic"), {{"path", path}, {"id", a}}).isError);  // with B
    QCOMPARE(childTitles(read(path).value("root").toObject()), QStringList{"Primo"});
    QVERIFY(call(QStringLiteral("update_topic"), {{"path", path}, {"id", b}, {"title", "x"}}).text.contains("No topic"));

    // The Home's preview follows the agent's changes.
    const QByteArray previewBefore = BmapStorage::loadPreview(path);
    QVERIFY(!previewBefore.isEmpty());
    // The file opens in the app laid out: the topics are not stacked on the central one.
    add(rootId, "Secondo");
    QVERIFY(BmapStorage::loadPreview(path) != previewBefore);
    Document document;
    QVERIFY(document.open(path, false).ok);
    const MindNode *centre = document.model()->root();
    QCOMPARE(int(centre->children.size()), 2);
    QVERIFY(centre->children[0]->position != centre->position);
    QVERIFY(centre->children[0]->position != centre->children[1]->position);
    document.close();
}

void McpToolsTest::sheetsAndMarkdown() {
    const QString path = file("fogli.mentis");
    const QString rootId = call(QStringLiteral("create_map"), {{"path", path}, {"title", "Uno"}}).structured.value("rootId").toString();
    const McpServer::ToolResult added = call(QStringLiteral("add_sheet"), {{"path", path}, {"title", "Ricerca"}});
    QVERIFY2(!added.isError, qPrintable(added.text));
    const QString sheet = added.structured.value("sheet").toString();
    QJsonObject map = read(path, sheet);
    QCOMPARE(map.value("sheets").toArray().size(), 2);
    QCOMPARE(map.value("sheets").toArray()[1].toObject().value("title").toString(), QStringLiteral("Ricerca"));
    QCOMPARE(map.value("root").toObject().value("title").toString(), QStringLiteral("Ricerca"));
    QCOMPARE(read(path).value("root").toObject().value("title").toString(), QStringLiteral("Uno"));  // the first by default
    QVERIFY(!call(QStringLiteral("rename_sheet"), {{"path", path}, {"sheet", sheet}, {"title", "Fonti"}}).isError);
    QCOMPARE(read(path, sheet).value("sheets").toArray()[1].toObject().value("title").toString(), QStringLiteral("Fonti"));
    QVERIFY(call(QStringLiteral("read_map"), {{"path", path}, {"sheet", "nessuno"}}).text.contains("No sheet"));
    // Markdown under a topic, and back out.
    const McpServer::ToolResult pasted = call(QStringLiteral("import_markdown"),
        {{"path", path}, {"parent_id", rootId}, {"markdown", "- Libri\n  - Saggi\n- Articoli\n"}});
    QVERIFY2(!pasted.isError, qPrintable(pasted.text));
    QCOMPARE(childTitles(read(path).value("root").toObject()), (QStringList{"Libri", "Articoli"}));
    const McpServer::ToolResult exported = call(QStringLiteral("export_markdown"), {{"path", path}});
    QVERIFY(exported.text.contains("Libri") && exported.text.contains("Saggi"));
    QVERIFY(call(QStringLiteral("import_markdown"), {{"path", path}, {"parent_id", rootId}, {"markdown", "  \n "}}).isError);  // nothing to add
    const McpServer::ToolResult outlined = call(QStringLiteral("add_sheet"), {{"path", path}, {"title", "Piano"}, {"markdown", "# Piano\n- Fase 1\n- Fase 2\n"}});
    QCOMPARE(childTitles(read(path, outlined.structured.value("sheet").toString()).value("root").toObject()), (QStringList{"Fase 1", "Fase 2"}));
}

// BUG-033: the only sheet of a closed file keeps its new name.
void McpToolsTest::renameOnlySheet() {
    const QString path = file("solo.mentis");
    QVERIFY(!call(QStringLiteral("create_map"), {{"path", path}, {"title", "Uno"}}).isError);
    const QJsonArray before = read(path).value("sheets").toArray();
    QCOMPARE(before.size(), 1);
    const QString sheet = before[0].toObject().value("id").toString();
    const McpServer::ToolResult renamed = call(QStringLiteral("rename_sheet"), {{"path", path}, {"sheet", sheet}, {"title", "Ricerca"}});
    QVERIFY2(!renamed.isError, qPrintable(renamed.text));
    QCOMPARE(read(path).value("sheets").toArray()[0].toObject().value("title").toString(), QStringLiteral("Ricerca"));
    // A second sheet added later leaves the first one's name as it is.
    QVERIFY(!call(QStringLiteral("add_sheet"), {{"path", path}, {"title", "Due"}}).isError);
    const QJsonArray after = read(path).value("sheets").toArray();
    QCOMPARE(after.size(), 2);
    QCOMPARE(after[0].toObject().value("title").toString(), QStringLiteral("Ricerca"));
    QCOMPARE(after[1].toObject().value("title").toString(), QStringLiteral("Due"));
}

// FEAT-045 (2026-11-06): the agents' apps get the server "mentis"; what else is in their files stays.
void McpToolsTest::agentAppsGetMentis() {
    QTemporaryDir apps;
    QVERIFY(apps.isValid());
    const QString command = QStringLiteral("C:\\Programs\\Mentis \"beta\"\\Mentis.exe");
    QDir(apps.path()).mkpath("Claude");
    QDir(apps.path()).mkpath("codex");
    const QString desktop = apps.filePath("Claude/claude_desktop_config.json");
    const QString codex = apps.filePath("codex/config.toml");
    QFile json(desktop);
    QVERIFY(json.open(QIODevice::WriteOnly));
    json.write("{\"theme\": \"dark\", \"mcpServers\": {\"other\": {\"command\": \"x\"}, \"mentis\": {\"command\": \"old\"}}}");
    json.close();
    QFile toml(codex);
    QVERIFY(toml.open(QIODevice::WriteOnly));
    toml.write("model = \"o4\"\n\n[mcp_servers.mentis]\ncommand = \"old\"\n\n[mcp_servers.mentis.env]\nA = \"1\"\n\n"
               "[mcp_servers.other]\ncommand = \"y\"\n");
    toml.close();
    const McpClients::Locations where{apps.filePath("Claude"), QString(), apps.filePath("codex")};
    for (int round = 0; round < 2; ++round) {  // a second time changes nothing more
        const QList<McpClients::Outcome> outcomes = McpClients::connectAll(where, command);
        QCOMPARE(outcomes.size(), 2);
        for (const McpClients::Outcome &outcome : outcomes) QVERIFY2(outcome.ok, qPrintable(outcome.app + outcome.error));
    }
    QVERIFY(json.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(json.readAll()).object();
    json.close();
    QCOMPARE(root.value("theme").toString(), QStringLiteral("dark"));
    const QJsonObject servers = root.value("mcpServers").toObject();
    QCOMPARE(servers.value("other").toObject().value("command").toString(), QStringLiteral("x"));
    QCOMPARE(servers.value("mentis").toObject().value("command").toString(), command);
    QCOMPARE(servers.value("mentis").toObject().value("args").toArray(), QJsonArray{QStringLiteral("--mcp")});
    QVERIFY(toml.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(toml.readAll());
    toml.close();
    QVERIFY2(text.contains(QStringLiteral("model = \"o4\"")) && text.contains(QStringLiteral("[mcp_servers.other]\ncommand = \"y\"")), qPrintable(text));
    QCOMPARE(text.count(QStringLiteral("[mcp_servers.mentis]")), 1);
    QVERIFY(!text.contains(QStringLiteral("mentis.env")) && !text.contains(QStringLiteral("\"old\"")));
    QVERIFY2(text.contains(QStringLiteral("command = \"C:\\\\Programs\\\\Mentis \\\"beta\\\"\\\\Mentis.exe\"")), qPrintable(text));
    // A file that is not JSON is left as it is.
    QVERIFY(json.open(QIODevice::WriteOnly));
    json.write("{ broken");
    json.close();
    QString error;
    QVERIFY(!McpClients::writeClaudeDesktop(desktop, command, &error));
    QVERIFY(json.open(QIODevice::ReadOnly));
    QCOMPARE(json.readAll(), QByteArray("{ broken"));
}

void McpToolsTest::listRecentMapsAndFolders() {
    QDir(m_allowed.path()).mkpath("sotto");
    QVERIFY(!call(QStringLiteral("create_map"), {{"path", file("sotto/interna.mentis")}}).isError);
    const QJsonObject all = call(QStringLiteral("list_maps"), {{"folder", m_allowed.path()}}).structured;
    QStringList names;
    for (const QJsonValue &map : all.value("maps").toArray()) names.append(QFileInfo(map.toObject().value("path").toString()).fileName());
    QVERIFY(names.contains("interna.mentis") && names.contains("progetto.mentis"));
    const QJsonObject inner = call(QStringLiteral("list_maps"), {{"folder", file("sotto")}}).structured;
    QCOMPARE(inner.value("maps").toArray().size(), 1);
    QVERIFY(call(QStringLiteral("list_maps"), {{"folder", file("manca")}}).isError);
    // Without folder: the Home's recent maps still on disk.
    QSettings().setValue(QStringLiteral("recentFiles"), QStringList{file("progetto.mentis"), file("sparita.mentis")});
    const QJsonArray recent = call(QStringLiteral("list_maps"), {}).structured.value("maps").toArray();
    QCOMPARE(recent.size(), 1);
    QCOMPARE(QFileInfo(recent[0].toObject().value("path").toString()).fileName(), QStringLiteral("progetto.mentis"));
    QSettings().remove(QStringLiteral("recentFiles"));
}

void McpToolsTest::mapOpenInTheAppIsLocked() {
    const QString path = file("aperta.mentis");
    const QString rootId = call(QStringLiteral("create_map"), {{"path", path}}).structured.value("rootId").toString();
    Document app;  // as the Mentis window holding the file
    QVERIFY(app.open(path, false).ok);
    const McpServer::ToolResult locked = call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId}, {"title", "No"}});
    QVERIFY(locked.isError);
    QVERIFY(locked.text.contains("open in Mentis"));
    app.close();
    QVERIFY(!call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId}, {"title", "Ora si"}}).isError);
}

void McpToolsTest::limitsAndBadArguments() {
    const QString path = file("limiti.mentis");
    const QString rootId = call(QStringLiteral("create_map"), {{"path", path}}).structured.value("rootId").toString();
    const QString longTitle(McpMapTools::kMaxTextLength + 1, QLatin1Char('x'));
    QVERIFY(call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId}, {"title", longTitle}}).text.contains("longer"));
    QVERIFY(call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId}, {"title", 5}}).isError);
    QVERIFY(call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId}, {"title", "x"}, {"index", -1}}).isError);
    QVERIFY(call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId}, {"title", "x"}, {"labels", "x"}}).isError);
    QVERIFY(call(QStringLiteral("add_topic"), {{"path", path}, {"title", "x"}}).text.contains("parent_id"));
    QVERIFY(read(path).value("root").toObject().value("children").toArray().isEmpty());  // nothing was saved
}

void McpToolsTest::reportIssueSendsNothing() {
    const McpServer::ToolResult report = call(QStringLiteral("report_issue"),
        {{"title", "Errore & strano"}, {"description", "Succede questo"}, {"steps", "1. apri"}});
    QVERIFY2(!report.isError, qPrintable(report.text));
    const QString url = report.structured.value("url").toString();
    QVERIFY(url.startsWith(McpMapTools::kNewIssueUrl));
    QVERIFY(QUrl(url).isValid());
    QVERIFY(url.contains(QString::fromUtf8(QUrl::toPercentEncoding("Errore & strano"))));
    QVERIFY(report.structured.value("body").toString().contains("Steps to reproduce"));
    QVERIFY(report.structured.value("note").toString().contains("Nothing was sent"));
}

// Phase 3 stage A: style and contents of topics, saved in the file; wrong values change nothing.
void McpToolsTest::styleAndContentsOfTopics() {
    const QString path = file("stile.mentis");
    const QString rootId = call(QStringLiteral("create_map"), {{"path", path}, {"title", "Stile"}}).structured.value("rootId").toString();
    const auto add = [&](const QString &title) {
        return call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId}, {"title", title}}).structured.value("id").toString();
    };
    const QString a = add("A"), b = add("B");
    const QJsonObject options = call(QStringLiteral("list_options"), {}).structured;
    QVERIFY(options.value("shape").toArray().size() > 5);
    QVERIFY(!options.value("stickers").toArray().isEmpty());
    QVERIFY(!options.value("markers").toArray().isEmpty());
    const QString shape = options.value("shape").toArray()[1].toString();
    const QString preset = options.value("preset").toArray()[1].toString();
    // Style: values checked first, all or nothing.
    QVERIFY(call(QStringLiteral("set_style"), {{"path", path}, {"ids", QJsonArray{a}}, {"fill", "rosso"}}).text.contains("colour"));
    QVERIFY(call(QStringLiteral("set_style"), {{"path", path}, {"ids", QJsonArray{a}}, {"shape", "boh"}}).text.contains("unknown"));
    QVERIFY(call(QStringLiteral("set_style"), {{"path", path}, {"ids", QJsonArray{a}}, {"font_size", 500}}).isError);
    QVERIFY(call(QStringLiteral("set_style"), {{"path", path}, {"ids", QJsonArray{a}}}).text.contains("Nothing"));
    const McpServer::ToolResult styled = call(QStringLiteral("set_style"),
        {{"path", path}, {"ids", QJsonArray{a, b}}, {"fill", "#2563eb"}, {"shape", shape}, {"bold", true}, {"preset", preset}});
    QVERIFY2(!styled.isError, qPrintable(styled.text));
    QVERIFY(!call(QStringLiteral("set_style"), {{"path", path}, {"ids", QJsonArray{b}}, {"fill", QJsonValue::Null}}).isError);
    // Markers, sticker, formula, links, checkbox.
    const QJsonObject firstSeries = options.value("markers").toArray().first().toObject();
    const QString series = firstSeries.value("series").toString();
    const QString marker = firstSeries.value("markers").toArray().first().toString();
    QVERIFY(!call(QStringLiteral("set_marker"), {{"path", path}, {"ids", QJsonArray{a}}, {"series", series}, {"marker", marker}}).isError);
    QVERIFY(call(QStringLiteral("set_marker"), {{"path", path}, {"ids", QJsonArray{a}}, {"series", "nope"}, {"marker", ""}}).isError);
    const QString sticker = options.value("stickers").toArray().first().toString();
    QVERIFY(!call(QStringLiteral("set_sticker"), {{"path", path}, {"id", b}, {"sticker", sticker}}).isError);
    QVERIFY(call(QStringLiteral("set_sticker"), {{"path", path}, {"id", b}, {"sticker", "C:/x.svg"}}).isError);
    QVERIFY(!call(QStringLiteral("set_formula"), {{"path", path}, {"id", rootId}, {"latex", "E = mc^2"}}).isError);
    QVERIFY(!call(QStringLiteral("set_links"), {{"path", path}, {"id", a}, {"web", "example.com"}, {"topic", b}}).isError);
    QVERIFY(call(QStringLiteral("set_links"), {{"path", path}, {"id", a}, {"web", "javascript:alert(1)"}}).isError);
    QVERIFY(!call(QStringLiteral("set_task"), {{"path", path}, {"ids", QJsonArray{a, b}}, {"state", "done"}}).isError);
    // An image and an attached file from any folder; not a missing file.
    QImage picture(40, 30, QImage::Format_RGB32);
    picture.fill(Qt::red);
    QVERIFY(picture.save(file("foto.png")));
    QVERIFY(!call(QStringLiteral("set_image"), {{"path", path}, {"id", a}, {"file", file("foto.png")}}).isError);
    QVERIFY(call(QStringLiteral("set_image"), {{"path", path}, {"id", a}, {"file", QDir(m_outside.path()).filePath("manca.png")}}).text.contains("No file"));
    QVERIFY(!call(QStringLiteral("attach_files"), {{"path", path}, {"id", b}, {"files", QJsonArray{file("foto.png")}}}).isError);

    Document document;
    QVERIFY(document.open(path, false).ok);
    MindMapModel *model = document.model();
    const MindNode *na = model->nodeById(a);
    const MindNode *nb = model->nodeById(b);
    QVERIFY(na && nb);
    QCOMPARE(na->style.fill, std::optional<QColor>(QColor("#2563eb")));
    QVERIFY(!nb->style.fill);  // back to automatic
    QCOMPARE(NodeStyle::shapeKey(*na->style.shape), shape);
    QCOMPARE(na->style.fontWeight, std::optional<int>(700));
    QCOMPARE(na->markers, QStringList{marker});
    QVERIFY(nb->imageSticker);
    QVERIFY(!nb->imageAssetId.isEmpty());  // embedded in the file (reopened from the cache)
    QCOMPARE(model->root()->formula, QStringLiteral("E = mc^2"));
    QCOMPARE(na->webLink, QStringLiteral("https://example.com"));
    QCOMPARE(na->topicLink, b);
    QCOMPARE(na->task, MindNode::Task::Done);
    QVERIFY(!na->imageAssetId.isEmpty() || !na->imagePath.isEmpty());
    QCOMPARE(int(nb->files.size()), 1);
    document.close();
    // Reset, remove the picture and the file.
    QVERIFY(!call(QStringLiteral("reset_style"), {{"path", path}, {"ids", QJsonArray{a}}}).isError);
    QVERIFY(!call(QStringLiteral("remove_visual"), {{"path", path}, {"id", a}}).isError);
    QVERIFY(call(QStringLiteral("remove_visual"), {{"path", path}, {"id", a}}).isError);  // nothing left
    QVERIFY(!call(QStringLiteral("remove_file"), {{"path", path}, {"id", b}, {"index", 0}}).isError);
    QVERIFY(call(QStringLiteral("remove_file"), {{"path", path}, {"id", b}, {"index", 0}}).isError);
    Document reopened;
    QVERIFY(reopened.open(path, false).ok);
    QVERIFY(!reopened.model()->nodeById(a)->style.fill);
    QVERIFY(reopened.model()->nodeById(a)->imagePath.isEmpty());
    QVERIFY(reopened.model()->nodeById(b)->files.isEmpty());
    reopened.close();
}

// Phase 3 stage B: relationships, summaries, boundaries and floating topics, listed by read_map.
void McpToolsTest::relationshipsSummariesBoundaries() {
    const QString path = file("elementi.mentis");
    const QString rootId = call(QStringLiteral("create_map"), {{"path", path}, {"title", "Elementi"}}).structured.value("rootId").toString();
    const auto add = [&](const QString &title) {
        return call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId}, {"title", title}}).structured.value("id").toString();
    };
    const QString a = add("A"), b = add("B"), c = add("C");
    const McpServer::ToolResult relation = call(QStringLiteral("add_relationship"),
        {{"path", path}, {"from", a}, {"to", c}, {"label", "causa"}, {"shape", "elbow"}});
    QVERIFY2(!relation.isError, qPrintable(relation.text));
    QVERIFY(call(QStringLiteral("add_relationship"), {{"path", path}, {"from", a}, {"to", a}}).isError);
    QVERIFY(call(QStringLiteral("add_relationship"), {{"path", path}, {"from", a}, {"to", c}, {"shape", "zigzag"}}).isError);
    const McpServer::ToolResult summary = call(QStringLiteral("add_summary"), {{"path", path}, {"ids", QJsonArray{a, b}}, {"title", "Insieme"}});
    QVERIFY2(!summary.isError, qPrintable(summary.text));
    const McpServer::ToolResult boundary = call(QStringLiteral("add_boundary"), {{"path", path}, {"ids", QJsonArray{b, c}}, {"title", "Gruppo"}});
    QVERIFY2(!boundary.isError, qPrintable(boundary.text));
    QVERIFY(call(QStringLiteral("add_boundary"), {{"path", path}, {"ids", QJsonArray{rootId}}}).isError);
    const McpServer::ToolResult floating = call(QStringLiteral("add_floating_topic"), {{"path", path}, {"title", "Isola"}});
    QVERIFY2(!floating.isError, qPrintable(floating.text));

    QJsonObject map = read(path);
    QCOMPARE(map.value("relationships").toArray().size(), 1);
    const QJsonObject line = map.value("relationships").toArray().first().toObject();
    QCOMPARE(line.value("label").toString(), QStringLiteral("causa"));
    QCOMPARE(line.value("shape").toString(), QStringLiteral("elbow"));
    QCOMPARE(map.value("summaries").toArray().first().toObject().value("title").toString(), QStringLiteral("Insieme"));
    QCOMPARE(map.value("boundaries").toArray().first().toObject().value("title").toString(), QStringLiteral("Gruppo"));
    bool hasFloating = false;
    for (const QJsonValue &child : map.value("root").toObject().value("children").toArray())
        hasFloating |= child.toObject().value("floating").toBool() && child.toObject().value("title").toString() == QLatin1String("Isola");
    QVERIFY(hasFloating);

    const QString relationId = relation.structured.value("id").toString();
    const QString summaryId = summary.structured.value("ids").toArray().first().toString();
    const QString boundaryId = boundary.structured.value("ids").toArray().first().toString();
    QVERIFY(!call(QStringLiteral("update_element"), {{"path", path}, {"id", relationId}, {"title", "effetto"}, {"shape", "straight"}}).isError);
    QVERIFY(!call(QStringLiteral("update_element"), {{"path", path}, {"id", summaryId}, {"title", "Tutto"}}).isError);
    QVERIFY(call(QStringLiteral("update_element"), {{"path", path}, {"id", boundaryId}, {"shape", "elbow"}}).isError);
    QVERIFY(call(QStringLiteral("update_element"), {{"path", path}, {"id", "nessuno"}, {"title", "x"}}).isError);
    map = read(path);
    QCOMPARE(map.value("relationships").toArray().first().toObject().value("label").toString(), QStringLiteral("effetto"));
    QCOMPARE(map.value("summaries").toArray().first().toObject().value("title").toString(), QStringLiteral("Tutto"));
    // One element per side of a balanced map, as in the app: delete them all.
    QJsonArray everything{relationId};
    for (const QJsonValue &id : summary.structured.value("ids").toArray()) everything.append(id);
    for (const QJsonValue &id : boundary.structured.value("ids").toArray()) everything.append(id);
    QVERIFY(!call(QStringLiteral("delete_elements"), {{"path", path}, {"ids", everything}}).isError);
    map = read(path);
    QVERIFY(map.value("relationships").toArray().isEmpty());
    QVERIFY(map.value("summaries").toArray().isEmpty());
    QVERIFY(map.value("boundaries").toArray().isEmpty());
    QCOMPARE(childTitles(map.value("root").toObject()).mid(0, 3), (QStringList{"A", "B", "C"}));  // the topics stay
}

// Phase 3 stage C: sheets (deleting one needs confirm: true without a Mentis window) and structure.
void McpToolsTest::sheetsAndStructure() {
    const QString path = file("struttura.mentis");
    const QString rootId = call(QStringLiteral("create_map"), {{"path", path}, {"title", "Uno"}}).structured.value("rootId").toString();
    const QString child = call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId}, {"title", "Ramo"}}).structured.value("id").toString();
    call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", child}, {"title", "Foglia"}});
    const QString second = call(QStringLiteral("add_sheet"), {{"path", path}, {"title", "Due"}}).structured.value("sheet").toString();
    const QString first = read(path).value("sheets").toArray()[0].toObject().value("id").toString();
    // Duplicate and move.
    const McpServer::ToolResult copy = call(QStringLiteral("duplicate_sheet"), {{"path", path}, {"sheet", first}});
    QVERIFY2(!copy.isError, qPrintable(copy.text));
    QJsonArray sheets = read(path).value("sheets").toArray();
    QCOMPARE(sheets.size(), 3);
    QCOMPARE(sheets[1].toObject().value("title").toString(), sheets[0].toObject().value("title").toString() + QStringLiteral(" (copy)"));
    QVERIFY(!call(QStringLiteral("move_sheet"), {{"path", path}, {"sheet", second}, {"index", 0}}).isError);
    QCOMPARE(read(path).value("sheets").toArray()[0].toObject().value("id").toString(), second);
    // Delete: refused without the user's yes, done with confirm: true; the last sheet stays.
    const McpServer::ToolResult refused = call(QStringLiteral("delete_sheet"), {{"path", path}, {"sheet", second}});
    QVERIFY(refused.isError);
    QVERIFY(refused.text.contains("confirm"));
    QCOMPARE(read(path).value("sheets").toArray().size(), 3);
    QVERIFY(!call(QStringLiteral("delete_sheet"), {{"path", path}, {"sheet", second}, {"confirm", true}}).isError);
    QCOMPARE(read(path).value("sheets").toArray().size(), 2);
    // Structure, balance, layout, folding.
    const QJsonObject options = call(QStringLiteral("list_options"), {}).structured;
    const QString orgChart = options.value("structure").toArray()[2].toString();
    QVERIFY(call(QStringLiteral("set_structure"), {{"path", path}, {"sheet", first}, {"id", rootId}, {"structure", "spirale"}}).isError);
    QVERIFY(!call(QStringLiteral("set_structure"), {{"path", path}, {"sheet", first}, {"id", rootId}, {"structure", orgChart}}).isError);
    QVERIFY(!call(QStringLiteral("set_balanced"), {{"path", path}, {"sheet", first}, {"balanced", false}}).isError);
    QVERIFY(!call(QStringLiteral("auto_layout"), {{"path", path}, {"sheet", first}}).isError);
    QVERIFY(!call(QStringLiteral("set_collapsed"), {{"path", path}, {"sheet", first}, {"ids", QJsonArray{child}}, {"collapsed", true}}).isError);
    Document document;
    QVERIFY(document.open(path, false).ok);
    QVERIFY(document.switchSheet(document.sheetIndex(first)).ok);
    QCOMPARE(MapStructure::typeKey(MapStructure::typeOf(document.model()->root())), orgChart);
    QVERIFY(!document.model()->balanced);
    QVERIFY(document.model()->nodeById(child)->collapsed);
    document.close();
    QVERIFY(call(QStringLiteral("delete_sheet"), {{"path", path}, {"sheet", first}, {"confirm", true}}).isError == false);
    QVERIFY(call(QStringLiteral("delete_sheet"), {{"path", path}, {"confirm", true}}).text.contains("last"));
}

// Phase 3 stage D: dates, durations, progress, dependencies and people of the sheet's Gantt.
void McpToolsTest::ganttPlan() {
    const QString path = file("gantt.mentis");
    const QString rootId = call(QStringLiteral("create_map"), {{"path", path}, {"title", "Esame"}}).structured.value("rootId").toString();
    const auto add = [&](const QString &parent, const QString &title) {
        return call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", parent}, {"title", title}}).structured.value("id").toString();
    };
    const QString study = add(rootId, "Studio");
    const QString read1 = add(study, "Capitolo 1"), read2 = add(study, "Capitolo 2");
    const McpServer::ToolResult planned = call(QStringLiteral("set_gantt_task"),
        {{"path", path}, {"id", read1}, {"start", "2026-11-09"}, {"duration", 3}, {"progress", 50}});
    QVERIFY2(!planned.isError, qPrintable(planned.text));
    QCOMPARE(planned.structured.value("plannedStart").toString(), QStringLiteral("2026-11-09"));
    QVERIFY(call(QStringLiteral("set_gantt_task"), {{"path", path}, {"id", read1}, {"start", "9 novembre"}}).isError);
    QVERIFY(call(QStringLiteral("set_gantt_task"), {{"path", path}, {"id", read1}, {"duration", 0}}).isError);
    QVERIFY(call(QStringLiteral("set_gantt_task"), {{"path", path}, {"id", study}, {"duration", 5}}).text.contains("summary"));
    QVERIFY(call(QStringLiteral("set_gantt_task"), {{"path", path}, {"id", rootId}, {"duration", 5}}).isError);
    // Chapter 2 after chapter 1.
    QVERIFY(!call(QStringLiteral("set_gantt_task"), {{"path", path}, {"id", read2}, {"duration", 2}}).isError);
    const McpServer::ToolResult linked = call(QStringLiteral("add_dependency"), {{"path", path}, {"from", read1}, {"to", read2}});
    QVERIFY2(!linked.isError, qPrintable(linked.text));
    QVERIFY(call(QStringLiteral("add_dependency"), {{"path", path}, {"from", read2}, {"to", read1}}).isError);  // a cycle
    QVERIFY(call(QStringLiteral("add_dependency"), {{"path", path}, {"from", read1}, {"to", read2}, {"type", "dopo"}}).isError);
    // People.
    const McpServer::ToolResult person = call(QStringLiteral("add_person"), {{"path", path}, {"first_name", "Ada"}, {"role", "Tutor"}});
    QVERIFY2(!person.isError, qPrintable(person.text));
    const QString ada = person.structured.value("id").toString();
    QVERIFY(!call(QStringLiteral("assign_people"), {{"path", path}, {"ids", QJsonArray{read1}}, {"assign", QJsonArray{ada}}}).isError);
    QVERIFY(call(QStringLiteral("assign_people"), {{"path", path}, {"ids", QJsonArray{read1}}, {"assign", QJsonArray{"chi"}}}).isError);

    const QJsonObject gantt = call(QStringLiteral("read_gantt"), {{"path", path}}).structured;
    QCOMPARE(gantt.value("dependencies").toArray().size(), 1);
    QCOMPARE(gantt.value("people").toArray().first().toObject().value("name").toString(), QStringLiteral("Ada"));
    QJsonObject second, first;
    for (const QJsonValue &task : gantt.value("tasks").toArray()) {
        if (task.toObject().value("id").toString() == read2) second = task.toObject();
        if (task.toObject().value("id").toString() == read1) first = task.toObject();
    }
    QCOMPARE(first.value("progress").toInt(), 50);
    QCOMPARE(first.value("people").toArray().first().toString(), ada);
    QCOMPARE(first.value("plannedEnd").toString(), QStringLiteral("2026-11-11"));
    QCOMPARE(second.value("plannedStart").toString(), QStringLiteral("2026-11-12"));  // after chapter 1
    QVERIFY(!call(QStringLiteral("remove_dependency"), {{"path", path}, {"from", read1}, {"to", read2}}).isError);
    QVERIFY(call(QStringLiteral("read_gantt"), {{"path", path}}).structured.value("dependencies").toArray().isEmpty());
}

// Phase 3 stage E: exports in every format, replacing only with the user's yes, the Pitch settings,
// and a replaced map written before the old one goes.
void McpToolsTest::exportsPitchAndReplacing() {
    const QString path = file("esporta.mentis");
    const QString rootId = call(QStringLiteral("create_map"), {{"path", path}, {"title", "Esporta"}}).structured.value("rootId").toString();
    const QString child = call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId}, {"title", "Ramo"}}).structured.value("id").toString();
    for (const QString name : {QStringLiteral("mappa.png"), QStringLiteral("mappa.svg"), QStringLiteral("mappa.pdf"),
                               QStringLiteral("mappa.docx"), QStringLiteral("mappa.xlsx")}) {
        const McpServer::ToolResult exported = call(QStringLiteral("export_map"), {{"path", path}, {"file", file(name)}});
        QVERIFY2(!exported.isError, qPrintable(name + ": " + exported.text));
        QVERIFY2(QFileInfo(file(name)).size() > 0, qPrintable(name));
    }
    const McpServer::ToolResult markdown = call(QStringLiteral("export_map"), {{"path", path}, {"file", file("appunti.md")}});
    QVERIFY2(!markdown.isError, qPrintable(markdown.text));
    QVERIFY(QFileInfo::exists(markdown.structured.value("file").toString()));
    QVERIFY(call(QStringLiteral("export_map"), {{"path", path}, {"file", file("mappa.gif")}}).isError);
    QVERIFY(call(QStringLiteral("export_map"), {{"path", path}, {"file", QDir(m_outside.path()).filePath("manca/fuori.png")}}).text.contains("does not exist"));
    // Replacing an exported file needs the yes.
    const qint64 before = QFileInfo(file("mappa.png")).lastModified().toMSecsSinceEpoch();
    QVERIFY(call(QStringLiteral("export_map"), {{"path", path}, {"file", file("mappa.png")}}).text.contains("confirmation"));
    QCOMPARE(QFileInfo(file("mappa.png")).lastModified().toMSecsSinceEpoch(), before);
    QVERIFY(!call(QStringLiteral("export_map"), {{"path", path}, {"file", file("mappa.png")}, {"confirm", true}}).isError);
    // Pitch.
    const QJsonObject options = call(QStringLiteral("list_options"), {}).structured;
    const QString theme = options.value("pitch_theme").toArray()[1].toString();
    const QString layout = options.value("pitch_layout").toArray()[1].toString();
    QVERIFY(call(QStringLiteral("set_pitch"), {{"path", path}, {"theme", "arcobaleno"}}).isError);
    QVERIFY(!call(QStringLiteral("set_pitch"), {{"path", path}, {"theme", theme}, {"id", child}, {"layout", layout}}).isError);
    {
        Document document;
        QVERIFY(document.open(path, false).ok);
        QCOMPARE(document.model()->pitchTheme, theme);
        QCOMPARE(document.model()->nodeById(child)->pitchLayout, layout);
        document.close();
    }
    // Replace the map itself: with confirm the new one is there, and no copy of the old one is left.
    const McpServer::ToolResult replaced = call(QStringLiteral("create_map"), {{"path", path}, {"title", "Nuova"}, {"confirm", true}});
    QVERIFY2(!replaced.isError, qPrintable(replaced.text));
    QCOMPARE(read(path).value("root").toObject().value("title").toString(), QStringLiteral("Nuova"));
    const QStringList leftovers = QDir(m_allowed.path()).entryList({QStringLiteral("esporta.mentis.replaced-*")});
    QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(", ")));
    // Never while it is open in Mentis.
    Document app;
    QVERIFY(app.open(path, false).ok);
    QVERIFY(call(QStringLiteral("create_map"), {{"path", path}, {"confirm", true}}).text.contains("open in Mentis"));
    app.close();
}

// The agent's own SVG becomes a sticker (and a marker) of the library, used on a topic; collections are
// created, renamed and ordered; deleting files needs the user's yes, hiding a built-in one does not.
void McpToolsTest::stickerAndMarkerLibrary() {
    for (auto kind : {IconLibrary::Kind::Sticker, IconLibrary::Kind::Marker}) QDir(IconLibrary::folder(kind)).removeRecursively();
    const QString svg = QStringLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><circle cx=\"12\" cy=\"12\" r=\"9\" fill=\"#16a34a\"/></svg>");
    const QJsonArray builtIns = call(QStringLiteral("list_library"), {}).structured.value("collections").toArray();
    QVERIFY(!builtIns.isEmpty());
    QVERIFY(builtIns.first().toObject().value("builtIn").toBool());
    QVERIFY(call(QStringLiteral("add_to_library"), {{"name", "rotto"}, {"svg", "<svg><nope"}}).isError);
    QVERIFY(call(QStringLiteral("add_to_library"), {{"name", "x"}}).text.contains("either"));
    const McpServer::ToolResult added = call(QStringLiteral("add_to_library"), {{"name", "Cerchio verde"}, {"svg", svg}, {"collection_name", "Mie"}});
    QVERIFY2(!added.isError, qPrintable(added.text));
    const QString sticker = added.structured.value("path").toString();
    const QString mine = added.structured.value("collection").toString();
    QVERIFY(QFileInfo::exists(sticker));
    // Used on a topic of a map.
    const QString path = file("adesivi.mentis");
    const QString rootId = call(QStringLiteral("create_map"), {{"path", path}}).structured.value("rootId").toString();
    QVERIFY(!call(QStringLiteral("set_sticker"), {{"path", path}, {"id", rootId}, {"sticker", sticker}}).isError);
    // Order and regroup.
    const QString other = call(QStringLiteral("create_collection"), {{"name", "Altre"}}).structured.value("id").toString();
    QVERIFY(!other.isEmpty());
    QVERIFY(!call(QStringLiteral("move_library_item"), {{"path", sticker}, {"collection", other}}).isError);
    QVERIFY(!call(QStringLiteral("move_collection"), {{"collection", other}, {"index", 0}}).isError);
    QVERIFY(!call(QStringLiteral("rename_collection"), {{"collection", other}, {"name", "Preferite"}}).isError);
    QJsonArray collections = call(QStringLiteral("list_library"), {}).structured.value("collections").toArray();
    QCOMPARE(collections.first().toObject().value("name").toString(), QStringLiteral("Preferite"));
    QCOMPARE(collections.first().toObject().value("items").toArray().first().toObject().value("path").toString(), sticker);
    // Deleting needs the yes.
    QVERIFY(call(QStringLiteral("remove_library_item"), {{"path", sticker}}).text.contains("confirmation"));
    QVERIFY(QFileInfo::exists(sticker));
    QVERIFY(!call(QStringLiteral("remove_library_item"), {{"path", sticker}, {"confirm", true}}).isError);
    QVERIFY(call(QStringLiteral("remove_collection"), {{"collection", mine}}).isError);
    QVERIFY(!call(QStringLiteral("remove_collection"), {{"collection", mine}, {"confirm", true}}).isError);
    const QString builtIn = builtIns.first().toObject().value("id").toString();
    QVERIFY(!call(QStringLiteral("remove_collection"), {{"collection", builtIn}}).isError);  // hidden, no question
    collections = call(QStringLiteral("list_library"), {}).structured.value("collections").toArray();
    bool hidden = false;
    for (const QJsonValue &c : collections) if (c.toObject().value("id").toString() == builtIn) hidden = c.toObject().value("hidden").toBool();
    QVERIFY(hidden);
    QVERIFY(!call(QStringLiteral("restore_builtins"), {}).isError);
    // A marker of the agent's own, on a topic.
    const McpServer::ToolResult marker = call(QStringLiteral("add_to_library"), {{"kind", "marker"}, {"name", "Fatto"}, {"svg", svg}});
    QVERIFY2(!marker.isError, qPrintable(marker.text));
    QVERIFY(marker.structured.value("marker").toString().startsWith(QStringLiteral("custom:")));
    QVERIFY(!call(QStringLiteral("set_marker"), {{"path", path}, {"ids", QJsonArray{rootId}}, {"marker", marker.structured.value("path").toString()}}).isError);
    Document document;
    QVERIFY(document.open(path, false).ok);
    QCOMPARE(document.model()->root()->markers, QStringList{marker.structured.value("marker").toString()});
    QVERIFY(document.model()->root()->imageSticker);
    document.close();
}

// Own palettes, the sheet's theme and style, formatted titles and notes, labels renamed everywhere,
// find and replace in every sheet.
void McpToolsTest::coloursAndText() {
    for (const NodeStyle::Palette &own : CustomPalettes::all()) CustomPalettes::remove(own.id);  // a run before may have left one
    const QString path = file("colori.mentis");
    const QString rootId = call(QStringLiteral("create_map"), {{"path", path}, {"title", "Colori"}}).structured.value("rootId").toString();
    QVERIFY(!call(QStringLiteral("list_palettes"), {}).structured.value("palettes").toArray().isEmpty());
    const QJsonArray colors{"#0f766e", "#b45309", "#7c3aed", "#be123c", "#1d4ed8", "#4d7c0f"};
    QVERIFY(call(QStringLiteral("create_palette"), {{"name", "Scuola"}, {"colors", QJsonArray{"#000000"}}}).isError);
    const McpServer::ToolResult palette = call(QStringLiteral("create_palette"), {{"name", "Scuola"}, {"colors", colors}});
    QVERIFY2(!palette.isError, qPrintable(palette.text));
    QVERIFY(call(QStringLiteral("create_palette"), {{"name", "Scuola"}, {"colors", colors}}).text.contains("already"));
    const QString paletteId = palette.structured.value("id").toString();
    QVERIFY(!call(QStringLiteral("set_theme"), {{"path", path}, {"palette", paletteId}}).isError);
    QVERIFY(!call(QStringLiteral("set_theme"), {{"path", path}, {"variant", 2}}).isError);
    QVERIFY(!call(QStringLiteral("set_theme"), {{"path", path}, {"background", "#1e3a2a"}}).isError);
    QVERIFY(call(QStringLiteral("set_theme"), {{"path", path}, {"variant", 9}}).isError);
    QVERIFY(!call(QStringLiteral("set_sheet_style"), {{"path", path}, {"compact", true}, {"branch_width", 4}}).isError);
    // Formatted text.
    const QString chapter = call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId},
        {"title_markdown", "**Capitolo** *primo*"}, {"notes_markdown", "- uno\n- **due**"}, {"labels", QJsonArray{"ripasso"}}})
        .structured.value("id").toString();
    QVERIFY(!chapter.isEmpty());
    const QString second = call(QStringLiteral("add_sheet"), {{"path", path}, {"title", "Due"}}).structured.value("sheet").toString();
    const QString secondRoot = read(path, second).value("root").toObject().value("id").toString();
    call(QStringLiteral("add_topic"), {{"path", path}, {"sheet", second}, {"parent_id", secondRoot}, {"title", "Capitolo secondo"},
                                       {"labels", QJsonArray{"ripasso"}}});
    // Labels and titles in both sheets.
    const McpServer::ToolResult renamed = call(QStringLiteral("rename_label"), {{"path", path}, {"from", "ripasso"}, {"to", "da ripassare"}});
    QVERIFY2(!renamed.isError, qPrintable(renamed.text));
    QCOMPARE(renamed.structured.value("topics").toInt(), 2);
    QVERIFY(call(QStringLiteral("rename_label"), {{"path", path}, {"from", "nessuna"}, {"to", "x"}}).isError);
    const McpServer::ToolResult replaced = call(QStringLiteral("find_replace"), {{"path", path}, {"find", "capitolo"}, {"replace", "Cap."}});
    QVERIFY2(!replaced.isError, qPrintable(replaced.text));
    QCOMPARE(replaced.structured.value("topics").toInt(), 2);

    Document document;
    QVERIFY(document.open(path, false).ok);
    MindMapModel *model = document.model();
    QCOMPARE(model->paletteId, paletteId);
    QCOMPARE(model->paletteVariant, 2);
    QCOMPARE(model->background, std::optional<QColor>(QColor("#1e3a2a")));
    QVERIFY(model->sheetStyle.compact);
    QCOMPARE(model->sheetStyle.branchWidth, 4.0);
    const MindNode *node = model->nodeById(chapter);
    QCOMPARE(node->title, QStringLiteral("Cap. primo"));
    QVERIFY(!node->notesHtml.isEmpty());
    QVERIFY(node->notes.contains(QStringLiteral("due")));
    QCOMPARE(node->labels, QStringList{"da ripassare"});
    QVERIFY(document.switchSheet(document.sheetIndex(second)).ok);
    QCOMPARE(document.model()->root()->children.back()->title, QStringLiteral("Cap. secondo"));  // after the new sheet's own topics
    document.close();
    // A formatted title is kept (not only plain text).
    const QString bold = call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId}, {"title_markdown", "**Forte** debole"}})
                             .structured.value("id").toString();
    Document again;
    QVERIFY(again.open(path, false).ok);
    QCOMPARE(again.model()->nodeById(bold)->title, QStringLiteral("Forte debole"));
    QVERIFY(!again.model()->nodeById(bold)->titleHtml.isEmpty());
    again.close();
    // Deleting an own palette needs the yes.
    QVERIFY(call(QStringLiteral("delete_palette"), {{"id", paletteId}}).text.contains("confirmation"));
    QVERIFY(!call(QStringLiteral("delete_palette"), {{"id", paletteId}, {"confirm", true}}).isError);
}

// People changed and removed, currency, calendar and baselines; a library item, an own palette and
// an attached file renamed; find and replace in notes and labels, formatting kept.
void McpToolsTest::namesCalendarAndReplacing() {
    const QString path = file("nomi.mentis");
    const QString rootId = call(QStringLiteral("create_map"), {{"path", path}, {"title", "Nomi"}}).structured.value("rootId").toString();
    const QString task = call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId}, {"title", "Lettura"},
        {"notes_markdown", "Leggere **tutto** il libro"}, {"labels", QJsonArray{"libro"}}}).structured.value("id").toString();
    QVERIFY(!call(QStringLiteral("set_gantt_task"), {{"path", path}, {"id", task}, {"start", "2026-11-09"}, {"duration", 2}}).isError);
    // People.
    const QString ada = call(QStringLiteral("add_person"), {{"path", path}, {"first_name", "Ada"}, {"hourly_cost", 30}}).structured.value("id").toString();
    QVERIFY(call(QStringLiteral("update_person"), {{"path", path}, {"id", ada}, {"email", "non un indirizzo"}}).isError);
    QVERIFY(call(QStringLiteral("update_person"), {{"path", path}, {"id", ada}, {"hourly_cost", -1}}).isError);
    QVERIFY(!call(QStringLiteral("update_person"), {{"path", path}, {"id", ada}, {"last_name", "Lovelace"}, {"role", "Autrice"},
                                                     {"email", "ada@example.org"}, {"hourly_cost", 42.5}}).isError);
    const QString bob = call(QStringLiteral("add_person"), {{"path", path}, {"first_name", "Bob"}}).structured.value("id").toString();
    QVERIFY(!call(QStringLiteral("assign_people"), {{"path", path}, {"ids", QJsonArray{task}}, {"assign", QJsonArray{ada}}}).isError);
    QVERIFY(call(QStringLiteral("remove_person"), {{"path", path}, {"id", ada}}).text.contains("confirmation"));
    QVERIFY(!call(QStringLiteral("remove_person"), {{"path", path}, {"id", bob}}).isError);  // not assigned: no question
    QVERIFY(!call(QStringLiteral("set_currency"), {{"path", path}, {"currency", "CHF Fr."}}).isError);
    // Calendar and baselines.
    QVERIFY(call(QStringLiteral("set_calendar"), {{"path", path}, {"working_days", QJsonArray{8}}}).isError);
    QVERIFY(call(QStringLiteral("set_calendar"), {{"path", path}, {"holidays", QJsonArray{"domani"}}}).isError);
    QVERIFY(!call(QStringLiteral("set_calendar"), {{"path", path}, {"working_days", QJsonArray{1, 2, 3, 4, 5, 6}},
                                                    {"holidays", QJsonArray{"2026-12-25"}}, {"hours_per_day", 7.5}}).isError);
    const McpServer::ToolResult baseline = call(QStringLiteral("add_baseline"), {{"path", path}, {"name", "Inizio"}});
    QVERIFY2(!baseline.isError, qPrintable(baseline.text));
    const QString baselineId = baseline.structured.value("id").toString();
    QVERIFY(!call(QStringLiteral("rename_baseline"), {{"path", path}, {"id", baselineId}, {"name", "Piano iniziale"}}).isError);
    QVERIFY(call(QStringLiteral("rename_baseline"), {{"path", path}, {"id", "nessuna"}, {"name", "x"}}).isError);
    const QJsonObject gantt = call(QStringLiteral("read_gantt"), {{"path", path}}).structured;
    QCOMPARE(gantt.value("people").toArray().size(), 1);
    const QJsonObject person = gantt.value("people").toArray().first().toObject();
    QCOMPARE(person.value("name").toString(), QStringLiteral("Ada Lovelace"));
    QCOMPARE(person.value("email").toString(), QStringLiteral("ada@example.org"));
    QCOMPARE(person.value("hourly_cost").toDouble(), 42.5);
    QVERIFY(gantt.value("currency").toString().startsWith(QStringLiteral("CHF")));
    QCOMPARE(gantt.value("baselines").toArray().first().toObject().value("name").toString(), QStringLiteral("Piano iniziale"));
    const QJsonObject calendar = gantt.value("calendar").toObject();
    QCOMPARE(calendar.value("working_days").toArray().size(), 6);
    QCOMPARE(calendar.value("holidays").toArray(), QJsonArray{"2026-12-25"});
    QCOMPARE(calendar.value("hours_per_day").toDouble(), 7.5);
    QVERIFY(!call(QStringLiteral("remove_person"), {{"path", path}, {"id", ada}, {"confirm", true}}).isError);
    // An attached file renamed; the extension stays.
    QImage picture(8, 8, QImage::Format_RGB32);
    picture.fill(Qt::blue);
    QVERIFY(picture.save(file("allegato.png")));
    QVERIFY(!call(QStringLiteral("attach_files"), {{"path", path}, {"id", task}, {"files", QJsonArray{file("allegato.png")}}}).isError);
    QVERIFY(call(QStringLiteral("rename_file"), {{"path", path}, {"id", task}, {"index", 3}, {"name", "x"}}).isError);
    QVERIFY(call(QStringLiteral("rename_file"), {{"path", path}, {"id", task}, {"index", 0}, {"name", "a/b"}}).isError);
    const McpServer::ToolResult named = call(QStringLiteral("rename_file"), {{"path", path}, {"id", task}, {"index", 0}, {"name", "Copertina"}});
    QVERIFY2(!named.isError, qPrintable(named.text));
    QCOMPARE(named.structured.value("name").toString(), QStringLiteral("Copertina.png"));
    // Notes and labels; the bold in the notes stays.
    const McpServer::ToolResult notes = call(QStringLiteral("find_replace"), {{"path", path}, {"find", "tutto"}, {"replace", "meta"}, {"in", QJsonArray{"notes"}}});
    QVERIFY2(!notes.isError, qPrintable(notes.text));
    QVERIFY(call(QStringLiteral("find_replace"), {{"path", path}, {"find", "libro"}, {"replace", "x"}, {"in", QJsonArray{"tutto"}}}).isError);
    QVERIFY(!call(QStringLiteral("find_replace"), {{"path", path}, {"find", "libro"}, {"replace", "manuale"}, {"in", QJsonArray{"labels"}}}).isError);
    Document document;
    QVERIFY(document.open(path, false).ok);
    const MindNode *node = document.model()->nodeById(task);
    QCOMPARE(node->notes.trimmed(), QStringLiteral("Leggere meta il libro"));
    QVERIFY(node->notesHtml.contains(QStringLiteral("meta")));
    QVERIFY(node->notesHtml.contains(QStringLiteral("font-weight")));
    QCOMPARE(node->labels, QStringList{"manuale"});
    QCOMPARE(node->files.first().name, QStringLiteral("Copertina.png"));
    document.close();
    // Library item and palette names.
    const QString svg = QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><rect width=\"20\" height=\"20\"/></svg>");
    const QString item = call(QStringLiteral("add_to_library"), {{"name", "Quadrato"}, {"svg", svg}}).structured.value("path").toString();
    const McpServer::ToolResult renamedItem = call(QStringLiteral("rename_library_item"), {{"path", item}, {"name", "Riquadro"}});
    QVERIFY2(!renamedItem.isError, qPrintable(renamedItem.text));
    QCOMPARE(renamedItem.structured.value("name").toString(), QStringLiteral("Riquadro"));
    QVERIFY(QFileInfo::exists(renamedItem.structured.value("path").toString()));
    IconLibrary::removeDeletedFiles(IconLibrary::Kind::Sticker);  // BUG-037: the old file goes at the next start
    QVERIFY(!QFileInfo::exists(item));
    QVERIFY(!call(QStringLiteral("remove_library_item"), {{"path", renamedItem.structured.value("path").toString()}, {"confirm", true}}).isError);
    const QJsonArray colors{"#0f766e", "#b45309", "#7c3aed", "#be123c", "#1d4ed8", "#4d7c0f"};
    const QString palette = call(QStringLiteral("create_palette"), {{"name", "Prova"}, {"colors", colors}}).structured.value("id").toString();
    QVERIFY(call(QStringLiteral("update_palette"), {{"id", palette}}).isError);
    QVERIFY(call(QStringLiteral("update_palette"), {{"id", palette}, {"colors", QJsonArray{"#000000"}}}).isError);
    QJsonArray changed = colors;
    changed[0] = "#111111";
    QVERIFY(!call(QStringLiteral("update_palette"), {{"id", palette}, {"name", "Prova due"}, {"colors", changed}}).isError);
    QCOMPARE(CustomPalettes::find(palette)->name, QStringLiteral("Prova due"));
    QVERIFY(!call(QStringLiteral("delete_palette"), {{"id", palette}, {"confirm", true}}).isError);
}

// A relationship moved to other ends, its curve's handles and its style changed, then put back.
void McpToolsTest::relationshipEndsCurveAndStyle() {
    const QString path = file("relazioni.mentis");
    const QString rootId = call(QStringLiteral("create_map"), {{"path", path}}).structured.value("rootId").toString();
    const auto add = [&](const QString &title) {
        return call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId}, {"title", title}}).structured.value("id").toString();
    };
    const QString a = add("A"), b = add("B"), c = add("C");
    const QString id = call(QStringLiteral("add_relationship"), {{"path", path}, {"from", a}, {"to", b}}).structured.value("id").toString();
    QVERIFY(!id.isEmpty());
    QVERIFY(call(QStringLiteral("update_relationship"), {{"path", path}, {"id", "nessuna"}, {"label", "x"}}).isError);
    QVERIFY(call(QStringLiteral("update_relationship"), {{"path", path}, {"id", id}, {"to", a}}).isError);  // the same end twice
    QVERIFY(call(QStringLiteral("update_relationship"), {{"path", path}, {"id", id}, {"to", "chi"}}).isError);
    QVERIFY(call(QStringLiteral("update_relationship"), {{"path", path}, {"id", id}, {"line_width", 50}}).isError);
    QVERIFY(call(QStringLiteral("update_relationship"), {{"path", path}, {"id", id}, {"end_mark", "freccia"}}).isError);
    QVERIFY(call(QStringLiteral("update_relationship"), {{"path", path}, {"id", id}, {"from_control", QJsonArray{1}}}).isError);
    QVERIFY(!call(QStringLiteral("update_relationship"), {{"path", path}, {"id", id}, {"from_control", QJsonArray{40, -60}},
                                                           {"to_control", QJsonArray{-30, 20}}}).isError);
    const McpServer::ToolResult moved = call(QStringLiteral("update_relationship"), {{"path", path}, {"id", id}, {"to", c},
        {"line_color", "#be123c"}, {"line_width", 3}, {"line_pattern", "solid"}, {"start_mark", "dot"}, {"end_mark", "openArrow"},
        {"font_size", 16}, {"text_color", "#1d4ed8"}, {"label", "porta a"}, {"shape", "elbow"}});
    QVERIFY2(!moved.isError, qPrintable(moved.text));
    QJsonObject relation = call(QStringLiteral("read_map"), {{"path", path}}).structured.value("relationships").toArray().first().toObject();
    QCOMPARE(relation.value("to").toString(), c);
    QCOMPARE(relation.value("fromControl").toArray(), (QJsonArray{40, -60}));
    QVERIFY(!relation.contains("toControl"));  // the moved end's handle is automatic again
    QCOMPARE(relation.value("shape").toString(), QStringLiteral("elbow"));
    QCOMPARE(relation.value("startMark").toString(), QStringLiteral("dot"));

    Document document;
    QVERIFY(document.open(path, false).ok);
    const Relationship *saved = document.relation(id);
    QVERIFY(saved);
    QCOMPARE(saved->lineColor, std::optional<QColor>(QColor("#be123c")));
    QCOMPARE(saved->lineWidth, std::optional<qreal>(3));
    QCOMPARE(saved->linePattern, std::optional<int>(int(Qt::SolidLine)));
    QCOMPARE(saved->endMark, std::optional<BranchEnd>(BranchEnd::OpenArrow));
    QCOMPARE(saved->fontSize, std::optional<qreal>(16));
    QCOMPARE(saved->label, QStringLiteral("porta a"));
    document.close();
    // null puts back the automatic curve and the default style.
    QVERIFY(!call(QStringLiteral("update_relationship"), {{"path", path}, {"id", id}, {"from_control", QJsonValue()},
        {"line_color", QJsonValue()}, {"line_width", QJsonValue()}, {"end_mark", QJsonValue()}}).isError);
    relation = call(QStringLiteral("read_map"), {{"path", path}}).structured.value("relationships").toArray().first().toObject();
    QVERIFY(!relation.contains("fromControl") && !relation.contains("lineColor") && !relation.contains("lineWidth") && !relation.contains("endMark"));
}

void McpToolsTest::sharesCurrenciesAndGanttStyle() {
    const QString path = file("quote.mentis");
    const QString rootId = call(QStringLiteral("create_map"), {{"path", path}, {"title", "Quote"}}).structured.value("rootId").toString();
    const QString task = call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId}, {"title", "Scavo"}}).structured.value("id").toString();
    const QString ada = call(QStringLiteral("add_person"), {{"path", path}, {"first_name", "Ada"}, {"email", "ada@example.org"}}).structured.value("id").toString();
    const QString bob = call(QStringLiteral("add_person"), {{"path", path}, {"first_name", "Bob"}}).structured.value("id").toString();
    // Shares: given with the assignment, changed later for one person only.
    QVERIFY(call(QStringLiteral("assign_people"), {{"path", path}, {"ids", QJsonArray{task}}, {"assign", QJsonArray{ada}}, {"percent", 0}}).isError);
    QVERIFY(call(QStringLiteral("assign_people"), {{"path", path}, {"ids", QJsonArray{task}}, {"unassign", QJsonArray{ada}}, {"percent", 50}}).isError);
    QVERIFY(!call(QStringLiteral("assign_people"), {{"path", path}, {"ids", QJsonArray{task}}, {"assign", QJsonArray{ada, bob}}, {"percent", 50}}).isError);
    QVERIFY(!call(QStringLiteral("assign_people"), {{"path", path}, {"ids", QJsonArray{task}}, {"assign", QJsonArray{bob}}, {"percent", 25}}).isError);
    // Progress and priority in one call.
    QVERIFY(call(QStringLiteral("set_gantt_task"), {{"path", path}, {"id", task}, {"priority", 6}}).isError);
    const McpServer::ToolResult planned = call(QStringLiteral("set_gantt_task"), {{"path", path}, {"id", task}, {"progress", 40}, {"priority", 4}});
    QVERIFY2(!planned.isError, qPrintable(planned.text));
    QCOMPARE(planned.structured.value("priority").toInt(), 4);
    // The sheet's chart style and a task's own.
    QVERIFY(call(QStringLiteral("set_gantt_style"), {{"path", path}, {"tasks", QJsonObject{{"shape", "bracket"}}}}).isError);
    QVERIFY(call(QStringLiteral("set_gantt_style"), {{"path", path}, {"tasks", QJsonObject{{"size", 3}}}}).isError);
    const McpServer::ToolResult styled = call(QStringLiteral("set_gantt_style"), {{"path", path},
        {"tasks", QJsonObject{{"shape", "rounded"}, {"fill", NodeStyle::fillStyleKey(NodeFillStyle::Nessuno)}, {"color", "#2563eb"}}},
        {"summaries", QJsonObject{{"shape", "brace"}}}, {"milestones", QJsonObject{{"shape", "star"}, {"border", "dashed"}}},
        {"relations", QJsonObject{{"line", "dotted"}, {"end", NodeStyle::branchEndKey(BranchEnd::Dot)}, {"width", "bold"}, {"end_size", "large"}}}});
    QVERIFY2(!styled.isError, qPrintable(styled.text));
    QVERIFY(call(QStringLiteral("set_gantt_task_style"), {{"path", path}, {"ids", QJsonArray{task}}, {"shape", "blob"}}).isError);
    const McpServer::ToolResult own = call(QStringLiteral("set_gantt_task_style"), {{"path", path}, {"ids", QJsonArray{task}},
        {"shape", "arrow"}, {"border", "none"}, {"color", "#16a34a"}, {"label", true}});
    QVERIFY2(!own.isError, qPrintable(own.text));
    const QJsonObject gantt = call(QStringLiteral("read_gantt"), {{"path", path}}).structured;
    const QJsonObject first = gantt.value("tasks").toArray().first().toObject();
    QCOMPARE(first.value("shares").toObject().value(ada).toInt(), 50);
    QCOMPARE(first.value("shares").toObject().value(bob).toInt(), 25);
    QCOMPARE(first.value("progress").toInt(), 40);
    QCOMPARE(first.value("priority").toInt(), 4);
    QCOMPARE(first.value("shape").toString(), QStringLiteral("arrow"));
    QCOMPARE(first.value("border").toString(), QStringLiteral("none"));
    QCOMPARE(first.value("label").toBool(), true);
    const QJsonObject style = gantt.value("style").toObject();
    QCOMPARE(style.value("tasks").toObject().value("shape").toString(), QStringLiteral("rounded"));
    QCOMPARE(style.value("tasks").toObject().value("color").toString(), QStringLiteral("#2563eb"));
    QCOMPARE(style.value("summaries").toObject().value("shape").toString(), QStringLiteral("brace"));
    QCOMPARE(style.value("milestones").toObject().value("border").toString(), QStringLiteral("dashed"));
    QCOMPARE(style.value("relations").toObject().value("end_size").toString(), QStringLiteral("large"));
    Document document;
    QVERIFY(document.open(path, false).ok);
    const GanttPlan plan = document.model()->gantt();
    QCOMPARE(plan.barShape(), GanttPlan::BarShape::Rounded);
    QCOMPARE(plan.linkLine(), GanttPlan::LinkLine::Dotted);
    QCOMPARE(plan.linkEnd(), BranchEnd::Dot);
    QCOMPARE(plan.linkWidth(), GanttPlan::LinkWidth::Bold);
    QCOMPARE(plan.milestoneShape(), GanttPlan::MilestoneShape::Star);
    QCOMPARE(document.model()->nodeById(task)->branchColor, QColor("#16a34a"));
    document.close();
    // null: back to the sheet's and the default.
    QVERIFY(!call(QStringLiteral("set_gantt_task_style"), {{"path", path}, {"ids", QJsonArray{task}}, {"shape", QJsonValue()}, {"label", QJsonValue()}}).isError);
    QVERIFY(!call(QStringLiteral("set_gantt_style"), {{"path", path}, {"tasks", QJsonObject{{"color", QJsonValue()}}}}).isError);
    const QJsonObject reset = call(QStringLiteral("read_gantt"), {{"path", path}}).structured;
    QVERIFY(!reset.value("tasks").toArray().first().toObject().contains("shape"));
    QVERIFY(!reset.value("tasks").toArray().first().toObject().contains("label"));
    QVERIFY(reset.value("style").toObject().value("tasks").toObject().value("color").isNull());
    // Currencies saved on this computer.
    GanttResources::removeSavedCurrency(QStringLiteral("RON lei"));
    QVERIFY(call(QStringLiteral("add_currency"), {{"currency", "EUR"}}).isError);  // built in
    const McpServer::ToolResult added = call(QStringLiteral("add_currency"), {{"currency", "RON lei"}});
    QVERIFY2(!added.isError, qPrintable(added.text));
    QVERIFY(call(QStringLiteral("add_currency"), {{"currency", "RON lei"}}).isError);  // already there
    QVERIFY(call(QStringLiteral("list_currencies"), {}).structured.value("saved").toArray().contains(QJsonValue("RON lei")));
    QVERIFY(!call(QStringLiteral("set_currency"), {{"path", path}, {"currency", "RON lei"}}).isError);
    QVERIFY(!call(QStringLiteral("remove_currency"), {{"currency", "RON lei"}}).isError);
    QVERIFY(call(QStringLiteral("remove_currency"), {{"currency", "RON lei"}}).isError);
    QVERIFY(call(QStringLiteral("remove_currency"), {{"currency", "USD"}}).isError);
    QCOMPARE(call(QStringLiteral("read_gantt"), {{"path", path}}).structured.value("currency").toString(), QStringLiteral("RON lei"));
    // A person removed (the shares go with the assignment).
    QVERIFY(!call(QStringLiteral("remove_person"), {{"path", path}, {"id", bob}, {"confirm", true}}).isError);
    QCOMPARE(call(QStringLiteral("read_gantt"), {{"path", path}}).structured.value("people").toArray().size(), 1);
}

void McpToolsTest::picturePlacementAndWidth() {
    const QString path = file("posti.mentis");
    const QString rootId = call(QStringLiteral("create_map"), {{"path", path}, {"title", "Posti"}}).structured.value("rootId").toString();
    const auto add = [&](const char *title) {
        return call(QStringLiteral("add_topic"), {{"path", path}, {"parent_id", rootId}, {"title", title}}).structured.value("id").toString();
    };
    const QString withImage = add("Foto"), withSticker = add("Adesivo"), withFormula = add("Formula"), plain = add("Niente");
    QImage picture(200, 100, QImage::Format_RGB32);
    picture.fill(Qt::red);
    QVERIFY(picture.save(file("foto.png")));
    // Placement and width given with the picture.
    QVERIFY(call(QStringLiteral("set_image"), {{"path", path}, {"id", withImage}, {"file", file("foto.png")}, {"placement", "sopra"}}).isError);
    QVERIFY(call(QStringLiteral("set_image"), {{"path", path}, {"id", withImage}, {"file", file("foto.png")}, {"width", 5}}).isError);
    const McpServer::ToolResult image = call(QStringLiteral("set_image"), {{"path", path}, {"id", withImage}, {"file", file("foto.png")},
                                                                           {"placement", "left"}, {"width", 100}});
    QVERIFY2(!image.isError, qPrintable(image.text));
    QCOMPARE(image.structured.value("placement").toString(), QStringLiteral("left"));
    QCOMPARE(image.structured.value("height").toDouble(), 50.0);  // proportions kept
    const QString sticker = call(QStringLiteral("list_options"), {}).structured.value("stickers").toArray().first().toString();
    QVERIFY(!call(QStringLiteral("set_sticker"), {{"path", path}, {"id", withSticker}, {"sticker", sticker}, {"placement", "right"}}).isError);
    QVERIFY(!call(QStringLiteral("set_formula"), {{"path", path}, {"id", withFormula}, {"latex", "E = mc^2"}, {"placement", "below"}}).isError);
    // Moved later, several at once; a topic without a picture is refused.
    QVERIFY(call(QStringLiteral("place_visual"), {{"path", path}, {"ids", QJsonArray{withImage, plain}}, {"placement", "above"}}).isError);
    QVERIFY(call(QStringLiteral("place_visual"), {{"path", path}, {"ids", QJsonArray{withImage}}}).isError);
    const McpServer::ToolResult placed = call(QStringLiteral("place_visual"), {{"path", path}, {"ids", QJsonArray{withImage, withSticker}},
                                                                               {"placement", "below"}, {"width", 80}});
    QVERIFY2(!placed.isError, qPrintable(placed.text));
    const QJsonArray topics = read(path).value("root").toObject().value("children").toArray();
    QHash<QString, QJsonObject> pictures;
    for (const QJsonValue &topic : topics) pictures.insert(topic.toObject().value("id").toString(), topic.toObject().value("picture").toObject());
    QCOMPARE(pictures.value(withImage).value("placement").toString(), QStringLiteral("below"));
    QCOMPARE(pictures.value(withImage).value("width").toDouble(), 80.0);
    QCOMPARE(pictures.value(withImage).value("height").toDouble(), 40.0);
    QCOMPARE(pictures.value(withSticker).value("kind").toString(), QStringLiteral("sticker"));
    QCOMPARE(pictures.value(withSticker).value("placement").toString(), QStringLiteral("below"));
    QCOMPARE(pictures.value(withFormula).value("kind").toString(), QStringLiteral("formula"));
    QCOMPARE(pictures.value(withFormula).value("placement").toString(), QStringLiteral("below"));
    QVERIFY(pictures.value(plain).isEmpty());
    Document document;
    QVERIFY(document.open(path, false).ok);
    QCOMPARE(document.model()->nodeById(withImage)->imagePlacement, MindNode::ImagePlacement::Below);
    QCOMPARE(document.model()->nodeById(withImage)->imageSize, QSizeF(80, 40));
    document.close();
}

// FEAT-089: a Pitch template created and changed with a logo, exported, imported, applied to a sheet
// (a theme removes it again) and deleted after the yes.
void McpToolsTest::pitchTemplates() {
    QDir(PitchTemplates::folder()).removeRecursively();
    const QString logo = file("logo.png");
    QImage picture(64, 32, QImage::Format_ARGB32);
    picture.fill(Qt::red);
    QVERIFY(picture.save(logo));
    QVERIFY(call(QStringLiteral("save_pitch_template"), {{"name", "Scuola"}, {"motif", "spiral"}}).isError);
    QVERIFY(call(QStringLiteral("save_pitch_template"), {{"name", "Scuola"}, {"colors", QJsonObject{{"paper", "#ffffff"}}}}).isError);
    QVERIFY(call(QStringLiteral("save_pitch_template"), {{"name", "Scuola"}, {"logo", m_outside.filePath("x.png")}}).isError);
    const McpServer::ToolResult created = call(QStringLiteral("save_pitch_template"),
        {{"name", "Scuola"}, {"base", "light"}, {"colors", QJsonObject{{"background", "#102030"}}}, {"logo", logo},
         {"logo_corner", "top-left"}, {"footer", "Liceo"}, {"numbers", true}});
    QVERIFY2(!created.isError, qPrintable(created.text));
    const QString id = created.structured.value("id").toString();
    QCOMPARE(created.structured.value("logo").toBool(), true);
    const McpServer::ToolResult changed = call(QStringLiteral("save_pitch_template"),
        {{"id", id}, {"colors", QJsonObject{{"background", QJsonValue::Null}}}, {"footer", "Liceo Galilei"}});
    QVERIFY2(!changed.isError, qPrintable(changed.text));
    const Pitch::Template stored = *PitchTemplates::find(id);
    QVERIFY(!stored.background.isValid());  // back to the base theme's
    QCOMPARE(stored.footer, QStringLiteral("Liceo Galilei"));
    QCOMPARE(stored.base, Pitch::Theme::Light);
    QVERIFY(!stored.logo.isEmpty());
    const QJsonArray listed = call(QStringLiteral("list_pitch_templates"), {}).structured.value("templates").toArray();
    QCOMPARE(listed.size(), 1);
    QCOMPARE(listed[0].toObject().value("name").toString(), QStringLiteral("Scuola"));
    // Export, then import into an empty library.
    const QString shared = file("scuola.mentis-theme");
    QVERIFY(!call(QStringLiteral("export_pitch_template"), {{"id", id}, {"file", shared}}).isError);
    QVERIFY(call(QStringLiteral("export_pitch_template"), {{"id", id}, {"file", shared}}).text.contains("confirmation"));
    QVERIFY(PitchTemplates::remove(id));
    QVERIFY(!call(QStringLiteral("import_pitch_template"), {{"file", shared}}).isError);
    QCOMPARE(PitchTemplates::find(id)->footer, QStringLiteral("Liceo Galilei"));
    // Applied to a sheet, then replaced by a theme.
    const QString path = file("presenta.mentis");
    QVERIFY(!call(QStringLiteral("create_map"), {{"path", path}, {"title", "Presenta"}}).isError);
    QVERIFY(call(QStringLiteral("set_pitch"), {{"path", path}, {"template", "tpl-none"}}).isError);
    QVERIFY(call(QStringLiteral("set_pitch"), {{"path", path}, {"template", id}, {"theme", "night"}}).isError);
    const McpServer::ToolResult applied = call(QStringLiteral("set_pitch"), {{"path", path}, {"template", id}});
    QVERIFY2(!applied.isError, qPrintable(applied.text));
    QCOMPARE(applied.structured.value("template").toString(), id);
    {
        Document document;
        QVERIFY(document.open(path, false).ok);
        QCOMPARE(Pitch::sheetTemplate(*document.model())->footer, QStringLiteral("Liceo Galilei"));
        QCOMPARE(document.model()->pitchTheme, QStringLiteral("light"));
        document.close();
    }
    QVERIFY(call(QStringLiteral("set_pitch"), {{"path", path}, {"theme", "night"}}).structured.value("template").isNull());
    // Deleting needs the yes; the map keeps nothing of it now, the library loses it.
    QVERIFY(call(QStringLiteral("delete_pitch_template"), {{"id", id}}).text.contains("confirmation"));
    QVERIFY(!call(QStringLiteral("delete_pitch_template"), {{"id", id}, {"confirm", true}}).isError);
    QVERIFY(!PitchTemplates::find(id));
}

QTEST_MAIN(McpToolsTest)
#include "mcp_tools_test.moc"
