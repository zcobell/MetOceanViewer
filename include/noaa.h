#ifndef NOAA_H
#define NOAA_H

#include <MetOceanViewer.h>
#include <mov_errors.h>
#include <mov_flags.h>
#include <QtWebKitWidgets>
#include <QNetworkInterface>
#include <QUrl>
#include <QtNetwork>
#include <QVector>

class noaa : public QObject
{
public:
    //...Constructor
    noaa(QObject *parent = 0);

    //...Destructor
    ~noaa();

    //...Public Functions
    int fetchNOAAData(QVector<QString> &javascript);
    int PlotNOAAResponse(QVector<QString> &javascript);

    //...Structures
    struct NOAAStationData
    {
        QDate Date;
        QTime Time;
        double value;
    };

    //...Public Variables
    int NOAAMarkerID;
    int ProductIndex;

    double CurrentNOAALat;
    double CurrentNOAALon;

    QString CurrentNOAAStationName;
    QString Datum;
    QString Units;

    QDateTime StartDate;
    QDateTime EndDate;

    QVector< QVector<NOAAStationData> > CurrentNOAAStation;

private:

    //...Private Functions
    QString FormatNOAAResponse(QVector<QByteArray> Input, QString &ErrorString, int index);
    void ReadNOAAResponse(QNetworkReply *reply, int index, int index2);
    int retrieveProduct(int type, QString &Product, QString &Product2);

    //...Private Variables
    QVector< QVector<QByteArray> > NOAAWebData;

};

#endif // NOAA_H
