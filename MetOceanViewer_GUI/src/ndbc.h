#ifndef NDBC_H
#define NDBC_H

#include <QObject>
#include <QQuickWidget>
#include "chartview.h"
#include "hmdf.h"
#include "stationmodel.h"

class Ndbc : public QObject {
  Q_OBJECT
 public:
  explicit Ndbc(QQuickWidget *inMap, ChartView *inChart,
                QComboBox *inProductBox, QDateTimeEdit *inStartDateEdit,
                QDateTimeEdit *inEndDateEdit, QStatusBar *instatusBar,
                StationModel *stationModel, QString *inSelectedStation,
                QObject *parent = nullptr);

  int plotStation();
  int replotStation(int index);

 signals:
  void ndbcError(QString);

 private:
  int plot(int index);

  QQuickWidget *m_map;
  ChartView *m_chartView;
  QComboBox *m_productBox;
  QDateTimeEdit *m_startDateEdit, *m_endDateEdit;
  StationModel *m_stationModel;
  QStatusBar *m_statusBar;
  QString *m_selectedStation;
  Hmdf *m_data;

  Station m_station;
};

#endif  // NDBC_H
