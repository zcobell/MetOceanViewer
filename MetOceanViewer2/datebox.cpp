#include "datebox.h"
#include <QDebug>

DateBox::DateBox(QWidget *parent) : QWidget(parent) { this->initialize(); }

DateBox::DateBox(const QString &label, QWidget *parent) : QWidget(parent) {
    this->initialize(label);
}

void DateBox::initialize(const QString &label) {
  this->m_dateEdit = new QDateTimeEdit(this);
  this->m_label = new QLabel(this);
  this->m_layout = new QHBoxLayout();
  this->m_label->setText(label);
  this->m_dateEdit->setCalendarPopup(true);
  this->m_layout->addWidget(this->m_label);
  this->m_layout->addWidget(this->m_dateEdit);
}

QDateTimeEdit *DateBox::dateEdit() const { return m_dateEdit; }

QLabel *DateBox::label() const { return m_label; }

QHBoxLayout *DateBox::layout() const { return m_layout; }
