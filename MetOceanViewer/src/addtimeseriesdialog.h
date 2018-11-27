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

#ifndef ADDTIMESERIESDIALOG_H
#define ADDTIMESERIESDIALOG_H

#include <QDateTime>
#include <QDialog>
#include <QVector>
#include <memory>
#include "dflow.h"
#include "ezproj.h"

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

  QString previousDirectory() const;
  void setPreviousDirectory(const QString &previousDirectory);

  bool fileReadError() const;
  void setFileReadError(bool fileReadError);

  bool editBox() const;
  void setEditBox(bool editBox);

  double unitConversion() const;
  void setUnitConversion(double unitConversion);

  double xadjust() const;
  void setXadjust(double xadjust);

  double yadjust() const;
  void setYadjust(double yadjust);

  QColor randomButtonColor() const;
  void setRandomButtonColor(const QColor &randomButtonColor);

  QString inputFileName() const;
  void setInputFileName(const QString &inputFileName);

  QString inputColorString() const;
  void setInputColorString(const QString &inputColorString);

  QString inputSeriesName() const;
  void setInputSeriesName(const QString &inputSeriesName);

  QString inputFilePath() const;
  void setInputFilePath(const QString &inputFilePath);

  QString stationFilePath() const;
  void setStationFilePath(const QString &stationFilePath);

  QString dflowVariable() const;
  void setDflowVariable(const QString &dflowVariable);

  QString inputStationFile() const;
  void setInputStationFile(const QString &inputStationFile);

  QString currentFileName() const;
  void setCurrentFileName(const QString &currentFileName);

  QDateTime inputFileColdstart() const;
  void setInputFileColdstart(const QDateTime &inputFileColdstart);

  int inputFileType() const;
  void setInputFileType(int inputFileType);

  int epsg() const;
  void setEpsg(int epsg);

  int layer() const;
  void setLayer(int layer);

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

  QString m_previousDirectory;
  bool m_fileReadError;
  bool m_editBox;
  double m_unitConversion, m_xadjust, m_yadjust;
  QColor m_randomButtonColor;
  QString m_inputFileName, m_inputColorString;
  QString m_inputSeriesName, m_inputFilePath;
  QString m_stationFilePath, m_dflowVariable;
  QString m_inputStationFile, m_currentFileName;
  QDateTime m_inputFileColdstart;
  int m_inputFileType;
  int m_epsg, m_layer;

  Dflow *dflow;
  std::unique_ptr<Ezproj> proj;

};

#endif // ADDTIMESERIESDIALOG_H
