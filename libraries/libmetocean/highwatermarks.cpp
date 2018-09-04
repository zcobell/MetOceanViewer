#include "highwatermarks.h"
#include <QFile>
#include <QString>
#include <QStringList>
#include <cmath>

HighWaterMarks::HighWaterMarks(QObject *parent) : QObject(parent) {
  this->m_filename = QString();
  this->m_regressionThroughZero = true;
  this->m_r2 = -1;
  this->m_slope = 0.0;
  this->m_intercept = 0.0;
  this->m_standardDeviation = 0.0;
}

HighWaterMarks::HighWaterMarks(QString filename, bool regressionThroughZero,
                               QObject *parent)
    : QObject(parent) {
  this->m_filename = filename;
  this->m_regressionThroughZero = regressionThroughZero;
  this->m_r2 = -1;
  this->m_slope = 0.0;
  this->m_intercept = 0.0;
  this->m_standardDeviation = 0.0;
}

bool HighWaterMarks::regressionThroughZero() const {
  return m_regressionThroughZero;
}

void HighWaterMarks::setRegressionThroughZero(bool regressionThroughZero) {
  m_regressionThroughZero = regressionThroughZero;
}

QString HighWaterMarks::filename() const { return m_filename; }

void HighWaterMarks::setFilename(const QString &filename) {
  m_filename = filename;
}

HwmData *HighWaterMarks::hwm(size_t index) {
  if (index >= 0 && index < this->m_hwms.size())
    return this->m_hwms[index];
  else
    return nullptr;
}

double HighWaterMarks::r2() const { return m_r2; }

double HighWaterMarks::standardDeviation() const { return m_standardDeviation; }

double HighWaterMarks::slope() const { return m_slope; }

double HighWaterMarks::intercept() const { return m_intercept; }

int HighWaterMarks::read() {
  if (this->m_filename == QString()) return 1;

  QFile f(this->m_filename);
  if (!f.open(QIODevice::ReadOnly)) {
    return 1;
  }

  while (!f.atEnd()) {
    QString line = f.readLine().simplified();
    QStringList list = line.split(",");
    double lon = list.value(0).toDouble();
    double lat = list.value(1).toDouble();
    double bathy = list.value(2).toDouble();
    double measured = list.value(3).toDouble();
    double modeled = list.value(4).toDouble();
    this->m_hwms.push_back(
        new HwmData(QGeoCoordinate(lat, lon), bathy, modeled, measured, this));
  }
  f.close();
  if (this->m_hwms.size() > 0) {
    this->calculateStats();
    return 0;
  } else {
    return 2;
  }
}

int HighWaterMarks::calculateStats() {
  double sumXY = 0;
  double sumX2 = 0;
  double sumY2 = 0;
  double sumY = 0;
  double sumX = 0;
  double n = static_cast<double>(this->m_hwms.size());
  double ndry = 0;
  double sumErr = 0;
  double meanErr = 0;
  for (int i = 0; i < n; ++i) {
    // We ditch points that didn't wet since they
    // skew calculation
    if (this->m_hwms[i]->modeledElevation() > -999) {
      sumX = sumX + (this->m_hwms[i]->observedElevation());
      sumY = sumY + (this->m_hwms[i]->modeledElevation());
      sumXY = sumXY + (this->m_hwms[i]->observedElevation() *
                       this->m_hwms[i]->modeledElevation());
      sumX2 = sumX2 + (this->m_hwms[i]->observedElevation() *
                       this->m_hwms[i]->observedElevation());
      sumY2 = sumY2 + (this->m_hwms[i]->modeledElevation() *
                       this->m_hwms[i]->modeledElevation());
      sumErr = sumErr + this->m_hwms[i]->modeledError();
    } else {
      ndry = ndry + 1;
    }
  }

  // Number of points that we'll end up using
  this->m_n2 = n - ndry;

  // Calculate the slope (M) and Correllation (R2)
  if (this->regressionThroughZero()) {
    this->m_slope = sumXY / sumX2;
    this->m_intercept = 0;

    double ybar = sumY / this->m_n2;
    double sstot = 0.0;
    for (int i = 0; i < n; ++i) {
      // We ditch points that didn't wet since they
      // skew calculation
      if (this->m_hwms[i]->modeledElevation() > -9999) {
        sstot += pow((this->m_hwms[i]->modeledElevation() - ybar), 2.0);
      }
    }

    // Sum of square errors
    double sse = sumY2 - pow(this->slope(), 2) * sumX2;

    this->m_r2 = 1.0 - (sse / sstot);
  } else {
    this->m_slope = (this->m_n2 * sumXY - sumX * sumY) /
                    (this->m_n2 * sumX2 - (sumX * sumX));
    this->m_intercept = ((sumY * sumX2) - (sumX * sumXY)) /
                        (this->m_n2 * sumX2 - (sumX * sumX));
    this->m_r2 = pow(((this->m_n2 * sumXY - (sumX * sumY)) /
                      sqrt((this->m_n2 * sumX2 - (sumX * sumX)) *
                           (this->m_n2 * sumY2 - (sumY * sumY)))),
                     2.0);
  }

  // Calculate Standard Deviation
  meanErr = sumErr / this->m_n2;
  sumErr = 0;
  for (int i = 0; i < n; ++i)
    if (this->m_hwms[i]->modeledElevation() > -999)
      sumErr += pow(this->m_hwms[i]->modeledError() - meanErr, 2.0);

  this->m_standardDeviation = sqrt(sumErr / this->m_n2);

  return 0;
}

size_t HighWaterMarks::n() { return this->m_hwms.size(); }

size_t HighWaterMarks::nValid() { return this->m_n2; }

void HighWaterMarks::clear() {
  for (size_t i = 0; i < this->m_hwms.size(); ++i) {
    delete this->m_hwms[i];
  }
  this->m_hwms.clear();
  return;
}
