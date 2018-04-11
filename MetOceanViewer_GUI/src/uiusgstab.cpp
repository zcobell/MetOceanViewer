/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2015  Zach Cobell
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
#include "usgs.h"
#include "timeseriesoptionsdialog.h"
#include "timezone.h"
#include "ui_mov_window_main.h"

//-------------------------------------------//
// Send the data to the panTo function
//-------------------------------------------//
void MainWindow::on_combo_usgs_panto_currentIndexChanged(int index) {
  Q_UNUSED(index);
  this->changeUsgsMaptype();
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Function to fetch the data from the USGS
// server
//-------------------------------------------//
void MainWindow::on_button_usgs_fetch_clicked() {
  int ierr;

  //...Create a new USGS object
  if (!thisUSGS.isNull()) delete thisUSGS;
  thisUSGS = new Usgs(
      ui->quick_usgsMap, ui->usgs_graphics, ui->radio_usgsDaily,
      ui->radio_usgshistoric, ui->radio_usgs_instant, ui->combo_USGSProduct,
      ui->Date_usgsStart, ui->Date_usgsEnd, ui->statusBar,
      ui->combo_usgsTimezoneLocation, ui->combo_usgsTimezone, this);
  connect(thisUSGS, SIGNAL(usgsError(QString)), this,
          SLOT(throwErrorMessageBox(QString)));

  ierr = thisUSGS->plotNewUSGSStation();

  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Sets the data range when the usgs instant
// radio button is clicked since the "instant"
// data is only available 120 days into the past
//-------------------------------------------//
void MainWindow::on_radio_usgs_instant_clicked() {
  if (!thisUSGS.isNull()) thisUSGS->setUSGSBeenPlotted(false);
  ui->Date_usgsStart->setMinimumDateTime(
      QDateTime::currentDateTime().addDays(-120));
  ui->Date_usgsEnd->setMinimumDateTime(
      QDateTime::currentDateTime().addDays(-120));

  if (ui->Date_usgsStart->dateTime() <
      QDateTime::currentDateTime().addDays(-120))
    ui->Date_usgsStart->setDateTime(QDateTime::currentDateTime().addDays(-120));

  if (ui->Date_usgsEnd->dateTime() < QDateTime::currentDateTime().addDays(-120))
    ui->Date_usgsEnd->setDateTime(QDateTime::currentDateTime().addDays(-120));

  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Resets the minimum datetime to a long time
// ago when using daily data from USGS
//-------------------------------------------//
void MainWindow::on_radio_usgsDaily_clicked() {
  if (!thisUSGS.isNull()) thisUSGS->setUSGSBeenPlotted(false);
  ui->Date_usgsStart->setMinimumDateTime(QDateTime(QDate(1900, 1, 1)));
  ui->Date_usgsEnd->setMinimumDateTime(QDateTime(QDate(1900, 1, 1)));
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Resets the minimum datetime to a long time
// ago when using daily data from USGS
//-------------------------------------------//
void MainWindow::on_radio_usgshistoric_clicked() {
  if (!thisUSGS.isNull()) thisUSGS->setUSGSBeenPlotted(false);
  ui->Date_usgsStart->setMinimumDateTime(QDateTime(QDate(1900, 1, 1)));
  ui->Date_usgsEnd->setMinimumDateTime(QDateTime(QDate(1900, 1, 1)));
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Fires when the combo box is changed and
// plots the data immediately
//-------------------------------------------//
void MainWindow::on_combo_USGSProduct_currentIndexChanged(int index) {
  int ierr;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  ierr = thisUSGS->replotCurrentUSGSStation(index);
  QApplication::restoreOverrideCursor();
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Function to save the map and chart as a jpg
//-------------------------------------------//
void MainWindow::on_button_usgssavemap_clicked() {
  QString filename;

  QString MarkerID = thisUSGS->getLoadedUSGSStation();
  QString MarkerID2 = thisUSGS->getClickedUSGSStation();

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

  if (!thisUSGS->getUSGSBeenPlotted()) {
    QMessageBox::critical(this, tr("ERROR"),
                          tr("Plot the data before attempting to save."));
    return;
  }

  QString filter = "JPG (*.jpg *.jpeg)";
  QString DefaultFile = "/USGS_" + MarkerID + ".jpg";
  QString TempString = QFileDialog::getSaveFileName(
      this, tr("Save as..."), PreviousDirectory + DefaultFile,
      "JPG (*.jpg *.jpeg) ;; PDF (*.pdf)", &filter);

  if (TempString == NULL) return;

  Generic::splitPath(TempString, filename, PreviousDirectory);

  thisUSGS->saveUSGSImage(TempString, filter);

  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Saves the USGS data as an IMEDS formatted file
// or a CSV
//-------------------------------------------//
void MainWindow::on_button_usgssavedata_clicked() {
  QString filename;

  QString MarkerID = thisUSGS->getLoadedUSGSStation();
  QString MarkerID2 = thisUSGS->getClickedUSGSStation();

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

  if (!thisUSGS->getUSGSBeenPlotted()) {
    QMessageBox::critical(this, tr("ERROR"),
                          tr("Plot the data before attempting to save."));
    return;
  }

  QString filter;
  QString DefaultFile = "/USGS_" + MarkerID + ".imeds";

  QString TempString = QFileDialog::getSaveFileName(
      this, tr("Save as..."), PreviousDirectory + DefaultFile,
      "IMEDS (*.imeds);;CSV (*.csv)", &filter);

  QStringList filter2 = filter.split(" ");
  QString format = filter2.value(0);

  if (TempString == NULL) return;

  Generic::splitPath(TempString, filename, PreviousDirectory);

  thisUSGS->saveUSGSData(TempString, format);

  return;
}
//-------------------------------------------//

void MainWindow::on_button_usgsresetzoom_clicked() {
  if (!this->thisUSGS.isNull()) ui->usgs_graphics->resetZoom();
  return;
}

void MainWindow::on_button_usgsOptions_clicked() {
  TimeseriesOptionsDialog *optionsWindow = new TimeseriesOptionsDialog(this);
  optionsWindow->setDisplayValues(this->usgsDisplayValues);
  optionsWindow->setShowHideInfoWindowOption(false);

  int ierr = optionsWindow->exec();

  if (ierr == QDialog::Accepted) {
    this->usgsDisplayValues = optionsWindow->displayValues();
    ui->usgs_graphics->setDisplayValues(this->usgsDisplayValues);
  }
  return;
}

void MainWindow::on_combo_usgsTimezoneLocation_currentIndexChanged(int index) {
  TZData::Location l = static_cast<TZData::Location>(index);
  Timezone *t = new Timezone(this);
  QStringList tz = t->getTimezoneAbbreviations(l);
  ui->combo_usgsTimezone->clear();
  ui->combo_usgsTimezone->addItems(tz);
  delete t;
  return;
}

void MainWindow::on_combo_usgsTimezone_currentIndexChanged(
    const QString &arg1) {
  if (this->thisUSGS == nullptr) return;

  Timezone *t = new Timezone(this);
  if (!t->fromAbbreviation(arg1,
                           static_cast<TZData::Location>(
                               ui->combo_usgsTimezoneLocation->currentIndex())))
    return;
  this->thisUSGS->replotChart(t);
  delete t;
  return;
}
