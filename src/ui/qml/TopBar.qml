// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Placeholder top bar, floating over the map. A Pane, so clicks on it do not
// reach the map underneath.
Pane {
    id: bar

    signal aboutRequested

    padding: Theme.spacing / 2
    leftPadding: Theme.spacing * 2
    background: Rectangle {
        color: Theme.surface
        border.color: Theme.border
        radius: Theme.radius
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        Label {
            text: Application.displayName
            font.bold: true
        }

        Item {
            Layout.fillWidth: true
        }

        ToolButton {
            text: qsTr("About")
            onClicked: bar.aboutRequested()
        }
    }
}
