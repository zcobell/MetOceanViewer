#include "timeseriesoptionsdialog.h"
#include "ui_movusertimeseriesoptions.h"

TimeseriesOptionsDialog::TimeseriesOptionsDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::TimeseriesOptionsDialog) {
  ui->setupUi(this);
  this->_mDisplayValues = false;
  this->_mHideInfoWindows = false;
}

TimeseriesOptionsDialog::~TimeseriesOptionsDialog() { delete ui; }

bool TimeseriesOptionsDialog::displayValues() { return this->_mDisplayValues; }

bool TimeseriesOptionsDialog::hideInfoWindows() {
  return this->_mHideInfoWindows;
}

void TimeseriesOptionsDialog::on_check_displayValues_toggled(bool checked) {
  this->_mDisplayValues = checked;
  return;
}

void TimeseriesOptionsDialog::on_check_hideBubbles_toggled(bool checked) {
  this->_mHideInfoWindows = checked;
  return;
}

void TimeseriesOptionsDialog::setDisplayValues(bool value) {
  this->_mDisplayValues = value;
  return;
}

void TimeseriesOptionsDialog::setHideInfoWindows(bool value) {
  this->_mHideInfoWindows = value;
  return;
}

int TimeseriesOptionsDialog::exec() {
  ui->check_displayValues->setChecked(this->_mDisplayValues);
  ui->check_hideBubbles->setChecked(this->_mHideInfoWindows);
  return QDialog::exec();
}

void TimeseriesOptionsDialog::setShowHideInfoWindowOption(bool value) {
  if (value)
    ui->check_hideBubbles->show();
  else
    ui->check_hideBubbles->hide();
  return;
}
