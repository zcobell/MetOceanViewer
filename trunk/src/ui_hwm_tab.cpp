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
//  File: ui_hwm_tab.cpp
//
//------------------------------------------------------------------------------

//
//This file contains all the user interactions that occur on the HWM tab
//

#include "ADCvalidator.h"
#include "ui_ADCvalidator_main.h"
#include "hwm.h"

//Global variables
QVector<hwm_data> HighWaterMarks;
QString HighWaterMarkFile;
QVector<double> classes;

//Called when the browse for HWM file button is clicked
void MainWindow::on_browse_hwm_clicked()
{
    HighWaterMarkFile = QFileDialog::getOpenFileName(this,"Select High Water Mark File",
                                    PreviousDirectory,
                                    "Shintaro Style High Water Mark File (*.csv) ;; All Files (*.*)");
    if(HighWaterMarkFile==NULL)
        return;

    QString TempString = RemoveLeadingPath(HighWaterMarkFile);
    GetLeadingPath(HighWaterMarkFile);
    ui->Text_HWMFile->setText(TempString);
}

//Called when the process HWM button is clicked
void MainWindow::on_button_processHWM_clicked()
{
    QString Marker, unitString, Regression, MeasuredString, ModeledString;
    QString HWMColor,One21Color,RegColor,BoundColor,RegressionTitle,XLabel,YLabel;
    QString RegressionGlobal,MyClassList;
    double x,y,measurement,modeled,error,M,B,R,MaximumValue;
    double c0,c1,c2,c3,c4,c5,c6,BoundValue;
    int classification, unit, ierr, PlotUpperLower;
    bool ThroughZero;

    ThroughZero = ui->check_forceregthroughzero->isChecked();

    ierr = ReadHWMData(HighWaterMarkFile, HighWaterMarks);
    if(ierr!=0)
    {
        QMessageBox::information(this,"ERROR","Could not read the high water mark file.");
        return;
    }

    ierr = ComputeLinearRegression(ThroughZero, HighWaterMarks, M, B, R);
    if(ierr!=0)
    {
        QMessageBox::information(this,"ERROR","Could not calculate the regression function.");
        return;
    }

    unit = ui->combo_hwmunits->currentIndex();
    if(unit==0)
        unitString = "'ft'";
    else
        unitString = "'m'";

    ui->map_hwm->page()->mainFrame()->evaluateJavaScript("clearMarkers()");

    //Sanity check on classes
    if(ui->check_manualHWM->isChecked())
    {
        c0 = ui->spin_class0->value();
        c1 = ui->spin_class1->value();
        c2 = ui->spin_class2->value();
        c3 = ui->spin_class3->value();
        c4 = ui->spin_class4->value();
        c5 = ui->spin_class5->value();
        c6 = ui->spin_class6->value();

        if(c1<=c0)
        {
            QMessageBox::information(this,"ERROR","Your classifications are invalid.");
            return;
        }
        else if(c2<=c1)
        {
            QMessageBox::information(this,"ERROR","Your classifications are invalid.");
            return;
        }
        else if(c3<=c2)
        {
            QMessageBox::information(this,"ERROR","Your classifications are invalid.");
            return;
        }
        else if(c4<=c3)
        {
            QMessageBox::information(this,"ERROR","Your classifications are invalid.");
            return;
        }
        else if(c5<=c4)
        {
            QMessageBox::information(this,"ERROR","Your classifications are invalid.");
            return;
        }
        else if(c6<=c5)
        {
            QMessageBox::information(this,"ERROR","Your classifications are invalid.");
            return;
        }
    }

    //Plot the high water mark map
    MeasuredString = "";
    ModeledString = "";
    MaximumValue = 0;

    //Make sure we are fresh for if this is a second round of plotting
    ui->map_hwm->page()->mainFrame()->evaluateJavaScript("clearMarkers()");
    ui->map_regression->reload();

    //Give the browsers a chance to catch up to us
    delay(2);

    for(int i=0;i<HighWaterMarks.size();i++)
    {
        x = HighWaterMarks[i].lon;
        y = HighWaterMarks[i].lat;
        measurement = HighWaterMarks[i].measured;
        modeled = HighWaterMarks[i].modeled;
        error = HighWaterMarks[i].error;

        if(modeled < -9999)
            classification = -1;
        else
            classification = ClassifyHWM(error);

        if(measurement > MaximumValue)
            MaximumValue = measurement + 1;
        else if(modeled > MaximumValue)
            MaximumValue = modeled + 1;

        Marker = "addHWM("+QString::number(x)+","+QString::number(y)+
                ","+QString::number(i)+","+QString::number(modeled)+","+QString::number(measurement)+
                ","+QString::number(error)+","+QString::number(classification)+","+unitString+
                ")";
        ui->map_hwm->page()->mainFrame()->evaluateJavaScript(Marker);
        if(i==0)
        {
            ModeledString = QString::number(modeled);
            MeasuredString = QString::number(measurement);
        }
        else
        {
            ModeledString = ModeledString+":"+QString::number(modeled);
            MeasuredString = MeasuredString+":"+QString::number(measurement);
        }
    }

    ui->subtab_hwm->setCurrentIndex(2);

    RegressionTitle = ui->text_hwmplottitle->text();
    XLabel = ui->text_adchwmaxislabel->text();
    YLabel = ui->text_measuredhwmaxislabel->text();
    HWMColor = DotColorHWM.name();
    One21Color = LineColor121Line.name();
    BoundColor = LineColorBounds.name();
    RegColor = LineColorRegression.name();

    if(ui->check_dispupperlowerlines->isChecked())
        PlotUpperLower = 1;
    else
        PlotUpperLower = 0;

    BoundValue = ui->spin_upperlowervalue->value();

    RegressionGlobal = "setGlobal('"+RegressionTitle+"','"+XLabel+"','"+YLabel+"','"+
            HWMColor+"','"+RegColor+"','"+One21Color+"','"+BoundColor+"')";
    ui->map_regression->page()->mainFrame()->evaluateJavaScript(RegressionGlobal);

    Regression = "plotRegression('"+ModeledString+"','"+MeasuredString+"',"+
            unitString+","+QString::number(MaximumValue)+","+QString::number(M,'f',2)+
            ","+QString::number(B,'f',2)+","+QString::number(R,'f',2)+","+QString::number(PlotUpperLower)+
            ","+QString::number(BoundValue)+")";
    ui->map_regression->page()->mainFrame()->evaluateJavaScript(Regression);

    ui->subtab_hwm->setCurrentIndex(1);
    delay(1);
    ui->map_hwm->page()->mainFrame()->evaluateJavaScript("fitMarkers()");

    MyClassList = "addLegend("+unitString+",'"+QString::number(classes[0],'f',2)+":"+QString::number(classes[1],'f',2)+":"+
            QString::number(classes[2],'f',2)+":"+QString::number(classes[3],'f',2)+":"+
            QString::number(classes[4],'f',2)+":"+QString::number(classes[5],'f',2)+":"+
            QString::number(classes[6],'f',2)+"')";

    ui->map_hwm->page()->mainFrame()->evaluateJavaScript(MyClassList);

    return;

}

//Called when the manual HWM scale button is clicked
void MainWindow::on_check_manualHWM_toggled(bool checked)
{
    ui->spin_class0->setEnabled(checked);
    ui->spin_class1->setEnabled(checked);
    ui->spin_class2->setEnabled(checked);
    ui->spin_class3->setEnabled(checked);
    ui->spin_class4->setEnabled(checked);
    ui->spin_class5->setEnabled(checked);
    ui->spin_class6->setEnabled(checked);

}

//Called when the save HWM map image button is clicked
void MainWindow::on_button_saveHWMMap_clicked()
{
    QString filter = "JPG (*.jpg *.jpeg)";
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",
                PreviousDirectory,"JPG (*.jpg *.jpeg)",&filter);

    if(Filename==NULL)
        return;

    GetLeadingPath(Filename);
    QFile HWMOutput(Filename);
    QPixmap HWMImage(ui->map_hwm->size());
    ui->map_hwm->render(&HWMImage);
    HWMOutput.open(QIODevice::WriteOnly);
    HWMImage.save(&HWMOutput,"JPG",100);

}

//Called when the save regression image button is clicked
void MainWindow::on_button_saveregression_clicked()
{
    QString filter = "JPG (*.jpg *.jpeg)";
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",
                PreviousDirectory,"JPG (*.jpg *.jpeg)",&filter);

    if(Filename==NULL)
        return;

    GetLeadingPath(Filename);

    QFile HWMOutput(Filename);
    QPixmap HWMImage(ui->map_regression->size());
    ui->map_regression->render(&HWMImage);
    HWMOutput.open(QIODevice::WriteOnly);
    HWMImage.save(&HWMOutput,"JPG",100);
}

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
