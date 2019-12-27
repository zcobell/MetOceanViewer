#include "mainwindow.h"

#include <QDebug>
#include <QPushButton>

#include "newtabdialog.h"
#include "noaatab.h"
#include "tabwidget.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
  ui->setupUi(this);

  //...Initialize the background image
  ui->centralwidget->setStyleSheet(
      "#centralwidget{"
      "background-image: url(:/rsc/img/mov.png);"
      "background-repeat: no-repeat; "
      "background-attachment: fixed; "
      "background-position: center;"
      "}");

  //...Create a tab widget and add to the central widget
  this->m_tabWidget = new TabWidget(this);
  ui->centralwidget->layout()->addWidget(this->m_tabWidget);

}

MainWindow::~MainWindow() { delete ui; }
