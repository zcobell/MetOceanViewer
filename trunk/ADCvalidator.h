/* ADCValidator.h
 *
 * $Author$
 * $Date$
 * $Rev$
 * $HeadURL$
 * $ID$
 *
 */

#ifndef ADCVALIDATOR_H
#define ADCVALIDATOR_H

#include <QMainWindow>
#include <QtWebKitWidgets>
#include <QUrl>
#include <QtNetwork>
#include <QVector>
#include <QFileDialog>
#include "imeds.h"

struct NOAAStationData
{
    QDate Date;
    QTime Time;
    double value;
};

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

    void on_button_saveIMEDSData_clicked();

    void on_button_saveIMEDSImage_clicked();

    void on_check_imedyauto_toggled(bool checked);

    void on_browse_hwm_clicked();

    void on_button_processHWM_clicked();

    void on_check_manualHWM_toggled(bool checked);

    void on_button_saveHWMMap_clicked();

    void on_button_saveregression_clicked();

private:
    Ui::MainWindow *ui;

    void drawMarkers();

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

};

#endif // ADCVALIDATOR_H
