#include "unitsmenu.h"

UnitsMenu::UnitsMenu(QDoubleSpinBox *target, QWidget *parent)
    : QPushButton(parent), m_target(target) {
  this->generateMenu();
  this->setText("Conversions");
  this->setMenu(this->m_menu);
}

void UnitsMenu::generateMenu() {
  this->m_menu = new QMenu(this);
  QMenu *length = new QMenu(this->m_menu);
  QMenu *speed = new QMenu(this->m_menu);
  QMenu *pressure = new QMenu(this->m_menu);
  length->setTitle("Length");
  speed->setTitle("Speed");
  pressure->setTitle("Pressure");

  QAction *meters2feet = length->addAction("meters --> feet");
  QAction *feet2meters = length->addAction("feet --> meters");
  QAction *meters2inches = length->addAction("meters --> inches");
  QAction *feet2inches = length->addAction("feet --> inches");
  QAction *miles2meters = length->addAction("miles --> meters");
  QAction *miles2feet = length->addAction("miles --> feet");
  QAction *kilo2miles = length->addAction("kilometers --> miles");
  QAction *miles2kilo = length->addAction("miles --> kilometers");

  connect(meters2feet, &QAction::triggered,
          [=] { this->setUnitValue(3.28084); });
  connect(feet2meters, &QAction::triggered,
          [=] { this->setUnitValue(0.3048); });
  connect(meters2inches, &QAction::triggered,
          [=] { this->setUnitValue(39.3701); });
  connect(feet2inches, &QAction::triggered, [=] { this->setUnitValue(12.0); });
  connect(miles2meters, &QAction::triggered,
          [=] { this->setUnitValue(1609.34); });
  connect(miles2feet, &QAction::triggered, [=] { this->setUnitValue(5280.0); });
  connect(kilo2miles, &QAction::triggered,
          [=] { this->setUnitValue(0.621371); });
  connect(miles2kilo, &QAction::triggered,
          [=] { this->setUnitValue(1.60934); });

  QAction *mpsmph = speed->addAction("m/s --> mph");
  QAction *mphmps = speed->addAction("mph --> m/s");
  QAction *mphfps = speed->addAction("mph --> fps");
  QAction *mpskt = speed->addAction("m/s --> knots");
  QAction *ktmps = speed->addAction("knots --> m/s");
  QAction *mpsfps = speed->addAction("m/s --> f/s");
  QAction *fpsmps = speed->addAction("f/s --> m/s");
  QAction *fpskt = speed->addAction("f/s --> knots");
  QAction *ktfps = speed->addAction("knots --> f/s");

  connect(mpsmph, &QAction::triggered, [=] { this->setUnitValue(2.23694); });
  connect(mphmps, &QAction::triggered, [=] { this->setUnitValue(0.44704); });
  connect(mphfps, &QAction::triggered, [=] { this->setUnitValue(1.46667); });
  connect(mpskt, &QAction::triggered, [=] { this->setUnitValue(1.94384); });
  connect(ktmps, &QAction::triggered, [=] { this->setUnitValue(0.514444); });
  connect(mpsfps, &QAction::triggered, [=] { this->setUnitValue(3.28084); });
  connect(fpsmps, &QAction::triggered, [=] { this->setUnitValue(0.3048); });
  connect(fpskt, &QAction::triggered, [=] { this->setUnitValue(0.592484); });
  connect(ktfps, &QAction::triggered, [=] { this->setUnitValue(1.68781); });

  QAction *mh202mb = pressure->addAction("mH20 --> mb");
  QAction *mb2mh20 = pressure->addAction("mb --> mH20");
  QAction *mH202pa = pressure->addAction("mH20 --> pa");
  QAction *pa2mH20 = pressure->addAction("pa --> mH20");
  QAction *mb2pa = pressure->addAction("mb --> pa");
  QAction *pa2mb = pressure->addAction("pa --> mb");

  connect(mh202mb, &QAction::triggered, [=] { this->setUnitValue(98.07); });
  connect(mb2mh20, &QAction::triggered, [=] { this->setUnitValue(0.010197); });
  connect(mH202pa, &QAction::triggered, [=] { this->setUnitValue(9806.38); });
  connect(pa2mH20, &QAction::triggered,
          [=] { this->setUnitValue(0.00010197442889221); });
  connect(mb2pa, &QAction::triggered, [=] { this->setUnitValue(100.0); });
  connect(pa2mb, &QAction::triggered, [=] { this->setUnitValue(0.01); });

  this->m_menu->addMenu(length);
  this->m_menu->addMenu(speed);
  this->m_menu->addMenu(pressure);
}

void UnitsMenu::setUnitValue(const double &v) { this->m_target->setValue(v); }
