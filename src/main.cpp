#include "formulatext.h"
#include "iconlibrary.h"
#include "localization.h"
#include "mainwindow.h"
#include "mcpstdio.h"
#include "nodeitem.h"
#include "nodestyle.h"
#include "uitheme.h"
#include "updateprompt.h"
#include "wheelguard.h"
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QStyleFactory>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

int main(int argc, char *argv[]) {
    const QStringList rawArguments = [&] {
        QStringList values;
        for (int i = 0; i < argc; ++i) values.append(QString::fromLocal8Bit(argv[i]));
        return values;
    }();
    const bool snapshotRequested = rawArguments.contains(QStringLiteral("--ui-snapshot"));
    if (snapshotRequested) QStandardPaths::setTestModeEnabled(true);
    // FEAT-045: "Mentis --mcp" is the MCP server an agent's client starts over stdio, no window.
    const bool mcpRequested = rawArguments.contains(QStringLiteral("--mcp"));
    if (mcpRequested) McpStdio::prepareProcess();

    QApplication app(argc, argv);
    FormulaText::installRendering();  // FEAT-096: the core renders formulas through it (also with --mcp)
    QCoreApplication::setOrganizationName("Mentis");
    // Bug 2026-10-18: la schermata deterministica azzera le impostazioni (settings.clear() qui
    // sotto) e lo faceva su quelle vere dell'utente: recenti, colori, scorciatoie, profilo. Ora usa
    // impostazioni proprie. QStandardPaths::setTestModeEnabled non vale per QSettings.
    QCoreApplication::setApplicationName(snapshotRequested ? "MentisSnapshot" : "Mentis");
    QCoreApplication::setApplicationVersion(Updates::currentVersion());
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    // The interface font is Atkinson Hyperlegible, bundled (user, 2026-10-28; before: the system
    // font). Only its four files are registered here; the other bundled fonts wait for the map.
    NodeItem::loadInterfaceFont();
    {
        QFont font = QApplication::font();
        font.setFamily(NodeStyle::kDefaultMapFontFamily);
        QApplication::setFont(font);
    }
    // The agent's server: the user's settings and fonts, no window, no translations (agents read
    // English).
    if (mcpRequested) return McpStdio::run();
    WheelGuard::install();  // the wheel over a closed select does not change it

    // FEAT-046 / ADR-003: testi del codice in inglese; traduzione della lingua scelta nelle
    // Impostazioni o, se "Automatica", della lingua del sistema (oggi l'italiano), con quelle di Qt.
    Localization::installTranslators();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Mentis"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption snapshotOption(QStringLiteral("ui-snapshot"),
                                            QCoreApplication::translate("main", "Save a deterministic screenshot of the Home and quit."),
                                            QCoreApplication::translate("main", "file"));
    parser.addOption(snapshotOption);
    // FEAT-105: an agent's map opens where the agent wrote it, not as a copy in the Mentis folder.
    const QCommandLineOption inPlaceOption(QStringLiteral("in-place"),
                                           QCoreApplication::translate("main", "Open the map where it is, without copying it into the Mentis folder."));
    parser.addOption(inPlaceOption);
    // A map to open at start (FEAT-045: open_map starts Mentis with the map the agent made).
    parser.addPositionalArgument(QStringLiteral("file"), QCoreApplication::translate("main", "Map to open."));
    parser.process(app);

    if (parser.isSet(snapshotOption)) {
        QSettings settings;
        settings.clear();
        settings.setValue(QStringLiteral("performance/mode"), QStringLiteral("light"));
    }
    UiTheme::loadAppearance();  // FEAT-035 part B: font, text size and density from Settings
    // 2026-10-14: la cache delle immagini estratte dai documenti (embedded-assets) cresceva senza
    // limite. Una volta al giorno, prima di aprire qualunque documento, si tolgono i file non
    // modificati da 30 giorni: se servono ancora, l'apertura del documento li riestrae dal file.
    {
        QSettings settings;
        const QDateTime now = QDateTime::currentDateTime();
        const QDateTime lastCleanup = settings.value("cache/lastCleanup").toDateTime();
        if (!lastCleanup.isValid() || lastCleanup.addDays(1) < now) {
            const QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/embedded-assets";
            QDirIterator files(root, QDir::Files, QDirIterator::Subdirectories);
            while (files.hasNext()) {
                const QFileInfo file(files.next());
                if (file.lastModified().addDays(30) < now) QFile::remove(file.absoluteFilePath());
            }
            settings.setValue("cache/lastCleanup", now);
        }
    }
    // BUG-037: files taken out of the sticker and marker libraries in the previous sessions.
    for (const auto kind : {IconLibrary::Kind::Sticker, IconLibrary::Kind::Marker}) IconLibrary::removeDeletedFiles(kind);
    MainWindow window;
    if (parser.isSet(snapshotOption)) window.resize(1440, 900);
    window.show();
    if (const QStringList files = parser.positionalArguments(); !files.isEmpty() && !parser.isSet(snapshotOption))
        QTimer::singleShot(0, &window, [&window, file = files.first(), inPlace = parser.isSet(inPlaceOption)] { window.openAtStart(file, inPlace); });

    // FEAT-090: the update check waits for the window to be ready; never in the snapshot.
    if (!parser.isSet(snapshotOption)) QTimer::singleShot(5000, &window, [&window] { window.updatePrompt()->checkAtStartup(); });
    // FEAT-093: the mascot offers the tour once, at the first start.
    if (!parser.isSet(snapshotOption)) QTimer::singleShot(800, &window, [&window] { window.offerTourAtFirstStart(); });

    if (parser.isSet(snapshotOption)) {
        const QString outputPath = QFileInfo(parser.value(snapshotOption)).absoluteFilePath();
        QDir().mkpath(QFileInfo(outputPath).absolutePath());
        QTimer::singleShot(250, &window, [&window, outputPath] {
            const bool saved = window.grab().save(outputPath, "PNG");
            QCoreApplication::exit(saved ? 0 : 2);
        });
    }
    return app.exec();
}
