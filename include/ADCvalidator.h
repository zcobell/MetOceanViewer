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
//  File: ADCvalidator.h
//
//------------------------------------------------------------------------------
        
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
extern double CurrentNOAALat,CurrentNOAALon;
extern QString CurrentNOAAStationName;
extern QString USGSMarkerID;
extern double CurrentUSGSLat,CurrentUSGSLon;
extern QString CurrentUSGSStationName,USGSErrorString;
extern bool USGSbeenPlotted,USGSdataReady;

bool isConnectedToNetwork();

struct NOAAStationData
{
    QDate Date;
    QTime Time;
    double value;
};
extern QVector<NOAAStationData> CurrentNOAAStation;
extern QVector<NOAAStationData> USGSPlot;

struct USGSData
{
    int NumDataPoints;
    QString Description;
    QVector<QDateTime> Date;
    QVector<double> Data;
};
extern QVector<USGSData> CurrentUSGSStation;

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

private slots:

    void ReadNOAADataFinished(QNetworkReply*);

    void ReadUSGSDataFinished(QNetworkReply*);

    void on_Button_FetchData_clicked();

    void on_Combo_PanTo_currentIndexChanged(int index);

    void on_button_noaasavechart_clicked();

    void on_button_noaasavedata_clicked();

    void on_button_saveIMEDSImage_clicked();

    void on_check_imedyauto_toggled(bool checked);

    void on_browse_hwm_clicked();

    void on_button_processHWM_clicked();

    void on_check_manualHWM_toggled(bool checked);

    void on_button_saveHWMMap_clicked();

    void on_button_hwmcolor_clicked();

    void on_button_121linecolor_clicked();

    void on_button_reglinecolor_clicked();

    void on_button_boundlinecolor_clicked();

    void on_actionQuit_triggered();

    void on_button_deleterow_clicked();

    void on_button_addrow_clicked();

    void on_button_editrow_clicked();

    void on_button_processIMEDSData_clicked();

    void on_check_imedsalldata_toggled(bool checked);

    void on_button_fitHWM_clicked();

    void on_button_fitIMEDS_clicked();

    void unsupportedContent(QNetworkReply * reply);

    void on_button_plotStation_clicked();

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

private:
    Ui::MainWindow *ui;

    void drawMarkers();

    void delay(int delayTime);

    void initializeGoogleMaps(Ui::MainWindow *ui);

    QString FormatNOAAResponse(QByteArray Input, QString &ErrorString);

    void FormatUSGSResponse(QByteArray Input);

    void addIMEDSandADCIRCMarker(IMEDS Observation, IMEDS ADCIRC);

    void addADCIRCMarker(IMEDS ADCIRC);

    void addIMEDSMarker(IMEDS Observation);

    void getStartEndTime(IMEDS Input,int index, QDateTime &Start, QDateTime &End);

    void getGlobalStartEndTime(IMEDS Input, QDateTime &Start, QDateTime &End);

    int ClassifyHWM(double diff);

    QString retrieveProduct(int type);

    void SetupIMEDSTable();

    void UpdateIMEDSDateRange(IMEDS MyIMEDS);

    QString FormatIMEDSString(IMEDS MyStation, int index, double unitConvert);

    int CheckStationLocationsIMEDS(IMEDS Control, IMEDS Test);

    int saveSession();

    int NETCDF_ERR(int status);
};


#endif // ADCVALIDATOR_H
