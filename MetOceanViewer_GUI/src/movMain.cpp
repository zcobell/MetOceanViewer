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
        
#include "mov_window_main.h"
#include "ui_mov_window_main.h"
#include "mov_dialog_about.h"
#include "mov_dialog_update.h"
#include "movGeneric.h"
#include "movSession.h"

//-------------------------------------------//
//Main routine which will intialize all the tabs
mov_window_main::mov_window_main(bool processCommandLine, QString commandLineFile, QWidget *parent):QMainWindow(parent),ui(new Ui::mov_window_main)
{
    //Setup UI
    ui->setupUi(this);
    this->processCommandLine = processCommandLine;
    this->commandLineFile = commandLineFile;
    setupMetOceanViewerUI();
}

//Main destructor routine
mov_window_main::~mov_window_main()
{
    delete ui;
}

//-------------------------------------------//
//Terminates the application when quit button clicked
//-------------------------------------------//
void mov_window_main::on_actionQuit_triggered()
{
    this->close();
}
//-------------------------------------------//


void mov_window_main::closeEvent(QCloseEvent *event)
{
    if(confirmClose())
        event->accept();
    else
        event->ignore();
}

bool mov_window_main::confirmClose()
{
    QMessageBox::StandardButton answer;
    answer = QMessageBox::question(this,tr("Exit"),tr("Do you want to exit MetOcean Viewer?"),QMessageBox::Yes|QMessageBox::No);
    return (answer == QMessageBox::Yes);
}

//-------------------------------------------//
//Bring up the about dialog box
//-------------------------------------------//
void mov_window_main::on_actionAbout_triggered()
{
    QPointer<mov_dialog_about> aboutWindow = new mov_dialog_about(this);
    aboutWindow->setModal(false);
    aboutWindow->exec();
    return;
}
//-------------------------------------------//

//-------------------------------------------//
//Bring up the update dialog
//-------------------------------------------//
void mov_window_main::on_actionCheck_For_Updates_triggered()
{
    QPointer<mov_dialog_update> updateWindow = new mov_dialog_update(this);
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
void mov_window_main::on_actionLoad_Session_triggered()
{
    QString BaseFile;
    QString LoadFile = QFileDialog::getOpenFileName(this,
                            tr("Open Session..."),
                            this->PreviousDirectory,
                            tr("MetOcean Viewer Sessions (*.mvs)"));

    if(LoadFile==NULL)
        return;

    MovGeneric::splitPath(LoadFile,BaseFile,this->PreviousDirectory);
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
void mov_window_main::on_actionSave_Session_triggered()
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
void mov_window_main::on_actionSave_Session_As_triggered()
{
    QString SaveFile = QFileDialog::getSaveFileName(this,
                        tr("Save Session..."),this->PreviousDirectory,
                        tr("MetOcean Viewer Sessions (*.mvs)"));
    if(SaveFile!=NULL)
    {
        this->sessionState->setSessionFilename(SaveFile);
        this->sessionState->save();
    }
    return;
}
//-------------------------------------------//

void mov_window_main::handleEnterKey()
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


void mov_window_main::throwErrorMessageBox(QString errorString)
{
    QMessageBox::critical(this,tr("ERROR"),errorString);
    return;
}


void mov_window_main::setLoadSessionFile(bool toggle, QString sessionFile)
{
    this->processCommandLine = toggle;
    this->commandLineFile = sessionFile;
    return;
}
