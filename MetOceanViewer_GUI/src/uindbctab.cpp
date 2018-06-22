#include "mainwindow.h"
#include "ndbc.h"
#include "timezone.h"
#include "ui_mainwindow.h"

void MainWindow::on_button_fetchndbc_clicked() {
  if (this->m_ndbc != nullptr) delete this->m_ndbc;
  this->m_ndbc =
      new Ndbc(ui->quick_ndbcMap, ui->ndbc_graphics, ui->combo_ndbcproduct,
               ui->date_ndbcStarttime, ui->date_ndbcEndtime, ui->statusBar,
               this->ndbcStationModel, &this->ndbcSelectedStation, this);

  this->m_ndbc->plotStation();

  return;
}

void MainWindow::on_combo_ndbcproduct_currentIndexChanged(int index) {
  if (this->m_ndbc != nullptr) {
    this->m_ndbc->replotStation(index);
  }
}
