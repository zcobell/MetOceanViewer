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
#ifndef MOV_QWEBENGINEPAGE_H
#define MOV_QWEBENGINEPAGE_H

#include <QDesktopServices>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QtCharts>

using namespace QtCharts;

class MovQWebEnginePage : public QWebEnginePage {
  Q_OBJECT

public:
  MovQWebEnginePage(QObject *parent = 0) : QWebEnginePage(parent) {}

  bool acceptNavigationRequest(const QUrl &url,
                               QWebEnginePage::NavigationType type,
                               bool isMainFrame) {
    Q_UNUSED(isMainFrame);
    if (type == QWebEnginePage::NavigationTypeLinkClicked) {
      QDesktopServices::openUrl(url);
      return false;
    }
    return true;
  }
};

#endif // MOV_QWEBENGINEPAGE_H
