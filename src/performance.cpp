#include "performance.h"

#include <QCoreApplication>
#include <QSettings>
#include <QThread>

namespace Performance {
namespace {
// Il profilo si calcola una volta: cambia solo da Impostazioni (setMode).
Profile &cachedProfile() {
    static Profile profile = [] {
        const Mode chosen = mode();
        if (chosen == Mode::Standard) return Profile::Standard;
        if (chosen == Mode::Light) return Profile::Light;
        // Automatico: con 2 core logici o meno (CPU single/dual core senza hyper-threading) si usa il
        // profilo leggero. Fonte: QThread::idealThreadCount restituisce i core logici del sistema.
        return QThread::idealThreadCount() <= 2 ? Profile::Light : Profile::Standard;
    }();
    return profile;
}
bool light() { return cachedProfile() == Profile::Light; }
}

Mode mode() {
    const QString value = QSettings().value(QStringLiteral("performance/mode"), QStringLiteral("auto")).toString();
    if (value == QStringLiteral("standard")) return Mode::Standard;
    if (value == QStringLiteral("light")) return Mode::Light;
    return Mode::Automatic;
}

void setMode(Mode value) {
    QSettings().setValue(QStringLiteral("performance/mode"),
                         value == Mode::Standard ? QStringLiteral("standard")
                         : value == Mode::Light ? QStringLiteral("light") : QStringLiteral("auto"));
    cachedProfile() = value == Mode::Standard ? Profile::Standard
                    : value == Mode::Light ? Profile::Light
                    : (QThread::idealThreadCount() <= 2 ? Profile::Light : Profile::Standard);
}

Profile profile() { return cachedProfile(); }

QString profileName(Profile value) {
    return value == Profile::Light ? QCoreApplication::translate("Performance", "Light")
                                   : QCoreApplication::translate("Performance", "Standard");
}

// Standard: valori in uso prima dei profili. Leggero: meno lavoro per CPU, disco e memoria.
int autoSaveDelayMs() { return light() ? 2000 : 750; }
double detailZoomThreshold() { return light() ? 0.6 : 0.4; }
double imageScaleFactor() { return light() ? 1.0 : 2.0; }
int pixmapCacheKb() { return light() ? 10240 : 32768; }
bool smoothPixmaps() { return !light(); }
bool animationsEnabled() { return !light(); }
bool generateMissingPreviews() { return !light(); }
int historyCacheSteps() { return light() ? 50 : 100; }
}
