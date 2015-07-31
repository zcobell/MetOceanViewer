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
        
#ifndef ADCVALIDATOR_H
#define ADCVALIDATOR_H

#include <QMainWindow>
#include <QtWebKitWidgets>
#include <QNetworkInterface>
#include <QUrl>
#include <QtNetwork>
#include <QVector>
#include <QFileDialog>
#include <QColorDialog>
#include <timeseries.h>

//-------------------------------------------//
//Data structures
//-------------------------------------------//
//Structure that holds data downloaded from
//the NOAA website
//-------------------------------------------//
struct NOAAStationData
{
    QDate Date;
    QTime Time;
    double value;
};
//-------------------------------------------//

//-------------------------------------------//
//Structure that holds the data downloaded
//from the USGS website
//-------------------------------------------//
struct USGSData
{
    int NumDataPoints;
    QString Description;
    QVector<QDateTime> Date;
    QVector<double> Data;
};
//-------------------------------------------//

//-------------------------------------------//
//Main class used by the Qt program that holds
//the main set of functions used
//-------------------------------------------//
namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);

    ~MainWindow();

    static QString MakeColorString(QColor InputColor);

    static void GetLeadingPath(QString Input);

    static QString GetMyLeadingPath(QString Input);

    static QColor GenerateRandomColor();

    static QString RemoveLeadingPath(QString Input);

    int loadSession();

    void delay(int delayTime);

    void delayM(int delayTime);

private slots:

    void PlotNOAAResponse();

    void ReadNOAAResponse(QNetworkReply*);

    void ReadUSGSDataFinished(QNetworkReply*);

    void on_Button_FetchData_clicked();

    void on_Combo_NOAAPanTo_currentIndexChanged(int index);

    void on_button_noaasavechart_clicked();

    void on_button_noaasavedata_clicked();

    void on_button_saveTimeseriesImage_clicked();

    void on_check_TimeseriesYauto_toggled(bool checked);

    void on_browse_hwm_clicked();

    void on_button_processHWM_clicked();

    void on_check_manualHWM_toggled(bool checked);

    void on_button_saveHWMMap_clicked();

    void on_button_hwmcolor_clicked();

    void on_button_121linecolor_clicked();

    void on_button_reglinecolor_clicked();

    void on_button_boundlinecolor_clicked();

    void on_actionQuit_triggered();

    void on_button_TimeseriesDeleteRow_clicked();

    void on_button_TimeseriesAddRow_clicked();

    void on_button_TimeseriesEditRow_clicked();

    void on_button_processTimeseriesData_clicked();

    void on_check_TimeseriesAllData_toggled(bool checked);

    void on_button_fitHWM_clicked();

    void on_button_fitTimeseries_clicked();

    void unsupportedContent(QNetworkReply * reply);

    void on_button_plotTimeseriesStation_clicked();

    void keyPressEvent(QKeyEvent* key);

    void OpenExternalBrowser(const QUrl & url);

    void on_actionAbout_triggered();

    void on_actionLoad_Session_triggered();

    void on_actionSave_Session_triggered();

    void on_actionSave_Session_As_triggered();

    void on_combo_usgs_panto_currentIndexChanged(int index);

    void on_button_usgs_fetch_clicked();

    int GetTimezoneOffset(QString timezone);

    void on_button_usgssavemap_clicked();

    void on_button_usgssavedata_clicked();

    void on_combo_USGSProduct_currentIndexChanged(int index);

    void PlotUSGS();

    void on_radio_usgsDaily_clicked();

    void on_radio_usgshistoric_clicked();

    void on_radio_usgs_instant_clicked();

    void on_check_regressionColorMatch_clicked(bool checked);

    void on_button_moveRowUp_clicked();

    void on_button_moveRowDown_clicked();

    QList<QTableWidgetItem *> grabTableRow(int row);

    void setTableRow(int row, const QList<QTableWidgetItem*>& rowItems);

private:
    Ui::MainWindow *ui;

    void drawMarkers();

    void initializeGoogleMaps(Ui::MainWindow *ui);

    QString FormatNOAAResponse(QVector<QByteArray> Input, QString &ErrorString);

    void FormatUSGSInstantResponse(QByteArray Input);

    void FormatUSGSDailyResponse(QByteArray Input);

    void getStartEndTime(IMEDS Input,int index, QDateTime &Start, QDateTime &End);

    void getGlobalStartEndTime(IMEDS Input, QDateTime &Start, QDateTime &End);

    int ClassifyHWM(double diff);

    QString retrieveProduct(int type);

    void SetupTimeseriesTable();

    void UpdateTimeseriesDateRange(IMEDS MyIMEDS);

    QString FormatTimeseriesString(IMEDS MyStation, int index, double unitConvert);

    int CheckStationLocationsTimeseries(IMEDS Control, IMEDS Test);

    int saveSession();

    int NETCDF_ERR(int status);
};

//-------------------------------------------//
//Function prototypes not included in the class
bool isConnectedToNetwork();
//-------------------------------------------//


//-------------------------------------------//
//Some global variables used throughout the
//code
//-------------------------------------------//
extern QString PreviousDirectory;
extern QColor ADCIRCIMEDSColor,OBSIMEDSColor;
extern QColor LineColor121Line,LineColorBounds;
extern QColor DotColorHWM,LineColorRegression;
extern QDateTime IMEDSMinDate,IMEDSMaxDate;
extern QVector<QVector<double> > NOAAStations;
extern QVector<QString> NOAAStationNames;
extern QVector<QString> panToLocations;
extern bool ColorUpdated;
extern bool EditBox;
extern QString SessionFile;
extern QString AlternateFolder;
extern int NOAAMarkerID;
extern int NumNOAADataRead;
extern double CurrentNOAALat,CurrentNOAALon;
extern QString CurrentNOAAStationName;
extern QString USGSMarkerID;
extern double CurrentUSGSLat,CurrentUSGSLon;
extern QString CurrentUSGSStationName,USGSErrorString;
extern bool USGSbeenPlotted,USGSdataReady,USGSDataReadFinished;
extern int USGSdataMethod;
extern QVector<NOAAStationData> CurrentNOAAStation;
extern QVector<NOAAStationData> USGSPlot;
extern QVector<USGSData> CurrentUSGSStation;
extern QVector<QByteArray> NOAAWebData;
//-------------------------------------------//


#endif // ADCVALIDATOR_H
