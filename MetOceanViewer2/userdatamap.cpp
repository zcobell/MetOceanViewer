#include "userdatamap.h"

UserdataMap::UserdataMap(QWidget *parent)
    : MapChartWidget(TabType::USERTS, nullptr, parent) {}

void UserdataMap::plot() {}

void UserdataMap::refreshStations() {}

QGroupBox *UserdataMap::generateInputBox() {
  QGroupBox *input = new QGroupBox(this);

  this->setCbx_mapType(new ComboBox("Map: ", this));
  this->setChartOptions(
      new ChartOptionsMenu(true, false, true, true, true, true, this));
  this->mapWidget()->mapFunctions()->setMapTypes(this->cbx_mapType()->combo());

  this->cbx_mapType()->setMinimumWidth(250);
  this->cbx_mapType()->setMaximumWidth(250);

  QHBoxLayout *hlayout = new QHBoxLayout();
  hlayout->addWidget(this->cbx_mapType());
  hlayout->addWidget(this->chartOptions());
  hlayout->addStretch();
  input->setLayout(hlayout);
  input->setTitle("Map Options");

  return input;
}

void UserdataMap::connectSignals() { MapChartWidget::connectSignals(); }
