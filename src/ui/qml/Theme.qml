// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

pragma Singleton

import QtQuick

// Design tokens of the light and dark themes. Colors and sizes come from here
// (Main.qml maps the colors onto the Qt Quick Controls palette); components do
// not hard-code them.
QtObject {
    // Follows the system color scheme; a user override comes with Settings.
    readonly property bool dark: Application.styleHints.colorScheme === Qt.ColorScheme.Dark

    readonly property color background: dark ? "#16181d" : "#f4f5f7"
    readonly property color surface: dark ? "#23262d" : "#ffffff"
    readonly property color border: dark ? "#3a3f4a" : "#d9dce1"
    readonly property color text: dark ? "#e6e8eb" : "#1d2125"
    readonly property color textMuted: dark ? "#9aa1ab" : "#5e6570"
    readonly property color accent: dark ? "#5ea8ff" : "#0b62d6"

    readonly property int spacing: 8
    readonly property int radius: 8
    readonly property int smallFontSize: 11
}
