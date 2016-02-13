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

//-------------------------------------------//
//Main routine which will intialize all the tabs
MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),ui(new Ui::MainWindow)
{
    //Setup UI
    ui->setupUi(this);
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
    if(confirmClose())
        close();
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
//Handle the "enter" or "return" keypress
//events on certain pages to automatically
//draw plots
//-------------------------------------------//
void MainWindow::keyPressEvent(QKeyEvent *key)
{

    //Catch "ENTER" or "RETURN" Key
    if(key->key() == Qt::Key_Enter || key->key() == Qt::Key_Return)
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
                    on_Button_FetchData_clicked();
            }
            //USGS Tab
            else if(ui->subtab_livedata->currentIndex()==1)
            {
                if(ui->combo_usgs_panto->hasFocus())
                    ui->usgs_map->page()->runJavaScript("panTo('"+ui->combo_usgs_panto->currentText()+"')");
                else
                    on_button_usgs_fetch_clicked();
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

    }
    return;

}
//-------------------------------------------//


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

    splitPath(LoadFile,BaseFile,this->PreviousDirectory);

    SessionFile = LoadFile;

    loadSession();

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//The save session button from the file menu
//is triggered here
//-------------------------------------------//
void MainWindow::on_actionSave_Session_triggered()
{
    saveSession();
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
        SessionFile = SaveFile;
        saveSession();
    }
    return;
}
//-------------------------------------------//
