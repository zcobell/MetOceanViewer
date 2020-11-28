#ifndef DFLOW_H
#define DFLOW_H

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QObject>
#include <QVector>
#include "errors.h"
#include "hmdf.h"

class Dflow : public QObject {
  Q_OBJECT
 public:
  explicit Dflow(QString filename, QObject *parent = nullptr);

  QStringList getVaribleList();

  int dflowToImeds(QString variable);

  int getStations();

  int getVariable(QString variable, int layer, Hmdf *hmdf);

  int getNumLayers();

  bool is3d();

  bool variableIs3d(QString variable);

  Errors *error;

 private:
  int _init();
  int _getPlottingVariables();
  int _getStations();
  int _get3d();
  int _getTime(QVector<qint64> &timeList);
  int _getVar(QString variable, int layer, QVector<QVector<double>> &data);
  int _getVar2D(QString variable, QVector<QVector<double>> &data);
  int _getVar3D(QString variable, int layer, QVector<QVector<double>> &data);
  int _get2DVelocityMagnitude(int layer, QVector<QVector<double>> &data);
  int _get2DVelocityDirection(int layer, QVector<QVector<double>> &data);
  int _get3DVeloctiyMagnitude(int layer, QVector<QVector<double>> &data);
  int _getWindVelocityMagnitude(QVector<QVector<double>> &data);
  int _getWindDirection(QVector<QVector<double>> &data);

  bool _isInitialized;
  bool _readError;
  bool _is3d;
  size_t _nStations;
  size_t _nSteps;
  size_t _nLayers;
  QString _filename;
  QMap<QString, size_t> _varnames;
  QMap<QString, size_t> _dimnames;
  QMap<QString, size_t> _nDims;
  QList<QString> _plotvarnames;
  QVector<double> _xCoordinates;
  QVector<double> _yCoordinates;
  QVector<QString> _stationNames;
  QDateTime _refTime;
};

#endif  // DFLOW_H
