#include "noaa.h"

noaa::noaa(QMainWindow *parent)
{
    ThisParent = parent;
    return;
}


int noaa::fetchNOAAData()
{
    QEventLoop loop;
    qint64 Duration;
    QString PlotTitle,RequestURL,StartString,EndString,Product,Product2;
    QString javascript,Units,Datum,XLabel,YLabel;
    QDateTime StartDate,EndDate;
    int i,j,ierr,ProductIndex,NumDownloads,NumData;
    QVector<QDateTime> StartDateList,EndDateList;

    if(this->StartDate==this->EndDate||this->EndDate<this->StartDate)
        return NOAA_ERR_INVALIDDATERANGE;

    //Begin organizing the dates for download
    Duration = StartDate.daysTo(EndDate);
    NumDownloads = (Duration / 30) + 1;
    StartDateList.resize(NumDownloads);
    EndDateList.resize(NumDownloads);

    //Build the list of dates in 30 day intervals
    for(i=0;i<NumDownloads;i++)
    {
        StartDateList[i] = this->StartDate.addDays(i*30).addDays(i);
        StartDateList[i].setTime(QTime(0,0,0));
        EndDateList[i]   = StartDateList[i].addDays(30);
        EndDateList[i].setTime(QTime(23,59,59));
        if(EndDateList[i]>this->EndDate)
            EndDateList[i] = this->EndDate;
    }

    ierr = this->retrieveProduct(2,Product,Product2);

    if(this->ProductIndex==0)
        NumData = 2;
    else
        NumData = 1;

    if(ProductIndex == 4 || ProductIndex == 5 || ProductIndex == 6 || ProductIndex == 7 || ProductIndex == 8)
        datum = "Stnd";

    //Allocate the NOAA array
    this->NOAAWebData.clear();
    this->NOAAWebData.resize(NumData);
    for(i=0;i<this->NumData;i++)
        this->NOAAWebData[i].resize(NumDownloads);

    QNetworkAccessManager* manager = new QNetworkAccessManager(this);

    for(j=0;j<NumData;j++)
    {
        for(i=0;i<NumDownloads;i++)
        {
            //Make the date string
            StartString = StartDateList[i].toString("yyyyMMdd hh:mm");
            EndString = EndDateList[i].toString("yyyyMMdd hh:mm");

            //Build the URL to request data from the NOAA CO-OPS API
            if(j==0)
                RequestURL = QString("http://tidesandcurrents.noaa.gov/api/datagetter?")+
                             QString("product="+Product+"&application=metoceanviewer")+
                             QString("&begin_date=")+StartString+QString("&end_date=")+EndString+
                             QString("&station=")+QString::number(NOAAMarkerID)+
                             QString("&time_zone=GMT&units=")+Units+
                             QString("&interval=&format=csv");
            else
                RequestURL = QString("http://tidesandcurrents.noaa.gov/api/datagetter?")+
                             QString("product="+Product2+"&application=metoceanviewer")+
                             QString("&begin_date=")+StartString+QString("&end_date=")+EndString+
                             QString("&station=")+QString::number(NOAAMarkerID)+
                             QString("&time_zone=GMT&units=")+Units+
                             QString("&interval=&format=csv");

            //Allow a different datum where allowed
            if(this->Datum != "Stnd")RequestURL = RequestURL+QString("&datum=")+Datum;

            //Send the request
            QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(RequestURL)));
            connect(reply,SIGNAL(finished()),&loop,SLOT(quit()));
            connect(reply,SIGNAL(error(QNetworkReply::NetworkError)),&loop,SLOT(quit()));
            loop.exec();
            this->ReadNOAAResponse(reply,i,j);

        }
    }

    //Update status
    ui->statusBar->showMessage("Plotting the data from NOAA...",0);
    if(ProductIndex == 0)
    {
        if(Units=="metric")
            Units="m";
        else
            Units="ft";
        YLabel = "Water Level ("+Units+", "+Datum+")";
    }
    else if(ProductIndex == 1 || ProductIndex == 2 || ProductIndex == 3)
    {
        if(Units=="metric")
            Units="m";
        else
            Units="ft";
        Datum = ui->combo_noaadatum->currentText();
        YLabel = Product+" ("+Units+", "+Datum+")";
    }
    else if(ProductIndex == 6)
    {
        if(Units=="metric")
            Units="m/s";
        else
            Units="knots";
        Datum = "Stnd";
        YLabel = Product+" ("+Units+")";
    }
    else if(ProductIndex == 4 || ProductIndex == 5)
    {
        if(Units=="metric")
            Units="Celcius";
        else
            Units="Fahrenheit";
        Datum = "Stnd";
        YLabel = Product+" ("+Units+")";
    }
    else if(ProductIndex == 7)
    {
        Units = "%";
        Datum = "Stnd";
        YLabel = Product+" ("+Units+")";
    }
    else if(ProductIndex == 8)
    {
        Units = "mb";
        Datum = "Stnd";
        YLabel = Product+" ("+Units+")";
    }

    XLabel = "Date";
    PlotTitle = "Station "+QString::number(NOAAMarkerID)+": "+CurrentNOAAStationName;
    javascript = "setGlobal('"+PlotTitle+"','"+XLabel+"','"+YLabel+"','"+QString::number(FLAG_NULL_TS)+"')";
    ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javascript);

    if(NumData==2)
    {
        javascript = "SetSeriesOptions(0,'Observed Water Level','#0101DF')";
        ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javascript);
        javascript = "SetSeriesOptions(1,'Predicted Water Level','#00FF00')";
        ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javascript);
    }
    else
    {
        javascript = "SetSeriesOptions(0,'"+Product+"','#0101DF')";
        ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javascript);
    }

    javascript = "allocateData("+QString::number(NumData)+")";
    ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javascript);

    //Call the plotting routine
    PlotNOAAResponse();

    //Remove the wait cursor
    QApplication::restoreOverrideCursor();
    ui->statusBar->clearMessage();


    return;
}

//-------------------------------------------//
//Routine to read the data that has been
//received from the NOAA server
//-------------------------------------------//
void noaa::ReadNOAAResponse(QNetworkReply *reply, int index, int index2)
{
    QByteArray Data;

    //Catch some errors during the download
    if(reply->error()!=0)
    {
        QMessageBox::information(this,"ERROR","ERROR: "+reply->errorString());
        ui->statusBar->clearMessage();
        reply->deleteLater();
        return;
    }

    //Read the data received from NOAA server
    Data=reply->readAll();

    //Save the data into an array and increment the counter
    NOAAWebData[index2][index] = Data;

    //Delete this response
    reply->deleteLater();

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Routine that fires after the NOAA data is
//finished downloading
//-------------------------------------------//
void noaa::PlotNOAAResponse()
{
    QVector<QString> NOAAData,Error;
    QString javascript;
    int i;

    for(i=0;i<CurrentNOAAStation.length();i++)
        CurrentNOAAStation[i].clear();
    CurrentNOAAStation.clear();

    CurrentNOAAStation.resize(NOAAWebData.length());

    NOAAData.resize(NOAAWebData.length());
    Error.resize(NOAAWebData.length());
    for(i=0;i<NOAAWebData.length();i++)
    {
        NOAAData[i] = FormatNOAAResponse(NOAAWebData[i],Error[i],i);
        Error[i].remove(QRegExp("[\\n\\t\\r]"));
        javascript="AddDataSeries("+QString::number(i)+","+NOAAData[i]+",'"+Error[i]+"')";
        ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javascript);
    }

    ui->noaa_map->page()->mainFrame()->evaluateJavaScript("PlotTimeseries()");

    ui->statusBar->clearMessage();

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Routine that formats the response from the
//NOAA server in CSV
//-------------------------------------------//
QString noaa::FormatNOAAResponse(QVector<QByteArray> Input,QString &ErrorString,int index)
{
    int i,j,k;
    int dataCount;
    QString Output,TempData,DateS,YearS,MonthS,DayS,HourMinS,HourS,MinS,WLS;
    QStringList TimeSnap;
    QVector<QString> InputData;
    QVector<QStringList> DataList;
    QVector<QString> Temp;

    InputData.resize(Input.length());
    DataList.resize(Input.length());
    Temp.resize(Input.length());

    for(i=0;i<DataList.length();i++)
    {
        InputData[i] = QString(Input[i]);
        DataList[i] = InputData[i].split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
        Temp[i] = QString(Input[i]);
        ErrorString = Temp[i] + "\n";
    }

    Output = "'";

    dataCount = 0;
    for(i=0;i<DataList.length();i++)
        dataCount = dataCount+DataList[i].length()-1;

    CurrentNOAAStation[index].resize(dataCount);

    k = 0;
    for(j=0;j<DataList.length();j++)
    {
        for(i=1;i<DataList[j].length();i++)
        {
            TempData = DataList[j].value(i);
            TimeSnap = TempData.split(",");
            DateS    = TimeSnap.value(0);
            YearS    = DateS.mid(0,4);
            MonthS   = DateS.mid(5,2);
            DayS     = DateS.mid(8,2);
            HourMinS = DateS.mid(11,5);
            HourS    = HourMinS.mid(0,2);
            MinS     = HourMinS.mid(3,2);
            WLS      = TimeSnap.value(1);
            Output=Output+YearS+":"+MonthS+":"+
                   DayS+":"+HourS+":"+MinS+":"+WLS+";";
            TempData = YearS+"/"+MonthS+"/"+DayS;
            CurrentNOAAStation[index][k].Date.setDate(YearS.toInt(),MonthS.toInt(),DayS.toInt());
            CurrentNOAAStation[index][k].Time = QTime(HourS.toInt(),MinS.toInt(),0);
            CurrentNOAAStation[index][k].value = WLS.toDouble();
            k = k + 1;
        }
    }
    Output = Output+"'";

    return Output;
}
//-------------------------------------------//


//-------------------------------------------//
//Function that picks the product name and
//the product id on the NOAA site by the
//index in the NOAA selection combo box
//-------------------------------------------//
int noaa::retrieveProduct(int type, QString &Product, QString &Product2)
{
    Product2 = "null";
    int index = ui->combo_NOAAProduct->currentIndex();
    if(type==1)
    {
        switch(index)
        {
            case(0):Product = "6 Min Observed Water Level vs. Predicted"; break;
            case(1):Product = "6 Min Observed Water Level"; break;
            case(2):Product = "Hourly Observed Water Level"; break;
            case(3):Product = "Predicted Water Level"; break;
            case(4):Product = "Air Temperature"; break;
            case(5):Product = "Water Temperature"; break;
            case(6):Product = "Wind Speed"; break;
            case(7):Product = "Relative Humidity"; break;
            case(8):Product = "Air Pressure"; break;
        }
    }
    else if(type==2)
    {
        switch(index)
        {
            case(0):Product = "water_level"; Product2 = "predictions"; break;
            case(1):Product = "water_level"; break;
            case(2):Product = "hourly_height"; break;
            case(3):Product = "predictions"; break;
            case(4):Product = "air_temperature"; break;
            case(5):Product = "water_temperature"; break;
            case(6):Product = "wind"; break;
            case(7):Product = "humidity"; break;
            case(8):Product = "air_pressure"; break;
        }
    }
    return 0;
}
//-------------------------------------------//
