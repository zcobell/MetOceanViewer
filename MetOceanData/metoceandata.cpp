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
#include "metoceandata.h"
#include <QHash>
#include <algorithm>
#include <iostream>
#include "constants.h"
#include "generic.h"
#include "hmdf.h"
#include "ndbcdata.h"
#include "noaacoops.h"
#include "usgswaterdata.h"
#include "xtidedata.h"

static const QHash<int, QString> noaaProducts = {
    {1, "water_level"},     {2, "hourly_height"},     {3, "predictions"},
    {4, "air_temperature"}, {5, "water_temperature"}, {6, "wind:speed"},
    {7, "humidity"},        {8, "air_pressure"},      {9, "wind:direction"},
    {10, "wind:gusts"}};
static const QHash<int, QString> noaaProductLongName = {
    {1, "6 minute water level"},
    {2, "Hourly water level"},
    {3, "Tide predictoins"},
    {4, "Air temperature"},
    {5, "Water temperature"},
    {6, "Wind speed"},
    {7, "Humidity"},
    {8, "Air pressure"},
    {9, "Wind direction"},
    {10, "Wind gusts"}};
static const QHash<int, QString> noaaUnits = {
    {1, "m"},   {2, "m"}, {3, "m"},  {4, "C"},   {5, "C"},
    {6, "m/s"}, {7, "%"}, {8, "mb"}, {9, "deg"}, {10, "m/s"}};
static const QHash<int, QString> noaaDatum = {
    {1, "MHHW"}, {2, "MHW"}, {3, "MTL"}, {4, "MSL"},   {5, "MLW"},  {6, "MLLW"},
    {7, "NAVD"}, {8, "LWI"}, {9, "HWI"}, {10, "IGLD"}, {11, "Stnd"}};
static const QHash<int, QString> vDatum = {
    {1, "MLLW"}, {2, "MLW"},    {3, "MSL"},   {4, "MHW"},
    {5, "MHHW"}, {6, "NGVD29"}, {7, "NAVD88"}};

MetOceanData::MetOceanData(QObject *parent)
    : m_service(0),
      m_product(0),
      m_station(nullptr),
      m_datum(0),
      m_startDate(QDateTime()),
      m_endDate(QDateTime()),
      m_outputFile(QString()),
      m_usevdatum(false),
      m_previousProduct(QString()),
      m_productId(QString()),
      QObject(parent) {}

MetOceanData::MetOceanData(serviceTypes service, QStringList station,
                           int product, QString productId, bool useVdatum,
                           int datum, QDateTime startDate, QDateTime endDate,
                           QString outputFile, QObject *parent)
    : m_service(service),
      m_product(product),
      m_station(station),
      m_datum(datum),
      m_startDate(startDate),
      m_endDate(endDate),
      m_outputFile(outputFile),
      m_usevdatum(useVdatum),
      m_productId(productId),
      m_previousProduct((QString())),
      QObject(parent) {}

int MetOceanData::service() const { return this->m_service; }

void MetOceanData::setService(int service) { this->m_service = service; }

QStringList MetOceanData::station() const { return this->m_station; }

void MetOceanData::setStation(QStringList station) {
  this->m_station = station;
}

QDateTime MetOceanData::startDate() const { return this->m_startDate; }

void MetOceanData::setStartDate(const QDateTime &startDate) {
  this->m_startDate = startDate;
}

QDateTime MetOceanData::endDate() const { return this->m_endDate; }

void MetOceanData::setEndDate(const QDateTime &endDate) {
  this->m_endDate = endDate;
}

QString MetOceanData::outputFile() const { return this->m_outputFile; }

void MetOceanData::setOutputFile(const QString &outputFile) {
  this->m_outputFile = outputFile;
}

void MetOceanData::setLoggingActive() {
  connect(this, SIGNAL(error(QString)), this, SLOT(showError(QString)));
  connect(this, SIGNAL(status(QString, int)), this,
          SLOT(showStatus(QString, int)));
  connect(this, SIGNAL(warning(QString)), this, SLOT(showWarning(QString)));
  return;
}

void MetOceanData::setLoggingInactive() {
  disconnect(this, SIGNAL(error(QString)), this, SLOT(showError(QString)));
  disconnect(this, SIGNAL(status(QString, int)), this,
             SLOT(showStatus(QString, int)));
  disconnect(this, SIGNAL(warning(QString)), this, SLOT(showWarning(QString)));
  return;
}

void MetOceanData::showError(QString errorString) {
  std::cerr << "[ERROR] " << errorString.toStdString().c_str() << std::endl;
  std::cerr.flush();
  return;
}

void MetOceanData::showWarning(QString errorString) {
  std::cout << "[WARNING] " << errorString.toStdString() << std::endl;
  std::cout.flush();
  return;
}

void MetOceanData::showStatus(QString message, int pct) {
  QString complete;
  complete.sprintf("%3i", pct);
  std::cout << "[" << complete.toStdString().c_str() << "%] "
            << message.toStdString().c_str() << std::endl;
  std::cout.flush();
  return;
}

void MetOceanData::run() {
  //  if (this->service() == USGS && this->m_station.length() > 1) {
  //    emit error(
  //        "Beacuase each station has different characteristics, only one "
  //        "USGS station may be selected at a time.");
  //    emit finished();
  //    return;
  //  }

  if (this->service() == NOAA)
    this->getNoaaData();
  else if (this->service() == USGS)
    this->getUsgsData();
  else if (this->service() == NDBC)
    this->getNdbcData();
  else if (this->service() == XTIDE)
    this->getXtideData();

  emit finished();
  return;
}

QStringList MetOceanData::selectStations(serviceTypes service, double x1,
                                         double y1, double x2, double y2) {
  QStringList stationList;
  StationLocations::MarkerType m = MetOceanData::serviceToMarkerType(service);
  QVector<Station> markerLocations = StationLocations::readMarkers(m);
  double xmin = std::min(x1, x2);
  double xmax = std::max(x1, x2);
  double ymin = std::min(y1, y2);
  double ymax = std::max(y1, y2);
  for (size_t i = 0; i < markerLocations.size(); ++i) {
    double x = markerLocations[i].coordinate().longitude();
    double y = markerLocations[i].coordinate().latitude();
    if (x >= xmin && x <= xmax && y >= ymin && y <= ymax) {
      stationList.push_back(markerLocations[i].id());
    }
  }
  return stationList;
}

QString MetOceanData::selectNearestStation(serviceTypes service, double x,
                                           double y) {
  StationLocations::MarkerType m = MetOceanData::serviceToMarkerType(service);
  QVector<Station> markerLocations = StationLocations::readMarkers(m);

  double d = std::numeric_limits<double>::max();
  size_t j = std::numeric_limits<size_t>::max();

  for (size_t i = 0; i < markerLocations.size(); i++) {
    double xs = markerLocations[i].coordinate().longitude();
    double ys = markerLocations[i].coordinate().latitude();
    double d1 = Constants::distance(x, y, xs, ys, true);
    if (d1 < d) {
      d = d1;
      j = i;
    }
  }

  if (j != std::numeric_limits<size_t>::max()) {
    return markerLocations[j].id();
  } else {
    return QString();
  }
}

StationLocations::MarkerType MetOceanData::serviceToMarkerType(
    serviceTypes type) {
  switch (type) {
    case NOAA:
      return StationLocations::NOAA;
    case USGS:
      return StationLocations::USGS;
    case XTIDE:
      return StationLocations::XTIDE;
    case NDBC:
      return StationLocations::NDBC;
    default:
      return StationLocations::NOAA;
  }
}

bool MetOceanData::findStation(QStringList name,
                               StationLocations::MarkerType type,
                               QVector<Station> &s) {
  QVector<bool> found;
  found.resize(name.size());
  std::fill(found.begin(), found.end(), false);
  QVector<Station> markerLocations = StationLocations::readMarkers(type);
  s.resize(name.length());

  for (size_t j = 0; j < name.length(); j++) {
    for (size_t i = 0; i < markerLocations.length(); ++i) {
      if (name.at(j).simplified() == markerLocations[i].id().simplified()) {
        found[j] = true;
        s[j] = markerLocations[i];
        break;
      }
    }
    if (!found[j]) return false;
  }
  return true;
}

void MetOceanData::getNdbcData() {
  QVector<Station> s;
  bool found = this->findStation(this->station(), StationLocations::NDBC, s);
  if (!found) {
    emit error("Station not found.");
    emit finished();
    return;
  }

  Hmdf *dataOut = new Hmdf(this);

  for (size_t i = 0; i < s.size(); ++i) {
    Hmdf *data = new Hmdf(this);
    NdbcData *ndbc =
        new NdbcData(s[i], this->startDate(), this->endDate(), this);
    int ierr = ndbc->get(data);
    if (ierr != 0) {
      emit warning(QString(s[i].id() + ": " + ndbc->errorString()));
      delete data;
      delete ndbc;
      continue;
    }

    bool useStation = false;
    ierr = this->printAvailableProducts(data);
    if (ierr != 0) {
      useStation = false;
    } else {
      useStation = true;
    }

    if (useStation) {
      data->station(this->m_product - 1)->setName(s[i].name());
      data->station(this->m_product - 1)->setId(s[i].id());
      dataOut->addStation(data->station(this->m_product - 1));
      data->station(this->m_product - 1)->setParent(dataOut);
    }

    delete ndbc;
    delete data;
  }

  if (dataOut->nstations() == 0) return;

  dataOut->setUnits("ndbc_units");
  dataOut->setDatum("ndbc_datum");

  int ierr = dataOut->write(this->m_outputFile);
  if (ierr != 0) {
    emit error("Error writing to file.");
    return;
  }

  return;
}

void MetOceanData::getXtideData() {
  QVector<Station> s;
  bool found = this->findStation(this->station(), StationLocations::XTIDE, s);
  if (!found) {
    emit error("Station not found.");
    emit finished();
    return;
  }

  Generic::createConfigDirectory();

  Hmdf *dataOut = new Hmdf(this);

  for (size_t i = 0; i < s.size(); ++i) {
    Hmdf *data = new Hmdf(this);
    XtideData *x = new XtideData(s[i], this->startDate(), this->endDate(),
                                 Generic::configDirectory(), this);
    int ierr = x->get(data);
    if (ierr != 0) {
      emit error(x->errorString());
      return;
    }

    QString datum = "MLLW";
    if (this->m_usevdatum) {
      QString d = this->indexToDatum();
      Datum::VDatum datumid = Datum::datumID(d);
      if (!data->applyDatumCorrection(s[i], datumid)) {
        std::cout << "Warning: Could not apply datum transformation for "
                  << s[i].name().toStdString() << "Using MLLW." << std::endl;
      }
    }

    data->setDatum(datum);
    data->setUnits("m");

    dataOut->addStation(data->station(0));
    data->station(0)->setParent(dataOut);

    delete data;
    delete x;
  }

  int ierr = dataOut->write(this->m_outputFile);
  if (ierr != 0) {
    emit error("Error writing data to file.");
    return;
  }

  return;
}

void MetOceanData::getUsgsData() {
  QVector<Station> s;
  bool found = this->findStation(this->station(), StationLocations::USGS, s);
  if (!found) {
    emit error("Station not found.");
    emit finished();
    return;
  }

  QString productId = QString();
  if (this->m_productId != QString()) {
    productId = this->m_productId;
  }

  Hmdf *data2 = new Hmdf(this);

  for (size_t i = 0; i < s.size(); ++i) {
    Hmdf *data = new Hmdf(this);
    UsgsWaterdata *usgs =
        new UsgsWaterdata(s[i], this->startDate(), this->endDate(), 0, this);
    int ierr = usgs->get(data);
    if (ierr != 0) {
      emit error(s[i].name() + ": " + usgs->errorString());
      continue;
    }

    if (productId == QString() && this->m_product == -1) {
      if (this->printAvailableProducts(data, false) != 0) return;
    }

    int productIndex;
    if (productId != QString()) {
      productIndex = this->getUSGSProductIndex(data, productId);
    } else {
      productIndex = this->m_product - 1;
      productId = data->station(productIndex)->id();
    }

    if (productIndex < 0) continue;

    data2->addStation(data->station(productIndex));
    data2->setUnits(data->station(productIndex)->name().split(",").value(0));
    data2->setDatum("usgs_datum");
    data2->station(i)->setName(s.at(i).name());
    data2->station(i)->setId(s.at(i).id());
  }

  if (data2->nstations() > 0) {
    int ierr = data2->write(this->m_outputFile);
    if (ierr != 0) {
      emit error("Error writing to file.");
      return;
    }
  } else {
    emit error("No station data found.");
    return;
  }

  return;
}

int MetOceanData::printAvailableProducts(Hmdf *data, bool reselect) {
  if (reselect && this->m_product > 0 && this->m_product <= data->nstations()) {
    if (this->m_previousProduct != QString() &&
        this->m_previousProduct == data->station(this->m_product)->name()) {
      return 0;
    }
  } else if (!reselect && this->m_product > 0) {
    return 0;
  }

  int selection;
  std::cout << "Select product" << std::endl;
  for (int i = 0; i < data->nstations(); i++) {
    std::cout << "(" << i + 1 << ") " << data->station(i)->name().toStdString()
              << std::endl;
  }
  std::cout << "==> ";
  std::cin >> selection;

  if (selection > 0 && selection <= data->nstations()) {
    this->m_product = selection;
    this->m_previousProduct = data->station(selection - 1)->name();
    return 0;
  } else {
    emit error("Invalid selection");
    return 1;
  }
}

int MetOceanData::getUSGSProductIndex(Hmdf *stationdata,
                                      const QString &product) {
  for (size_t i = 0; i < stationdata->nstations(); ++i) {
    if (stationdata->station(i)->id() == product) return i;
  }
  return -1;
}

void MetOceanData::getNoaaData() {
  QVector<Station> s;
  bool found = this->findStation(this->station(), StationLocations::NOAA, s);
  if (!found) {
    emit error("Station not found.");
    emit finished();
    return;
  }

  QString p = this->noaaIndexToProduct();
  if (p == QString()) {
    emit finished();
    return;
  }

  QString d = this->indexToDatum();
  if (d == QString()) {
    emit finished();
    return;
  }

  Datum::VDatum datumid = Datum::VDatum::NullDatum;
  if (this->m_usevdatum) {
    datumid = Datum::datumID(d);
  }

  QString u = this->noaaIndexToUnits();

  Hmdf *dataOut = new Hmdf(this);

  for (size_t i = 0; i < s.size(); ++i) {
    QString d2 = "MSL";
    if (!this->m_usevdatum) d2 = d;

    NoaaCoOps *coops = new NoaaCoOps(s[i], this->startDate(), this->endDate(),
                                     p, d2, this->m_usevdatum, "metric", this);
    Hmdf *data = new Hmdf(this);
    int ierr = coops->get(data);
    if (ierr != 0) {
      emit warning(QString(s[i].id() + ": " + coops->errorString()));
      delete data;
      delete coops;
      continue;
    }

    if (this->m_usevdatum) {
      if (!data->applyDatumCorrection(s[i], datumid)) {
        std::cout << "Warning: Could not convert datum for "
                  << s[i].name().toStdString() << ". Using MSL." << std::endl;
        data->setDatum("MSL");
      } else {
        data->setDatum(d);
      }
    } else {
      data->setDatum(d);
    }

    data->setUnits(u);

    dataOut->addStation(data->station(0));
    data->station(0)->setParent(dataOut);

    delete data;
    delete coops;
  }

  int ierr = dataOut->write(this->m_outputFile);
  if (ierr != 0) {
    emit error("Error writing data to file");
    return;
  }

  return;
}

QString MetOceanData::noaaIndexToProduct() {
  if (this->m_product < 1 || this->m_product > noaaProducts.size() + 1) {
    int selection;
    std::cout << "Select NOAA product" << std::endl;
    for (int i = 0; i < noaaProducts.size(); i++) {
      std::cout << "(" << i + 1 << ") "
                << noaaProductLongName[i + 1].toStdString() << std::endl;
    }
    std::cout << "==> ";
    std::cin >> selection;
    if (selection > 0 && selection < noaaProducts.size())
      this->m_product = selection;
    else {
      emit error("Invalid product selection.");
      return QString();
    }
  }
  return noaaProducts[this->m_product];
}

QString MetOceanData::indexToDatum() {
  if (this->m_service == NOAA && this->m_product > 3) {
    return QStringLiteral("Stnd");
  }

  if (this->m_usevdatum) {
    if (this->m_datum < 1 || this->m_datum > vDatum.size() + 1) {
      int selection;
      std::cout << "Select Datum" << std::endl;
      for (int i = 0; i < vDatum.size(); i++) {
        std::cout << "(" << i + 1 << ") " << vDatum[i + 1].toStdString()
                  << std::endl;
      }
      std::cout << "==> ";
      std::cin >> selection;
      if (selection > 0 && selection < vDatum.size() + 1) {
        this->m_datum = selection;
        return vDatum[this->m_datum];
      } else {
        emit error("Invliad datum selection.");
        return QString();
      }
    } else {
      return vDatum[this->m_datum];
    }
  } else {
    if (this->m_datum < 1 || this->m_datum > noaaDatum.size() + 1) {
      int selection;
      std::cout << "Select Datum" << std::endl;
      for (int i = 0; i < noaaDatum.size(); i++) {
        std::cout << "(" << i + 1 << ") " << noaaDatum[i + 1].toStdString()
                  << std::endl;
      }
      std::cout << "==> ";
      std::cin >> selection;
      if (selection > 0 && selection < noaaDatum.size()) {
        this->m_datum = selection;
        return noaaDatum[this->m_datum];
      } else {
        emit error("Invliad datum selection.");
        return QString();
      }
    } else {
      return noaaDatum[this->m_datum];
    }
  }
}

QString MetOceanData::noaaIndexToUnits() { return noaaUnits[this->m_product]; }

int MetOceanData::getDatum() const { return m_datum; }

void MetOceanData::setDatum(int datum) { m_datum = datum; }
