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
#include "crms.h"
#include "generic.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

void MainWindow::on_button_fetchcrms_clicked() {
  if (this->m_crms != nullptr) delete this->m_crms;
  this->m_crms =
      new Crms(ui->quick_crmsMap, ui->crms_graphics, ui->date_crmsStarttime,
               ui->date_crmsEndtime, ui->combo_crmsproduct, ui->statusBar,
               this->crmsStationModel, &this->crmsSelectedStations,
               this->crmsHeader, this->crmsMapping, this);
  connect(this->m_crms, SIGNAL(error(QString)), this,
          SLOT(throwErrorMessageBox(QString)));

  this->m_crms->plotStation();

  return;
}

void MainWindow::on_combo_crmsproduct_currentIndexChanged(int index) {
  this->m_crms->replot(index);
}

void MainWindow::on_button_crms_savechart_clicked() {
  QString filename;

  QString markerID = this->m_crms->getLoadedStation();
  QString markerID2 = this->m_crms->getCurrentStation();

  if (markerID == "-1") {
    QMessageBox::critical(this, tr("ERROR"),
                          tr("No Station has been selected."));
    return;
  }

  if (markerID != markerID2) {
    QMessageBox::critical(
        this, tr("ERROR"),
        tr("The currently selected station is not the data loaded."));
    return;
  }

  QString filter = "JPG (*.jpg *.jpeg)";
  QString name = this->crmsStationModel->findStation(markerID).name();
  QString DefaultFile = "/" + name + ".jpg";
  QString TempString = QFileDialog::getSaveFileName(
      this, tr("Save as..."), this->previousDirectory + DefaultFile,
      "JPG (*.jpg *.jpeg) ;; PDF (*.pdf)", &filter);

  if (TempString == nullptr) return;

  Generic::splitPath(TempString, filename, this->previousDirectory);

  this->m_crms->savePlot(TempString, filter);

  return;
}

void MainWindow::on_button_crms_savedata_clicked() {
  if (this->m_crms == nullptr) {
    QMessageBox::critical(this, tr("ERROR"),
                          tr("No station has been queried."));
    return;
  }

  QString filename;

  QString markerID = this->m_crms->getLoadedStation();
  QString markerID2 = this->m_crms->getCurrentStation();

  if (markerID == "none") {
    QMessageBox::critical(this, tr("ERROR"),
                          tr("No Station has been selected."));
    return;
  }

  if (markerID != markerID2) {
    QMessageBox::critical(
        this, tr("ERROR"),
        tr("The currently selected station is not the data loaded."));
    return;
  }

  QString filter;
  QString name = this->crmsStationModel->findStation(markerID).name();
  QString defaultFile = "/" + name.replace(" ", "_") + ".imeds";

  QString TempString = QFileDialog::getSaveFileName(
      this, tr("Save as..."), this->previousDirectory + defaultFile,
      "IMEDS (*.imeds);;CSV (*.csv);;netCDF (*.nc)", &filter);

  QStringList filter2 = filter.split(" ");
  QString format = filter2.value(0);

  if (TempString == QString()) return;

  Generic::splitPath(TempString, filename, this->previousDirectory);

  this->m_crms->saveData(TempString, format);
}

void MainWindow::on_button_crms_resetzoom_clicked() {
  if (this->m_crms != nullptr) ui->crms_graphics->resetZoom();
  return;
}

void MainWindow::on_button_crmsDisplayValues_toggled(bool checked) {
  this->m_crms->chartview()->setDisplayValues(checked);
}
