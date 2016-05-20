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
#include "usgs.h"
#include "javascriptAsyncReturn.h"

usgs::usgs(QWebEngineView *inMap,
           mov_QChartView *inChart, QRadioButton *inDailyButton,
           QRadioButton *inHistoricButton, QRadioButton *inInstantButton,
           QComboBox *inProductBox, QDateEdit *inStartDateEdit,
           QDateEdit *inEndDateEdit, QStatusBar *instatusBar, QObject *parent) : QObject(parent)
{

    //...Initialize variables
    this->USGSDataReady = false;
    this->USGSBeenPlotted = false;
    this->CurrentUSGSStationName = "none";
    this->USGSMarkerID = "none";
    this->ProductIndex = 0;
    this->ProductName = "none";

    //...Assign object pointers
    this->map = inMap;
    this->chart = inChart;
    this->dailyButton = inDailyButton;
    this->historicButton = inHistoricButton;
    this->instantButton = inInstantButton;
    this->productBox = inProductBox;
    this->startDateEdit = inStartDateEdit;
    this->endDateEdit = inEndDateEdit;
    this->statusBar = instatusBar;

}

usgs::~usgs()
{

}

void usgs::handleLegendMarkerClicked()
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


int usgs::fetchUSGSData()
{

    //...Get the current marker
    this->setAsyncMarkerSelection();

    return 0;
}

void usgs::javascriptDataReturned(QString data)
{

    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QString endDateString1,startDateString1;
    QString endDateString2,startDateString2;
    QString RequestURL;
    QStringList evalList;
    QEventLoop loop;
    int i,ierr;

    //...Station information
    evalList = data.split(";");
    this->USGSMarkerID = evalList.value(0).mid(4);
    this->CurrentUSGSStationName = evalList.value(1).simplified();
    this->CurrentUSGSLon = evalList.value(2).toDouble();
    this->CurrentUSGSLat = evalList.value(3).toDouble();

    //...Format the date strings
    endDateString1 = "&endDT="+this->requestEndDate.addDays(1).toString("yyyy-MM-dd");
    startDateString1 = "&startDT="+this->requestStartDate.toString("yyyy-MM-dd");
    endDateString2 = "&end_date="+this->requestEndDate.addDays(1).toString("yyyy-MM-dd");
    startDateString2 = "&begin_date="+this->requestStartDate.toString("yyyy-MM-dd");

    //...Construct the correct request URL
    if(this->USGSdataMethod==0)
        RequestURL = "http://nwis.waterdata.usgs.gov/nwis/uv?format=rdb&site_no="+this->USGSMarkerID+startDateString2+endDateString2;
    else if(this->USGSdataMethod==1)
        RequestURL = "http://waterservices.usgs.gov/nwis/iv/?sites="+this->USGSMarkerID+startDateString1+endDateString1+"&format=rdb";
    else
        RequestURL = "http://waterservices.usgs.gov/nwis/dv/?sites="+this->USGSMarkerID+startDateString1+endDateString1+"&format=rdb";

    //...Make the request to the server
    QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(RequestURL)));
    connect(reply,SIGNAL(finished()),&loop,SLOT(quit()));
    connect(reply,SIGNAL(error(QNetworkReply::NetworkError)),&loop,SLOT(quit()));
    loop.exec();

    if(reply->error()!=QNetworkReply::NoError)
    {
        emit usgsError("There was an error contacting the USGS data server");
        return;
    }

    //...Read the response
    ierr = this->readUSGSDataFinished(reply);
    if(ierr!=0)
    {
        emit usgsError("Error reading the USGS data");
        return;
    }

    //...Update the combo box
    for(i=0;i<this->Parameters.length();i++)
        productBox->addItem(this->Parameters[i]);
    productBox->setCurrentIndex(0);
    this->ProductName = productBox->currentText();

    //...Plot the first series
    ierr = this->plotUSGS();
    if(ierr!=0)
    {
        emit usgsError("No data available for this station");
        return;
    }

    //...Restore the status bar
    statusBar->clearMessage();

    return;
}


int usgs::formatUSGSInstantResponse(QByteArray Input)
{
    bool doubleok;
    int i,j,ParamStart,ParamStop,OffsetHours;
    int HeaderEnd;
    double TempData;
    QStringList TempList;
    QString TempLine,TempDateString,TempTimeZoneString;
    QDateTime CurrentDate;

    QString InputData(Input);
    QStringList SplitByLine = InputData.split(QRegExp("[\n]"),QString::SkipEmptyParts);

    ParamStart = -1;
    ParamStop = -1;
    HeaderEnd = -1;

    if(InputData.isEmpty()||InputData.isNull())
    {
        this->USGSErrorString = QString("This data is not available except from the USGS archive server.");
        return ERR_USGS_ARCHIVEONLY;
    }

    //...Save the potential error string
    this->USGSErrorString = InputData.remove(QRegExp("[\n\t\r]"));

    //...Start by finding the header and reading the parameters from it
    for(i=0;i<SplitByLine.length();i++)
    {
        if(SplitByLine.value(i).left(15)=="# Data provided")
        {
            ParamStart = i + 2;
            break;
        }
    }

    for(i=ParamStart;i<SplitByLine.length();i++)
    {
        TempLine = SplitByLine.value(i);
        if(TempLine == "#")
        {
            ParamStop = i - 1;
            break;
        }
    }

    this->Parameters.resize(ParamStop-ParamStart+1);

    for(i=ParamStart;i<=ParamStop;i++)
    {
        TempLine = SplitByLine.value(i);
        TempList = TempLine.split(" ",QString::SkipEmptyParts);
        this->Parameters[i-ParamStart] = QString();
        for(j=3;j<TempList.length();j++)
        {
            if(j==3)
                this->Parameters[i-ParamStart] = TempList.value(j);
            else
                this->Parameters[i-ParamStart] = this->Parameters[i-ParamStart] + " " + TempList.value(j);
        }
    }

    //...Find out where the header ends
    for(i=0;i<SplitByLine.length();i++)
    {
        if(SplitByLine.value(i).left(1) != "#")
        {
            HeaderEnd = i + 2;
            break;
        }
    }

    //...Initialize the array
    this->CurrentUSGSStation.resize(this->Parameters.length());

    //...Sanity check
    if(this->CurrentUSGSStation.length()==0)
        return -1;

    //...Zero counters
    for(i=0;i<this->CurrentUSGSStation.length();i++)
        this->CurrentUSGSStation[i].NumDataPoints = 0;

    //...Read the data into the array
    for(i=HeaderEnd;i<SplitByLine.length();i++)
    {
        TempLine = SplitByLine.value(i);
        TempList = TempLine.split(QRegExp("[\t]"));
        TempDateString = TempList.value(2);
        TempTimeZoneString = TempList.value(3);
        CurrentDate = QDateTime::fromString(TempDateString,"yyyy-MM-dd hh:mm");
        CurrentDate.setTimeSpec(Qt::UTC);
        OffsetHours = getTimezoneOffset(TempTimeZoneString);
        CurrentDate = CurrentDate.addSecs(-3600*OffsetHours);
        for(j=0;j<this->Parameters.length();j++)
        {
            TempData = TempList.value(2*j+4).toDouble(&doubleok);
            if(!TempList.value(2*j+4).isNull() && doubleok)
            {
                this->CurrentUSGSStation[j].NumDataPoints = this->CurrentUSGSStation[j].NumDataPoints + 1;
                this->CurrentUSGSStation[j].Data.resize(this->CurrentUSGSStation[j].Data.length()+1);
                this->CurrentUSGSStation[j].Date.resize(this->CurrentUSGSStation[j].Date.length()+1);
                this->CurrentUSGSStation[j].Data[this->CurrentUSGSStation[j].Data.length()-1] = TempData;
                this->CurrentUSGSStation[j].Date[this->CurrentUSGSStation[j].Date.length()-1] = CurrentDate;
            }
        }
    }

    return 0;
}


int usgs::formatUSGSDailyResponse(QByteArray Input)
{
    int i,j,ParamStart,ParamStop;
    int HeaderEnd;
    double TempData;
    QStringList TempList;
    QString TempLine,TempDateString;
    QString InputData(Input);
    QStringList SplitByLine = InputData.split(QRegExp("[\n]"),QString::SkipEmptyParts);
    QDateTime CurrentDate;
    bool doubleok;

    ParamStart = -1;
    ParamStop = -1;
    HeaderEnd = -1;

    //...Save the potential error string
    this->USGSErrorString = InputData.remove(QRegExp("[\n\t\r]"));

    //...Start by finding the header and reading the parameters from it
    for(i=0;i<SplitByLine.length();i++)
    {
        if(SplitByLine.value(i).left(15)=="# Data provided")
        {
            ParamStart = i + 2;
            break;
        }
    }

    for(i=ParamStart;i<SplitByLine.length();i++)
    {
        TempLine = SplitByLine.value(i);
        if(TempLine == "#")
        {
            ParamStop = i - 1;
            break;
        }
    }

    this->Parameters.resize(ParamStop-ParamStart+1);

    for(i=ParamStart;i<=ParamStop;i++)
    {
        TempLine = SplitByLine.value(i);
        TempList = TempLine.split(" ",QString::SkipEmptyParts);
        this->Parameters[i-ParamStart] = QString();
        for(j=3;j<TempList.length();j++)
        {
            if(j==3)
                this->Parameters[i-ParamStart] = TempList.value(j);
            else
                this->Parameters[i-ParamStart] = this->Parameters[i-ParamStart] + " " + TempList.value(j);
        }

    }

    //...Remove the leading number
    for(i=0;i<this->Parameters.length();i++)
        Parameters[i] = Parameters[i].mid(6).simplified();

    //...Find out where the header ends
    for(i=0;i<SplitByLine.length();i++)
    {
        if(SplitByLine.value(i).left(1) != "#")
        {
            HeaderEnd = i + 2;
            break;
        }
    }

    //...Delete the old data
    for(i=0;i<this->CurrentUSGSStation.length();i++)
    {
        this->CurrentUSGSStation[i].Data.clear();
        this->CurrentUSGSStation[i].Date.clear();
        this->CurrentUSGSStation[i].NumDataPoints = 0;
    }
    this->CurrentUSGSStation.clear();

    //...Initialize the array
    this->CurrentUSGSStation.resize(Parameters.length());

    //...Zero counters
    for(i=0;i<this->CurrentUSGSStation.length();i++)
        this->CurrentUSGSStation[i].NumDataPoints = 0;

    //...Read the data into the array
    for(i=HeaderEnd;i<SplitByLine.length();i++)
    {
        TempLine = SplitByLine.value(i);
        TempList = TempLine.split(QRegExp("[\t]"));
        TempDateString = TempList.value(2);
        CurrentDate = QDateTime::fromString(TempDateString,"yyyy-MM-dd");
        CurrentDate.setTimeSpec(Qt::UTC);
        for(j=0;j<Parameters.length();j++)
        {
            TempData = TempList.value(2*j+3).toDouble(&doubleok);
            if(!TempList.value(2*j+3).isNull() && doubleok)
            {
                this->CurrentUSGSStation[j].NumDataPoints = this->CurrentUSGSStation[j].NumDataPoints + 1;
                this->CurrentUSGSStation[j].Data.resize(this->CurrentUSGSStation[j].Data.length()+1);
                this->CurrentUSGSStation[j].Date.resize(this->CurrentUSGSStation[j].Date.length()+1);
                this->CurrentUSGSStation[j].Data[this->CurrentUSGSStation[j].Data.length()-1] = TempData;
                this->CurrentUSGSStation[j].Date[this->CurrentUSGSStation[j].Date.length()-1] = CurrentDate;
            }
        }
    }

    this->USGSDataReady = true;

    return 0;
}


int usgs::getDataBounds(double &ymin, double &ymax)
{
    int j;

    ymin =  999999999.0;
    ymax = -999999999.0;

    for(j=0;j<this->USGSPlot.length();j++)
    {
        if(this->USGSPlot[j].value<ymin)
            ymin = this->USGSPlot[j].value;
        if(this->USGSPlot[j].value>ymax)
            ymax = this->USGSPlot[j].value;
    }
    return 0;
}

int usgs::getTimezoneOffset(QString timezone)
{
    if(timezone.isNull()||timezone.isEmpty())
        return 0;
    else if(timezone=="UTC")
        return 0;
    else if(timezone=="GMT")
        return 0;
    else if(timezone=="EST")
        return -5;
    else if(timezone=="EDT")
        return -4;
    else if(timezone=="CST")
        return -6;
    else if(timezone=="CDT")
        return -5;
    else if(timezone=="MST")
        return -7;
    else if(timezone=="MDT")
        return -6;
    else if(timezone=="PST")
        return -8;
    else if(timezone=="PDT")
        return -7;
    else if(timezone=="AKST")
        return -9;
    else if(timezone=="AKDT")
        return -8;
    else if(timezone=="HST")
        return -10;
    else if(timezone=="HDT")
        return -9;
    else if(timezone=="AST")
        return -4;
    else if(timezone=="ADT")
        return -3;
    else if(timezone=="SST")
        return -11;
    else if(timezone=="SDT")
        return -10;
    else
        return -9999;
}


int usgs::plotNewUSGSStation()
{
    int ierr;

    //...Check the data type
    if(instantButton->isChecked())
        this->USGSdataMethod = 1;
    else if(dailyButton->isChecked())
        this->USGSdataMethod = 2;
    else
        this->USGSdataMethod = 0;

    //...Set status bar
    statusBar->showMessage("Downloading data from USGS...");

    //...Wipe out the combo box
    productBox->clear();

    //...Get the time period for the data
    this->requestEndDate = endDateEdit->dateTime();
    this->requestStartDate = startDateEdit->dateTime();

    //...Grab the data from the server
    ierr = this->fetchUSGSData();

    return 0;
}

int usgs::replotCurrentUSGSStation(int index)
{
    int ierr;
    if(this->USGSDataReady)
    {
        this->ProductIndex = index;
        this->ProductName = productBox->currentText();
        ierr = this->plotUSGS();
        statusBar->clearMessage();
        if(ierr!=0)
        {
            this->USGSErrorString = "No data available for this selection.";
            return ierr;
        }
        return 0;
    }
    return 0;
}



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

    QLineSeries *series1 = new QLineSeries(this);
    series1->setName(this->ProductName);
    series1->setPen(QPen(QColor(0,0,255),3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));

    //...Create the chart
    this->thisChart = new QChart();
    this->chart->m_chart = this->thisChart;
    this->thisChart->setAnimationOptions(QChart::SeriesAnimations);
    this->thisChart->legend()->setAlignment(Qt::AlignBottom);
    for(j=0;j<this->USGSPlot.length();j++)
    {
        series1->append(QDateTime(this->USGSPlot[j].Date,this->USGSPlot[j].Time).toMSecsSinceEpoch(),this->USGSPlot[j].value);
        if(minDateTime>QDateTime(USGSPlot[j].Date,USGSPlot[j].Time))
            minDateTime = QDateTime(USGSPlot[j].Date,USGSPlot[j].Time);
        if(maxDateTime<QDateTime(USGSPlot[j].Date,USGSPlot[j].Time))
            maxDateTime = QDateTime(USGSPlot[j].Date,USGSPlot[j].Time);
    }
    this->thisChart->addSeries(series1);

    minDateTime = QDateTime(minDateTime.date(),QTime(minDateTime.time().hour()  ,0,0));
    maxDateTime = QDateTime(maxDateTime.date(),QTime(maxDateTime.time().hour()+1,0,0));

    QDateTimeAxis *axisX = new QDateTimeAxis(this);
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
    this->thisChart->addAxis(axisX, Qt::AlignBottom);
    series1->attachAxis(axisX);

    QValueAxis *axisY = new QValueAxis(this);
    axisY->setLabelFormat(format);
    axisY->setTitleText(this->ProductName.split(",").value(0));
    axisY->setMin(ymin);
    axisY->setMax(ymax);
    this->thisChart->addAxis(axisY, Qt::AlignLeft);
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
    this->thisChart->legend()->markers().at(0)->setFont(QFont("Helvetica",10,QFont::Bold));

    this->thisChart->setTitle("USGS Station "+this->USGSMarkerID+": "+this->CurrentUSGSStationName);
    this->thisChart->setTitleFont(QFont("Helvetica",14,QFont::Bold));
    chart->setRenderHint(QPainter::Antialiasing);
    chart->setChart(this->thisChart);

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

    this->setUSGSBeenPlotted(true);

    return 0;
}


int usgs::readUSGSDataFinished(QNetworkReply *reply)
{
    int ierr;

    if(reply->error()!=QNetworkReply::NoError)
    {
        this->USGSErrorString = reply->errorString();
        return ERR_USGS_SERVERREADERROR;
    }

    //Read the data that was received
    QByteArray RawUSGSData = reply->readAll();

    //Put the data into an array with all variables
    if(this->USGSdataMethod==0||this->USGSdataMethod==1)
        ierr = this->formatUSGSInstantResponse(RawUSGSData);
    else
        ierr = this->formatUSGSDailyResponse(RawUSGSData);
    if(ierr!=0)
        return ERR_USGS_FORMATTING;

    this->USGSDataReady = true;

    //Delete the QNetworkReply object off the heap
    reply->deleteLater();

    return 0;
}


int usgs::saveUSGSImage(QString filename, QString filter)
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


int usgs::saveUSGSData(QString filename, QString format)
{
    QFile USGSOutput(filename);
    QTextStream Output(&USGSOutput);
    USGSOutput.open(QIODevice::WriteOnly);

    if(format.compare("CSV")==0)
    {
        Output << "Station: "+this->USGSMarkerID+"\n";
        Output << "Datum: N/A\n";
        Output << "Units: N/A\n";
        Output << "\n";
        for(int i=0;i<this->USGSPlot.length();i++)
        {
            Output << this->USGSPlot[i].Date.toString("MM/dd/yyyy")+","+
                      this->USGSPlot[i].Time.toString("hh:mm")+","+
                      QString::number(this->USGSPlot[i].value)+"\n";
        }
    }
    else if(format.compare("IMEDS")==0)
    {
        Output << "% IMEDS generic format - Water Level\n";
        Output << "% year month day hour min sec value\n";
        Output << "USGS   UTC    N/A\n";
        Output << "USGS_"+this->USGSMarkerID+"   "+QString::number(this->CurrentUSGSLat)+
                  "   "+QString::number(this->CurrentUSGSLon)+"\n";
        for(int i=0;i<this->USGSPlot.length();i++)
        {
            Output << this->USGSPlot[i].Date.toString("yyyy")+"    "+
                        this->USGSPlot[i].Date.toString("MM")+"    "+
                        this->USGSPlot[i].Date.toString("dd")+"    "+
                        this->USGSPlot[i].Time.toString("hh")+"    "+
                        this->USGSPlot[i].Time.toString("mm")+"    "+
                                                        "00" +"    "+
                        QString::number(this->USGSPlot[i].value)+"\n";
        }

    }
    USGSOutput.close();

    return 0;
}


int usgs::setUSGSBeenPlotted(bool input)
{
    this->USGSBeenPlotted = input;
    return 0;
}

QString usgs::getClickedUSGSStation()
{
    QString JunkString;
    double JunkDouble1,JunkDouble2;
    return this->getMarkerSelection(JunkString,JunkDouble1,JunkDouble2);
}

QString usgs::getLoadedUSGSStation()
{
    return this->USGSMarkerID;
}


void usgs::setAsyncMarkerSelection()
{
    javascriptAsyncReturn *javaReturn = new javascriptAsyncReturn(this);
    connect(javaReturn,SIGNAL(valueChanged(QString)),this,SLOT(javascriptDataReturned(QString)));
    this->map->page()->runJavaScript("returnStationID()",[javaReturn](const QVariant &v){javaReturn->setValue(v);});
    return;
}


QString usgs::getMarkerSelection(QString &name, double &longitude, double &latitude)
{
    QVariant eval = QVariant();
    map->page()->runJavaScript("returnStationID()",[&eval](const QVariant &v){eval = v;});
    while(eval.isNull())
        mov_generic::delayM(5);
    QStringList evalList = eval.toString().split(";");

    //...Station information
    name = evalList.value(1).simplified();
    longitude = evalList.value(2).toDouble();
    latitude = evalList.value(3).toDouble();

    return evalList.value(0).mid(4);
}

int usgs::setMarkerSelection()
{
    this->USGSMarkerID = getMarkerSelection(this->CurrentUSGSStationName,
                                            this->CurrentUSGSLon,this->CurrentUSGSLat);
    return 0;
}

bool usgs::getUSGSBeenPlotted()
{
    return this->USGSBeenPlotted;
}

QString usgs::getUSGSErrorString()
{
    return this->USGSErrorString;
}

