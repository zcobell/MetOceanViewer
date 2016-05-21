/*-------------------------------GPL-------------------------------------//
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
//-----------------------------------------------------------------------*/
#include "user_timeseries.h"
#include "mov_generic.h"
#include "mov_flags.h"
#include "javascriptAsyncReturn.h"
#include "adcirc_station_output.h"
#include <netcdf.h>

user_timeseries::user_timeseries(QTableWidget *inTable, QCheckBox *inXAxisCheck,
                                 QCheckBox *inYAxisCheck, QDateEdit *inStartDate,
                                 QDateEdit *inEndDate, QDoubleSpinBox *inYMinEdit,
                                 QDoubleSpinBox *inYMaxEdit, QLineEdit *inPlotTitle,
                                 QLineEdit *inXLabelEdit, QLineEdit *inYLabelEdit,
                                 QWebEngineView *inMap, mov_QChartView *inChart,
                                 QStatusBar *inStatusBar, QVector<QColor> inRandomColorList,
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
    statusBar  = inStatusBar;
    randomColorList = inRandomColorList;
}

user_timeseries::~user_timeseries()
{

}

int user_timeseries::getDataBounds(double &ymin, double &ymax, QDateTime &minDate, QDateTime &maxDate, QVector<double> timeAddList)
{
    int i,j,k;
    double unitConversion,addY;

    QDateTime nullDate(QDate(MOV_NULL_YEAR,MOV_NULL_MONTH,MOV_NULL_DAY),QTime(MOV_NULL_HOUR,MOV_NULL_MINUTE,MOV_NULL_SECOND));
    ymin = DBL_MAX;
    ymax = DBL_MIN;
    minDate = QDateTime(QDate(3000,1,1),QTime(0,0,0));
    maxDate = QDateTime(QDate(1500,1,1),QTime(0,0,0));

    for(i=0;i<fileDataUnique.length();i++)
    {
        unitConversion = table->item(i,3)->text().toDouble();
        addY = table->item(i,5)->text().toDouble();
        for(k=0;k<this->selectedStations.length();k++)
        {
            if(!fileDataUnique[i]->station[this->selectedStations[k]]->isNull)
            {
                for(j=0;j<fileDataUnique[i]->station[this->selectedStations[k]]->data.length();j++)
                {
                    if(fileDataUnique[i]->station[this->selectedStations[k]]->data[j]*unitConversion+addY<ymin &&
                            fileDataUnique[i]->station[this->selectedStations[k]]->data[j]!=MOV_NULL_TS)
                        ymin = fileDataUnique[i]->station[this->selectedStations[k]]->data[j]*unitConversion+addY;
                    if(fileDataUnique[i]->station[this->selectedStations[k]]->data[j]*unitConversion+addY>ymax &&
                            fileDataUnique[i]->station[this->selectedStations[k]]->data[j]!=MOV_NULL_TS)
                        ymax = fileDataUnique[i]->station[this->selectedStations[k]]->data[j]*unitConversion+addY;
                    if(fileDataUnique[i]->station[this->selectedStations[k]]->date[j].addSecs(timeAddList[i]*3600.0)<minDate &&
                            fileDataUnique[i]->station[this->selectedStations[k]]->date[j]!=nullDate)
                        minDate = fileDataUnique[i]->station[this->selectedStations[k]]->date[j].addSecs(timeAddList[i]*3600.0);
                    if(fileDataUnique[i]->station[this->selectedStations[k]]->date[j].addSecs(timeAddList[i]*3600.0)>maxDate &&
                            fileDataUnique[i]->station[this->selectedStations[k]]->date[j]!=nullDate)
                        maxDate = fileDataUnique[i]->station[this->selectedStations[k]]->date[j].addSecs(timeAddList[i]*3600.0);
                }
            }
        }
    }
    return 0;
}



int user_timeseries::saveImage(QString filename, QString filter)
{
    if(filter == "PDF (*.pdf)")
    {
        QPrinter printer(QPrinter::HighResolution);
        printer.setPageSize(QPrinter::Letter);
        printer.setResolution(400);
        printer.setOrientation(QPrinter::Landscape);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(filename);

        QPainter painter(&printer);
        painter.setRenderHint(QPainter::Antialiasing,true);
        painter.begin(&printer);

        //...Page 1 - Chart
        this->chart->render(&painter);

        //...Page 2 - Map
        printer.newPage();
        QPixmap renderedMap = this->map->grab();
        QPixmap mapScaled = renderedMap.scaledToWidth(printer.width());
        if(mapScaled.height()>printer.height())
            mapScaled = renderedMap.scaledToHeight(printer.height());
        int cw = (printer.width()-mapScaled.width())/2;
        int ch = (printer.height()-mapScaled.height())/2;
        painter.drawPixmap(cw,ch,mapScaled.width(),mapScaled.height(),mapScaled);

        painter.end();
    }
    else if(filter == "JPG (*.jpg *.jpeg)")
    {
        QFile outputFile(filename);
        QSize imageSize(this->map->size().width()+this->chart->size().width(),this->map->size().height());
        QRect chartRect(this->map->size().width(),0,this->chart->size().width(),this->chart->size().height());

        QImage pixmap(imageSize, QImage::Format_ARGB32);
        pixmap.fill(Qt::white);
        QPainter imagePainter(&pixmap);
        imagePainter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        this->map->render(&imagePainter,QPoint(0,0));
        this->chart->render(&imagePainter,chartRect);

        outputFile.open(QIODevice::WriteOnly);
        pixmap.save(&outputFile,"JPG",100);
    }

    return 0;
}


int user_timeseries::getCurrentMarkerID()
{
    return this->markerID;
}

int user_timeseries::setMarkerID()
{
    this->markerID = this->getMarkerIDFromMap();
    return 0;
}

int user_timeseries::getClickedMarkerID()
{
    return this->getMarkerIDFromMap();
}

int user_timeseries::getMarkerIDFromMap()
{
    QVariant eval = QVariant();
    this->map->page()->runJavaScript("getMarker()",[&eval](const QVariant &v){eval = v;});
    while(eval.isNull())
        mov_generic::delayM(5);
    return eval.toInt();
}

int user_timeseries::getMultipleMarkersFromMap()
{
    QVariant eval = QVariant();
    this->map->page()->runJavaScript("getMarkers()",[&eval](const QVariant &v){eval = v;});
    while(eval.isNull())
        mov_generic::delayM(5);

    int i;
    QString tempString;
    QString data = eval.toString();
    QStringList dataList = data.split(",");
    tempString = dataList.value(0);
    int nMarkers = tempString.toInt();

    if(nMarkers>0)
    {
        this->selectedStations.resize(nMarkers);
        for(i=1;i<=nMarkers;i++)
        {
            tempString = dataList.value(i);
            this->selectedStations[i-1] = tempString.toInt();
        }
    }
    else
        return -1;

    return 0;
}


int user_timeseries::getAsyncMultipleMarkersFromMap()
{
    javascriptAsyncReturn *javaReturn = new javascriptAsyncReturn(this);
    connect(javaReturn,SIGNAL(valueChanged(QString)),this,SLOT(javascriptDataReturned(QString)));
    this->map->page()->runJavaScript("getMarkers()",[javaReturn](const QVariant &v){javaReturn->setValue(v);});
    return 0;
}

void user_timeseries::javascriptDataReturned(QString data)
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

    QString tempString;
    QStringList dataList = data.split(",");
    tempString = dataList.value(0);
    int nMarkers = tempString.toInt();

    if(nMarkers>0)
    {
        this->selectedStations.resize(nMarkers);
        for(i=1;i<=nMarkers;i++)
        {
            tempString = dataList.value(i);
            this->selectedStations[i-1] = tempString.toInt();
        }
    }
    else
    {
        emit timeseriesError("No stations selected");
        return;
    }

    addXList.resize(fileDataUnique.length());
    for(i=0;i<fileDataUnique.length();i++)
        addXList[i] = table->item(i,4)->text().toDouble();

    this->markerID = this->selectedStations[0];
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
        minDate = minDate.addSecs(-offset/1000);
        maxDate = maxDate.addSecs(-offset/1000);
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
          for(j=0;j<fileDataUnique[i]->station[this->markerID]->data.length();j++)
          {
              TempDate = fileDataUnique[i]->station[this->markerID]->date[j].toMSecsSinceEpoch()+addX*3.6e+6-offset;
              TempValue = fileDataUnique[i]->station[this->markerID]->data[j]*unitConversion+addY;
              if(fileDataUnique[i]->station[this->markerID]->data[j]!=MOV_NULL_TS)
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
              if(!fileDataUnique[i]->station[this->selectedStations[k]]->isNull)
              {
                  seriesCounter = seriesCounter + 1;
                  colorCounter = colorCounter + 1;

                  //...Loop the colors
                  if(colorCounter>=this->randomColorList.length())
                      colorCounter = 0;

                  series.resize(seriesCounter);
                  series[seriesCounter-1] = new QLineSeries(this);
                  series[seriesCounter-1]->setName(fileDataUnique[i]->station[this->selectedStations[k]]->StationName+": "+table->item(i,1)->text());
                  seriesColor = this->randomColorList[colorCounter];
                  series[seriesCounter-1]->setPen(QPen(seriesColor,3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
                  unitConversion = table->item(i,3)->text().toDouble();
                  addX = table->item(i,4)->text().toDouble();
                  addY = table->item(i,5)->text().toDouble();
                  for(j=0;j<fileDataUnique[i]->station[this->selectedStations[k]]->data.length();j++)
                  {
                      TempDate = fileDataUnique[i]->station[this->selectedStations[k]]->date[j].toMSecsSinceEpoch()+addX*3.6e+6-offset;
                      TempValue = fileDataUnique[i]->station[this->selectedStations[k]]->data[j]*unitConversion+addY;
                      if(fileDataUnique[i]->station[this->selectedStations[k]]->data[j]!=MOV_NULL_TS)
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
        this->thisChart->setTitle(this->plotTitle->text()+": "+this->fileDataUnique[0]->station[this->markerID]->StationName);
    else
        this->thisChart->setTitle(this->plotTitle->text());

    this->thisChart->setTitleFont(QFont("Helvetica",14,QFont::Bold));
    this->chart->setRenderHint(QPainter::Antialiasing);
    this->chart->setChart(this->thisChart);

    foreach (QLegendMarker* marker, this->thisChart->legend()->markers())
    {
        // Disconnect possible existing connection to avoid multiple connections
        disconnect(marker, SIGNAL(clicked()), this->chart, SLOT(handleLegendMarkerClicked()));
        connect(marker, SIGNAL(clicked()), this->chart, SLOT(handleLegendMarkerClicked()));
    }

    this->chart->m_style = 1;
    this->chart->m_coord = new QGraphicsSimpleTextItem(this->thisChart);
    this->chart->m_coord->setPos(this->chart->size().width()/2 - 100, this->chart->size().height() - 20);
    this->chart->initializeAxisLimits();
    this->chart->setStatusBar(this->statusBar);

    return;
}

QString user_timeseries::getErrorString()
{
    return this->errorString;
}


int user_timeseries::processData()
{
    int ierr,i,j,nRow;
    double x,y;
    QStringList TempList;
    QString javascript,StationName,TempFile,TempStationFile,InputFileType;
    QDateTime ColdStart;
    adcirc_station_output *adcircData;

    nRow = table->rowCount();
    map->reload();

    j = 0;

    for(i=0;i<nRow;i++)
    {
        TempFile = table->item(i,6)->text();
        TempList = TempFile.split(".");
        InputFileType = TempList.value(TempList.length()-1).toUpper();
        this->fileData.resize(j+1);

        if(InputFileType=="IMEDS")
        {
            this->fileData[j] = new imeds(this);
            ierr = this->fileData[j]->read(TempFile);
            if(ierr!=0)
            {
                this->errorString = "Error reading file: "+TempFile;
                return -1;
            }
            this->fileData[j]->success = true;
        }
        else if(InputFileType=="NC")
        {
            ColdStart = QDateTime::fromString(table->item(i,7)->text(),"yyyy-MM-dd hh:mm:ss");
            adcircData = new adcirc_station_output(this);
            ierr = adcircData->read(TempFile,ColdStart);
            if(ierr!=0)
            {
                this->errorString = "Error reading file: "+TempFile;
                return -1;
            }

            this->fileData[j] = adcircData->toIMEDS();
            if(ierr!=0)
                return -1;
            this->fileData[j]->success = true;

        }
        else if(InputFileType=="61"||InputFileType=="62"||InputFileType=="71"||InputFileType=="72")
        {
            ColdStart = QDateTime::fromString(table->item(i,7)->text(),"yyyy-MM-dd hh:mm:ss");
            TempStationFile = table->item(i,10)->text();
            adcircData = new adcirc_station_output(this);
            ierr = adcircData->read(TempFile,TempStationFile,ColdStart);
            if(ierr!=0)
            {
                this->errorString = "Error reading file: "+TempFile;
                return -1;
            }
            this->fileData[j] = adcircData->toIMEDS();
            if(ierr!=0)
                return -1;
            this->fileData[j]->success = true;

        }
        else
        {
            this->errorString = "Invalid file format";
            return -1;
        }

        if(fileData[j]->success)
            j = j + 1;
        else
            return -1;

    }

    //...Build a unique set of timeseries data
    ierr = this->getUniqueStationList(fileData,StationXLocs,StationYLocs);
    if(ierr!=0)
    {
        this->errorString = "Error building the station list";
        return -1;
    }
    ierr = this->buildRevisedIMEDS(fileData,StationXLocs,StationYLocs,fileDataUnique);
    if(ierr!=0)
    {
        this->errorString = "Error building the unique dataset";
        return -1;
    }

    //...Check that the page is finished loading
    QEventLoop loop;
    connect(map->page(),SIGNAL(loadFinished(bool)),&loop,SLOT(quit()));
    loop.exec();

    //...Add the markers to the map
    map->page()->runJavaScript("allocateData("+QString::number(fileDataUnique.length())+")");
    for(i=0;i<fileDataUnique[0]->nstations;i++)
    {
        x=-1.0;
        y=-1.0;
        StationName = "NONAME";

        //Check that we aren't sending a null location to
        //the backend
        for(j=0;j<fileDataUnique.length();j++)
        {
            if(!fileDataUnique[j]->station[i]->isNull)
            {
                StationName = fileDataUnique[j]->station[i]->StationName;
                x = fileDataUnique[j]->station[i]->longitude;
                y = fileDataUnique[j]->station[i]->latitude;
                break;
            }
        }

        javascript = "SetMarkerLocations("+QString::number(i)+
                ","+QString::number(x)+","+
                QString::number(y)+",'"+
                StationName+"')";
        map->page()->runJavaScript(javascript);
    }
    map->page()->runJavaScript("AddToMap()");

    return 0;
}


int user_timeseries::plotData()
{

    //...Get the current marker selections, multiple if user ctrl+click selects
    this->getAsyncMultipleMarkersFromMap();

    return 0;
}


//-------------------------------------------//
//Generate a unique list of stations so that
//we can later build a complete list of stations
//and not show data where it doesn't exist for
//certain files
//-------------------------------------------//
int user_timeseries::GetUniqueStationList(QVector<imeds *> Data, QVector<double> &X, QVector<double> &Y)
{
    int i,j,k;
    bool found;
    for(i=0;i<Data.length();i++)
    {
        for(j=0;j<Data[i]->nstations;j++)
        {
            found = false;
            for(k=0;k<X.length();k++)
            {
                if(Data[i]->station[j]->longitude == X[k] &&
                        Data[i]->station[j]->latitude == Y[k] )
                {
                    found = true;
                    break;
                }
            }
            if(!found)
            {
                X.resize(X.length()+1);
                Y.resize(Y.length()+1);
                X[X.length()-1] = Data[i]->station[j]->longitude;
                Y[Y.length()-1] = Data[i]->station[j]->latitude;
            }

        }
    }
    return 0;
}
//-------------------------------------------//



int user_timeseries::getUniqueStationList(QVector<imeds *> Data, QVector<double> &X, QVector<double> &Y)
{
    int i,j,k;
    bool found;
    for(i=0;i<Data.length();i++)
    {
        for(j=0;j<Data[i]->nstations;j++)
        {
            found = false;
            for(k=0;k<X.length();k++)
            {
                if(Data[i]->station[j]->longitude == X[k] &&
                        Data[i]->station[j]->latitude == Y[k] )
                {
                    found = true;
                    break;
                }
            }
            if(!found)
            {
                X.resize(X.length()+1);
                Y.resize(Y.length()+1);
                X[X.length()-1] = Data[i]->station[j]->longitude;
                Y[Y.length()-1] = Data[i]->station[j]->latitude;
            }

        }
    }
    return 0;
}
//-------------------------------------------//


//-------------------------------------------//
//Build a revised set of IMEDS data series
//which will have null data where there was
//not data in the file
//-------------------------------------------//
int user_timeseries::buildRevisedIMEDS(QVector<imeds*> &Data,QVector<double> X, QVector<double> Y, QVector<imeds*> &DataOut)
{
    int i,j,k;
    bool found;

    DataOut.resize(Data.length());

    for(i=0;i<Data.length();i++)
    {
        DataOut[i] = new imeds(this);

        DataOut[i]->nstations = X.length();
        DataOut[i]->header1 = Data[i]->header1;
        DataOut[i]->header2 = Data[i]->header2;
        DataOut[i]->header3 = Data[i]->header3;
        DataOut[i]->station.resize(X.length());
        for(j=0;j<X.length();j++)
        {

            DataOut[i]->station[j] = new imeds_station(this);

            DataOut[i]->station[j]->longitude = X[j];
            DataOut[i]->station[j]->latitude = Y[j];
        }
    }

    for(i=0;i<Data.length();i++)
    {
        for(j=0;j<DataOut[i]->nstations;j++)
        {
            found = false;
            for(k=0;k<Data[i]->nstations;k++)
            {
                if(Data[i]->station[k]->longitude == DataOut[i]->station[j]->longitude &&
                   Data[i]->station[k]->latitude == DataOut[i]->station[j]->latitude)
                {
                    DataOut[i]->station[j]->data.resize(Data[i]->station[k]->data.length());
                    DataOut[i]->station[j]->date.resize(Data[i]->station[k]->date.length());
                    DataOut[i]->station[j]->NumSnaps = Data[i]->station[k]->NumSnaps;
                    DataOut[i]->station[j]->StationName = Data[i]->station[k]->StationName;
                    DataOut[i]->station[j]->data = Data[i]->station[k]->data;
                    DataOut[i]->station[j]->date = Data[i]->station[k]->date;
                    DataOut[i]->station[j]->isNull = false;
                    found = true;
                    break;
                }
            }
            if(!found)
            {
                //Build a station with a null dataset we can find later
                DataOut[i]->station[j]->data.resize(1);
                DataOut[i]->station[j]->date.resize(1);
                DataOut[i]->station[j]->StationName = "NONAME";
                DataOut[i]->station[j]->data[0] = MOV_NULL_TS;
                DataOut[i]->station[j]->date[0] = QDateTime(QDate(MOV_NULL_YEAR,MOV_NULL_MONTH,MOV_NULL_DAY),
                                                          QTime(MOV_NULL_HOUR,MOV_NULL_MINUTE,MOV_NULL_SECOND));
                DataOut[i]->station[j]->isNull = true;
            }
        }
    }
    return 0;
}
