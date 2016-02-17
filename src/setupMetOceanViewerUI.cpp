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
#include <MetOceanViewer.h>
#include <ui_MetOceanViewer_main.h>
#include <keyhandler.h>
#include <update_dialog.h>

void MainWindow::setupMetOceanViewerUI()
{
    QString BaseFile;

    //-------------------------------------------//
    //Setting up the NOAA tab for the user

    //Define which web page we will use from the resource included
    this->noaa_page = new mov_QWebEnginePage;
    ui->noaa_map->setPage(this->noaa_page);
    ui->noaa_map->page()->load(QUrl("qrc:/rsc/html/noaa_maps.html"));

    //Get the local timezone offset
    this->LocalTimezoneOffset = getLocalTimzoneOffset();
    this->LocalTimeUTC = QDateTime::currentDateTimeUtc();

    //For NOAA, set the default date/time to today and today minus 1
    ui->Date_StartTime->setDateTime(QDateTime::currentDateTimeUtc().addDays(-1));
    ui->Date_EndTime->setDateTime(QDateTime::currentDateTimeUtc());
    ui->Date_StartTime->setMaximumDateTime(QDateTime::currentDateTimeUtc());
    ui->Date_EndTime->setMaximumDateTime(QDateTime::currentDateTimeUtc());


    //-------------------------------------------//
    //Set up the USGS tab for the user

    //Set the dates used and the minimum and maximum dates that can be selected
    //Since data is more sparse from USGS, the default range is the last 7 days of data
    ui->Date_usgsStart->setDateTime(QDateTime::currentDateTime().addDays(-7));
    ui->Date_usgsEnd->setDateTime(QDateTime::currentDateTime());
    ui->Date_usgsStart->setMinimumDateTime(QDateTime(QDate(1900,1,1)));
    ui->Date_usgsEnd->setMinimumDateTime(QDateTime(QDate(1900,1,1)));
    ui->Date_usgsEnd->setMaximumDateTime(QDateTime::currentDateTime());
    ui->Date_usgsStart->setMaximumDateTime(QDateTime::currentDateTime());
    this->usgs_page = new mov_QWebEnginePage;
    ui->usgs_map->setPage(this->usgs_page);
    ui->usgs_map->load(QUrl("qrc:/rsc/html/usgs_maps.html"));


    //-------------------------------------------//
    //Set up the time series tab for the user

    //Load the selected web page file from the resource
    ui->timeseries_map->load(QUrl("qrc:/rsc/html/timeseries_maps.html"));

    //Set the minimum and maximum times that can be selected
    ui->date_TimeseriesStartDate->setDateTime(ui->date_TimeseriesStartDate->minimumDateTime());
    ui->date_TimeseriesEndDate->setDateTime(ui->date_TimeseriesEndDate->maximumDateTime());

    //...Build a table of random colors
    this->randomColors.resize(16);
    this->randomColors[0] = QColor(Qt::GlobalColor::green);
    this->randomColors[1] = QColor(Qt::GlobalColor::red);
    this->randomColors[2] = QColor(Qt::GlobalColor::blue);
    this->randomColors[3] = QColor(Qt::GlobalColor::yellow);
    this->randomColors[4] = QColor(Qt::GlobalColor::magenta);
    this->randomColors[5] = QColor(Qt::GlobalColor::cyan);
    this->randomColors[6] = QColor(Qt::GlobalColor::black);
    this->randomColors[7] = QColor(Qt::GlobalColor::darkRed);
    this->randomColors[8] = QColor(Qt::GlobalColor::darkGreen);
    this->randomColors[9] = QColor(Qt::GlobalColor::darkBlue);
    this->randomColors[10] = QColor(Qt::GlobalColor::darkCyan);
    this->randomColors[11] = QColor(Qt::GlobalColor::darkMagenta);
    this->randomColors[12] = QColor(Qt::GlobalColor::darkYellow);
    this->randomColors[13] = QColor(Qt::GlobalColor::darkGray);
    this->randomColors[14] = QColor(Qt::GlobalColor::lightGray);
    this->randomColors[15] = QColor(Qt::GlobalColor::gray);

    //-------------------------------------------//
    //Load the High Water Mark Map and Regression Chart

    //Set the web pages used
    ui->map_hwm->load(QUrl("qrc:/rsc/html/hwm_map.html"));

    //Set the colors that are being used on the display page for various
    //things that will be displayed
    this->DotColorHWM.setRgb(11,84,255);
    this->LineColorRegression.setRgb(7,145,0);
    this->LineColor121Line.setRgb(255,0,0);
    this->LineColorBounds.setRgb(0,0,0);

    //Set the button color for high water marks
    QString ButtonStyle = MakeColorString(this->DotColorHWM);
    ui->button_hwmcolor->setStyleSheet(ButtonStyle);
    ui->button_hwmcolor->update();

    //Set the button color for the 1:1 line
    ButtonStyle = MakeColorString(this->LineColor121Line);
    ui->button_121linecolor->setStyleSheet(ButtonStyle);
    ui->button_121linecolor->update();

    //Set the button color for the linear regression line
    ButtonStyle = MakeColorString(this->LineColorRegression);
    ui->button_reglinecolor->setStyleSheet(ButtonStyle);
    ui->button_reglinecolor->update();

    //Set the button color for StdDev bounding lines
    ButtonStyle = MakeColorString(this->LineColorBounds);
    ui->button_boundlinecolor->setStyleSheet(ButtonStyle);
    ui->button_boundlinecolor->update();


    //-------------------------------------------//
    //Setup the Table
    SetupTimeseriesTable();

    //Get the directory path to start in
    //For Mac/Unix, use the user's home directory.
    //For Windows, use the user's desktop
    this->PreviousDirectory = QDir::homePath();
#ifdef Q_OS_WIN
    this->PreviousDirectory = this->PreviousDirectory+"/Desktop";
#endif

    //-------------------------------------------//
    //Check if the command line argument was passed
    //If it was, load the session file
    if(SessionFile!=NULL)
    {
        splitPath(SessionFile,BaseFile,this->PreviousDirectory);
        loadSession();
        ui->MainTabs->setCurrentIndex(1);
    }

    ui->map_hwm->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->timeseries_map->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->noaa_map->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->usgs_map->setContextMenuPolicy(Qt::CustomContextMenu);

    keyhandler* key = new keyhandler();
    this->centralWidget()->installEventFilter(key);

    connect(key,SIGNAL(enterKeyPressed()),this,SLOT(handleEnterKey()));

    //...Check for updates and alert the user if there is a new version
    QPointer<update_dialog> update = new update_dialog(this);
    bool doUpdate = update->checkForUpdate();
    if(doUpdate)
    {
        update->runUpdater();
        update->exec();
    }

}
