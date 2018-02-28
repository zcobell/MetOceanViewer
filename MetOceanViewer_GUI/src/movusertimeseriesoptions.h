#ifndef MOVUSERTIMESERIESOPTIONS_H
#define MOVUSERTIMESERIESOPTIONS_H

#include <QDialog>

namespace Ui {
class movUserTimeseriesOptions;
}

class movUserTimeseriesOptions : public QDialog {
  Q_OBJECT

public:
  explicit movUserTimeseriesOptions(QWidget *parent = nullptr);
  ~movUserTimeseriesOptions();

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
  Ui::movUserTimeseriesOptions *ui;

  bool _mHideInfoWindows;
  bool _mDisplayValues;
};

#endif // MOVUSERTIMESERIESOPTIONS_H
