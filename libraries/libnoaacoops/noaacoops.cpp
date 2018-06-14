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
#include "noaacoops.h"
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

NoaaCoOps::NoaaCoOps(QString station, QGeoCoordinate location, QString product,
                     QString datum, QString units, QDateTime startDate,
                     QDateTime endDate, QObject *parent)
    : QObject(parent) {
  this->m_stationId = station;
  this->m_product = product;
  this->m_datum = datum;
  this->m_units = units;
  this->m_location = location;
  this->m_startDate = startDate;
  this->m_endDate = endDate;
}

int NoaaCoOps::get(Hmdf *data) { return this->downloadData(data); }

QString NoaaCoOps::errorString() { return this->m_errorString; }

int NoaaCoOps::downloadData(Hmdf *data) {
  QVector<QDateTime> startDateList, endDateList;
  QVector<QByteArray> rawNoaaData;
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
  int numDownloads = (this->m_startDate.daysTo(this->m_endDate) / 30) + 1;

  // Build the list of dates in 30 day intervals
  for (int i = 0; i < numDownloads; i++) {
    QDateTime startDate = this->m_startDate.addDays(i * 30).addDays(i);
    QDateTime endDate = startDate.addDays(30);
    if (endDate > this->m_endDate) endDate = this->m_endDate;
    startDateList.push_back(startDate);
    endDateList.push_back(endDate);
  }
  return 0;
}

int NoaaCoOps::downloadDataFromNoaaServer(QVector<QDateTime> startDateList,
                                          QVector<QDateTime> endDateList,
                                          QVector<QByteArray> &downloadedData) {
  QNetworkAccessManager *manager = new QNetworkAccessManager(this);

  for (int i = 0; i < startDateList.length(); i++) {
    // Make the date string
    QString startString = startDateList[i].toString("yyyyMMdd hh:mm");
    QString endString = endDateList[i].toString("yyyyMMdd hh:mm");

    // Build the URL to request data from the NOAA CO-OPS API
    QString requestURL =
        QString("http://tidesandcurrents.noaa.gov/api/datagetter?") +
        QString("product=" + this->m_product + "&application=metoceanviewer") +
        QString("&begin_date=") + startString + QString("&end_date=") +
        endString + QString("&station=") + this->m_stationId +
        QString("&time_zone=GMT&units=") + this->m_units +
        QString("&interval=&format=csv");

    // Allow a different datum where allowed
    if (this->m_datum != "Stnd")
      requestURL = requestURL + QString("&datum=") + this->m_datum;

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
                                QVector<QByteArray> &downloadedData) {
  // Catch some errors during the download
  if (reply->error() != 0) {
    this->m_errorString = "ERROR: " + reply->errorString();
    reply->deleteLater();
    return 1;
  }

  // Store the data at the back of the vector
  downloadedData.push_back(reply->readAll());

  // Delete this response
  reply->deleteLater();

  return 0;
}

int NoaaCoOps::formatNoaaResponse(QVector<QByteArray> &downloadedData,
                                  Hmdf *outputData) {
  QVector<QStringList> data;
  QString error;

  data.resize(downloadedData.size());

  for (int i = 0; i < downloadedData.size(); i++) {
    data[i] = QString(downloadedData[i])
                  .split(QRegExp("[\r\n]"), QString::SkipEmptyParts);
    error = QString(downloadedData[i]) + "\n";
  }

  HmdfStation *station = new HmdfStation(outputData);
  station->setCoordinate(this->m_location);
  station->setName(this->m_stationId);
  station->setStationIndex(0);

  for (int i = 0; i < data.size(); i++) {
    if (data[i].size() > 3) {
      for (int j = 1; j < data[i].size(); j++) {
        int year, month, day, hour, minute;
        double value;
        const QRegExp rx("-|:");

        QStringList t = data[i][j].split(",");
        QStringList d = t[0].replace(rx, " ").split(" ");

        year = d[0].toInt();
        month = d[1].toInt();
        day = d[2].toInt();
        hour = d[3].toInt();
        minute = d[4].toInt();
        value = t[1].toDouble();

        QDateTime tempDate =
            QDateTime(QDate(year, month, day), QTime(hour, minute, 0));
        if (value != 0.0 && tempDate.isValid()) {
          outputData->station(0)->setNext(tempDate.toMSecsSinceEpoch(), value);
        }
      }
    }
  }
  outputData->addStation(station);

  if (outputData->station(0)->numSnaps() < 5) {
    this->m_errorString = "No valid data";
    return 1;
  }

  return 0;
}
