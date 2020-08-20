#include "doublespinbox.h"

DoubleSpinBox::DoubleSpinBox(QWidget *parent) : QDoubleSpinBox(parent) {}

QString DoubleSpinBox::textFromValue(double val) const {
  return QString::number(val);
}
