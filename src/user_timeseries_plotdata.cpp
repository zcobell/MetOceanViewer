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
#include <user_timeseries.h>
#include <mov_flags.h>

int user_timeseries::plotData()
{
    int i,j,ierr;
    double TempValue,TempDate;
    double unitConversion,addX,addY;
    QVector<double> addXList;
    double ymin,ymax;
    QVector<QLineSeries *> series;
    QColor seriesColor;
    QDateTime minDate,maxDate;

    addXList.resize(fileDataUnique.length());
    for(i=0;i<fileDataUnique.length();i++)
        addXList[i] = table->item(i,4)->text().toDouble();

    ierr = this->setMarkerID();
    ierr = this->getDataBounds(ymin,ymax,minDate,maxDate,addXList);

    this->thisChart = new QChart();
    this->chart->m_chart = this->thisChart;
    this->thisChart->setAnimationOptions(QChart::SeriesAnimations);
    this->thisChart->legend()->setAlignment(Qt::AlignBottom);

    QDateTimeAxis *axisX = new QDateTimeAxis;
    axisX->setTickCount(5);
    if(minDate.daysTo(maxDate)>90)
        axisX->setFormat("MM/yyyy");
    else if(minDate.daysTo(maxDate)>4)
        axisX->setFormat("MM/dd/yyyy");
    else
        axisX->setFormat("MM/dd/yyyy hh:mm");
    axisX->setTitleText("Date");
    axisX->setMin(minDate);
    axisX->setMax(maxDate);
    axisX->setTitleFont(QFont("Helvetica",10,QFont::Bold));
    this->thisChart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis *axisY = new QValueAxis;
    axisY->setTickCount(5);
    axisY->setTitleText(this->yLabelEdit->text());
    axisY->setMin(ymin);
    axisY->setMax(ymax);
    axisY->setTitleFont(QFont("Helvetica",10,QFont::Bold));
    this->thisChart->addAxis(axisY, Qt::AlignLeft);

    series.resize(fileDataUnique.length());

    for(i=0;i<fileDataUnique.length();i++)
    {
      series[i] = new QLineSeries();
      series[i]->setName(table->item(i,1)->text());
      seriesColor.setNamedColor(table->item(i,2)->text());
      series[i]->setPen(QPen(seriesColor,3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
      unitConversion = table->item(i,3)->text().toDouble();
      addX = table->item(i,4)->text().toDouble();
      addY = table->item(i,5)->text().toDouble();

      for(j=0;j<fileDataUnique[i].station[this->markerID].NumSnaps;j++)
      {
          TempDate = fileDataUnique[i].station[this->markerID].date[j].toMSecsSinceEpoch()+addX*3.6e+6;
          TempValue = fileDataUnique[i].station[this->markerID].data[j]*unitConversion+addY;
          if(TempValue>MOV_NULL_TS)
            series[i]->append(TempDate,TempValue);
      }
      this->thisChart->addSeries(series[i]);
      this->thisChart->legend()->markers().at(i)->setFont(QFont("Helvetica",10,QFont::Bold));
      series[i]->attachAxis(axisX);
      series[i]->attachAxis(axisY);
    }

    axisY->setTickCount(10);
    axisY->applyNiceNumbers();
    axisX->setGridLineColor(QColor(200,200,200));
    axisY->setGridLineColor(QColor(200,200,200));
    axisY->setShadesPen(Qt::NoPen);
    axisY->setShadesBrush(QBrush(QColor(240,240,240)));
    axisY->setShadesVisible(true);
    axisY->applyNiceNumbers();

    this->thisChart->setTitle(this->plotTitle->text());
    this->thisChart->setTitleFont(QFont("Helvetica",14,QFont::Bold));
    this->chart->setRenderHint(QPainter::Antialiasing);
    this->chart->setChart(this->thisChart);

    foreach (QLegendMarker* marker, this->thisChart->legend()->markers())
    {
        // Disconnect possible existing connection to avoid multiple connections
        disconnect(marker, SIGNAL(clicked()), this, SLOT(handleLegendMarkerClicked()));
        connect(marker, SIGNAL(clicked()), this, SLOT(handleLegendMarkerClicked()));
    }

    this->chart->m_style = 1;
    this->chart->m_coord = new QGraphicsSimpleTextItem(this->thisChart);
    this->chart->m_coord->setPos(this->chart->size().width()/2 - 100, this->chart->size().height() - 20);
    this->chart->initializeAxisLimits();
    this->chart->setStatusBar(this->statusBar);

    return 0;
}
