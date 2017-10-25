#ifndef MOVDFLOW_H
#define MOVDFLOW_H

#include "movErrors.h"
#include "movImeds.h"
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QObject>
#include <QVector>

class MovDflow : public QObject {
  Q_OBJECT
public:
  explicit MovDflow(QString filename, QObject *parent = 0);

  QStringList getVaribleList();

  int dflowToImeds(QString variable);

  int getStations();

  int getVariable(QString variable, int layer, MovImeds *imeds);

  int getNumLayers();

  bool is3d();

  bool variableIs3d(QString variable);

  movErrors *error;

private:
  int _init();
  int _getPlottingVariables();
  int _getStations();
  int _get3d();
  int _getTime(QVector<QDateTime> &timeList);
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
  int _nStations;
  int _nSteps;
  int _nLayers;
  QString _filename;
  QMap<QString, int> _varnames;
  QMap<QString, int> _dimnames;
  QMap<QString, int> _nDims;
  QList<QString> _plotvarnames;
  QVector<double> _xCoordinates;
  QVector<double> _yCoordinates;
  QVector<QString> _stationNames;
  QDateTime _refTime;
};

#endif // MOVDFLOW_H
