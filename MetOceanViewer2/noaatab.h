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
  NoaaTab(QVector<MovStation> *stations, QWidget *parent = nullptr);

  void plot() override;

 private slots:
  void updateDatumList(bool b);
  void refreshStations() override;

 private:
  QGroupBox *generateInputBox() override;
  void connectSignals() override;
  std::pair<QString, bool> getDatumParameters();
  int getDataFromNoaa(const MovStation &s,
                      const NoaaProductList::NoaaProduct &product,
                      const QDateTime startDate, const QDateTime endDate,
                      const QString &datumString, Hmdf::HmdfData *data);
  QString getUnitsLabel(const NoaaProductList::NoaaProduct &p);
  void performDatumTransformation(const MovStation &s, Hmdf::HmdfData *data);
  void addSeriesToChart(Hmdf::HmdfData *data, const qint64 &tzOffset);

  //...Variables
  NoaaProductList m_noaaProductList;

  //...Widgets
  QPushButton *m_btn_plot;
  ComboBox *m_cbx_datum;
  ComboBox *m_cbx_datatype;
  ComboBox *m_cbx_units;
  QCheckBox *m_chk_activeOnly;
  QCheckBox *m_chk_vdatum;
  std::vector<QHBoxLayout *> m_rowLayouts;
};

#endif  // NOAATAB_H
