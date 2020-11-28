#ifndef UNITSMENU_H
#define UNITSMENU_H

#include <QDoubleSpinBox>
#include <QMenu>
#include <QPushButton>

class UnitsMenu : public QPushButton {
  Q_OBJECT
 public:
  explicit UnitsMenu(QDoubleSpinBox *target, QWidget *parent = nullptr);

 signals:

 private:
  void setUnitValue(const double &v);
  void generateMenu();
  QDoubleSpinBox *m_target;
  QMenu *m_menu;
};

#endif  // UNITSMENU_H
