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

#ifndef MOV_DIALOG_ADDTIMESERIES_H
#define MOV_DIALOG_ADDTIMESERIES_H

#include <QDateTime>
#include <QDialog>
#include <QVector>
#include "proj4.h"

namespace Ui {
class mov_dialog_addtimeseries;
}

class mov_dialog_addtimeseries : public QDialog
{
    Q_OBJECT

public:
    explicit mov_dialog_addtimeseries(QWidget *parent = 0);

    ~mov_dialog_addtimeseries();

    void set_default_dialog_box_elements(int NumberOfRows);

    void set_dialog_box_elements(QString Filename, QString Filepath, QString SeriesName,
                                 double UnitConvert, double xmove, double ymove, QColor Color,
                                 QDateTime ColdStart, int FileType, QString StationPath, int epsg);

    QString PreviousDirectory;

    int NumIMEDSFiles;
    int CurrentRowsInTable;
    bool ColorUpdated, FileReadError;
    bool EditBox;
    double UnitConversion, xadjust, yadjust;
    QColor RandomButtonColor;
    QString InputFileName,InputColorString;
    QString InputSeriesName,InputFilePath;
    QString StationFilePath;
    QString InputStationFile,CurrentFileName;
    QDateTime InputFileColdStart;
    int InputFileType;
    int epsg;
    proj4 *proj;
    QMap<int,std::string> *epsgmap;

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

private:
    Ui::mov_dialog_addtimeseries *ui;

    struct IMEDSList
    {
        QString Filename;
        QString Label;
        QColor  Color;
    };

};

#endif // MOV_DIALOG_ADDTIMESERIES_H
