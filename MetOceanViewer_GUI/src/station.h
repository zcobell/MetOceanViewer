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
#ifndef STATION_H
#define STATION_H

#include <QGeoCoordinate>

class Station {
 public:
  Station();
  Station(QGeoCoordinate coordinate, QString id, QString name);

  ~Station();

  QGeoCoordinate coordinate() const;
  void setCoordinate(const QGeoCoordinate &coordinate);

  QString name() const;
  void setName(const QString &name);

  QString id() const;
  void setId(const QString &id);

 private:
  QGeoCoordinate m_coordinate;
  QString m_name;
  QString m_id;
};
Q_DECLARE_METATYPE(Station)

#endif  // STATION_H
