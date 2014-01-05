/* imeds.h
 *
 * $Author$
 * $Date$
 * $Rev$
 * $HeadURL$
 * $Id$
 *
*/
        
#ifndef IMEDS_H
#define IMEDS_H

#include <QMainWindow>
#include <QVector>
#include <QFile>
#include <QMessageBox>
#include <QDateTime>

struct IMEDS_DATA
{
    double             latitude;
    double             longitude;
    QString            StationName;
    int                NumSnaps;
    int                StationIndex;
    QVector<QDateTime>  date;
    QVector<double>     data;
};

struct IMEDS{
    int nstations;
    QString header1;
    QString header2;
    QString header3;
    QVector<IMEDS_DATA> station;
    bool success;
};

IMEDS readIMEDS(QString filename);

extern QString ADCIMEDSFile;
extern QString OBSIMEDSFile;
extern IMEDS ADCIMEDS;
extern IMEDS OBSIMEDS;

#endif // IMEDS_H
