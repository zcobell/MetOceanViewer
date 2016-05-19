#ifndef IMEDS_STATION_H
#define IMEDS_STATION_H

#include <QObject>
#include <QVector>
#include <QDateTime>

class imeds_station : public QObject
{

public:

    explicit imeds_station(QObject *parent = 0);

    double              latitude;
    double              longitude;

    QString             StationName;
    QString             StationID;

    int                 NumSnaps;
    int                 StationIndex;

    QVector<QDateTime>  date;
    QVector<double>     data;

    bool                isNull;

};

#endif // IMEDS_STATION_H
