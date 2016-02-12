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
#include <noaa.h>

int noaa::plotChart()
{
    int i,j,ierr,nFrac;
    double ymin,ymax;
    QVector<double> labels;
    QString S1,S2,format;
    QDateTime minDateTime,maxDateTime;

    maxDateTime = QDateTime(QDate(1000,1,1),QTime(0,0,0));
    minDateTime = QDateTime(QDate(3000,1,1),QTime(0,0,0));

    //...Create the line series
    ierr = this->generateLabels();
    ierr = this->retrieveProduct(3,S1,S2);
    ierr = this->getDataBounds(ymin,ymax);
    labels = niceLabels(ymin,ymax,5,nFrac);
    for(i=0;i<labels.length();i++)
    {
        if(labels[i]>ymax)
            ymax = labels[i];
        if(labels[i]<ymin)
            ymin = labels[i];
    }

    //...Create the chart
    QChart *thisChart = new QChart();

    QLineSeries *series1 = new QLineSeries();
    QLineSeries *series2 = new QLineSeries();
    series1->setName(S1);
    series2->setName(S2);
    series1->setPen(QPen(QColor(0,0,255),3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    series2->setPen(QPen(QColor(0,255,0),3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));

    minDateTime = this->startDateEdit->dateTime();
    maxDateTime = this->endDateEdit->dateTime().addDays(1);

    minDateTime.setTime(QTime(0,0,0));
    maxDateTime.setTime(QTime(0,0,0));

    QDateTimeAxis *axisX = new QDateTimeAxis;
    axisX->setTickCount(5);
    if(this->StartDate.daysTo(this->EndDate)>90)
        axisX->setFormat("MM/yyyy");
    else if(this->StartDate.daysTo(this->EndDate)>4)
        axisX->setFormat("MM/dd/yyyy");
    else
        axisX->setFormat("MM/dd/yyyy hh:mm");
    axisX->setTitleText("Date (GMT)");
    axisX->setMin(minDateTime);
    axisX->setMax(maxDateTime);
    thisChart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis *axisY = new QValueAxis;
    axisY->setLabelFormat(format);
    axisY->setTitleText(this->yLabel);
    axisY->setMin(ymin);
    axisY->setMax(ymax);
    thisChart->addAxis(axisY, Qt::AlignLeft);

    for(i=0;i<this->CurrentNOAAStation.length();i++)
    {
        if(i==0)
        {
            for(j=0;j<this->CurrentNOAAStation[i].length();j++)
                series1->append(QDateTime(this->CurrentNOAAStation[i][j].Date,
                                          this->CurrentNOAAStation[i][j].Time).toMSecsSinceEpoch(),
                                          this->CurrentNOAAStation[i][j].value);
            thisChart->addSeries(series1);
            series1->attachAxis(axisX);
        }
        else if(i==1)
        {
            for(j=0;j<this->CurrentNOAAStation[i].length();j++)
                series2->append(QDateTime(this->CurrentNOAAStation[i][j].Date,
                                          this->CurrentNOAAStation[i][j].Time).toMSecsSinceEpoch(),
                                          this->CurrentNOAAStation[i][j].value);
            thisChart->addSeries(series2);
            series2->attachAxis(axisX);
        }
    }

    thisChart->setAnimationOptions(QChart::SeriesAnimations);
    thisChart->legend()->setAlignment(Qt::AlignBottom);
    thisChart->setTitle("NOAA Station "+QString::number(this->NOAAMarkerID)+": "+this->CurrentNOAAStationName);
    chart->setRenderHint(QPainter::Antialiasing);
    chart->setChart(thisChart);

    return 0;
}
