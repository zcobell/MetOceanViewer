#ifndef NEWTABDIALOG_H
#define NEWTABDIALOG_H

#include <QDialog>
#include "tabtypes.h"

namespace Ui {
class NewTabDialog;
}

class NewTabDialog : public QDialog {
  Q_OBJECT

 public:
  explicit NewTabDialog(QWidget *parent = nullptr);
  ~NewTabDialog();

  TabType type() const;

  QString tabName() const;

 private slots:
  void setType(const TabType &type);

  void on_button_noaa_clicked();

  void on_button_usgs_clicked();

  void on_button_ndbc_clicked();

  void on_button_xtide_clicked();

  void on_button_timeseries_clicked();

  void on_button_hwm_clicked();

 private:
  void closeEvent(QCloseEvent *event) override;

 private:
  Ui::NewTabDialog *ui;
  TabType m_type;
};

#endif  // NEWTABDIALOG_H
