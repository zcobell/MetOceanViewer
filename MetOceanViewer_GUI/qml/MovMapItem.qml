import QtQuick 2.5
import QtLocation 5.9

MapQuickItem {
    property bool selected: false
    property string defaultImage: "qrc:/rsc/img/mm_20_darkgreen.png"
    property string selectedImage: "qrc:/rsc/img/mm_20_red.png"

    property Image sourceImage: Image {
        id: image
        source: defaultImage
    }

    sourceItem: image

    function deselect(){
        selected = false
        image.source = defaultImage
    }

    function select(){
        selected = true
        image.source = selectedImage
    }

}
