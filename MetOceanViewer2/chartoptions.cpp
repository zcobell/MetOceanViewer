#include "chartoptions.h"

#include "ui_chartoptions.h"

ChartOptions::ChartOptions(int titleFontsize, int axisFontsize,
                           int legendFontsize, QString dateFormat,
                           QWidget *parent)
    : QDialog(parent), ui(new Ui::ChartOptions) {
  ui->setupUi(this);

  ui->combo_dateFormat->addItems(QStringList() << "auto"
                                               << "MM/yyyy"
                                               << "MM/dd/yyyy"
                                               << "MM/dd hh:mm"
                                               << "MM/dd/yyyy hh:mm");

  ui->spin_titleFont->setValue(titleFontsize);
  ui->spin_axisLabelFont->setValue(axisFontsize);
  ui->spin_legendFont->setValue(legendFontsize);
  ui->combo_dateFormat->setCurrentText(dateFormat);
}

ChartOptions::~ChartOptions() { delete ui; }

int ChartOptions::titleFontsize() const { return ui->spin_titleFont->value(); }

void ChartOptions::setTitleFontsize(int titleFontsize) {
  ui->spin_titleFont->setValue(titleFontsize);
}

int ChartOptions::axisFontsize() const {
  return ui->spin_axisLabelFont->value();
}

void ChartOptions::setAxisFontsize(int axisFontsize) {
  ui->spin_axisLabelFont->setValue(axisFontsize);
}

int ChartOptions::legendFontsize() const {
  return ui->spin_legendFont->value();
}

void ChartOptions::setLegendFontsize(int legendFontsize) {
  ui->spin_legendFont->setValue(legendFontsize);
}

QString ChartOptions::dateFormat() const {
  return ui->combo_dateFormat->currentText();
}

void ChartOptions::setDateFormat(const QString &dateFormat) {
  ui->combo_dateFormat->setCurrentText(dateFormat);
}
