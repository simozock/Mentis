#pragma once

// FEAT-045 (user's request 2026-11-06): turning agents on adds Mentis to the agents' apps found on
// this computer, so nobody pastes the configuration by hand. Where each app keeps its MCP servers:
// - Claude Desktop: claude_desktop_config.json, "mcpServers" (https://modelcontextprotocol.io/quickstart/user);
// - Claude Code: its own command, "claude mcp add -s user" (https://docs.anthropic.com/en/docs/claude-code/mcp);
// - Codex: config.toml in CODEX_HOME (~/.codex), table [mcp_servers.<name>] (https://developers.openai.com/codex/mcp).

#include <QList>
#include <QString>

namespace McpClients {

// Where the apps are; empty = not on this computer. Tests give temporary folders.
struct Locations {
    QString claudeDesktopFolder;  // the folder of claude_desktop_config.json
    QString claudeCommand;        // the claude program (Claude Code)
    QString codexFolder;          // CODEX_HOME
    static Locations system();
};

struct Outcome {
    QString app;    // "Claude Desktop", "Claude Code", "Codex"
    bool ok = false;
    QString error;  // when not ok
};

// Adds (or updates) the server "mentis" = command --mcp in every app found; other servers and
// settings stay as they are. A file that cannot be read as it should is left untouched.
QList<Outcome> connectAll(const Locations &where, const QString &command);

// The single writers, for the tests.
bool writeClaudeDesktop(const QString &file, const QString &command, QString *error);
bool writeCodex(const QString &file, const QString &command, QString *error);

}  // namespace McpClients
