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
#ifndef MOV_NEFIS_H
#define MOV_NEFIS_H

#include <QObject>
#include <QList>
#include <QVector>
#include <QPointF>
#include <QDateTime>

#define MAX_NEFIS_CEL_DIM 100
#define MAX_NEFIS_DESC     64
#define MAX_NEFIS_DIM       5
#define MAX_NEFIS_NAME     16
#define MAX_NEFIS_TYPE      8

extern "C" {
#include "nefis_defines.h"
#include "btps.h"
#include "nefis.h"
}

class mov_nefis : public QObject
{
    Q_OBJECT
public:
    explicit mov_nefis(QString defFilename, QString datFilename, QObject *parent = 0);

    int open();
    int close();


signals:

public slots:

private:

    int _init();
    int _getStationLocations();
    int _getSeriesList();
    int _getTimes();
    int _getSeriesNames(QString seriesGroup, QVector<QString> &seriesNames, QVector<QString> &seriesDescriptions);


    QString mDatFilename;
    QString mDefFilename;
    bool isOpen;
    int fd;
    int mNumStations;
    QVector<QPointF>   mStationLocations;
    QVector<QString>   mStationNames;
    QVector<QString>   mSeriesNames;
    QVector<QString>   mSeriesDescriptions;
    QVector<QString>   mSeriesSource;
    QVector<QDateTime> mOutputTimes;

};

#endif // MOV_NEFIS_H
