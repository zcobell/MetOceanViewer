/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2018  Zach Cobell
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
#ifndef STATIONMODEL_H
#define STATIONMODEL_H

#include <QAbstractListModel>
#include <QGeoCoordinate>
#include <QHash>
#include <QObject>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickWidget>
#include "station.h"

class StationModel : public QAbstractListModel {
  Q_OBJECT

 public:
  using QAbstractListModel::QAbstractListModel;

  enum StationRoles {
    positionRole = Qt::UserRole + 1,
    stationIDRole,
    stationNameRole,
    longitudeRole,
    latitudeRole,
    modeledRole,
    measuredRole,
    differenceRole,
    categoryRole,
    selectedRole
  };

  StationModel(QObject *parent = Q_NULLPTR);

  Q_INVOKABLE void addMarker(const Station &station);

  void addMarkers(QVector<Station> &stations);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;

  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;

  QHash<int, QByteArray> roleNames() const;

  Station findStation(QString stationName) const;

  void boundingBox(QRectF &box);

  void clear();

  static void fitMarkers(QQuickWidget *quickWidget, StationModel *model);

 public slots:

  void selectStation(QString name);

  void deselectStation(QString name);

 private:
  void buildRoles();

  QList<Station> m_stations;
  QHash<QString, Station> m_stationMap;
  QHash<QString, int> m_stationLocationMap;
  QHash<int, QByteArray> m_roles;

};

#endif  // STATIONMODEL_H
