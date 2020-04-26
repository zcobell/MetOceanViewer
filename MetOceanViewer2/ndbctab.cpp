#include "ndbctab.h"

NdbcTab::NdbcTab(QVector<Station> *stations, QWidget *parent)
    : MapChartWidget(TabType::NDBC, stations, parent) {
  this->initialize();
}

void NdbcTab::connectSignals() { MapChartWidget::connectSignals(); }

QGroupBox *NdbcTab::generateInputBox() {
  QGroupBox *box = new QGroupBox(this);
  box->setTitle("NDBC Stataion Download Options");

  this->m_btn_fetchData = new QPushButton(this);
  this->m_btn_fetchData->setText("Fetch Data");
  this->setStartDateEdit(new DateBox("Start Date:", this));
  this->setEndDateEdit(new DateBox("End Date:", this));
  this->setTimezoneCombo(new ComboBox("Timezone:", this));
  this->setCbx_mapType(new ComboBox("Map:", this));
  this->setChartOptions(
      new ChartOptionsMenu(true, true, true, false, true, true, this));

  QDateTime startDate =
      QDateTime::currentDateTimeUtc().addYears(-1).addDays(-14);
  QDateTime endDate = QDateTime::currentDateTime().addYears(-1);

  this->startDateEdit()->dateEdit()->setDateTime(startDate);
  this->endDateEdit()->dateEdit()->setDateTime(endDate);

  this->mapWidget()->mapFunctions()->setMapTypes(this->cbx_mapType()->combo());
  this->timezoneCombo()->combo()->addItems(timezoneList());
  this->timezoneCombo()->combo()->setCurrentText("GMT");

  this->startDateEdit()->setMinimumWidth(160);
  this->startDateEdit()->setMaximumWidth(160);
  this->endDateEdit()->setMinimumWidth(160);
  this->endDateEdit()->setMaximumWidth(160);

  QHBoxLayout *r1 = new QHBoxLayout();
  r1->addLayout(this->startDateEdit()->layout());
  r1->addLayout(this->endDateEdit()->layout());
  r1->addWidget(this->m_btn_fetchData);

  QHBoxLayout *r2 = new QHBoxLayout();
  r2->addLayout(this->cbx_mapType()->layout());
  r2->addLayout(this->timezoneCombo()->layout());
  r2->addWidget(this->chartOptions());

  r1->addStretch();
  r1->addSpacing(0);
  r2->addStretch();
  r2->addSpacing(0);

  QVBoxLayout *allLayout = new QVBoxLayout();
  allLayout->addLayout(r1);
  allLayout->addLayout(r2);
  allLayout->addStretch();
  allLayout->addSpacing(0);
  allLayout->setContentsMargins(5, 0, 5, 5);

  box->setMaximumHeight(100);
  box->setMinimumHeight(100);
  box->setLayout(allLayout);

  return box;
}

void NdbcTab::plot() { return; }
