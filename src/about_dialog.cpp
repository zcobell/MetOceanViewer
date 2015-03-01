//---- LGPL --------------------------------------------------------------------
//
// Copyright (C)  ARCADIS, 2011-2015.
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
// $Rev$
// $HeadURL$
//------------------------------------------------------------------------------
//  File: about_dialog.cpp
//
//------------------------------------------------------------------------------

#include "about_dialog.h"
#include "ui_about_dialog.h"
#include "version.h"

about_dialog::about_dialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::about_dialog)
{
    ui->setupUi(this);

    QString aboutText = generateAboutText();
    ui->text_about->appendHtml(aboutText);

}

about_dialog::~about_dialog()
{
    delete ui;
}

void about_dialog::on_button_ok_clicked()
{
    accept();
}

QString about_dialog::generateAboutText()
{
    QString text;

    text = "<html><body>" +
            QString(0xA9) + " Copyright 2015 Zach Cobell, ARCADIS <br><br>" +
            "<b>Version: </b> "+QString(VER_FILEVERSION_STR) + "<br><br>" +
            "<b>Subversion Revision: </b>"+QString(SVER) + "<br><br>" +
            "<b>External Libraries</b> <br><br>" +
            "NOAA CO-OPS API - http://tidesandcurrents.noaa.gov<br>" +
            "USGS Waterdata API - http://waterdata.usgs.gov<br>" +
            "Google Maps v3.x API - http://maps.google.com <br>" +
            "Google Fusion Tables API - http://tables.googlelabs.com<br>" +
            "HighCharts v4.1.1 - http://www.highcharts.com/ <br>" +
            "netCDF v4.3.3 - http://www.unidata.ucar.edu/software/netcdf/ <br>" +
            "HDF5 v1.8.14 - http://www.hdfgroup.org/HDF5/ <br>" +
            "Curl v7.35.0 - http://curl.haxx.se/ <br>" +
            "zlib v1.2.8 - http://www.zlib.net/" +
            "</body></html>";

    return text;
}
