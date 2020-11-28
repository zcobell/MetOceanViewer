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
#include "usgswaterdata.h"

#include "timefunc.h"
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <utility>
#include <vector>

UsgsWaterdata::UsgsWaterdata(MovStation &station, QDateTime startDate,
                             QDateTime endDate, int databaseOption)
    : WaterData(station, std::move(startDate), std::move(endDate)),
      m_databaseOption(databaseOption) {}

int UsgsWaterdata::get(Hmdf::HmdfData *data, Datum::VDatum datum) {
  Q_UNUSED(datum)
  return this->fetch(data);
}

int UsgsWaterdata::fetch(Hmdf::HmdfData *data) {
  if (this->station().id().isNull() || this->station().id().isEmpty()) {
    this->setErrorString("You must select a station");
    return 1;
  }
  QUrl request = this->buildUrl();

  return this->download(request, data);
}

QUrl UsgsWaterdata::buildUrl() {
  //...Format the date strings
  QString endDateString1 =
      "&endDT=" + this->endDate().addDays(1).toString("yyyy-MM-dd");
  QString startDateString1 =
      "&startDT=" + this->startDate().toString("yyyy-MM-dd");

  //...Construct the correct request URL
  QString requestUrl;
  if (this->m_databaseOption == 0 || this->m_databaseOption == 1)
    requestUrl = "https://waterservices.usgs.gov/nwis/iv/?sites=" +
                 this->station().id() + startDateString1 + endDateString1 +
                 "&format=json";
  else
    requestUrl = "https://waterservices.usgs.gov/nwis/dv/?sites=" +
                 this->station().id() + startDateString1 + endDateString1 +
                 "&format=json";

  return QUrl(requestUrl);
}

int UsgsWaterdata::download(const QUrl &url, Hmdf::HmdfData *data) {
  std::unique_ptr<QNetworkAccessManager> manager(new QNetworkAccessManager());
  QEventLoop loop;

  //...Make the request to the server
  QNetworkReply *reply = manager->get(QNetworkRequest(url));
  QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
  QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), &loop,
                   SLOT(quit()));
  loop.exec();

  if (reply->error() != QNetworkReply::NoError) {
    this->setErrorString("There was an error contacting the USGS data server");
    return 1;
  }

  int ierr = this->readDownloadedData(reply, data);

  reply->deleteLater();
  return ierr;
}

int UsgsWaterdata::readDownloadedData(QNetworkReply *reply,
                                      Hmdf::HmdfData *output) {
  QByteArray data = reply->readAll();
  return this->readUsgsDataJson(data, output);
}

int UsgsWaterdata::readUsgsDataJson(const QByteArray &data,
                                    Hmdf::HmdfData *output) {

  auto doc = QJsonDocument::fromJson(data);
  auto obj = doc.object();
  auto valueObj = obj["value"].toObject();
  auto tsObject = valueObj["timeSeries"].toArray();

  if (tsObject.empty()) {
    this->setErrorString("No variables found for this station.");
    return 1;
  }

  for (auto j : tsObject) {
    auto jo = j.toObject();
    auto vo = jo["variable"].toObject();
    auto vc = vo["variableCode"]
                  .toArray()[0]
                  .toObject()["value"]
                  .toString()
                  .toStdString();
    auto des = vo["variableDescription"].toString().toStdString();
    auto unit = vo["unit"].toObject()["unitCode"].toString().toStdString();
    auto vdata = jo["values"].toArray()[0].toObject()["value"].toArray();
    auto options = vo["options"].toObject()["option"].toArray()[0].toObject();
    auto defaultValue = vo["noDataValue"].toDouble();
    auto code = options["optionCode"].toString();
    if (code != "00000") {
      auto stat = options["value"].toString().toStdString();
      des = des.substr(0, des.rfind(',')) + ", " + stat + "," +
            des.substr(des.rfind(',') + 1, des.length());
    }

    Hmdf::Station stn;
    stn.setId(vc);
    stn.setName(des);
    stn.setUnits(unit);

    for (auto val : vdata) {
      auto vv = val.toObject();
      auto date = Timefunc::fromQDateTime(
          QDateTime::fromString(vv["dateTime"].toString(), Qt::ISODate)
              .toTimeSpec(Qt::UTC));
      double value = vv["value"].toString().toDouble();
      if (value == defaultValue)
        stn << Hmdf::Timepoint(date, Hmdf::Timepoint::nullValue());
      else
        stn << Hmdf::Timepoint(date, value);
    }
    output->moveStation(std::move(stn));
  }

  return 0;
}