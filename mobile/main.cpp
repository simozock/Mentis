// FEAT-097: Mentis for Android, a Qt Quick front end on MentisDomain (ADR-006). No Qt Widgets.
#include "mobileapp.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(Mentis_MobilePlugin)  // the static module of mobile/CMakeLists.txt

Q_LOGGING_CATEGORY(lcMobileStart, "mentis.mobile.start")

int main(int argc, char *argv[]) {
    QElapsedTimer startClock;  // FEAT-097: cold start to the Home's first frame (from main)
    startClock.start();
    MobileApp::prepareSurfaceFormat();
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Mentis"));
    // Settings of their own: on the desktop the prototype never reads or writes the app's.
    QCoreApplication::setApplicationName(QStringLiteral("MentisMobile"));
    MobileApp::prepare();

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app, [&startClock](QObject *object) {
        auto *window = qobject_cast<QQuickWindow *>(object);
        if (!window) return;
        // frameSwapped comes from the render thread: queued, so more frames may arrive before the
        // first one is handled; only that one is logged.
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection = QObject::connect(window, &QQuickWindow::frameSwapped, window, [connection, &startClock] {
            if (!*connection) return;
            QObject::disconnect(*connection);
            *connection = {};
            qCInfo(lcMobileStart, "FEAT-097 start: first frame after %lld ms", startClock.elapsed());
        }, Qt::QueuedConnection);
    });
    engine.loadFromModule("Mentis.Mobile", "Main");
    return QGuiApplication::exec();
}
