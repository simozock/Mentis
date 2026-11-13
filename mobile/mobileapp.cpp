#include "mobileapp.h"

#include "localization.h"
#include "nodegeometry.h"
#include "nodestyle.h"

#include <QFont>
#include <QGuiApplication>
#include <QQuickStyle>
#include <QSurfaceFormat>

void MobileApp::prepareSurfaceFormat() {
    // The map's lines and shapes are plain triangles (MapScene): 4x multisampling smooths their
    // edges. Qt Quick takes the sample count from the default format on every graphics API
    // (https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph-renderer.html#antialiasing).
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);
}

void MobileApp::prepare() {
    // The interface font of the desktop (ui-foundations.md); the map's fonts are loaded when measured.
    NodeGeometry::loadInterfaceFont();
    QFont font = QGuiApplication::font();
    font.setFamily(NodeStyle::kDefaultMapFontFamily);
    QGuiApplication::setFont(font);
    Localization::installTranslators();  // qsTr reads the translators of QCoreApplication
    // ui-mobile.md: the desktop's look, not a platform style; Basic is the plainest to draw over
    // (https://doc.qt.io/qt-6/qtquickcontrols-styles.html).
    QQuickStyle::setStyle(QStringLiteral("Basic"));
}
