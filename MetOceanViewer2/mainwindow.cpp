#include "mainwindow.h"

#include <QDebug>
#include <QPushButton>

#include "generic.h"
#include "newtabdialog.h"
#include "noaatab.h"
#include "tabwidget.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
  ui->setupUi(this);

  Generic::createConfigDirectory();

  //...Create a tab widget and add to the central widget
  this->m_tabWidget = new TabWidget(this);
  ui->centralwidget->layout()->addWidget(this->m_tabWidget);
}

MainWindow::~MainWindow() { delete ui; }
