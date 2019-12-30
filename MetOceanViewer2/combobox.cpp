#include "combobox.h"

#include <QFontMetrics>

ComboBox::ComboBox(QWidget *parent) : QWidget(parent) { this->initialize(); }

ComboBox::ComboBox(const QString &label, QWidget *parent) : QWidget(parent) {
  this->initialize(label);
}

void ComboBox::initialize(const QString &label) {
  this->m_combo = new QComboBox(this);
  this->m_label = new QLabel(this);
  this->m_layout = new QHBoxLayout();
  QFontMetrics fm(this->m_combo->font());
  this->m_label->setText(label);
  this->m_layout->addWidget(this->m_label);
  this->m_layout->addWidget(this->m_combo);
  this->m_combo->setMinimumHeight(fm.height()*1.25);
  this->m_label->setMinimumHeight(fm.height()*1.25);
}

QComboBox *ComboBox::combo() const { return m_combo; }

QLabel *ComboBox::label() const { return m_label; }

QHBoxLayout *ComboBox::layout() const { return m_layout; }
