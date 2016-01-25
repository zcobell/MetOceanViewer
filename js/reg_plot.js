var PlotTitle;
var XLabel,YLabel;
var DotColor,RegColor,One21Color,BoundColor,DynamicColor;
var dotColors = [];

window.onresize = function()
{
    $('#chart_div').highcharts().setSize($(window).width(),$(window).height(), false);
}

function setGlobal(InPlotTitle,InXLabel,InYLabel,InDotColor,InRegColor,In121Color,InBoundColor,InDynamicColor)
{
    PlotTitle = InPlotTitle;
    XLabel = InXLabel;
    YLabel = InYLabel;
    DotColor = InDotColor;
    RegColor = InRegColor;
    One21Color = In121Color;
    BoundColor = InBoundColor;
    DynamicColor = InDynamicColor;

    dotColors[0] = "#ffffff";
    dotColors[1] = "#ffffff";
    dotColors[2] = "#ffffff";
    dotColors[3] = "#ffffff";
    dotColors[4] = "#ffffff";
    dotColors[5] = "#ffffff";

    return;
}

function plotRegression(model,measure,unit,maximum,slope,intercept,corr,PlotUpperLowerLines,BoundValue,SD,Confidence,classes)
{
    //Split the data into parts
    var modelList = model.split(":");
    var measureList = measure.split(":");
    var classbreaks = classes.split(":");

    if(DynamicColor == "off")
    {
        var ScatterData = new Array();
        for(var idx=0;idx<modelList.length;idx++)
        {
            if(Number(modelList[idx])>-9999)
                ScatterData[idx] = [ Number(measureList[idx]), Number(modelList[idx]) ];
        }
    }
    else
    {
        var Scatter_Class0 = new Array();
        var Scatter_Class1 = new Array();
        var Scatter_Class2 = new Array();
        var Scatter_Class3 = new Array();
        var Scatter_Class4 = new Array();
        var Scatter_Class5 = new Array();
        var Scatter_Class6 = new Array();
        var Scatter_Class7 = new Array();
        var Scatter_Class8 = new Array();
        var Length_Scatter = new Array();
        Length_Scatter[0] = 0;
        Length_Scatter[1] = 0;
        Length_Scatter[2] = 0;
        Length_Scatter[3] = 0;
        Length_Scatter[4] = 0;
        Length_Scatter[5] = 0;
        Length_Scatter[6] = 0;
        Length_Scatter[7] = 0;
        Length_Scatter[8] = 0;


        for(var idx=0;idx<modelList.length;idx++)
        {
            if(Number(modelList[idx])<-100)
            {
                Length_Scatter[0] = Length_Scatter[0] + 1;
                Scatter_Class0[Length_Scatter[0]-1] = [ Number(measureList[idx]), Number(modelList[idx]) ];
            }
            else if(Number(modelList[idx])-Number(measureList[idx])<=classbreaks[0])
            {
                Length_Scatter[1] = Length_Scatter[1] + 1;
                Scatter_Class1[Length_Scatter[1]-1] = [ Number(measureList[idx]), Number(modelList[idx]) ];
            }
            else if(Number(modelList[idx])-Number(measureList[idx])<=classbreaks[1] && Number(modelList[idx])-Number(measureList[idx])>classbreaks[0])
            {
                Length_Scatter[2] = Length_Scatter[2] + 1;
                Scatter_Class2[Length_Scatter[2]-1] = [ Number(measureList[idx]), Number(modelList[idx]) ];
            }
            else if(Number(modelList[idx])-Number(measureList[idx])<=classbreaks[2] && Number(modelList[idx])-Number(measureList[idx])>classbreaks[1])
            {
                Length_Scatter[3] = Length_Scatter[3] + 1;
                Scatter_Class3[Length_Scatter[3]-1] = [ Number(measureList[idx]), Number(modelList[idx]) ];
            }
            else if(Number(modelList[idx])-Number(measureList[idx])<=classbreaks[3] && Number(modelList[idx])-Number(measureList[idx])>classbreaks[2])
            {
                Length_Scatter[4] = Length_Scatter[4] + 1;
                Scatter_Class4[Length_Scatter[4]-1] = [ Number(measureList[idx]), Number(modelList[idx]) ];
            }
            else if(Number(modelList[idx])-Number(measureList[idx])<=classbreaks[4] && Number(modelList[idx])-Number(measureList[idx])>classbreaks[3])
            {
                Length_Scatter[5] = Length_Scatter[5] + 1;
                Scatter_Class5[Length_Scatter[5]-1] = [ Number(measureList[idx]), Number(modelList[idx]) ];
            }
            else if(Number(modelList[idx])-Number(measureList[idx])<=classbreaks[5] && Number(modelList[idx])-Number(measureList[idx])>classbreaks[4])
            {
                Length_Scatter[6] = Length_Scatter[6] + 1;
                Scatter_Class6[Length_Scatter[6]-1] = [ Number(measureList[idx]), Number(modelList[idx]) ];
            }
            else if(Number(modelList[idx])-Number(measureList[idx])<=classbreaks[6] && Number(modelList[idx])-Number(measureList[idx])>classbreaks[5])
            {
                Length_Scatter[7] = Length_Scatter[7] + 1;
                Scatter_Class7[Length_Scatter[7]-1] = [ Number(measureList[idx]), Number(modelList[idx]) ];
            }
            else if(Number(modelList[idx])-Number(measureList[idx])>classbreaks[6])
            {
                Length_Scatter[8] = Length_Scatter[8] + 1;
                Scatter_Class8[Length_Scatter[8]-1] = [ Number(measureList[idx]), Number(modelList[idx]) ];
            }
            else
                alert(Number(modelList[idx])-Number(measureList[idx]));

        }


    }

    MX = Number(maximum);
    M  = Number(slope);
    B  = Number(intercept);
    R  = Number(corr);

    var ChartAnnotation = 'Linear Regression Line: y = '+String(M)+'x + '+B+'<br>'+
        'Correlation: R^2 = '+String(R)+'<br>Standard Deviation = '+
        String(SD)+'<br>Confidence Interval Shown = '+String(Confidence)+"%";
    var annotation_x=1;
    var annotation_y=MX;

    var plotToolTip = {
            formatter: function() {
                return '<b>'+YLabel+'</b>: '+this.x+'<br><b>'+XLabel+'</b>: '+this.y.toFixed(2)+' ';
                }
            };

    if(DynamicColor == "off")
    {
        if(PlotUpperLowerLines == 1)
        {
            var seriesList = [{
                    type: 'line',
                    name: 'Lower Bound',
                    data: [[0,-BoundValue],[MX,MX-BoundValue]],
                    color: BoundColor,
                    enableMouseTracking: false,
                    showInLegend: false,
                    marker: {
                        enabled: false
                        }
                },{
                    type: 'line',
                    name: 'Upper Bound',
                    data: [[0,BoundValue],[MX,MX+BoundValue]],
                    color: BoundColor,
                    enableMouseTracking: false,
                    showInLegend: false,
                    marker: {
                        enabled: false
                        }
                },{
                    type: 'line',
                    name: '1:1 Line',
                    data: [[0,0],[MX,MX]],
                    color: One21Color,
                    enableMouseTracking: false,
                    marker: {
                        enabled: false
                        }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: DotColor,
                    data: ScatterData,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'line',
                    name: 'Regression Line',
                    data: [[0,B],[MX,MX*M+B]],
                    color: RegColor,
                    enableMouseTracking: false,
                    marker: {
                        enabled: false
                        }
                }];
        }
        else
        {
            var seriesList = [{
                    type: 'line',
                    name: '1:1 Line',
                    data: [[0,0],[MX,MX]],
                    color: One21Color,
                    enableMouseTracking: false,
                    marker: {
                        enabled: false
                        }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: DotColor,
                    data: ScatterData,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'line',
                    name: 'Regression Line',
                    data: [[0,B],[MX,MX*M+B]],
                    color: RegColor,
                    enableMouseTracking: false,
                    marker: {
                        enabled: false
                        }
                }];
        }
    }
    else
    {
        if(PlotUpperLowerLines == 1)
        {
            var seriesList = [{
                    type: 'line',
                    name: 'Lower Bound',
                    data: [[0,-BoundValue],[MX,MX-BoundValue]],
                    color: BoundColor,
                    enableMouseTracking: false,
                    showInLegend: false,
                    marker: {
                        enabled: false
                        }
                },{
                    type: 'line',
                    name: 'Upper Bound',
                    data: [[0,BoundValue],[MX,MX+BoundValue]],
                    color: BoundColor,
                    enableMouseTracking: false,
                    showInLegend: false,
                    marker: {
                        enabled: false
                        }
                },{
                    type: 'line',
                    name: '1:1 Line',
                    data: [[0,0],[MX,MX]],
                    color: One21Color,
                    enableMouseTracking: false,
                    marker: {
                        enabled: false
                        }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#B8B8B8',
                    data: Scatter_Class0,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#FF00FF',
                    data: Scatter_Class1,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#8282CD',
                    data: Scatter_Class2,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#8282CD',
                    data: Scatter_Class3,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#006600',
                    data: Scatter_Class4,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#00CC66',
                    data: Scatter_Class5,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#CCCC00',
                    data: Scatter_Class6,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#FF9933',
                    data: Scatter_Class7,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#FF0000',
                    data: Scatter_Class8,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'line',
                    name: 'Regression Line',
                    data: [[0,B],[MX,MX*M+B]],
                    color: RegColor,
                    enableMouseTracking: false,
                    marker: {
                        enabled: false
                        }
                }];
        }
        else
        {
            var seriesList = [{
                    type: 'line',
                    name: '1:1 Line',
                    data: [[0,0],[MX,MX]],
                    color: One21Color,
                    enableMouseTracking: false,
                    marker: {
                        enabled: false
                        }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#B8B8B8',
                    data: Scatter_Class0,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#FF00FF',
                    data: Scatter_Class1,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#8282CD',
                    data: Scatter_Class2,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#8282CD',
                    data: Scatter_Class3,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#006600',
                    data: Scatter_Class4,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#00CC66',
                    data: Scatter_Class5,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#CCCC00',
                    data: Scatter_Class6,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#FF9933',
                    data: Scatter_Class7,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'scatter',
                    name: 'Comparison',
                    color: '#FF0000',
                    data: Scatter_Class8,
                    showInLegend: false,
                    marker: { symbol: "circle" }
                },{
                    type: 'line',
                    name: 'Regression Line',
                    data: [[0,B],[MX,MX*M+B]],
                    color: RegColor,
                    enableMouseTracking: false,
                    marker: {
                        enabled: false
                        }
                }];
        }
    }

    $('#chart_div').highcharts({
        chart: {
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
        annotations: [{
            xValue: annotation_x,
            yValue: annotation_y,
            anchorX: 'left',
            anchorY: 'top',
            allowDragX: 'true',
            allowDragY: 'true',
            title: {
                text: ChartAnnotation
            },
            shape: {
                type: 'rect',
                params: {
                    x: 0,
                    y: 0,
                    width: 250,
                    height: 70,
                    fill: '#FFFFFF',
                    stroke: "#000000",
                    strokeWidth: 2
                }
            }
        }],
        title: { text: PlotTitle },
        subtitle: {
            text: 'High Water Mark Comparison'
        },
        xAxis: {
            labels: {format: '{value:.1f}'},
            title: { text: YLabel },
            min: 0,
            max: MX,
            gridLineWidth: 1
        },
        yAxis: {
            labels: {format: '{value:.1f}'},
            title: { text: XLabel },
            min: 0,
            max: MX,
            gridLineWidth: 1,
            alternateGridColor: '#EEEEEE'
        },
        tooltip: plotToolTip,
        series: seriesList
    });

}

function initializePlot()
{

    $('#chart_div').highcharts({
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
