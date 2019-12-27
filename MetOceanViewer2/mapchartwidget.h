#ifndef MAPCHARTWIDGET_H
#define MAPCHARTWIDGET_H

#include <QGroupBox>
#include <QHBoxLayout>
#include <QTabWidget>

#include "chartview.h"
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

 protected:
  QStringList timezoneList();

  MapView *mapWidget() const;
  void setMapWidget(MapView *mapWidget);

  ChartView *chartview() const;
  void setChartview(ChartView *chartview);

  QLineSeries *stationToSeries(HmdfStation *s);

  QHBoxLayout *generateMapChartLayout();
  virtual QGroupBox *generateInputBox();

 private slots:
  virtual void plot();

 private:
  QVector<Station> *m_stations;
  const TabType m_type;
  MapView *m_mapWidget;
  ChartView *m_chartview;
};

#endif  // MAPCHARTWIDGET_H
