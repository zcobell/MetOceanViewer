var map;
var CurrentID = "none";
var CurrentName;
var CurrentLat;
var CurrentLon;
var listenerHandle;
var layer;

window.onresize = function()
{
    $('#plot_area').highcharts().setSize($(window).width()/2,$(window).height(), false);
}

function repaint()
{
    google.maps.event.trigger(map,'resize');
}

function returnStationID()
{
    var answer = CurrentID+";"+CurrentName+";"+String(CurrentLon)+";"+String(CurrentLat);
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

    //var logoControlDiv = document.createElement('DIV');
    var zoomDiv        = document.createElement('DIV');
    zoomDiv.id         = "zoomText";
    zoomDiv.innerHTML  = "Zoom in to see stations";
    //var logoControl = new ArcadisLogo(logoControlDiv);
    //logoControlDiv.index = 0; // used for ordering
    zoomDiv.index = 1;
    //map.controls[google.maps.ControlPosition.TOP_LEFT].push(logoControlDiv);
    map.controls[google.maps.ControlPosition.BOTTOM_LEFT].push(zoomDiv);

    layer = new google.maps.FusionTablesLayer({
        query: {
            select: '\'Geocodable address\'',
            from: '1F53L-rdX5ObuZk-4vbzEs-cs-knucaitaJ7kmVfD'
        },
        options: {
            styleId: 3,
            templateId: 2
        }
    });

    google.maps.event.addListener(map, 'zoom_changed', function() {
        var zoomLevel = map.getZoom();
        if(zoomLevel>5)
        {
            if(layer.map == null)
            {
                layer.setMap(map);
                layer.setOptions({options: {suppressInfoWindows: false}});
                map.controls[google.maps.ControlPosition.BOTTOM_LEFT].clear();
                listenerHandle = google.maps.event.addListener(layer, 'click', function(event) {
                    CurrentID = event.row.SiteNumber2.value;
                    CurrentName = event.row.SiteName.value;
                    CurrentLat = event.row.Latitude.value;
                    CurrentLon = event.row.Longitude.value;
                });
            }
        }
        else
        {
            layer.setMap(null);
            map.controls[google.maps.ControlPosition.BOTTOM_LEFT].clear();
            map.controls[google.maps.ControlPosition.BOTTOM_LEFT].push(zoomDiv);
            google.maps.event.removeListener(listenerHandle);
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


  ////////////////////////////////////////////////////////////
  //   DRAWUSGSDATA                                         //
  //                                                        //
  //  Plot the USGS data on the google chart api            //
  ////////////////////////////////////////////////////////////
function drawUSGSData(RawData,Product,YLabel,NData,ErrorString) {

    //Split by rows
    RawData2 = new Array();
    RawData2 = RawData.split(";");
    USGSData = new Array();

    if(NData<2)
    {
        jAlert('There is not currently data for this parameter at this station.','USGS Data Retreival Error');
        return;
    }

    //Split the rows into their pieces
    if(RawData2.length<5)
    {
        jAlert('The following error was returned from the USGS server: \n\n'+ErrorString,'USGS Data Retreival Error');
        return;
    }

    var TempData = new Array();
    for(var i=0;i<RawData2.length;i++)
    {
        TempData = RawData2[i].split(":");
        USGSData[i] = new Array();
        for(var j=0;j<TempData.length;j++)
            USGSData[i][j] = TempData[j];
    }

    var PlottingData = new Array();
    idx2=-1;
    var ThisDate;
    var ThisData;
    for( var idx=0;idx<USGSData.length;idx++)
    {
        ThisDate = Date.UTC(Number(USGSData[idx][0]),Number(USGSData[idx][1])-1,
                            Number(USGSData[idx][2]),Number(USGSData[idx][3]),
                            Number(USGSData[idx][4]),0,0);
        ThisData = Number(USGSData[idx][5]);
        if(!isNaN(ThisDate) && !isNaN(ThisData) && (ThisData!=0))
        {
            idx2 = idx2 + 1;
            PlottingData[idx2]     = new Array();
            PlottingData[idx2][0]  = ThisDate;
            PlottingData[idx2][1]  = ThisData;
        }
    }

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
            text: 'USGS Gage Data'
        },
        subtitle: {
            text: 'Station '+window.CurrentID+' - '+window.CurrentName
        },
        xAxis: {
            type: 'datetime',
            title: 'Date (UTC)',
            dateTimeLabelFormats: { // don't display the dummy year
                month: '%e. %b',
                year: '%b'
            },
            gridLineWidth: 1
        },
        yAxis: {
            labels: {format: '{value:.2f}'},
            title: {
                text: YLabel
            },
            alternateGridColor: '#EEEEEE'
        },
        plotOptions: {
            series: {
                marker: {
                    enabled: false
                },
                animation: {
                   enabled: true,
                   duration: 1000,
                   easing: 'linear'
                },
                gridLineWidth: 1
            }
        },
        tooltip: {
            formatter: function() {
                    return '<b>'+ this.series.name +'</b><br/>'+
                    Highcharts.dateFormat('%b %e, %Y %l:%M%p', this.x) +': '+ this.y;
            }
        },
        series: [{
            name: Product,
            data: PlottingData
        }]
    });


    return;
}

