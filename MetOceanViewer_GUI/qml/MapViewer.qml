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
                mode: markerMode
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

    InfoWindow {
        id: infoWindow
        mode: markerMode
    }
}
