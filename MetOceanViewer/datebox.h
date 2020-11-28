#ifndef DATEBOX_H
#define DATEBOX_H

#include <QDateTimeEdit>
#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>

class DateBox : public QWidget {
  Q_OBJECT
 public:
  explicit DateBox(QWidget *parent = nullptr);
  explicit DateBox(const QString &label, QWidget *parent = nullptr);

  QDateTimeEdit *dateEdit() const;

  QLabel *label() const;

  void setToSizeHints();

 private:
  void initialize(const QString &label = QString());

  QDateTimeEdit *m_dateEdit;
  QLabel *m_label;
};

#endif  // DATEBOX_H
