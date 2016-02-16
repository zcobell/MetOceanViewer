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
    int i,j,ierr;
    double ymin,ymax;
    QString S1,S2,format;
    QDateTime minDateTime,maxDateTime;

    maxDateTime = QDateTime(QDate(1000,1,1),QTime(0,0,0));
    minDateTime = QDateTime(QDate(3000,1,1),QTime(0,0,0));

    //...Create the line series
    ierr = this->generateLabels();
    ierr = this->retrieveProduct(3,S1,S2);
    ierr = this->getDataBounds(ymin,ymax);

    //...Create the chart
    this->thisChart = new QChart();
    this->chart->m_chart = this->thisChart;

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
    axisX->setTitleFont(QFont("Helvetica",10,QFont::Bold));
    axisX->setMin(minDateTime);
    axisX->setMax(maxDateTime);
    this->thisChart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis *axisY = new QValueAxis;
    axisY->setLabelFormat(format);
    axisY->setTitleText(this->yLabel);
    axisY->setTitleFont(QFont("Helvetica",10,QFont::Bold));
    axisY->setMin(ymin);
    axisY->setMax(ymax);
    this->thisChart->addAxis(axisY, Qt::AlignLeft);

    for(i=0;i<this->CurrentNOAAStation.length();i++)
    {
        if(i==0)
        {
            for(j=0;j<this->CurrentNOAAStation[i].length();j++)
                series1->append(QDateTime(this->CurrentNOAAStation[i][j].Date,
                                          this->CurrentNOAAStation[i][j].Time).toMSecsSinceEpoch(),
                                          this->CurrentNOAAStation[i][j].value);
            this->thisChart->addSeries(series1);
            series1->attachAxis(axisX);
            series1->attachAxis(axisY);
        }
        else if(i==1)
        {
            for(j=0;j<this->CurrentNOAAStation[i].length();j++)
                series2->append(QDateTime(this->CurrentNOAAStation[i][j].Date,
                                          this->CurrentNOAAStation[i][j].Time).toMSecsSinceEpoch(),
                                          this->CurrentNOAAStation[i][j].value);
            this->thisChart->addSeries(series2);
            series2->attachAxis(axisX);
            series2->attachAxis(axisY);
        }
    }

    for(i=0;i<this->thisChart->legend()->markers().length();i++)
        this->thisChart->legend()->markers().at(i)->setFont(QFont("Helvetica",10,QFont::Bold));

    axisY->setTickCount(10);
    axisY->applyNiceNumbers();
    axisX->setGridLineColor(QColor(200,200,200));
    axisY->setGridLineColor(QColor(200,200,200));
    axisY->setShadesPen(Qt::NoPen);
    axisY->setShadesBrush(QBrush(QColor(240,240,240)));
    axisY->setShadesVisible(true);

    this->thisChart->setAnimationOptions(QChart::SeriesAnimations);
    this->thisChart->legend()->setAlignment(Qt::AlignBottom);
    this->thisChart->setTitle("NOAA Station "+QString::number(this->NOAAMarkerID)+": "+this->CurrentNOAAStationName);
    this->thisChart->setTitleFont(QFont("Helvetica",14,QFont::Bold));
    this->chart->setRenderHint(QPainter::Antialiasing);
    this->chart->setChart(this->thisChart);

    foreach (QLegendMarker* marker, this->thisChart->legend()->markers())
    {
        // Disconnect possible existing connection to avoid multiple connections
        QObject::disconnect(marker, SIGNAL(clicked()), this, SLOT(handleLegendMarkerClicked()));
        QObject::connect(marker, SIGNAL(clicked()), this, SLOT(handleLegendMarkerClicked()));
    }

    this->chart->m_style = 1;
    this->chart->m_coord = new QGraphicsSimpleTextItem(this->thisChart);
    this->chart->m_coord->setPos(this->chart->size().width()/2 - 100, this->chart->size().height() - 20);
    this->chart->initializeAxisLimits();
    this->chart->setStatusBar(this->statusBar);

    return 0;
}

