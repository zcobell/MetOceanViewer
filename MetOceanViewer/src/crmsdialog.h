/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2018  Zach Cobell
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------*/
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
