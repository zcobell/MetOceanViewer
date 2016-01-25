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
var map;
var CurrentID = -1;
var CurrentName;
var CurrentLat;
var CurrentLon;

var Locations = new Array();
var DataSeries = new Array();
var SeriesOptions = new Array();
var StationName = new Array();
var ErrorList = new Array();
var PlotTitle;
var XLabel,YLabel;

window.onresize = function()
{
    $('#plot_area').highcharts().setSize($(window).width()/2,$(window).height(), false);
}

function returnStationID(){
    var answer = String(CurrentID)+";"+CurrentName+";"+String(CurrentLon)+";"+String(CurrentLat);
    return answer;
}

////////////////////////////////////////////////////////////
//   PAN TO FUNCTION                                      //
//                                                        //
//  Function to pan to a different area on the map        //
////////////////////////////////////////////////////////////
function panTo(location){
var geocoder = new google.maps.Geocoder();
    geocoder.geocode( { 'address': location}, function(results, status) {
        if (status == google.maps.GeocoderStatus.OK) {
          map.setCenter(results[0].geometry.location);
          map.fitBounds(results[0].geometry.viewport);
        }
    });

}

////////////////////////////////////////////////////////////
//   INITIALIZE FUNCTION                                  //
//                                                        //
//  Function to initialize the google maps area on load   //
////////////////////////////////////////////////////////////
function initialize() {
//Initialize the map

    window.LastInfo = -1;
    var myOptions = {
      center: new google.maps.LatLng(29.5, -91.5),
      zoom: 4,
      mapTypeId: google.maps.MapTypeId.ROADMAP,
      panControl: true,
      streetViewControl: false,
      panControlOptions: { position: google.maps.ControlPosition.LEFT_TOP },
      zoomControlOptions: { position: google.maps.ControlPosition.LEFT_TOP }
    };
    map = new google.maps.Map(document.getElementById("map_canvas"), myOptions);

    var layer = new google.maps.FusionTablesLayer({
        query: {
            select: '\'Geocodable address\'',
            from: '15d2USe6y0Atqt09PUJpzhBC2HmBs3Ap6IEMt-uEs'
        },
        options: {
            styleId: 2,
            templateId: 2
        }
    });
    layer.setMap(map);

    google.maps.event.addListener(layer, 'click', function(event) {
        CurrentID = event.row.StationID.value;
        CurrentName = event.row.StationName.value;
        CurrentLat = event.row.Latitude.value;
        CurrentLon = event.row.Longitude.value;
    });

    var geocoder = new google.maps.Geocoder();
    geocoder.geocode( { 'address': "United States"}, function(results, status) {
        if (status == google.maps.GeocoderStatus.OK) {
            map.setCenter(results[0].geometry.location);
            map.fitBounds(results[0].geometry.viewport);
        }
    });

    $('#plot_area').highcharts({
        title: {
            text: ''
        },
        legend: {
            enabled: false},
        yAxis: {
            labels: {format: '{value:.2f}'},
            title: {
                text: " "
            },
            alternateGridColor: '#EEEEEE'
            },
        exporting: {
            sourceWidth: 800,
            sourceHeight: 800,
            scale: 2,
            buttons: {
                contextButton: {
                    menuItems: [{
                        text: 'Export to PNG',
                        onclick: function() {
                            this.exportChart();
                        },
                        separator: false
                    }]
                }
            }
        },
        series: [{
            type: 'line',
            name: 'none',
            data: []
        }]
    });

    }

    google.maps.event.addDomListener(window, "resize", function() {
     var center = map.getCenter();
     google.maps.event.trigger(map, "resize");
     map.setCenter(center);
    });

//Build the data series
function allocateData(NumSeries)
{
    DataSeries = new Array();
    for(var i=0;i<NumSeries;i++)
    {
      DataSeries[i] = new Array();
    }
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

//Save the time series data into a single 3 dimensional array
function AddDataSeries(SeriesIndex,ThisData,ThisError)
{

    DataSeries[SeriesIndex] = [];
    ErrorList[SeriesIndex] = ThisError;

    var ThisDataSplit = ThisData.split(";");
    for(var i=0;i<ThisDataSplit.length;i++)
    {
        DataSeries[SeriesIndex][i] = new Array();
        ThisDataSplit2 = ThisDataSplit[i].split(":");

        DataSeries[SeriesIndex][i][0] = ThisDataSplit2[0];
        DataSeries[SeriesIndex][i][1] = ThisDataSplit2[1];
        DataSeries[SeriesIndex][i][2] = ThisDataSplit2[2];
        DataSeries[SeriesIndex][i][3] = ThisDataSplit2[3];
        DataSeries[SeriesIndex][i][4] = ThisDataSplit2[4];
        DataSeries[SeriesIndex][i][5] = ThisDataSplit2[5];

        if(Number(DataSeries[SeriesIndex][i][5])<-400)
            DataSeries[SeriesIndex][i][5] = -9999;
    }
    return;
}

//Save the options for each data series
function SetSeriesOptions(index,MyName,MyColor)
{
    SeriesOptions[index] = { name: MyName, color: MyColor };
    return;
}

function setGlobal(InPlotTitle,InXLabel,InYLabel,InNullFlag)
{
    PlotTitle = InPlotTitle;
    XLabel = InXLabel;
    YLabel = InYLabel;
    NullFlag = InNullFlag;
    return;
}

function PlotTimeseries()
{
    var i,j,k;
    var seriesList = new Array();
    var Data = new Array();
    var contentString;
    var ThisData;

    if(DataSeries[0].length<5)
    {
        jAlert('The following error was returned from the NOAA server: \n\n'+ErrorList[0],'NOAA Data Retreival Error');
        return;
    }

    //Create the data tables
    for(i=0;i<DataSeries.length;i++)
    {
        Data[i]    = new Array();

        for(j=0;j<DataSeries[i].length-1;j++)
        {
            Data[i][j] = new Array();
            ThisDate = Date.UTC(Number(DataSeries[i][j][0]),Number(DataSeries[i][j][1])-1,
                                Number(DataSeries[i][j][2]),Number(DataSeries[i][j][3]),
                                Number(DataSeries[i][j][4]),0,0);
            Data[i][j][0] = ThisDate;
            Data[i][j][1] = Number(DataSeries[i][j][5]);

            //Null value check
            if(Data[i][j][1]<-999)
                Data[i][j][1] = null;
        }
    }

    //Create the series
    var LocalData;
    for(i=0;i<DataSeries.length;i++)
    {
        LocalData = Data[i];
        seriesList[i] = {name: SeriesOptions[i].name, color: SeriesOptions[i].color, data: LocalData };
    }


    //Start setting up the plot
    var yData = { labels: {format: '{value:.2f}'}, title: { text: YLabel }, gridLineWidth: 1, alternateGridColor: '#EEEEEE' };
    var xData = { type: 'datetime', title: { text: XLabel }, dateTimeLabelFormats: { month: '%e. %b', year: '%b' }, gridLineWidth: 1 };
    var plotOption = { series: { marker: { enabled: false }, animation: { enabled: true, duration: 1000, easing: 'linear' }, } };
    var plotToolTip = {
            formatter: function() {
                return '<b>'+ this.series.name +'</b><br/>'+
                Highcharts.dateFormat('%b %e, %Y %l:%M%p', this.x) +': '+ this.y.toFixed(2) +' ';
                }
            };

    // Create the HighChart
    $('#plot_area').highcharts({
        chart: {
            type: 'line',
            zoomType: 'xy'
        },
        exporting: {
            sourceWidth: 800,
            sourceHeight: 800,
            scale: 2,
            buttons: {
                contextButton: {
                    menuItems: [{
                        text: 'Export to PNG',
                        onclick: function() {
                            this.exportChart();
                        },
                        separator: false
                    }]
                }
            }
        },
        title: {
            text: 'NOAA Gage Data'
        },
        subtitle: {
            text: PlotTitle
        },
        xAxis: xData,
        yAxis: yData,
        plotOptions: plotOption,
        tooltip: plotToolTip,
        series: seriesList
    });

    return;
}
