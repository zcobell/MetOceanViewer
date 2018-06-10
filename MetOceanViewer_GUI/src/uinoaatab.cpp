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

#include "generic.h"
#include "mainwindow.h"
#include "noaa.h"
#include "timezone.h"
#include "ui_mainwindow.h"

//-------------------------------------------//
// Called when the pan to combo box is updated
//-------------------------------------------//
void MainWindow::on_combo_noaa_maptype_currentIndexChanged(int index) {
  Q_UNUSED(index);
  this->changeNoaaMaptype();
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Called when the user attempts to save the NOAA chart
//-------------------------------------------//
void MainWindow::on_button_noaasavechart_clicked() {
  QString filename;

  int MarkerID = this->thisNOAA->getLoadedNOAAStation();
  int MarkerID2 = this->thisNOAA->getClickedNOAAStation();

  if (MarkerID == -1) {
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
  QString DefaultFile = "/NOAA_" + QString::number(MarkerID) + ".jpg";
  QString TempString = QFileDialog::getSaveFileName(
      this, tr("Save as..."), this->previousDirectory + DefaultFile,
      "JPG (*.jpg *.jpeg) ;; PDF (*.pdf)", &filter);

  if (TempString == NULL) return;

  Generic::splitPath(TempString, filename, this->previousDirectory);

  this->thisNOAA->saveNOAAImage(TempString, filter);

  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Called when the user tries to save the NOAA data
//-------------------------------------------//
void MainWindow::on_button_noaasavedata_clicked() {
  if (this->thisNOAA == nullptr) {
    QMessageBox::critical(this, tr("ERROR"),
                          tr("No station has been downloaded."));
    return;
  };

  QString filename;

  int MarkerID = this->thisNOAA->getLoadedNOAAStation();
  int MarkerID2 = this->thisNOAA->getClickedNOAAStation();

  if (MarkerID == -1) {
    QMessageBox::critical(this, tr("ERROR"),
                          tr("No station has been selected."));
    return;
  }

  if (MarkerID != MarkerID2) {
    QMessageBox::critical(
        this, tr("ERROR"),
        tr("The currently selected station is not the data loaded."));
    return;
  }

  QString filter;
  QString DefaultFile = "/NOAA_" + QString::number(MarkerID) + ".imeds";
  QString TempString = QFileDialog::getSaveFileName(
      this, tr("Save as..."), this->previousDirectory + DefaultFile,
      "IMEDS (*.imeds);;CSV (*.csv);;netCDF (*.nc)", &filter);
  QStringList filter2 = filter.split(" ");
  QString format = filter2.value(0);

  if (TempString == NULL) return;

  Generic::splitPath(TempString, filename, this->previousDirectory);

  thisNOAA->saveNOAAData(filename, this->previousDirectory, format);

  return;
}
//-------------------------------------------//


//-------------------------------------------//
// Called when the NOAA fetch data button is clicked
//-------------------------------------------//
void MainWindow::on_Button_FetchData_clicked() {
  this->plotNOAAStation();
  return;
}
//-------------------------------------------//

void MainWindow::plotNOAAStation() {
  //...Create a new NOAA object
  if (!this->thisNOAA.isNull()) delete this->thisNOAA;
  this->thisNOAA =
      new Noaa(ui->quick_noaaMap, ui->noaa_graphics, ui->Date_StartTime,
               ui->Date_EndTime, ui->combo_NOAAProduct, ui->combo_noaaunits,
               ui->combo_noaadatum, ui->statusBar,
               ui->combo_noaaTimezoneLocation, ui->combo_noaaTimezone,
               this->noaaStationModel, &this->noaaSelectedStation, this);

  connect(this->thisNOAA, SIGNAL(noaaError(QString)), this,
          SLOT(throwErrorMessageBox(QString)));

  this->thisNOAA->plotNOAAStation();
  return;
}

void MainWindow::on_button_noaaresetzoom_clicked() {
  if (!this->thisNOAA.isNull()) ui->noaa_graphics->resetZoom();
  return;
}

void MainWindow::on_combo_noaaTimezoneLocation_currentIndexChanged(int index) {
  TZData::Location l = static_cast<TZData::Location>(index);
  Timezone *t = new Timezone(this);
  QStringList tz = t->getTimezoneAbbreviations(l);
  ui->combo_noaaTimezone->clear();
  ui->combo_noaaTimezone->addItems(tz);
  delete t;
  return;
}

void MainWindow::on_combo_noaaTimezone_currentIndexChanged(
    const QString &arg1) {
  if (this->thisNOAA == nullptr) return;

  Timezone *t = new Timezone(this);
  if (!t->fromAbbreviation(arg1,
                           static_cast<TZData::Location>(
                               ui->combo_noaaTimezoneLocation->currentIndex())))
    return;
  this->thisNOAA->replotChart(t);
  delete t;
  return;
}
