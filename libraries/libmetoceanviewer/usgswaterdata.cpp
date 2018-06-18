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
#include <QVector>

UsgsWaterdata::UsgsWaterdata(Station station, QDateTime startDate,
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
    requestUrl = "https://waterdata.usgs.gov/nwis/uv?format=rdb&site_no=" +
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

  this->readDownloadedData(reply, data);

  reply->deleteLater();

  delete manager;

  return 0;
}

int UsgsWaterdata::readDownloadedData(QNetworkReply *reply, Hmdf *output) {
  int ierr;

  QByteArray data = reply->readAll();

  if (this->m_databaseOption == 0 || this->m_databaseOption == 1) {
    ierr = this->readUsgsInstantData(data, output);
  } else {
    ierr = this->readUsgsDailyData(data, output);
  }

  return ierr;
}

int UsgsWaterdata::readUsgsInstantData(QByteArray &data, Hmdf *output) {
  bool doubleok;
  int ParamStart, ParamStop;
  int HeaderEnd;
  QStringList TempList;
  QString TempLine, TempDateString, TempTimeZoneString;

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
    TempLine = SplitByLine.value(i);
    if (TempLine == "#") {
      ParamStop = i - 1;
      break;
    }
  }

  QVector<QString> availableDatatypes;
  availableDatatypes.resize(ParamStop - ParamStart + 1);

  for (int i = ParamStart; i <= ParamStop; i++) {
    TempLine = SplitByLine.value(i);
    TempList = TempLine.split(" ", QString::SkipEmptyParts);
    availableDatatypes[i - ParamStart] = QString();
    for (int j = 3; j < TempList.length(); j++) {
      if (j == 3)
        availableDatatypes[i - ParamStart] = TempList.value(j);
      else
        availableDatatypes[i - ParamStart] =
            availableDatatypes[i - ParamStart] + " " + TempList.value(j);
    }
  }

  //...Find out where the header ends
  for (int i = 0; i < SplitByLine.length(); i++) {
    if (SplitByLine.value(i).left(1) != "#") {
      HeaderEnd = i + 2;
      break;
    }
  }

  //...Initialize the array
  QVector<HmdfStation *> stations;
  stations.resize(availableDatatypes.length());
  for (int i = 0; i < stations.length(); i++) {
    stations[i] = new HmdfStation(output);
    stations[i]->setName(availableDatatypes[i]);
  }

  //...Sanity check
  if (stations.length() == 0) return 1;

  //...Read the data into the array
  for (int i = HeaderEnd; i < SplitByLine.length(); i++) {
    TempLine = SplitByLine.value(i);
    TempList = TempLine.split(QRegExp("[\t]"));
    TempDateString = TempList.value(2);
    TempTimeZoneString = TempList.value(3);
    QDateTime currentDate =
        QDateTime::fromString(TempDateString, "yyyy-MM-dd hh:mm");
    currentDate.setTimeSpec(Qt::UTC);
    int offset = Timezone::offsetFromUtc(TempTimeZoneString);
    currentDate = currentDate.addSecs(-offset);
    for (int j = 0; j < availableDatatypes.length(); j++) {
      double TempData = TempList.value(2 * j + 4).toDouble(&doubleok);
      if (!TempList.value(2 * j + 4).isNull() && doubleok) {
        stations[j]->setNext(currentDate.toMSecsSinceEpoch(), TempData);
      }
    }
  }

  //...Add stations to object
  for (int i = 0; i < stations.length(); i++) output->addStation(stations[i]);

  return 0;
}

int UsgsWaterdata::readUsgsDailyData(QByteArray &data, Hmdf *output) {
  int ParamStart, ParamStop;
  int HeaderEnd;
  QStringList TempList;
  QString TempLine, TempDateString;
  QString InputData(data);
  QStringList SplitByLine =
      InputData.split(QRegExp("[\n]"), QString::SkipEmptyParts);
  bool doubleok;

  ParamStart = -1;
  ParamStop = -1;
  HeaderEnd = -1;

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
    TempLine = SplitByLine.value(i);
    if (TempLine == "#") {
      ParamStop = i - 1;
      break;
    }
  }

  QVector<QString> availableDatatypes;
  availableDatatypes.resize(ParamStop - ParamStart + 1);

  for (int i = ParamStart; i <= ParamStop; i++) {
    TempLine = SplitByLine.value(i);
    TempList = TempLine.split(" ", QString::SkipEmptyParts);
    availableDatatypes[i - ParamStart] = QString();
    for (int j = 3; j < TempList.length(); j++) {
      if (j == 3)
        availableDatatypes[i - ParamStart] = TempList.value(j);
      else
        availableDatatypes[i - ParamStart] =
            availableDatatypes[i - ParamStart] + " " + TempList.value(j);
    }
  }

  //...Remove the leading number
  for (int i = 0; i < availableDatatypes.length(); i++)
    availableDatatypes[i] = availableDatatypes[i].mid(6).simplified();

  //...Find out where the header ends
  for (int i = 0; i < SplitByLine.length(); i++) {
    if (SplitByLine.value(i).left(1) != "#") {
      HeaderEnd = i + 2;
      break;
    }
  }

  //...Initialize the array
  QVector<HmdfStation *> stations;
  stations.resize(availableDatatypes.length());
  for (int i = 0; i < availableDatatypes.length(); i++) {
    stations[i] = new HmdfStation(output);
    stations[i]->setName(availableDatatypes[i]);
  }

  //...Read the data into the array
  for (int i = HeaderEnd; i < SplitByLine.length(); i++) {
    TempLine = SplitByLine.value(i);
    TempList = TempLine.split(QRegExp("[\t]"));
    TempDateString = TempList.value(2);
    QDateTime currentDate = QDateTime::fromString(TempDateString, "yyyy-MM-dd");
    currentDate.setTimeSpec(Qt::UTC);
    for (int j = 0; j < availableDatatypes.length(); j++) {
      double tempData = TempList.value(2 * j + 3).toDouble(&doubleok);
      if (!TempList.value(2 * j + 3).isNull() && doubleok) {
        stations[j]->setNext(currentDate.toMSecsSinceEpoch(), tempData);
      }
    }
  }

  //...Add stations to object
  for (int i = 0; i < stations.length(); i++) output->addStation(stations[i]);

  return 0;
}
