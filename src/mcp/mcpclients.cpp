#include "mcpclients.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace McpClients {

namespace {

const QString kServer = QStringLiteral("mentis");

bool saveFile(const QString &file, const QByteArray &bytes, QString *error) {
    if (!QDir().mkpath(QFileInfo(file).absolutePath())) { *error = QStringLiteral("cannot create %1").arg(QDir::toNativeSeparators(QFileInfo(file).absolutePath())); return false; }
    QSaveFile out(file);  // all or nothing: the app's file is never left half written
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        *error = out.errorString();
        return false;
    }
    return true;
}

// A TOML basic string (https://toml.io/en/v1.0.0#string): backslashes and quotes escaped.
QString tomlString(QString text) {
    text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    text.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + text + QLatin1Char('"');
}

// Claude Code keeps its configuration itself: its own command adds the server for every project
// (user scope). An older "mentis" is removed first, since add refuses a name that exists.
bool runClaude(const QString &program, const QString &command, QString *error) {
    const auto run = [&program](const QStringList &arguments, QString *output) {
        QProcess process;
        process.setStandardInputFile(QProcess::nullDevice());
        process.start(program, arguments);
        const bool finished = process.waitForFinished(20000);
        if (!finished) process.kill();
        *output = QString::fromLocal8Bit(process.readAllStandardError() + process.readAllStandardOutput()).trimmed();
        return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    QString ignored;
    run({QStringLiteral("mcp"), QStringLiteral("remove"), QStringLiteral("-s"), QStringLiteral("user"), kServer}, &ignored);
    // -s before "--": what follows "--" is the server's own command line.
    if (run({QStringLiteral("mcp"), QStringLiteral("add"), QStringLiteral("-s"), QStringLiteral("user"), kServer, QStringLiteral("--"),
             command, QStringLiteral("--mcp")}, error))
        return true;
    if (error->isEmpty()) *error = QStringLiteral("claude did not answer");
    return false;
}

}  // namespace

Locations Locations::system() {
    Locations where;
    const QString home = QDir::homePath();
#if defined(Q_OS_WIN)
    const QString desktop = qEnvironmentVariable("APPDATA") + QStringLiteral("/Claude");
#elif defined(Q_OS_MACOS)
    const QString desktop = home + QStringLiteral("/Library/Application Support/Claude");
#else
    const QString config = qEnvironmentVariable("XDG_CONFIG_HOME", home + QStringLiteral("/.config"));
    const QString desktop = config + QStringLiteral("/Claude");
#endif
    if (QFileInfo(desktop).isDir()) where.claudeDesktopFolder = desktop;
    // The native installer puts claude in ~/.local/bin, which a program started from the desktop may
    // not have in its PATH.
    where.claudeCommand = QStandardPaths::findExecutable(QStringLiteral("claude"));
    if (where.claudeCommand.isEmpty())
        where.claudeCommand = QStandardPaths::findExecutable(QStringLiteral("claude"), {home + QStringLiteral("/.local/bin")});
    const QString codex = qEnvironmentVariable("CODEX_HOME", home + QStringLiteral("/.codex"));
    if (QFileInfo(codex).isDir() || !QStandardPaths::findExecutable(QStringLiteral("codex")).isEmpty()) where.codexFolder = codex;
    return where;
}

bool writeClaudeDesktop(const QString &file, const QString &command, QString *error) {
    QJsonObject root;
    QFile in(file);
    if (in.exists()) {
        if (!in.open(QIODevice::ReadOnly)) { *error = in.errorString(); return false; }
        const QByteArray bytes = in.readAll();
        in.close();  // Windows refuses to replace a file that is still open
        if (!bytes.trimmed().isEmpty()) {
            QJsonParseError parse;
            const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
            if (!document.isObject()) { *error = QStringLiteral("%1 is not valid JSON, left as it is").arg(QFileInfo(file).fileName()); return false; }
            root = document.object();
        }
    }
    QJsonObject servers = root.value(QStringLiteral("mcpServers")).toObject();
    servers.insert(kServer, QJsonObject{{"command", command}, {"args", QJsonArray{QStringLiteral("--mcp")}}});
    root.insert(QStringLiteral("mcpServers"), servers);
    return saveFile(file, QJsonDocument(root).toJson(QJsonDocument::Indented), error);
}

bool writeCodex(const QString &file, const QString &command, QString *error) {
    QString text;
    QFile in(file);
    if (in.exists()) {
        if (!in.open(QIODevice::ReadOnly)) { *error = in.errorString(); return false; }
        text = QString::fromUtf8(in.readAll());
        in.close();
    }
    // The old [mcp_servers.mentis] table and its sub-tables (env, ...) go; everything else stays.
    static const QRegularExpression ours(QStringLiteral(R"(^\s*\[\s*mcp_servers\s*\.\s*"?mentis"?\s*(\]|\.))"));
    static const QRegularExpression header(QStringLiteral(R"(^\s*\[)"));
    QStringList kept;
    bool skipping = false;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (header.match(line).hasMatch()) skipping = ours.match(line).hasMatch();
        if (!skipping) kept.append(line);
    }
    while (!kept.isEmpty() && kept.last().trimmed().isEmpty()) kept.removeLast();
    if (!kept.isEmpty()) kept.append(QString());
    kept.append(QStringLiteral("[mcp_servers.mentis]"));
    kept.append(QStringLiteral("command = ") + tomlString(command));
    kept.append(QStringLiteral("args = [\"--mcp\"]"));
    kept.append(QString());
    return saveFile(file, kept.join(QLatin1Char('\n')).toUtf8(), error);
}

QList<Outcome> connectAll(const Locations &where, const QString &command) {
    QList<Outcome> outcomes;
    if (!where.claudeDesktopFolder.isEmpty()) {
        Outcome outcome{QStringLiteral("Claude Desktop")};
        outcome.ok = writeClaudeDesktop(where.claudeDesktopFolder + QStringLiteral("/claude_desktop_config.json"), command, &outcome.error);
        outcomes.append(outcome);
    }
    if (!where.claudeCommand.isEmpty()) {
        Outcome outcome{QStringLiteral("Claude Code")};
        outcome.ok = runClaude(where.claudeCommand, command, &outcome.error);
        outcomes.append(outcome);
    }
    if (!where.codexFolder.isEmpty()) {
        Outcome outcome{QStringLiteral("Codex")};
        outcome.ok = writeCodex(where.codexFolder + QStringLiteral("/config.toml"), command, &outcome.error);
        outcomes.append(outcome);
    }
    return outcomes;
}

}  // namespace McpClients
