import QtQuick 2.5
import QtLocation 5.9

MapQuickItem {
    property bool selected: false
    property int mode: 0
    property string defaultImage: "qrc:/rsc/img/mm_20_darkgreen.png"
    property string selectedImage: "qrc:/rsc/img/mm_20_red.png"
    property var markerColors: [ "qrc:/rsc/img/mm_20_darkblue.png",
                                    "qrc:/rsc/img/mm_20_lightblue.png",
                                    "qrc:/rsc/img/mm_20_darkgreen.png",
                                    "qrc:/rsc/img/mm_20_yellow.png",
                                    "qrc:/rsc/img/mm_20_darkorange.png",
                                    "qrc:/rsc/img/mm_20_red.png" ]
    property double color0;
    property double color1;
    property double color2;
    property double color3;
    property double color4;
    property double color5;
    property var categories: [ color0, color1, color2, color3, color4, color5 ]

    property Image sourceImage: Image {
        id: image
        source: defaultImage
    }

    sourceItem: image

    Component.onCompleted: {
        if(mode===1) {

        }
    }

    function deselect(){
        selected = false
        if(mode===0) image.source = defaultImage
    }

    function select(){
        selected = true
        if(mode===0) image.source = selectedImage
    }

}
