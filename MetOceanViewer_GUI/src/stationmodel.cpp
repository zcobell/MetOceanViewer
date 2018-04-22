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
#include "stationmodel.h"

StationModel::StationModel(QObject *parent) : QAbstractListModel(parent) {}

void StationModel::addMarker(const Station &station) {
  this->beginInsertRows(QModelIndex(), rowCount(), rowCount());
  this->m_stations.append(station);
  this->m_stationMap[station.id()] =
      this->m_stations.at(this->m_stations.length() - 1);
  this->m_stationLocationMap[station.id()] = this->m_stations.length() - 1;
  this->endInsertRows();
}

int StationModel::rowCount(const QModelIndex &parent) const {
  Q_UNUSED(parent)
  return this->m_stations.count();
}

QVariant StationModel::data(const QModelIndex &index, int role) const {
  if (index.row() < 0 || index.row() >= this->m_stations.count())
    return QVariant();

  if (role == StationModel::positionRole) {
    return QVariant::fromValue(this->m_stations[index.row()].coordinate());
  } else if (role == StationModel::stationIDRole) {
    return QVariant::fromValue(this->m_stations[index.row()].id());
  } else if (role == StationModel::stationNameRole) {
    return QVariant::fromValue(this->m_stations[index.row()].name());
  } else if (role == StationModel::latitudeRole) {
    return QVariant::fromValue(
        this->m_stations[index.row()].coordinate().latitude());
  } else if (role == StationModel::longitudeRole) {
    return QVariant::fromValue(
        this->m_stations[index.row()].coordinate().longitude());
  } else if (role == StationModel::measuredRole) {
    return QVariant::fromValue(this->m_stations[index.row()].measured());
  } else if (role == StationModel::modeledRole) {
    return QVariant::fromValue(this->m_stations[index.row()].modeled());
  } else if (role == StationModel::selectedRole) {
    return QVariant::fromValue(this->m_stations[index.row()].selected());
  } else {
    return QVariant();
  }
}

QHash<int, QByteArray> StationModel::roleNames() const {
  QHash<int, QByteArray> roles;
  roles[positionRole] = "position";
  roles[stationIDRole] = "id";
  roles[stationNameRole] = "name";
  roles[latitudeRole] = "latitude";
  roles[longitudeRole] = "longitude";
  roles[modeledRole] = "modeled";
  roles[measuredRole] = "measured";
  roles[selectedRole] = "selected";
  return roles;
}

Station StationModel::findStation(QString stationName) const {
  if (this->m_stationMap.contains(stationName)) {
    return this->m_stationMap[stationName];
  } else {
    return Station();
  }
}

void StationModel::selectStation(QString name) {
  if (this->m_stationLocationMap.contains(name)) {
    this->m_stations[this->m_stationLocationMap[name]].setSelected(true);
  }
  return;
}

void StationModel::deselectStation(QString name) {
  if (this->m_stationLocationMap.contains(name)) {
    this->m_stations[this->m_stationLocationMap[name]].setSelected(false);
  }
  return;
}
