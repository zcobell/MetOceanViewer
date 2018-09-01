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
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QTimer>
#include <iostream>
#include "metoceandata.h"
#include "version.h"

QDateTime checkDateString(QString str);
MetOceanData::serviceTypes checkServiceString(QString str);
int checkIntegerString(QString str);
void processCommandLineOptions(QCoreApplication &a,
                               MetOceanData::serviceTypes &service,
                               QStringList &station, int &product, int &datum,
                               QDateTime &startDate, QDateTime &endDate,
                               QString &outputFile);

int main(int argc, char *argv[]) {
  QCoreApplication a(argc, argv);
  QCoreApplication::setApplicationName("MetOceanData");
  QCoreApplication::setApplicationVersion(
      QString::fromStdString(metoceanVersion()));

  Q_INIT_RESOURCE(resource_files);

  int product, datum;
  MetOceanData::serviceTypes service;
  QDateTime startDate, endDate;
  QString outputFile;
  QStringList station;

  processCommandLineOptions(a, service, station, product, datum, startDate,
                            endDate, outputFile);

  MetOceanData *d = new MetOceanData(service, station, product, datum,
                                     startDate, endDate, outputFile, &a);
  d->setLoggingActive();
  QObject::connect(d, SIGNAL(finished()), &a, SLOT(quit()));
  QTimer::singleShot(0, d, SLOT(run()));
  return a.exec();
}

void processCommandLineOptions(QCoreApplication &app,
                               MetOceanData::serviceTypes &service,
                               QStringList &station, int &product, int &datum,
                               QDateTime &startDate, QDateTime &endDate,
                               QString &outputFile) {
  QCommandLineParser parser;
  parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
  parser.setApplicationDescription(
      "Command line interface to MetOceanViewer's library to generate live "
      "time series data.");

  QCommandLineOption cmd_serviceType =
      QCommandLineOption(QStringList() << "s"
                                       << "service",
                         "Service to use to generate data. Can be one of NOAA, "
                         "USGS, NDBC, or XTIDE",
                         "source");
  QCommandLineOption cmd_stationId = QCommandLineOption(
      QStringList() << "station",
      "Station ID to use to generate data. Ex. NOAA Station 2695540 would "
      "be be specified with '2695540'. XTide stations should be specified "
      "using their ID from MetOceanViewer",
      "id");

  QCommandLineOption cmd_startDate =
      QCommandLineOption(QStringList() << "b"
                                       << "startdate",
                         "Start date for generating the data. Should be "
                         "formatted as: yyyyMMddhhmmss",
                         "date");

  QCommandLineOption cmd_endDate = QCommandLineOption(
      QStringList() << "e"
                    << "enddate",
      "End date for generating the data. Should be formatted as yyyyMMddhhmmss",
      "date");

  QCommandLineOption cmd_product =
      QCommandLineOption(QStringList() << "p"
                                       << "product",
                         "Product index to download numbered from 1 to the "
                         "number of available products. If left unspecified, "
                         "you will be presented with a list of options",
                         "index");

  QCommandLineOption cmd_datum =
      QCommandLineOption(QStringList() << "d"
                                       << "datum",
                         "Specified datum to use. Only available for NOAA "
                         "products.",
                         //      "Options "
                         //      "are: \n    (1)  MHHW\n    (2)  MHW\n    (3)
                         //      MTL\n    (4)  MSL\n (5) " " MLW\n    (6) MLLW\n
                         //      (7)  NAVD\n    (8)  LWI\n    (9) HWI\n    "
                         //      "(10) IGLD\n    (11) Station Datum",
                         "option");

  QCommandLineOption cmd_outputFile =
      QCommandLineOption(QStringList() << "o"
                                       << "output",
                         "Name of the output file. Format will be guessed from "
                         "extension (.imeds or .nc)",
                         "filename");

  QCommandLineOption cmd_boundingBox =
      QCommandLineOption(QStringList() << "boundingbox",
                         "Bounding box coordinates. Selects all stations that "
                         "fall within the bounding box.",
                         "x1,y1,x2,y2");

  QCommandLineOption cmd_nearest = QCommandLineOption(
      QStringList() << "nearest",
      "Selects the station that falls closest to the specfied location", "x,y");

  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOptions(QList<QCommandLineOption>()
                    << cmd_serviceType << cmd_stationId << cmd_boundingBox
                    << cmd_nearest << cmd_startDate << cmd_endDate
                    << cmd_product << cmd_outputFile << cmd_datum);

  parser.process(app);

  if (parser.isSet(cmd_stationId) && parser.isSet(cmd_nearest)) {
    std::cerr << "Error: Cannot select station id and nearest station options."
              << std::endl;
    parser.showHelp(1);
  }

  if (parser.isSet(cmd_stationId) && parser.isSet(cmd_boundingBox)) {
    std::cerr
        << "Error: Cannot select station id and bound box station options."
        << std::endl;
    ;
    parser.showHelp(1);
  }

  if (parser.isSet(cmd_boundingBox) && parser.isSet(cmd_nearest)) {
    std::cerr
        << "Error: Cannot select nearest station and bounding box options."
        << std::endl;
    parser.showHelp(1);
  }

  if (app.arguments().length() == 1) {
    std::cerr << "Error: No command line arguments detected." << std::endl;
    std::cerr.flush();
    parser.showHelp(1);
  }

  if (!parser.isSet(cmd_serviceType)) {
    std::cerr << "Error: No service selected." << std::endl;
    parser.showHelp(1);
  }

  if (!parser.isSet(cmd_stationId) && !parser.isSet(cmd_boundingBox) &&
      !parser.isSet(cmd_nearest)) {
    std::cerr << "Error: No stations selected." << std::endl;
    parser.showHelp(1);
  }

  if (!parser.isSet(cmd_startDate)) {
    std::cerr << "Error: No start date selected." << std::endl;
    parser.showHelp(1);
  }

  if (!parser.isSet(cmd_endDate)) {
    std::cerr << "Error: No end date selected." << std::endl;
    parser.showHelp(1);
  }

  if (!parser.isSet(cmd_outputFile)) {
    std::cerr << "Error: No output file specified." << std::endl;
    parser.showHelp(1);
  }

  QString serviceString = parser.value(cmd_serviceType);
  QString startDateString = parser.value(cmd_startDate);
  QString endDateString = parser.value(cmd_endDate);
  outputFile = parser.value(cmd_outputFile);

  service = checkServiceString(serviceString);
  if (service == MetOceanData::UNKNOWNSERVICE) {
    std::cerr << "Error: Unknown service specified." << std::endl;
    parser.showHelp(1);
  }

  if (parser.isSet(cmd_stationId)) {
    station = parser.values(cmd_stationId);
  } else if (parser.isSet(cmd_nearest)) {
    QStringList v = parser.value(cmd_nearest).split(",");
    if (v.length() != 2) {
      std::cerr << "Error: Poorly formed coordinate input." << std::endl;
      exit(1);
    }
    double x = parser.value(cmd_nearest).split(",").at(0).toDouble();
    double y = parser.value(cmd_nearest).split(",").at(1).toDouble();
    station.push_back(MetOceanData::selectNearestStation(service, x, y));
    if (!station.at(0).isNull()) {
      std::cout << "Selected " << station.at(0).toStdString()
                << " using nearest location." << std::endl;
    } else {
      std::cerr << "No station could be selected." << std::endl;
      exit(1);
    }
  } else if (parser.isSet(cmd_boundingBox)) {
    QStringList v = parser.value(cmd_boundingBox).split(",");
    if (v.length() != 4) {
      std::cerr << "Error: Poorly formed bounding box input." << std::endl;
      exit(1);
    }
    double x1 = v.at(0).toDouble();
    double y1 = v.at(1).toDouble();
    double x2 = v.at(2).toDouble();
    double y2 = v.at(3).toDouble();
    station = MetOceanData::selectStations(service, x1, y1, x2, y2);
    if (station.length() == 0) {
      std::cerr << "No station could be selected." << std::endl;
      exit(1);
    } else {
      std::cout << "Selected " << station.length()
                << " stations using bounding box." << std::endl;
    }
  }

  if (station.length() == 0) {
    std::cerr << "Error: No stations selected." << std::endl;
    parser.showHelp(1);
  }

  if (parser.isSet(cmd_product)) {
    QString productString = parser.value(cmd_product);
    product = checkIntegerString(productString);
    if (product == -1) {
      std::cout << "Error: Invalid product selection." << std::endl;
      parser.showHelp(1);
    }
  } else {
    product = -1;
  }

  if (parser.isSet(cmd_datum)) {
    QString datumString = parser.value(cmd_datum);
    datum = checkIntegerString(datumString);
    if (datum == -1) {
      std::cout << "Error: Invalid datum selection." << std::endl;
      parser.showHelp(1);
    }
  } else {
    datum = -1;
  }

  startDate = checkDateString(startDateString);
  if (startDate.isNull()) {
    std::cerr << "Error: Invalid start date." << std::endl;
    parser.showHelp(1);
  }

  endDate = checkDateString(endDateString);
  if (endDate.isNull()) {
    std::cerr << "Error: Invalid end date." << std::endl;
    parser.showHelp(1);
  }

  outputFile = parser.value(cmd_outputFile);

  if (startDate >= endDate) {
    std::cerr << "Error: Date range is not logical." << std::endl;
    parser.showHelp(1);
  }

  return;
}

MetOceanData::serviceTypes checkServiceString(QString str) {
  str = str.toUpper();
  if (str == "NOAA") return MetOceanData::NOAA;
  if (str == "USGS") return MetOceanData::USGS;
  if (str == "XTIDE") return MetOceanData::XTIDE;
  if (str == "NDBC") return MetOceanData::NDBC;
  return MetOceanData::UNKNOWNSERVICE;
}

QDateTime checkDateString(QString str) {
  QDateTime d = QDateTime();
  d = QDateTime::fromString(str.simplified(), "yyyyMMddhhmmss");
  if (d.isValid()) return d;
  return QDateTime();
}

int checkIntegerString(QString str) {
  bool ok;
  int s = str.toInt(&ok);
  if (!ok && s > 0) {
    return -1;
  } else {
    return s;
  }
}
