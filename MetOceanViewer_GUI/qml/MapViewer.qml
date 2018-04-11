import QtQuick.Window 2.2
import QtQuick 2.7
import QtQuick.Controls 1.4
import QtLocation 5.9
import QtPositioning 5.5

Rectangle {

    id: window

    signal markerChanged(string msg)

    property string stationText;

    Plugin {
       id: esriPlugin
       name: "esri"
    }

    Map {
        id: map
        anchors.fill: parent
        activeMapType: supportedMapTypes[mapType]
        plugin: esriPlugin
        copyrightsVisible: false
        zoomLevel: 3
        center: QtPositioning.coordinate(40.0,-100.0)

        property MovMapItem previousMarker

        MapItemView{
            model: stationModel
            delegate: mapcomponent
        }

        MouseArea {
            anchors.fill: parent
            propagateComposedEvents: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: {
                if(map.previousMarker){
                    map.previousMarker.deselect()
                    map.previousMarker = null
                    window.markerChanged(-1)
                    infoWindow.state = "hidden"
                }
            }
            onDoubleClicked: {
                if (mouse.button === Qt.RightButton) {
                    map.zoomLevel = map.zoomLevel-1;
                } else if(mouse.button === Qt.LeftButton) {
                    map.zoomLevel = map.zoomLevel+1;
                }
            }

        }

        Component {
            id: mapcomponent
            MovMapItem {
                id: markerid
                anchorPoint.x: sourceImage.width/4
                anchorPoint.y: sourceImage.height
                coordinate: position

                MouseArea{
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if(markerid.selected){
                            markerid.deselect()
                            infoWindow.state = "hidden"
                        } else {
                            if(map.previousMarker){
                                map.previousMarker.deselect()
                            }

                            markerid.select()
                            map.previousMarker = markerid
                            window.markerChanged(id);

                            stationText =
                                "<b>Location: &nbsp;</b>"+longitude+", "+latitude+"<br>"+
                                "<b>Station: &nbsp;&nbsp;&nbsp; </b>"+id+"<br>"+
                                "<b>Name: &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</b>"+name

                            infoWindow.state = "shown"
                        }
                    }
                }
            }
        }

    }

    Rectangle {
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
}
