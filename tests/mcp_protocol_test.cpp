// FEAT-045: the MCP protocol of mentis-mcp (McpServer) with a fake tool. Both eras of
// https://modelcontextprotocol.io/specification/versioning: 2026-07-28 (per-request _meta,
// server/discover, resultType) and the initialize handshake; JSON-RPC errors and notifications.
#include "mcpserver.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

class McpProtocolTest final : public QObject {
    Q_OBJECT

private slots:
    void init();
    void legacyHandshake();
    void legacyUnknownVersionGetsTheNewest();
    void modernRequests();
    void modernRequestNeedsItsMeta();
    void requestsBeforeAnyVersionAreRefused();
    void toolErrorsAndUnknownTools();
    void jsonRpcErrors();
    void notificationsAndResponsesGetNoAnswer();

private:
    QJsonObject send(const QJsonObject &message);
    QJsonObject send(const QByteArray &text);
    static QJsonObject modernMeta() {
        return {{"_meta", QJsonObject{{"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
                                      {"io.modelcontextprotocol/clientCapabilities", QJsonObject()}}}};
    }
    std::unique_ptr<McpServer> m_server;
};

void McpProtocolTest::init() {
    McpServer::Tool echo{QStringLiteral("echo"), QStringLiteral("Echo"), QStringLiteral("Returns its text"),
                         QJsonObject{{"type", "object"}}, true, [](const QJsonObject &args) {
        if (!args.value(QStringLiteral("text")).isString()) return McpServer::ToolResult::error(QStringLiteral("text is missing"));
        return McpServer::ToolResult::object({{"echo", args.value(QStringLiteral("text"))}});
    }};
    m_server = std::make_unique<McpServer>(QStringLiteral("mentis"), QStringLiteral("9.9"), QStringLiteral("Guide"),
                                           QList<McpServer::Tool>{echo});
}

QJsonObject McpProtocolTest::send(const QByteArray &text) {
    const std::optional<QByteArray> answer = m_server->handleLine(text);
    if (!answer) return {};
    if (answer->contains('\n')) return {{"brokenLine", true}};  // one message per line (stdio)
    return QJsonDocument::fromJson(*answer).object();
}

QJsonObject McpProtocolTest::send(const QJsonObject &message) {
    return send(QJsonDocument(message).toJson(QJsonDocument::Indented));  // a newline-free line is the client's job; indented must still parse
}

void McpProtocolTest::legacyHandshake() {
    const QJsonObject init = send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
                                              {"params", QJsonObject{{"protocolVersion", "2025-06-18"}, {"capabilities", QJsonObject()},
                                                                     {"clientInfo", QJsonObject{{"name", "test"}, {"version", "1"}}}}}});
    const QJsonObject result = init.value("result").toObject();
    QCOMPARE(init.value("id").toInt(), 1);
    QCOMPARE(result.value("protocolVersion").toString(), QStringLiteral("2025-06-18"));
    QCOMPARE(result.value("instructions").toString(), QStringLiteral("Guide"));
    QCOMPARE(result.value("serverInfo").toObject().value("name").toString(), QStringLiteral("mentis"));
    QVERIFY(result.value("capabilities").toObject().contains("tools"));
    QVERIFY(send(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}).isEmpty());
    QVERIFY(send(QJsonObject{{"jsonrpc", "2.0"}, {"id", "p"}, {"method", "ping"}}).value("result").toObject().isEmpty());
    QCOMPARE(send(QJsonObject{{"jsonrpc", "2.0"}, {"id", "p"}, {"method", "ping"}}).value("id").toString(), QStringLiteral("p"));
    const QJsonObject list = send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}}).value("result").toObject();
    QCOMPARE(list.value("tools").toArray().size(), 1);
    QVERIFY(!list.contains("resultType"));  // the 2026-07-28 envelope is not sent to older clients
    const QJsonObject tool = list.value("tools").toArray().first().toObject();
    QCOMPARE(tool.value("name").toString(), QStringLiteral("echo"));
    QVERIFY(tool.value("annotations").toObject().value("readOnlyHint").toBool());
    const QJsonObject call = send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"},
                                              {"params", QJsonObject{{"name", "echo"}, {"arguments", QJsonObject{{"text", "ciao\nmondo"}}}}}})
                                 .value("result").toObject();
    QVERIFY(!call.value("isError").toBool());
    QCOMPARE(call.value("structuredContent").toObject().value("echo").toString(), QStringLiteral("ciao\nmondo"));
    QCOMPARE(call.value("content").toArray().first().toObject().value("type").toString(), QStringLiteral("text"));
}

void McpProtocolTest::legacyUnknownVersionGetsTheNewest() {
    const QJsonObject init = send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
                                              {"params", QJsonObject{{"protocolVersion", "2099-01-01"}}}});
    QCOMPARE(init.value("result").toObject().value("protocolVersion").toString(), McpServer::legacyVersions().first());
}

void McpProtocolTest::modernRequests() {
    QJsonObject params = modernMeta();
    const QJsonObject discover = send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "server/discover"}, {"params", params}})
                                     .value("result").toObject();
    QCOMPARE(discover.value("resultType").toString(), QStringLiteral("complete"));
    QCOMPARE(discover.value("supportedVersions").toArray(), QJsonArray{"2026-07-28"});
    QVERIFY(discover.value("capabilities").toObject().contains("tools"));
    QCOMPARE(discover.value("instructions").toString(), QStringLiteral("Guide"));
    QCOMPARE(discover.value("_meta").toObject().value("io.modelcontextprotocol/serverInfo").toObject().value("version").toString(),
             QStringLiteral("9.9"));
    const QJsonObject list = send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}, {"params", params}})
                                 .value("result").toObject();
    QCOMPARE(list.value("resultType").toString(), QStringLiteral("complete"));
    QVERIFY(list.value("ttlMs").toInt() > 0);
    QCOMPARE(list.value("cacheScope").toString(), QStringLiteral("public"));
    params.insert("name", "echo");
    params.insert("arguments", QJsonObject{{"text", "x"}});
    const QJsonObject call = send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"}, {"params", params}})
                                 .value("result").toObject();
    QCOMPARE(call.value("resultType").toString(), QStringLiteral("complete"));
    QCOMPARE(call.value("structuredContent").toObject().value("echo").toString(), QStringLiteral("x"));
    // ping and initialize-era methods do not exist in 2026-07-28.
    QCOMPARE(send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 4}, {"method", "ping"}, {"params", modernMeta()}})
                 .value("error").toObject().value("code").toInt(), -32601);
}

void McpProtocolTest::modernRequestNeedsItsMeta() {
    QJsonObject wrongVersion = modernMeta();
    QJsonObject meta = wrongVersion.value("_meta").toObject();
    meta.insert("io.modelcontextprotocol/protocolVersion", "1900-01-01");
    wrongVersion.insert("_meta", meta);
    const QJsonObject error = send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}, {"params", wrongVersion}})
                                  .value("error").toObject();
    QCOMPARE(error.value("code").toInt(), -32022);
    QCOMPARE(error.value("data").toObject().value("requested").toString(), QStringLiteral("1900-01-01"));
    QCOMPARE(error.value("data").toObject().value("supported").toArray(), QJsonArray{"2026-07-28"});
    QJsonObject noCapabilities{{"_meta", QJsonObject{{"io.modelcontextprotocol/protocolVersion", "2026-07-28"}}}};
    QCOMPARE(send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}, {"params", noCapabilities}})
                 .value("error").toObject().value("code").toInt(), -32602);
}

void McpProtocolTest::requestsBeforeAnyVersionAreRefused() {
    const QJsonObject error = send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}}).value("error").toObject();
    QCOMPARE(error.value("code").toInt(), -32602);
    QVERIFY(error.value("message").toString().contains("initialize"));
}

void McpProtocolTest::toolErrorsAndUnknownTools() {
    QJsonObject params = modernMeta();
    params.insert("name", "echo");
    params.insert("arguments", QJsonObject());
    const QJsonObject failed = send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"}, {"params", params}})
                                   .value("result").toObject();
    QVERIFY(failed.value("isError").toBool());  // input errors are results (2026-07-28)
    QCOMPARE(failed.value("content").toArray().first().toObject().value("text").toString(), QStringLiteral("text is missing"));
    params.insert("arguments", "not an object");
    QVERIFY(send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/call"}, {"params", params}})
                .value("result").toObject().value("isError").toBool());
    params.insert("name", "nope");
    const QJsonObject unknown = send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"}, {"params", params}})
                                    .value("error").toObject();
    QCOMPARE(unknown.value("code").toInt(), -32602);
    QVERIFY(unknown.value("message").toString().contains("nope"));
}

void McpProtocolTest::jsonRpcErrors() {
    QCOMPARE(send(QByteArray("{not json")).value("error").toObject().value("code").toInt(), -32700);
    QCOMPARE(send(QByteArray("[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}]")).value("error").toObject().value("code").toInt(), -32600);
    QCOMPARE(send(QJsonObject{{"jsonrpc", "1.0"}, {"id", 1}, {"method", "ping"}}).value("error").toObject().value("code").toInt(), -32600);
    QCOMPARE(send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "x"}, {"params", QJsonArray()}})
                 .value("error").toObject().value("code").toInt(), -32602);
    QCOMPARE(send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "nope/nope"}, {"params", modernMeta()}})
                 .value("error").toObject().value("code").toInt(), -32601);
    const QJsonObject parse = send(QByteArray("{oops"));
    QVERIFY(parse.value("id").isNull());
    QVERIFY(!parse.value("error").toObject().contains("data"));
}

void McpProtocolTest::notificationsAndResponsesGetNoAnswer() {
    QVERIFY(!m_server->handleLine(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":1}})"));
    QVERIFY(!m_server->handleLine(R"({"jsonrpc":"2.0","method":"tools/list"})"));  // no id = notification
    QVERIFY(!m_server->handleLine(R"({"jsonrpc":"2.0","id":5,"result":{}})"));     // the client's answer
}

QTEST_GUILESS_MAIN(McpProtocolTest)
#include "mcp_protocol_test.moc"
