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

#include "version.h"
#include "aboutdialog.h"
#include "ui_aboutdialog.h"

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::AboutDialog) {
  ui->setupUi(this);

  QString aboutText = generateAboutText();
  QString licenseText = generateLicenseText();
  ui->text_about->setHtml(aboutText);
  ui->text_license->setHtml(licenseText);
}

AboutDialog::~AboutDialog() { delete ui; }

void AboutDialog::on_button_ok_clicked() { accept(); }

//-------------------------------------------//
// This is the text that will appear in the
// about dialog box.
//-------------------------------------------//
QString AboutDialog::generateAboutText() {
  QString text, compiler, architecture;

  if (QString(MOV_COMPILER) == "msvc")
    compiler = "Microsoft Visual C++ with Qt";
  else if (QString(MOV_COMPILER) == "mingw")
    compiler = "MinGW g++ with Qt";
  else if (QString(MOV_COMPILER) == "gpp")
    compiler = "g++ with Qt";
  else if (QString(MOV_COMPILER) == "xcode")
    compiler = "XCode with Qt";

  if (QString(MOV_ARCH) == "i386")
    architecture = "32 bit";
  else if (QString(MOV_ARCH) == "x86_64")
    architecture = "64 bit";
  else
    architecture = QString(MOV_ARCH);

  text =
      QString("") +
      "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" "
      "\"http://www.w3.org/TR/REC-html40/strict.dtd\">" +
      "<html><head><meta name=\"qrichtext\" content=\"1\" /><style "
      "type=\"text/css\">" +
      "p, li { white-space: pre-wrap; }" +
      "</style></head><body style=\" font-family:'MS Shell Dlg 2'; "
      "font-size:8pt; font-weight:400; font-style:normal;\">" +
      "<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; "
      "margin-right:0px; -qt-block-indent:0; text-indent:0px;\">" +

      "<a href=\"https://github.com/zcobell/MetOceanViewer\">Visit GitHub "
      "repository</a><br>" +
      "<a href=\"https://github.com/zcobell/MetOceanViewer/wiki\">Visit "
      "Documentation Page</a><br><br>" +

#ifndef GIT_VERSION
      "<b>Version: </b> " + QString(VER_FILEVERSION_STR) + "<br><br>" +
#else
      "<b>Git Revision: </b>" + QString(GIT_VERSION) + "<br><br>" +
#endif

      "<b>Compiler: </b>" + compiler + "<br>" + "<b>Architecture: </b>" +
      architecture + "<br>" + "<b>Qt Version:  </b>" + QString(QT_VERSION_STR) +
      "<br><br>"

      "<b>External Libraries</b> <br><br>" +
      "NOAA CO-OPS API - <a "
      "href=\"http://tidesandcurrents.noaa.gov\">http://"
      "tidesandcurrents.noaa.gov</a><br>" +
      "USGS Waterdata API - <a "
      "href=\"http://waterdata.usgs.gov\">http://waterdata.usgs.gov</a><br>" +
      "Google Maps v3.x API - <a "
      "href=\"http://maps.google.com\">http://maps.google.com</a><br>" +
      "Google Fusion Tables API - <a "
      "href=\"http://tables.googlelabs.com\">http://tables.googlelabs.com</"
      "a><br>" +
      "netCDF v4.3.3 - <a "
      "href=\"http://www.unidata.ucar.edu/software/netcdf/\">http://"
      "www.unidata.ucar.edu/software/netcdf/</a><br>" +
      "HDF5 v1.8.14 - <a "
      "href=\"http://www.hdfgroup.org/HDF5/\">http://www.hdfgroup.org/HDF5/</"
      "a> <br>" +
      "Curl v7.35.0 - <a "
      "href=\"http://curl.haxx.se/\">http://curl.haxx.se/</a><br>" +
      "zlib v1.2.8 - <a "
      "href=\"http://www.zlib.net/\">http://www.zlib.net/</a><br>" +
      "proj.4 v4.9.2 - <a "
      "href=\"http://proj4.org/\">http://proj4.org/</a><br>" +
      "OpenSSL v1.0.2d - <a "
      "href=\"https://www.openssl.org\">https://www.openssl.org</a><br>" +
      "XTide - <a "
      "href=\"http://www.flaterco.com/xtide/\">http://www.flaterco.com/xtide/</"
      "a><br>" +
      "KDTree2 - Matthew Kennel (Institute for Nonlinear Science)<br>" +
      "Boost v1.6 - <a "
      "href=\"http://www.boost.org/\">http://www.boost.org/</a>" +

      "</p></body></html>";

  return text;
}

QString AboutDialog::generateLicenseText() {
  QString text;

  text =
      QString("") +
      "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" "
      "\"http://www.w3.org/TR/REC-html40/strict.dtd\">" +
      "<html><head><meta name=\"qrichtext\" content=\"1\" /><style "
      "type=\"text/css\">" +
      "p, li { white-space: pre-wrap; }" +
      "</style></head><body style=\" font-family:'MS Shell Dlg 2'; "
      "font-size:8pt; font-weight:400; font-style:normal;\">" +
      "<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; "
      "margin-right:0px; -qt-block-indent:0; text-indent:0px;\">" +
      "<b>MetOcean Viewer</b> <br>" +
      "<i>A simple interface for viewing hydrodynamic model data</i> <br><br>" +
      "Copyright (C) 2017  Zach Cobell <br><br>" +
      "This program is free software: you can redistribute it and/or modify it "
      "under the terms of the GNU General Public License" +
      " as published by the Free Software Foundation, either version 3 of the "
      "License, or (at your option) any later version. <br><br>"
      "This program is distributed in the hope that it will be useful, but "
      "WITHOUT ANY WARRANTY; without even the implied " +
      "warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See "
      "the GNU General Public License for more details. <br><br>" +
      "You should have received a copy of the GNU General Public License along "
      "with this program.  " +
      "If not, see http://www.gnu.org/licenses/.";

  return text;
}
