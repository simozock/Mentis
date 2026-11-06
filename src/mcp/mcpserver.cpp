#include "mcpserver.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace {

// JSON-RPC 2.0 and MCP error codes (https://www.jsonrpc.org/specification, MCP 2026-07-28 basic/index).
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kUnsupportedProtocolVersion = -32022;

const QString kVersionKey = QStringLiteral("io.modelcontextprotocol/protocolVersion");
const QString kCapabilitiesKey = QStringLiteral("io.modelcontextprotocol/clientCapabilities");
const QString kServerInfoKey = QStringLiteral("io.modelcontextprotocol/serverInfo");
// The tool list never changes while the server runs: clients may keep it for an hour.
constexpr int kListTtlMs = 3600000;

QJsonObject response(const QJsonValue &id, const QJsonObject &result) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

QJsonObject errorResponse(const QJsonValue &id, int code, const QString &message,
                          const QJsonValue &data = QJsonValue(QJsonValue::Undefined)) {
    QJsonObject error{{"code", code}, {"message", message}};
    if (!data.isUndefined()) error.insert(QStringLiteral("data"), data);
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", error}};
}

QByteArray line(const QJsonObject &message) {
    // Compact JSON escapes the newlines inside strings, so one message stays on one line.
    return QJsonDocument(message).toJson(QJsonDocument::Compact);
}

}  // namespace

McpServer::ToolResult McpServer::ToolResult::object(const QJsonObject &value) {
    return {QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact)), value, false};
}

const QStringList &McpServer::legacyVersions() {
    static const QStringList versions{QStringLiteral("2025-11-25"), QStringLiteral("2025-06-18"),
                                      QStringLiteral("2025-03-26"), QStringLiteral("2024-11-05")};
    return versions;
}

McpServer::McpServer(QString name, QString version, QString instructions, QList<Tool> tools)
    : m_name(std::move(name)), m_version(std::move(version)), m_instructions(std::move(instructions)),
      m_tools(std::move(tools)) {}

QJsonObject McpServer::serverInfo() const { return {{"name", m_name}, {"version", m_version}}; }

QJsonArray McpServer::toolList() const {
    QJsonArray tools;
    for (const Tool &tool : m_tools) {
        QJsonObject entry{{"name", tool.name}, {"description", tool.description}, {"inputSchema", tool.inputSchema}};
        if (!tool.title.isEmpty()) entry.insert(QStringLiteral("title"), tool.title);
        entry.insert(QStringLiteral("annotations"), QJsonObject{{"readOnlyHint", tool.readOnly},
                                                                {"destructiveHint", false},
                                                                {"openWorldHint", false}});
        tools.append(entry);
    }
    return tools;
}

std::optional<QByteArray> McpServer::handleLine(const QByteArray &text) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return line(errorResponse(QJsonValue::Null, kParseError, QStringLiteral("Parse error: %1").arg(parseError.errorString())));
    // Batches were removed in 2025-06-18 and are not in 2026-07-28.
    if (!document.isObject()) return line(errorResponse(QJsonValue::Null, kInvalidRequest, QStringLiteral("Expected one JSON-RPC object")));
    const QJsonObject message = document.object();
    const bool hasId = message.contains(QStringLiteral("id"));
    const QJsonValue id = message.value(QStringLiteral("id"));
    if (!message.contains(QStringLiteral("method"))) return std::nullopt;  // a response from the client: we send no requests
    if (message.value(QStringLiteral("jsonrpc")).toString() != QLatin1String("2.0") || !message.value(QStringLiteral("method")).isString())
        return hasId ? std::optional(line(errorResponse(id, kInvalidRequest, QStringLiteral("Invalid JSON-RPC request")))) : std::nullopt;
    // Notifications (initialized, cancelled...) never get an answer; every call here is short and
    // synchronous, so a cancellation has nothing left to stop.
    if (!hasId) return std::nullopt;
    if (!id.isString() && !id.isDouble()) return line(errorResponse(QJsonValue::Null, kInvalidRequest, QStringLiteral("Invalid id")));
    const QJsonValue params = message.value(QStringLiteral("params"));
    if (!params.isUndefined() && !params.isObject())
        return line(errorResponse(id, kInvalidParams, QStringLiteral("params must be an object")));
    return line(handleRequest(id, message.value(QStringLiteral("method")).toString(), params.toObject()));
}

QJsonObject McpServer::handleRequest(const QJsonValue &id, const QString &method, const QJsonObject &params) {
    if (method == QLatin1String("initialize")) {
        const QString requested = params.value(QStringLiteral("protocolVersion")).toString();
        m_legacyVersion = legacyVersions().contains(requested) ? requested : legacyVersions().first();
        QJsonObject result{{"protocolVersion", m_legacyVersion},
                           {"capabilities", QJsonObject{{"tools", QJsonObject{{"listChanged", false}}}}},
                           {"serverInfo", serverInfo()}};
        if (!m_instructions.isEmpty()) result.insert(QStringLiteral("instructions"), m_instructions);
        return response(id, result);
    }

    const QJsonObject meta = params.value(QStringLiteral("_meta")).toObject();
    if (meta.contains(kVersionKey)) {
        // 2026-07-28: each request stands alone, nothing is inferred from earlier ones.
        const QString requested = meta.value(kVersionKey).toString();
        if (requested != kModernVersion)
            return errorResponse(id, kUnsupportedProtocolVersion, QStringLiteral("Unsupported protocol version"),
                                 QJsonObject{{"supported", QJsonArray{kModernVersion}}, {"requested", requested}});
        if (!meta.value(kCapabilitiesKey).isObject())
            return errorResponse(id, kInvalidParams, QStringLiteral("Missing _meta %1").arg(kCapabilitiesKey));
        if (method == QLatin1String("server/discover")) {
            QJsonObject result{{"resultType", "complete"},
                               {"supportedVersions", QJsonArray{kModernVersion}},
                               {"capabilities", QJsonObject{{"tools", QJsonObject()}}},
                               {"ttlMs", kListTtlMs},
                               {"cacheScope", "public"},
                               {"_meta", QJsonObject{{kServerInfoKey, serverInfo()}}}};
            if (!m_instructions.isEmpty()) result.insert(QStringLiteral("instructions"), m_instructions);
            return response(id, result);
        }
        if (method == QLatin1String("tools/list"))
            return response(id, {{"resultType", "complete"}, {"tools", toolList()}, {"ttlMs", kListTtlMs}, {"cacheScope", "public"}});
        if (method == QLatin1String("tools/call")) return callTool(id, params, true);
        return errorResponse(id, kMethodNotFound, QStringLiteral("Method not found: %1").arg(method));
    }

    if (m_legacyVersion.isEmpty())
        return errorResponse(id, kInvalidParams,
                             QStringLiteral("Missing _meta %1: send it with each request (%2) or start with initialize")
                                 .arg(kVersionKey, kModernVersion));
    if (method == QLatin1String("ping")) return response(id, {});
    if (method == QLatin1String("tools/list")) return response(id, {{"tools", toolList()}});
    if (method == QLatin1String("tools/call")) return callTool(id, params, false);
    return errorResponse(id, kMethodNotFound, QStringLiteral("Method not found: %1").arg(method));
}

QJsonObject McpServer::callTool(const QJsonValue &id, const QJsonObject &params, bool modern) {
    const QString name = params.value(QStringLiteral("name")).toString();
    const auto tool = std::find_if(m_tools.cbegin(), m_tools.cend(), [&name](const Tool &t) { return t.name == name; });
    if (tool == m_tools.cend()) return errorResponse(id, kInvalidParams, QStringLiteral("Unknown tool: %1").arg(name));
    const QJsonValue arguments = params.value(QStringLiteral("arguments"));
    const ToolResult answer = !arguments.isUndefined() && !arguments.isObject()
        ? ToolResult::error(QStringLiteral("arguments must be an object"))
        : tool->call(arguments.toObject());
    QJsonObject result{{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", answer.text}}}},
                       {"isError", answer.isError}};
    if (!answer.structured.isEmpty()) result.insert(QStringLiteral("structuredContent"), answer.structured);
    if (modern) result.insert(QStringLiteral("resultType"), QStringLiteral("complete"));
    return response(id, result);
}
