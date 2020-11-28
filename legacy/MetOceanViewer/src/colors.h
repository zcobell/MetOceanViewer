/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2019  Zach Cobell
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
#ifndef COLORS_H
#define COLORS_H

#include <QColor>
#include <QObject>
#include <QTime>
#include <QPushButton>

class Colors : public QObject {
  Q_OBJECT
public:
  explicit Colors(QObject *parent = nullptr);

  static QString makeColorString(QColor InputColor);

  static QColor generateRandomColor();

  static QColor styleSheetToColor(QString stylesheet);

  static QString getHexColor(QColor InputColor);

  static void selectButtonColor(QPushButton *button);

  static void changeButtonColor(QPushButton *button, QColor color);
};

#endif // COLORS_H
