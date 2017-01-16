#ifndef MOVDFLOW_H
#define MOVDFLOW_H

#include <QObject>
#include <QMap>
#include <QList>
#include <QVector>
#include <QDateTime>
#include "movImeds.h"

class MovDflow : public QObject
{
    Q_OBJECT
public:
    explicit MovDflow(QString filename, QObject *parent = 0);

    QStringList getVaribleList();

    int dflowToImeds(QString variable);

    int getStations();

    int getVariable(QString variable, MovImeds *imeds);

    bool isError();

private:

    int _init();
    int _getPlottingVariables();
    int _getStations();
    int _getTime(QVector<QDateTime> &timeList);
    int _getVar(QString variable, QVector<QVector<double> > &data);

    bool              _isInitialized;
    bool              _readError;
    int               _nStations;
    int               _nSteps;
    QString           _filename;
    QMap<QString,int> _varnames;
    QMap<QString,int> _dimnames;
    QList<QString>    _plotvarnames;
    QVector<double>   _xCoordinates;
    QVector<double>   _yCoordinates;
    QVector<QString>  _stationNames;
    QDateTime         _refTime;

};

#endif // MOVDFLOW_H
