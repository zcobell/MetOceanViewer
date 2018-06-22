#include "ndbcdata.h"
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QStringList>

NdbcData::NdbcData(Station &station, QDateTime startDate, QDateTime endDate,
                   QObject *parent)
    : WaterData(station, startDate, endDate, parent) {
  this->buildDataNameMap();
}

void NdbcData::buildDataNameMap() {
  this->m_dataNameMap["WD"] = "Wind Direction";
  this->m_dataNameMap["WDIR"] = "Wind Direction";
  this->m_dataNameMap["WSPD"] = "Wind Speed";
  this->m_dataNameMap["GST"] = "Wind Gusts";
  this->m_dataNameMap["WVHT"] = "Wave Height";
  this->m_dataNameMap["DPD"] = "Dominant Wave Period";
  this->m_dataNameMap["APD"] = "Average Wave Period";
  this->m_dataNameMap["MWD"] = "Mean Wave Direction";
  this->m_dataNameMap["BAR"] = "Barometric Pressure";
  this->m_dataNameMap["PRES"] = "Atmospheric Pressure";
  this->m_dataNameMap["ATMP"] = "Air Temperature";
  this->m_dataNameMap["WTMP"] = "Water Temperature";
  this->m_dataNameMap["DEWP"] = "Dewpoint";
  this->m_dataNameMap["VIS"] = "Visibility";
  this->m_dataNameMap["TIDE"] = "Water Level";
  return;
}

int NdbcData::retrieveData(Hmdf *data) {
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

  int n, p, q, r;

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
    s->setId(s->name());
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
