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
import QtQuick 2.11
import QtLocation 5.9
import QtGraphicalEffects 1.0

MapQuickItem {

    property bool selected: false
    property int mode: 0
    property bool stationActive: true
    property string stationId;
    property string sourceImage;
    property string selectedSourceImage;
    property string defaultImage: "qrc:/rsc/img/mm_20_darkgreen.png"
    property string defaultInactiveImage: "qrc:/rsc/img/mm_20_darkorange.png"
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
        if(mode===2) {
            if(modeled<-900){
                sourceImage = markerWhite
            } else {
                sourceImage = markerColors[markerCategory];
            }
            selectedSourceImage = sourceImage
        } else if(mode===3){
            if(stationActive){
                sourceImage = defaultImage
            } else {
                sourceImage = defaultInactiveImage
            }
            selectedSourceImage = selectedImage
        } else {
            sourceImage = defaultImage
            selectedSourceImage = selectedImage
        }
        image.source = sourceImage;
    }

    function deselect(){
        selected = false
        glowing.visible = false
        image.source = sourceImage
    }

    function select(){
        selected = true
        glowing.visible = true
        image.source = selectedSourceImage
    }

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

    Component.onCompleted: selectMarkerImage()

}
