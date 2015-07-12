//----GPL-----------------------------------------------------------------------
//
// This file is part of MetOceanViewer.
//
//    MetOceanViewer is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    MetOceanViewer is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with MetOceanViewer.  If not, see <http://www.gnu.org/licenses/>.
//------------------------------------------------------------------------------

#include "MetOceanViewer.h"
#include "ui_MetOceanViewer_main.h"

//-------------------------------------------//
//Called when the pan to combo box is updated
//-------------------------------------------//
void MainWindow::on_Combo_NOAAPanTo_currentIndexChanged(int index)
{
    ui->noaa_map->page()->mainFrame()->evaluateJavaScript("panTo('"+ui->Combo_NOAAPanTo->currentText()+"')");
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Called when the user attempts to save the NOAA chart
//-------------------------------------------//
void MainWindow::on_button_noaasavechart_clicked()
{
    QString filter = "JPG (*.jpg *.jpeg)";
    QString DefaultFile = "/NOAA_"+QString::number(NOAAMarkerID)+".jpg";
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",
                PreviousDirectory+DefaultFile,"JPG (*.jpg *.jpeg)",&filter);

    if(Filename==NULL)
        return;

    GetLeadingPath(Filename);
    QFile NOAAOutput(Filename);
    QPixmap NOAAImage(ui->noaa_map->size());
    ui->noaa_map->render(&NOAAImage);
    NOAAOutput.open(QIODevice::WriteOnly);
    NOAAImage.save(&NOAAOutput,"JPG",100);

}
//-------------------------------------------//


//-------------------------------------------//
//Called when the user tries to save the NOAA data
//-------------------------------------------//
void MainWindow::on_button_noaasavedata_clicked()
{

    if(NOAAMarkerID==-1)
    {
        QMessageBox::information(this,"ERROR","No Station has been selected.");
        return;
    }
    QVariant eval = ui->noaa_map->page()->mainFrame()->evaluateJavaScript("returnStationID()");
    QStringList evalList = eval.toString().split(";");
    int NOAAMarkerID2 = evalList.value(0).toInt();
    if(NOAAMarkerID != NOAAMarkerID2)
    {
        QMessageBox::information(this,"ERROR","The currently selected station is not the data loaded.");
        return;
    }

    QString filter;
    QString DefaultFile = "/NOAA_"+QString::number(NOAAMarkerID)+".imeds";
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",PreviousDirectory+DefaultFile,
                                                    "IMEDS (*.imeds);;CSV (*.csv)",&filter);
    QStringList filter2 = filter.split(" ");
    QString format = filter2.value(0);

    if(Filename == NULL)
        return;

    GetLeadingPath(Filename);
    QFile NOAAOutput(Filename);
    QTextStream Output(&NOAAOutput);
    NOAAOutput.open(QIODevice::WriteOnly);

    if(format.compare("CSV")==0)
    {
        Output << "Station: "+QString::number(NOAAMarkerID)+"\n";
        Output << "Datum: "+ui->combo_noaadatum->currentText()+"\n";
        Output << "Units: "+ui->combo_noaaunits->currentText()+"\n";
        Output << "\n";
        for(int i=0;i<CurrentNOAAStation.size();i++)
        {
            Output << CurrentNOAAStation[i].Date.toString("MM/dd/yyyy")+","+
                      CurrentNOAAStation[i].Time.toString("hh:mm")+","+
                      QString::number(CurrentNOAAStation[i].value)+"\n";
        }
    }
    else if(format.compare("IMEDS")==0)
    {
        QString datum = ui->combo_noaadatum->currentText();
        QString units = ui->combo_noaaunits->currentText();
        QString units2;
        if(units.compare("metric")==0)
        {
            units = "meters";
            units2 = "m";
        }
        else
        {
            units = "feet";
            units2 = "ft";
        }
        Output << "% IMEDS generic format - Water Level\n";
        Output << "% year month day hour min sec watlev("+units2+")\n";
        Output << "NOAA    UTC    "+datum+"\n";
        Output << "NOAA_"+QString::number(NOAAMarkerID)+"   "+QString::number(CurrentNOAALat)+
                  "   "+QString::number(CurrentNOAALon)+"\n";
        for(int i=0;i<CurrentNOAAStation.size();i++)
        {
            Output << CurrentNOAAStation[i].Date.toString("yyyy")+"    "+
                        CurrentNOAAStation[i].Date.toString("MM")+"    "+
                        CurrentNOAAStation[i].Date.toString("dd")+"    "+
                        CurrentNOAAStation[i].Time.toString("hh")+"    "+
                        CurrentNOAAStation[i].Time.toString("mm")+"    "+
                                                        "00" +"    "+
                        QString::number(CurrentNOAAStation[i].value)+"\n";
        }

    }
    NOAAOutput.close();
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Called when the NOAA fetch data button is clicked
//-------------------------------------------//
void MainWindow::on_Button_FetchData_clicked()
{
    qint64 Duration;
    QString RequestURL,StartString,EndString,Datum,Units,Product;
    QDateTime StartDate,EndDate;
    int i,ProductIndex,NumDownloads,CurrentDownloadIndex;
    QVector<QDateTime> StartDateList,EndDateList;

    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QVariant eval = ui->noaa_map->page()->mainFrame()->evaluateJavaScript("returnStationID()");
    QStringList evalList = eval.toString().split(";");

    NOAAMarkerID = evalList.value(0).toInt();
    CurrentNOAAStationName = evalList.value(1).simplified();
    CurrentNOAALat = evalList.value(3).toDouble();
    CurrentNOAALon = evalList.value(2).toDouble();

    //Update status
    ui->statusBar->showMessage("Downloading data from NOAA...",0);

    //Get the date range
    StartDate = ui->Date_StartTime->dateTime();
    EndDate = ui->Date_EndTime->dateTime();

    if(StartDate==EndDate||EndDate<StartDate)
    {
        QMessageBox::information(NULL,"ERROR","Dates must be a valid range.");
        ui->statusBar->clearMessage();
        return;
    }

    //Begin organizing the dates for download
    Duration = StartDate.daysTo(EndDate);
    NumDownloads = (Duration / 30) + 1;
    StartDateList.resize(NumDownloads);
    EndDateList.resize(NumDownloads);

    //Build the list of dates in 30 day intervals
    for(i=0;i<NumDownloads;i++)
    {
        StartDateList[i] = StartDate.addDays(i*30).addDays(i);
        StartDateList[i].setTime(QTime(0,0,0));
        EndDateList[i]   = StartDateList[i].addDays(30);
        EndDateList[i].setTime(QTime(23,59,59));
        if(EndDateList[i]>EndDate)
            EndDateList[i] = EndDate;
    }

    //Get options
    Units = ui->combo_noaaunits->currentText();
    Product = retrieveProduct(2);
    ProductIndex = ui->combo_NOAAProduct->currentIndex();
    if(ProductIndex == 0 || ProductIndex == 1 || ProductIndex == 2)
        Datum = ui->combo_noaadatum->currentText();
    else if(ProductIndex == 3 || ProductIndex == 5
         || ProductIndex == 4 || ProductIndex == 6 || ProductIndex == 7)
        Datum = "Stnd";

    //Connect the finished downloading signal to the routine that plots the markers
    connect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(ReadNOAAResponse(QNetworkReply*)));

    //Allocate the NOAA array
    NOAAWebData.clear();
    NOAAWebData.resize(NumDownloads);

    NumNOAADataRead = 0;
    for(i=0;i<NumDownloads;i++)
    {
        //Make the date string
        StartString = StartDateList[i].toString("yyyyMMdd hh:mm");
        EndString = EndDateList[i].toString("yyyyMMdd hh:mm");

        //See where we are in the download list
        CurrentDownloadIndex = NumNOAADataRead;

        //Build the URL to request data from the NOAA CO-OPS API
        RequestURL = QString("http://tidesandcurrents.noaa.gov/api/datagetter?")+
                     QString("product="+Product+"&application=metoceanviewer")+
                     QString("&begin_date=")+StartString+QString("&end_date=")+EndString+
                     QString("&station=")+QString::number(NOAAMarkerID)+
                     QString("&time_zone=GMT&units=")+Units+
                     QString("&interval=&format=csv");

        //Allow a different datum where allowed
        if(Datum != "Stnd")RequestURL = RequestURL+QString("&datum=")+Datum;

        //Send the request
        manager->get(QNetworkRequest(QUrl(RequestURL)));

        //Wait for the download before starting the next
        while(CurrentDownloadIndex==NumNOAADataRead)
            delayM(100);

    }

    //Disconnect the download manager
    disconnect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(ReadNOAAResponse(QNetworkReply*)));

    //Call the plotting routine
    PlotNOAAResponse();

    return;
}
//-------------------------------------------//

