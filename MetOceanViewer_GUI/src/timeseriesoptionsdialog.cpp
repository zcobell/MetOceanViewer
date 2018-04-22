#include "timeseriesoptionsdialog.h"
#include "ui_timeseriesoptionsdialog.h"

TimeseriesOptionsDialog::TimeseriesOptionsDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::TimeseriesOptionsDialog) {
  ui->setupUi(this);
  this->_mDisplayValues = false;
}

TimeseriesOptionsDialog::~TimeseriesOptionsDialog() { delete ui; }

bool TimeseriesOptionsDialog::displayValues() { return this->_mDisplayValues; }

void TimeseriesOptionsDialog::on_check_displayValues_toggled(bool checked) {
  this->_mDisplayValues = checked;
  return;
}

void TimeseriesOptionsDialog::setDisplayValues(bool value) {
  this->_mDisplayValues = value;
  return;
}

int TimeseriesOptionsDialog::exec() {
  ui->check_displayValues->setChecked(this->_mDisplayValues);
  return QDialog::exec();
}
