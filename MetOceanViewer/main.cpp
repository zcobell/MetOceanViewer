#include <QApplication>

#include "mainwindow.h"
#include "metocean_init.h"

int main(int argc, char *argv[]) {
  Metocean_init::init();
  QApplication a(argc, argv);
  MainWindow w;
  w.show();
  return a.exec();
}
