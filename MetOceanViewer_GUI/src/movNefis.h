/*-------------------------------GPL-------------------------------------//
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
//-----------------------------------------------------------------------*/
#ifndef MOVNEFIS_H
#define MOVNEFIS_H

#include <QObject>
#include <QList>
#include <QVector>
#include <QPointF>
#include <QDateTime>
#include <QMap>

#include "movImeds.h"

#define MAX_NEFIS_CEL_DIM 100
#define MAX_NEFIS_DESC     64
#define MAX_NEFIS_DIM       5
#define MAX_NEFIS_NAME     16
#define MAX_NEFIS_TYPE      8

class MovNefis : public QObject
{
    Q_OBJECT
public:
    explicit MovNefis(QString defFilename, QString datFilename, QObject *parent = 0);

    int open(bool fullInit = true);
    int close();

    QVector<QPointF> getStationLocations();
    QStringList      getSeriesNames();
    QString          getSeriesDescription(QString seriesName);
    int              generateIMEDS(QString seriesName, MovImeds *stationData, int layer);
    int              getNumLayers();

    static QString   getNefisDefFilename(QString datFilename);
    static QString   getNefisDatFilename(QString defFilename);

signals:

public slots:

private:

    int _init(bool fullInit = true);
    int _getStationLocations();
    int _getSeriesList();
    int _getTimes();
    int _getSeriesNames(QString seriesGroup, QVector<QString> &seriesNames,
                        QVector<QString> &seriesDescriptions,
                        QVector<QString> &seriesTypes,
                        QVector<bool> &seriesIs3d,
                        QVector<QVector<int> > &seriesDimensions);
    int _get(QString seriesName);
    int _getLayers();
    int _generatePlotVariableList();

    QString _mDatFilename;
    QString _mDefFilename;
    bool    _isOpen;
    int     _fd;
    int     _mNumStations;
    int     _mNumSteps;
    int     _mNumLayers;
    int     _mLayerModel; //...0=Sigma, 1=Zeta

    QVector<double>                       _mLayerDepths;
    QVector<QDateTime>                    _mOutputTimes;
    QVector< QVector< QVector<double> > > _mOutputData;
    QVector<QPointF>                      _mStationLocations;
    QStringList                           _mStationNames;
    QStringList                           _mSeriesNames;
    QMap<QString,QString>                 _mSeriesDescriptionsMap;
    QMap<QString,QString>                 _mSourceMap;
    QMap<QString,QString>                 _mTypeMap;
    QMap<QString,bool>                    _mIs3d;
    QMap<QString,QVector<int>>            _mSeriesDimensions;
    QStringList                           _mPlotEligibleVariables;


};

#endif // MOVNEFIS_H
