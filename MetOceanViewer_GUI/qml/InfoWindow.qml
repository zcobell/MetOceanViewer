/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2018  Zach Cobell
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------*/
import QtQuick 2.0

Rectangle {
    property int mode: 0
    id: infoWindow
    color: "#ffffff"
    border.color: "#000000"
    border.width: 3
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    width: infoWindowText.width+20
    state: "hidden"

    states: [
        State {
            name: "shown"
            PropertyChanges { target: infoWindow; height:65 }
        }, State {
            name: "hidden"
            PropertyChanges { target: infoWindow; height:0 }
        }
    ]

    transitions: [
        Transition { NumberAnimation { target: infoWindow; property: "height" ; easing.type: Easing.InOutBack; duration: 400}},
        Transition { NumberAnimation { target: infoWindow; property: "width" ; easing.type: Easing.InOutBack; duration: 400}}
    ]

    TextEdit {
        id: infoWindowText
        anchors.bottom: parent.bottom
        anchors.top: parent.top
        anchors.right: parent.right
        readOnly: true
        textFormat: Text.RichText
        parent: infoWindow
        text: stationText
        anchors.margins: 10
        font.family: "Arial"
        font.pointSize: 10
        selectByMouse: true
        mouseSelectionMode: TextEdit.SelectWords
        NumberAnimation { target: infoWindowText; property: "height"; easing.type: Easing.InOutElastic; duration: 400 }
        Behavior on width { NumberAnimation { easing.type: Easing.InOutBack; duration: 400} }

    }
}
