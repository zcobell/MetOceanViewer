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
import QtQuick 2.5
import QtLocation 5.9
import QtGraphicalEffects 1.0

MapQuickItem {

    property bool selected: false
    property int mode: 0
    property string stationId;
    property string defaultImage: "qrc:/rsc/img/mm_20_darkgreen.png"
    property string selectedImage: "qrc:/rsc/img/mm_20_red.png"
    property string markerWhite: "qrc:/rsc/img/mm_20_white.png"
    property var markerColors: [ "qrc:/rsc/img/mm_20_purple.png",
                                 "qrc:/rsc/img/mm_20_darkblue.png",
                                 "qrc:/rsc/img/mm_20_lightblue.png",
                                 "qrc:/rsc/img/mm_20_darkgreen.png",
                                 "qrc:/rsc/img/mm_20_lightgreen.png",
                                 "qrc:/rsc/img/mm_20_yellow.png",
                                 "qrc:/rsc/img/mm_20_darkorange.png",
                                 "qrc:/rsc/img/mm_20_red.png" ]
    property int markerCategory: 0;

    function selectMarkerImage(){
        if(modeled<-900){
            image.source = markerWhite
        } else {
            image.source = markerColors[markerCategory];
        }
    }

    function deselect(){
        selected = false
        glowing.visible = false
        if(mode!=2) image.source = defaultImage
    }

    function select(){
        selected = true
        glowing.visible = true
        if(mode!=2) image.source = selectedImage
    }

    parent: map

    sourceItem: Rectangle{
        id: imageRectangle
        Image {
            id: image
            source: defaultImage
            smooth: false
            visible: true
        }
        width: image.width
        height: image.height
        border.width: 0
        color: "transparent"

        Glow {
            id: glowing
            anchors.fill: image
            radius: 8
            samples: 17
            color: "red"
            source: image
            visible: false
        }
    }

    anchorPoint.x: imageRectangle.width/4
    anchorPoint.y: imageRectangle.height

    Component.onCompleted: {
        if(mode===2) {
            selectMarkerImage();
        }
    }

}
