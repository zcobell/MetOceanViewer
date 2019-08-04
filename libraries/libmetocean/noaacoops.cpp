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
#include "noaacoops.h"
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include "boost/algorithm/string/split.hpp"
#include "boost/algorithm/string/trim.hpp"
#include "boost/config/warning_disable.hpp"
#include "boost/spirit/include/phoenix.hpp"
#include "boost/spirit/include/qi.hpp"

NoaaCoOps::NoaaCoOps(Station &station, QDateTime startDate, QDateTime endDate,
                     QString product, QString datum, QString units,
                     QObject *parent)
    : WaterData(station, startDate, endDate, parent) {
  this->m_product = product;
  this->m_units = units;
  this->m_datum = datum;
  this->m_useJson = true;
  this->parseProduct();
}

int NoaaCoOps::parseProduct() {
  this->m_productParsed = this->m_product.split(":");
  return 0;
}

int NoaaCoOps::retrieveData(Hmdf *data) {
  QVector<QDateTime> startDateList, endDateList;
  std::vector<std::string> rawNoaaData;
  int ierr = this->generateDateRanges(startDateList, endDateList);
  if (ierr != 0) return ierr;
  ierr =
      this->downloadDataFromNoaaServer(startDateList, endDateList, rawNoaaData);
  if (ierr != 0) return ierr;
  ierr = this->formatNoaaResponse(rawNoaaData, data);
  if (ierr != 0) return ierr;
  return 0;
}

int NoaaCoOps::generateDateRanges(QVector<QDateTime> &startDateList,
                                  QVector<QDateTime> &endDateList) {
  long long numDownloads = (this->startDate().daysTo(this->endDate()) / 30) + 1;

  this->startDate().setTime(QTime(this->startDate().time().hour(),
                                  this->startDate().time().minute(), 0));
  this->endDate().setTime(
      QTime(this->endDate().time().hour(), this->endDate().time().minute(), 0));

  // Build the list of dates in 30 day intervals
  for (int i = 0; i < numDownloads; i++) {
    QDateTime startDate;
    if (i == 0)
      startDate = this->startDate().addDays(i * 30).addDays(i);
    else
      startDate = endDateList.last();

    QDateTime endDate = startDate.addDays(30);
    if (endDate > this->endDate()) endDate = this->endDate();
    startDateList.push_back(startDate);
    endDateList.push_back(endDate);
  }
  return 0;
}

int NoaaCoOps::downloadDataFromNoaaServer(
    QVector<QDateTime> startDateList, QVector<QDateTime> endDateList,
    std::vector<std::string> &downloadedData) {
  QNetworkAccessManager *manager = new QNetworkAccessManager(this);

  for (int i = 0; i < startDateList.length(); i++) {
    // Make the date string
    QString startString =
        startDateList[i].toString(QStringLiteral("yyyyMMdd hh:mm"));
    QString endString =
        endDateList[i].toString(QStringLiteral("yyyyMMdd hh:mm"));

    //...Select parser type
    QString format;
    if (this->m_useJson) {
      format = "json";
    } else {
      format = "csv";
    }

    // Build the URL to request data from the NOAA CO-OPS API
    QString requestURL =
        QStringLiteral("http://tidesandcurrents.noaa.gov/api/datagetter?") +
        QStringLiteral("product=") + this->m_productParsed[0] +
        QStringLiteral("&application=metoceanviewer") +
        QStringLiteral("&begin_date=") + startString +
        QStringLiteral("&end_date=") + endString + QStringLiteral("&station=") +
        this->station().id() + QStringLiteral("&time_zone=GMT&units=") +
        this->m_units + QStringLiteral("&interval=&format=") + format;

    // Allow a different datum where allowed
    if (this->m_datum != QStringLiteral("Stnd"))
      requestURL = requestURL + QStringLiteral("&datum=") + this->m_datum;

    // Send the request
    QEventLoop loop;
    QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(requestURL)));
    connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), &loop,
            SLOT(quit()));
    loop.exec();

    //...Check for a redirect from NOAA. This fixes bug #26
    QVariant redirectionTargetURL =
        reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (!redirectionTargetURL.isNull()) {
      QNetworkReply *reply2 =
          manager->get(QNetworkRequest(redirectionTargetURL.toUrl()));
      connect(reply2, SIGNAL(finished()), &loop, SLOT(quit()));
      connect(reply2, SIGNAL(error(QNetworkReply::NetworkError)), &loop,
              SLOT(quit()));
      loop.exec();
      reply->deleteLater();
      this->readNoaaResponse(reply2, downloadedData);
    } else
      this->readNoaaResponse(reply, downloadedData);
  }

  return 0;
}

int NoaaCoOps::readNoaaResponse(QNetworkReply *reply,
                                std::vector<std::string> &downloadedData) {
  // Catch some errors during the download
  if (reply->error() != 0) {
    this->setErrorString(QStringLiteral("ERROR: ") + reply->errorString());
    reply->deleteLater();
    return 1;
  }

  // Store the data at the back of the vector
  downloadedData.push_back(static_cast<std::string>(reply->readAll()));

  // Delete this response
  reply->deleteLater();

  return 0;
}

int NoaaCoOps::formatNoaaResponse(std::vector<std::string> &downloadedData,
                                  Hmdf *outputData) {
  if (this->m_useJson) {
    return this->formatNoaaResponseJson(downloadedData, outputData);
  } else {
    return this->formatNoaaResponseCsv(downloadedData, outputData);
  }
}

void NoaaCoOps::parseCsvToValuePair(std::string &data, QDateTime &date,
                                    double &value) {
  namespace qi = boost::spirit::qi;
  namespace ascii = boost::spirit::ascii;
  namespace phoenix = boost::phoenix;
  std::string dir;
  std::vector<double> v2(3);
  int year, month, day, hour, minute;
  qi::phrase_parse(data.begin(), data.begin() + 10,
                   (qi::int_ >> qi::int_ >> qi::int_), qi::skip['-'], year,
                   month, day);
  qi::phrase_parse(data.begin() + 11, data.begin() + 16, (qi::int_ >> qi::int_),
                   qi::skip[':'], hour, minute);
  qi::phrase_parse(data.begin() + 17, data.end(),
                   (qi::double_ >> qi::double_ >>
                    qi::lexeme[+(qi::char_ - ',')] >> qi::double_),
                   qi::skip[','], v2[0], v2[1], dir, v2[2]);
  date = QDateTime(QDate(year, month, day), QTime(hour, minute, 0), Qt::UTC);

  if (this->m_productParsed.size() > 1) {
    if (this->m_productParsed[1] == "speed") {
      value = v2[0];
    } else if (this->m_productParsed[1] == "direction") {
      value = v2[1];
    } else if (this->m_productParsed[1] == "gusts") {
      value = v2[2];
    }
  } else {
    value = v2[0];
  }
  return;
}

int NoaaCoOps::formatNoaaResponseCsv(std::vector<std::string> &downloadedData,
                                     Hmdf *outputData) {
  std::vector<std::vector<std::string>> data(downloadedData.size());

  for (size_t i = 0; i < downloadedData.size(); ++i) {
    boost::algorithm::split(data[i], downloadedData[i], boost::is_any_of("\n"),
                            boost::token_compress_on);
  }

  HmdfStation *station = new HmdfStation(outputData);
  station->setCoordinate(this->station().coordinate());
  station->setName(this->station().name());
  station->setId(this->station().id());
  station->setStationIndex(0);

  for (auto &d : data) {
    if (d.size() > 3) {
      for (auto &d2 : d) {
        if (d2.size() == 0) continue;

        QDateTime date = QDateTime();
        double value = HmdfStation::nullDataValue();
        this->parseCsvToValuePair(d2, date, value);

        if (std::abs(value - HmdfStation::nullDataValue()) > 0.001 &&
            date.isValid()) {
          qint64 t = date.toMSecsSinceEpoch();
          if (station->numSnaps() > 0) {
            if (station->date(station->numSnaps() - 1) != t)
              station->setNext(t, value);
          } else {
            station->setNext(t, value);
          }
        }
      }
    }
  }
  outputData->addStation(station);

  if (outputData->station(0)->numSnaps() < 5) {
    this->setErrorString(QStringLiteral("No valid data was found."));
    return 1;
  }

  return 0;
}

int NoaaCoOps::formatNoaaResponseJson(std::vector<std::string> &downloadedData,
                                      Hmdf *outputData) {
  HmdfStation *station = new HmdfStation(outputData);
  station->setCoordinate(this->station().coordinate());
  station->setName(this->station().name());
  station->setId(this->station().id());
  station->setStationIndex(0);

  for (size_t i = 0; i < downloadedData.size(); i++) {
    std::string data = downloadedData[i];
    QJsonDocument jsonData =
        QJsonDocument::fromJson(QString::fromStdString(data).toUtf8());
    QJsonObject jsonObj = jsonData.object();

    QJsonArray jsonArr;
    if (jsonObj.contains("data"))
      jsonArr = jsonObj["data"].toArray();
    else if (jsonObj.contains("predictions"))
      jsonArr = jsonObj["predictions"].toArray();
    else {
      QJsonValue val = jsonData.object()["error"];
      QJsonObject obj = val.toObject();
      QJsonValue val2 = obj["message"];
      this->setErrorString(val2.toString());
    }

    //...Ditch duplicate data
    size_t start;
    if (i == 0)
      start = 0;
    else
      start = 1;

    for (size_t j = start; j < jsonArr.size(); j++) {
      QJsonObject obj = jsonArr[j].toObject();
      QDateTime t =
          QDateTime::fromString(obj["t"].toString(), "yyyy-MM-dd hh:mm");
      t.setTimeSpec(Qt::UTC);
      bool ok = false;
      double v = station->nullValue();
      if (this->m_productParsed.size() == 1) {
        v = obj["v"].toString().toDouble(&ok);
      } else {
        if (this->m_productParsed[1] == "speed") {
          v = obj["s"].toString().toDouble(&ok);
        } else if (this->m_productParsed[1] == "direction") {
          v = obj["d"].toString().toDouble(&ok);
        } else if (this->m_productParsed[1] == "gusts") {
          v = obj["g"].toString().toDouble(&ok);
        }
      }
      if (t.isValid() && ok) {
        station->setNext(t.toMSecsSinceEpoch(), v);
      }
    }
  }

  if (station->numSnaps() > 3) {
    outputData->addStation(station);
    return 0;
  } else {
    return 1;
  }
}
