//-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2015  Zach Cobell
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
// The name "MetOcean Viewer" is specific to this project and may not be
// used for projects "forked" or derived from this work.
//
//-----------------------------------------------------------------------//
var TimeseriesMarkers = [];
var Locations = [];
var StationName = [];
var map;
var LastInfoWindow;
var LastMarker = -1;
var selectedMarkers = [];
var selecting = false;
var showInfoWindowDuringMulti = false;

// check if an element exists in array using a comparer function
// comparer : function(currentElement)
Array.prototype.inArray = function(comparer) {
    for(var i=0; i < this.length; i++) {
        if(comparer(this[i])) return true;
    }
    return false;
};

// adds an element to the array if it does not already exist using a comparer
// function
Array.prototype.pushIfNotExist = function(element, comparer) {
    if (!this.inArray(comparer)) {
        this.push(element);
    }
};

function toggleInfoWindows(state)
{
    if(state==="true")
        showInfoWindowDuringMulti = true;
    else
        showInfoWindowDuringMulti = false;
    return;
}

//Functions for the new version of the timeseries map
window.onresize = function()
{
    $('#plot_area').highcharts().setSize($(window).width()/2,$(window).height(), false);
}

//Build the data series
function allocateData(NumSeries)
{
    Locations[0] = [];
    Locations[1] = [];
    return;
}

//Set the locations array
function SetMarkerLocations(index,x,y,name)
{
    Locations[0][index] = x;
    Locations[1][index] = y;
    StationName[index]  = name;
    return;
}


function AddToMap()
{
    var i,x,y,NLocations,Latlng,contentString;

    NLocations = Locations[1].length;

    for(i=0;i<NLocations;i++)
    {
        x = Locations[0][i];
        y = Locations[1][i];
        Latlng = new google.maps.LatLng(y,x);
        TimeseriesMarkers[i] = new google.maps.Marker({
            position: Latlng,
            map: map,
            title: StationName[i],
            LocalID: i,
        });
        TimeseriesMarkers[i].setIcon('http://maps.google.com/mapfiles/ms/icons/red-dot.png');
        google.maps.event.addListener(TimeseriesMarkers[i], 'click', function(event) {
            window.MarkerID = this.LocalID;

            if(selecting===true)
            {
                //...Add to the selection list
                selectedMarkers.pushIfNotExist(this,function(e) {
                    return e.LocalID === this.LocalID; });
                this.setIcon('http://maps.google.com/mapfiles/ms/icons/green-dot.png');
            }
            else
            {
                //...Clear the icons in all previously selected markers
                for(i=0;i<selectedMarkers.length;i++)
                    selectedMarkers[i].setIcon('http://maps.google.com/mapfiles/ms/icons/red-dot.png');
                selectedMarkers = [];
                this.setIcon('http://maps.google.com/mapfiles/ms/icons/green-dot.png');
                selectedMarkers.push(this);
            }

            if(LastMarker!==-1)
                LastInfoWindow.close();
            LastMarker = this.LocalID;
            var xLocal = Locations[0][this.LocalID];
            var yLocal = Locations[1][this.LocalID];
            contentString = "<table>"+
                            "<tr>"+
                                "<td align=\"right\"> <b>Station Name:</b> </td>"+
                                "<td> "+StationName[this.LocalID]+
                                "</td>"+
                            "</tr>"+
                            "<tr>"+
                                "<td align=\"right\"> <b>Location:</b> </td>"+
                                "<td> "+xLocal+"&deg, "+yLocal+"&deg"+
                            "</tr>"+
                            "</table>";

            var InfoWindow = new google.maps.InfoWindow({content: contentString});

            if(selecting===false || showInfoWindowDuringMulti === true)
            {
                InfoWindow.open(map,this);
                LastInfoWindow = InfoWindow;
            }

        });
    }
    return;
}

//...Return Just the last marker
function getMarker()
{
    return LastMarker;
}

//...Return all selected markers
function getMarkers()
{
    var nMarkers = selectedMarkers.length;
    var returnString = "";
    returnString = String(nMarkers);
    for(var i = 0;i<selectedMarkers.length;i++)
        returnString = returnString+","+String(selectedMarkers[i].LocalID);

    return returnString;
}

//Old map Functions
function clearMarkers()
{
    while(TimeseriesMarkers[0])
    {
        TimeseriesMarkers.pop().setMap(null);
    }
}

function fitMarkers()
{
    var extent = new google.maps.LatLngBounds();
    var nMarker = TimeseriesMarkers.length;
    for(i=0;i<nMarker;i++)
    {
        extent.extend(TimeseriesMarkers[i].getPosition());
    }
    map.fitBounds(extent);
}

function initializeTimeseries() {

    //Initialize the map
    window.LastInfo = -1;

    var myOptions = {
      center: new google.maps.LatLng(29.5, -91.5),
      zoom: 8,
      mapTypeId: google.maps.MapTypeId.ROADMAP,
      panControl: true,
      streetViewControl: false,
      panControlOptions: { position: google.maps.ControlPosition.LEFT_TOP },
      zoomControlOptions: { position: google.maps.ControlPosition.LEFT_TOP }
    };
    map = new google.maps.Map(document.getElementById("map_canvas"), myOptions);
}

google.maps.event.addDomListener(window, "resize", function() {
    var center = map.getCenter();
    google.maps.event.trigger(map, "resize");
    map.setCenter(center);
});

window.onkeydown = function(e) {
  selecting = ((e.keyIdentifier === 'Control') || (e.ctrlKey === true));
}
window.onkeyup = function(e) {
  selecting = false;
}
