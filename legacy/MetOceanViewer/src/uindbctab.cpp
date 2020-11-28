/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2019  Zach Cobell
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
#include "generic.h"
#include "mainwindow.h"
#include "ndbc.h"
#include "timezone.h"
#include "ui_mainwindow.h"

void MainWindow::on_button_fetchndbc_clicked() {
  if (this->m_ndbc != nullptr) delete this->m_ndbc;
  this->m_ndbc =
      new Ndbc(ui->quick_ndbcMap, ui->ndbc_graphics, ui->combo_ndbcproduct,
               ui->date_ndbcStarttime, ui->date_ndbcEndtime, ui->statusBar,
               this->ndbcStationModel, &this->ndbcSelectedStation, this);

  connect(this->m_ndbc, SIGNAL(ndbcError(QString)), this,
          SLOT(throwErrorMessageBox(QString)));

  this->m_ndbc->plotStation();

  return;
}

void MainWindow::on_combo_ndbcproduct_currentIndexChanged(int index) {
  if (this->m_ndbc != nullptr) {
    if (this->m_ndbc->dataReady()) this->m_ndbc->replotStation(index);
  }
}

void MainWindow::on_button_ndbc_resetzoom_clicked() {
  if (this->m_ndbc != nullptr) {
    ui->ndbc_graphics->resetZoom();
  }
}

void MainWindow::on_button_ndbcDisplayValues_toggled(bool checked) {
  if (this->m_ndbc != nullptr) {
    ui->ndbc_graphics->setDisplayValues(checked);
  }
}

void MainWindow::on_button_ndbc_savechart_clicked() {
  QString filename;
  QString filter = "JPG (*.jpg *.jpeg)";
  QString defaultFile = "/NDBC_" + this->m_ndbc->getSelectedMarker() + ".jpg";
  QString tempString = QFileDialog::getSaveFileName(
      this, tr("Save as..."), this->previousDirectory + defaultFile,
      "JPG (*.jpg *.jpeg) ;; PDF (*.pdf)");

  if (tempString == QString()) return;

  Generic::splitPath(tempString, filename, this->previousDirectory);

  if (this->m_ndbc != nullptr) {
    this->m_ndbc->saveImage(tempString, filter);
  }

  return;
}

void MainWindow::on_button_ndbc_savedata_clicked() {
  QString filename;
  QString product = ui->combo_ndbcproduct->currentText();
  product.replace(" ", "_");
  QString defaultFile =
      "/NDBC_" + this->m_ndbc->getSelectedMarker() + "_" + product + ".imeds";

  QString filter = "IMEDS (*.imeds)";
  QString TempString = QFileDialog::getSaveFileName(
      this, tr("Save as..."), this->previousDirectory + defaultFile,
      "IMEDS (*.imeds);;CSV (*.csv);;netCDF (*.nc)", &filter);

  if (TempString == QString()) return;

  Generic::splitPath(TempString, filename, this->previousDirectory);

  if (this->m_ndbc != nullptr) {
    this->m_ndbc->saveData(TempString);
  }
  return;
}
