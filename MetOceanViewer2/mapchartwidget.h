#ifndef MAPCHARTWIDGET_H
#define MAPCHARTWIDGET_H

#include <QGroupBox>
#include <QHBoxLayout>
#include <QMenu>
#include <QTabWidget>

#include "chartoptionsmenu.h"
#include "chartview.h"
#include "combobox.h"
#include "datebox.h"
#include "hmdf.h"
#include "hmdfstation.h"
#include "mapview.h"
#include "tabtypes.h"

class MapChartWidget : public QWidget {
  Q_OBJECT

 public:
  MapChartWidget(TabType type, QVector<Station> *stations = nullptr,
                 QWidget *parent = nullptr);

  TabType type() const;

  void initialize();

  MapView *mapWidget() const;
  ChartView *chartview() const;

 protected:
  ChartOptionsMenu *chartOptions() const;
  void setChartOptions(ChartOptionsMenu *chartOptions);

  QStringList timezoneList();

  void setMapWidget(MapView *mapWidget);

  void setChartview(ChartView *chartview);

  void setStations(QVector<Station> *station);

  QLineSeries *stationToSeries(HmdfStation *s, qint64 offset);

  QHBoxLayout *generateMapChartLayout();
  virtual QGroupBox *generateInputBox();
  virtual void connectSignals();

  MapFunctions *mapFunctions() const;

 signals:
  void error(QString);
  void warning(QString);

 protected:
  virtual void writeData(Hmdf *data);
  virtual int calculateDateInfo(QDateTime &startDate, QDateTime &endDate,
                                QDateTime &startDateGmt, QDateTime &endDateGmt,
                                QString &timezoneString, qint64 &tzOffset);
  virtual void setPlotAxis(Hmdf *data, const QDateTime &startDate,
                           const QDateTime &endDate, const QString &tzAbbrev,
                           const QString &datumString = QString(),
                           const QString &unitString = QString(),
                           const QString &productName = QString());

  DateBox *startDateEdit();
  void setStartDateEdit(DateBox *widget);

  DateBox *endDateEdit();
  void setEndDateEdit(DateBox *widget);

  ComboBox *timezoneCombo();
  void setTimezoneCombo(ComboBox *widget);

  ComboBox *cbx_mapType() const;
  void setCbx_mapType(ComboBox *cbx_mapType);

  std::unique_ptr<Hmdf> *data();

 public slots:
  void changeBasemapEsri();
  void changeBasemapOsm();
  void changeBasemapMapbox();

 protected slots:
  void chartOptionsChangeTriggered();
  void changeMapType(const MapFunctions::MapSource s);
  virtual void refreshStations();

 private slots:
  virtual void plot();

  virtual void saveGraphic();
  virtual void saveData();
  virtual void resetChart();
  virtual void toggleDisplayValues(bool);
  virtual void fitMarkers();

  void showErrorDialog(QString errorString);
  void showWarningDialog(QString warningString);

  void keyPressEvent(QKeyEvent *event) override;

 private:
  QVector<Station> *m_stations;
  const TabType m_type;
  MapView *m_mapWidget;
  QHBoxLayout *m_mapLayout;
  QVBoxLayout *m_windowLayout;
  QGroupBox *m_inputBox;
  ChartOptionsMenu *m_chartOptions;
  ChartView *m_chartview;
  DateBox *m_dte_startDate;
  DateBox *m_dte_endDate;
  ComboBox *m_cbx_timezone;
  ComboBox *m_cbx_mapType;
  std::unique_ptr<MapFunctions> m_mapFunctions;
  std::unique_ptr<Hmdf> m_data;
};

#endif  // MAPCHARTWIDGET_H
