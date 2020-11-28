#ifndef USERDATASERIES_H
#define USERDATASERIES_H

#include <QColor>
#include <QDateTime>
#include <QString>

class UserdataSeries {
 public:
  UserdataSeries();

  QString filename() const;
  void setFilename(const QString &filename);

  QString seriesName() const;
  void setSeriesName(const QString &seriesName);

  QColor color() const;
  void setColor(const QColor &color);

  double unitConversion() const;
  void setUnitConversion(double unitConversion);

  double xshift() const;
  void setXshift(double xshift);

  double yshift() const;
  void setYshift(double yshift);

  QString fullPath() const;

  QDateTime coldStart() const;
  void setColdStart(const QDateTime &coldStart);

  QString fileType() const;
  void setFileType(const QString &fileType);

  QString stationFile() const;

  QString stationFilePath() const;
  void setStationFilePath(const QString &stationFilePath);

  int epsg() const;
  void setEpsg(int epsg);

  QString dflowvar() const;
  void setDflowvar(const QString &dflowvar);

  int layer() const;
  void setLayer(int layer);

  int linestyle() const;
  void setLinestyle(int linestyle);

  double timeUnits() const;
  void setTimeUnits(double timeUnits);

private:
  QString m_filename;
  QString m_seriesName;
  QColor m_color;
  double m_unitConversion;
  double m_xshift;
  double m_yshift;
  double m_timeUnits;
  QDateTime m_coldStart;
  QString m_fileType;
  QString m_stationFilePath;
  int m_epsg;
  QString m_dflowvar;
  int m_layer;
  int m_linestyle;
};

#endif  // USERDATASERIES_H
