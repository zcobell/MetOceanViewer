#include "combobox.h"

ComboBox::ComboBox(QWidget *parent)
    : QWidget(parent), m_combo(new QComboBox(this)), m_label(new QLabel(this)) {
  this->initialize();
}

ComboBox::ComboBox(const QString &label, QWidget *parent)
    : QWidget(parent), m_combo(new QComboBox(this)), m_label(new QLabel(this)) {
  this->initialize(label);
}

void ComboBox::initialize(const QString &label) {
  this->setLayout(new QHBoxLayout());
  this->m_label->setText(label);
  this->layout()->addItem(
      new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
  this->layout()->addWidget(this->m_label);
  this->layout()->addItem(
      new QSpacerItem(5, 0, QSizePolicy::Fixed, QSizePolicy::Preferred));
  this->layout()->addWidget(this->m_combo);
  this->layout()->addItem(
      new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
  this->m_label->setMinimumHeight(30);
  this->m_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  this->layout()->setAlignment(Qt::AlignVCenter);
  this->setToSizeHint();
}

QComboBox *ComboBox::combo() const { return m_combo; }

QLabel *ComboBox::label() const { return m_label; }

void ComboBox::setToSizeHint() {
  int labelMin = this->m_label->minimumSizeHint().width();
  this->m_label->setMinimumWidth(labelMin);
  this->m_label->setMaximumWidth(labelMin + 10);
  this->m_combo->setSizeAdjustPolicy(
      QComboBox::SizeAdjustPolicy::AdjustToContents);
}
