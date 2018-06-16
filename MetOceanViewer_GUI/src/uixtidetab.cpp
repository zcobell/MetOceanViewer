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
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "xtide.h"

void MainWindow::on_button_xtide_compute_clicked() {
  this->plotXTideStation();
  return;
}

void MainWindow::plotXTideStation() {

  //...Create an xTide object
  if (!this->m_xtide.isNull()) delete this->m_xtide;
  this->m_xtide =
      new XTide(ui->quick_xtideMap, ui->xtide_graphics, ui->date_xtide_start,
                ui->date_xtide_end, ui->combo_xtide_units, ui->statusBar,
                this->xtideStationModel, &this->xtideSelectedStation, this);
  connect(m_xtide, SIGNAL(xTideError(QString)), this,
          SLOT(throwErrorMessageBox(QString)));

  //...Call the plotting routine
  this->m_xtide->plotXTideStation();

  return;
}

void MainWindow::on_button_xtide_resetzoom_clicked() {
  if (!this->m_xtide.isNull()) ui->xtide_graphics->resetZoom();
  return;
}

void MainWindow::on_button_xtide_savemap_clicked() {
  QString filename;

  QString MarkerID = m_xtide->getLoadedXTideStation();
  QString MarkerID2 = m_xtide->getCurrentXTideStation();

  if (MarkerID == "none") {
    QMessageBox::critical(this, tr("ERROR"),
                          tr("No Station has been selected."));
    return;
  }

  if (MarkerID != MarkerID2) {
    QMessageBox::critical(
        this, tr("ERROR"),
        tr("The currently selected station is not the data loaded."));
    return;
  }

  QString filter = "JPG (*.jpg *.jpeg)";
  QString DefaultFile = "/XTide_" + MarkerID.replace(" ", "_") + ".jpg";
  QString TempString = QFileDialog::getSaveFileName(
      this, tr("Save as..."), previousDirectory + DefaultFile,
      "JPG (*.jpg *.jpeg) ;; PDF (*.pdf)", &filter);

  if (TempString == NULL) return;

  Generic::splitPath(TempString, filename, previousDirectory);

  m_xtide->saveXTidePlot(TempString, filter);

  return;
}

void MainWindow::on_button_xtide_savedata_clicked() {

  if(this->m_xtide==nullptr){
    QMessageBox::critical(this, tr("ERROR"),
                          tr("No station has been calculated."));
    return;
  }

  QString filename;

  QString MarkerID = m_xtide->getLoadedXTideStation();
  QString MarkerID2 = m_xtide->getCurrentXTideStation();

  if (MarkerID == "none") {
    QMessageBox::critical(this, tr("ERROR"),
                          tr("No Station has been selected."));
    return;
  }

  if (MarkerID != MarkerID2) {
    QMessageBox::critical(
        this, tr("ERROR"),
        tr("The currently selected station is not the data loaded."));
    return;
  }

  QString filter;
  QString DefaultFile = "/XTide_" + MarkerID.replace(" ", "_") + ".imeds";

  QString TempString = QFileDialog::getSaveFileName(
      this, tr("Save as..."), previousDirectory + DefaultFile,
      "IMEDS (*.imeds);;CSV (*.csv);;netCDF (*.nc)", &filter);

  QStringList filter2 = filter.split(" ");
  QString format = filter2.value(0);

  if (TempString == NULL) return;

  Generic::splitPath(TempString, filename, previousDirectory);

  m_xtide->saveXTideData(TempString, format);

  return;
}
