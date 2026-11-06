#pragma once

#include "mcpserver.h"

#include <QObject>
#include <QString>
#include <functional>
#include <optional>

class QLocalServer;
class QLocalSocket;

// FEAT-045 phase 2: "Mentis --mcp" reaches the map open in a Mentis window. Each window with agents
// turned on listens on a local channel (only the same user can connect) and writes a small entry in
// AppLocalDataLocation/agents/<pid>.json: channel, open file, last activation. One request per
// connection: a JSON line {"tool", "arguments"} in, a JSON line {"text", "structured", "isError"} out.
namespace McpBridge {

struct Instance {
    QString server;
    QString path;      // canonical path of the open map; empty = a map without a name
    qint64 activeMs = 0;  // last activation of the window (ms since the epoch)
};

// The windows that answer; entries of windows that are gone are removed.
QList<Instance> instances();
std::optional<Instance> instanceWith(const QString &canonicalPath);
std::optional<Instance> lastActive();
McpServer::ToolResult forward(const Instance &window, const QString &tool, const QJsonObject &arguments);

// The window's side. run executes one tool on the open map (MainWindow::runAgentTool).
class Host final : public QObject {
    Q_OBJECT
public:
    using Runner = std::function<McpServer::ToolResult(const QString &tool, const QJsonObject &arguments)>;
    explicit Host(Runner run, QObject *parent = nullptr);
    ~Host() override;

    // Agents on: the channel opens and the entry is written; off: both go (no channel, no attack
    // surface, FEAT-045).
    void setListening(bool on);
    bool isListening() const { return m_server != nullptr; }
    void setPath(const QString &path);  // the open map; empty = without a name
    void markActive();

private:
    void writeEntry() const;
    void serve(QLocalSocket *socket);

    Runner m_run;
    QLocalServer *m_server = nullptr;
    QString m_path;
    qint64 m_activeMs = 0;
    QString m_entryFile;
};

}  // namespace McpBridge
