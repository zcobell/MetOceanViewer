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
#include "crmsdialog.h"
#include <QFileDialog>
#include <QMessageBox>
#include "generic.h"
#include "ui_crmsdialog.h"

CrmsDialog::CrmsDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::CrmsDialog) {
  ui->setupUi(this);

  QString crmsInstructions =
      "<html><head/><body><p>You can manually convert the CIMS database to "
      "netCDF format for MetOceanViewer using the executable distributed with "
      "this code called BuildCrmsDatabase. "
      "However, the process is completed automatically and a publicly accessable "
      "database is updated every Monday morning"
      " <a href=\"https://metoceanviewer.s3.amazonaws.com/crms.nc\">"
      "here</a>.</p>"
      "<p>Place the downloaded file here: " +
      Generic::crmsDataFile() + "</p></body></html>";

  ui->label_crmsInstructions->setTextFormat(Qt::RichText);
  ui->label_crmsInstructions->setText(crmsInstructions);
  ui->label_crmsInstructions->setTextInteractionFlags(
      Qt::TextBrowserInteraction);
  ui->label_crmsInstructions->setOpenExternalLinks(true);
}

CrmsDialog::~CrmsDialog() { delete ui; }
