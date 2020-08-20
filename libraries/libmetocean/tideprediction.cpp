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
#include <string>

#include "libxtide.hh"
#include "stringutil.h"
#include "timefunc.h"

TidePrediction::TidePrediction(std::string root)
    : m_harmonicsDatabase(root + "/harmonics.tcd") {
  this->initHarmonicsDatabase();
}

TidePrediction::~TidePrediction() {
  if (this->m_deleteHarmonicsOnExit) {
    QFile file(QString::fromStdString(this->m_harmonicsDatabase));
    if (file.exists()) file.remove();
  }
  return;
}

void TidePrediction::initHarmonicsDatabase() {
  QFile harm(QString::fromStdString(this->m_harmonicsDatabase));
  if (!harm.exists()) {
    Q_INIT_RESOURCE(resource_files);
    QFile::copy(":/rsc/harmonics.tcd",
                QString::fromStdString(this->m_harmonicsDatabase));
  }
  return;
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

    // std::stringList tide = std::string(text_out.aschar()).split("\n");
    std::vector<std::string> tide =
        StringUtil::stringSplitToVector(std::string(text_out.aschar()), "\n");

    for (size_t i = 0; i < tide.size(); i++) {
      std::string datestr = StringUtil::sanitizeString(tide[i]).substr(0, 20);
      std::string val =
          StringUtil::sanitizeString(tide[i]).substr(23, tide[i].length());
      QDateTime d = QDateTime::fromString(QString::fromStdString(datestr),
                                          "yyyy-MM-dd h:mm AP");
      d.setTimeSpec(Qt::UTC);
      Hmdf::Date dt = Timefunc::fromQDateTime(d);
      if (d.isValid()) {
        st << Hmdf::Timepoint(dt, stod(val));
      }
    }
    data->addStation(st);

    return 0;
  } else {
    return 1;
  }
}
