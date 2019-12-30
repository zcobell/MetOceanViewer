#ifndef MAPCHARTWIDGET_H
#define MAPCHARTWIDGET_H

#include <QGroupBox>
#include <QHBoxLayout>
#include <QMenu>
#include <QTabWidget>

#include "chartoptionsmenu.h"
#include "chartview.h"
#include "hmdf.h"
#include "hmdfstation.h"
#include "mapview.h"
#include "tabtypes.h"

class MapChartWidget : public QWidget {
  Q_OBJECT

 public:
  MapChartWidget(TabType type, QVector<Station> *stations,
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

  QLineSeries *stationToSeries(HmdfStation *s, qint64 offset);

  QHBoxLayout *generateMapChartLayout();
  virtual QGroupBox *generateInputBox();
  virtual void connectSignals();

 signals:
  void error(QString);
  void warning(QString);

 protected:
  void writeData(Hmdf *data);

 protected slots:
  void chartOptionsChangeTriggered();

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
};

#endif  // MAPCHARTWIDGET_H
