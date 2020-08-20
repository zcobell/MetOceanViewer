#ifndef DOUBLESPINBOX_H
#define DOUBLESPINBOX_H

#include <QDoubleSpinBox>

class DoubleSpinBox : public QDoubleSpinBox {
 public:
  DoubleSpinBox(QWidget *parent = nullptr);

 protected:
  QString textFromValue(double val) const override;
};

#endif  // DOUBLESPINBOX_H
