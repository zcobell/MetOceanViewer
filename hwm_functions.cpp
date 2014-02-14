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
//  File: hwm_functions.cpp
//
//------------------------------------------------------------------------------

#include "ADCvalidator.h"
#include "ui_ADCvalidator_main.h"
#include "hwm.h"

//Classifies high water marks into classes for coloring
int MainWindow::ClassifyHWM(double diff)
{
    int color;
    int units;
    QVector<double> classes;
    //This is the default classification
    classes.resize(7);

    units = ui->combo_hwmunits->currentIndex();
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

    ui->spin_class0->setValue(classes[0]);
    ui->spin_class1->setValue(classes[1]);
    ui->spin_class2->setValue(classes[2]);
    ui->spin_class3->setValue(classes[3]);
    ui->spin_class4->setValue(classes[4]);
    ui->spin_class5->setValue(classes[5]);
    ui->spin_class6->setValue(classes[6]);

    if(diff<classes[0])
        color=1;
    else if(diff<classes[1])
        color=2;
    else if(diff<classes[2])
        color=3;
    else if(diff<classes[3])
        color=4;
    else if(diff<classes[4])
        color=5;
    else if(diff<classes[5])
        color=6;
    else if(diff<classes[6])
        color=7;
    else
        color=8;

    return color;
}
