//---- LGPL --------------------------------------------------------------------
//
// Copyright (C)  ARCADIS, 2011-2015.
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
// $Rev$
// $HeadURL$
//------------------------------------------------------------------------------
//  File: MetOceanViewer_main.cpp
//
//------------------------------------------------------------------------------
        
#include <MetOceanViewer.h>
#include <ui_MetOceanViewer_main.h>

QString PreviousDirectory;
QString SLASH;
QColor ADCIRCIMEDSColor,OBSIMEDSColor;
QColor LineColor121Line,LineColorBounds;
QColor DotColorHWM,LineColorRegression;
QDateTime IMEDSMinDate,IMEDSMaxDate;

int NOAAMarkerID = -1;
QString USGSMarkerID = "none";
double CurrentNOAALat,CurrentNOAALon;
double CurrentUSGSLat,CurrentUSGSLon;
QString CurrentNOAAStationName;
QString CurrentUSGSStationName;
QString USGSErrorString;
QVector<NOAAStationData> CurrentNOAAStation;
QVector<USGSData> CurrentUSGSStation;
QVector<NOAAStationData> USGSPlot;
bool USGSbeenPlotted = false;
bool USGSdataReady = false;
int USGSdataMethod= 0;

//Main routine which will intialize all the tabs
MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),ui(new Ui::MainWindow)
{
    QString ButtonStyle;

    //Optional javascript/html debugging - disabled for release versions
    //QWebSettings::globalSettings()->setAttribute(QWebSettings::DeveloperExtrasEnabled, true);

    //Setup UI
    ui->setupUi(this);

    //Load the NOAA tab and set accordingly
    QWebSettings::globalSettings()->setAttribute(QWebSettings::PluginsEnabled, true);
    ui->noaa_map->load(QUrl("qrc:/rsc/html/noaa_maps.html"));
    ui->noaa_map->page()->mainFrame()->setScrollBarPolicy(Qt::Horizontal,Qt::ScrollBarAlwaysOff);
    ui->noaa_map->page()->mainFrame()->setScrollBarPolicy(Qt::Vertical,Qt::ScrollBarAlwaysOff);
    ui->noaa_map->page()->setForwardUnsupportedContent(true);
    connect(ui->noaa_map->page(),SIGNAL(unsupportedContent(QNetworkReply*)),this,SLOT(unsupportedContent(QNetworkReply*)));
    ui->noaa_map->page()->setLinkDelegationPolicy(QWebPage::DelegateAllLinks);
    connect( ui->noaa_map->page(), SIGNAL(linkClicked(const QUrl &)),
                    this, SLOT(OpenExternalBrowser(const QUrl &)));
    ui->noaa_map->setContextMenuPolicy(Qt::CustomContextMenu);

    //Set the initial dates to today and today minus a day
    ui->Date_StartTime->setDateTime(QDateTime::currentDateTime().addDays(-1));
    ui->Date_EndTime->setDateTime(QDateTime::currentDateTime());
    ui->Date_StartTime->setMaximumDateTime(QDateTime::currentDateTime());
    ui->Date_EndTime->setMaximumDateTime(QDateTime::currentDateTime());
    ui->Date_usgsStart->setDateTime(QDateTime::currentDateTime().addDays(-7));
    ui->Date_usgsEnd->setDateTime(QDateTime::currentDateTime());
    ui->Date_usgsStart->setMinimumDateTime(QDateTime(QDate(1900,1,1)));
    ui->Date_usgsEnd->setMinimumDateTime(QDateTime(QDate(1900,1,1)));
    ui->Date_usgsEnd->setMaximumDateTime(QDateTime::currentDateTime());
    ui->Date_usgsStart->setMaximumDateTime(QDateTime::currentDateTime());

    //Load the timeseries tab and set accordingly
    QWebSettings::globalSettings()->setAttribute(QWebSettings::PluginsEnabled, true);
    ui->timeseries_map->load(QUrl("qrc:/rsc/html/timeseries_maps.html"));
    ui->timeseries_map->page()->mainFrame()->setScrollBarPolicy(Qt::Horizontal,Qt::ScrollBarAlwaysOff);
    ui->timeseries_map->page()->mainFrame()->setScrollBarPolicy(Qt::Vertical,Qt::ScrollBarAlwaysOff);
    ui->timeseries_map->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->timeseries_map->page()->setForwardUnsupportedContent(true);
    ui->date_TimeseriesStartDate->setDateTime(ui->date_TimeseriesStartDate->minimumDateTime());
    ui->date_TimeseriesEndDate->setDateTime(ui->date_TimeseriesEndDate->maximumDateTime());
    connect(ui->timeseries_map->page(),SIGNAL(unsupportedContent(QNetworkReply*)),this,SLOT(unsupportedContent(QNetworkReply*)));

    //Initialize Variables
    IMEDSMinDate.setDate(QDate(2900,1,1));
    IMEDSMaxDate.setDate(QDate(1820,1,1));

    //Load the High Water Mark Map and Regression Chart
    ui->map_hwm->load(QUrl("qrc:/rsc/html/hwm_map.html"));
    ui->map_hwm->page()->mainFrame()->setScrollBarPolicy(Qt::Horizontal,Qt::ScrollBarAlwaysOff);
    ui->map_hwm->page()->mainFrame()->setScrollBarPolicy(Qt::Vertical,Qt::ScrollBarAlwaysOff);
    ui->map_hwm->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->map_regression->load(QUrl("qrc:/rsc/html/reg_plot.html"));
    ui->map_regression->page()->mainFrame()->setScrollBarPolicy(Qt::Horizontal,Qt::ScrollBarAlwaysOff);
    ui->map_regression->page()->mainFrame()->setScrollBarPolicy(Qt::Vertical,Qt::ScrollBarAlwaysOff);
    ui->map_regression->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->map_regression->page()->setForwardUnsupportedContent(true);
    connect(ui->map_regression->page(),SIGNAL(unsupportedContent(QNetworkReply*)),this,SLOT(unsupportedContent(QNetworkReply*)));
    DotColorHWM.setRgb(11,84,255);
    LineColorRegression.setRgb(7,145,0);
    LineColor121Line.setRgb(255,0,0);
    LineColorBounds.setRgb(0,0,0);
    ButtonStyle = MakeColorString(DotColorHWM);
    ui->button_hwmcolor->setStyleSheet(ButtonStyle);
    ui->button_hwmcolor->update();
    ButtonStyle = MakeColorString(LineColor121Line);
    ui->button_121linecolor->setStyleSheet(ButtonStyle);
    ui->button_121linecolor->update();
    ButtonStyle = MakeColorString(LineColorRegression);
    ui->button_reglinecolor->setStyleSheet(ButtonStyle);
    ui->button_reglinecolor->update();
    ButtonStyle = MakeColorString(LineColorBounds);
    ui->button_boundlinecolor->setStyleSheet(ButtonStyle);
    ui->button_boundlinecolor->update();

    //Setup the Table
    SetupTimeseriesTable();

    //Get the directory path to start in
    PreviousDirectory = QDir::homePath();
#ifdef Q_OS_WIN
    PreviousDirectory = PreviousDirectory+"/Desktop";
#endif

    //Check if the command line argument was passed
    if(SessionFile!=NULL)
    {
        GetLeadingPath(SessionFile);
        loadSession();
        ui->MainTabs->setCurrentIndex(1);
    }

}

//Main destructor routine
MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::OpenExternalBrowser(const QUrl & url)
{
        QDesktopServices::openUrl(url);
}
