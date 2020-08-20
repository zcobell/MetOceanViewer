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
#ifndef MOVSTATION_H
#define MOVSTATION_H

#include <QDateTime>
#include <QGeoCoordinate>
#include <array>

#include "datum.h"
#include "metocean_global.h"

class MovStation {
 public:
  METOCEANSHARED_EXPORT MovStation();
  METOCEANSHARED_EXPORT MovStation(QGeoCoordinate coordinate, QString id, QString name,
      double measured = 0.0, double modeled = 0.0, int category = 0,
      bool active = true,
      QDateTime startValidDate = QDateTime(QDate(1900, 1, 1), QTime(0, 0, 0)),
      QDateTime endValidDate = QDateTime(QDate(2050, 1, 1), QTime(0, 0, 0)));

  METOCEANSHARED_EXPORT ~MovStation();

  bool METOCEANSHARED_EXPORT operator==(const MovStation &s);

  QGeoCoordinate METOCEANSHARED_EXPORT coordinate() const;
  void METOCEANSHARED_EXPORT setCoordinate(const QGeoCoordinate &coordinate);
  void METOCEANSHARED_EXPORT setLatitude(const double latitude);
  void METOCEANSHARED_EXPORT setLongitude(const double longitude);

  QString name() const;
  void METOCEANSHARED_EXPORT setName(const QString &name);

  QString id() const;
  void METOCEANSHARED_EXPORT setId(const QString &id);

  bool METOCEANSHARED_EXPORT selected() const;
  void METOCEANSHARED_EXPORT setSelected(bool selcted);

  double METOCEANSHARED_EXPORT modeled() const;
  void METOCEANSHARED_EXPORT setModeled(double modeled);

  double METOCEANSHARED_EXPORT measured() const;
  void METOCEANSHARED_EXPORT setMeasured(double measured);

  int METOCEANSHARED_EXPORT category() const;
  void METOCEANSHARED_EXPORT setCategory(int category);

  double METOCEANSHARED_EXPORT difference() const;

  QDateTime METOCEANSHARED_EXPORT startValidDate() const;
  void METOCEANSHARED_EXPORT setStartValidDate(const QDateTime &startValidDate);

  QDateTime METOCEANSHARED_EXPORT endValidDate() const;
  void METOCEANSHARED_EXPORT setEndValidDate(const QDateTime &endValidDate);

  bool METOCEANSHARED_EXPORT active() const;
  void METOCEANSHARED_EXPORT setActive(bool active);

  double METOCEANSHARED_EXPORT navd88Offset() const;
  void METOCEANSHARED_EXPORT setNavd88Offset(double navd88Offset);

  double METOCEANSHARED_EXPORT mslOffset() const;
  void METOCEANSHARED_EXPORT setMslOffset(double mslOffset);

  double METOCEANSHARED_EXPORT ngvd29Offset() const;
  void METOCEANSHARED_EXPORT setNgvd29Offset(double ngvd29Offset);

  double METOCEANSHARED_EXPORT mlwOffset() const;
  void METOCEANSHARED_EXPORT setMlwOffset(double mlwOffset);

  double METOCEANSHARED_EXPORT mllwOffset() const;
  void METOCEANSHARED_EXPORT setMllwOffset(double mllwOffset);

  double METOCEANSHARED_EXPORT mhwOffset() const;
  void METOCEANSHARED_EXPORT setMhwOffset(double mhwOffset);

  double METOCEANSHARED_EXPORT mhhwOffset() const;
  void METOCEANSHARED_EXPORT setMhhwOffset(double mhhwOffset);

  double METOCEANSHARED_EXPORT offset(const Datum::VDatum &d) const;
  void METOCEANSHARED_EXPORT setOffset(const Datum::VDatum &d,
                                       const double value);

  static constexpr double METOCEANSHARED_EXPORT nullOffset() { return -9999.0; }
  bool METOCEANSHARED_EXPORT isNullOffset(double offset);

 private:
  QGeoCoordinate m_coordinate;
  QString m_name;
  QString m_id;
  std::array<double, 7> m_offset;
  double m_modeled;
  double m_measured;
  int m_category;
  bool m_selected;
  bool m_active;
  QDateTime m_startValidDate;
  QDateTime m_endValidDate;
};
Q_DECLARE_METATYPE(MovStation)

#endif  // MOVTATION_H
