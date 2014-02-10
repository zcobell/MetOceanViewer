//---- LGPL --------------------------------------------------------------------
//
// Copyright (C)  ARCADIS, 2011-2013.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// contact: Zachary Cobell, zachary.cobell@arcadis-us.com
// ARCADIS
// 4999 Pearl East Circle, Suite 200
// Boulder, CO 80301
//
// All indications and logos of, and references to, "ARCADIS" 
// are registered trademarks of ARCADIS, and remain the property of
// ARCADIS. All rights reserved.
//
//------------------------------------------------------------------------------
// $Author$
// $Date$
// $Id$
// $HeadURL$
//------------------------------------------------------------------------------
//  File: ADCvalidator_main.cpp
//
//------------------------------------------------------------------------------
        
#include "ADCvalidator.h"
#include "ui_ADCvalidator_main.h"
#include "hwm.h"

QVector<QVector<double> > NOAAStations;
QVector<QString> NOAAStationNames;
QVector<QString> panToLocations;
QVector<NOAAStationData> CurrentStation;
QVariant MarkerID = -1;
QDateTime IMEDSMinDate,IMEDSMaxDate;
QVector<hwm_data> HighWaterMarks;
QString HighWaterMarkFile;
int ierr;

//Main routine which will intialize all the tabs
MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),ui(new Ui::MainWindow)
{

    //Setup UI
    ui->setupUi(this);

    //Load the NOAA tab and set accordingly
    QWebSettings::globalSettings()->setAttribute(QWebSettings::PluginsEnabled, true);
    ui->noaa_map->load(QUrl("qrc:/html/noaa_maps.html"));
    ui->noaa_map->page()->mainFrame()->setScrollBarPolicy(Qt::Horizontal,Qt::ScrollBarAlwaysOff);
    ui->noaa_map->page()->mainFrame()->setScrollBarPolicy(Qt::Vertical,Qt::ScrollBarAlwaysOff);
    QObject::connect(ui->noaa_map,SIGNAL(loadFinished(bool)),this,SLOT(BeginGatherStations()));
    ui->noaa_map->setContextMenuPolicy(Qt::CustomContextMenu);

    //Set the initial dates to today and today minus a day
    ui->Date_StartTime->setDateTime(QDateTime::currentDateTime().addDays(-1));
    ui->Date_EndTime->setDateTime(QDateTime::currentDateTime());

    //Set unselectable combo box items
    QStandardItemModel* model = qobject_cast<QStandardItemModel*>(ui->Combo_PanTo->model());
    QModelIndex firstIndex = model->index(3,ui->Combo_PanTo->modelColumn(),ui->Combo_PanTo->rootModelIndex());
    QStandardItem* firstItem = model->itemFromIndex(firstIndex);
    firstItem->setSelectable(false);

    //Set up the pan to combo box limits for the google map
    int NumItems = ui->Combo_PanTo->count();
    initializePanToLocations(NumItems);

    //Load the IMEDS tab and set accordingly
    QWebSettings::globalSettings()->setAttribute(QWebSettings::PluginsEnabled, true);
    ui->imeds_map->load(QUrl("qrc:/html/imeds_maps.html"));
    ui->imeds_map->page()->mainFrame()->setScrollBarPolicy(Qt::Horizontal,Qt::ScrollBarAlwaysOff);
    ui->imeds_map->page()->mainFrame()->setScrollBarPolicy(Qt::Vertical,Qt::ScrollBarAlwaysOff);
    ui->imeds_map->setContextMenuPolicy(Qt::CustomContextMenu);

    //Set the initial values for the IMEDS Data input tab
    ui->browse_ADCIMEDS->setEnabled(false);
    ui->browse_OBSIMEDS->setEnabled(false);
    ui->text_ADCIMEDS->setEnabled(false);
    ui->text_OBSIMEDS->setEnabled(false);
    ui->text_ADCIMEDS->setReadOnly(true);
    ui->text_OBSIMEDS->setReadOnly(true);
    ui->button_processIMEDS->setEnabled(false);
    ui->textbox_IMEDSstatus->setReadOnly(true);

    //Initialize Variables
    IMEDSMinDate.setDate(QDate(2900,1,1));
    IMEDSMaxDate.setDate(QDate(1820,1,1));

    //Load the High Water Mark Map and Regression Chart
    ui->map_hwm->load(QUrl("qrc:/html/hwm_map.html"));
    ui->map_hwm->page()->mainFrame()->setScrollBarPolicy(Qt::Horizontal,Qt::ScrollBarAlwaysOff);
    ui->map_hwm->page()->mainFrame()->setScrollBarPolicy(Qt::Vertical,Qt::ScrollBarAlwaysOff);
    ui->map_regression->load(QUrl("qrc:/html/reg_plot.html"));
    ui->map_regression->page()->mainFrame()->setScrollBarPolicy(Qt::Horizontal,Qt::ScrollBarAlwaysOff);
    ui->map_regression->page()->mainFrame()->setScrollBarPolicy(Qt::Vertical,Qt::ScrollBarAlwaysOff);

}

//Main destructor routine
MainWindow::~MainWindow()
{
    delete ui;
}

//----------------------------------------------------//
// ROUTINES                                           //
//                                                    //
// These are routines used within the main execution  //
//                                                    //
//----------------------------------------------------//

//Simple delay function which will pause execution for a number of seconds
void MainWindow::delay(int delayTime)
{
    QTime dieTime= QTime::currentTime().addSecs(delayTime);
    while( QTime::currentTime() < dieTime )
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}

//Routine that fires after the NOAA data is read from the server
void MainWindow::ReadNOAADataFinished(QNetworkReply *reply)
{
    QByteArray NOAAWebData;
    QString NOAAData,javastring;
    QString Datum,Units,Product;
    int ProductIndex;

    Units = ui->combo_noaaunits->currentText();
    ProductIndex = ui->combo_NOAAProduct->currentIndex();
    Product = retrieveProduct(1);

    if(ProductIndex == 0 || ProductIndex == 1)
    {
        if(Units=="metric")
            Units="m";
        else
            Units="ft";
        Datum = ui->combo_noaadatum->currentText();
    }
    else if(ProductIndex == 2 || ProductIndex == 5)
    {
        if(Units=="metric")
            Units="m/s";
        else
            Units="ft/s";
        Datum = "Stnd";
    }
    else if(ProductIndex == 3 || ProductIndex == 4)
    {
        if(Units=="metric")
            Units="Celcius";
        else
            Units="Fahrenheit";
        Datum = "Stnd";
    }
    else if(ProductIndex == 6)
    {
        Units = "%";
        Datum = "Stnd";
    }
    else if(ProductIndex == 7)
    {
        Units = "mb";
        Datum = "Stnd";
    }


    if(reply->error()!=0)
    {
        QMessageBox::information(this,"ERROR","ERROR: "+reply->errorString());
        return;
    }
    NOAAWebData=reply->readAll();
    NOAAData = FormatNOAAResponse(NOAAWebData);

    javastring="drawNOAAData("+NOAAData+",'"+Datum+"','"+Units+"','"+Product+"')";
    ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javastring);

}

//Routine that draws the markers on the NOAA map
void MainWindow::drawMarkers(bool DelayDraw)
{
    int i,ID;
    double X,Y;
    QVariant MyVar;
    QString javastring,StationName;

    //Make the requests for the all the stations
    ui->noaa_map->page()->mainFrame()->evaluateJavaScript("clearMarkers()");

    if(DelayDraw)
        delay(2); //Give the map a chance to set up

    for(i=0;i<NOAAStations[0].size();i++)
    {
        X  = NOAAStations[1][i];
        Y  = NOAAStations[2][i];
        ID = static_cast<int>(NOAAStations[0][i]);
        StationName = NOAAStationNames[i];
        //Plot a station
        javastring = "addNOAAStation("+QString::number(X)+","+QString::number(Y)+","+QString::number(ID)
                +","+QString::number(i)+",'"+StationName+"')";
        MyVar = ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javastring);
    }
    return;
}

//Routine that starts to read the list of NOAA station locations
void MainWindow::BeginGatherStations()
{
    int i;
    QString MyLine,TempString;
    QStringList MyList;
    QFile StationFile(":/data/NOAA_StationList.txt");

    if(!StationFile.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        QMessageBox::information(NULL,"ERROR",StationFile.errorString());
        return;
    }

    NOAAStations.resize(3);
    i = 0;
    while(!StationFile.atEnd())
    {
        i = i + 1;
        MyLine = StationFile.readLine().simplified();
        MyList = MyLine.split(";");
        NOAAStations[0].resize(i+1);
        NOAAStations[1].resize(i+1);
        NOAAStations[2].resize(i+1);
        NOAAStationNames.resize(i+1);
        TempString = MyList.value(0);
        NOAAStations[0][i] = TempString.toDouble();
        TempString = MyList.value(1);
        NOAAStations[1][i] = TempString.toDouble();
        TempString = MyList.value(2);
        NOAAStations[2][i] = TempString.toDouble();
        NOAAStationNames[i] = MyList.value(3);
    }
    StationFile.close();
    drawMarkers(true);
    return;
}

//Routine that formats the response from the NOAA server in CSV
QString MainWindow::FormatNOAAResponse(QByteArray Input)
{
    int i;
    QString Output,TempData,DateS,YearS,MonthS,DayS,HourMinS,HourS,MinS,WLS;
    QString InputData(Input);
    QStringList DataList = InputData.split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
    QStringList TimeSnap;

    Output = "'";
    CurrentStation.resize(DataList.length()-1);
    for(i=1;i<DataList.length();i++)
    {
        TempData = DataList.value(i);
        TimeSnap = TempData.split(",");
        DateS    = TimeSnap.value(0);
        YearS    = DateS.mid(0,4);
        MonthS   = DateS.mid(5,2);
        DayS     = DateS.mid(8,2);
        HourMinS = DateS.mid(11,5);
        HourS    = HourMinS.mid(0,2);
        MinS     = HourMinS.mid(3,2);
        WLS      = TimeSnap.value(1);
        Output=Output+YearS+":"+MonthS+":"+
               DayS+":"+HourS+":"+MinS+":"+WLS+";";
        TempData = YearS+"/"+MonthS+"/"+DayS;
        CurrentStation[i-1].Date.setDate(YearS.toInt(),MonthS.toInt(),DayS.toInt());
        CurrentStation[i-1].Time = QTime(HourS.toInt(),MinS.toInt(),0);
        CurrentStation[i-1].value = WLS.toDouble();
    }
    Output = Output+"'";

    return Output;
}

//Routine to remove the leading path of a filename for a simpler
// display in a text box
QString MainWindow::RemoveLeadingPath(QString Input)
{
    QRegExp rx("[/\\\\]");
    QStringList parts = Input.split(rx);
    QString Output = parts.value(parts.length()-1);
    return Output;
}

//Adds the dual plot data for a time series validation station to the map
void MainWindow::addIMEDSandADCIRCMarker(IMEDS Observation, IMEDS ADCIRC)
{
    QVector<QString> MarkerCalls;
    QString TempString,DateString,RangeString;
    QDateTime TempStart1,TempStart2,TempEnd1,TempEnd2,StartData,EndData;
    int i,j;
    bool hasData;
    double ymin,ymax;

    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("clearMarkers()");
    MarkerCalls.resize(ADCIRC.nstations);

    if(ui->check_imedyauto->isChecked())
        RangeString="'auto'";
    else
    {
        ymin = ui->spin_imedsymin->value();
        ymax = ui->spin_imedsymax->value();
        RangeString="'"+QString::number(ymin)+":"+QString::number(ymax)+"'";
    }

    for(i=0;i<ADCIRC.nstations;i++)
    {

        if(ui->check_imedsalldata->isChecked())
        {
            getStartEndTime(Observation,i,TempStart1,TempEnd1);
            getStartEndTime(ADCIRC,i,TempStart2,TempEnd2);
            if(TempStart1<TempStart2)
                StartData = TempStart1;
            else
                StartData = TempStart2;
            if(TempEnd1>TempEnd2)
                EndData = TempEnd1;
            else
                EndData = TempEnd2;

            DateString = "'"+StartData.toString("yyyy:MM:dd::hh:mm:ss")+"','"+
                    EndData.toString("yyyy:MM:dd::hh:mm:ss")+"'";
        }
        else
        {
            StartData.setDate(ui->date_imedsstart->date());
            EndData.setDate(ui->date_imedsend->date());
            StartData.setTime(QTime(0,0,0));
            EndData.setTime(QTime(23,59,59));
            DateString = "'"+StartData.toString("yyyy:MM:dd::hh:mm:ss")+"','"+
                    EndData.toString("yyyy:MM:dd::hh:mm:ss")+"'";
        }

        MarkerCalls[i]="addValidationStation("+QString::number(ADCIRC.station[i].longitude)+","
                +QString::number(ADCIRC.station[i].latitude)+","
                +QString::number(ADCIRC.station[i].StationIndex)+",'"
                +ADCIRC.station[i].StationName+"',"+DateString+","
                +RangeString+",'";
        TempString = QString();
        hasData=false;
        for(j=0;j<ADCIRC.station[i].NumSnaps;j++)
        {
            if(ADCIRC.station[i].date[j].operator >(StartData) &&
                 ADCIRC.station[i].date[j].operator <(EndData))
            {
                if(ADCIRC.station[i].data[j]>-999)
                    hasData=true;
                TempString = ADCIRC.station[i].date[j].toString("yyyy:MM:dd:hh:mm");
                TempString = TempString+":"+QString::number(ADCIRC.station[i].data[j]);
                MarkerCalls[i]=MarkerCalls[i]+TempString+";";
            }
        }
        MarkerCalls[i]=MarkerCalls[i]+"','";
        TempString = QString();
        for(j=0;j<Observation.station[i].NumSnaps;j++)
        {
            if(Observation.station[i].date[j].operator >(StartData) &&
                 Observation.station[i].date[j].operator <(EndData))
            {
                if(Observation.station[i].data[j]>-999)
                    hasData=true;
                TempString = Observation.station[i].date[j].toString("yyyy:MM:dd:hh:mm");
                TempString = TempString+":"+QString::number(Observation.station[i].data[j]);
                MarkerCalls[i]=MarkerCalls[i]+TempString+";";
            }
        }

        MarkerCalls[i]=MarkerCalls[i]+"')";
        if(ui->check_ignoreNodata->isChecked() && hasData==false)
        {
            hasData=false;
        }
        else
        {
            ui->imeds_map->page()->mainFrame()->evaluateJavaScript(MarkerCalls[i]);
        }

        double complete = (static_cast<double>(i) / (static_cast<double>(ADCIRC.nstations)-1))*100.0;
        ui->progress_IMEDS->setValue(static_cast<int>(complete));
    }
    ui->subtab_IMEDS->setCurrentIndex(1);
    delay(1);
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("fitMarkers()");
    ui->progress_IMEDS->setValue(0);

    return;

}

//Adds a single ADCIRC measurement to the plot
void MainWindow::addADCIRCMarker(IMEDS ADCIRC)
{
    QVector<QString> MarkerCalls;
    QString TempString,DateString,RangeString;
    int i,j;
    double ymin,ymax;
    bool hasData;
    QDateTime StartData,EndData;

    if(ui->check_imedyauto->isChecked())
        RangeString="'auto'";
    else
    {
        ymin = ui->spin_imedsymin->value();
        ymax = ui->spin_imedsymax->value();
        RangeString="'"+QString::number(ymin)+":"+QString::number(ymax)+"'";
    }

    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("clearMarkers()");
    MarkerCalls.resize(ADCIRC.nstations);
    for(i=0;i<ADCIRC.nstations;i++)
    {
        if(ui->check_imedsalldata->isChecked())
        {
            getStartEndTime(ADCIRC,i,StartData,EndData);
            DateString = "'"+StartData.toString("yyyy:MM:dd::hh:mm:ss")+"','"+
                    EndData.toString("yyyy:MM:dd::hh:mm:ss")+"'";
        }
        else
        {
            StartData.setDate(ui->date_imedsstart->date());
            EndData.setDate(ui->date_imedsend->date());
            StartData.setTime(QTime(0,0,0));
            EndData.setTime(QTime(23,59,59));
            DateString = "'"+StartData.toString("yyyy:MM:dd::hh:mm:ss")+"','"+
                    EndData.toString("yyyy:MM:dd::hh:mm:ss")+"'";
        }

        MarkerCalls[i]="addIMEDSStation("+QString::number(ADCIRC.station[i].longitude)+","
                +QString::number(ADCIRC.station[i].latitude)+","
                +QString::number(ADCIRC.station[i].StationIndex)+",'"
                +ADCIRC.station[i].StationName+"',"+DateString+","
                +RangeString+",'";
        TempString = QString();
        hasData=false;
        for(j=0;j<ADCIRC.station[i].NumSnaps;j++)
        {
            if(ADCIRC.station[i].date[j].operator >(StartData) &&
                 ADCIRC.station[i].date[j].operator <(EndData))
            {
                if(ADCIRC.station[i].data[j]>-999)
                    hasData=true;
                TempString = ADCIRC.station[i].date[j].toString("yyyy:MM:dd:hh:mm");
                TempString = TempString+":"+QString::number(ADCIRC.station[i].data[j]);
                MarkerCalls[i]=MarkerCalls[i]+TempString+";";
            }
        }
        MarkerCalls[i]=MarkerCalls[i]+"','ADCIRC')";

        if(ui->check_ignoreNodata->isChecked() && hasData==false)
        {
            hasData=false;
        }
        else
        {
            ui->imeds_map->page()->mainFrame()->evaluateJavaScript(MarkerCalls[i]);
        }

        double complete = (static_cast<double>(i) / (static_cast<double>(ADCIRC.nstations)-1))*100.0;
        ui->progress_IMEDS->setValue(static_cast<int>(complete));
    }
    ui->subtab_IMEDS->setCurrentIndex(1);
    delay(1);
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("fitMarkers()");
    ui->progress_IMEDS->setValue(0);

    return;
}

//Adds a single IMEDS station to the plot
void MainWindow::addIMEDSMarker(IMEDS Observation)
{
    QVector<QString> MarkerCalls;
    QString TempString,DateString,RangeString;
    QDateTime StartData,EndData;
    int i,j;
    double ymin,ymax;
    bool hasData;

    if(ui->check_imedyauto->isChecked())
        RangeString="'auto'";
    else
    {
        ymin = ui->spin_imedsymin->value();
        ymax = ui->spin_imedsymax->value();
        RangeString="'"+QString::number(ymin)+":"+QString::number(ymax)+"'";
    }

    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("clearMarkers()");
    MarkerCalls.resize(Observation.nstations);
    for(i=0;i<Observation.nstations;i++)
    {
        if(ui->check_imedsalldata->isChecked())
        {
            getStartEndTime(Observation,i,StartData,EndData);
            DateString = "'"+StartData.toString("yyyy:MM:dd::hh:mm:ss")+"','"+
                    EndData.toString("yyyy:MM:dd::hh:mm:ss")+"'";
        }
        else
        {
            StartData.setDate(ui->date_imedsstart->date());
            EndData.setDate(ui->date_imedsend->date());
            StartData.setTime(QTime(0,0,0));
            EndData.setTime(QTime(23,59,59));
            DateString = "'"+StartData.toString("yyyy:MM:dd::hh:mm:ss")+"','"+
                    EndData.toString("yyyy:MM:dd::hh:mm:ss")+"'";
        }
        MarkerCalls[i]="addIMEDSStation("+QString::number(Observation.station[i].longitude)+","
                +QString::number(Observation.station[i].latitude)+","
                +QString::number(Observation.station[i].StationIndex)+",'"
                +Observation.station[i].StationName+"',"+DateString+","
                +RangeString+",'";
        TempString = QString();
        hasData = false;
        for(j=0;j<Observation.station[i].NumSnaps;j++)
        {
            if(Observation.station[i].date[j].operator >(StartData) &&
                 Observation.station[i].date[j].operator <(EndData))
            {
                if(Observation.station[i].data[j]>-999)
                    hasData=true;
                TempString = Observation.station[i].date[j].toString("yyyy:MM:dd:hh:mm");
                TempString = TempString+":"+QString::number(Observation.station[i].data[j]);
                MarkerCalls[i]=MarkerCalls[i]+TempString+";";
            }
        }
        MarkerCalls[i]=MarkerCalls[i]+"','Observation')";

        if(ui->check_ignoreNodata->isChecked() && hasData==false)
        {
            hasData=false;
        }
        else
        {
            ui->imeds_map->page()->mainFrame()->evaluateJavaScript(MarkerCalls[i]);
        }

        double complete = (static_cast<double>(i) / (static_cast<double>(Observation.nstations)-1))*100.0;
        ui->progress_IMEDS->setValue(static_cast<int>(complete));

    }
    ui->subtab_IMEDS->setCurrentIndex(1);
    delay(1);
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("fitMarkers()");
    ui->progress_IMEDS->setValue(0);

    return;
}

//Determines the start and end time in an IMEDS variable
void MainWindow::getStartEndTime(IMEDS Input, int index, QDateTime &Start, QDateTime &End)
{
    Start.setDate(QDate(2200,1,1));
    End.setDate(QDate(1820,1,1));
    Start.setTime(QTime(0,0,0));
    End.setTime(QTime(0,0,0));
    for(int i=0;i<Input.station[index].NumSnaps;i++)
    {
        if(Start.operator >(Input.station[index].date[i]))
        {
            Start = Input.station[index].date[i];
        }
        if(End.operator <(Input.station[index].date[i]))
        {
            End = Input.station[index].date[i];
        }
    }
    return;
}

//Determines the start and end time in a IMEDS vector
void MainWindow::getGlobalStartEndTime(IMEDS Input, QDateTime &Start, QDateTime &End)
{
    Start.setDate(QDate(2200,1,1));
    End.setDate(QDate(1820,1,1));
    Start.setTime(QTime(0,0,0));
    End.setTime(QTime(0,0,0));
    for(int index=0;index<Input.nstations;index++)
    {
        for(int i=0;i<Input.station[index].NumSnaps;i++)
        {
            if(Start.operator >(Input.station[index].date[i]))
            {
                Start = Input.station[index].date[i];
            }
            if(End.operator <(Input.station[index].date[i]))
            {
                End = Input.station[index].date[i];
            }
        }
    }
    return;
}

//Initializes the locations for the pan to box
void MainWindow::initializePanToLocations(int size)
{
    panToLocations.resize(size);
    //Locations in the select box to pan to
    panToLocations[0]  = "-89.33,24.73,6";  //Gulf of Mexico
    panToLocations[1]  = "-72.45,36.05,5";  //Atlantic Ocean
    panToLocations[2]  = "-141.24,40.46,4"; //Pacific Ocean
    panToLocations[3]  = "0,0,0";           //-----------------
    panToLocations[4]  = "-87.49,30.71,9";  //Alabama
    panToLocations[5]  = "-152.75,64.00,4"; //Alaska
    panToLocations[6]  = "-121.10,37.36,6"; //California
    panToLocations[7]  = "-72.82,41.43,9";  //Connecticut
    panToLocations[8]  = "-75.08,39.11,9";  //Deleware
    panToLocations[9]  = "-77.02,38.88,13"; //Washington, DC
    panToLocations[10] = "-83.77,28.08,13"; //Florida
    panToLocations[11] = "-80.67,31.67,8";  //Georgia
    panToLocations[12] = "-157.40,20.67,7"; //Hawaii
    panToLocations[13] = "-91.5,29.5,8";    //Louisiana
    panToLocations[14] = "-68.69,44.07,8";  //Maine
    panToLocations[15] = "-76.18,38.41,8";  //Maryland
    panToLocations[16] = "-70.73,42.12,9";  //Massachusettes
    panToLocations[17] = "-87.95,30.46,10"; //Mississippi
    panToLocations[18] = "-70.73,42.98,11"; //New Hampshire
    panToLocations[19] = "-73.43,40.15,8";  //New Jersey
    panToLocations[20] = "-73.16,41.11,9";  //New York
    panToLocations[21] = "-76.79,32.25,8";  //North Carolina
    panToLocations[22] = "-122.99,44.41,7"; //Oregon
    panToLocations[23] = "-66.43,18.22,9";  //Puerto Rico
    panToLocations[24] = "-71.25,41.58,10"; //Rhode Island
    panToLocations[25] = "-79.67,33.21,8";  //South Carolina
    panToLocations[26] = "-94.72,28.55,7";  //Texas
    panToLocations[27] = "-121.81,47.55,7"; //Washington
    panToLocations[28] = "-76.29,37.85,8";  //Virginia
    return;
}

QString MainWindow::retrieveProduct(int type)
{
    QString Product;
    int index = ui->combo_NOAAProduct->currentIndex();
    if(type==1)
    {
        switch(index)
        {
            case(0):Product = "Observed Water Level"; break;
            case(1):Product = "Predicted Water Level"; break;
            case(2):Product = "Current Speed"; break;
            case(3):Product = "Air Temperature"; break;
            case(4):Product = "Water Temperature"; break;
            case(5):Product = "Wind Speed"; break;
            case(6):Product = "Relative Humidity"; break;
            case(7):Product = "Air Pressure"; break;
        }
    }
    else if(type==2)
    {
        switch(index)
        {
            case(0):Product = "water_level"; break;
            case(1):Product = "predictions"; break;
            case(2):Product = "currents"; break;
            case(3):Product = "air_temperature"; break;
            case(4):Product = "water_temperature"; break;
            case(5):Product = "wind"; break;
            case(6):Product = "humidity"; break;
            case(7):Product = "air_pressure"; break;
        }
    }
    return Product;
}

//Classifies high water marks into classes for coloring
int MainWindow::ClassifyHWM(double diff)
{
    int color;
    int units;
    QVector<double> classes;
    //This is the default classification
    classes.resize(7);

    units = ui->combo_hwmunits->currentIndex();
    if(ui->check_manualHWM->isChecked())
    {
        classes[0] = ui->spin_class0->value();
        classes[1] = ui->spin_class1->value();
        classes[2] = ui->spin_class2->value();
        classes[3] = ui->spin_class3->value();
        classes[4] = ui->spin_class4->value();
        classes[5] = ui->spin_class5->value();
        classes[6] = ui->spin_class6->value();
    }
    else
    {
        if(units==1)
        {
            classes[0] = -1.5;
            classes[1] = -1.0;
            classes[2] = -0.5;
            classes[3] =  0.0;
            classes[4] =  0.5;
            classes[5] =  1.0;
            classes[6] =  1.5;
        }
        else
        {
            classes[0] = -5.0;
            classes[1] = -3.5;
            classes[2] = -1.5;
            classes[3] =  0.0;
            classes[4] =  1.5;
            classes[5] =  3.5;
            classes[6] =  5.0;
        }
    }

    ui->spin_class0->setValue(classes[0]);
    ui->spin_class1->setValue(classes[1]);
    ui->spin_class2->setValue(classes[2]);
    ui->spin_class3->setValue(classes[3]);
    ui->spin_class4->setValue(classes[4]);
    ui->spin_class5->setValue(classes[5]);
    ui->spin_class6->setValue(classes[6]);

    if(diff<classes[0])
        color=1;
    else if(diff<classes[1])
        color=2;
    else if(diff<classes[2])
        color=3;
    else if(diff<classes[3])
        color=4;
    else if(diff<classes[4])
        color=5;
    else if(diff<classes[5])
        color=6;
    else if(diff<classes[6])
        color=7;
    else
        color=8;

    return color;
}

//----------------------------------------------------//
// ACTIONS                                            //
//                                                    //
// These are routines that are called upon a user     //
// interaction with the GUI                           //
//                                                    //
//----------------------------------------------------//

//Called when the pan to combo box is updated
void MainWindow::on_Combo_PanTo_currentIndexChanged(int index)
{
    //Call the javascript code to reposition the google map center and zoom
    double x,y,z;
    QString data,xS,yS,zS;
    data = panToLocations.value(index);
    QStringList List = data.split(",");
    xS = List.value(0);
    yS = List.value(1);
    zS = List.value(2);
    x = xS.toDouble();
    y = yS.toDouble();
    z = zS.toDouble();
    QString javastring = "panTo("+QString::number(x)+","+QString::number(y)+","+QString::number(z)+")";
    ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javastring);
}

//Called when the user attempts to save the NOAA chart
void MainWindow::on_button_savechart_clicked()
{
    QString filter = "JPG (*.jpg *.jpeg)";
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",
                "","JPG (*.jpg *.jpeg)",&filter);

    if(Filename==NULL)
        return;

    QFile NOAAOutput(Filename);
    QPixmap NOAAImage(ui->noaa_map->size());
    ui->noaa_map->render(&NOAAImage);
    NOAAOutput.open(QIODevice::WriteOnly);
    NOAAImage.save(&NOAAOutput,"JPG",100);

}

//Called when the user tries to save the NOAA data
void MainWindow::on_button_savedata_clicked()
{

    if(MarkerID==-1)
    {
        QMessageBox::information(this,"ERROR","No Station has been selected.");
        return;
    }
    QVariant MarkerID2 = ui->noaa_map->page()->mainFrame()->evaluateJavaScript("returnStationID()");
    if(MarkerID != MarkerID2)
    {
        QMessageBox::information(this,"ERROR","The currently selected station is not the data loaded.");
        return;
    }

    QString filter;
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...","","CSV (*.csv);;IMEDS (*.imeds)",&filter);
    QStringList filter2 = filter.split(" ");
    QString format = filter2.value(0);

    if(Filename == NULL)
        return;

    QFile NOAAOutput(Filename);
    QTextStream Output(&NOAAOutput);
    NOAAOutput.open(QIODevice::WriteOnly);

    if(format.compare("CSV")==0)
    {
        Output << "Station: "+MarkerID.toString()+"\n";
        Output << "Datum: "+ui->combo_noaadatum->currentText()+"\n";
        Output << "Units: "+ui->combo_noaaunits->currentText()+"\n";
        Output << "\n";
        for(int i=0;i<CurrentStation.size();i++)
        {
            Output << CurrentStation[i].Date.toString("MM/dd/yyyy")+","+
                      CurrentStation[i].Time.toString("hh:mm")+","+
                      QString::number(CurrentStation[i].value)+"\n";
        }
    }
    else if(format.compare("IMEDS")==0)
    {
        //Find the station index from the station ID
        double Lat,Lon;
        Lat = -9999;
        Lon = -9999;
        for(int i=0;i<NOAAStations[0].size();i++)
        {
            if(MarkerID.toDouble()==NOAAStations[0][i])
            {
                Lat = NOAAStations[2][i];
                Lon = NOAAStations[1][i];
            }
        }

        QString datum = ui->combo_noaadatum->currentText();
        QString units = ui->combo_noaaunits->currentText();
        QString units2;
        if(units.compare("metric")==0)
        {
            units = "meters";
            units2 = "m";
        }
        else
        {
            units = "feet";
            units2 = "ft";
        }
        Output << "% IMEDS generic format - Water Level\n";
        Output << "% year month day hour min sec watlev("+units2+")\n";
        Output << "NOAA    UTC    "+datum+"\n";
        Output << "NOAA_"+MarkerID.toString()+"   "+QString::number(Lat)+"   "+QString::number(Lon)+"\n";
        for(int i=0;i<CurrentStation.size();i++)
        {
            Output << CurrentStation[i].Date.toString("yyyy")+"    "+
                        CurrentStation[i].Date.toString("MM")+"    "+
                        CurrentStation[i].Date.toString("dd")+"    "+
                        CurrentStation[i].Time.toString("hh")+"    "+
                        CurrentStation[i].Time.toString("mm")+"    "+
                                                        "00" +"    "+
                        QString::number(CurrentStation[i].value)+"\n";
        }

    }
    NOAAOutput.close();

}

//Called when the user tries to save the imeds data
void MainWindow::on_button_saveIMEDSData_clicked()
{
    return;

    QString Filename = QFileDialog::getSaveFileName(this,"Save as...","","CSV (*.csv)");
    if(Filename==NULL)
        return;

    QFile OutputFile(Filename);
    OutputFile.open(QIODevice::WriteOnly);

}

//Called when the user tries to save the IMEDS image
void MainWindow::on_button_saveIMEDSImage_clicked()
{
    QString filter = "JPG (*.jpg *.jpeg)";
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",
                "","JPG (*.jpg *.jpeg)",&filter);

    if(Filename==NULL)
        return;

    QFile IMEDSOutput(Filename);
    QPixmap IMEDSImage(ui->imeds_map->size());
    ui->imeds_map->render(&IMEDSImage);
    IMEDSOutput.open(QIODevice::WriteOnly);
    IMEDSImage.save(&IMEDSOutput,"JPG",100);

}

//Called when the user checks the imeds auto y axis box
void MainWindow::on_check_imedyauto_toggled(bool checked)
{
    ui->spin_imedsymin->setEnabled(!checked);
    ui->spin_imedsymax->setEnabled(!checked);
}

//Called when the browse for HWM file button is clicked
void MainWindow::on_browse_hwm_clicked()
{
    HighWaterMarkFile = QFileDialog::getOpenFileName(this,"Select High Water Mark File","","Shintaro Style High Water Mark File (*.csv) ;; All Files (*.*)");
    if(HighWaterMarkFile==NULL)
        return;

    QString TempString = RemoveLeadingPath(HighWaterMarkFile);
    ui->Text_HWMFile->setText(TempString);
}

//Called when the process HWM button is clicked
void MainWindow::on_button_processHWM_clicked()
{
    QString Marker, unitString, Regression, MeasuredString, ModeledString;
    double x,y,measurement,modeled,error,M,R;
    double c0,c1,c2,c3,c4,c5,c6;
    int classification, unit;


    ierr = ReadHWMData(HighWaterMarkFile, HighWaterMarks);
    ierr = ComputeLinearRegression(HighWaterMarks, M, R);
    unit = ui->combo_hwmunits->currentIndex();
    if(unit==0)
        unitString = "'ft'";
    else
        unitString = "'m'";

    ui->map_hwm->page()->mainFrame()->evaluateJavaScript("clearMarkers()");

    //Sanity check on classes
    if(ui->check_manualHWM->isChecked())
    {
        c0 = ui->spin_class0->value();
        c1 = ui->spin_class1->value();
        c2 = ui->spin_class2->value();
        c3 = ui->spin_class3->value();
        c4 = ui->spin_class4->value();
        c5 = ui->spin_class5->value();
        c6 = ui->spin_class6->value();

        if(c1<=c0)
        {
            QMessageBox::information(this,"ERROR","Your classifications are invalid.");
            return;
        }
        else if(c2<=c1)
        {
            QMessageBox::information(this,"ERROR","Your classifications are invalid.");
            return;
        }
        else if(c3<=c2)
        {
            QMessageBox::information(this,"ERROR","Your classifications are invalid.");
            return;
        }
        else if(c4<=c3)
        {
            QMessageBox::information(this,"ERROR","Your classifications are invalid.");
            return;
        }
        else if(c5<=c4)
        {
            QMessageBox::information(this,"ERROR","Your classifications are invalid.");
            return;
        }
        else if(c6<=c5)
        {
            QMessageBox::information(this,"ERROR","Your classifications are invalid.");
            return;
        }
    }

    //Plot the high water mark map
    MeasuredString = "";
    ModeledString = "";
    for(int i=0;i<HighWaterMarks.size();i++)
    {
        x = HighWaterMarks[i].lon;
        y = HighWaterMarks[i].lat;
        measurement = HighWaterMarks[i].measured;
        modeled = HighWaterMarks[i].modeled;
        error = HighWaterMarks[i].error;

        classification = ClassifyHWM(error);

        Marker = "addHWM("+QString::number(x)+","+QString::number(y)+
                ","+QString::number(i)+","+QString::number(modeled)+","+QString::number(measurement)+
                ","+QString::number(error)+","+QString::number(classification)+","+unitString+
                ")";
        ui->map_hwm->page()->mainFrame()->evaluateJavaScript(Marker);
        if(i==0)
        {
            ModeledString = QString::number(modeled);
            MeasuredString = QString::number(measurement);
        }
        else
        {
            ModeledString = ModeledString+":"+QString::number(modeled);
            MeasuredString = MeasuredString+":"+QString::number(measurement);
        }
    }

    ui->label_class2->setText(QString::number(ui->spin_class0->value()));
    ui->label_class3->setText(QString::number(ui->spin_class1->value()));
    ui->label_class4->setText(QString::number(ui->spin_class2->value()));
    ui->label_class5->setText(QString::number(ui->spin_class3->value()));
    ui->label_class6->setText(QString::number(ui->spin_class4->value()));
    ui->label_class7->setText(QString::number(ui->spin_class5->value()));
    ui->label_class8->setText(QString::number(ui->spin_class6->value()));

    ui->subtab_hwm->setCurrentIndex(2);
    Regression = "plotRegression('"+ModeledString+"','"+MeasuredString+"',"+
            unitString+")";
    ui->map_regression->page()->mainFrame()->evaluateJavaScript(Regression);
    ui->label_slope->setText(QString::number(M));
    ui->label_r2->setText(QString::number(R));

    ui->subtab_hwm->setCurrentIndex(1);
    delay(1);
    ui->map_hwm->page()->mainFrame()->evaluateJavaScript("fitMarkers()");

    return;

}

//Called when the manual HWM scale button is clicked
void MainWindow::on_check_manualHWM_toggled(bool checked)
{
    ui->spin_class0->setEnabled(checked);
    ui->spin_class1->setEnabled(checked);
    ui->spin_class2->setEnabled(checked);
    ui->spin_class3->setEnabled(checked);
    ui->spin_class4->setEnabled(checked);
    ui->spin_class5->setEnabled(checked);
    ui->spin_class6->setEnabled(checked);

}

//Called when the save HWM map image button is clicked
void MainWindow::on_button_saveHWMMap_clicked()
{
    QString filter = "JPG (*.jpg *.jpeg)";
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",
                "","JPG (*.jpg *.jpeg)",&filter);

    if(Filename==NULL)
        return;

    QFile HWMOutput(Filename);
    QPixmap HWMImage(ui->map_hwm->size());
    ui->map_hwm->render(&HWMImage);
    HWMOutput.open(QIODevice::WriteOnly);
    HWMImage.save(&HWMOutput,"JPG",100);

}

//Called when the save regression image button is clicked
void MainWindow::on_button_saveregression_clicked()
{
    QString filter = "JPG (*.jpg *.jpeg)";
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",
                "","JPG (*.jpg *.jpeg)",&filter);

    if(Filename==NULL)
        return;

    QFile HWMOutput(Filename);
    QPixmap HWMImage(ui->map_regression->size());
    ui->map_regression->render(&HWMImage);
    HWMOutput.open(QIODevice::WriteOnly);
    HWMImage.save(&HWMOutput,"JPG",100);
}

//Called when the all date range imeds check box is toggled
void MainWindow::on_check_imedsalldata_toggled(bool checked)
{
    ui->date_imedsstart->setEnabled(!checked);
    ui->date_imedsend->setEnabled(!checked);
}

//Called when the process IMEDS button is clicked
void MainWindow::on_button_processIMEDS_clicked()
{
    bool ReadData1 = false;
    bool ReadData2 = false;
    int i;

    this->setCursor(Qt::WaitCursor);
    ui->textbox_IMEDSstatus->clear();
    if(ADCIMEDSFile!=NULL && ui->check_ADCIMEDS->checkState()==2)
    {
        ui->textbox_IMEDSstatus->appendPlainText("Processing ADCIRC IMEDS Data...");
        if(ADCIMEDS.success)
        {
            ui->textbox_IMEDSstatus->appendPlainText("Successfully processed ADCIRC IMEDS Data!\n\n");
            ReadData1 = true;
        }
        else
        {
            ui->textbox_IMEDSstatus->appendPlainText("ERROR processing ADCIRC IMEDS Data!");
        }
    }
    if(OBSIMEDSFile!=NULL && ui->check_OBSIMEDS->checkState()==2)
    {
        ui->textbox_IMEDSstatus->appendPlainText("Processing Observation IMEDS Data...");
        if(OBSIMEDS.success)
        {
            ui->textbox_IMEDSstatus->appendPlainText("Successfully processed Observation IMEDS Data!\n\n");
            ReadData2 = true;
        }
        else
        {
            ui->textbox_IMEDSstatus->appendPlainText("ERROR processing Observation IMEDS Data!\n\n");
        }
    }

    if(ReadData1 && ReadData2)
    {
        if(OBSIMEDS.nstations!=ADCIMEDS.nstations)
        {
            ui->textbox_IMEDSstatus->appendPlainText("ERROR: Datasets are not equivelent!\n\n");
        }
        for(i=0;i<OBSIMEDS.nstations;i++)
        {
            if(OBSIMEDS.station[i].latitude!=ADCIMEDS.station[i].latitude &&
                    OBSIMEDS.station[i].longitude!=ADCIMEDS.station[i].longitude)
            {
                ui->textbox_IMEDSstatus->appendPlainText(
                            "ERROR: Station Index "+QString::number(i+1)+
                            " not at same location in both files!");
            }
        }
        addIMEDSandADCIRCMarker(OBSIMEDS,ADCIMEDS);
    }
    else if(ReadData1 && !ReadData2)
    {
        addADCIRCMarker(ADCIMEDS);
    }
    else if(ReadData2 && !ReadData1)
    {
        addIMEDSMarker(OBSIMEDS);
    }
    this->setCursor(Qt::ArrowCursor);

}

//Called when the browse observation IMEDS data button is clicked
void MainWindow::on_browse_OBSIMEDS_clicked()
{
    QDateTime TempStartDate,TempEndDate;
    OBSIMEDSFile = QFileDialog::getOpenFileName(this,"Select Observation IMEDS File",
                                                "","IMEDS File (*.imeds *.IMEDS) ;; All Files (*.*)");
    if(OBSIMEDSFile!=NULL)
    {
        this->setCursor(Qt::WaitCursor);
        ui->text_OBSIMEDS->setText(RemoveLeadingPath(OBSIMEDSFile));
        ui->text_OBSIMEDS->setToolTip(OBSIMEDSFile);
        ui->button_processIMEDS->setEnabled(true);
        OBSIMEDS = readIMEDS(OBSIMEDSFile);
        getGlobalStartEndTime(OBSIMEDS,TempStartDate,TempEndDate);
        if(TempStartDate.operator <(IMEDSMinDate))
            IMEDSMinDate = TempStartDate;
        if(TempEndDate.operator >(IMEDSMaxDate))
            IMEDSMaxDate = TempEndDate;
        ui->date_imedsstart->setDateTime(IMEDSMinDate);
        ui->date_imedsend->setDateTime(IMEDSMaxDate);
        ui->date_imedsstart->setMaximumDateTime(IMEDSMaxDate);
        ui->date_imedsend->setMaximumDateTime(IMEDSMaxDate);
        ui->date_imedsstart->setMinimumDateTime(IMEDSMinDate);
        ui->date_imedsend->setMinimumDateTime(IMEDSMinDate);
        this->setCursor(Qt::ArrowCursor);
    }
}

//Called when the browse adcirc IMEDS data button is clicked
void MainWindow::on_browse_ADCIMEDS_clicked()
{
    QDateTime TempStartDate,TempEndDate;
    ADCIMEDSFile = QFileDialog::getOpenFileName(this,"Select ADCIRC IMEDS File",
                                                "","IMEDS File (*.imeds *.IMEDS) ;; All Files (*.*)");
    if(ADCIMEDSFile!=NULL)
    {
        this->setCursor(Qt::WaitCursor);
        ui->text_ADCIMEDS->setText(RemoveLeadingPath(ADCIMEDSFile));
        ui->text_ADCIMEDS->setToolTip(ADCIMEDSFile);
        ui->button_processIMEDS->setEnabled(true);
        ADCIMEDS = readIMEDS(ADCIMEDSFile);
        getGlobalStartEndTime(ADCIMEDS,TempStartDate,TempEndDate);
        if(TempStartDate.operator <(IMEDSMinDate))
            IMEDSMinDate = TempStartDate;
        if(TempEndDate.operator >(IMEDSMaxDate))
            IMEDSMaxDate = TempEndDate;
        ui->date_imedsstart->setDateTime(IMEDSMinDate);
        ui->date_imedsend->setDateTime(IMEDSMaxDate);
        ui->date_imedsstart->setMaximumDateTime(IMEDSMaxDate);
        ui->date_imedsend->setMaximumDateTime(IMEDSMaxDate);
        ui->date_imedsstart->setMinimumDateTime(IMEDSMinDate);
        ui->date_imedsend->setMinimumDateTime(IMEDSMinDate);
        this->setCursor(Qt::ArrowCursor);
    }

}

//Called when the observation imeds check box is toggeled
void MainWindow::on_check_OBSIMEDS_toggled(bool checked)
{
    ui->browse_OBSIMEDS->setEnabled(checked);
    ui->text_OBSIMEDS->setEnabled(checked);
    if(!checked && !(ui->check_ADCIMEDS->checkState()==2))
    {
        ui->button_processIMEDS->setEnabled(false);
    }
    else if(checked && ui->text_OBSIMEDS->text()!=NULL)
    {
        ui->button_processIMEDS->setEnabled(true);
    }
}

//Called when the adcirc imeds check box is toggled
void MainWindow::on_check_ADCIMEDS_toggled(bool checked)
{
    ui->browse_ADCIMEDS->setEnabled(checked);
    ui->text_ADCIMEDS->setEnabled(checked);
    if(!checked && !(ui->check_OBSIMEDS->checkState()==2))
    {
        ui->button_processIMEDS->setEnabled(false);
    }
    else if(checked && ui->text_ADCIMEDS->text()!=NULL)
    {
        ui->button_processIMEDS->setEnabled(true);
    }
}

//Called when the NOAA fetch data button is clicked
void MainWindow::on_Button_FetchData_clicked()
{
    qint64 Duration;
    QString RequestURL,StartString,EndString,Datum,Units,Product;
    QDateTime StartDate,EndDate;
    int ProductIndex;
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    MarkerID = ui->noaa_map->page()->mainFrame()->evaluateJavaScript("returnStationID()");

    //Get the date range
    StartDate = ui->Date_StartTime->dateTime();
    EndDate = ui->Date_EndTime->dateTime();

    //Sanity check
    Duration = StartDate.daysTo(EndDate);
    if(Duration>31)
    {
        QMessageBox::information(NULL,"ERROR","Date range must be less than 31 days.");
        return;
    }
    if(StartDate==EndDate||EndDate<StartDate)
    {
        QMessageBox::information(NULL,"ERROR","Dates must be a valid range.");
        return;
    }

    //Get options
    Units = ui->combo_noaaunits->currentText();
    Product = retrieveProduct(2);
    ProductIndex = ui->combo_NOAAProduct->currentIndex();
    if(ProductIndex == 0 || ProductIndex == 1)
        Datum = ui->combo_noaadatum->currentText();
    else if(ProductIndex == 2 || ProductIndex == 5)
        Datum = "Stnd";
    else if(ProductIndex == 3 || ProductIndex == 4)
        Datum = "Stnd";
    else if(ProductIndex == 6)
        Datum = "Stnd";
    else if(ProductIndex == 7)
        Datum = "Stnd";

    //Make the date string
    StartString = StartDate.toString("yyyyMMdd");
    EndString = EndDate.toString("yyyyMMdd");
    StartString = StartString+" 00:00";
    EndString = EndString+" 23:59";

    //Connect the finished downloading signal to the routine that plots the markers
    connect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(ReadNOAADataFinished(QNetworkReply*)));
    RequestURL = QString("http://tidesandcurrents.noaa.gov/api/datagetter?")+
                 QString("product="+Product+"&application=adcvalidator")+
                 QString("&begin_date=")+StartString+QString("&end_date=")+EndString+
                 QString("&station=")+MarkerID.toString()+
                 QString("&time_zone=GMT&units=")+Units+
                 QString("&interval=&format=csv");
    if(Datum != "Stnd")RequestURL = RequestURL+QString("&datum=")+Datum;
    manager->get(QNetworkRequest(QUrl(RequestURL)));
}

void MainWindow::on_Button_RefreshNOAAStations_clicked()
{
    drawMarkers(false);
}
