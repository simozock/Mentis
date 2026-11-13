import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Mentis.Mobile

// FEAT-107: a folder of the Home, as the desktop's card (MainWindow homeFolderIcon): filled with its
// colour, the tab darker; without a colour, the same folder outlined in the light text colour
// (assets/folder.svg, user's request 2026-11-11). Drawn on the 24-unit grid of the Hugeicons.
Item {
    id: icon
    property string color
    implicitWidth: 32
    implicitHeight: 32
    readonly property real unit: width / 24

    IconImage {
        anchors.fill: parent
        visible: icon.color === ""
        source: "qrc:/assets/folder.svg"
        sourceSize: Qt.size(icon.width * Screen.devicePixelRatio, icon.height * Screen.devicePixelRatio)
        color: Theme.darkText
    }
    Item {
        anchors.fill: parent
        visible: icon.color !== ""
        Rectangle {  // the tab and the back
            x: 2.5 * icon.unit; y: 4 * icon.unit
            width: 8 * icon.unit; height: 4 * icon.unit
            radius: 1.2 * icon.unit
            color: Qt.darker(icon.color, 1.25)
        }
        Rectangle {
            x: 2.5 * icon.unit; y: 6 * icon.unit
            width: 19 * icon.unit; height: 13 * icon.unit
            radius: 2 * icon.unit
            color: Qt.darker(icon.color, 1.25)
        }
        Rectangle {  // the front
            x: 2.5 * icon.unit; y: 9 * icon.unit
            width: 19 * icon.unit; height: 10 * icon.unit
            radius: 2 * icon.unit
            color: icon.color
        }
    }
}
