#ifndef NOAATAB_H
#define NOAATAB_H

#include <QObject>

#include "chartoptionsmenu.h"
#include "combobox.h"
#include "datebox.h"
#include "hmdf.h"
#include "mapchartwidget.h"
#include "noaaproductlist.h"

class NoaaTab : public MapChartWidget {
  Q_OBJECT
 public:
  NoaaTab(QVector<Station> *stations, QWidget *parent = nullptr);

  void plot() override;

 private slots:
  void updateDatumList(bool b);
  void refreshStations(bool b);
  void saveData() override;

 private:
  QGroupBox *generateInputBox() override;
  void connectSignals() override;
  int calculateDateInfo(QDateTime &startDate, QDateTime &endDate,
                        QDateTime &startDateGmt, QDateTime &endDateGmt,
                        QString &timezoneString, qint64 &tzOffset);
  std::pair<QString, bool> getDatumParameters();
  int getDataFromNoaa(const Station &s,
                      const NoaaProductList::NoaaProduct &product,
                      const QDateTime startDate, const QDateTime endDate,
                      const QString &datumString, Hmdf *data);
  QString getUnitsLabel(const NoaaProductList::NoaaProduct &p);
  void performDatumTransformation(const Station &s, Hmdf *data);
  void setPlotAxis(Hmdf *data, const QDateTime &startDate,
                   const QDateTime &endDate, const QString &tzAbbrev,
                   const QString &datumString, const QString &unitString,
                   const QString &productName);
  void addSeriesToChart(Hmdf *data, const qint64 &tzOffset);

  //...Variables
  NoaaProductList m_noaaProductList;

  //...Widgets
  DateBox *m_dte_startDate;
  DateBox *m_dte_endDate;
  QPushButton *m_btn_refresh;
  QPushButton *m_btn_plot;
  ComboBox *m_cbx_timezones;
  ComboBox *m_cbx_datum;
  ComboBox *m_cbx_datatype;
  ComboBox *m_cbx_units;
  ComboBox *m_cbx_mapType;
  QCheckBox *m_chk_activeOnly;
  QCheckBox *m_chk_vdatum;
  ChartOptionsMenu *m_chartOptions;
  std::unique_ptr<Hmdf> m_noaaData;
};

#endif  // NOAATAB_H
