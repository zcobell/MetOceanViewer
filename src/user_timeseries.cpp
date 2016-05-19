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
#include "user_timeseries.h"
#include "general_functions.h"
#include "mov_flags.h"
#include "javascriptAsyncReturn.h"
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

void user_timeseries::handleLegendMarkerClicked()
{
    QLegendMarker* marker = qobject_cast<QLegendMarker*> (sender());

    Q_ASSERT(marker);

    switch (marker->type())
    {
        case QLegendMarker::LegendMarkerTypeXY:
        {
            // Toggle visibility of series
            marker->series()->setVisible(!marker->series()->isVisible());

            // Turn legend marker back to visible, since hiding series also hides the marker
            // and we don't want it to happen now.
            marker->setVisible(true);

            // Dim the marker, if series is not visible
            qreal alpha = 1.0;

            if (!marker->series()->isVisible())
                alpha = 0.5;

            QColor color;
            QBrush brush = marker->labelBrush();
            color = brush.color();
            color.setAlphaF(alpha);
            brush.setColor(color);
            marker->setLabelBrush(brush);

            brush = marker->brush();
            color = brush.color();
            color.setAlphaF(alpha);
            brush.setColor(color);
            marker->setBrush(brush);

            QPen pen = marker->pen();
            color = pen.color();
            color.setAlphaF(alpha);
            pen.setColor(color);
            marker->setPen(pen);

            break;
        }
        default:
        {
            qDebug() << "Unknown marker type";
            break;
        }
    }
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
        delayM(5);
    return eval.toInt();
}

int user_timeseries::getMultipleMarkersFromMap()
{
    QVariant eval = QVariant();
    this->map->page()->runJavaScript("getMarkers()",[&eval](const QVariant &v){eval = v;});
    while(eval.isNull())
        delayM(5);

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


int user_timeseries::getAsyncClickedMarkerID()
{

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
        disconnect(marker, SIGNAL(clicked()), this, SLOT(handleLegendMarkerClicked()));
        connect(marker, SIGNAL(clicked()), this, SLOT(handleLegendMarkerClicked()));
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
    ADCNC TempAdcircNC;
    ADCASCII TempAdcircAscii;
    QDateTime ColdStart;

    nRow = table->rowCount();
    map->reload();

    j = 0;

    for(i=0;i<nRow;i++)
    {
        TempFile = table->item(i,6)->text();
        TempList = TempFile.split(".");
        InputFileType = TempList.value(TempList.length()-1).toUpper();
        this->fileData.resize(j+1);
        this->fileData[j] = new imeds(this);

        if(InputFileType=="IMEDS")
        {
            ierr = this->fileData[j]->read(TempFile);
            if(ierr!=0)
            {
                this->errorString = "Error reading file: "+TempFile;
                return -1;
            }
        }
        else if(InputFileType=="NC")
        {
            ColdStart = QDateTime::fromString(table->item(i,7)->text(),"yyyy-MM-dd hh:mm:ss");
            ierr = this->readADCIRCnetCDF(TempFile,TempAdcircNC);
            if(ierr!=0)
            {
                this->errorString = "Error reading file: "+TempFile;
                return -1;
            }
            ierr = this->NetCDF_to_IMEDS(TempAdcircNC,ColdStart,this->fileData[j]);
            if(ierr!=0)
            {
                this->errorString = "Error reading file: "+TempFile;
                return -1;
            }
        }
        else if(InputFileType=="61"||InputFileType=="62"||InputFileType=="71"||InputFileType=="72")
        {
            ColdStart = QDateTime::fromString(table->item(i,7)->text(),"yyyy-MM-dd hh:mm:ss");
            TempStationFile = table->item(i,9)->text();
            ierr = this->readADCIRCascii(TempFile,TempStationFile,TempAdcircAscii);
            if(ierr!=0)
            {
                this->errorString = "Error reading file: "+TempFile;
                return -1;
            }
            ierr = this->ADCIRC_to_IMEDS(TempAdcircAscii,ColdStart,this->fileData[j]);
            if(ierr!=0)
            {
                this->errorString = "Error reading file: "+TempFile;
                return -1;
            }
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
//Read an ADCIRC netCDF file
//-------------------------------------------//
int user_timeseries::readADCIRCnetCDF(QString filename, ADCNC &MyData)
{

    size_t station_size,time_size,startIndex;
    int i,j,time_size_int,station_size_int;
    int ierr, ncid, varid_zeta, varid_zeta2, varid_lat, varid_lon, varid_time;
    int dimid_time,dimid_station;
    bool isVector;
    double Temp;
    QVector<double> readData1;
    QVector<double> readData2;

    //Size the location array
    size_t start[2];

    QVector<QString> netcdf_types;
    netcdf_types.resize(6);
    netcdf_types[0] = "zeta";
    netcdf_types[1] = "u-vel";
    netcdf_types[2] = "v-vel";
    netcdf_types[3] = "pressure";
    netcdf_types[4] = "windx";
    netcdf_types[5] = "windy";

    //Open the file
    ierr = nc_open(filename.toUtf8(),NC_NOWRITE,&ncid);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }

    //Get the dimension ids
    ierr = nc_inq_dimid(ncid,"time",&dimid_time);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }
    ierr = nc_inq_dimid(ncid,"station",&dimid_station);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }

    //Find out the dimension size
    ierr = nc_inq_dimlen(ncid,dimid_time,&time_size);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }
    ierr = nc_inq_dimlen(ncid,dimid_station,&station_size);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }
    station_size_int = static_cast<unsigned int>(station_size);
    time_size_int = static_cast<unsigned int>(time_size);

    //Find the variable in the NetCDF file
    for(i=0;i<6;i++)
    {
        ierr = nc_inq_varid(ncid,netcdf_types[i].toUtf8(),&varid_zeta);

        //If we found the variable, we're done
        if(ierr==NC_NOERR)
        {
            if(i==1)
            {
                isVector = true;
                ierr = nc_inq_varid(ncid,netcdf_types[i+1].toUtf8(),&varid_zeta2);
                if(ierr!=NC_NOERR)
                {
                    MyData.err = ierr;
                    MyData.success = false;
                    return -1;
                }
            }
            else
                isVector = false;

            MyData.DataType=netcdf_types[i];
            break;
        }

        //If we're at the end of the array
        //and haven't quit yet, that's a problem
        if(i==5)
        {
            MyData.err = ierr;
            MyData.success = false;
            return -1;
        }
    }

    //Size the output variables
    MyData.latitude.resize(station_size_int);
    MyData.longitude.resize(station_size_int);
    MyData.nstations = station_size_int;
    MyData.NumSnaps = time_size_int;
    MyData.time.resize(time_size_int);
    MyData.data.resize(station_size_int);
    for(i=0;i<station_size_int;++i)
        MyData.data[i].resize(time_size_int);

    //Read the station locations and times
    ierr = nc_inq_varid(ncid,"time",&varid_time);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }
    ierr = nc_inq_varid(ncid,"x",&varid_lon);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }
    ierr = nc_inq_varid(ncid,"y",&varid_lat);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }

    for(j=0;j<time_size_int;j++)
    {
        startIndex = static_cast<size_t>(j);
        ierr = nc_get_var1(ncid,varid_time,&startIndex,&Temp);
        if(ierr!=NC_NOERR)
        {
            MyData.success = false;
            MyData.err = ierr;
            return -1;
        }
        MyData.time[j] = Temp;
    }

    for(j=0;j<station_size_int;j++)
    {
        startIndex = static_cast<size_t>(j);
        ierr = nc_get_var1(ncid,varid_lon,&startIndex,&Temp);
        if(ierr!=NC_NOERR)
        {
            MyData.success = false;
            MyData.err = ierr;
            return -1;
        }
        MyData.longitude[j] = Temp;

        ierr = nc_get_var1(ncid,varid_lat,&startIndex,&Temp);
        if(ierr!=NC_NOERR)
        {
            MyData.success = false;
            MyData.err = ierr;
            return -1;
        }
        MyData.latitude[j] = Temp;
    }

    readData1.resize(time_size_int);
    readData2.resize(time_size_int);

    //Loop over the stations, reading the data into memory
    for(i=0;i<station_size_int;++i)
    {


        //Read from NetCDF
        for(j=0;j<time_size_int;j++)
        {
            start[0] = static_cast<size_t>(j);
            start[1] = static_cast<size_t>(i);
            ierr = nc_get_var1(ncid,varid_zeta,start,&Temp);
            if(ierr!=NC_NOERR)
            {
                MyData.success = false;
                MyData.err = ierr;
                return -1;
            }
            readData1[j] = Temp;
        }

        if(isVector)
        {
            for(j=0;j<time_size_int;j++)
            {
                start[0] = static_cast<size_t>(j);
                start[1] = static_cast<size_t>(i);
                ierr = nc_get_var1(ncid,varid_zeta2,start,&Temp);
                if(ierr!=NC_NOERR)
                {
                    MyData.success = false;
                    MyData.err = ierr;
                    return -1;
                }
                readData2[j] = Temp;
                MyData.data[i][j] = qSqrt(qPow(readData1[j],2.0)+qPow(readData2[j],2.0));
            }
        }
        else
        {
            //Place in the output variable
            for(j=0;j<time_size_int;++j)
                MyData.data[i][j] = readData1[j];
        }
    }
    ierr = nc_close(ncid);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }

    //Finally, name the stations the default names for now. Later
    //we can get fancy and try to get the ADCIRC written names in
    //the NetCDF file
    MyData.station_name.resize(station_size_int);
    for(i=0;i<station_size_int;++i)
        MyData.station_name[i] = "Station "+QString::number(i);

    MyData.success = true;

    return 0;
}
//-------------------------------------------//


//-------------------------------------------//
//Convert the netCDF ADCIRC variable to an
//IMEDS style variable
//-------------------------------------------//
int user_timeseries::NetCDF_to_IMEDS(ADCNC netcdf, QDateTime Cold, imeds *Output)
{

    Output->nstations = netcdf.nstations;
    Output->station.resize(netcdf.nstations);
    for(int i=0;i<Output->nstations;++i)
    {
        Output->station[i]->latitude = netcdf.latitude[i];
        Output->station[i]->longitude = netcdf.longitude[i];
        Output->station[i]->NumSnaps = netcdf.NumSnaps;
        Output->station[i]->StationIndex = i;
        Output->station[i]->StationName = netcdf.station_name[i];
        Output->station[i]->data.resize(Output->station[i]->NumSnaps);
        Output->station[i]->date.resize(Output->station[i]->NumSnaps);
        for(int j=0;j<Output->station[i]->NumSnaps;++j)
        {
            Output->station[i]->data[j] = netcdf.data[i][j];
            Output->station[i]->date[j] = Cold.addSecs(netcdf.time[j]);
        }
    }
    Output->success = true;
    return -1;

}
//-------------------------------------------//


//-------------------------------------------//
//Read an ADCIRC ASCII file
//-------------------------------------------//
int user_timeseries::readADCIRCascii(QString filename, QString stationfile, ADCASCII &MyData)
{
    QFile MyFile(filename), StationFile(stationfile);
    QString header1, header2, TempLine;
    QStringList headerData, TempList, TempList2;

    MyData.success = false;

    //Check if we can open the file
    if(!MyFile.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        QMessageBox::information(NULL,"ERROR","ERROR:"+MyFile.errorString());
        MyFile.close();
        return -1;
    }
    if(!StationFile.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        QMessageBox::information(NULL,"ERROR","ERROR:"+StationFile.errorString());
        StationFile.close();
        return -1;
    }


    //Read the 61/62 style file
    header1 = MyFile.readLine();
    header2 = MyFile.readLine().simplified();
    headerData = header2.split(" ");

    MyData.NumSnaps = headerData.value(0).toInt();
    MyData.nstations = headerData.value(1).toInt();
    MyData.OutputTimeFreq = headerData.value(2).toDouble();
    MyData.OutputTSFreq = headerData.value(3).toInt();
    MyData.NumColumns = headerData.value(4).toInt();

    MyData.time.resize(MyData.NumSnaps);
    MyData.data.resize(MyData.nstations);

    for(int i=0;i<MyData.nstations;++i)
        MyData.data[i].resize(MyData.NumSnaps);

    for(int i=0;i<MyData.NumSnaps;++i)
    {
        TempLine = MyFile.readLine().simplified();
        TempList = TempLine.split(" ");
        MyData.time[i] = TempList.value(1).toDouble();
        for(int j=0;j<MyData.nstations;++j)
        {
            TempLine = MyFile.readLine().simplified();
            TempList = TempLine.split(" ");
            MyData.data[j][i] = TempList.value(1).toDouble();
            if(MyData.NumColumns==2)
                MyData.data[j][i] = qPow(qPow(MyData.data[j][i],2) +
                                         qPow(TempList.value(2).toDouble(),2),2);
        }
    }
    MyFile.close();

    //Now read the station location file
    TempLine = StationFile.readLine().simplified();
    TempList = TempLine.split(" ");
    int TempStations = TempList.value(0).toInt();
    if(TempStations!=MyData.nstations)
    {
        QMessageBox::information(NULL,"ERROR","The number of stations do not match in both files");
        return -1;
    }

    MyData.longitude.resize(MyData.nstations);
    MyData.latitude.resize(MyData.nstations);
    MyData.station_name.resize(MyData.nstations);

    for(int i=0;i<TempStations;++i)
    {
        TempLine = StationFile.readLine().simplified();
        TempList = TempLine.split(QRegExp(",| "));
        MyData.longitude[i] = TempList.value(0).toDouble();
        MyData.latitude[i] = TempList.value(1).toDouble();

        if(TempList.length()>2)
        {
            MyData.station_name[i] = "";
            for(int j=2;j<TempList.length();++j)
                MyData.station_name[i] = MyData.station_name[i]+" "+TempList.value(j);
        }
        else
            MyData.station_name[i] = "Station_"+QString::number(i);

    }
    StationFile.close();

    MyData.success = true;
    return -1;
}
//-------------------------------------------//


//-------------------------------------------//
//Convert an ADCIRC ASCII file to an IMEDS
//style file
//-------------------------------------------//
int user_timeseries::ADCIRC_to_IMEDS(ADCASCII ASCII, QDateTime Cold, imeds *MyOutput)
{

    MyOutput->nstations = ASCII.nstations;
    MyOutput = new imeds(this);
    MyOutput->station.resize(MyOutput->nstations);

    for(int i=0;i<MyOutput->nstations;++i)
    {
        MyOutput->station[i] = new imeds_station(this);
        MyOutput->station[i]->data.resize(ASCII.NumSnaps);
        MyOutput->station[i]->date.resize(ASCII.NumSnaps);
        MyOutput->station[i]->StationName = ASCII.station_name[i];
        MyOutput->station[i]->NumSnaps = ASCII.NumSnaps;
        MyOutput->station[i]->longitude = ASCII.longitude[i];
        MyOutput->station[i]->latitude = ASCII.latitude[i];
        MyOutput->station[i]->StationIndex = i;
        for(int j=0;j<ASCII.NumSnaps;++j)
        {
            MyOutput->station[i]->date[j] = Cold.addSecs(ASCII.time[j]);
            MyOutput->station[i]->data[j] = ASCII.data[i][j];
        }
    }
    MyOutput->success = true;
    return -1;
}
//-------------------------------------------//


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


//-------------------------------------------//
//Build a revised set of IMEDS data series
//which will have null data where there was
//not data in the file
//-------------------------------------------//
//int user_timeseries::BuildRevisedIMEDS(QVector<imeds *> Data, QVector<double> X, QVector<double> Y, QVector<imeds *> &DataOut)
//{
//    int i,j,k;
//    bool found;

//    QDateTime NullDate(QDate(MOV_NULL_YEAR,MOV_NULL_MONTH,MOV_NULL_DAY),QTime(MOV_NULL_HOUR,MOV_NULL_MINUTE,MOV_NULL_SECOND));

//    DataOut.resize(Data.length());
//    for(i=0;i<Data.length();i++)
//    {
//        DataOut[i]->nstations = X.length();
//        DataOut[i]->header1 = Data[i]->header1;
//        DataOut[i]->header2 = Data[i]->header2;
//        DataOut[i]->header3 = Data[i]->header3;
//        DataOut[i]->station.resize(X.length());
//        for(j=0;j<X.length();j++)
//        {
//            DataOut[i]->station[j]->longitude = X[j];
//            DataOut[i]->station[j]->latitude = Y[j];
//        }
//    }

//    for(i=0;i<Data.length();i++)
//    {
//        for(j=0;j<DataOut[i]->nstations;j++)
//        {
//            found = false;
//            for(k=0;k<Data[i]->nstations;k++)
//            {
//                if(Data[i]->station[k]->longitude == DataOut[i]->station[j]->longitude &&
//                   Data[i]->station[k]->latitude == DataOut[i]->station[j]->latitude)
//                {
//                    DataOut[i]->station[j]->data.resize(Data[i]->station[k]->data.length());
//                    DataOut[i]->station[j]->date.resize(Data[i]->station[k]->date.length());
//                    DataOut[i]->station[j]->NumSnaps = Data[i]->station[k]->NumSnaps;
//                    DataOut[i]->station[j]->StationName = Data[i]->station[k]->StationName;
//                    DataOut[i]->station[j]->data = Data[i]->station[k]->data;
//                    DataOut[i]->station[j]->date = Data[i]->station[k]->date;
//                    DataOut[i]->station[j]->isNull = false;
//                    found = true;
//                    break;
//                }
//            }
//            if(!found)
//            {
//                //Build a station with a null dataset we can find later
//                DataOut[i]->station[j]->data.resize(1);
//                DataOut[i]->station[j]->date.resize(1);
//                DataOut[i]->station[j]->StationName = "NONAME";
//                DataOut[i]->station[j]->data[0] = MOV_NULL_TS;
//                DataOut[i]->station[j]->date[0] = NullDate;
//                DataOut[i]->station[j]->isNull = true;
//            }
//        }
//    }
//    return 0;
//}


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
