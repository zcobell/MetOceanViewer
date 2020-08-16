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
#include <QColorDialog>
#include <QObject>
#include <QPalette>
#include <QPushButton>
#include <QRandomGenerator>
#include <QTime>

namespace Colors {
QString makeColorString(const QColor &input) {
  return QString("background-color: #" + QString(input.red() < 16 ? "0" : "") +
                 QString::number(input.red(), 16) +
                 QString(input.green() < 16 ? "0" : "") +
                 QString::number(input.green(), 16) +
                 QString(input.blue() < 16 ? "0" : "") +
                 QString::number(input.blue(), 16) + ";");
}

QString getHexColor(const QColor &input) {
  return QString(QString(input.red() < 16 ? "0" : "") +
                 QString::number(input.red(), 16) +
                 QString(input.green() < 16 ? "0" : "") +
                 QString::number(input.green(), 16) +
                 QString(input.blue() < 16 ? "0" : "") +
                 QString::number(input.blue(), 16) + ";");
}

QColor styleSheetToColor(const QString &stylesheet) {
  QColor thisColor;
  QString colorString;

  colorString = stylesheet.split(": ").value(1);
  colorString = colorString.split(";").value(0);
  thisColor.setNamedColor(colorString.simplified());

  return thisColor;
}

QColor generateRandomColor() {
  QRandomGenerator rng(QTime::currentTime().msec());

#if 0
  QColor c;
  QColor mixer;
  Mix.setRed(255);
  Mix.setGreen(255);
  Mix.setBlue(255);

  c.setRed((rng.generate() % 255 + mixer.red()) / 2);
  c.setGreen((rng.generate() % 255 + mixer.green()) / 2);
  c.setBlue((rng.generate()% 255 + mixer.blue()) / 2);
  return c;
#else
  return QColor(rng.generate() % 255, rng.generate() % 255,
                rng.generate() % 255);
#endif
}

void changeButtonColor(QPushButton *button, const QColor color) {
  if (color.isValid()) {
    button->setStyleSheet(QString("background-color: %1").arg(color.name()));
  }
  return;
}

void selectButtonColor(QPushButton *button) {
  QPalette pal = button->palette();
  QColor c = pal.color(QPalette::Button);
  QColor n = QColorDialog::getColor(c);
  if (!n.isValid()) return;
  Colors::changeButtonColor(button, n);
  return;
}

};  // namespace Colors

#endif  // COLORS_H
