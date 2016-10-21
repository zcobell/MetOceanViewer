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
#ifndef MOV_SESSION_H
#define MOV_SESSION_H

#include <QObject>
#include <netcdf.h>
#include <QtWidgets>
#include <QFileDialog>

class mov_session : public QObject
{

    Q_OBJECT

public:
    explicit mov_session(QTableWidget *inTableWidget,
                         QLineEdit *inPlotTitleWidget,
                         QLineEdit *inXLabelWidget,
                         QLineEdit *inYLabelWidget,
                         QDateEdit *inStartDateEdit,
                         QDateEdit *inEndDateEdit,
                         QDoubleSpinBox *inYMinSpinBox,
                         QDoubleSpinBox *inYMaxSpinBox,
                         QCheckBox *incheckAllData,
                         QCheckBox *inCheckYAuto,
                         QString &inPreviousDirectory,
                         QObject *parent = 0);

    int save();
    int open(QString openFilename);
    int setSessionFilename(QString filename);
    QString getSessionFilename();

signals:
    void sessionError(QString);

private:
    QTableWidget   *tableWidget;
    QLineEdit      *plotTitleWidget;
    QLineEdit      *xLabelWidget;
    QLineEdit      *yLabelWidget;
    QDateEdit      *startDateEdit;
    QDateEdit      *endDateEdit;
    QDoubleSpinBox *yMinSpinBox;
    QDoubleSpinBox *yMaxSpinBox;
    QCheckBox      *checkAllData;
    QCheckBox      *checkYAuto;
    QString         previousDirectory;
    QString         currentDirectory;
    QString         sessionFileName;
    QString         alternateFolder;

};

#endif // MOV_SESSION_H
