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
#include "stationmodel.h"

StationModel::StationModel(QObject *parent) : QAbstractListModel(parent) {
  this->buildRoles();
}

void StationModel::buildRoles() {
  this->m_roles[positionRole] = "position";
  this->m_roles[stationIDRole] = "id";
  this->m_roles[stationNameRole] = "name";
  this->m_roles[latitudeRole] = "latitude";
  this->m_roles[longitudeRole] = "longitude";
  this->m_roles[modeledRole] = "modeled";
  this->m_roles[measuredRole] = "measured";
  this->m_roles[selectedRole] = "selected";
  this->m_roles[differenceRole] = "difference";
  this->m_roles[categoryRole] = "category";
  return;
}

void StationModel::addMarker(const Station &station) {
  this->beginInsertRows(QModelIndex(), rowCount(), rowCount());
  this->m_stations.append(station);
  this->m_stationMap[station.id()] =
      this->m_stations.at(this->m_stations.length() - 1);
  this->m_stationLocationMap[station.id()] = this->m_stations.length() - 1;
  this->endInsertRows();
}

void StationModel::addMarkers(QVector<Station> &stations) {
  for (int i = 0; i < stations.size(); i++) {
    this->addMarker(stations[i]);
  }
  return;
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
  } else if (role == StationModel::differenceRole) {
    return QVariant::fromValue(this->m_stations[index.row()].difference());
  } else if (role == StationModel::categoryRole) {
    return QVariant::fromValue(this->m_stations[index.row()].category());
  } else if (role == StationModel::selectedRole) {
    return QVariant::fromValue(this->m_stations[index.row()].selected());
  } else {
    return QVariant();
  }
}

QHash<int, QByteArray> StationModel::roleNames() const { return this->m_roles; }

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

void StationModel::boundingBox(QRectF &box) {
  for (int i = 0; i < this->m_stations.length(); i++) {
    if (i == 0) {
      box.setTopLeft(QPointF(this->m_stations[i].coordinate().longitude(),
                             this->m_stations[i].coordinate().latitude()));
      box.setBottomRight(box.topLeft());
    } else {
      box.setBottomLeft(
          QPointF(std::min(this->m_stations[i].coordinate().longitude(),
                           box.bottomLeft().x()),
                  std::min(this->m_stations[i].coordinate().latitude(),
                           box.bottomLeft().y())));
      box.setTopRight(
          QPointF(std::max(this->m_stations[i].coordinate().longitude(),
                           box.topRight().x()),
                  std::max(this->m_stations[i].coordinate().latitude(),
                           box.topRight().y())));
    }
  }
  return;
}

void StationModel::fitMarkers(QQuickWidget *quickWidget, StationModel *model) {
  //...Generate the bounding box, expand by 10% to give some margin
  QRectF boundingBox;
  model->boundingBox(boundingBox);

  double percent = 10;
  double width_new = boundingBox.width() * (1.0 + (percent / 100.0));
  double height_new = boundingBox.height() * (1.0 + (percent / 100.0));
  double dx = (width_new - boundingBox.width()) / 2.0;
  double dy = (height_new - boundingBox.height()) / 2.0;
  boundingBox.adjust(-dx, -dy, dx, dy);

  QObject *mapObject = quickWidget->rootObject();
  QMetaObject::invokeMethod(mapObject, "setVisibleRegion",
                            Q_ARG(QVariant, boundingBox.topLeft().x()),
                            Q_ARG(QVariant, boundingBox.topLeft().y()),
                            Q_ARG(QVariant, boundingBox.bottomRight().x()),
                            Q_ARG(QVariant, boundingBox.bottomRight().y()));
  return;
}

void StationModel::clear() {
  this->beginResetModel();
  this->m_stations.clear();
  this->endResetModel();
}
