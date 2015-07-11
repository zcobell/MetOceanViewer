//----GPL-----------------------------------------------------------------------
//
// This file is part of MetOceanViewer.
//
//    MetOceanViewer is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    MetOceanViewer is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with MetOceanViewer.  If not, see <http://www.gnu.org/licenses/>.
//------------------------------------------------------------------------------
        
#include <MetOceanViewer.h>
#include <ui_MetOceanViewer_main.h>
#include <timeseries.h>

//-------------------------------------------//
//A whole load of variables we'll need at
//some point or another. These are sort of
//"Global" variables. Since I learned by
//programming in FORTRAN, this makes me happy
//-------------------------------------------//
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

//Some flags that we'll use later. These are just
//values we might need to search for at some point
double FLAG_NULL_TS = -999991025;
QDateTime FLAG_NULL_DATE = QDateTime::fromString(
                              "08/09/1987 17:50",
                              "MM/dd/yyyy hh:mm");
                              //Happy birthday to me
//-------------------------------------------//




//-------------------------------------------//
//Main routine which will intialize all the tabs
MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),ui(new Ui::MainWindow)
{
    QString ButtonStyle;

    //Setup UI
    ui->setupUi(this);

    //-------------------------------------------//
    //Setting up the NOAA tab for the user

    //Turn plugins on
    QWebSettings::globalSettings()->setAttribute(QWebSettings::PluginsEnabled, true);

    //Define which web page we will use from the resource included
    ui->noaa_map->load(QUrl("qrc:/rsc/html/noaa_maps.html"));

    //Turn off scroll bars
    ui->noaa_map->page()->mainFrame()->setScrollBarPolicy(Qt::Horizontal,Qt::ScrollBarAlwaysOff);
    ui->noaa_map->page()->mainFrame()->setScrollBarPolicy(Qt::Vertical,Qt::ScrollBarAlwaysOff);

    //Catch unsupported content coming from highcharts to download images for the user
    ui->noaa_map->page()->setForwardUnsupportedContent(true);
    connect(ui->noaa_map->page(),SIGNAL(unsupportedContent(QNetworkReply*)),this,SLOT(unsupportedContent(QNetworkReply*)));

    //Tell Qt to delegate clicked links to the users dafault browser
    ui->noaa_map->page()->setLinkDelegationPolicy(QWebPage::DelegateAllLinks);
    connect( ui->noaa_map->page(), SIGNAL(linkClicked(const QUrl &)),
                    this, SLOT(OpenExternalBrowser(const QUrl &)));

    //For NOAA, set the default date/time to today and today minus 1
    ui->Date_StartTime->setDateTime(QDateTime::currentDateTime().addDays(-1));
    ui->Date_EndTime->setDateTime(QDateTime::currentDateTime());
    ui->Date_StartTime->setMaximumDateTime(QDateTime::currentDateTime());
    ui->Date_EndTime->setMaximumDateTime(QDateTime::currentDateTime());


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

    //Catch unsupported content coming from highcharts to download images for the user
    ui->usgs_map->page()->setForwardUnsupportedContent(true);
    connect(ui->usgs_map->page(),SIGNAL(unsupportedContent(QNetworkReply*)),this,SLOT(unsupportedContent(QNetworkReply*)));


    //-------------------------------------------//
    //Set up the time series tab for the user

    //Load the selected web page file from the resource
    ui->timeseries_map->load(QUrl("qrc:/rsc/html/timeseries_maps.html"));

    //Turn off the scroll bars
    ui->timeseries_map->page()->mainFrame()->setScrollBarPolicy(Qt::Horizontal,Qt::ScrollBarAlwaysOff);
    ui->timeseries_map->page()->mainFrame()->setScrollBarPolicy(Qt::Vertical,Qt::ScrollBarAlwaysOff);

    //Catch unsupported content coming from highcharts to download images for the user
    ui->timeseries_map->page()->setForwardUnsupportedContent(true);
    connect(ui->timeseries_map->page(),SIGNAL(unsupportedContent(QNetworkReply*)),this,SLOT(unsupportedContent(QNetworkReply*)));

    //Set the minimum and maximum times that can be selected
    ui->date_TimeseriesStartDate->setDateTime(ui->date_TimeseriesStartDate->minimumDateTime());
    ui->date_TimeseriesEndDate->setDateTime(ui->date_TimeseriesEndDate->maximumDateTime());


    //-------------------------------------------//
    //Load the High Water Mark Map and Regression Chart

    //Set the web pages used
    ui->map_hwm->load(QUrl("qrc:/rsc/html/hwm_map.html"));
    ui->map_regression->load(QUrl("qrc:/rsc/html/reg_plot.html"));

    //Turn off scroll bars
    ui->map_hwm->page()->mainFrame()->setScrollBarPolicy(Qt::Horizontal,Qt::ScrollBarAlwaysOff);
    ui->map_hwm->page()->mainFrame()->setScrollBarPolicy(Qt::Vertical,Qt::ScrollBarAlwaysOff);
    ui->map_regression->page()->mainFrame()->setScrollBarPolicy(Qt::Horizontal,Qt::ScrollBarAlwaysOff);
    ui->map_regression->page()->mainFrame()->setScrollBarPolicy(Qt::Vertical,Qt::ScrollBarAlwaysOff);

    //Catch unsupported content coming from highcharts to download images for the user
    ui->map_regression->page()->setForwardUnsupportedContent(true);
    connect(ui->map_regression->page(),SIGNAL(unsupportedContent(QNetworkReply*)),this,SLOT(unsupportedContent(QNetworkReply*)));

    //Set the colors that are being used on the display page for various
    //things that will be displayed
    DotColorHWM.setRgb(11,84,255);
    LineColorRegression.setRgb(7,145,0);
    LineColor121Line.setRgb(255,0,0);
    LineColorBounds.setRgb(0,0,0);

    //Set the button color for high water marks
    ButtonStyle = MakeColorString(DotColorHWM);
    ui->button_hwmcolor->setStyleSheet(ButtonStyle);
    ui->button_hwmcolor->update();

    //Set the button color for the 1:1 line
    ButtonStyle = MakeColorString(LineColor121Line);
    ui->button_121linecolor->setStyleSheet(ButtonStyle);
    ui->button_121linecolor->update();

    //Set the button color for the linear regression line
    ButtonStyle = MakeColorString(LineColorRegression);
    ui->button_reglinecolor->setStyleSheet(ButtonStyle);
    ui->button_reglinecolor->update();

    //Set the button color for StdDev bounding lines
    ButtonStyle = MakeColorString(LineColorBounds);
    ui->button_boundlinecolor->setStyleSheet(ButtonStyle);
    ui->button_boundlinecolor->update();


    //-------------------------------------------//
    //Some other initializations

    IMEDSMinDate.setDate(QDate(2900,1,1));
    IMEDSMaxDate.setDate(QDate(1820,1,1));

    //Setup the Table
    SetupTimeseriesTable();

    //Get the directory path to start in
    //For Mac/Unix, use the user's home directory.
    //For Windows, use the user's desktop
    PreviousDirectory = QDir::homePath();
#ifdef Q_OS_WIN
    PreviousDirectory = PreviousDirectory+"/Desktop";
#endif

    //-------------------------------------------//
    //Check if the command line argument was passed
    //If it was, load the session file
    if(SessionFile!=NULL)
    {
        GetLeadingPath(SessionFile);
        loadSession();
        ui->MainTabs->setCurrentIndex(1);
    }

//If compiled with "-DEBUG", the QWebViews will have debugging enabled.
#ifdef EBUG
    //Optional javascript/html debugging - disabled for release versions
    QWebSettings::globalSettings()->setAttribute(QWebSettings::DeveloperExtrasEnabled, true);
#else
    //If not in debug mode, we turn off the right click options
    ui->map_hwm->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->map_regression->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->timeseries_map->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->noaa_map->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->usgs_map->setContextMenuPolicy(Qt::CustomContextMenu);
#endif

}

//Main destructor routine
MainWindow::~MainWindow()
{
    delete ui;
}

//Function to handle a clicked link and open in the user's
//default browser.
void MainWindow::OpenExternalBrowser(const QUrl & url)
{
    QDesktopServices::openUrl(url);
}
