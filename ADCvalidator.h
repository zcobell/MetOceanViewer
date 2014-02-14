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
//  File: ADCvalidator.h
//
//------------------------------------------------------------------------------
        
#ifndef ADCVALIDATOR_H
#define ADCVALIDATOR_H

#include <QMainWindow>
#include <QtWebKitWidgets>
#include <QUrl>
#include <QtNetwork>
#include <QVector>
#include <QFileDialog>
#include <QColorDialog>
#include "imeds.h"

extern QString PreviousDirectory;
extern QColor ADCIRCIMEDSColor,OBSIMEDSColor;
extern QColor LineColor121Line,LineColorBounds;
extern QColor DotColorHWM,LineColorRegression;
extern QDateTime IMEDSMinDate,IMEDSMaxDate;
extern QVector<QVector<double> > NOAAStations;
extern QVector<QString> NOAAStationNames;
extern QVector<QString> panToLocations;
extern QVariant MarkerID;

struct NOAAStationData
{
    QDate Date;
    QTime Time;
    double value;
};
extern QVector<NOAAStationData> CurrentStation;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void BeginGatherStations();

    void ReadNOAADataFinished(QNetworkReply*);

    void on_Button_FetchData_clicked();

    void on_check_ADCIMEDS_toggled(bool checked);

    void on_check_OBSIMEDS_toggled(bool checked);

    void on_browse_ADCIMEDS_clicked();

    void on_browse_OBSIMEDS_clicked();

    void on_button_processIMEDS_clicked();

    void on_check_imedsalldata_toggled(bool checked);

    void on_Combo_PanTo_currentIndexChanged(int index);

    void on_button_savechart_clicked();

    void on_button_savedata_clicked();

    void on_button_saveIMEDSImage_clicked();

    void on_check_imedyauto_toggled(bool checked);

    void on_browse_hwm_clicked();

    void on_button_processHWM_clicked();

    void on_check_manualHWM_toggled(bool checked);

    void on_button_saveHWMMap_clicked();

    void on_button_saveregression_clicked();

    void on_Button_RefreshNOAAStations_clicked();

    void on_button_adcselectcolor_clicked();

    void on_button_imedsselectcolor_clicked();

    void on_button_hwmcolor_clicked();

    void on_button_121linecolor_clicked();

    void on_button_reglinecolor_clicked();

    void on_button_boundlinecolor_clicked();

private:
    Ui::MainWindow *ui;

    void drawMarkers(bool DelayDraw);

    void delay(int delayTime);

    void initializeGoogleMaps(Ui::MainWindow *ui);

    QString FormatNOAAResponse(QByteArray InputString);

    QString RemoveLeadingPath(QString Input);

    void addIMEDSandADCIRCMarker(IMEDS Observation, IMEDS ADCIRC);

    void addADCIRCMarker(IMEDS ADCIRC);

    void addIMEDSMarker(IMEDS Observation);

    void getStartEndTime(IMEDS Input,int index, QDateTime &Start, QDateTime &End);

    void getGlobalStartEndTime(IMEDS Input, QDateTime &Start, QDateTime &End);

    int ClassifyHWM(double diff);

    void initializePanToLocations(int size);

    QString retrieveProduct(int type);

    QString MakeColorString(QColor InputColor);

    void GetLeadingPath(QString Input);

};

#endif // ADCVALIDATOR_H
