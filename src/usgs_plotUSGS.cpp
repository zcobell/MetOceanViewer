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
#include <usgs.h>

int usgs::plotUSGS()
{

    int i,j,ierr;
    double ymin,ymax;
    QString format;
    QDateTime minDateTime,maxDateTime;

    maxDateTime = QDateTime(QDate(1000,1,1),QTime(0,0,0));
    minDateTime = QDateTime(QDate(3000,1,1),QTime(0,0,0));

    //Put the data into a plotting object
    this->USGSPlot.resize(this->CurrentUSGSStation[this->ProductIndex].Date.length());
    for(int i=0;i<this->CurrentUSGSStation[this->ProductIndex].Date.length();i++)
    {
        this->USGSPlot[i].Date = this->CurrentUSGSStation[this->ProductIndex].Date[i].date();
        this->USGSPlot[i].Time = this->CurrentUSGSStation[this->ProductIndex].Date[i].time();
        this->USGSPlot[i].value = this->CurrentUSGSStation[this->ProductIndex].Data[i];
    }

    if(USGSPlot.length()<5)
        return -1;

    //...Create the line series
    ierr = this->getDataBounds(ymin,ymax);

    QLineSeries *series1 = new QLineSeries();
    series1->setName(this->ProductName);
    series1->setPen(QPen(QColor(0,0,255),3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));

    //...Create the chart
    QChart *thisChart = new QChart();
    thisChart->setAnimationOptions(QChart::SeriesAnimations);
    thisChart->legend()->setAlignment(Qt::AlignBottom);
    for(j=0;j<this->USGSPlot.length();j++)
    {
        series1->append(QDateTime(this->USGSPlot[j].Date,this->USGSPlot[j].Time).toMSecsSinceEpoch(),this->USGSPlot[j].value);
        if(minDateTime>QDateTime(USGSPlot[j].Date,USGSPlot[j].Time))
            minDateTime = QDateTime(USGSPlot[j].Date,USGSPlot[j].Time);
        if(maxDateTime<QDateTime(USGSPlot[j].Date,USGSPlot[j].Time))
            maxDateTime = QDateTime(USGSPlot[j].Date,USGSPlot[j].Time);
    }
    thisChart->addSeries(series1);

    minDateTime = QDateTime(minDateTime.date(),QTime(minDateTime.time().hour()  ,0,0));
    maxDateTime = QDateTime(maxDateTime.date(),QTime(maxDateTime.time().hour()+1,0,0));

    QDateTimeAxis *axisX = new QDateTimeAxis;
    axisX->setTickCount(5);
    if(this->requestStartDate.daysTo(this->requestEndDate)>90)
        axisX->setFormat("MM/yyyy");
    else if(this->requestStartDate.daysTo(this->requestEndDate)>4)
        axisX->setFormat("MM/dd/yyyy");
    else
        axisX->setFormat("MM/dd/yyyy hh:mm");
    axisX->setTitleText("Date (GMT)");
    axisX->setMin(minDateTime);
    axisX->setMax(maxDateTime);
    thisChart->addAxis(axisX, Qt::AlignBottom);
    series1->attachAxis(axisX);

    QValueAxis *axisY = new QValueAxis;
    axisY->setLabelFormat(format);
    axisY->setTitleText(this->ProductName.split(",").value(0));
    axisY->setMin(ymin);
    axisY->setMax(ymax);
    thisChart->addAxis(axisY, Qt::AlignLeft);
    series1->attachAxis(axisY);

    axisY->setTickCount(10);
    axisY->applyNiceNumbers();
    axisX->setGridLineColor(QColor(200,200,200));
    axisY->setGridLineColor(QColor(200,200,200));
    axisY->setShadesPen(Qt::NoPen);
    axisY->setShadesBrush(QBrush(QColor(240,240,240)));
    axisY->setShadesVisible(true);
    axisY->applyNiceNumbers();
    axisX->setTitleFont(QFont("Helvetica",10,QFont::Bold));
    axisY->setTitleFont(QFont("Helvetica",10,QFont::Bold));
    thisChart->legend()->markers().at(0)->setFont(QFont("Helvetica",10,QFont::Bold));

    thisChart->setTitle("USGS Station "+this->USGSMarkerID+": "+this->CurrentUSGSStationName);
    thisChart->setTitleFont(QFont("Helvetica",14,QFont::Bold));
    chart->setRenderHint(QPainter::Antialiasing);
    chart->setChart(thisChart);

    this->setUSGSBeenPlotted(true);

    return 0;
}

