#ifndef TIMESERIESOPTIONSDIALOG_H
#define TIMESERIESOPTIONSDIALOG_H

#include <QDialog>

namespace Ui {
class TimeseriesOptionsDialog;
}

class TimeseriesOptionsDialog : public QDialog {
  Q_OBJECT

 public:
  explicit TimeseriesOptionsDialog(QWidget *parent = nullptr);
  ~TimeseriesOptionsDialog();

  bool displayValues();
  void setDisplayValues(bool value);

 private slots:
  void on_check_displayValues_toggled(bool checked);

 public slots:
  int exec();

 private:
  Ui::TimeseriesOptionsDialog *ui;

  bool _mDisplayValues;
};

#endif  // TIMESERIESOPTIONSDIALOG_H
