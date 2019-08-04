/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2019  Zach Cobell
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

#include <QDateTime>
#include <QGeoCoordinate>
#include "metocean_global.h"

class Station {
 public:
  Station();
  Station(QGeoCoordinate coordinate, QString id, QString name,
          double measured = 0.0, double modeled = 0.0, int category = 0,
          bool active = true,
          QDateTime startValidDate = QDateTime(QDate(1900, 1, 1),
                                               QTime(0, 0, 0)),
          QDateTime endValidDate = QDateTime(QDate(2050, 1, 1),
                                             QTime(0, 0, 0)));

  ~Station();

  QGeoCoordinate coordinate() const;
  void setCoordinate(const QGeoCoordinate &coordinate);
  void setLatitude(const double latitude);
  void setLongitude(const double longitude);

  QString name() const;
  void setName(const QString &name);

  QString id() const;
  void setId(const QString &id);

  bool selected() const;
  void setSelected(bool selcted);

  double modeled() const;
  void setModeled(double modeled);

  double measured() const;
  void setMeasured(double measured);

  int category() const;
  void setCategory(int category);

  double difference() const;

  QDateTime startValidDate() const;
  void setStartValidDate(const QDateTime &startValidDate);

  QDateTime endValidDate() const;
  void setEndValidDate(const QDateTime &endValidDate);

  bool active() const;
  void setActive(bool active);

 private:
  QGeoCoordinate m_coordinate;
  QString m_name;
  QString m_id;
  double m_modeled;
  double m_measured;
  int m_category;
  bool m_selected;
  bool m_active;
  QDateTime m_startValidDate;
  QDateTime m_endValidDate;
};
Q_DECLARE_METATYPE(Station)

#endif  // STATION_H
