#include "datebox.h"

#include <QDebug>

DateBox::DateBox(QWidget *parent) : QWidget(parent) { this->initialize(); }

DateBox::DateBox(const QString &label, QWidget *parent) : QWidget(parent) {
  this->initialize(label);
}

void DateBox::initialize(const QString &label) {
  this->setLayout(new QHBoxLayout());
  this->m_dateEdit = new QDateTimeEdit(this);
  this->m_label = new QLabel(this);
  this->m_label->setText(label);
  this->m_dateEdit->setCalendarPopup(true);
  this->layout()->addWidget(this->m_label);
  this->layout()->addWidget(this->m_dateEdit);
  this->m_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  this->m_label->setContentsMargins(0, 0, 0, 0);
  this->m_dateEdit->setContentsMargins(0, 0, 0, 0);
  this->setContentsMargins(0, 0, 0, 0);
  this->layout()->setSpacing(0);
  this->layout()->setMargin(0);
  this->setToSizeHints();
}

QDateTimeEdit *DateBox::dateEdit() const { return m_dateEdit; }

QLabel *DateBox::label() const { return m_label; }

void DateBox::setToSizeHints() {
  this->m_label->setMinimumWidth(this->m_label->minimumSizeHint().width());
  this->m_label->setMaximumWidth(this->m_label->minimumSizeHint().width() + 10);
  this->m_dateEdit->setMinimumWidth(
      this->m_dateEdit->minimumSizeHint().width());
  this->m_dateEdit->setMaximumWidth(
      this->m_dateEdit->minimumSizeHint().width() + 10);
}
