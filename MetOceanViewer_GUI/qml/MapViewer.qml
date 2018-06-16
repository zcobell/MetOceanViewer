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

    function clearMarkers() {
        map.clearMapItems();
        return;
    }

    function getVisibleRegion() {
        return map.visibleRegion;
    }

    function setVisibleRegion(x1,y1,x2,y2) {
        var xmin = Math.min(x1,x2);
        var xmax = Math.max(x1,x2);
        var ymin = Math.min(y1,y2);
        var ymax = Math.max(y1,y2);
        map.visibleRegion = QtPositioning.rectangle(QtPositioning.coordinate(ymax,xmin),
                                                    QtPositioning.coordinate(ymin,xmax));
        return;
    }

    function setMapLocation(x,y,zoom) {
        map.center = QtPositioning.coordinate(y,x);
        map.zoomLevel = zoom;
    }

    function selectedMarkers() {
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

    function numSelectedMarkers() {
        var nSelected = 0;
        for(var i=0;i<map.children.length;i++){
            if(map.children[i].selected===true){
                nSelected = nSelected + 1;
            }
        }
        return nSelected;
    }

    function showLegend(c0,c1,c2,c3,c4,c5,c6,units) {
        legend.c0=c0;
        legend.c1=c1;
        legend.c2=c2;
        legend.c3=c3;
        legend.c4=c4;
        legend.c5=c5;
        legend.c6=c6;
        legend.units=units;
        legend.visible = true;
        return;
    }

    function hideLegend() {
        legend.visible = false
        return;
    }

    function listMapTypes() {
        for(var i=0;i<map.supportedMapTypes.length;i++){
            console.log(map.supportedMapTypes[i].name);
        }
        return;
    }

    Map {
        id: map
        anchors.fill: parent
        activeMapType: supportedMapTypes[mapType]
        plugin: mapPlugin;
        //copyrightsVisible: false

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

                listMapTypes();

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
                } else if (markerMode===2) {
                    if(map.previousMarker){
                        map.previousMarker.deselect()
                        map.previousMarker = null
                    }
                    infoWindow.state = "hidden"
                }
            }
            onDoubleClicked: {
                if (mouse.button === Qt.RightButton) {
                    map.zoomLevel = map.zoomLevel-1;
                    setMapPlugin(1);
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
                stationId: id
                coordinate: position
                markerCategory: category

                function generateInfoWindowText(){
                    var text;
                    if(markerMode===0){
                        text =
                            "<b>Location: &nbsp;</b>"+longitude+", "+latitude+"<br>"+
                            "<b>Station: &nbsp;&nbsp;&nbsp; </b>"+id+"<br>"+
                            "<b>Name: &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</b>"+name
                    }else if (markerMode===1){
                        text =
                            "<b>Location: &nbsp;</b>"+longitude+", "+latitude+"<br>"+
                            "<b>Station: &nbsp;&nbsp;&nbsp; </b>"+id+"<br>"+
                            "<b>Name: &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</b>"+name
                    }else if(markerMode===2){
                        var diff;
                        var modeledText;
                        if(modeled<-900){
                            modeledText = "Dry";
                            diff = "n/a";
                        } else {
                            modeledText = modeled.toFixed(2);
                            diff = (Math.round((-difference)*100)/100).toFixed(2);
                        }

                        text = "<b>Location: &nbsp;</b>"+longitude+", "+latitude+"<br>"+
                               "<b>Observed:</b>&nbsp;&nbsp; "+measured.toFixed(2)+"<br>"+
                               "<b>Modeled:</b> &nbsp;&nbsp;&nbsp;&nbsp;"+modeledText+"<br>"+
                               "<b>Difference:</b> "+diff
                    }
                    return text;
                }

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
                            stationText = parent.generateInfoWindowText()
                            infoWindow.state = "shown"
                        }
                    }

                    function multipleMarkerSelection(){
                        markerid.select()
                        selectedMarkers()

                        if(numSelectedMarkers()===1){
                            infoWindow.state = "shown"
                            stationText = parent.generateInfoWindowText()
                        } else {
                            infoWindow.state = "hidden"
                        }
                    }

                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if(markerMode===0 || markerMode===2) {
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

    MapLegend {
        id: legend
        visible: false
    }

}

