#ifndef XTIDE_H
#define XTIDE_H

#include <QObject>
#include <QtWidgets>
#include <QWebEngineView>
#include <QtCharts>
#include <QChartView>
#include <QPrinter>
#include <QVector>
#include <QtWebEngine/QtWebEngine>

#include "mov_qchartview.h"
#include "mov_errors.h"
#include "mov_flags.h"
#include "general_functions.h"

using namespace QtCharts;

class XTide : public QObject
{

public:
    //...Constructor
    explicit XTide(QWebEngineView *inMap, mov_QChartView *inChart,
                   QDateEdit *inStartDateEdit, QDateEdit *inEndDateEdit,
                   QComboBox *inUnits, QStatusBar *inStatusBar, QObject *parent = 0);

    //...Destructor
    ~XTide();

    //...Public Functions
    int plotXTideStation();
    int getLoadedXTideStation();
    int getClickedXTideStation();

private:
    int findXTideExe();
    int calculateXTides();
    int plotChart();
    int getDataBounds(double min, double max);
    int parseXTideResponse(QString xTideResponse);

    //Structures
    struct XTideStationData
    {
        QDateTime date;
        double value;
    };

    //...Private Variables
    QVector<XTideStationData> currentXTideStation;
    double currentXTideLat;
    double currentXTideLon;

    QString currentStationName;
    QString units;
    QString yLabel;
    QString xLabel;
    QString plotTitle;
    QString xTideErrorString;
    QString xTideExe;
    QString xTideHarmFile;
    QChart  *thisChart;

    //...Pointers to GUI elements
    QWebEngineView *map;
    mov_QChartView *chart;
    QDateEdit *startDateEdit,*endDateEdit;
    QComboBox *unitSelect;
    QStatusBar *statusBar;

};

#endif // XTIDE_H
