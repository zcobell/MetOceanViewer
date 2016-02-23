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
    int i,j,k,ierr,seriesCounter,colorCounter;
    qint64 TempDate;
    qreal TempValue;
    double unitConversion,addX,addY;
    QVector<double> addXList;
    double ymin,ymax;
    QVector<QLineSeries *> series;
    QColor seriesColor;
    QDateTime minDate,maxDate;

    colorCounter = -1;

    //...At some point this may no longer be
    //   needed, but this defines an offset from UTC
    //   For some reason, the qDatTime axis operate in local
    //   time, which can show an offset when converting to mSecSinceEpoch
    QDateTime now = QDateTime::currentDateTime();
    qint64 offset = now.offsetFromUtc()*1000;

    //...Get the current marker selections, multiple if user ctrl+click selects
    this->getMultipleMarkersFromMap();
    if(this->selectedStations.length()>16)
    {
        this->errorString = "Too many markers are selected. A maximum of 16 is allowed.";
        return -1;
    }

    addXList.resize(fileDataUnique.length());
    for(i=0;i<fileDataUnique.length();i++)
        addXList[i] = table->item(i,4)->text().toDouble();

    ierr = this->setMarkerID();
    ierr = this->getDataBounds(ymin,ymax,minDate,maxDate,addXList);

    this->thisChart = new QChart();
    this->chart->m_chart = this->thisChart;
    this->thisChart->setAnimationOptions(QChart::SeriesAnimations);
    this->thisChart->legend()->setAlignment(Qt::AlignBottom);

    QDateTimeAxis *axisX = new QDateTimeAxis(this);
    axisX->setTickCount(5);
    axisX->setTitleText("Date");
    if(!this->xAxisCheck->isChecked())
    {
        axisX->setMin(this->startDate->dateTime());
        axisX->setMax(this->endDate->dateTime());
    }
    else
    {
        axisX->setMin(minDate);
        axisX->setMax(maxDate);
    }
    axisX->setTitleFont(QFont("Helvetica",10,QFont::Bold));
    this->thisChart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis *axisY = new QValueAxis(this);
    axisY->setTickCount(5);
    axisY->setTitleText(this->yLabelEdit->text());
    if(!this->yAxisCheck->isChecked())
    {
        axisY->setMin(this->yMinEdit->value());
        axisY->setMax(this->yMaxEdit->value());
    }
    else
    {
        axisY->setMin(ymin);
        axisY->setMax(ymax);
    }
    axisY->setTitleFont(QFont("Helvetica",10,QFont::Bold));
    this->thisChart->addAxis(axisY, Qt::AlignLeft);

    if(axisX->min().daysTo(axisX->max())>90)
        axisX->setFormat("MM/yyyy");
    else if(axisX->min().daysTo(axisX->max())>4)
        axisX->setFormat("MM/dd/yyyy");
    else
        axisX->setFormat("MM/dd/yyyy hh:mm");

    seriesCounter = 0;

    for(i=0;i<fileDataUnique.length();i++)
    {

      if(this->selectedStations.length()==1)
      {
          seriesCounter = seriesCounter + 1;
          series.resize(seriesCounter);
          series[seriesCounter-1] = new QLineSeries(this);
          series[seriesCounter-1]->setName(table->item(seriesCounter-1,1)->text());
          seriesColor.setNamedColor(table->item(seriesCounter-1,2)->text());
          series[seriesCounter-1]->setPen(QPen(seriesColor,3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
          unitConversion = table->item(seriesCounter-1,3)->text().toDouble();
          addX = table->item(seriesCounter-1,4)->text().toDouble();
          addY = table->item(seriesCounter-1,5)->text().toDouble();
          for(j=0;j<fileDataUnique[i].station[this->markerID].data.length();j++)
          {
              TempDate = fileDataUnique[i].station[this->markerID].date[j].toMSecsSinceEpoch()+addX*3.6e+6-offset;
              TempValue = fileDataUnique[i].station[this->markerID].data[j]*unitConversion+addY;
              if(fileDataUnique[i].station[this->markerID].data[j]!=MOV_NULL_TS)
                series[seriesCounter-1]->append(TempDate,TempValue);
          }
          this->thisChart->addSeries(series[seriesCounter-1]);
          this->thisChart->legend()->markers().at(seriesCounter-1)->setFont(QFont("Helvetica",10,QFont::Bold));
          series[seriesCounter-1]->attachAxis(axisX);
          series[seriesCounter-1]->attachAxis(axisY);
      }
      else
      {
          //...Plot multiple stations. We use random colors and append the station number
          for(k=0;k<this->selectedStations.length();k++)
          {
              if(!fileDataUnique[i].station[this->selectedStations[k]].isNull)
              {
                  seriesCounter = seriesCounter + 1;
                  colorCounter = colorCounter + 1;

                  //...Loop the colors
                  if(colorCounter>=this->randomColorList.length())
                      colorCounter = 0;

                  series.resize(seriesCounter);
                  series[seriesCounter-1] = new QLineSeries(this);
                  series[seriesCounter-1]->setName(fileDataUnique[i].station[this->selectedStations[k]].StationName+": "+table->item(i,1)->text());
                  seriesColor = this->randomColorList[colorCounter];
                  series[seriesCounter-1]->setPen(QPen(seriesColor,3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
                  unitConversion = table->item(i,3)->text().toDouble();
                  addX = table->item(i,4)->text().toDouble();
                  addY = table->item(i,5)->text().toDouble();
                  for(j=0;j<fileDataUnique[i].station[this->selectedStations[k]].data.length();j++)
                  {
                      TempDate = fileDataUnique[i].station[this->selectedStations[k]].date[j].toMSecsSinceEpoch()+addX*3.6e+6-offset;
                      TempValue = fileDataUnique[i].station[this->selectedStations[k]].data[j]*unitConversion+addY;
                      if(fileDataUnique[i].station[this->selectedStations[k]].data[j]!=MOV_NULL_TS)
                        series[seriesCounter-1]->append(TempDate,TempValue);
                  }
                  this->thisChart->addSeries(series[seriesCounter-1]);
                  this->thisChart->legend()->markers().at(seriesCounter-1)->setFont(QFont("Helvetica",10,QFont::Bold));
                  series[seriesCounter-1]->attachAxis(axisX);
                  series[seriesCounter-1]->attachAxis(axisY);
              }
          }
      }

    }

    axisY->setTickCount(10);
    axisY->applyNiceNumbers();
    axisX->setGridLineColor(QColor(200,200,200));
    axisY->setGridLineColor(QColor(200,200,200));
    axisY->setShadesPen(Qt::NoPen);
    axisY->setShadesBrush(QBrush(QColor(240,240,240)));
    axisY->setShadesVisible(true);
    axisY->applyNiceNumbers();


    if(this->selectedStations.length()==1)
        this->thisChart->setTitle(this->plotTitle->text()+": "+this->fileDataUnique[0].station[this->markerID].StationName);
    else
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
