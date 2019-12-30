#include <QApplication>

#include "mainwindow.h"

int main(int argc, char *argv[]) {
  Q_INIT_RESOURCE(resource_files);
  QApplication a(argc, argv);
  MainWindow w;
  w.show();
  return a.exec();
}
