#ifndef XTIDETAB_H
#define XTIDETAB_H

#include <memory>

#include "mapchartwidget.h"

class XTideTab : public MapChartWidget {
  Q_OBJECT
 public:
  XTideTab(std::vector<MovStation> *stations, QWidget *parent = nullptr);

 private slots:
  void plot() override;
  void refreshStations() override;

 private:
  QGroupBox *generateInputBox() override;
  void connectSignals() override;
  void addSeriesToChart(Hmdf::HmdfData *data, const qint64 tzOffset);

  ComboBox *m_cbx_datum;
  ComboBox *m_cbx_units;
  QPushButton *m_btn_compute;
};

#endif  // XTIDETAB_H
