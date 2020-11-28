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
#include "tideprediction.h"

#include <QFile>
#include <QString>
#include <memory>
#include <string>

#include "libxtide.hh"
#include "stringutil.h"
#include "timefunc.h"

TidePrediction::TidePrediction(const std::string &root)
    : m_harmonicsDatabase(root + "/harmonics.tcd") {
  this->initHarmonicsDatabase();
}

TidePrediction::~TidePrediction() {
  if (this->m_deleteHarmonicsOnExit) {
    QFile file(QString::fromStdString(this->m_harmonicsDatabase));
    if (file.exists())
      file.remove();
  }
}

void TidePrediction::initHarmonicsDatabase() {
  QFile harm(QString::fromStdString(this->m_harmonicsDatabase));
  if (!harm.exists()) {
    Q_INIT_RESOURCE(resource_files);
    QFile::copy(":/rsc/harmonics.tcd",
                QString::fromStdString(this->m_harmonicsDatabase));
  }
}

void TidePrediction::deleteHarmonicsOnExit(bool b) {
  this->m_deleteHarmonicsOnExit = b;
}

int TidePrediction::get(MovStation &s, QDateTime startDate, QDateTime endDate,
                        int interval, Hmdf::HmdfData *data) {
  Hmdf::Station st(0, s.coordinate().longitude(), s.coordinate().latitude());

  st.setName(s.name().toStdString());
  st.setId(s.id().toStdString());

  const libxtide::StationRef *sr =
      libxtide::Global::stationIndex(this->m_harmonicsDatabase.c_str())
          .getStationRefByName(s.name().toStdString().c_str());

  if (sr) {
    std::unique_ptr<libxtide::Station> station(sr->load());

    station->step = interval;

    startDate.setTime(QTime(0, 0, 0));
    endDate.setTime(QTime(0, 0, 0));

    libxtide::Timestamp startTime = libxtide::Timestamp(
        startDate.toString("yyyy-MM-dd hh:mm").toStdString().c_str(),
        sr->timezone);
    libxtide::Timestamp endTime = libxtide::Timestamp(
        endDate.toString("yyyy-MM-dd hh:mm").toStdString().c_str(),
        sr->timezone);

    station->setUnits(libxtide::Units::meters);

    Dstr text_out;
    station->print(text_out, startTime, endTime, libxtide::Mode::mediumRare,
                   libxtide::Format::text);

    std::vector<std::string> tide =
        StringUtil::stringSplitToVector(std::string(text_out.aschar()), "\n");

    for (auto &i : tide) {
      std::string datestr = StringUtil::sanitizeString(i).substr(0, 20);
      std::string val = StringUtil::sanitizeString(i).substr(23, i.length());

      int year = stoi(datestr.substr(0, 4));
      int month = stoi(datestr.substr(5, 2));
      int day = stoi(datestr.substr(8, 2));
      int hour = stoi(datestr.substr(11, 2));
      int minute = stoi(datestr.substr(14, 2));
      bool ampm = datestr.substr(17, 2) == "PM";
      if (!ampm && hour == 12) {
        hour = 0;
      } else if (ampm && hour != 12) {
        hour += 12;
      }

      Hmdf::Date dt(year, month, day, hour, minute);
      st << Hmdf::Timepoint(dt, stod(val));
    }
    data->addStation(st);

    return 0;
  } else {
    return 1;
  }
}
