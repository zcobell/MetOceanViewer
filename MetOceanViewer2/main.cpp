#include <QApplication>

#include "mainwindow.h"

int main(int argc, char *argv[]) {
  QApplication a(argc, argv);
  MainWindow w;
  Q_INIT_RESOURCE(resource_files);
  w.show();
  return a.exec();
}
