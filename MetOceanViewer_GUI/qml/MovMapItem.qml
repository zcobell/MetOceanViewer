import QtQuick 2.5
import QtLocation 5.9

MapQuickItem {
    property bool selected: false
    property int mode: 0
    property string stationId;
    property string defaultImage: "qrc:/rsc/img/mm_20_darkgreen.png"
    property string selectedImage: "qrc:/rsc/img/mm_20_red.png"
    property var markerColors: [ "qrc:/rsc/img/mm_20_darkblue.png",
                                    "qrc:/rsc/img/mm_20_lightblue.png",
                                    "qrc:/rsc/img/mm_20_darkgreen.png",
                                    "qrc:/rsc/img/mm_20_yellow.png",
                                    "qrc:/rsc/img/mm_20_darkorange.png",
                                    "qrc:/rsc/img/mm_20_red.png" ]
    property double markerClass0;
    property double markerClass1;
    property double markerClass2;
    property double markerClass3;
    property double markerClass4;
    property double markerClass5;
    property double markerClass6;
    property double diff;
    property var categories: [ markerClass0, markerClass1, markerClass2, markerClass3, markerClass4, markerClass5, markerClass6 ]

    function selectMarkerImage(){
        for(var i=0;i<7;i++){
            if(diff<categories[i]) {
                image.source = markerColors[i]
                break
            }
            if(i===6)
                image.source = markerColors[6]
        }
    }

    property Image sourceImage: Image {
        id: image
        source: defaultImage
    }

    sourceItem: image
    parent: map

    Component.onCompleted: {
        if(mode===2) {
            selectMarkerImage();
        }
    }

    function deselect(){
        selected = false
        if(mode!=2) image.source = defaultImage
    }

    function select(){
        selected = true
        if(mode!=2) image.source = selectedImage
    }

}
