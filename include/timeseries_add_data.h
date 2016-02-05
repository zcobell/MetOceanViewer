//-------------------------------GPL-------------------------------------//
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
// The name "MetOcean Viewer" is specific to this project and may not be
// used for projects "forked" or derived from this work.
//
//-----------------------------------------------------------------------//

#ifndef TIMESERIES_ADD_DATA_H
#define TIMESERIES_ADD_DATA_H

#include <QDateTime>
#include <QDialog>
#include <QVector>

namespace Ui {
class add_imeds_data;
}

class add_imeds_data : public QDialog
{
    Q_OBJECT

public:
    explicit add_imeds_data(QWidget *parent = 0);

    ~add_imeds_data();

    void set_default_dialog_box_elements(int NumberOfRows);

    void set_dialog_box_elements(QString Filename, QString Filepath, QString SeriesName,
                                 double UnitConvert, double xmove, double ymove, QColor Color,
                                 QDateTime ColdStart, QString FileType, QString StationPath);

    QString PreviousDirectory;

    int NumIMEDSFiles;
    int CurrentRowsInTable;
    bool ColorUpdated, FileReadError;
    bool EditBox;
    double UnitConversion, xadjust, yadjust;
    QColor RandomButtonColor;
    QString InputFileName,InputColorString;
    QString InputSeriesName,InputFilePath;
    QString StationFilePath,InputFileType;
    QString InputStationFile,CurrentFileName;
    QDateTime InputFileColdStart;

private slots:

    void on_browse_filebrowse_clicked();

    void on_button_IMEDSColor_clicked();

    void on_browse_stationfile_clicked();

    void accept();

private:
    Ui::add_imeds_data *ui;

    struct IMEDSList
    {
        QString Filename;
        QString Label;
        QColor  Color;
    };

};

#endif // TIMESERIES_ADD_DATA_H
