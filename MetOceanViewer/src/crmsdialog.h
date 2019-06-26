#ifndef CRMSDIALOG_H
#define CRMSDIALOG_H

#include <QCloseEvent>
#include <QDialog>
#include <QProgressDialog>
#include <QThread>

namespace Ui {
class CrmsDialog;
}

class CrmsDialog : public QDialog {
  Q_OBJECT

 public:
  explicit CrmsDialog(QWidget *parent = nullptr);
  ~CrmsDialog();

 private slots:
  void on_button_browse_clicked();
  void on_button_process_clicked();
  void lock();
  void unlock();
  void successDialog();
  void errorDialog();

 signals:
  void dialogClosed();

 private:
  Ui::CrmsDialog *ui;
  QThread *m_thread;
  void closeEvent(QCloseEvent *event);
};

#endif  // CRMSDIALOG_H
