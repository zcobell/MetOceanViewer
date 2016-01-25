#ifndef NOAA_H
#define NOAA_H

#include <MetOceanViewer.h>
#include <QtWebKitWidgets>
#include <QNetworkInterface>
#include <QUrl>
#include <QtNetwork>
#include <QVector>

#define NOAA_ERR_INVALIDDATERANGE -9990

class noaa : public QMainWindow
{
public:
    //...Initializer
    noaa();

    //...Public Functions
    int fetchNOAAData();

private:

    //...Private Functions
    void ReadNOAAResponse(QNetworkReply *reply, int index, int index2);
    void PlotNOAAResponse();

    //...Private Variables
    QVector< QVector<QByteArray> > NOAAWebData;
    int NOAAMarkerID,ProductIndex;
    double CurrentNOAALat,CurrentNOAALon;
    QString CurrentNOAAStationName,Datum,Units;
    QDateTime StartDate,EndDate;
};

#endif // NOAA_H
