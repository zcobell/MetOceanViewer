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
#ifndef WATERDATA_H
#define WATERDATA_H

#include <QNetworkReply>
#include <QObject>
#include "hmdf.h"
#include "station.h"
#include "timezone.h"

class WaterData : public QObject {
  Q_OBJECT
 public:
  explicit WaterData(Station station, QDateTime startDate, QDateTime endDate,
                     QObject *parent = nullptr);

  int get(Hmdf *data);

  QString errorString() const;

  Timezone *getTimezone() const;
  void setTimezone(Timezone *timezone);

protected:
  virtual int downloadData(Hmdf *data);

  void setErrorString(const QString &errorString);

  Station station() const;
  void setStation(const Station &station);

  QDateTime startDate() const;
  void setStartDate(const QDateTime &startDate);

  QDateTime endDate() const;
  void setEndDate(const QDateTime &endDate);

 private:
  QString m_errorString;
  Station m_station;
  QDateTime m_startDate;
  QDateTime m_endDate;
  Timezone *m_timezone;
};

#endif  // WATERDATA_H
