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
#include "ndbcdata.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QStringList>
#include <array>
#include <vector>

#include "stringutil.h"
#include "timefunc.h"

static constexpr std::array<std::string_view, 15> c_dataTypes() {
  return {"WD",  "WDIR", "WSPD", "GST",  "WVHT", "DPD", "APD", "MWD",
          "BAR", "PRES", "ATMP", "WTMP", "DEWP", "VIS", "TIDE"};
}

static constexpr std::array<std::string_view, 15> c_dataNames() {
  return {"Wind Direction",
          "Wind Direction",
          "Wind Speed",
          "Wind Gusts",
          "Wave Height",
          "Dominant Wave Period",
          "Average Wave Period",
          "Mean Wave Direction",
          "Barometric Pressure",
          "Atmospheric Pressure",
          "Air Temperature",
          "Water Temperature",
          "Dewpoint",
          "Visibility",
          "Water Level"};
}

constexpr std::array<std::string_view, 15> c_dataUnits() {
  return {"deg", "deg", "m/s",  "m/s",  "m",    "s",  "s", "deg",
          "mb",  "mb",  "degC", "degC", "degC", "nm", "m"};
}

NdbcData::NdbcData(MovStation &station, QDateTime startDate, QDateTime endDate)
    : WaterData(station, startDate, endDate) {}

std::string_view NdbcData::units(const std::string_view &parameter) {
  auto it = std::find(c_dataNames().begin(), c_dataNames().end(), parameter);
  return it == c_dataNames().end() ? std::string_view("--")
                                   : c_dataUnits()[it - c_dataNames().begin()];
  for (size_t i = 0; i < c_dataNames().size(); ++i) {
    if (c_dataNames().at(i) == parameter)
      return c_dataUnits().at(i);
  }
  return std::string_view("--");
}

int NdbcData::retrieveData(Hmdf::HmdfData *data, Datum::VDatum datum) {
  Q_UNUSED(datum)
  int yearStart = startDate().date().year();
  int yearEnd = endDate().date().year();

  std::vector<std::vector<std::string>> ndbcResponse;

  for (int i = yearStart; i <= yearEnd; ++i) {
    QUrl url = QUrl(QString::fromStdString(
        "https://www.ndbc.noaa.gov/view_text_file.php?filename=" +
        this->station().id().toStdString() + "h" + std::to_string(i) +
        ".txt.gz&dir=data/historical/stdmet/"));
    this->download(url, ndbcResponse);
  }

  if (ndbcResponse.size() == 0)
    return 1;

  return this->formatNdbcResponse(ndbcResponse, data);
}

int NdbcData::download(QUrl url,
                       std::vector<std::vector<std::string>> &dldata) {
  // Send the request
  std::unique_ptr<QNetworkAccessManager> manager(new QNetworkAccessManager());
  QEventLoop loop;
  QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(url)));
  QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
  QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), &loop,
                   SLOT(quit()));
  loop.exec();

  QVariant redirectionTargetURL =
      reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
  if (!redirectionTargetURL.isNull()) {
    QNetworkReply *reply2 =
        manager->get(QNetworkRequest(redirectionTargetURL.toUrl()));
    QObject::connect(reply2, SIGNAL(finished()), &loop, SLOT(quit()));
    QObject::connect(reply2, SIGNAL(error(QNetworkReply::NetworkError)), &loop,
                     SLOT(quit()));
    loop.exec();
    reply->deleteLater();
    return this->readNdbcResponse(reply2, dldata);
  } else {
    return this->readNdbcResponse(reply, dldata);
  }
}

int NdbcData::readNdbcResponse(QNetworkReply *reply,
                               std::vector<std::vector<std::string>> &data) {
  // Catch some errors during the download
  if (reply->error() != 0) {
    this->setErrorString("ERROR: " + reply->errorString().toStdString());
    reply->deleteLater();
    return 1;
  }

  // Store the data
  QByteArray serverData = reply->readAll();
  std::vector<std::string> d =
      StringUtil::stringSplitToVector(serverData.toStdString());

  if (d.size() > 4)
    data.push_back(d);

  // Delete this response
  reply->deleteLater();

  return 0;
}

int NdbcData::formatNdbcResponse(
    std::vector<std::vector<std::string>> &serverResponse,
    Hmdf::HmdfData *data) {
  //...Create stations
  std::vector<std::string> d =
      StringUtil::stringSplitToVector(serverResponse[0][0]);
  std::vector<Hmdf::Station> st;

  int p = 0;
  for (size_t i = 0; i < serverResponse[0].size(); i++) {
    if (serverResponse[0][i].substr(0, 1) != "#") {
      p = i;
      break;
    }
  }

  size_t q = 0;
  size_t r = 0;
  if (d[4] == "mm") {
    q = 16;
    r = 5;
  } else {
    q = 13;
    r = 4;
  }

  size_t n = d.size() - r;

  for (size_t i = 0; i < n; i++) {
    Hmdf::Station s(i, this->station().coordinate().longitude(),
                    this->station().coordinate().latitude());

    auto it = std::find(c_dataTypes().begin(), c_dataTypes().end(), d[i + r]);
    if (it != c_dataTypes().end()) {
      s.setName(std::string(*(it)));
    } else {
      s.setName(d[i + r]);
    }
    s.setId(d[i + r]);
    st.push_back(s);
  }

  qint64 start = this->startDate().toMSecsSinceEpoch();
  qint64 end = this->endDate().toMSecsSinceEpoch();

  for (auto &i : serverResponse) {
    for (size_t j = p; j < i.size(); j++) {
      std::string m = i[j].substr(0, q);
      std::string ds = StringUtil::sanitizeString(m);

      QDateTime dt;
      if (q == 13) {
        dt = QDateTime::fromString(QString::fromStdString(ds), "yyyy MM dd hh");
      } else if (q == 16) {
        dt = QDateTime::fromString(QString::fromStdString(ds),
                                   "yyyy MM dd hh mm");
      }
      dt.setTimeSpec(Qt::UTC);

      if (dt.isValid()) {
        std::string vs = i[j].substr(q, i[j].length() - q);
        std::vector<std::string> v = StringUtil::stringSplitToVector(vs);
        qint64 dm = dt.toMSecsSinceEpoch();
        if (dm >= start && dm <= end) {
          for (size_t k = 0; k < n; k++) {
            if (v[k] != "999" && v[k] != "99.0" && v[k] != "99.00" &&
                v[k] != "999.0") {
              double vl = std::stod(v[k]);
              st[k] << Hmdf::Timepoint(Timefunc::fromQDateTime(dt), vl);
            }
          }
        }
      }
    }
  }

  //...Check for null values
  for (size_t i = st.size() - 1; i >= 0; i--) {
    if (st[i].size() < 3) {
      st.erase(st.begin() + i);
    }
  }

  for (auto &i : st) {
    data->moveStation(std::move(i));
  }

  if (data->nStations() == 0) {
    this->setErrorString("No valid station data found.");
    return 1;
  }

  return 0;
}

std::array<std::string_view, 15> NdbcData::dataNames() { return c_dataNames(); }

std::array<std::string_view, 15> NdbcData::dataTypes() { return c_dataTypes(); }
