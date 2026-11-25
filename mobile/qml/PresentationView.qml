import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-120 stage A: the presentation, full screen over the app (the desktop's F5). On Android the
// system bars hide while it runs (the window's full screen), the screen stays on and the phone is held
// in landscape (PitchPresentation; user's decision 2026-11-14).
// Forward: a tap on the right two thirds or a swipe left; back: a tap on the left third or a swipe
// right (user's decision 2026-11-14); on a hardware keyboard the desktop's keys (arrows, Page Up/Down,
// Space, Enter, Backspace, Home, End). Android's Back ends it and the map selects the topic of the last
// slide shown, as the desktop's Esc.
Popup {
    id: view
    objectName: "presentationView"
    parent: Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    padding: 0
    modal: true
    focus: true  // Back (Escape) reaches a popup with the focus
    dim: false
    // Back (Escape) ends it; nothing else does.
    closePolicy: Popup.CloseOnEscape
    property int windowVisibility: Window.AutomaticVisibility
    // A move this long (dp) is a swipe; shorter, a tap.
    readonly property int swipeDistance: Theme.touchTarget

    // Starts the active sheet's presentation; false (the map says why) without slides.
    function openFor(pitch) {
        if (!slides.start(pitch)) return false
        view.open()
        return true
    }

    onAboutToShow: {
        const window = slides.Window.window
        if (Qt.platform.os === "android" && window) {
            view.windowVisibility = window.visibility
            window.visibility = Window.FullScreen
        }
    }
    onClosed: {
        const window = slides.Window.window
        if (Qt.platform.os === "android" && window) window.visibility = view.windowVisibility
        slides.finish()
    }

    background: Item {}
    contentItem: PitchPresentation {
        id: slides
        objectName: "pitchPresentation"
        focus: true  // the keyboard's keys step the slides
        MouseArea {
            anchors.fill: parent
            property point pressedAt
            onPressed: mouse => pressedAt = Qt.point(mouse.x, mouse.y)
            onReleased: mouse => {
                const dx = mouse.x - pressedAt.x
                const dy = mouse.y - pressedAt.y
                if (Math.abs(dx) >= view.swipeDistance && Math.abs(dx) > Math.abs(dy)) {
                    if (dx < 0) slides.next()
                    else slides.previous()
                } else if (Math.abs(dx) < view.swipeDistance && Math.abs(dy) < view.swipeDistance) {
                    if (pressedAt.x >= width / 3) slides.next()
                    else slides.previous()
                }
            }
        }
    }
}
