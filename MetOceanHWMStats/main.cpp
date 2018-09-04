/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2018  Zach Cobell
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
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <iostream>
#include "highwatermarks.h"
#include "version.h"

int main(int argc, char *argv[]) {
  QCoreApplication a(argc, argv);
  QCoreApplication::setApplicationName("MetOceanHWMStats");
  QCoreApplication::setApplicationVersion(
      QString::fromStdString(metoceanVersion()));

  QCommandLineOption cmd_file =
      QCommandLineOption(QStringList() << "f"
                                       << "filename",
                         "Name of the high water mark file", "file");
  QCommandLineOption cmd_tz = QCommandLineOption(
      QStringList() << "z", "Force the regression through point 0,0.");

  QCommandLineParser p;
  p.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
  p.addHelpOption();
  p.addVersionOption();
  p.addOption(cmd_file);
  p.addOption(cmd_tz);
  p.process(a);

  QString filename;
  if (!p.isSet(cmd_file)) {
    std::cerr << "Error: No file name specified." << std::endl;
    p.showHelp(1);
  } else {
    filename = p.value(cmd_file);
  }

  bool tz;
  if (!p.isSet(cmd_tz)) {
    tz = false;
  } else {
    tz = true;
  }

  HighWaterMarks *h = new HighWaterMarks(filename, tz, &a);
  int ierr = h->read();
  if (ierr == 0) {
    std::cout << "Processed " << h->n() << " high water marks. [Ignored "
              << h->n() - h->nValid() << " dry locations]" << std::endl;
    std::cout << "Regression Line Slope:     " << h->slope() << std::endl;
    std::cout << "Regression Line Intercept: " << h->intercept() << std::endl;
    std::cout << "Correlation (R2):          " << h->r2() << std::endl;
    std::cout << "Standard Deviation:        " << h->standardDeviation() << std::endl;
    std::cout.flush();
    return 0;
  } else {
    std::cerr << "Exit code: " << ierr
              << " Error processing high water mark data." << std::endl;
    return ierr;
  }
}
