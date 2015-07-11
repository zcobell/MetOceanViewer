//----GPL-----------------------------------------------------------------------
//
// This file is part of MetOceanViewer.
//
//    MetOceanViewer is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    MetOceanViewer is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with MetOceanViewer.  If not, see <http://www.gnu.org/licenses/>.
//------------------------------------------------------------------------------

#ifndef TIMESERIES_ADD_DATA_H
#define TIMESERIES_ADD_DATA_H

#include <QDialog>
#include <QVector>

//-------------------------------------------//
//Data structure used for a list of files
//for time series plotting
//-------------------------------------------//
struct IMEDSList
{
    QString Filename;
    QString Label;
    QColor  Color;
};
//-------------------------------------------//


//-------------------------------------------//
//Some variables that will be used throughout
//-------------------------------------------//
extern int NumIMEDSFiles;
extern int CurrentRowsIntTable;
extern QString InputFileName,InputStationFile;
extern QString InputColorString,InputSeriesName;
extern QString InputFilePath,StationFilePath,InputFileType;
extern double UnitConversion,xadjust,yadjust;
extern QDateTime InputFileColdStart;
//-------------------------------------------//


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

private slots:

    void on_browse_filebrowse_clicked();

    void on_button_IMEDSColor_clicked();

    void on_browse_stationfile_clicked();

    void accept();

private:
    Ui::add_imeds_data *ui;

};

#endif // TIMESERIES_ADD_DATA_H
