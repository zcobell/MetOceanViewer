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
var HWMMarkers = [];
var HWMInfoWindow = [];
var map;
var classbreaks;

function clearMarkers()
{
    while(HWMMarkers[0])
    {
        HWMMarkers.pop().setMap(null);
    }
}

function addLegend(unitString,classes)
{

    classbreaks = classes.split(":");
    var iconBase = "qrc:/rsc/img/";
    var icons = {
        c1: { name: "< "+classbreaks[0], icon: iconBase + 'mm_20_purple.png' },
        c2: { name: classbreaks[0]+" - "+classbreaks[1], icon: iconBase + 'mm_20_darkblue.png' },
        c3: { name: classbreaks[1]+" - "+classbreaks[2], icon: iconBase + 'mm_20_lightblue.png' },
        c4: { name: classbreaks[2]+" - "+classbreaks[3], icon: iconBase + 'mm_20_darkgreen.png' },
        c5: { name: classbreaks[3]+" - "+classbreaks[4], icon: iconBase + 'mm_20_lightgreen.png' },
        c6: { name: classbreaks[4]+" - "+classbreaks[5], icon: iconBase + 'mm_20_yellow.png' },
        c7: { name: classbreaks[5]+" - "+classbreaks[6], icon: iconBase + 'mm_20_darkorange.png' },
        c8: { name: "> "+classbreaks[6], icon: iconBase + 'mm_20_red.png' }
    };

    var legend = document.getElementById('legend');
    legend.innerHTML = "";
    legend.innerHTML = "<h3>Legend ("+unitString+")</h3>"
    var lHead = document.createElement('div');
    lHead.innerHTML = "Modeled less Observed";
    legend.appendChild(lHead);
    for (var key in icons) {
      var type = icons[key];
      var name = type.name;
      var icon = type.icon;
      var div = document.createElement('div');
      div.innerHTML = '<img src="' + icon + '"> ' + name;
      legend.appendChild(div);
    }
    div = document.createElement('div');
    div.innerHTML = '<img src="qrc:/rsc/img/mm_20_white.png"> Dry in model';
    legend.appendChild(div);

    map.controls[google.maps.ControlPosition.RIGHT_BOTTOM].clear();
    map.controls[google.maps.ControlPosition.RIGHT_BOTTOM].push(legend);

}

function fitMarkers()
{
    var extent = new google.maps.LatLngBounds();
    var nMarker = HWMMarkers.length;
    for(i=0;i<nMarker;i++)
    {
        extent.extend(HWMMarkers[i].getPosition());
    }
    map.fitBounds(extent);
}

function addHWM(x,y,ID,modeled,measured,diff,color,unit)
{
    var myLatlng = new google.maps.LatLng(y,x);

    //Set the marker image
    var image;
    if (color == -1)
        image = "qrc:/rsc/img/mm_20_white.png";
    else if(color==1)
        image = "qrc:/rsc/img/mm_20_purple.png";
    else if(color==2)
        image = "qrc:/rsc/img/mm_20_darkblue.png";
    else if(color==3)
        image = "qrc:/rsc/img/mm_20_lightblue.png";
    else if(color==4)
        image = "qrc:/rsc/img/mm_20_darkgreen.png";
    else if(color==5)
        image = "qrc:/rsc/img/mm_20_lightgreen.png";
    else if(color==6)
        image = "qrc:/rsc/img/mm_20_yellow.png";
    else if(color==7)
        image = "qrc:/rsc/img/mm_20_darkorange.png";
    else if(color==8)
        image = "qrc:/rsc/img/mm_20_red.png";

    HWMMarkers[ID] = new google.maps.Marker({
      position: myLatlng,
      map: map,
      title: String(ID),
      icon: image
    });

    if(Number(modeled)<-9999)
    {
        modeled = "Dry";
        diff = "N/A";
        var contentString =
            "<table>"+
                "<tr>"+
                    "<td align=\"right\"> <b>Location:</b> </td>"+
                    "<td> "+x+", "+y+
                "</tr>"+
                "<tr>"+
                    "<td align=\"right\"> <b>Measured High Water Mark:</b> </td>"+
                    "<td> "+measured+" "+unit+"</td>"+
                "<tr>"+
                    "<td align=\"right\"> <b>Modeled High Water Mark:</b> </td>"+
                    "<td> "+modeled+"</td>"+
                "</tr>"+
                    "<td align=\"right\"> <b>Difference:</b> </td>"+
                    "<td> "+diff+"</td>"+
                "</tr>"+
            "</table>";
    }
    else
    {
        var contentString =
            "<table>"+
                "<tr>"+
                    "<td align=\"right\"> <b>Location:</b> </td>"+
                    "<td> "+x+"&deg, "+y+"&deg"+
                "</tr>"+
                "<tr>"+
                    "<td align=\"right\"> <b>Measured High Water Mark:</b> </td>"+
                    "<td> "+measured+" "+unit+"</td>"+
                "</tr>"+
                "<tr>"+
                    "<td align=\"right\"> <b>Modeled High Water Mark:</b> </td>"+
                    "<td> "+modeled+" "+unit+"</td>"+
                "</tr>"+
                "<tr>"+
                    "<td align=\"right\"> <b>Difference:</b> </td>"+
                    "<td> "+diff+" "+unit+"</td>"+
                "</tr>"+
            "</table>";
    }
    HWMInfoWindow[ID] = new google.maps.InfoWindow({content: contentString});
    google.maps.event.addListener(HWMMarkers[ID], 'click', function() {
    //Set the Marker ID
    window.markerID = ID;
    if(window.LastInfo!=-1)
    {
        window.LastInfo.close();
    }
    window.LastInfo = HWMInfoWindow[ID];
    HWMInfoWindow[ID].open(map,HWMMarkers[ID]);
    });
}


////////////////////////////////////////////////////////////
//   PAN TO FUNCTION                                      //
//                                                        //
//  Function to pan to a different x,y,zoom on the map    //
////////////////////////////////////////////////////////////
function panToData(x,y,z){
    var center = new google.maps.LatLng(y,x);
    map.setCenter(center);
    map.setZoom(z);
}

////////////////////////////////////////////////////////////
//   INITIALIZE FUNCTION                                  //
//                                                        //
//  Function to initialize the google maps area on load   //
////////////////////////////////////////////////////////////
function initializeHWM()
{
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
