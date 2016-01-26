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
var YMin,YMax;
var PlotTitle;
var XLabel,YLabel;
var ADCLabel,OBSLabel;
var ADCColorCode,OBSColorCode;
var AutoY, AutoX;
var XMin,XMax;
var NullFlag;

var Locations = [];
var DataSeries = [];
var SeriesOptions = [];
var StationName = [];
var map;
var LastInfoWindow;
var LastMarker = -1;

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

    for(var i=0;i<NumSeries;i++)
    {
        DataSeries[i] = [];
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
function AddDataSeries(SeriesIndex,ThisData)
{

    DataSeries[SeriesIndex] = [];

    var ThisDataSplit = ThisData.split(";");
    for(var i=0;i<ThisDataSplit.length;i++)
    {
        DataSeries[SeriesIndex][i] = [];
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
function SetSeriesOptions(index,MyName,MyColor,MyUnit,MyXadjust,MyYadjust,MyDefaultOn)
{
    SeriesOptions[index] = { name: MyName, color: MyColor, unit: MyUnit, xadjust: MyXadjust, yadjust: MyYadjust, defaultOn: MyDefaultOn };
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
            LocalID: i
        });
        google.maps.event.addListener(TimeseriesMarkers[i], 'click', function() {
            window.MarkerID = this.LocalID;
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
            InfoWindow.open(map,this);
            LastInfoWindow = InfoWindow;
        });
    }
    return;
}

function getMarker()
{
    return LastMarker;
}

function PlotTimeseries()
{
    var i,j,k;
    var seriesList = [];
    var Data = [];
    var contentString;
    var ThisData;
    var multiplier,plusX,plusY;

    //Create the data tables
    for(i=0;i<DataSeries.length;i++)
    {
        Data[i]    = [];
        multiplier = SeriesOptions[i].unit;
        plusX      = SeriesOptions[i].xadjust;
        plusY      = SeriesOptions[i].yadjust;

        for(j=0;j<DataSeries[i].length-1;j++)
        {
            Data[i][j] = [];
            ThisDate = new Date.UTC(Number(DataSeries[i][j][0]),Number(DataSeries[i][j][1])-1,
                                Number(DataSeries[i][j][2]),Number(DataSeries[i][j][3])+plusX,
                                Number(DataSeries[i][j][4]),0,0);
            Data[i][j][0] = ThisDate;
            Data[i][j][1] = Number(DataSeries[i][j][5])+plusY;

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

        //For the data series that is not available at this station,
        //turn it off in both the legend and the plot
        if(seriesList[i].data.length < 2)
        {
            seriesList[i].visible = false;
            seriesList[i].showInLegend = false;
        }
        else if(SeriesOptions[i].defaultOn==="off")
        {
            seriesList[i].visible = false;
        }
        else
        {
            seriesList[i].visible = true;
            seriesList[i].showInLegend = true;
        }
    }

    //Start setting up the plot

    var yData, xData;

    if(AutoY === "auto")
        yData = { labels: {format: '{value:.2f}'}, title: { text: YLabel }, gridLineWidth: 1, alternateGridColor: '#EEEEEE' };
    else
        yData = { min: YMin, max: YMax, labels: {format: '{value:.2f}'}, title: { text: YLabel }, gridLineWidth: 1, alternateGridColor: '#EEEEEE' };

    if(AutoX === "auto")
        xData = { type: 'datetime', title: { text: XLabel }, dateTimeLabelFormats: { month: '%e. %b', year: '%b' }, gridLineWidth: 1 };
    else
        xData = { min: XMin, max: XMax, type: 'datetime', title: { text: XLabel }, dateTimeLabelFormats: { month: '%e. %b', year: '%b' }, gridLineWidth: 1 };

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
            text: PlotTitle
        },
        subtitle: {
            text: 'Station '+StationName[LastMarker]
        },
        xAxis: xData,
        yAxis: yData,
        plotOptions: plotOption,
        tooltip: plotToolTip,
        series: seriesList
    });

    return;
}


function setGlobal(InPlotTitle,InAutoY,InYMin,InYMax,InXLabel,InYLabel,InAutoX,InXMin,InXMax,InNullFlag)
{
    PlotTitle = InPlotTitle;
    YMin = InYMin;
    YMax = InYMax;
    XLabel = InXLabel;
    YLabel = InYLabel;
    AutoY = InAutoY;
    AutoX = InAutoX;
    NullFlag = InNullFlag;

    if(AutoX=="none")
    {
        var Temp1 = InXMin.split("-");
        var Temp2 = InXMax.split("-");
        XMin      = new Date.UTC(Number(Temp1[0]),Number(Temp1[1])-1,Number(Temp1[2]),0,0,0);
        XMax      = new Date.UTC(Number(Temp2[0]),Number(Temp2[1])-1,Number(Temp2[2]),0,0,0);
    }
    return;
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


////////////////////////////////////////////////////////////
//   INITIALIZE FUNCTION                                  //
//                                                        //
//  Function to initialize the google maps area on load   //
////////////////////////////////////////////////////////////
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
