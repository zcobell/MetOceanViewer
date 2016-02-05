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
#include <general_functions.h>
#include <mov_flags.h>

user_timeseries::user_timeseries(QTableWidget *inTable, QCheckBox *inXAxisCheck,
                                 QCheckBox *inYAxisCheck, QDateEdit *inStartDate,
                                 QDateEdit *inEndDate, QDoubleSpinBox *inYMinEdit,
                                 QDoubleSpinBox *inYMaxEdit, QLineEdit *inPlotTitle,
                                 QLineEdit *inXLabelEdit, QLineEdit *inYLabelEdit,
                                 QWebEngineView *inMap, QChartView *inChart,
                                 QObject *parent) : QObject(parent)
{
    table      = inTable;
    xAxisCheck = inXAxisCheck;
    yAxisCheck = inYAxisCheck;
    startDate  = inStartDate;
    endDate    = inEndDate;
    yMaxEdit   = inYMaxEdit;
    yMinEdit   = inYMinEdit;
    plotTitle  = inPlotTitle;
    xLabelEdit = inXLabelEdit;
    yLabelEdit = inYLabelEdit;
    map        = inMap;
    chart      = inChart;
}


int user_timeseries::plotData()
{
    int i,j,ierr,nFrac;
    double TempValue,TempDate;
    double unitConversion,addX,addY;
    double ymin,ymax;
    QVector<double> labels;
    QVector<QLineSeries *> series;
    QColor seriesColor;
    QDateTime minDate,maxDate;

    ierr = this->setMarkerID();
    ierr = this->getDataBounds(ymin,ymax,minDate,maxDate);
    labels = niceLabels(ymin,ymax,5,nFrac);
    for(i=0;i<labels.length();i++)
    {
        if(labels[i]>ymax)
            ymax = labels[i];
        if(labels[i]<ymin)
            ymin = labels[i];
    }

    QChart *thisChart = new QChart();
    thisChart->setAnimationOptions(QChart::SeriesAnimations);
    thisChart->legend()->setAlignment(Qt::AlignBottom);

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
    thisChart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis *axisY = new QValueAxis;
    axisY->setTickCount(5);
    axisY->setTitleText(this->yLabelEdit->text());
    axisY->setMin(ymin);
    axisY->setMax(ymax);
    thisChart->addAxis(axisY, Qt::AlignLeft);

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
      thisChart->addSeries(series[i]);
      series[i]->attachAxis(axisX);
      series[i]->attachAxis(axisY);
    }

    thisChart->setTitle(plotTitle->text());
    chart->setRenderHint(QPainter::Antialiasing);
    chart->setChart(thisChart);

    return 0;
}

int user_timeseries::getDataBounds(double &ymin, double &ymax, QDateTime &minDate, QDateTime &maxDate)
{
    int i,j;
    double unitConversion,addY;

    ymin = DBL_MAX;
    ymax = DBL_MIN;
    minDate = QDateTime(QDate(3000,1,1),QTime(0,0,0));
    maxDate = QDateTime(QDate(1500,1,1),QTime(0,0,0));

    for(i=0;i<fileDataUnique.length();i++)
    {
        unitConversion = table->item(i,3)->text().toDouble();
        addY = table->item(i,5)->text().toDouble();
        for(j=0;j<fileDataUnique[i].station[markerID].NumSnaps;j++)
        {
            if(fileDataUnique[i].station[markerID].data[j]*unitConversion+addY<ymin && fileDataUnique[i].station[markerID].data[j]>MOV_NULL_TS)
                ymin = fileDataUnique[i].station[markerID].data[j]*unitConversion+addY;
            if(fileDataUnique[i].station[markerID].data[j]*unitConversion+addY>ymax && fileDataUnique[i].station[markerID].data[j]>MOV_NULL_TS)
                ymax = fileDataUnique[i].station[markerID].data[j]*unitConversion+addY;
            if(fileDataUnique[i].station[markerID].date[j]<minDate)
                minDate = fileDataUnique[i].station[markerID].date[j];
            if(fileDataUnique[i].station[markerID].date[j]>maxDate)
                maxDate = fileDataUnique[i].station[markerID].date[j];
        }
    }
    return 0;
}
