#include "mcpstdio.h"

#include "mcpmaptools.h"
#include "mcpserver.h"

#include <QCoreApplication>

#include <iostream>
#include <string>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace McpStdio {

void prepareProcess() {
    // No window: the map is laid out by the same scene as the app, which needs a QApplication; the
    // offscreen platform also keeps macOS from showing a Dock icon for a background server.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
    // Bytes as they are: the text mode would turn "\n" into "\r\n" and read "\r\n" as two characters.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

int run() {
    McpServer server(QStringLiteral("mentis"), QCoreApplication::applicationVersion(), McpMapTools::instructions(),
                     McpMapTools::tools(&McpMapTools::Access::load));
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (const std::optional<QByteArray> answer = server.handleLine(QByteArray::fromStdString(line))) {
            std::cout.write(answer->constData(), answer->size());
            std::cout.put('\n');
            std::cout.flush();
        }
    }
    return 0;  // stdin closed: the client is done (MCP: exit promptly)
}

}  // namespace McpStdio
