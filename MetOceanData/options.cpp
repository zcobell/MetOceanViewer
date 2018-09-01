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
#include "options.h"
#include <QFile>
#include <iostream>
#include "optionslist.h"

Options::Options(QObject *parent) : QObject(parent) { this->addOptions(); }

QCommandLineParser *Options::parser() { return &m_parser; }

void Options::processOptions() { this->parser()->process(*qApp); }

void Options::addOptions() {
  this->parser()->setSingleDashWordOptionMode(
      QCommandLineParser::ParseAsLongOptions);
  this->parser()->addHelpOption();
  this->parser()->addVersionOption();
  this->parser()->addOptions(QList<QCommandLineOption>()
                             << m_serviceType << m_stationId << m_boundingBox
                             << m_nearest << m_startDate << m_endDate
                             << m_product << m_outputFile << m_datum << m_list
                             << m_show);
}

Options::CommandLineOptions Options::getCommandLineOptions() {
  Options::CommandLineOptions opt;

  std::vector<bool> inputOptions;
  inputOptions.push_back(this->parser()->isSet(m_stationId));
  inputOptions.push_back(this->parser()->isSet(m_boundingBox));
  inputOptions.push_back(this->parser()->isSet(m_nearest));
  inputOptions.push_back(this->parser()->isSet(m_list));

  int inputCount = std::count(inputOptions.begin(), inputOptions.end(), true);
  if (inputCount < 1) {
    std::cerr << "No station options specified." << std::endl;
    this->parser()->showHelp(1);
  } else if (inputCount > 1) {
    std::cerr << "More than one station selection option specified."
              << std::endl;
    this->parser()->showHelp(1);
  }

  if (!this->parser()->isSet(m_serviceType)) {
    std::cerr << "Error: No service selected." << std::endl;
    this->parser()->showHelp(1);
  }
  QString serviceString = this->parser()->value(m_serviceType);
  opt.service = checkServiceString(serviceString);
  if (opt.service == MetOceanData::UNKNOWNSERVICE) {
    std::cerr << "Error: Unknown service specified." << std::endl;
    this->parser()->showHelp(1);
  }

  if (this->parser()->isSet(m_stationId)) {
    opt.station = this->parser()->values(m_stationId);
  } else if (this->parser()->isSet(m_nearest)) {
    QStringList v = this->parser()->value(m_nearest).split(",");
    if (v.length() != 2) {
      std::cerr << "Error: Poorly formed coordinate input." << std::endl;
      exit(1);
    }
    double x = this->parser()->value(m_nearest).split(",").at(0).toDouble();
    double y = this->parser()->value(m_nearest).split(",").at(1).toDouble();
    opt.station.push_back(
        MetOceanData::selectNearestStation(opt.service, x, y));
    if (!opt.station.at(0).isNull()) {
      if (!this->parser()->isSet(m_show)) {
        std::cout << "Selected " << opt.station.at(0).toStdString()
                  << " using nearest location." << std::endl;
      }
    } else {
      std::cerr << "No station could be selected." << std::endl;
      exit(1);
    }
  } else if (this->parser()->isSet(m_boundingBox)) {
    QStringList v = this->parser()->value(m_boundingBox).split(",");
    if (v.length() != 4) {
      std::cerr << "Error: Poorly formed bounding box input." << std::endl;
      exit(1);
    }
    double x1 = v.at(0).toDouble();
    double y1 = v.at(1).toDouble();
    double x2 = v.at(2).toDouble();
    double y2 = v.at(3).toDouble();
    opt.station = MetOceanData::selectStations(opt.service, x1, y1, x2, y2);
    if (opt.station.length() == 0) {
      std::cerr << "No station could be selected." << std::endl;
      exit(1);
    } else {
      if (!this->parser()->isSet(m_show)) {
        std::cout << "Selected " << opt.station.length()
                  << " stations using bounding box." << std::endl;
      }
    }
  } else if (this->parser()->isSet(m_list)) {
    this->readStationList(opt.station, opt.service);
    if (!this->parser()->isSet(m_show)) {
      std::cout << "Selected " << opt.station.length()
                << " stations using list file." << std::endl;
    }
  }

  if (opt.station.length() == 0) {
    std::cerr << "Error: No stations selected." << std::endl;
    this->parser()->showHelp(1);
  }

  if (this->parser()->isSet(m_show)) {
    this->printStationList(opt.station, opt.service);
  }

  if (!this->parser()->isSet(m_startDate)) {
    std::cerr << "Error: No start date selected." << std::endl;
    this->parser()->showHelp(1);
  }

  if (!this->parser()->isSet(m_endDate)) {
    std::cerr << "Error: No end date selected." << std::endl;
    this->parser()->showHelp(1);
  }

  if (!this->parser()->isSet(m_outputFile)) {
    std::cerr << "Error: No output file specified." << std::endl;
    this->parser()->showHelp(1);
  }

  QString startDateString = this->parser()->value(m_startDate);
  QString endDateString = this->parser()->value(m_endDate);
  opt.outputFile = this->parser()->value(m_outputFile);

  if (this->parser()->isSet(m_product)) {
    QString productString = this->parser()->value(m_product);
    opt.product = checkIntegerString(productString);
    if (opt.product == -1) {
      std::cout << "Error: Invalid product selection." << std::endl;
      this->parser()->showHelp(1);
    }
  } else {
    opt.product = -1;
  }

  if (this->parser()->isSet(m_datum)) {
    QString datumString = this->parser()->value(m_datum);
    opt.datum = checkIntegerString(datumString);
    if (opt.datum == -1) {
      std::cout << "Error: Invalid datum selection." << std::endl;
      this->parser()->showHelp(1);
    }
  } else {
    opt.datum = -1;
  }

  opt.startDate = checkDateString(startDateString);
  if (opt.startDate.isNull()) {
    std::cerr << "Error: Invalid start date." << std::endl;
    this->parser()->showHelp(1);
  }

  opt.endDate = checkDateString(endDateString);
  if (opt.endDate.isNull()) {
    std::cerr << "Error: Invalid end date." << std::endl;
    this->parser()->showHelp(1);
  }

  if (opt.startDate >= opt.endDate) {
    std::cerr << "Error: Date range is not logical." << std::endl;
    exit(1);
  }

  return opt;
}

MetOceanData::serviceTypes Options::checkServiceString(QString str) {
  str = str.toUpper();
  if (str == "NOAA") return MetOceanData::NOAA;
  if (str == "USGS") return MetOceanData::USGS;
  if (str == "XTIDE") return MetOceanData::XTIDE;
  if (str == "NDBC") return MetOceanData::NDBC;
  return MetOceanData::UNKNOWNSERVICE;
}

QDateTime Options::checkDateString(QString str) {
  QDateTime d = QDateTime();
  d = QDateTime::fromString(str.simplified(), "yyyyMMddhhmmss");
  if (d.isValid()) return d;
  return QDateTime();
}

int Options::checkIntegerString(QString str) {
  bool ok;
  int s = str.toInt(&ok);
  if (!ok && s > 0) {
    return -1;
  } else {
    return s;
  }
}

void Options::printStationList(QStringList station,
                               MetOceanData::serviceTypes markerType) {
  QVector<Station> s;
  MetOceanData::findStation(station,
                            MetOceanData::serviceToMarkerType(markerType), s);
  for (size_t i = 0; i < station.size(); ++i) {
    std::cout << s[i].id().toStdString() << ",'"
              << s[i].name().replace(" ", "_").toStdString() << "',"
              << s[i].coordinate().longitude() << ","
              << s[i].coordinate().latitude() << std::endl;
  }
  exit(0);
  return;
}

void Options::readStationList(QStringList &station,
                              MetOceanData::serviceTypes markerType) {
  QString filename = this->parser()->value(m_list);
  QFile f(filename);
  if (!f.exists()) {
    std::cout << "Error: Input file does not exist." << std::endl;
    exit(1);
  }

  if (!f.open(QIODevice::ReadOnly)) {
    std::cout << "Error: Could not open input file." << std::endl;
    exit(1);
  }

  while (!f.atEnd()) {
    QString l = f.readLine();
    QString s = l.split(",").at(0);
    QStringList sl;
    sl.push_back(s);
    QVector<Station> st;
    bool found = MetOceanData::findStation(
        sl, MetOceanData::serviceToMarkerType(markerType), st);
    if (!found) {
      std::cerr << "Error: Station " << s.toStdString() << " not found."
                << std::endl;
    } else {
      station.push_back(s);
    }
  }

  if (station.length() == 0) {
    std::cerr << "Error: No valid stations found in file." << std::endl;
    exit(1);
  }

  return;
}
