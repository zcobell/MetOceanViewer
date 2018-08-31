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

static QHash<int, QString> noaaProducts = {
    {1, "water_level"},     {2, "hourly_height"},     {3, "predictions"},
    {4, "air_temperature"}, {5, "water_temperature"}, {6, "wind:speed"},
    {7, "humidity"},        {8, "air_pressure"},      {9, "wind:direction"},
    {10, "wind:gusts"}};
static QHash<int, QString> noaaProductLongName = {{1, "6 minute water level"},
                                                  {2, "Hourly water level"},
                                                  {3, "Tide predictoins"},
                                                  {4, "Air temperature"},
                                                  {5, "Water temperature"},
                                                  {6, "Wind speed"},
                                                  {7, "Humidity"},
                                                  {8, "Air pressure"},
                                                  {9, "Wind direction"},
                                                  {10, "Wind gusts"}};
static QHash<int, QString> noaaUnits = {
    {1, "m"},   {2, "m"}, {3, "m"},  {4, "C"},   {5, "C"},
    {6, "m/s"}, {7, "%"}, {8, "mb"}, {9, "deg"}, {10, "m/s"}};
static QHash<int, QString> noaaDatum = {
    {1, "MHHW"}, {2, "MHW"}, {3, "MTL"}, {4, "MSL"},   {5, "MLW"},  {6, "MLLW"},
    {7, "NAVD"}, {8, "LWI"}, {9, "HWI"}, {10, "IGLD"}, {11, "Stnd"}};

MetOceanData::MetOceanData(QObject *parent) : QObject(parent) {
  this->m_service = 0;
  this->m_product = 0;
  this->m_datum = 0;
  this->m_station = QStringList();
  this->m_startDate = QDateTime();
  this->m_endDate = QDateTime();
  this->m_outputFile = QString();
}

MetOceanData::MetOceanData(serviceTypes service, QStringList station,
                           int product, int datum, QDateTime startDate,
                           QDateTime endDate, QString outputFile,
                           QObject *parent)
    : QObject(parent) {
  this->m_service = service;
  this->m_product = product;
  this->m_station = station;
  this->m_datum = datum;
  this->m_startDate = startDate;
  this->m_endDate = endDate;
  this->m_outputFile = outputFile;
}

int MetOceanData::service() const { return m_service; }

void MetOceanData::setService(int service) { m_service = service; }

QStringList MetOceanData::station() const { return m_station; }

void MetOceanData::setStation(QStringList station) { m_station = station; }

QDateTime MetOceanData::startDate() const { return m_startDate; }

void MetOceanData::setStartDate(const QDateTime &startDate) {
  m_startDate = startDate;
}

QDateTime MetOceanData::endDate() const { return m_endDate; }

void MetOceanData::setEndDate(const QDateTime &endDate) { m_endDate = endDate; }

QString MetOceanData::outputFile() const { return m_outputFile; }

void MetOceanData::setOutputFile(const QString &outputFile) {
  m_outputFile = outputFile;
}

void MetOceanData::setLoggingActive() {
  connect(this, SIGNAL(error(QString)), this, SLOT(showError(QString)));
  connect(this, SIGNAL(status(QString, int)), this,
          SLOT(showStatus(QString, int)));
  return;
}

void MetOceanData::setLoggingInactive() {
  disconnect(this, SIGNAL(error(QString)), this, SLOT(showError(QString)));
  disconnect(this, SIGNAL(status(QString, int)), this,
             SLOT(showStatus(QString, int)));
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
  if (this->service() == USGS && this->m_station.length() > 1) {
    emit error(
        "Error: Beacuase each station has different characteristics, only one "
        "USGS station may be selected at a time.");
    emit finished();
    return;
  }

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
  size_t j = -1;
  for (size_t i = 0; i < markerLocations.size(); ++i) {
    double xs = markerLocations[i].coordinate().longitude();
    double ys = markerLocations[i].coordinate().latitude();
    double d1 = Constants::distance(x, y, xs, ys, true);
    if (d1 < d) {
      d = d1;
      j = i;
    }
  }
  if (j > -1) {
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
      break;
    case USGS:
      return StationLocations::USGS;
      break;
    case XTIDE:
      return StationLocations::XTIDE;
      break;
    case NDBC:
      return StationLocations::NDBC;
      break;
    default:
      return StationLocations::NOAA;
      break;
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

    ierr = this->printAvailableProducts(data);
    if (ierr != 0) return;

    dataOut->station(0)->setName(s[i].name());
    dataOut->station(0)->setId(s[i].id());

    dataOut->addStation(data->station(this->m_product - 1));
    data->station(this->m_product - 1)->setParent(dataOut);

    delete ndbc;
    delete data;
  }

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

    data->setDatum("MLLW");
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

  Hmdf *data = new Hmdf(this);
  UsgsWaterdata *usgs =
      new UsgsWaterdata(s[0], this->startDate(), this->endDate(), 0, this);
  int ierr = usgs->get(data);
  if (ierr != 0) {
    emit error(usgs->errorString());
    return;
  }

  ierr = this->printAvailableProducts(data);
  if (ierr != 0) return;

  Hmdf *data2 = new Hmdf(this);
  data2->addStation(data->station(this->m_product - 1));
  data2->setUnits(
      data->station(this->m_product - 1)->name().split(",").value(0));
  data2->setDatum("usgs_datum");
  data2->station(0)->setName(s.at(0).name());
  data2->station(0)->setId(s.at(0).id());

  ierr = data2->write(this->m_outputFile);
  if (ierr != 0) {
    emit error("Error writing to file.");
    return;
  }

  return;
}

int MetOceanData::printAvailableProducts(Hmdf *data) {
  if (this->m_product > 0 && this->m_product <= data->nstations()) {
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
    return 0;
  } else {
    emit error("Invalid selection");
    return 1;
  }
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

  QString d = this->noaaIndexToDatum();
  if (d == QString()) {
    emit finished();
    return;
  }

  QString u = this->noaaIndexToUnits();

  Hmdf *dataOut = new Hmdf(this);

  for (size_t i = 0; i < s.size(); ++i) {
    NoaaCoOps *coops = new NoaaCoOps(s[i], this->startDate(), this->endDate(),
                                     p, d, "metric", this);
    Hmdf *data = new Hmdf(this);
    int ierr = coops->get(data);
    if (ierr != 0) {
      emit warning(QString(s[i].id() + ": " + coops->errorString()));
      delete data;
      delete coops;
      continue;
    }

    data->setUnits(u);
    data->setDatum(d);

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

QString MetOceanData::noaaIndexToDatum() {
  if (this->m_product > 3) {
    return QStringLiteral("Stnd");
  }

  if (this->m_datum < 1 || this->m_datum > noaaDatum.size() + 1) {
    int selection;
    std::cout << "Select NOAA datum" << std::endl;
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

QString MetOceanData::noaaIndexToUnits() { return noaaUnits[this->m_product]; }

int MetOceanData::getDatum() const { return m_datum; }

void MetOceanData::setDatum(int datum) { m_datum = datum; }
