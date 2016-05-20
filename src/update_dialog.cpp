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
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "update_dialog.h"
#include "ui_update_dialog.h"
#include "version.h"

bool operator>(const update_dialog::gitVersion version1, const update_dialog::gitVersion version2)
{
    if(version1.versionMajor > version2.versionMajor)
        return true;
    else if(version1.versionMajor < version2.versionMajor)
        return false;
    else
    {
        if(version1.versionMinor > version2.versionMinor)
            return true;
        else if(version1.versionMinor < version2.versionMinor)
            return false;
        else
        {
            if(version1.versionRev > version2.versionRev)
                return true;
            else if(version1.versionRev < version2.versionRev)
                return false;
            else
            {
                if(version1.versionDev > version2.versionDev)
                    return true;
                else if(version1.versionDev < version2.versionDev)
                    return false;
                else
                {
                    if(version1.versionGit > version2.versionGit)
                        return true;
                    else
                        return false;
                }
            }

        }
    }
}

bool operator<(const update_dialog::gitVersion version1, const update_dialog::gitVersion version2)
{
    if(version1.versionMajor < version2.versionMajor)
        return true;
    else if(version1.versionMajor > version2.versionMajor)
        return false;
    else
    {
        if(version1.versionMinor < version2.versionMinor)
            return true;
        else if(version1.versionMinor > version2.versionMinor)
            return false;
        else
        {
            if(version1.versionRev < version2.versionRev)
                return true;
            else if(version1.versionRev > version2.versionRev)
                return false;
            else
            {
                if(version1.versionDev < version2.versionDev)
                    return true;
                else if(version1.versionDev > version2.versionDev)
                    return false;
                else
                {
                    if(version1.versionGit < version2.versionGit)
                        return true;
                    else
                        return false;
                }
            }

        }
    }
}

bool operator==(const update_dialog::gitVersion version1, const update_dialog::gitVersion version2)
{
    if(version1.versionMajor == version2.versionMajor &&
       version1.versionMinor == version2.versionMinor &&
       version1.versionRev   == version2.versionRev   &&
       version1.versionDev   == version2.versionDev   &&
       version1.versionGit   == version2.versionGit)
        return true;
    else
        return false;
}


update_dialog::update_dialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::update_dialog)
{
    ui->setupUi(this);

}

update_dialog::~update_dialog()
{
    delete ui;
}

int update_dialog::getLatestVersionData()
{
    QNetworkAccessManager manager;
    QEventLoop loop;
    QUrl versionURL;
    QByteArray responseBytes;
    QString response;

    //...Get the update list
    versionURL = QUrl("https://raw.githubusercontent.com/zcobell/MetOceanViewer/master/mov_release_revision.txt");
    QNetworkReply *reply = manager.get(QNetworkRequest(versionURL));
    connect(reply,SIGNAL(finished()),&loop,SLOT(quit()));
    connect(reply,SIGNAL(error(QNetworkReply::NetworkError)),&loop,SLOT(quit()));
    loop.exec();

    //...Allow no error or unknown content, which is a the server being picky about content headers
    if(reply->error()==QNetworkReply::NoError||reply->error()==QNetworkReply::UnknownContentError)
    {
        responseBytes = reply->readAll();
        response = QString(responseBytes).simplified();

        this->latestVersionDate = QDateTime(QDate::fromString(response.split(",").value(0),"MM/dd/yyyy"),QTime(0,0,0));
        this->latestVersion = response.split(",").value(1);
        this->latestVersionURL = response.split(",").value(2);
        this->networkError = false;
        return 0;
    }
    else
    {
        this->networkError = true;
        return -1;
    }
    return 0;
}

void update_dialog::parseUpdateData()
{
    int ierr;
    gitVersion versionMe,versionWeb;

    ierr = parseGitVersion(this->currentVersion,versionMe);
    ierr = parseGitVersion(this->latestVersion,versionWeb);

    if(versionMe<versionWeb)
        this->hasNewVersion = true;
    else
        this->hasNewVersion = false;

    return;
}

void update_dialog::runUpdater()
{
    int ierr;

    this->hasNewVersion = false;

    //...Check if the version is available from Git,
    //   if not, fall back on version.h
    if(QString(GIT_VERSION)!=QString())
        this->currentVersion = QString(GIT_VERSION);
    else
        this->currentVersion = QString(VER_FILEVERSION_STR);

    ierr = this->getLatestVersionData();

    if(ierr==0)
    {
        this->parseUpdateData();
        this->setDialogText();
    }
    else
        this->setDialogText();

    return;

}

bool update_dialog::checkForUpdate()
{
    int ierr;

    this->currentVersion = QString(GIT_VERSION);

    ierr = this->getLatestVersionData();

    this->hasNewVersion = false;

    if(ierr==0)
        this->parseUpdateData();

    return this->hasNewVersion;

}

int update_dialog::parseGitVersion(QString versionString, gitVersion &version)
{
    QString v1,v2,v3;
    QStringList v1L,v2L;
    QString temp,alphaBeta;

    QStringList versionStringList = versionString.split("-");

    version.versionMajor = 0;
    version.versionMinor = 0;
    version.versionRev   = 0;
    version.versionDev   = 0;
    version.versionGit   = 0;

    //...Get the major revisions parsed
    v1 = versionStringList.value(0);
    v1 = v1.mid(1,v1.length());
    v1L = v1.split(".");
    temp = v1L.value(0);
    version.versionMajor = temp.toInt();
    temp = v1L.value(1);
    version.versionMinor = temp.toInt();
    temp = v1L.value(2);
    version.versionRev = temp.toInt();

    if(versionStringList.length()==4)
    {
        v2 = versionStringList.value(1);
        v3 = versionStringList.value(2);

        //...This is a version with a beta number in it
        //   set "alphas" to n+100 and "betas" to n+200
        v2L = v2.split(".");
        temp = v2L.value(0);
        alphaBeta = v2L.value(0);
        temp = v2L.value(1);
        if(alphaBeta=="alpha")
            version.versionDev = 100+temp.toInt();
        else if(alphaBeta=="beta")
            version.versionDev = 200+temp.toInt();
        else
            version.versionDev = 1000;

        version.versionGit = v3.toInt();
    }
    else if(versionStringList.length()==3)
    {
        //...This is a version without a beta number in it. Set those to zero
        version.versionDev = 0;
        v3 = versionStringList.value(1);
        version.versionGit = v3.toInt();

    }
    else if(versionStringList.length()==2)
    {
        //...This is a version with just a beta number and major revision
        version.versionGit = 0;
        //...This is a version with a beta number in it
        //   set "alphas" to n+100 and "betas" to n+200
        //   if no version number, make it a large number
        //   to show it is newer than something with a beta tag
        v2 = versionStringList.value(1);
        v2L = v2.split(".");
        temp = v2L.value(0);
        alphaBeta = v2L.value(0);
        temp = v2L.value(1);
        if(alphaBeta=="alpha")
            version.versionDev = 100+temp.toInt();
        else if(alphaBeta=="beta")
            version.versionDev = 200+temp.toInt();
        else
            version.versionDev = 1000;
    }
    else if(versionStringList.length()==1)
    {
        //...This is a version without any additional versioning and is "pure"
        version.versionDev = 0;
        version.versionGit = 0;
    }

    return 0;
}

void update_dialog::setDialogText()
{
    QString dialogHTML;

    if(!this->networkError)
    {
        if(this->hasNewVersion)
            dialogHTML = "A new version of MetOcean Viewer is available for download <a href=\""+this->latestVersionURL+"\">here</a><br><br>";
        else
            dialogHTML = dialogHTML + "Your version of MetOcean Viewer is up to date. <br><br>";
        dialogHTML = dialogHTML +
                QString("<table>")+
                    QString("<tr>")+
                        QString("<td align=\"right\"> <b>Current Revision:</b> </td><td align=\"left\">")+this->currentVersion+QString("</td>")+
                    QString("</tr>")+
                    QString("<tr>")+
                        QString("<td align=\"right\"> <b>Latest Release:</b> </td><td align=\"left\">")+this->latestVersion+
                                " (Released: "+this->latestVersionDate.toString("MM/dd/yyyy)")+QString("</td>")+
                    QString("</tr>")+
                QString("</table>")+
                QString("<br><br>");
    }
    else
    {
        dialogHTML = QString("<br><br><table>")+
                    QString("<tr>")+
                        QString("<td align=\"right\"> <b>Current Revision:</b> </td><td align=\"right\">")+this->currentVersion+QString("</td>")+
                    QString("</tr>")+
                    QString("<tr>")+
                        QString("<td align=\"right\"> <b>Latest Revision:</b> </td><td align=\"right\">")+"Unknown"+QString("</td>")+
                    QString("</tr>")+
                QString("</table>")+
                QString("<br><br>")+
                QString("<b>ERROR:</b> The update server could not be contacted successfully.");
    }

    ui->text_update->setHtml(dialogHTML);

    return;
}

void update_dialog::on_button_ok_clicked()
{
    this->accept();
}
