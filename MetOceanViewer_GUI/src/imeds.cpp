/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2015  Zach Cobell
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
#include "imeds.h"
#include <QDateTime>

Imeds::Imeds(QObject *parent) : QObject(parent) {
  this->success = false;
  this->nstations = 0;
}

int Imeds::read(QString filename) {
  // Variables
  QString year;
  QString month;
  QString day;
  QString hour;
  QString minute;
  QString second;
  QStringList TempList;
  QVector<QString> FileData;
  QDateTime tempDate;
  int nLine;
  int nStation;
  int i;
  int j;
  int k;
  int expectedLength;
  double value;
  QFile MyFile(filename);

  tempDate.setTimeSpec(Qt::UTC);

  // Default to an unsuccessful read
  this->success = false;

  // Check if we can open the file
  if (!MyFile.open(QIODevice::ReadOnly | QIODevice::Text)) return -1;

  // Read the header to output variable

  this->header1 = MyFile.readLine().simplified();
  this->header2 = MyFile.readLine().simplified();
  this->header3 = MyFile.readLine().simplified();

  // Read in the data portion of the file
  nLine = 0;
  while (!MyFile.atEnd()) {
    nLine = nLine + 1;
    FileData.push_back(MyFile.readLine().simplified());
  }
  MyFile.close();

  // Count the number of stations in the file
  nStation = 0;
  for (i = 0; i < nLine; i++) {
    TempList = FileData[i].split(" ");
    if (TempList.length() == 3) {
      nStation = nStation + 1;
    }
  }

  // Size the station vector and read the datasets
  this->station.resize(nStation);
  this->nstations = nStation;

  for (i = 0; i < this->nstations; i++)

    // Zero out the incremented variable
    for (i = 0; i < nStation; i++) this->station[i].NumSnaps = 0;

  // Organize the data into the variable
  j = 0;
  for (i = 0; i < nLine; i++) {
    TempList = FileData[i].split(" ");
    if (TempList.length() == 3) {
      this->station[j].longitude = TempList[2].toDouble();
      this->station[j].latitude = TempList[1].toDouble();
      this->station[j].StationName = TempList[0];
      this->station[j].StationIndex = j;
      j = j + 1;
    } else {
      this->station[j - 1].NumSnaps = this->station[j - 1].NumSnaps + 1;
    }
  }
  // Preallocate arrays for data and dates

  for (i = 0; i < nStation; i++) {
    this->station[i].data.resize(this->station[i].NumSnaps);
    this->station[i].date.resize(this->station[i].NumSnaps);
  }

  // Now, loop over the data section and save to vectors
  j = -1;
  k = -1;
  expectedLength = 0;
  for (i = 0; i < nLine; i++) {
    TempList = FileData[i].split(" ");
    if (TempList.length() == 3) {
      j = j + 1;
      k = -1;
    } else {
      if (k == -1) {
        if (TempList.length() == 6) {
          expectedLength = 6;
          k = k + 1;
          year = TempList.value(0);
          month = TempList.value(1);
          day = TempList.value(2);
          hour = TempList.value(3);
          minute = TempList.value(4);
          second = "0";
          value = TempList.value(5).toDouble();

          tempDate =
              QDateTime(QDate(year.toInt(), month.toInt(), day.toInt()),
                        QTime(hour.toInt(), minute.toInt(), second.toInt()));

          this->station[j].date[k] = tempDate.toMSecsSinceEpoch();

          this->station[j].data[k] = value;
          this->success = true;
        } else if (TempList.length() == 7) {
          expectedLength = 7;
          k = k + 1;
          year = TempList.value(0);
          month = TempList.value(1);
          day = TempList.value(2);
          hour = TempList.value(3);
          minute = TempList.value(4);
          second = TempList.value(5);
          value = TempList.value(6).toDouble();

          tempDate =
              QDateTime(QDate(year.toInt(), month.toInt(), day.toInt()),
                        QTime(hour.toInt(), minute.toInt(), second.toInt()));
          this->station[j].date[k] = tempDate.toMSecsSinceEpoch();

          this->station[j].data[k] = value;
          this->success = true;
        }
      } else {
        if (expectedLength != TempList.length()) {
          this->success = false;
          return -1;
        }

        if (expectedLength == 6) {
          k = k + 1;
          year = TempList.value(0);
          month = TempList.value(1);
          day = TempList.value(2);
          hour = TempList.value(3);
          minute = TempList.value(4);
          second = "0";
          value = TempList.value(5).toDouble();
          tempDate =
              QDateTime(QDate(year.toInt(), month.toInt(), day.toInt()),
                        QTime(hour.toInt(), minute.toInt(), second.toInt()));
          this->station[j].date[k] = tempDate.toMSecsSinceEpoch();

          this->station[j].data[k] = value;
          this->success = true;
        } else if (expectedLength == 7) {
          expectedLength = 7;
          k = k + 1;
          year = TempList.value(0);
          month = TempList.value(1);
          day = TempList.value(2);
          hour = TempList.value(3);
          minute = TempList.value(4);
          second = TempList.value(5);
          value = TempList.value(6).toDouble();

          tempDate =
              QDateTime(QDate(year.toInt(), month.toInt(), day.toInt()),
                        QTime(hour.toInt(), minute.toInt(), second.toInt()));
          this->station[j].date[k] = tempDate.toMSecsSinceEpoch();

          this->station[j].data[k] = value;
          this->success = true;
        }
      }
    }
  }
  return 0;
}

int Imeds::write(QString filename) {
  QString value;
  QFile outputFile(filename);

  if (!outputFile.open(QIODevice::WriteOnly)) return -1;

  outputFile.write(QString("% IMEDS generic format - Water Level\n").toUtf8());
  outputFile.write(
      QString("% year month day hour min sec watlev(" + this->units + ")\n")
          .toUtf8());
  outputFile.write(
      QString("MetOceanViewer    UTC    " + this->datum + "\n").toUtf8());

  for (int s = 0; s < this->nstations; s++) {
    outputFile.write(QString(this->station[s].StationID + "   " +
                             QString::number(this->station[s].latitude) +
                             "   " +
                             QString::number(this->station[s].longitude) + "\n")
                         .toUtf8());

    for (int i = 0; i < this->station[s].data.length(); i++) {
      if (QDateTime::fromMSecsSinceEpoch(this->station[s].date[i]).isValid()) {
        value.sprintf("%10.4e", this->station[s].data[i]);
        outputFile.write(
            QString(QDateTime::fromMSecsSinceEpoch(this->station[s].date[i])
                        .toString("yyyy    MM    dd    hh    mm    ss") +
                    "    " + value + "\n")
                .toUtf8());
      }
    }
  }
  outputFile.close();
  return 0;
}

int Imeds::writeCSV(QString filename) {
  int i, s;
  QString value;
  QFile output(filename);

  if (!output.open(QIODevice::WriteOnly)) return -1;

  for (s = 0; s < this->nstations; s++) {
    output.write(
        QString("Station: " + this->station[s].StationID + "\n").toUtf8());
    output.write(QString("Datum: " + this->datum + "\n").toUtf8());
    output.write(QString("Units: " + this->units + "\n").toUtf8());
    output.write(QString("\n").toUtf8());
    for (i = 0; i < this->station[s].data.length(); i++) {
      if (QDateTime::fromMSecsSinceEpoch(this->station[s].date[i]).isValid()) {
        value.sprintf("%10.4e", this->station[s].data[i]);
        output.write(
            QString(QDateTime::fromMSecsSinceEpoch(this->station[s].date[i])
                        .toString("MM/dd/yyyy,hh:mm,") +
                    value + "\n")
                .toUtf8());
      }
    }
    output.write(QString("\n\n\n").toUtf8());
  }
  output.close();
  return 0;
}
