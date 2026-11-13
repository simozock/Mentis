import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-097: Home (maps of the Mentis folder, open from the system picker or from another app) and the map. The map's
// changes are written when the app leaves the screen, where Android may end it without notice.
ApplicationWindow {
    id: window
    width: 412
    height: 915
    visible: true
    title: "Mentis"
    color: Theme.darkSunken

    MapLibrary { id: library }
    MobileMap { id: mobileMap }

    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state !== Qt.ApplicationActive) mobileMap.save()
            else storageAccess.recheck()  // the access may have been given in the system settings
        }
    }

    // FEAT-105 decision 1: the Mentis folder at the root of the internal storage needs the access
    // to all files; asked on the Home until it is given or the user answers "Not now". Once given,
    // the maps kept so far in the app's own folder move there.
    StorageAccess {
        id: storageAccess
        onGranted: { library.reload(); home.showError(library.lastError) }
    }
    Component.onCompleted: if (storageAccess.needed && !storageAccess.declined) folderAccess.open()

    function openMap(url) {
        if (mobileMap.openUrl(url, library)) stack.push(mapPage)
        else home.showError(mobileMap.lastError)
    }

    // FEAT-105: a map given by another app ("Open with", "Share") is copied into the Mentis folder
    // and opened; the open map is saved and closed first. A failed save keeps it open and asks.
    function openIncoming(url) {
        if (stack.depth > 1) {
            if (!mobileMap.close()) { saveFailed.open(); return }
            backHome()
        }
        openMap(url)
    }

    IncomingMaps {
        onReceived: url => window.openIncoming(url)
    }

    StackView {
        id: stack
        objectName: "stack"
        anchors.fill: parent
        initialItem: HomePage {
            id: home
            library: library
            onOpenRequested: url => window.openMap(url)
        }
    }

    Component {
        id: mapPage
        MapPage {
            objectName: "mapPage"
            map: mobileMap
            // A failed save keeps the map open until the user chooses to close it.
            onBackRequested: {
                if (mobileMap.close()) window.backHome()
                else saveFailed.open()
            }
        }
    }

    function backHome() { stack.pop(); library.refresh() }

    ModalDialog {
        id: folderAccess
        objectName: "folderAccessDialog"
        title: qsTr("Mentis folder")
        anchors.centerIn: parent
        width: Math.min(window.width - 8 * Theme.grid, 400)
        Label {
            width: folderAccess.availableWidth
            text: qsTr("Mentis keeps your maps in the Mentis folder of the internal storage, where every file manager finds them. To create it, Android asks you to allow access to all files.")
            wrapMode: Text.Wrap
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        buttons: [
            DarkButton {
                objectName: "folderAccessLaterButton"
                text: qsTr("Not now")
                onClicked: { storageAccess.decline(); folderAccess.close() }
            },
            DarkButton {
                objectName: "folderAccessAllowButton"
                text: qsTr("Allow")
                primary: true
                onClicked: { folderAccess.close(); storageAccess.request() }
            }
        ]
    }

    // As the desktop's closing (MainWindow::closeEvent): a failed save asks before closing.
    ModalDialog {
        id: saveFailed
        objectName: "saveFailedDialog"
        title: qsTr("Save failed")
        anchors.centerIn: parent
        width: Math.min(window.width - 8 * Theme.grid, 400)
        onAccepted: { mobileMap.closeWithoutSaving(); window.backHome() }
        Label {
            width: saveFailed.availableWidth  // not parent.width: a binding loop on the dialog's implicit height
            text: qsTr("The latest changes have not been saved. Close anyway?") + "\n\n" + mobileMap.lastError
            wrapMode: Text.Wrap
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        buttons: [
            DarkButton {
                text: qsTr("Cancel")
                onClicked: saveFailed.reject()
            },
            DarkButton {
                objectName: "closeAnywayButton"
                text: qsTr("Close")
                primary: true
                onClicked: saveFailed.accept()
            }
        ]
    }
}
