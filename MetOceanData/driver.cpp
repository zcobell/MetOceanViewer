#include "driver.h"
#include <QHash>
#include <iostream>
#include "generic.h"
#include "hmdf.h"
#include "ndbcdata.h"
#include "noaacoops.h"
#include "station.h"
#include "stationlocations.h"
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

Driver::Driver(QObject *parent) : QObject(parent) {}

Driver::Driver(serviceTypes service, QString station, int product, int datum,
               QDateTime startDate, QDateTime endDate, QString outputFile,
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

int Driver::service() const { return m_service; }

void Driver::setService(int service) { m_service = service; }

QString Driver::station() const { return m_station; }

void Driver::setStation(QString station) { m_station = station; }

QDateTime Driver::startDate() const { return m_startDate; }

void Driver::setStartDate(const QDateTime &startDate) {
  m_startDate = startDate;
}

QDateTime Driver::endDate() const { return m_endDate; }

void Driver::setEndDate(const QDateTime &endDate) { m_endDate = endDate; }

QString Driver::outputFile() const { return m_outputFile; }

void Driver::setOutputFile(const QString &outputFile) {
  m_outputFile = outputFile;
}

void Driver::setLoggingActive() {
  connect(this, SIGNAL(error(QString)), this, SLOT(showError(QString)));
  connect(this, SIGNAL(status(QString, int)), this,
          SLOT(showStatus(QString, int)));
  return;
}

void Driver::setLoggingInactive() {
  disconnect(this, SIGNAL(error(QString)), this, SLOT(showError(QString)));
  disconnect(this, SIGNAL(status(QString, int)), this,
             SLOT(showStatus(QString, int)));
  return;
}

void Driver::showError(QString errorString) {
  QTextStream out(stderr, QIODevice::WriteOnly);
  out << "ERROR: " << errorString << "\n";
  out.flush();
  return;
}

void Driver::showStatus(QString message, int pct) {
  QTextStream out(stdout, QIODevice::WriteOnly);
  QString complete;

  complete.sprintf("%3i", pct);
  out << "[" << complete << "%] " << message << "\n";
  out.flush();

  return;
}

void Driver::run() {
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

bool Driver::findStation(QString name, StationLocations::MarkerType type,
                         Station &s) {
  bool found = false;
  QVector<Station> markerLocations = StationLocations::readMarkers(type);
  for (size_t i = 0; i < markerLocations.length(); ++i) {
    if (name.simplified() == markerLocations[i].id().simplified()) {
      found = true;
      s = markerLocations[i];
      break;
    }
  }
  return found;
}

void Driver::getNdbcData() {
  Station s;
  bool found = this->findStation(this->station(), StationLocations::NDBC, s);
  if (!found) {
    emit error("Station not found.");
    emit finished();
    return;
  }

  Hmdf *data = new Hmdf(this);
  NdbcData *ndbc = new NdbcData(s, this->startDate(), this->endDate(), this);
  int ierr = ndbc->get(data);
  if (ierr != 0) {
    emit error(ndbc->errorString());
    return;
  }

  ierr = this->printAvailableProducts(data);
  if (ierr != 0) return;

  Hmdf *data2 = new Hmdf(this);
  data2->addStation(data->station(this->m_product - 1));
  data2->setUnits("ndbc_units");
  data2->setDatum("ndbc_datum");
  data2->station(0)->setName(s.name());
  data2->station(0)->setId(s.id());

  ierr = data2->write(this->m_outputFile);
  if(ierr!=0){
    emit error("Error writing to file.");
    return;
  }

  return;
}

void Driver::getXtideData() {
  Station s;
  bool found = this->findStation(this->station(), StationLocations::XTIDE, s);
  if (!found) {
    emit error("Station not found.");
    emit finished();
    return;
  }

  Generic::createConfigDirectory();

  Hmdf *data = new Hmdf(this);
  XtideData *x = new XtideData(s, this->startDate(), this->endDate(),
                               Generic::configDirectory(), this);
  int ierr = x->get(data);
  if (ierr != 0) {
    emit error(x->errorString());
    return;
  }

  data->setDatum("MLLW");
  data->setUnits("m");

  ierr = data->write(this->m_outputFile);
  if (ierr != 0) {
    emit error("Error writing data to file.");
    return;
  }

  return;
}

void Driver::getUsgsData() {
  Station s;
  bool found = this->findStation(this->station(), StationLocations::USGS, s);
  if (!found) {
    emit error("Station not found.");
    emit finished();
    return;
  }

  Hmdf *data = new Hmdf(this);
  UsgsWaterdata *usgs =
      new UsgsWaterdata(s, this->startDate(), this->endDate(), 0, this);
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
  data2->station(0)->setName(s.name());
  data2->station(0)->setId(s.id());

  ierr = data2->write(this->m_outputFile);
  if(ierr!=0){
    emit error("Error writing to file.");
    return;
  }

  return;
}

int Driver::printAvailableProducts(Hmdf *data) {
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

void Driver::getNoaaData() {
  Station s;
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

  NoaaCoOps *coops = new NoaaCoOps(s, this->startDate(), this->endDate(), p, d,
                                   "metric", this);
  Hmdf *data = new Hmdf(this);
  int ierr = coops->get(data);
  if (ierr != 0) {
    emit error(coops->errorString());
    return;
  }

  data->setUnits(u);
  data->setDatum(d);
  ierr = data->write(this->m_outputFile);
  if (ierr != 0) {
    emit error("Error writing data to file");
    return;
  }

  return;
}

QString Driver::noaaIndexToProduct() {
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

QString Driver::noaaIndexToDatum() {
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

QString Driver::noaaIndexToUnits() { return noaaUnits[this->m_product]; }

int Driver::getDatum() const { return m_datum; }

void Driver::setDatum(int datum) { m_datum = datum; }
