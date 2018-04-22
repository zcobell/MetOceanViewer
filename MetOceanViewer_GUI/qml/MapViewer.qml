import QtQuick.Window 2.2
import QtQuick 2.7
import QtQuick.Controls 1.4
import QtLocation 5.9
import QtPositioning 5.5

Rectangle {

    id: window
    objectName: "mapWindow"

    signal markerChanged(string msg)

    property string stationText;

    function setVisibleRegion(x1,y1,x2,y2){
        var xmin = Math.min(x1,x2);
        var xmax = Math.max(x1,x2);
        var ymin = Math.min(y1,y2);
        var ymax = Math.max(y1,y2);
        map.visibleRegion = QtPositioning.rectangle(QtPositioning.coordinate(ymax,xmin),QtPositioning.coordinate(ymin,xmax));
        return;
    }

    function setViewport(x,y){
        map.center = QtPositioning.coordinate(y,x);
        map.zoomLevel = 5;
        return;
    }

    function setMapLocation(x,y,zoom){
        map.center = QtPositioning.coordinate(y,x);
        map.zoomLevel = zoom;
    }

    function selectedMarkers(){
        var selectedList = "";
        var nSelected = 0;
        for(var i=0;i<map.children.length;i++){
            if(map.children[i].selected===true){
                nSelected = nSelected + 1;
                if(nSelected==1)
                    selectedList = map.children[i].stationId;
                else
                    selectedList = selectedList + "," + map.children[i].stationId;
            }
        }
        markerChanged(selectedList);
        return nSelected;
    }

    Plugin {
       id: esriPlugin
       name: "esri"
    }

    Map {
        id: map
        anchors.fill: parent
        activeMapType: supportedMapTypes[mapType]
        plugin: esriPlugin
        // copyrightsVisible: false

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
                if(markerMode===0){
                    if(map.previousMarker){
                        map.previousMarker.deselect()
                        map.previousMarker = null
                        window.markerChanged(-1)
                        infoWindow.state = "hidden"
                    }
                } else if (markerMode===1) {
                    for(var i=0;i<map.children.length;i++){
                        if(map.children[i].selected===true){
                            map.children[i].deselect();
                        }
                    }
                    selectedMarkers();
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
                markerClass0: hwmClass0
                markerClass1: hwmClass1
                markerClass2: hwmClass2
                markerClass3: hwmClass3
                markerClass4: hwmClass4
                markerClass5: hwmClass5
                markerClass6: hwmClass6
                diff: measured - modeled

                id: markerid
                stationId: id
                anchorPoint.x: sourceImage.width/4
                anchorPoint.y: sourceImage.height
                coordinate: position

                MouseArea{

                    function singleMarkerSelection(){
                        if(markerid.selected){
                            markerid.deselect()
                            infoWindow.state = "hidden"
                        } else {
                            if(map.previousMarker){
                                map.previousMarker.deselect()
                            }

                            markerid.select()
                            selectedMarkers();
                            map.previousMarker = markerid
                            window.markerChanged(id);

                            stationText =
                                "<b>Location: &nbsp;</b>"+longitude+", "+latitude+"<br>"+
                                "<b>Station: &nbsp;&nbsp;&nbsp; </b>"+id+"<br>"+
                                "<b>Name: &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</b>"+name

                            infoWindow.state = "shown"
                        }
                    }

                    function multipleMarkerSelection(){
                        markerid.select()
                        selectedMarkers()
                        infoWindow.state = "hidden"
                    }

                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if(markerMode===0) {
                            singleMarkerSelection();
                        } else if(markerMode===1) {
                            if(markerid.selected){
                                markerid.deselect()
                            } else {
                                if ((mouse.button === Qt.LeftButton) && (mouse.modifiers & Qt.ControlModifier)) {
                                    multipleMarkerSelection();
                                } else {
                                    singleMarkerSelection();
                                }
                            }
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

