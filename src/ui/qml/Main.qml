// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

import QtQuick
import QtQuick.Controls.Basic
import QtLocation
import QtPositioning

// The main window: a full-bleed map with floating controls above it.
ApplicationWindow {
    id: window

    width: 1280
    height: 800
    visible: true
    title: Application.displayName
    color: Theme.background

    palette.window: Theme.surface
    palette.windowText: Theme.text
    palette.base: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.surface
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.mid: Theme.border
    palette.placeholderText: Theme.textMuted

    MapView {
        id: mapView

        objectName: "mapView" // found by the GUI smoke test
        anchors.fill: parent

        map.plugin: Plugin {
            name: "maplibre"

            // OpenFreeMap "liberty": keyless OpenStreetMap vector tiles
            // (plan §6.3). Loaded asynchronously; offline, the map stays empty.
            PluginParameter {
                name: "maplibre.map.styles"
                value: "https://tiles.openfreemap.org/styles/liberty"
            }
        }
        // The US Gulf and Atlantic coasts.
        map.center: QtPositioning.coordinate(34.0, -83.5)
        map.zoomLevel: 5.1
    }

    TopBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacing

        onAboutRequested: aboutDialog.open()
    }

    // The basemap's data licenses require this attribution.
    Label {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacing / 2
        padding: Theme.spacing / 2
        text: "© OpenFreeMap © OpenMapTiles © OpenStreetMap contributors"
        color: Theme.textMuted
        font.pixelSize: Theme.smallFontSize
        background: Rectangle {
            color: Theme.surface
            opacity: 0.8
            radius: Theme.radius / 2
        }
    }

    AboutDialog {
        id: aboutDialog
    }
}
