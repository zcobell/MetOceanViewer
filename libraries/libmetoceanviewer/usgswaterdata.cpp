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
#include "usgswaterdata.h"
#include <QEventLoop>
#include <QMap>
#include <QVector>

UsgsWaterdata::UsgsWaterdata(Station &station, QDateTime startDate,
                             QDateTime endDate, int databaseOption,
                             QObject *parent)
    : WaterData(station, startDate, endDate, parent) {
  this->m_databaseOption = databaseOption;
}

int UsgsWaterdata::get(Hmdf *data) { return this->fetch(data); }

int UsgsWaterdata::fetch(Hmdf *data) {
  if (this->station().id() == QString()) {
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
  QString endDateString2 =
      "&end_date=" + this->endDate().addDays(1).toString("yyyy-MM-dd");
  QString startDateString2 =
      "&begin_date=" + this->startDate().toString("yyyy-MM-dd");

  //...Construct the correct request URL
  QString requestUrl;
  if (this->m_databaseOption == 0)
    requestUrl =
        "https://nwis.waterdata.usgs.gov/usa/nwis/uv?format=rdb&site_no=" +
        this->station().id() + startDateString2 + endDateString2;
  else if (this->m_databaseOption == 1)
    requestUrl = "https://waterservices.usgs.gov/nwis/iv/?sites=" +
                 this->station().id() + startDateString1 + endDateString1 +
                 "&format=rdb";
  else
    requestUrl = "https://waterservices.usgs.gov/nwis/dv/?sites=" +
                 this->station().id() + startDateString1 + endDateString1 +
                 "&format=rdb";

  return QUrl(requestUrl);
}

int UsgsWaterdata::download(QUrl url, Hmdf *data) {
  QNetworkAccessManager *manager = new QNetworkAccessManager(this);
  QEventLoop loop;

  //...Make the request to the server
  QNetworkReply *reply = manager->get(QNetworkRequest(url));
  connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
  connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), &loop,
          SLOT(quit()));
  loop.exec();

  if (reply->error() != QNetworkReply::NoError) {
    this->setErrorString("There was an error contacting the USGS data server");
    return 1;
  }

  int ierr = this->readDownloadedData(reply, data);

  reply->deleteLater();

  delete manager;

  return ierr;
}

int UsgsWaterdata::readDownloadedData(QNetworkReply *reply, Hmdf *output) {
  QByteArray data = reply->readAll();
  return this->readUsgsData(data, output);
}

int UsgsWaterdata::readUsgsData(QByteArray &data, Hmdf *output) {
  bool doubleok;
  int ParamStart, ParamStop;
  int HeaderEnd;
  QStringList tempList;
  QString tempLine, TempDateString, TempTimeZoneString;

  QString InputData(data);
  QStringList SplitByLine =
      InputData.split(QRegExp("[\n]"), QString::SkipEmptyParts);

  ParamStart = -1;
  ParamStop = -1;
  HeaderEnd = -1;

  if (InputData.isEmpty() || InputData.isNull()) {
    this->setErrorString(
        "This data is not available except from the USGS archive server.");
    return 1;
  }

  //...Save the potential error string
  this->setErrorString(InputData.remove(QRegExp("[\n\t\r]")));

  //...Start by finding the header and reading the parameters from it
  for (int i = 0; i < SplitByLine.length(); i++) {
    if (SplitByLine.value(i).left(15) == "# Data provided") {
      ParamStart = i + 2;
      break;
    }
  }

  for (int i = ParamStart; i < SplitByLine.length(); i++) {
    tempLine = SplitByLine.value(i);
    if (tempLine == "#") {
      ParamStop = i - 1;
      break;
    }
  }

  struct UsgsParameter {
    QString description;
    QString ts;
    QString statistic;
    QString parameter;
    QString code;
  };
  QVector<UsgsParameter> params;

  for (int i = ParamStart; i <= ParamStop; i++) {
    tempLine = SplitByLine.value(i);
    tempList = tempLine.split("  ", QString::SkipEmptyParts);

    UsgsParameter p;
    p.ts = tempList.value(1).simplified();
    p.parameter = tempList.value(2).simplified();
    if (tempList.length() == 6) {
      p.description = tempList.value(5).simplified();
      p.statistic = tempList.value(3).simplified();
      p.code = p.ts + "_" + p.parameter + "_" + p.statistic;
    } else if (tempList.length() == 5) {
      p.description = tempList.value(4);
      p.statistic = tempList.value(3);
      p.code = p.ts + "_" + p.parameter + "_" + p.statistic;
    } else {
      p.description = tempList.value(3).simplified();
      p.code = p.ts + "_" + p.parameter;
    }
    params.push_back(p);
  }

  //...Find out where the header ends
  for (int i = 0; i < SplitByLine.length(); i++) {
    if (SplitByLine.value(i).left(1) != "#") {
      HeaderEnd = i + 2;
      break;
    }
  }

  //...Generate the mapping
  QMap<int, int> parameterMapping, revParmeterMapping;
  QString mapString = SplitByLine[HeaderEnd - 2];
  QStringList mapList = mapString.split("\t");
  for (int i = 4; i < mapList.length(); i++) {
    for (int j = 0; j < params.length(); j++) {
      if (mapList[i] == params[j].code) {
        parameterMapping[i] = j;
        revParmeterMapping[j] = i;
      }
    }
  }

  //...Initialize the array
  QVector<HmdfStation *> stations;
  stations.resize(params.length());
  for (int i = 0; i < stations.length(); i++) {
    stations[i] = new HmdfStation(output);
    stations[i]->setName(params[i].description);
  }

  //...Sanity check
  if (stations.length() == 0) return 1;

  //...Read the data into the array
  for (int i = HeaderEnd; i < SplitByLine.length(); i++) {
    tempLine = SplitByLine.value(i);
    tempList = tempLine.split(QRegExp("[\t]"));
    TempDateString = tempList.value(2);
    TempTimeZoneString = tempList.value(3);

    //...Account for both daily values (without time) and instant (with time)
    QDateTime currentDate =
        QDateTime::fromString(TempDateString, "yyyy-MM-dd hh:mm");
    if (!currentDate.isValid()) {
      currentDate = QDateTime::fromString(TempDateString, "yyyy-MM-dd");
    }

    //...Convert to UTC from the source timezone
    currentDate.setTimeSpec(Qt::UTC);
    int offset = Timezone::offsetFromUtc(TempTimeZoneString);
    currentDate = currentDate.addSecs(-offset);

    for (int j = 0; j < params.length(); j++) {
      double data = tempList.value(revParmeterMapping[j]).toDouble(&doubleok);
      if (doubleok) {
        stations[j]->setNext(currentDate.toMSecsSinceEpoch(), data);
      }
    }
  }

  for (int i = stations.size() - 1; i >= 0; i--) {
    if (stations[i]->numSnaps() < 3) {
      delete stations[i];
      stations.removeAt(i);
    }
  }

  //...Sanity check
  if (stations.length() == 0) {
    this->setErrorString(
        "No data available at this station for this time period\n" +
        this->errorString());
    return 1;
  }

  //...Add stations to object
  for (int i = 0; i < stations.length(); i++) output->addStation(stations[i]);

  return 0;
}
