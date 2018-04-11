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

  bool hideInfoWindows();
  bool displayValues();
  void setDisplayValues(bool value);
  void setHideInfoWindows(bool value);
  void setShowHideInfoWindowOption(bool value);

private slots:
  void on_check_displayValues_toggled(bool checked);

  void on_check_hideBubbles_toggled(bool checked);

public slots:
  int exec();

private:
  Ui::TimeseriesOptionsDialog *ui;

  bool _mHideInfoWindows;
  bool _mDisplayValues;
};

#endif // TIMESERIESOPTIONSDIALOG_H
