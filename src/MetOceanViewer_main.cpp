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
        
#include <MetOceanViewer.h>
#include <ui_MetOceanViewer_main.h>
#include <timeseries.h>

//-------------------------------------------//
//A whole load of variables we'll need at
//some point or another. These are sort of
//"Global" variables. Since I learned by
//programming in FORTRAN, this makes me happy
//-------------------------------------------//
QString PreviousDirectory;
QString SLASH;
QColor ADCIRCIMEDSColor,OBSIMEDSColor;
QColor LineColor121Line,LineColorBounds;
QColor DotColorHWM,LineColorRegression;
QDateTime IMEDSMinDate,IMEDSMaxDate;

//Some flags that we'll use later. These are just
//values we might need to search for at some point
double FLAG_NULL_TS = -999991025;
QDateTime FLAG_NULL_DATE = QDateTime::fromString(
                              "01/01/1900 00:00",
                              "MM/dd/yyyy hh:mm");
//-------------------------------------------//


//-------------------------------------------//
//Main routine which will intialize all the tabs
MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),ui(new Ui::MainWindow)
{
    //Setup UI
    ui->setupUi(this);
    setupMetOceanViewerUI();

    //Initialize some things
    this->thisNOAA = NULL;
}

//Main destructor routine
MainWindow::~MainWindow()
{
    delete ui;
}

//Function to handle a clicked link and open in the user's
//default browser.
void MainWindow::OpenExternalBrowser(const QUrl & url)
{
    QDesktopServices::openUrl(url);
}
