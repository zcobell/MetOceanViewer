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
#include <QCoreApplication>
#include <QTimer>
#include <iostream>
#include "metoceandata.h"
#include "options.h"
#include "version.h"

int main(int argc, char *argv[]) {
  QCoreApplication a(argc, argv);
  QCoreApplication::setApplicationName("MetOceanData");
  QCoreApplication::setApplicationVersion(
      QString::fromStdString(metoceanVersion()));

  Q_INIT_RESOURCE(resource_files);

  Options *option = new Options(&a);

  if (a.arguments().length() == 1) {
    std::cerr << "Error: No command line arguments detected." << std::endl;
    std::cerr.flush();
    option->parser()->showHelp(1);
  }

  option->processOptions();
  Options::CommandLineOptions opt = option->getCommandLineOptions();
  MetOceanData *d;
  d = new MetOceanData(opt.service, opt.station, opt.product, opt.parameterId,
                       opt.vdatum, opt.datum, opt.startDate, opt.endDate,
                       opt.outputFile, &a);
  d->setLoggingActive();
  QObject::connect(d, SIGNAL(finished()), &a, SLOT(quit()));
  QTimer::singleShot(0, d, SLOT(run()));

  return a.exec();
}
