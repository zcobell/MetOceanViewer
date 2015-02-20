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
            QString(VER_LEGALCOPYRIGHT_STR) + "<br><br>" +
            "<b>Version: </b> "+QString(VER_FILEVERSION_STR) + "<br><br>" +
            "<b>Subversion Revision: </b>"+QString(SVER) + "<br><br>" +
            "<b>External Libraries</b> <br><br>" +
            "Google Maps v3.x API - http://maps.google.com <br>" +
            "HighCharts v4.0.1 - http://www.highcharts.com/ <br>" +
            "netCDF v4.3.3 - http://www.unidata.ucar.edu/software/netcdf/ <br>" +
            "HDF5 v1.8.14 - http://www.hdfgroup.org/HDF5/ <br>" +
            "Curl v7.35.0 - http://curl.haxx.se/ <br>" +
            "zlib v1.2.8 - http://www.zlib.net/" +
            "</body></html>";

    return text;
}
