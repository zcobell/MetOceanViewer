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

#include "mainwindow.h"
#include "timezone.h"
#include "ui_mainwindow.h"
#include "usgs.h"

//-------------------------------------------//
// Send the data to the panTo function
//-------------------------------------------//
void MainWindow::on_combo_usgs_maptype_currentIndexChanged(int index) {
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
  //...Create a new USGS object
  if (m_usgs != nullptr) delete m_usgs;
  this->m_usgs = new Usgs(
      ui->quick_usgsMap, ui->usgs_graphics, ui->radio_usgsDaily,
      ui->radio_usgshistoric, ui->radio_usgs_instant, ui->combo_USGSProduct,
      ui->Date_usgsStart, ui->Date_usgsEnd, ui->statusBar,
      ui->combo_usgsTimezoneLocation, ui->combo_usgsTimezone,
      this->usgsStationModel, &this->usgsSelectedStation, this);
  connect(this->m_usgs, SIGNAL(usgsError(QString)), this,
          SLOT(throwErrorMessageBox(QString)));

  this->m_usgs->plotNewUSGSStation();

  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Sets the data range when the usgs instant
// radio button is clicked since the "instant"
// data is only available 120 days into the past
//-------------------------------------------//
void MainWindow::on_radio_usgs_instant_clicked() {
  if (m_usgs != nullptr) m_usgs->setUSGSBeenPlotted(false);
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
  if (this->m_usgs != nullptr) m_usgs->setUSGSBeenPlotted(false);
  ui->Date_usgsStart->setMinimumDateTime(QDateTime(QDate(1900, 1, 1),QTime(0,0,0)));
  ui->Date_usgsEnd->setMinimumDateTime(QDateTime(QDate(1900, 1, 1),QTime(0,0,0)));
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Resets the minimum datetime to a long time
// ago when using daily data from USGS
//-------------------------------------------//
void MainWindow::on_radio_usgshistoric_clicked() {
  if (this->m_usgs != nullptr) m_usgs->setUSGSBeenPlotted(false);
  ui->Date_usgsStart->setMinimumDateTime(QDateTime(QDate(1900, 1, 1),QTime(0,0,0)));
  ui->Date_usgsEnd->setMinimumDateTime(QDateTime(QDate(1900, 1, 1),QTime(0,0,0)));
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Fires when the combo box is changed and
// plots the data immediately
//-------------------------------------------//
void MainWindow::on_combo_USGSProduct_currentIndexChanged(int index) {
  QApplication::setOverrideCursor(Qt::WaitCursor);
  this->m_usgs->replotCurrentUSGSStation(index);
  QApplication::restoreOverrideCursor();
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Function to save the map and chart as a jpg
//-------------------------------------------//
void MainWindow::on_button_usgssavemap_clicked() {
  if (this->m_usgs == nullptr) {
    QMessageBox::critical(this, tr("ERROR"),
                          tr("No station has been downloaded."));
    return;
  }

  QString filename;

  QString MarkerID = m_usgs->getLoadedUSGSStation();
  QString MarkerID2 = m_usgs->getClickedUSGSStation();

  if (MarkerID == "none") {
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

  if (!m_usgs->getUSGSBeenPlotted()) {
    QMessageBox::critical(this, tr("ERROR"),
                          tr("Plot the data before attempting to save."));
    return;
  }

  QString filter = "JPG (*.jpg *.jpeg)";
  QString DefaultFile = "/USGS_" + MarkerID + ".jpg";
  QString TempString = QFileDialog::getSaveFileName(
      this, tr("Save as..."), previousDirectory + DefaultFile,
      "JPG (*.jpg *.jpeg) ;; PDF (*.pdf)", &filter);

  if (TempString == nullptr) return;

  Generic::splitPath(TempString, filename, previousDirectory);

  this->m_usgs->saveUSGSImage(TempString, filter);

  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Saves the USGS data as an IMEDS formatted file
// or a CSV
//-------------------------------------------//
void MainWindow::on_button_usgssavedata_clicked() {
  if (this->m_usgs == nullptr) {
    return;
  }

  QString filename;

  QString MarkerID = m_usgs->getLoadedUSGSStation();
  QString MarkerID2 = m_usgs->getClickedUSGSStation();

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

  if (!m_usgs->getUSGSBeenPlotted()) {
    QMessageBox::critical(this, tr("ERROR"),
                          tr("Plot the data before attempting to save."));
    return;
  }

  QString filter;
  QString DefaultFile = "/USGS_" + MarkerID + ".imeds";

  QString TempString = QFileDialog::getSaveFileName(
      this, tr("Save as..."), this->previousDirectory + DefaultFile,
      "IMEDS (*.imeds);;CSV (*.csv);;netCDF (*.nc)", &filter);

  QStringList filter2 = filter.split(" ");

  if (TempString == nullptr) return;

  Generic::splitPath(TempString, filename, this->previousDirectory);

  this->m_usgs->saveUSGSData(TempString);

  return;
}
//-------------------------------------------//

void MainWindow::on_button_usgsresetzoom_clicked() {
  if (this->m_usgs != nullptr) ui->usgs_graphics->resetZoom();
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
  if (this->m_usgs == nullptr) return;

  Timezone *t = new Timezone(this);
  if (!t->fromAbbreviation(arg1,
                           static_cast<TZData::Location>(
                               ui->combo_usgsTimezoneLocation->currentIndex())))
    return;
  this->m_usgs->replotChart(t);
  delete t;
  return;
}
