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

//
//This file contains all the user interactions that occur on the HWM tab
//

#include "MetOceanViewer.h"
#include "ui_MetOceanViewer_main.h"
#include "hwm_orig.h"

//Global variables
QVector<hwm_data> HighWaterMarks;
QString HighWaterMarkFile;
QVector<double> classes;


//-------------------------------------------//
//Called when the browse for HWM file button
//is clicked
//-------------------------------------------//
void MainWindow::on_browse_hwm_clicked()
{
    QString filename;
    HighWaterMarkFile = QFileDialog::getOpenFileName(this,"Select High Water Mark File",
                                    this->PreviousDirectory,
                                    "Shintaro Style High Water Mark File (*.csv) ;; All Files (*.*)");
    if(HighWaterMarkFile==NULL)
        return;

    splitPath(HighWaterMarkFile,filename,this->PreviousDirectory);
    ui->Text_HWMFile->setText(HighWaterMarkFile);
}
//-------------------------------------------//


//-------------------------------------------//
//Called when the process HWM button is clicked
//-------------------------------------------//
void MainWindow::on_button_processHWM_clicked()
{
    QVector<double> classes;

    classes.resize(7);
    int units = ui->combo_hwmunits->currentIndex();

    if(ui->check_manualHWM->isChecked())
    {
        classes[0] = ui->spin_class0->value();
        classes[1] = ui->spin_class1->value();
        classes[2] = ui->spin_class2->value();
        classes[3] = ui->spin_class3->value();
        classes[4] = ui->spin_class4->value();
        classes[5] = ui->spin_class5->value();
        classes[6] = ui->spin_class6->value();
    }
    else
    {
        if(units==1)
        {
            classes[0] = -1.5;
            classes[1] = -1.0;
            classes[2] = -0.5;
            classes[3] =  0.0;
            classes[4] =  0.5;
            classes[5] =  1.0;
            classes[6] =  1.5;
        }
        else
        {
            classes[0] = -5.0;
            classes[1] = -3.5;
            classes[2] = -1.5;
            classes[3] =  0.0;
            classes[4] =  1.5;
            classes[5] =  3.5;
            classes[6] =  5.0;
        }
    }

     if(!thisHWM.isNull())
        delete thisHWM;

    thisHWM = new hwm(ui->Text_HWMFile,ui->check_manualHWM,ui->combo_hwmunits,
                      ui->check_forceregthroughzero,ui->check_dispupperlowerlines,
                      ui->check_regressionColorMatch,ui->button_hwmcolor,ui->button_121linecolor,
                      ui->button_boundlinecolor,ui->button_reglinecolor,
                      ui->text_adchwmaxislabel,ui->text_measuredhwmaxislabel,
                      ui->text_hwmplottitle,ui->spin_upperlowervalue,ui->map_hwm,ui->graphics_hwm,
                      classes,this);

    thisHWM->processHWMData();

    ui->subtab_hwm->setCurrentIndex(1);

    ui->map_hwm->page()->runJavaScript("fitMarkers()");

    return;

}
//-------------------------------------------//


//-------------------------------------------//
//Called when the manual HWM scale button is
//clicked
//-------------------------------------------//
void MainWindow::on_check_manualHWM_toggled(bool checked)
{
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
//Called when the save HWM map image button
//is clicked
//-------------------------------------------//
void MainWindow::on_button_saveHWMMap_clicked()
{
    QString filename;
    QString filter = "JPG (*.jpg *.jpeg)";
    QString TempString = QFileDialog::getSaveFileName(this,"Save as...",
                this->PreviousDirectory,"JPG (*.jpg *.jpeg)",&filter);

    if(TempString==NULL)
        return;

    splitPath(TempString,filename,this->PreviousDirectory);

    QFile HWMOutput(TempString);
    QPixmap HWMImage(ui->map_hwm->size());
    ui->map_hwm->render(&HWMImage);
    HWMOutput.open(QIODevice::WriteOnly);
    HWMImage.save(&HWMOutput,"JPG",100);
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Bring up a color dialog to choose the color
//used for plotting high water marks
//-------------------------------------------//
void MainWindow::on_button_hwmcolor_clicked()
{
    QString ButtonStyle;
    QColor TempColor;

    TempColor = QColorDialog::getColor(DotColorHWM);
    if(TempColor.isValid())
        DotColorHWM = TempColor;
    else
        return;

    ButtonStyle = MakeColorString(DotColorHWM);
    ui->button_hwmcolor->setStyleSheet(ButtonStyle);
    ui->button_hwmcolor->update();
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Bring up a color dialog box for choosing the
//1:1 color
//-------------------------------------------//
void MainWindow::on_button_121linecolor_clicked()
{
    QString ButtonStyle;
    QColor TempColor;

    TempColor = QColorDialog::getColor(LineColor121Line);
    if(TempColor.isValid())
        LineColor121Line = TempColor;
    else
        return;

    ButtonStyle = MakeColorString(LineColor121Line);
    ui->button_121linecolor->setStyleSheet(ButtonStyle);
    ui->button_121linecolor->update();
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Bring up a color dialog for choosing the
//regression line color
//-------------------------------------------//
void MainWindow::on_button_reglinecolor_clicked()
{
    QString ButtonStyle;
    QColor TempColor;

    TempColor = QColorDialog::getColor(LineColorRegression);
    if(TempColor.isValid())
        LineColorRegression = TempColor;
    else
        return;

    ButtonStyle = MakeColorString(LineColorRegression);
    ui->button_reglinecolor->setStyleSheet(ButtonStyle);
    ui->button_reglinecolor->update();
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Bring up a color dialog to choose the
//standard deviation bounding line color
//-------------------------------------------//
void MainWindow::on_button_boundlinecolor_clicked()
{
    QString ButtonStyle;
    QColor TempColor;

    TempColor = QColorDialog::getColor(LineColorBounds);
    if(TempColor.isValid())
        LineColorBounds = TempColor;
    else
        return;

    ButtonStyle = MakeColorString(LineColorBounds);
    ui->button_boundlinecolor->setStyleSheet(ButtonStyle);
    ui->button_boundlinecolor->update();
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Button click routine for fitting high water
//marks to the current view
//-------------------------------------------//
void MainWindow::on_button_fitHWM_clicked()
{
    ui->map_hwm->page()->runJavaScript("fitMarkers()");
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Button to toggle options when the color match
//option for the linear regression is clicked
//-------------------------------------------//
void MainWindow::on_check_regressionColorMatch_clicked(bool checked)
{
    ui->button_hwmcolor->setEnabled(!checked);
    ui->label_hwmcolor->setEnabled(!checked);
    return;
}
//-------------------------------------------//
