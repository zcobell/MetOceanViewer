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
  explicit UsgsTab(std::vector<MovStation> *stations,
                   QWidget *parent = nullptr);

private slots:
  void plot() override;
  void replot(int);
  void saveData() override;

private:
  QGroupBox *generateInputBox() override;
  void connectSignals() override;
  static QString splitUsgsProductName(const std::string &product);
  void addSeriesToChart(int index, const QString &name, qint64 tzoffset);
  int getDatabaseType();

  QPushButton *m_btn_fetch;
  QLabel *m_lbl_buttonGroup;
  QRadioButton *m_rbtn_instant;
  QRadioButton *m_rbtn_daily;
  ComboBox *m_cbx_product;
  QButtonGroup *m_buttonGroup;
  std::vector<QHBoxLayout *> m_rowLayouts;
  MovStation m_currentStation;
  bool m_ready;
};

#endif // USGSTAB_H
