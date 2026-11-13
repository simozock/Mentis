pragma Singleton
import QtQuick

// FEAT-097: the desktop's tokens (docs/design/ui-foundations.md, ui-components.md) read as dp
// (docs/design/ui-mobile.md). As on the desktop the map and the floating groups over it are light;
// Home, dialogs and lists are dark ("Interfaccia scura", FEAT-035).
QtObject {
    // Light: the map and its floating groups
    readonly property color surface: "#ffffff"        // editor surface, scene background
    readonly property color text: "#1f2937"           // main text
    readonly property color textSecondary: "#6b7280"
    readonly property color icon: "#27303b"
    readonly property color hoverFill: Qt.rgba(15 / 255, 23 / 255, 42 / 255, 15 / 255)
    readonly property color pressedFill: Qt.rgba(15 / 255, 23 / 255, 42 / 255, 28 / 255)
    readonly property color groupFill: Qt.rgba(1, 1, 1, 214 / 255)                       // FloatingGroup
    readonly property color groupBorder: Qt.rgba(15 / 255, 23 / 255, 42 / 255, 13 / 255)
    readonly property color shadow: "#0f172a"                                              // rgb(15,23,42)
    // Dark: Home, dialogs, lists (src/uitheme.*)
    readonly property color darkSurface: "#2b2f33"
    readonly property color darkSunken: "#1f2326"
    readonly property color darkRaised: "#3f4348"
    readonly property color darkDivider: "#383c40"
    readonly property color darkText: "#ffffff"
    readonly property color darkTextMuted: "#747a80"
    readonly property color scrim: Qt.rgba(15 / 255, 23 / 255, 42 / 255, 0.24)
    // Both
    readonly property color accent: "#7047eb"
    readonly property color accentDisabled: Qt.rgba(112 / 255, 71 / 255, 235 / 255, 0.4)
    readonly property color danger: "#e5484d"         // a field with a wrong value
    readonly property color textSelection: "#b3d4fc" // selected text in a title (NodeItem::SelectionStyle)
    readonly property color selection: "#2ebdff"     // selection ring and "+" knobs (NodeItem::SelectionStyle)
    // Type scale (px = dp)
    readonly property int captionSize: 12
    readonly property int bodySize: 14
    readonly property int largeSize: 18
    readonly property int subtitleSize: 20
    readonly property int titleSize: 28
    // 4 px grid and radii
    readonly property int grid: 4
    readonly property int controlRadius: 4
    readonly property int surfaceRadius: 8
    readonly property int groupRadius: 10             // approved exception (ui-foundations.md)
    readonly property int groupButtonRadius: 6        // 10 - 4 of padding
    // Controls
    readonly property int groupButtonSize: 32         // buttons and selects of the floating groups
    readonly property int groupGap: 2                 // between the visible controls of a group, as the desktop
    readonly property int iconSize: 20
    readonly property int smallIconSize: 16
    readonly property int knobSize: 18                // the topic's "+" knobs (TopicTools::kKnobSizePx)
    readonly property int buttonHeight: 32            // modal buttons
    readonly property int fieldHeight: 30             // modal fields
    // ui-mobile.md: every control the finger hits (in a floating group only 48 high)
    readonly property int touchTarget: 48
}
