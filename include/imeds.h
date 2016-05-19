#ifndef IMEDS_H
#define IMEDS_H

#include <QObject>
#include <QFile>
#include "imeds_station.h"

class imeds : public QObject
{

public:
    explicit imeds(QObject *parent = 0);

    //...Methods
    int read(QString filename);
    int write(QString filename);

    //...Variables
    int nstations;

    QString header1;
    QString header2;
    QString header3;
    QString units;
    QString datum;

    QVector<imeds_station*> station;

    bool success;

};

#endif // IMEDS_H
