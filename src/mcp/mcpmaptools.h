#pragma once

#include "mcpserver.h"

class Document;
class MindMapModel;
class MindMapScene;

#include <QString>
#include <QStringList>
#include <functional>

// FEAT-045 phase 1: the tools an agent uses on .mentis files (base + Markdown, user's decision
// 2026-11-02). Every call opens the file, changes it, lays the map out, saves and closes it; the
// open app's lock is respected. Paths are absolute; the user only turns agents on in Settings
// (user's decision 2026-11-06: no allowed folders).
namespace McpMapTools {

// What the user allowed in Settings > General > Agents (MCP); off by default.
struct Access {
    bool enabled = false;
    // Asks the user to confirm what an agent is about to do (kind: "delete_sheet", "overwrite";
    // subject: the sheet's name, the file). nullopt = nobody can ask (no Mentis window): the agent's
    // confirm: true counts instead (user's decision 2026-11-02, "both").
    std::function<std::optional<bool>(const QString &kind, const QString &subject)> ask;
    static Access load();  // QSettings "mcp/enabled"
    void save() const;
};

// Limits of one call (FEAT-045).
inline constexpr int kMaxTextLength = 10000;           // a topic's title or notes
inline constexpr int kMaxMarkdownBytes = 1024 * 1024;  // Markdown sent in one call
inline constexpr int kMaxReadTopics = 5000;            // topics returned by read_map
inline constexpr int kMaxListedMaps = 500;
inline constexpr int kMaxListSeconds = 5;  // list_maps walking a folder

// Where report_issue opens a prefilled issue: the only place to change when the public repository
// moves (GitHub, Codeberg).
inline const QString kNewIssueUrl = QStringLiteral("https://github.com/simozock/Mentis/issues/new");

// access is read at every call, so turning the server off in Settings applies at once. A tool on a
// map goes to the Mentis window that has it open (phase 2, McpBridge), otherwise to the file;
// without path it goes to the window used last.
QList<McpServer::Tool> tools(std::function<Access()> access);

// The tools on one map, the same on a file and in a window. edit changes the document's active
// sheet (the caller shows the requested one first); write = the map changes, so the caller lays it
// out and records it (saves the file, or one Undo step in the window). save_open_map has no edit:
// only a window can run it.
struct MapEdit {
    McpServer::Tool tool;
    bool write = false;
    // access: whether agents are on, and how to ask the user for a confirmation.
    std::function<McpServer::ToolResult(const Access &access, const QJsonObject &args, Document &, MindMapScene &, MindMapModel &)> edit;
};
const QList<MapEdit> &mapEdits();
const MapEdit *mapEdit(const QString &name);

// The answer of every tool while agents are off.
QString offMessage();

// The guide for agents (the server's instructions).
QString instructions();

}  // namespace McpMapTools
