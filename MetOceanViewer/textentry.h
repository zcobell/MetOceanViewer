#ifndef TEXTENTRY_H
#define TEXTENTRY_H

#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QWidget>

#include "doublespinbox.h"

template <typename T>
class TextEntry : public QWidget {
 public:
  template <typename U = T>
  TextEntry(QString labelText, U defaultValue = 0.0, size_t labelWidth = 0,
            size_t boxWidth = 0, QWidget *parent = nullptr,
            typename std::enable_if<std::is_floating_point<U>::value>::type * =
                nullptr)
      : QWidget(parent),
        m_label(new QLabel(parent)),
        m_double(new DoubleSpinBox(parent)) {
    this->setLayout(new QHBoxLayout());
    this->m_double->setMinimumHeight(30);
    this->m_double->setAlignment(Qt::AlignLeft);
    this->m_label->setText(labelText);
    this->m_double->setValue(defaultValue);
    this->setWidth(this->m_label, labelWidth);
    this->setWidth(this->m_double, boxWidth);
    this->layout()->addWidget(this->m_label);
    this->layout()->addItem(
        new QSpacerItem(5, 0, QSizePolicy::Fixed, QSizePolicy::Preferred));
    this->layout()->addWidget(this->m_double);
    this->layout()->addItem(
        new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
    this->arrange();
  }

  template <typename U = T>
  TextEntry(
      QString labelText, U defaultValue = 0, size_t labelWidth = 0,
      size_t boxWidth = 0, QWidget *parent = nullptr,
      typename std::enable_if<std::is_integral<U>::value>::type * = nullptr)
      : QWidget(parent),
        m_label(new QLabel(parent)),
        m_integer(new QSpinBox(parent)) {
    this->setLayout(new QHBoxLayout());
    this->m_integer->setMinimumHeight(30);
    this->m_integer->setAlignment(Qt::AlignLeft);
    this->m_label->setText(labelText);
    this->m_integer->setValue(defaultValue);
    this->setWidth(this->m_label, labelWidth);
    this->setWidth(this->m_integer, boxWidth);
    this->layout()->addWidget(this->m_label);
    this->layout()->addItem(
        new QSpacerItem(5, 0, QSizePolicy::Fixed, QSizePolicy::Preferred));
    this->layout()->addWidget(this->m_integer);
    this->layout()->addItem(
        new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
    this->arrange();
  }

  template <typename U = T>
  TextEntry(QString labelText, QString defaultText = QString(),
            size_t labelWidth = 0, size_t boxWidth = 0,
            QWidget *parent = nullptr,
            typename std::enable_if<std::is_same<U, QString>::value>::type * =
                nullptr)
      : QWidget(parent),
        m_label(new QLabel(parent)),
        m_text(new QLineEdit(parent)) {
    this->setLayout(new QHBoxLayout());
    this->m_text->setMinimumHeight(30);
    this->m_text->setAlignment(Qt::AlignLeft);
    this->m_label->setText(labelText);
    this->m_text->setText(defaultText);
    this->setWidth(this->m_label, labelWidth);
    this->setWidth(this->m_text, boxWidth);
    this->layout()->addWidget(this->m_label);
    this->layout()->addItem(
        new QSpacerItem(5, 0, QSizePolicy::Fixed, QSizePolicy::Preferred));
    this->layout()->addWidget(this->m_text);
    this->layout()->addItem(
        new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
    this->arrange();
  }

  template <typename U = T>
  DoubleSpinBox *box(
      typename std::enable_if<std::is_floating_point<U>::value>::type * =
          nullptr) {
    return this->m_double;
  }

  template <typename U = T>
  QSpinBox *box(
      typename std::enable_if<std::is_integral<U>::value>::type * = nullptr) {
    return this->m_integer;
  }

  template <typename U = T>
  QLineEdit *box(typename std::enable_if<std::is_same<U, QString>::value>::type
                     * = nullptr) {
    return this->m_text;
  }

  QLabel *label() { return this->m_label; }

 private:
  QLabel *m_label;
  QLineEdit *m_text;
  DoubleSpinBox *m_double;
  QSpinBox *m_integer;

  void setWidth(QWidget *w, int width) {
    if (width <= 0) {
      w->setMinimumWidth(w->minimumSizeHint().width());
      w->setMaximumWidth(w->minimumSizeHint().width());
    } else {
      w->setMinimumWidth(width);
      w->setMaximumWidth(width);
    }
  }

  void arrange() {
    this->m_label->setMinimumHeight(30);
    this->m_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    this->layout()->setAlignment(Qt::AlignVCenter);
  }
};

#endif  // TEXTENTRY_H
