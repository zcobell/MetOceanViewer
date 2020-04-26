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
import QtQuick.Window 2.12
import QtQuick 2.12
import QtQuick.Controls 1.4
import QtLocation 5.12
import QtPositioning 5.12
import QtQuick.Layouts 1.12

Rectangle {

    id: window
    objectName: "mapWindow"
    width: 800
    height: 600

    signal markerChanged(string msg)
    signal mapViewChanged()

    property string stationText;

    function getVisibleRegion(){
        return map.visibleRegion;
    }

    function getVisibleCenter() {
        return map.visibleRegion.center();
    }

    function getCurrentZoomLevel() {
        return map.zoomLevel;
    }

    function getMapTypes() {
        var list;
        for(var i=0;i<map.supportedMapTypes.length;i++){
            if(i===0)
                list = "\""+map.supportedMapTypes[i].name+"\""
            else
                list=list+" << \""+map.supportedMapTypes[i].name+"\"";
        }
        console.log(list)
        return list;
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
        for(var child in mapItemView.children){
            if(mapItemView.children[child].objectName==="marker"){
                if(mapItemView.children[child].selected===true){
                    nSelected = nSelected + 1;
                    if(nSelected===1)
                        selectedList = mapItemView.children[child].stationId;
                    else
                        selectedList = selectedList + "," + mapItemView.children[child].stationId;
                }
            }
        }
        markerChanged(selectedList);
        return nSelected;
    }

    function deselectMarkers() {
        for(var child in mapItemView.children){
            if(mapItemView.children[child].objectName==="marker"){
                if(mapItemView.children[child].selected===true){
                    mapItemView.children[child].deselect();
                }
            }
        }
        infoWindow.state = "hidden"
    }

    function numSelectedMarkers() {
        var nSelected = 0;
        for(var child in mapItemView.children){
            if(mapItemView.children[child].objectName==="marker"){
                if(mapItemView.children[child].selected===true){
                    nSelected = nSelected + 1;
                }
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

    function zoomIn() {
        map.zoomLevel = map.zoomLevel + 1;
    }

    function zoomOut() {
        map.zoomLevel = map.zoomLevel - 1;
    }

    function zoomClick(button) {
        if(button === Qt.LeftButton) {
            zoomIn();
        } else if(button === Qt.RightButton) {
            zoomOut();
        }
    }

    function mapWindowChange(){
        mapViewChanged()
    }

    Map {
        id: map
        anchors.fill: parent
        activeMapType: supportedMapTypes[mapType]
        plugin: mapPlugin;
        copyrightsVisible: true
        onZoomLevelChanged: mapWindowChange()
        onCenterChanged: mapWindowChange()

        property MovMapItem previousMarker

        MouseArea{
            acceptedButtons: Qt.AllButtons
            anchors.fill: parent
            onClicked: deselectMarkers()
            onDoubleClicked: zoomClick(mouse.button)
        }

        MapItemView{
            id: mapItemView
            model: stationModel
            objectName: "mapItemView"
            delegate: mapcomponent
        }

        Component {
            id: mapcomponent
            MovMapItem {
                objectName: "marker"
                mode: markerMode
                stationActive: active
                id: markerid
                stationId: id
                coordinate: position
                markerCategory: category
                parent: mapItemView

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

                        text = "<b>Location: &nbsp;&nbsp;</b>"+longitude+", "+latitude+"<br>"+
                                "<b>Observed:</b>&nbsp;&nbsp; "+measured.toFixed(2)+"<br>"+
                                "<b>Modeled:</b> &nbsp;&nbsp;&nbsp;"+modeledText+"<br>"+
                                "<b>Difference:</b> &nbsp;"+diff
                        infoWindow.shownHeight = 78;
                    } else if(markerMode===3){
                        var endDateString;
                        if(endDate=="01/01/2050")
                            endDateString = "present";
                        else
                            endDateString = endDate;
                        text =
                                "<b>Location: &nbsp;</b>"+longitude+", "+latitude+"<br>"+
                                "<b>Station: &nbsp;&nbsp;&nbsp; </b>"+id+"<br>"+
                                "<b>Name: &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</b>"+name+"<br>"+
                                "<b>Available: </b> "+startDate+" - "+endDateString;
                        infoWindow.shownHeight = 78;
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
                                infoWindow.state = "hidden"
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
                        if(markerMode===0 || markerMode===2 || markerMode===3) {
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

