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
#include "xtide.h"

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


//...Overall routine for plotting XTide
int XTide::plotXTideStation()
{

    int ierr;

    //...Get the executables
    ierr = this->findXTideExe();
    if(ierr!=0)
        return -1;

    //...Get the selected station
    ierr = this->getClickedXTideStation();
    if(ierr!=0)
        return -1;

    //...Calculate the tidal signal
    ierr = this->calculateXTides();

    //...Plot the chart
    ierr = this->plotChart();

}



//...Get the XTide Station from the map
int XTide::getClickedXTideStation()
{
    QString tempString;
    QVariant eval = QVariant();
    this->map->page()->runJavaScript("returnStationID()",[&eval](const QVariant &v){eval = v;});
    while(eval.isNull())
        delayM(5);
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

//...Find the xTide executable
int XTide::findXTideExe()
{
    QString installLocation       = QApplication::applicationDirPath();
    QString buildLocationLinux    = QApplication::applicationDirPath()+"/../MetOceanViewer/mov_libs/bin";
    QString buildLocationWindows  = QApplication::applicationDirPath()+"/../../MetOceanViewer/mov_libs/bin";

    QFile location1(installLocation+"/tide");
    QFile location2(buildLocationLinux+"/tide");
    QFile location3(buildLocationWindows+"/tide.exe");
    QFile location4(buildLocationLinux+"/tide.exe");

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

    qDebug() << startDateString << endDateString;

    //...Build a calling string
    QString xTideCmd = this->xTideExe+
                       " -l \""+this->currentStationName+"\""+
                       " -b \""+startDateString+"\""+
                       " -e \""+endDateString+"\""+
                       " -s \"00:30\" -z -m m";

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HFILE_PATH",this->xTideHarmFile);
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
    QTime time;
    XTideStationData thisData;
    int hour,minute;
    double elevation;

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

        //thisData.date.setTimeSpec(Qt::UTC);
        thisData.date.setDate(date);
        thisData.date.setTime(QTime(hour,minute));
        thisData.value = elevation;

        qDebug() << thisData.date;

        this->currentXTideStation.push_back(thisData);

    }

    return 0;
}

int XTide::getDataBounds(double min, double max)
{
    int j;

    min =  DBL_MAX;
    max =  DBL_MIN;

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


    return 0;
}
