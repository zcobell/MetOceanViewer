#ifndef HWMDATA_H
#define HWMDATA_H

#include <QGeoCoordinate>
#include <QObject>

class HwmData : public QObject {
  Q_OBJECT
 public:
  explicit HwmData(QObject *parent = nullptr);
  explicit HwmData(QGeoCoordinate coordinate, double topoElevation,
                   double modeledElevation, double observedElevation,
                   QObject *parent = nullptr);

  double topoElevation() const;
  void setTopoElevation(double topoElevation);

  double modeledElevation() const;
  void setModeledElevation(double modeledElevation);

  double observedElevation() const;
  void setObservedElevation(double observedElevation);

  bool isValid();

  double modeledError();

  QGeoCoordinate *coordinate();

 private:
  double m_topoElevation;
  double m_modeledElevation;
  double m_observedElevation;
  QGeoCoordinate m_coordinate;
};

#endif  // HWMDATA_H
