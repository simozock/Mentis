#include "mcpbridge.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace McpBridge {
namespace {

constexpr int kConnectMs = 500;
// A tool runs on the window's thread between two events, in a moment; a confirmation waits for the
// user's answer in a dialog, so the window is given minutes.
constexpr int kAnswerMs = 5 * 60000;
constexpr qint64 kMaxLineBytes = 4 * 1024 * 1024;  // a request: Markdown up to 1 MB and the rest

constexpr Qt::CaseSensitivity kPathCase =
#ifdef Q_OS_WIN
    Qt::CaseInsensitive;
#else
    Qt::CaseSensitive;
#endif

QString registryFolder() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/agents");
}

bool answers(const QString &server) {
    QLocalSocket probe;
    probe.connectToServer(server);
    const bool ok = probe.waitForConnected(kConnectMs);
    probe.abort();
    return ok;
}

// One line (without the newline), or nothing when the other side does not finish it in time.
std::optional<QByteArray> readLine(QLocalSocket &socket, int timeoutMs) {
    QByteArray buffer;
    while (true) {
        const qsizetype end = buffer.indexOf('\n');
        if (end >= 0) return buffer.left(end);
        if (buffer.size() > kMaxLineBytes) return std::nullopt;
        if (!socket.waitForReadyRead(timeoutMs)) return std::nullopt;
        buffer += socket.readAll();
    }
}

QByteArray line(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n'; }

}  // namespace

QList<Instance> instances() {
    QList<Instance> live;
    const QDir folder(registryFolder());
    for (const QFileInfo &entry : folder.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        QFile file(entry.filePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        const Instance instance{json.value(QStringLiteral("server")).toString(), json.value(QStringLiteral("path")).toString(),
                                qint64(json.value(QStringLiteral("active")).toDouble())};
        if (instance.server.isEmpty() || !answers(instance.server)) {
            QFile::remove(entry.filePath());  // a window that closed without removing it (a crash)
            continue;
        }
        live.append(instance);
    }
    return live;
}

std::optional<Instance> instanceWith(const QString &canonicalPath) {
    for (const Instance &instance : instances())
        if (!instance.path.isEmpty() && instance.path.compare(canonicalPath, kPathCase) == 0) return instance;
    return std::nullopt;
}

std::optional<Instance> lastActive() {
    std::optional<Instance> best;
    for (const Instance &instance : instances())
        if (!best || instance.activeMs > best->activeMs) best = instance;
    return best;
}

McpServer::ToolResult forward(const Instance &window, const QString &tool, const QJsonObject &arguments) {
    QLocalSocket socket;
    socket.connectToServer(window.server);
    if (!socket.waitForConnected(kConnectMs)) return McpServer::ToolResult::error(QStringLiteral("Mentis did not answer"));
    socket.write(line({{"tool", tool}, {"arguments", arguments}}));
    socket.flush();
    const std::optional<QByteArray> answer = readLine(socket, kAnswerMs);
    if (!answer) return McpServer::ToolResult::error(QStringLiteral("Mentis did not answer in time"));
    const QJsonObject json = QJsonDocument::fromJson(*answer).object();
    return {json.value(QStringLiteral("text")).toString(), json.value(QStringLiteral("structured")).toObject(),
            json.value(QStringLiteral("isError")).toBool()};
}

Host::Host(Runner run, QObject *parent) : QObject(parent), m_run(std::move(run)) {
    m_entryFile = registryFolder() + QStringLiteral("/%1.json").arg(QCoreApplication::applicationPid());
}

Host::~Host() { setListening(false); }

void Host::setListening(bool on) {
    if (on == isListening()) return;
    if (!on) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
        QFile::remove(m_entryFile);
        return;
    }
    auto *server = new QLocalServer(this);
    server->setSocketOptions(QLocalServer::UserAccessOption);  // only this user's processes
    const QString name = QStringLiteral("mentis-agents-%1-%2")
                             .arg(QCoreApplication::applicationPid())
                             .arg(QUuid::createUuid().toString(QUuid::Id128).left(8));
    if (!server->listen(name)) {
        delete server;
        return;  // no channel: agents fall back to the files (phase 1)
    }
    connect(server, &QLocalServer::newConnection, this, [this, server] {
        while (QLocalSocket *socket = server->nextPendingConnection()) serve(socket);
    });
    m_server = server;
    writeEntry();
}

void Host::setPath(const QString &path) {
    const QString canonical = path.isEmpty() ? QString() : QFileInfo(path).canonicalFilePath();
    if (canonical == m_path) return;
    m_path = canonical;
    if (isListening()) writeEntry();
}

void Host::markActive() {
    m_activeMs = QDateTime::currentMSecsSinceEpoch();
    if (isListening()) writeEntry();
}

void Host::writeEntry() const {
    QDir().mkpath(registryFolder());
    QSaveFile file(m_entryFile);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(QJsonObject{{"server", m_server->serverName()}, {"path", m_path}, {"active", double(m_activeMs)}})
                   .toJson(QJsonDocument::Compact));
    file.commit();
}

void Host::serve(QLocalSocket *socket) {
    socket->setParent(this);
    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
        if (!socket->canReadLine()) {
            if (socket->bytesAvailable() > kMaxLineBytes) socket->abort();
            return;
        }
        const QJsonObject request = QJsonDocument::fromJson(socket->readLine()).object();
        const McpServer::ToolResult result = m_run(request.value(QStringLiteral("tool")).toString(),
                                                   request.value(QStringLiteral("arguments")).toObject());
        socket->write(line({{"text", result.text}, {"structured", result.structured}, {"isError", result.isError}}));
        socket->flush();
        socket->disconnectFromServer();
    });
}

}  // namespace McpBridge
