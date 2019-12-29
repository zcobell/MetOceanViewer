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

  QHBoxLayout *layout() const;

 private:
  void initialize(const QString &label = QString());

  QDateTimeEdit *m_dateEdit;
  QLabel *m_label;
  QHBoxLayout *m_layout;
};

#endif  // DATEBOX_H
