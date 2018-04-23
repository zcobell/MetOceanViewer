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

//
// This file contains all the user interactions that occur on the HWM tab
//

#include "colors.h"
#include "generic.h"
#include "hwm.h"
#include "mainwindow.h"
#include "timeseriesoptionsdialog.h"
#include "ui_mainwindow.h"

//-------------------------------------------//
// Called when the browse for HWM file button
// is clicked
//-------------------------------------------//
void MainWindow::on_browse_hwm_clicked() {
  QString filename;
  QString HighWaterMarkFile = QFileDialog::getOpenFileName(
      this, tr("Select High Water Mark File"), this->previousDirectory,
      tr("High Water Mark File (*.csv) ;; All Files (*.*)"));
  if (HighWaterMarkFile == NULL) return;

  Generic::splitPath(HighWaterMarkFile, filename, this->previousDirectory);
  ui->Text_HWMFile->setText(HighWaterMarkFile);
}
//-------------------------------------------//

//-------------------------------------------//
// Called when the process HWM button is clicked
//-------------------------------------------//
void MainWindow::on_button_processHWM_clicked() {
  int ierr;
  QVector<double> classes;

  QApplication::setOverrideCursor(Qt::WaitCursor);

  classes.resize(7);
  int units = ui->combo_hwmunits->currentIndex();

  if (ui->check_manualHWM->isChecked()) {
    classes[0] = ui->spin_class0->value();
    classes[1] = ui->spin_class1->value();
    classes[2] = ui->spin_class2->value();
    classes[3] = ui->spin_class3->value();
    classes[4] = ui->spin_class4->value();
    classes[5] = ui->spin_class5->value();
    classes[6] = ui->spin_class6->value();
  } else {
    if (units == 1) {
      classes[0] = -1.5;
      classes[1] = -1.0;
      classes[2] = -0.5;
      classes[3] = 0.0;
      classes[4] = 0.5;
      classes[5] = 1.0;
      classes[6] = 1.5;
    } else {
      classes[0] = -5.0;
      classes[1] = -3.5;
      classes[2] = -1.5;
      classes[3] = 0.0;
      classes[4] = 1.5;
      classes[5] = 3.5;
      classes[6] = 5.0;
    }
  }

  if (!thisHWM.isNull()) delete thisHWM;

  thisHWM = new Hwm(
      ui->Text_HWMFile, ui->check_manualHWM, ui->combo_hwmunits,
      ui->check_forceregthroughzero, ui->check_dispupperlowerlines,
      ui->check_regressionColorMatch, ui->button_hwmcolor,
      ui->button_121linecolor, ui->button_boundlinecolor,
      ui->button_reglinecolor, ui->text_adchwmaxislabel,
      ui->text_measuredhwmaxislabel, ui->text_hwmplottitle,
      ui->spin_upperlowervalue, ui->quick_hwmMap, ui->graphics_hwm,
      ui->statusBar, classes, ui->quick_hwmMap, this->hwmMarkerModel, this);

  ierr = thisHWM->processHWMData();

  QApplication::restoreOverrideCursor();

  if (ierr != 0) {
    QMessageBox::critical(this, tr("ERROR"), thisHWM->getErrorString());
    return;
  }

  ui->subtab_hwm->setCurrentIndex(1);

  StationModel::fitMarkers(ui->quick_hwmMap, this->hwmMarkerModel);

  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Called when the manual HWM scale button is
// clicked
//-------------------------------------------//
void MainWindow::on_check_manualHWM_toggled(bool checked) {
  ui->spin_class0->setEnabled(checked);
  ui->spin_class1->setEnabled(checked);
  ui->spin_class2->setEnabled(checked);
  ui->spin_class3->setEnabled(checked);
  ui->spin_class4->setEnabled(checked);
  ui->spin_class5->setEnabled(checked);
  ui->spin_class6->setEnabled(checked);
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Called when the save HWM map image button
// is clicked
//-------------------------------------------//
void MainWindow::on_button_saveHWMMap_clicked() {
  QString filename;
  QString filter = "JPG (*.jpg *.jpeg)";
  QString TempString = QFileDialog::getSaveFileName(
      this, tr("Save as..."), this->previousDirectory,
      "JPG (*.jpg *.jpeg) ;; PDF (*.pdf)", &filter);

  if (TempString == NULL) return;

  Generic::splitPath(TempString, filename, this->previousDirectory);

  QApplication::setOverrideCursor(Qt::WaitCursor);
  thisHWM->saveHWMMap(TempString, filter);
  QApplication::restoreOverrideCursor();

  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Bring up a color dialog to choose the color
// used for plotting high water marks
//-------------------------------------------//
void MainWindow::on_button_hwmcolor_clicked() {
  QString ButtonStyle;
  QColor TempColor;

  TempColor = QColorDialog::getColor(DotColorHWM);
  if (TempColor.isValid())
    DotColorHWM = TempColor;
  else
    return;

  ButtonStyle = Colors::MakeColorString(DotColorHWM);
  ui->button_hwmcolor->setStyleSheet(ButtonStyle);
  ui->button_hwmcolor->update();
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Bring up a color dialog box for choosing the
// 1:1 color
//-------------------------------------------//
void MainWindow::on_button_121linecolor_clicked() {
  QString ButtonStyle;
  QColor TempColor;

  TempColor = QColorDialog::getColor(LineColor121Line);
  if (TempColor.isValid())
    LineColor121Line = TempColor;
  else
    return;

  ButtonStyle = Colors::MakeColorString(LineColor121Line);
  ui->button_121linecolor->setStyleSheet(ButtonStyle);
  ui->button_121linecolor->update();
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Bring up a color dialog for choosing the
// regression line color
//-------------------------------------------//
void MainWindow::on_button_reglinecolor_clicked() {
  QString ButtonStyle;
  QColor TempColor;

  TempColor = QColorDialog::getColor(LineColorRegression);
  if (TempColor.isValid())
    LineColorRegression = TempColor;
  else
    return;

  ButtonStyle = Colors::MakeColorString(LineColorRegression);
  ui->button_reglinecolor->setStyleSheet(ButtonStyle);
  ui->button_reglinecolor->update();
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Bring up a color dialog to choose the
// standard deviation bounding line color
//-------------------------------------------//
void MainWindow::on_button_boundlinecolor_clicked() {
  QString ButtonStyle;
  QColor TempColor;

  TempColor = QColorDialog::getColor(LineColorBounds);
  if (TempColor.isValid())
    LineColorBounds = TempColor;
  else
    return;

  ButtonStyle = Colors::MakeColorString(LineColorBounds);
  ui->button_boundlinecolor->setStyleSheet(ButtonStyle);
  ui->button_boundlinecolor->update();
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Button click routine for fitting high water
// marks to the current view
//-------------------------------------------//
void MainWindow::on_button_fitHWM_clicked() {
  StationModel::fitMarkers(ui->quick_hwmMap, this->hwmMarkerModel);
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Button to toggle options when the color match
// option for the linear regression is clicked
//-------------------------------------------//
void MainWindow::on_check_regressionColorMatch_clicked(bool checked) {
  ui->button_hwmcolor->setEnabled(!checked);
  ui->label_hwmcolor->setEnabled(!checked);
  return;
}
//-------------------------------------------//

void MainWindow::on_button_saveHWMScatter_clicked() {
  QString filename;
  QString filter = "JPG (*.jpg *.jpeg)";
  QString TempString = QFileDialog::getSaveFileName(
      this, tr("Save as..."), this->previousDirectory,
      "JPG (*.jpg *.jpeg) ;; PDF (*.pdf)", &filter);

  if (TempString == NULL) return;

  Generic::splitPath(TempString, filename, this->previousDirectory);

  QApplication::setOverrideCursor(Qt::WaitCursor);
  thisHWM->saveRegressionPlot(TempString, filter);
  QApplication::restoreOverrideCursor();

  return;
}

void MainWindow::on_button_hwmResetZoom_clicked() {
  if (!this->thisHWM.isNull()) ui->graphics_hwm->resetZoom();
  return;
}

void MainWindow::on_button_hwmOptions_clicked() {
  TimeseriesOptionsDialog *optionsWindow = new TimeseriesOptionsDialog(this);
  optionsWindow->setDisplayValues(this->hwmDisplayValues);

  int ierr = optionsWindow->exec();

  if (ierr == QDialog::Accepted) {
    this->hwmDisplayValues = optionsWindow->displayValues();
    ui->graphics_hwm->setDisplayValues(this->hwmDisplayValues);
  }
  return;
}
