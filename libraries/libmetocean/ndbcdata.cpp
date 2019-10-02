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

const QStringList c_dataTypes = QStringList() << "WD"
                                              << "WDIR"
                                              << "WSPD"
                                              << "GST"
                                              << "WVHT"
                                              << "DPD"
                                              << "APD"
                                              << "MWD"
                                              << "BAR"
                                              << "PRES"
                                              << "ATMP"
                                              << "WTMP"
                                              << "DEWP"
                                              << "VIS"
                                              << "TIDE";

const QStringList c_dataNames = QStringList() << "Wind Direction"
                                              << "Wind Direction"
                                              << "Wind Speed"
                                              << "Wind Gusts"
                                              << "Wave Height"
                                              << "Dominant Wave Period"
                                              << "Average Wave Period"
                                              << "Mean Wave Direction"
                                              << "Barometric Pressure"
                                              << "Atmospheric Pressure"
                                              << "Air Temperature"
                                              << "Water Temperature"
                                              << "Dewpoint"
                                              << "Visibility"
                                              << "Water Level";

NdbcData::NdbcData(Station &station, QDateTime startDate, QDateTime endDate,
                   QObject *parent)
    : WaterData(station, startDate, endDate, parent) {
  this->m_dataNameMap = this->buildDataNameMap();
}

QMap<QString, QString> NdbcData::buildDataNameMap() {
  QMap<QString, QString> map;
  for (size_t i = 0; i < c_dataTypes.size(); ++i) {
    map[c_dataTypes[i]] = c_dataNames[i];
  }
  return map;
}

int NdbcData::retrieveData(Hmdf *data, Datum::VDatum datum) {
  Q_UNUSED(datum)
  int yearStart = startDate().date().year();
  int yearEnd = endDate().date().year();

  QVector<QStringList> ndbcResponse;

  for (int i = yearStart; i <= yearEnd; i++) {
    QUrl url = QUrl("https://www.ndbc.noaa.gov/view_text_file.php?filename=" +
                    this->station().id() + "h" + QString::number(i) +
                    ".txt.gz&dir=data/historical/stdmet/");
    this->download(url, ndbcResponse);
  }

  if (ndbcResponse.length() == 0) return 1;

  return this->formatNdbcResponse(ndbcResponse, data);
}

int NdbcData::download(QUrl url, QVector<QStringList> &dldata) {
  // Send the request
  QNetworkAccessManager *manager = new QNetworkAccessManager(this);
  QEventLoop loop;
  QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(url)));
  connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
  connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), &loop,
          SLOT(quit()));
  loop.exec();

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
    return this->readNdbcResponse(reply2, dldata);
  } else {
    return this->readNdbcResponse(reply, dldata);
  }
}

int NdbcData::readNdbcResponse(QNetworkReply *reply,
                               QVector<QStringList> &data) {
  // Catch some errors during the download
  if (reply->error() != 0) {
    this->setErrorString(QStringLiteral("ERROR: ") + reply->errorString());
    reply->deleteLater();
    return 1;
  }

  // Store the data
  QByteArray serverData = reply->readAll();
  QStringList d = QString(serverData).split("\n");
  if (d.length() > 4) data.push_back(d);

  // Delete this response
  reply->deleteLater();

  return 0;
}

int NdbcData::formatNdbcResponse(QVector<QStringList> &serverResponse,
                                 Hmdf *data) {
  //...Create stations
  QStringList d = serverResponse[0].at(0).simplified().split(" ");
  QVector<HmdfStation *> st;

  int n, p = 0, q, r;

  for (int i = 0; i < serverResponse[0].size(); i++) {
    if (serverResponse[0][i].mid(0, 1) != "#") {
      p = i;
      break;
    }
  }

  if (d[4] == "mm") {
    q = 16;
    r = 5;
  } else {
    q = 13;
    r = 4;
  }

  n = d.length() - r;

  for (int i = 0; i < n; i++) {
    HmdfStation *s = new HmdfStation(data);

    s->setCoordinate(this->station().coordinate());
    if (this->m_dataNameMap.contains(d[i + r])) {
      s->setName(this->m_dataNameMap[d[i + r]]);
    } else {
      s->setName(d[i + r]);
    }
    s->setId(d[i + r]);
    s->setStationIndex(i);
    st.push_back(s);
  }

  qint64 start = this->startDate().toMSecsSinceEpoch();
  qint64 end = this->endDate().toMSecsSinceEpoch();

  for (int i = 0; i < serverResponse.length(); i++) {
    for (int j = p; j < serverResponse[i].length(); j++) {
      QString ds = serverResponse[i][j].mid(0, q).simplified();

      QDateTime d;
      if (q == 13)
        d = QDateTime::fromString(ds, "yyyy MM dd hh");
      else if (q == 16)
        d = QDateTime::fromString(ds, "yyyy MM dd hh mm");
      d.setTimeSpec(Qt::UTC);

      if (d.isValid()) {
        QString vs =
            serverResponse[i][j].mid(q, serverResponse[i][j].length() - q);
        QStringList v = vs.simplified().split(" ");
        qint64 dm = d.toMSecsSinceEpoch();
        if (dm >= start && dm <= end) {
          for (int k = 0; k < n; k++) {
            if (v[k] != "999" && v[k] != "99.0" && v[k] != "99.00" &&
                v[k] != "999.0") {
              double vl = v[k].toDouble();
              st[k]->setNext(dm, vl);
            }
          }
        }
      }
    }
  }

  //...Check for null values
  for (int i = st.length() - 1; i >= 0; i--) {
    if (st[i]->numSnaps() < 3) {
      delete st[i];
      st.removeAt(i);
    }
  }

  for (int i = 0; i < st.length(); i++) {
    data->addStation(st[i]);
  }

  if (data->nstations() == 0) {
    this->setErrorString("No valid station data found.");
    return 1;
  }

  return 0;
}

QStringList NdbcData::dataNames() { return c_dataNames; }

QStringList NdbcData::dataTypes() { return c_dataTypes; }

QMap<QString, QString> NdbcData::dataMap() {
  return NdbcData::buildDataNameMap();
}
