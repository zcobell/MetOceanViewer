#ifndef USERDATAMAP_H
#define USERDATAMAP_H

#include "mapchartwidget.h"

class UserdataMap : public MapChartWidget {
  Q_OBJECT
 public:
  UserdataMap(QWidget *parent = nullptr);

 private slots:
  void plot() override;
  void refreshStations() override;

 private:
  QGroupBox *generateInputBox() override;
  void connectSignals() override;
};

#endif  // USERDATAMAP_H
