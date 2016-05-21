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
        
#include "MetOceanViewer.h"
#include "ui_MetOceanViewer_main.h"
#include "about_dialog.h"
#include "update_dialog.h"
#include "mov_generic.h"
#include "mov_session.h"

//-------------------------------------------//
//Main routine which will intialize all the tabs
MainWindow::MainWindow(bool processCommandLine, QString commandLineFile, QWidget *parent):QMainWindow(parent),ui(new Ui::MainWindow)
{
    //Setup UI
    ui->setupUi(this);
    this->processCommandLine = processCommandLine;
    this->commandLineFile = commandLineFile;
    setupMetOceanViewerUI();
}

//Main destructor routine
MainWindow::~MainWindow()
{
    delete ui;
}

//-------------------------------------------//
//Terminates the application when quit button clicked
//-------------------------------------------//
void MainWindow::on_actionQuit_triggered()
{
    this->close();
}
//-------------------------------------------//


void MainWindow::closeEvent(QCloseEvent *event)
{
    if(confirmClose())
        event->accept();
    else
        event->ignore();
}

bool MainWindow::confirmClose()
{
    QMessageBox::StandardButton answer;
    answer = QMessageBox::question(this,tr("Exit"),tr("Do you want to exit MetOcean Viewer?"),QMessageBox::Yes|QMessageBox::No);
    return (answer == QMessageBox::Yes);
}

//-------------------------------------------//
//Bring up the about dialog box
//-------------------------------------------//
void MainWindow::on_actionAbout_triggered()
{
    QPointer<about_dialog> aboutWindow = new about_dialog(this);
    aboutWindow->setModal(false);
    aboutWindow->exec();
    return;
}
//-------------------------------------------//

//-------------------------------------------//
//Bring up the update dialog
//-------------------------------------------//
void MainWindow::on_actionCheck_For_Updates_triggered()
{
    QPointer<update_dialog> updateWindow = new update_dialog(this);
    updateWindow->setModal(false);
    updateWindow->runUpdater();
    updateWindow->exec();
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Use of the load session button from the
//menu is triggered here
//-------------------------------------------//
void MainWindow::on_actionLoad_Session_triggered()
{
    QString BaseFile;
    QString LoadFile = QFileDialog::getOpenFileName(this,
                            "Open Session...",
                            this->PreviousDirectory,
                            "MetOcean Viewer Sessions (*.mvs)");

    if(LoadFile==NULL)
        return;

    mov_generic::splitPath(LoadFile,BaseFile,this->PreviousDirectory);
    int ierr = this->sessionState->open(LoadFile);

    if(ierr==0)
    {
        ui->MainTabs->setCurrentIndex(1);
        ui->subtab_timeseries->setCurrentIndex(0);
    }

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//The save session button from the file menu
//is triggered here
//-------------------------------------------//
void MainWindow::on_actionSave_Session_triggered()
{
    if(this->sessionState->getSessionFilename()!=QString())
        this->sessionState->save();
    else
        on_actionSave_Session_As_triggered();
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//The save as session button from the file
//menu is triggered here
//-------------------------------------------//
void MainWindow::on_actionSave_Session_As_triggered()
{
    QString SaveFile = QFileDialog::getSaveFileName(this,
                        "Save Session...",this->PreviousDirectory,
                        "MetOcean Viewer Sessions (*.mvs)");
    if(SaveFile!=NULL)
    {
        this->sessionState->setSessionFilename(SaveFile);
        this->sessionState->save();
    }
    return;
}
//-------------------------------------------//

void MainWindow::handleEnterKey()
{
    //Events for "ENTER" on the Live Data tabs
    if(ui->MainTabs->currentIndex()==0)
    {
        //NOAA Tab
        if(ui->subtab_livedata->currentIndex()==0)
        {
            if(ui->Combo_NOAAPanTo->hasFocus())
                ui->noaa_map->page()->runJavaScript("panTo('"+ui->Combo_NOAAPanTo->currentText()+"')");
            else
                this->plotNOAAStation();
        }
        //USGS Tab
        else if(ui->subtab_livedata->currentIndex()==1)
        {
            if(ui->combo_usgs_panto->hasFocus())
                ui->usgs_map->page()->runJavaScript("panTo('"+ui->combo_usgs_panto->currentText()+"')");
            else
                on_button_usgs_fetch_clicked();
        }
        //XTide Tab
        else if(ui->subtab_livedata->currentIndex()==2)
        {
            if(ui->combo_xtide_panto->hasFocus())
                ui->xtide_map->page()->runJavaScript("panTo('"+ui->combo_xtide_panto->currentText()+"')");
            else
                on_button_xtide_compute_clicked();

        }
    }
    //Events for "ENTER" on the timeseries tabs
    else if(ui->MainTabs->currentIndex()==1)
    {
       if(ui->subtab_timeseries->currentIndex()==0)
       {
           on_button_processTimeseriesData_clicked();
       }
       else if(ui->subtab_timeseries->currentIndex()==1)
       {
           on_button_plotTimeseriesStation_clicked();
       }
    }
    else if(ui->MainTabs->currentIndex()==2)
    {
        if(ui->subtab_hwm->currentIndex()==1)
            on_button_processHWM_clicked();
    }
    return;
}


void MainWindow::throwErrorMessageBox(QString errorString)
{
    QMessageBox::critical(this,"ERROR",errorString);
    return;
}


void MainWindow::setLoadSessionFile(bool toggle, QString sessionFile)
{
    this->processCommandLine = toggle;
    this->commandLineFile = sessionFile;
    return;
}
