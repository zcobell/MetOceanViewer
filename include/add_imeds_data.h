//---- LGPL --------------------------------------------------------------------
//
// Copyright (C)  ARCADIS, 2011-2013.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// contact: Zachary Cobell, zachary.cobell@arcadis-us.com
// ARCADIS
// 4999 Pearl East Circle, Suite 200
// Boulder, CO 80301
//
// All indications and logos of, and references to, "ARCADIS"
// are registered trademarks of ARCADIS, and remain the property of
// ARCADIS. All rights reserved.
//
//------------------------------------------------------------------------------
// $Author$
// $Date$
// $Id$
// $HeadURL$
//------------------------------------------------------------------------------
//  File: add_imeds_data.h
//
//------------------------------------------------------------------------------

#ifndef ADD_IMEDS_DATA_H
#define ADD_IMEDS_DATA_H

#include <QDialog>
#include <QVector>

struct IMEDSList
{
    QString Filename;
    QString Label;
    QColor  Color;
};

extern int NumIMEDSFiles;
extern int CurrentRowsIntTable;
extern QString InputFileName,InputColorString,InputSeriesName,InputFilePath,InputFileType;
extern double UnitConversion,xadjust,yadjust;
extern QDateTime InputFileColdStart;


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
                                 QDateTime ColdStart, QString FileType);

private slots:

    void on_browse_filebrowse_clicked();

    void on_button_IMEDSColor_clicked();

    void on_buttonBox_accepted();

private:
    Ui::add_imeds_data *ui;

};

#endif // ADD_IMEDS_DATA_H
