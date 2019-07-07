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
#include "crmsdialog.h"
#include <QFileDialog>
#include <QMessageBox>
#include "crmsdatabase.h"
#include "generic.h"
#include "ui_crmsdialog.h"

static QString s_crmsInstructions =
    "<html><head/><body><p>You can manually convert the CIMS database to "
    "netCDF format for MetOceanViewer using the following built-in processing. "
    "However, the process is completed automatically and a publicly accessable "
    "database is updated weekly <a href=\" "
    "https://metoceanviewer.s3.amazonaws.com/crms.nc\"><span style=\""
    "text-decoration : underline;color : "
    "#0000ff;\">here.</span></a></p><p>Place the downloaded file "
    "here: " +
    Generic::crmsDataFile() + "</p></body></html>";

CrmsDialog::CrmsDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::CrmsDialog) {
  ui->setupUi(this);
  ui->progressbar->setRange(0, 100);
  ui->progressbar->setValue(0);
  ui->progressbar->hide();
  this->m_thread = new QThread(this);
  ui->label_crmsInstructions->setText(s_crmsInstructions);
}

CrmsDialog::~CrmsDialog() { delete ui; }

void CrmsDialog::on_button_browse_clicked() {
  QString file = QFileDialog::getOpenFileName(this, "Select CRMS CSV file",
                                              QDir::homePath(), "*.csv");
  if (file != QString()) {
    ui->text_filename->setText(file);
  }
  return;
}

void CrmsDialog::on_button_process_clicked() {
  ui->progressbar->setValue(0);

  if (ui->text_filename->text() == QString()) {
    QMessageBox::critical(this, "Error", "No filename specified");
    return;
  }

  QFileInfo fi(ui->text_filename->text());
  if (!fi.exists()) {
    QMessageBox::critical(this, "Error",
                          "Specified input file does not exist.");
    return;
  }

  QString outputFile = Generic::crmsDataFile();
  CrmsDatabase *c = new CrmsDatabase(ui->text_filename->text().toStdString(),
                                     outputFile.toStdString());
  c->moveToThread(this->m_thread);
  connect(this->m_thread, SIGNAL(started()), c, SLOT(parse()));
  connect(c, SIGNAL(complete()), this->m_thread, SLOT(quit()));
  connect(c, SIGNAL(percentComplete(int)), ui->progressbar,
          SLOT(setValue(int)));
  connect(c, SIGNAL(complete()), c, SLOT(deleteLater()));
  connect(c, SIGNAL(success()), this, SLOT(successDialog()));
  connect(c, SIGNAL(error()), this, SLOT(errorDialog()));
  this->lock();
  this->m_thread->start();
  return;
}

void CrmsDialog::lock() {
  ui->button_browse->setEnabled(false);
  ui->button_process->setEnabled(false);
  ui->button_close->setEnabled(false);
  ui->progressbar->show();
}

void CrmsDialog::unlock() {
  ui->button_browse->setEnabled(true);
  ui->button_process->setEnabled(true);
  ui->button_close->setEnabled(true);
  ui->progressbar->hide();
}

void CrmsDialog::errorDialog() {
  QMessageBox::warning(this, "Warning",
                       "Issues encountered while creating CRMS database");
  this->unlock();
}

void CrmsDialog::successDialog() {
  QMessageBox::information(this, "Success",
                           "CRMS database successfully created");
  this->unlock();
  this->close();
}

void CrmsDialog::closeEvent(QCloseEvent *event) {
  emit dialogClosed();
  if (this->m_thread != nullptr)
    if (this->m_thread->isRunning()) this->m_thread->requestInterruption();
  QDialog::closeEvent(event);
  return;
}
