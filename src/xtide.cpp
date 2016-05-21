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
#include "xtide.h"
#include "javascriptAsyncReturn.h"
#include <float.h>

//...Constructor
XTide::XTide(QWebEngineView *inMap, mov_QChartView *inChart, QDateEdit *inStartDateEdit,
             QDateEdit *inEndDateEdit, QComboBox *inUnits, QStatusBar *inStatusBar,
             QObject *parent) : QObject(parent)
{
    this->map = inMap;
    this->chart = inChart;
    this->startDateEdit = inStartDateEdit;
    this->endDateEdit = inEndDateEdit;
    this->unitSelect = inUnits;
    this->statusBar = inStatusBar;
}

//...Destructor
XTide::~XTide()
{

}

QString XTide::getErrorString()
{
    return this->xTideErrorString;
}


//...Overall routine for plotting XTide
int XTide::plotXTideStation()
{

    int ierr;

    //...Get the executables
    ierr = this->findXTideExe();
    if(ierr!=0)
        return -1;

    //...Get the selected station
    ierr = this->getAsyncClickedXTideStation();

    return 0;
}


QString XTide::getCurrentXTideStation()
{
    QVariant eval = QVariant();
    this->map->page()->runJavaScript("returnStationID()",[&eval](const QVariant &v){eval = v;});
    while(eval.isNull())
        mov_generic::delayM(5);
    QStringList evalList = eval.toString().split(";");

    return evalList.value(0);
}


QString XTide::getLoadedXTideStation()
{
    return this->currentStationName;
}


//...Get the XTide Station from the map
int XTide::getClickedXTideStation()
{
    QString tempString;
    QVariant eval = QVariant();
    this->map->page()->runJavaScript("returnStationID()",[&eval](const QVariant &v){eval = v;});
    while(eval.isNull())
        mov_generic::delayM(5);
    QStringList evalList = eval.toString().split(";");

    this->currentStationName = evalList.value(0);
    tempString               = evalList.value(1);
    this->currentXTideLon    = tempString.toDouble();
    tempString               = evalList.value(2);
    this->currentXTideLat    = tempString.toDouble();

    if(this->currentStationName=="none")
        return -1;

    return 0;
}

//...In the case of the plotting routine, we handle the asynchronous behavior more gracefully
int XTide::getAsyncClickedXTideStation()
{
    javascriptAsyncReturn *javaReturn = new javascriptAsyncReturn(this);
    connect(javaReturn,SIGNAL(valueChanged(QString)),this,SLOT(javascriptDataReturned(QString)));
    this->map->page()->runJavaScript("returnStationID()",[javaReturn](const QVariant &v){javaReturn->setValue(v);});
    return 0;
}


//...Find the xTide executable
int XTide::findXTideExe()
{
    QString installLocation       = QApplication::applicationDirPath().replace(" ","\ ");
    QString buildLocationLinux    = QApplication::applicationDirPath()+"/../MetOceanViewer/mov_libs/bin";
    QString buildLocationWindows  = QApplication::applicationDirPath()+"/../../MetOceanViewer/thirdparty/xtide-2.15.1";
    QString appLocationMacOSX     = QApplication::applicationDirPath();

    QFile location1(installLocation+"/tide");
    QFile location2(buildLocationLinux+"/tide");
    QFile location3(buildLocationWindows+"/tide.exe");
    QFile location4(buildLocationLinux+"/tide.exe");
    QFile location5(installLocation+"/tide.exe");
    QFile location6(appLocationMacOSX+"/XTide/bin/tide");

    if(location1.exists())
    {
        this->xTideExe = installLocation+"/tide";
        this->xTideHarmFile = installLocation+"/harmonics.tcd";
        return 0;
    }

    if(location2.exists())
    {
        this->xTideExe = buildLocationLinux+"/tide";
        this->xTideHarmFile = buildLocationLinux+"/harmonics.tcd";
        return 0;
    }

    if(location3.exists())
    {
        this->xTideExe = buildLocationWindows+"/tide";
        this->xTideHarmFile = buildLocationWindows+"/harmonics.tcd";
        return 0;
    }

    if(location4.exists())
    {
        this->xTideExe = buildLocationLinux+"/tide.exe";
        this->xTideHarmFile = buildLocationLinux+"/harmonics.tcd";
        return 0;
    }

    if(location5.exists())
    {
        this->xTideExe = installLocation+"/tide.exe";
        this->xTideHarmFile = installLocation+"/harmonics.tcd";
        return 0;
    }

    if(location6.exists())
    {
        this->xTideExe = appLocationMacOSX+"/XTide/bin/tide";
        this->xTideHarmFile = appLocationMacOSX+"/XTide/bin/harmonics.tcd";
        return 0;
    }

    emit xTideError("The XTide executable was not found");
    return -1;
}


//...Compute the tidal signal
int XTide::calculateXTides()
{

    int ierr;
    QEventLoop loop;

    //...Get the selected dates
    QDateTime startDate     = this->startDateEdit->dateTime();
    QDateTime endDate       = this->endDateEdit->dateTime();
    startDate.setTime(QTime(0,0,0));
    endDate = endDate.addDays(1);
    endDate.setTime(QTime(0,0,0));
    QString startDateString = startDate.toString("yyyy-MM-dd hh:mm");
    QString endDateString   = endDate.toString("yyyy-MM-dd hh:mm");

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HFILE_PATH",this->xTideHarmFile);

    //...Build a calling string. For windows, quote the executable
    //   to avoid issues with path names like "Program Files". We'll
    //   assume Linux users aren't dumb enough to do such a thing
#ifdef _WIN32
    QString xTideCmd = "\""+this->xTideExe.replace(" ","\ ")+"\""
                       " -l \""+this->currentStationName+"\""+
                       " -b \""+startDateString+"\""+
                       " -e \""+endDateString+"\""+
                       " -s \"00:30\" -z -m m";
#else
    QString xTideCmd = this->xTideExe+
                       " -l \""+this->currentStationName+"\""+
                       " -b \""+startDateString+"\""+
                       " -e \""+endDateString+"\""+
                       " -s \"00:30\" -z -m m";
#endif

    QProcess xTideRun(this);
    xTideRun.setEnvironment(env.toStringList());
    xTideRun.start(xTideCmd);
    connect(&xTideRun, SIGNAL(finished(int , QProcess::ExitStatus )), &loop, SLOT(quit()));
    loop.exec();

    //...Check the error code
    ierr = xTideRun.exitCode();
    if(ierr!=0)
        return -1;

    //...Grab the output from XTide and send to the parse routine
    ierr = this->parseXTideResponse(xTideRun.readAllStandardOutput());

    return 0;
}


int XTide::parseXTideResponse(QString xTideResponse)
{
    QString tempString,tempDate,tempTime,tempAMPM,tempElev;
    QString tempHour,tempMinute;
    QStringList tempList;
    QStringList response = xTideResponse.split("\n");
    QDate date;
    XTideStationData thisData;
    int hour,minute;
    double elevation,unitConvert;

    if(this->unitSelect->currentIndex()==0)
        unitConvert = 1.0/3.28084;
    else
        unitConvert = 1.0;

    this->currentXTideStation.clear();

    for(int i=0;i<response.length();i++)
    {
        tempString = response.value(i);
        tempString = tempString.simplified();
        tempList   = tempString.split(" ");

        if(tempList.length()!=5)
            continue;

        tempDate = tempList.value(0);
        tempTime = tempList.value(1);
        tempAMPM = tempList.value(2);
        tempElev = tempList.value(4);

        tempHour   = tempTime.split(":").value(0);
        tempMinute = tempTime.split(":").value(1);
        hour = tempHour.toInt();
        minute = tempMinute.toInt();

        if(tempAMPM=="PM")
            if(hour!=12)
                hour = hour + 12;
            else
                hour = 12;
        else
            if(hour==12)
                hour = 0;

        date      = QDate::fromString(tempDate,"yyyy-MM-dd");
        elevation = tempElev.toDouble();

        thisData.date.setDate(date);
        thisData.date.setTime(QTime(hour,minute));
        thisData.value = elevation*unitConvert;

        this->currentXTideStation.push_back(thisData);

    }

    return 0;
}

int XTide::getDataBounds(double &min, double &max)
{
    int j;

    min =  DBL_MAX;
    max =  -DBL_MAX;

    for(j=0;j<this->currentXTideStation.length();j++)
    {
        if(this->currentXTideStation[j].value<min)
            min = this->currentXTideStation[j].value;
        if(this->currentXTideStation[j].value>max)
            max = this->currentXTideStation[j].value;
    }
    return 0;
}


int XTide::plotChart()
{
    int i,ierr;
    double ymin,ymax;
    QString format;
    QDateTime minDateTime,maxDateTime,startDate,endDate;

    maxDateTime = QDateTime(QDate(1000,1,1),QTime(0,0,0));
    minDateTime = QDateTime(QDate(3000,1,1),QTime(0,0,0));

    startDate = this->startDateEdit->dateTime();
    endDate   = this->endDateEdit->dateTime();

    ierr = this->getDataBounds(ymin,ymax);

    if(this->unitSelect->currentIndex()==1)
        this->yLabel = "Water Surface Elevation (ft, MLLW)";
    else
        this->yLabel = "Water Surface Elevation (m, MLLW)";

    //...Create the chart
    this->thisChart = new QChart();
    this->chart->m_chart = this->thisChart;

    QLineSeries *series1 = new QLineSeries(this);
    series1->setName(this->currentStationName);
    series1->setPen(QPen(QColor(0,255,0),3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));

    minDateTime = this->startDateEdit->dateTime();
    maxDateTime = this->endDateEdit->dateTime().addDays(1);
    minDateTime.setTime(QTime(0,0,0));
    maxDateTime.setTime(QTime(0,0,0));

    QDateTimeAxis *axisX = new QDateTimeAxis(this);
    axisX->setTickCount(5);
    if(startDate.daysTo(endDate)>90)
        axisX->setFormat("MM/yyyy");
    else if(startDate.daysTo(endDate)>4)
        axisX->setFormat("MM/dd/yyyy");
    else
        axisX->setFormat("MM/dd/yyyy hh:mm");
    axisX->setTitleText("Date (GMT)");
    axisX->setTitleFont(QFont("Helvetica",10,QFont::Bold));
    axisX->setMin(minDateTime);
    axisX->setMax(maxDateTime);
    this->thisChart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis *axisY = new QValueAxis(this);
    axisY->setLabelFormat(format);
    axisY->setTitleText(this->yLabel);
    axisY->setTitleFont(QFont("Helvetica",10,QFont::Bold));
    axisY->setMin(ymin);
    axisY->setMax(ymax);
    this->thisChart->addAxis(axisY, Qt::AlignLeft);

    for(i=0;i<this->currentXTideStation.length();i++)
        series1->append(this->currentXTideStation[i].date.toMSecsSinceEpoch(),
                        this->currentXTideStation[i].value);
    this->thisChart->addSeries(series1);
    series1->attachAxis(axisX);
    series1->attachAxis(axisY);
    axisY->setTickCount(10);
    axisY->applyNiceNumbers();
    axisX->setGridLineColor(QColor(200,200,200));
    axisY->setGridLineColor(QColor(200,200,200));
    axisY->setShadesPen(Qt::NoPen);
    axisY->setShadesBrush(QBrush(QColor(240,240,240)));
    axisY->setShadesVisible(true);

    this->thisChart->setAnimationOptions(QChart::SeriesAnimations);
    this->thisChart->legend()->setAlignment(Qt::AlignBottom);
    this->thisChart->setTitle("XTide Station: "+this->currentStationName);
    this->thisChart->setTitleFont(QFont("Helvetica",14,QFont::Bold));
    this->chart->setRenderHint(QPainter::Antialiasing);
    this->chart->setChart(this->thisChart);

    this->chart->m_style = 1;
    this->chart->m_coord = new QGraphicsSimpleTextItem(this->thisChart);
    this->chart->m_coord->setPos(this->chart->size().width()/2 - 100, this->chart->size().height() - 20);
    this->chart->initializeAxisLimits();
    this->chart->setStatusBar(this->statusBar);

    foreach (QLegendMarker* marker, this->thisChart->legend()->markers())
    {
        // Disconnect possible existing connection to avoid multiple connections
        QObject::disconnect(marker, SIGNAL(clicked()), this->chart, SLOT(handleLegendMarkerClicked()));
        QObject::connect(marker, SIGNAL(clicked()), this->chart, SLOT(handleLegendMarkerClicked()));
    }


    return 0;
}


int XTide::saveXTideData(QString filename, QString format)
{
    QFile XTideOutput(filename);
    QTextStream Output(&XTideOutput);
    XTideOutput.open(QIODevice::WriteOnly);

    if(format.compare("CSV")==0)
    {
        Output << "Station: "+this->currentStationName.replace(" ","_")+"\n";
        Output << "Datum: MLLW\n";
        Output << "Units: N/A\n";
        Output << "\n";
        for(int i=0;i<this->currentXTideStation.length();i++)
        {
            Output << this->currentXTideStation[i].date.toString("MM/dd/yyyy")+","+
                      this->currentXTideStation[i].date.toString("hh:mm")+","+
                      QString::number(this->currentXTideStation[i].value)+"\n";
        }
    }
    else if(format.compare("IMEDS")==0)
    {
        Output << "% IMEDS generic format - Water Level\n";
        Output << "% year month day hour min sec value\n";
        Output << "XTide   UTC    MLLW\n";
        Output << "XTide_"+this->currentStationName.replace(" ","_")+"   "+QString::number(this->currentXTideLat)+
                  "   "+QString::number(this->currentXTideLon)+"\n";
        for(int i=0;i<this->currentXTideStation.length();i++)
        {
            Output << this->currentXTideStation[i].date.toString("yyyy")+"    "+
                        this->currentXTideStation[i].date.toString("MM")+"    "+
                        this->currentXTideStation[i].date.toString("dd")+"    "+
                        this->currentXTideStation[i].date.toString("hh")+"    "+
                        this->currentXTideStation[i].date.toString("mm")+"    "+
                                                        "00" +"    "+
                        QString::number(this->currentXTideStation[i].value)+"\n";
        }

    }
    XTideOutput.close();

    return 0;
}


int XTide::saveXTidePlot(QString filename, QString filter)
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

void XTide::javascriptDataReturned(QString data)
{

    int ierr;
    QString tempString;
    QStringList dataList;

    dataList = data.split(";");

    this->currentStationName = dataList.value(0);
    tempString               = dataList.value(1);
    this->currentXTideLon    = tempString.toDouble();
    tempString               = dataList.value(2);
    this->currentXTideLat    = tempString.toDouble();

    //...Sanity check on data
    if(this->currentStationName==QString() || this->currentStationName=="none")
    {
        emit xTideError("You must select a station");
        return;
    }


    //...Calculate the tidal signal
    ierr = this->calculateXTides();
    if(ierr!=0)
    {
        emit xTideError("There was an error calculation tides");
        return;
    }

    //...Plot the chart
    ierr = this->plotChart();
    if(ierr!=0)
    {
        emit xTideError("There was an error drawing the chart");
        return;
    }
}
