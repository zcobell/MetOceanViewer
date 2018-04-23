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
    property string markerPurple: "qrc:/rsc/img/mm_20_purple.png"
    property var markerColors: [ "qrc:/rsc/img/mm_20_darkblue.png",
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
            if(markerCategory<1)
                image.source = markerPurple
            else if(markerCategory>6)
                image.source = markerColors[6]
            else
                image.source = markerColors[markerCategory-2];
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
