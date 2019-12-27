#include "newtabdialog.h"

#include <array>

#include "ui_newtabdialog.h"

std::array<QString, 7> c_names = {
    "unknown",         "NOAA", "USGS", "NDBC", "XTIDE", "User Timeseries",
    "High Water Marks"};

NewTabDialog::NewTabDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::NewTabDialog), m_type(UNKNOWN) {
  ui->setupUi(this);
}

NewTabDialog::~NewTabDialog() { delete ui; }

void NewTabDialog::on_button_noaa_clicked() {
  this->setType(TabType::NOAA);
  this->accept();
}

void NewTabDialog::on_button_usgs_clicked() {
  this->setType(TabType::USGS);
  this->accept();
}

void NewTabDialog::on_button_ndbc_clicked() {
  this->setType(TabType::NDBC);
  this->accept();
}

void NewTabDialog::on_button_xtide_clicked() {
  this->setType(TabType::XTIDE);
  this->accept();
}

void NewTabDialog::on_button_timeseries_clicked() {
  this->setType(TabType::USERTS);
  this->accept();
}

void NewTabDialog::on_button_hwm_clicked() {
  this->setResult(TabType::USERHWM);
  this->accept();
}

void NewTabDialog::closeEvent(QCloseEvent *event) {
  this->reject();
  QDialog::closeEvent(event);
}

TabType NewTabDialog::type() const { return m_type; }

void NewTabDialog::setType(const TabType &type) { m_type = type; }

QString NewTabDialog::tabName() const {
  return c_names[static_cast<size_t>(m_type)];
}
