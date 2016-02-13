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
        
#ifndef METOCEANVIEWER_H
#define METOCEANVIEWER_H

//...QT Library Includes...//
#include <QMainWindow>
#include <QtWebEngineWidgets>
#include <QNetworkInterface>
#include <QUrl>
#include <QtNetwork>
#include <QVector>
#include <QFileDialog>
#include <QColorDialog>
#include <QtCharts>

//...MetOcean Viewer Includes...//
#include <general_functions.h>
#include <noaa.h>
#include <usgs.h>
#include <hwm.h>
#include <user_timeseries.h>
#include <about_dialog.h>
#include <myqwebenginepage.h>

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

    int loadSession();

    QString PreviousDirectory;

private slots:

    void setupMetOceanViewerUI();

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

    void on_button_plotTimeseriesStation_clicked();

    void keyPressEvent(QKeyEvent* key);

    void on_actionAbout_triggered();

    void on_actionLoad_Session_triggered();

    void on_actionSave_Session_triggered();

    void on_actionSave_Session_As_triggered();

    void on_combo_usgs_panto_currentIndexChanged(int index);

    void on_button_usgs_fetch_clicked();

    void on_button_usgssavemap_clicked();

    void on_button_usgssavedata_clicked();

    void on_combo_USGSProduct_currentIndexChanged(int index);

    void on_radio_usgsDaily_clicked();

    void on_radio_usgshistoric_clicked();

    void on_radio_usgs_instant_clicked();

    void on_check_regressionColorMatch_clicked(bool checked);

    void on_button_moveRowUp_clicked();

    void on_button_moveRowDown_clicked();

    void on_combo_NOAAProduct_currentIndexChanged(int index);

private:
    Ui::MainWindow *ui;

    QList<QTableWidgetItem *> grabTableRow(int row);

    void setTableRow(int row, const QList<QTableWidgetItem*>& rowItems);

    void SetupTimeseriesTable();

    int saveSession();

    bool confirmClose();

    QPointer<noaa> thisNOAA;

    QPointer<usgs> thisUSGS;

    QPointer<hwm> thisHWM;

    QPointer<user_timeseries> thisTimeseries;

    QPointer<MyQWebEnginePage> noaa_page;

    QPointer<MyQWebEnginePage> usgs_page;

    QColor DotColorHWM,LineColorRegression,LineColor121Line,LineColorBounds;

protected:

    void closeEvent(QCloseEvent *);

};

//-------------------------------------------//
//Some global variables used throughout the
//code
//-------------------------------------------//
extern QString SessionFile;
extern QString AlternateFolder;
//-------------------------------------------//


#endif // METOCEANVIEWER_H
