#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

// FEAT-045 / ADR-005: the part of the Model Context Protocol a tools server needs, written in Mentis
// (there is no official C++ SDK). Knows nothing of maps: the tools come from outside.
//
// Dual era, as https://modelcontextprotocol.io/specification/2026-07-28/basic/versioning asks:
// - 2026-07-28: no handshake; every request carries its version and the client's capabilities in
//   _meta, server/discover describes the server, every result has resultType;
// - earlier versions: the initialize handshake, then tools/list, tools/call and ping.
// One JSON-RPC message per line in, one per line out (stdio transport).
class McpServer {
public:
    // A tool's answer. Invalid arguments and failures are results with isError (2026-07-28 moves input
    // validation there); structured is also sent as text for clients that only read text.
    struct ToolResult {
        QString text;
        QJsonObject structured;
        bool isError = false;
        static ToolResult error(const QString &message) { return {message, {}, true}; }
        static ToolResult object(const QJsonObject &value);
    };
    struct Tool {
        QString name;
        QString title;
        QString description;
        QJsonObject inputSchema;
        bool readOnly = false;  // annotations.readOnlyHint
        std::function<ToolResult(const QJsonObject &arguments)> call;
    };

    static inline const QString kModernVersion = QStringLiteral("2026-07-28");
    // The initialize era, newest first: an unknown requested version gets the first.
    static const QStringList &legacyVersions();

    McpServer(QString name, QString version, QString instructions, QList<Tool> tools);

    // One line read from stdin; the line to write on stdout (without the newline), or nothing for a
    // notification or a response the client sent.
    std::optional<QByteArray> handleLine(const QByteArray &line);

    QString negotiatedLegacyVersion() const { return m_legacyVersion; }

private:
    QJsonObject handleRequest(const QJsonValue &id, const QString &method, const QJsonObject &params);
    QJsonObject callTool(const QJsonValue &id, const QJsonObject &params, bool modern);
    QJsonArray toolList() const;
    QJsonObject serverInfo() const;

    QString m_name;
    QString m_version;
    QString m_instructions;
    QList<Tool> m_tools;
    QString m_legacyVersion;  // set by initialize; empty = no legacy session
};
