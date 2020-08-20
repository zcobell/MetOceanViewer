#include "userdataseries.h"

#include "fileinfo.h"

UserdataSeries::UserdataSeries() {}

QString UserdataSeries::filename() const {
  return FileInfo::basename(this->m_filename);
}

void UserdataSeries::setFilename(const QString &filename) {
  m_filename = filename;
}

QString UserdataSeries::seriesName() const { return m_seriesName; }

void UserdataSeries::setSeriesName(const QString &seriesName) {
  m_seriesName = seriesName;
}

QColor UserdataSeries::color() const { return m_color; }

void UserdataSeries::setColor(const QColor &color) { m_color = color; }

double UserdataSeries::unitConversion() const { return m_unitConversion; }

void UserdataSeries::setUnitConversion(double unitConversion) {
  m_unitConversion = unitConversion;
}

double UserdataSeries::xshift() const { return m_xshift; }

void UserdataSeries::setXshift(double xshift) { m_xshift = xshift; }

double UserdataSeries::yshift() const { return m_yshift; }

void UserdataSeries::setYshift(double yshift) { m_yshift = yshift; }

QString UserdataSeries::fullPath() const { return m_filename; }

QDateTime UserdataSeries::coldStart() const { return m_coldStart; }

void UserdataSeries::setColdStart(const QDateTime &coldStart) {
  m_coldStart = coldStart;
}

QString UserdataSeries::fileType() const { return m_fileType; }

void UserdataSeries::setFileType(const QString &fileType) {
  m_fileType = fileType;
}

QString UserdataSeries::stationFile() const {
  return FileInfo::basename(this->m_stationFilePath);
}

QString UserdataSeries::stationFilePath() const { return m_stationFilePath; }

void UserdataSeries::setStationFilePath(const QString &stationFilePath) {
  m_stationFilePath = stationFilePath;
}

int UserdataSeries::epsg() const { return m_epsg; }

void UserdataSeries::setEpsg(int epsg) { m_epsg = epsg; }

QString UserdataSeries::dflowvar() const { return m_dflowvar; }

void UserdataSeries::setDflowvar(const QString &dflowvar) {
  m_dflowvar = dflowvar;
}

int UserdataSeries::layer() const { return m_layer; }

void UserdataSeries::setLayer(int layer) { m_layer = layer; }

int UserdataSeries::linestyle() const { return m_linestyle; }

void UserdataSeries::setLinestyle(int linestyle) { m_linestyle = linestyle; }

double UserdataSeries::timeUnits() const { return m_timeUnits; }

void UserdataSeries::setTimeUnits(double timeUnits) { m_timeUnits = timeUnits; }
