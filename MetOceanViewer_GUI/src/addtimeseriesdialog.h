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

#ifndef ADDTIMESERIESDIALOG_H
#define ADDTIMESERIESDIALOG_H

#include <QDateTime>
#include <QDialog>
#include <QVector>
#include "dflow.h"
#include "proj4.h"

namespace Ui {
class AddTimeseriesDialog;
}

class AddTimeseriesDialog : public QDialog {
  Q_OBJECT

 public:
  explicit AddTimeseriesDialog(QWidget *parent = nullptr);

  ~AddTimeseriesDialog();

  void set_default_dialog_box_elements(int NumberOfRows);

  void set_dialog_box_elements(QString Filename, QString Filepath,
                               QString SeriesName, double UnitConvert,
                               double xmove, double ymove, QColor Color,
                               QDateTime ColdStart, int FileType,
                               QString StationPath, int epsg, QString varname,
                               int layer);

  QString PreviousDirectory;

  int NumIMEDSFiles;
  int CurrentRowsInTable;
  bool ColorUpdated, FileReadError;
  bool EditBox;
  double UnitConversion, xadjust, yadjust;
  QColor RandomButtonColor;
  QString InputFileName, InputColorString;
  QString InputSeriesName, InputFilePath;
  QString StationFilePath, dFlowVariable;
  QString InputStationFile, CurrentFileName;
  QDateTime InputFileColdStart;
  int InputFileType;
  int epsg, layer;

 private slots:

  void on_browse_filebrowse_clicked();

  void on_button_seriesColor_clicked();

  void on_browse_stationfile_clicked();

  void accept();

  void on_button_presetColor1_clicked();

  void on_button_presetColor2_clicked();

  void on_button_presetColor3_clicked();

  void on_button_presetColor4_clicked();

  void on_button_describeepsg_clicked();

  void on_spin_epsg_valueChanged(int arg1);

  void on_combo_variableSelect_currentIndexChanged(const QString &arg1);

 signals:
  void addTimeseriesError(QString);

 private slots:
  void throwErrorMessageBox(QString);

 private:
  Ui::AddTimeseriesDialog *ui;

  void setVariableSelectElements(bool enabled);
  void setStationSelectElements(bool enabled);
  void setColdstartSelectElements(bool enabled);
  void setVerticalLayerElements(bool enabled);
  void setItemsByFiletype();

  Dflow *dflow;
  proj4 *proj;

  struct IMEDSList {
    QString Filename;
    QString Label;
    QColor Color;
  };
};

#endif // ADDTIMESERIESDIALOG_H