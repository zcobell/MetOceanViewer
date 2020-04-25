#ifndef USGSTAB_H
#define USGSTAB_H

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QVBoxLayout>
#include <tuple>
#include <vector>

#include "combobox.h"
#include "datebox.h"
#include "mapchartwidget.h"

class UsgsTab : public MapChartWidget {
  Q_OBJECT
 public:
  UsgsTab(QVector<Station> *stations, QWidget *parent = nullptr);

 private slots:
  void plot() override;
  void replot(int);
  void saveData() override;

 private:
  QGroupBox *generateInputBox() override;
  void connectSignals() override;
  std::tuple<QString, QString> splitUsgsProductName(const QString &product);
  void addSeriesToChart(int const index, const QString &name, const qint64 tzoffset);
  int getDatabaseType();

  QPushButton *m_btn_fetch;
  QLabel *m_lbl_buttonGroup;
  QRadioButton *m_rbtn_historic;
  QRadioButton *m_rbtn_instant;
  QRadioButton *m_rbtn_daily;
  ComboBox *m_cbx_product;
  ComboBox *m_cbx_mapType;
  QButtonGroup *m_buttonGroup;
  std::vector<QHBoxLayout *> m_rowLayouts;
  Station m_currentStation;
  bool m_ready;
};

#endif  // USGSTAB_H
