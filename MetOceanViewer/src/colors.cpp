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
#include "colors.h"
#include <QColorDialog>

Colors::Colors(QObject *parent) : QObject(parent) {}

//-------------------------------------------//
// Makes a string that sets the CSS style
// according to the input color
//-------------------------------------------//
QString Colors::makeColorString(QColor InputColor) {
  QString S("background-color: #" + QString(InputColor.red() < 16 ? "0" : "") +
            QString::number(InputColor.red(), 16) +
            QString(InputColor.green() < 16 ? "0" : "") +
            QString::number(InputColor.green(), 16) +
            QString(InputColor.blue() < 16 ? "0" : "") +
            QString::number(InputColor.blue(), 16) + ";");
  return S;
}
//-------------------------------------------//

QString Colors::getHexColor(QColor InputColor) {
  QString S(QString(InputColor.red() < 16 ? "0" : "") +
            QString::number(InputColor.red(), 16) +
            QString(InputColor.green() < 16 ? "0" : "") +
            QString::number(InputColor.green(), 16) +
            QString(InputColor.blue() < 16 ? "0" : "") +
            QString::number(InputColor.blue(), 16) + ";");
  return S;
}

QColor Colors::styleSheetToColor(QString stylesheet) {
  QColor thisColor;
  QString colorString;

  colorString = stylesheet.split(": ").value(1);
  colorString = colorString.split(";").value(0);
  thisColor.setNamedColor(colorString.simplified());

  return thisColor;
}

//-------------------------------------------//
// Generates a random color and optionally
// mixes in white to make it a more pastel
// type color. This is turned off by default
//-------------------------------------------//
QColor Colors::generateRandomColor() {
  QColor MyColor;
  QTime SeedTime;

  SeedTime = QTime::currentTime();
  srand((uint)SeedTime.msec());

#if 0
  QColor Mix;
  Mix.setRed(255);
  Mix.setGreen(255);
  Mix.setBlue(255);

  MyColor.setRed((rand() % 255 + Mix.red()) / 2);
  MyColor.setGreen((rand() % 255 + Mix.green()) / 2);
  MyColor.setBlue((rand() % 255 + Mix.blue()) / 2);
#else
  MyColor.setRed(rand() % 255);
  MyColor.setGreen(rand() % 255);
  MyColor.setBlue(rand() % 255);
#endif

  return MyColor;
}
//-------------------------------------------//

void Colors::selectButtonColor(QPushButton *button) {
  QPalette pal = button->palette();
  QColor c = pal.color(QPalette::Button);
  QColor n = QColorDialog::getColor(c);
  if (!n.isValid()) return;
  Colors::changeButtonColor(button, n);
  return;
}

void Colors::changeButtonColor(QPushButton *button, QColor color) {
  if (!color.isValid()) return;
  QString styleSheet = Colors::makeColorString(color);
  button->setStyleSheet(styleSheet);
  return;
}
