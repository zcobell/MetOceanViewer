#ifndef NDBCTAB_H
#define NDBCTAB_H

#include <QObject>
#include <QPushButton>

#include "mapchartwidget.h"

class NdbcTab : public MapChartWidget {
  Q_OBJECT
 public:
  NdbcTab(QVector<MovStation> *stations, QWidget *parent = nullptr);
  void plot() override;

 private slots:
  void draw(int index);

 private:
  QGroupBox *generateInputBox() override;
  void connectSignals() override;
  int getDataFromNdbc(const MovStation &s);
  void addSeriesToChart(const int index, const QString &name,
                        const qint64 tzOffset);

  bool m_ready;
  MovStation m_currentStation;
  QPushButton *m_btn_fetchData;
  ComboBox *m_cbx_product;
};

#endif  // NDBCTAB_H
