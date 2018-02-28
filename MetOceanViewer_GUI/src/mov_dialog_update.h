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
#ifndef MOV_DIALOG_UPDATE_H
#define MOV_DIALOG_UPDATE_H

#include <QDateTime>
#include <QDialog>

namespace Ui {
class mov_dialog_update;
}

class mov_dialog_update : public QDialog {
  Q_OBJECT

public:
  explicit mov_dialog_update(QWidget *parent = nullptr);
  ~mov_dialog_update();

  void runUpdater();
  bool checkForUpdate();

  struct gitVersion {
    int versionMajor;
    int versionMinor;
    int versionRev;
    int versionDev;
    int versionGit;
  };

private slots:
  void on_button_ok_clicked();

private:
  Ui::mov_dialog_update *ui;

  int getLatestVersionData();
  int parseGitVersion(QString versionString, gitVersion &version);
  void parseUpdateData();
  void setDialogText();

  QString currentVersion;
  QString latestVersion;
  QDateTime latestVersionDate;
  QString latestVersionURL;
  bool networkError;
  bool hasNewVersion;
};

#endif // MOV_UPDATE_DIALOG_H
