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
//  File: add_imeds_data.cpp
//
//------------------------------------------------------------------------------

#include "add_imeds_data.h"
#include "ui_add_imeds_data.h"
#include "ADCvalidator.h"

int NumIMEDSFiles = 0;
int CurrentRowsInTable = 0;
bool ColorUpdated;
double UnitConversion, xadjust, yadjust;

QColor RandomButtonColor;
QString InputFileName,InputColorString,InputSeriesName,InputFilePath;

add_imeds_data::add_imeds_data(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::add_imeds_data)
{
    ui->setupUi(this);
    ui->text_unitconvert->setValidator(new QDoubleValidator(this));
    ui->text_xadjust->setValidator(new QDoubleValidator(this));
    ui->text_yadjust->setValidator(new QDoubleValidator(this));
}

add_imeds_data::~add_imeds_data()
{
    delete ui;
}

//Set the default elements with a series name, a blank filename, and
//a randomly generated color
void add_imeds_data::set_default_dialog_box_elements(int NumRowsInTable)
{
    QString ButtonStyle;
    ui->text_seriesname->setText("Series "+QString::number(NumRowsInTable+1));
    ui->text_unitconvert->setText("1.0");
    ui->text_xadjust->setText("0.0");
    ui->text_yadjust->setText("0.0");
    RandomButtonColor = MainWindow::GenerateRandomColor();
    ButtonStyle = MainWindow::MakeColorString(RandomButtonColor);
    ui->button_IMEDSColor->setStyleSheet(ButtonStyle);
    ui->button_IMEDSColor->update();
    return;
}

//When editing an existing row, set the dialog box elements that we're bringing
//up for the user
void add_imeds_data::set_dialog_box_elements(QString Filename, QString Filepath,
                                             QString SeriesName, double UnitConvert,
                                             double xmove, double ymove, QColor Color)
{
    QString ButtonStyle;
    ui->text_seriesname->setText(SeriesName);
    ui->text_filename->setText(Filename);
    ui->text_unitconvert->setText(QString::number(UnitConvert));
    ui->text_xadjust->setText(QString::number(xmove));
    ui->text_yadjust->setText(QString::number(ymove));
    InputFilePath = Filepath;
    ButtonStyle = MainWindow::MakeColorString(Color);
    ui->button_IMEDSColor->setStyleSheet(ButtonStyle);
    ui->button_IMEDSColor->update();
    return;
}

//Bring up the browse for file dialog
void add_imeds_data::on_browse_filebrowse_clicked()
{
    QString TempPath = QFileDialog::getOpenFileName(this,"Select Observation IMEDS File",
                            PreviousDirectory,"IMEDS File (*.imeds *.IMEDS) ;; All Files (*.*)");

    InputFilePath = TempPath;
    if(TempPath!=NULL)
    {
        MainWindow::GetLeadingPath(TempPath);
        QString TempFile = MainWindow::RemoveLeadingPath(TempPath);
        ui->text_filename->setText(TempFile);
    }

    return;
}

//Bring up a color palette and change the button color
//in the dialog when return comes
void add_imeds_data::on_button_IMEDSColor_clicked()
{
    QColor TempColor = QColorDialog::getColor(RandomButtonColor);
    QString ButtonStyle;

    ColorUpdated = false;

    if(TempColor.isValid())
    {
        RandomButtonColor = TempColor;
        ColorUpdated = true;
        ButtonStyle = MainWindow::MakeColorString(RandomButtonColor);
        ui->button_IMEDSColor->setStyleSheet(ButtonStyle);
        ui->button_IMEDSColor->update();
    }

    return;
}

//Set the data values when "ok" is selected
void add_imeds_data::on_buttonBox_accepted()
{
    QString TempString;

    InputFileName = ui->text_filename->text();
    InputColorString = RandomButtonColor.name();
    InputSeriesName = ui->text_seriesname->text();
    TempString = ui->text_unitconvert->text();
    if(TempString==NULL)
        UnitConversion = 1.0;
    else
        UnitConversion = TempString.toDouble();

    TempString = ui->text_xadjust->text();
    if(TempString==NULL)
        xadjust = 0.0;
    else
        xadjust = TempString.toDouble();

    TempString = ui->text_yadjust->text();
    if(TempString==NULL)
        yadjust = 0.0;
    else
        yadjust = TempString.toDouble();

    return;
}
