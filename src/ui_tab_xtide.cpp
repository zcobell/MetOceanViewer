
#include "MetOceanViewer.h"
#include "ui_MetOceanViewer_main.h"

void MainWindow::on_button_xtide_compute_clicked()
{
    this->plotXTideStation();
    return;
}


void MainWindow::plotXTideStation()
{
    int ierr;
    QString error;


    //...Display wait cursor
    QApplication::setOverrideCursor(Qt::WaitCursor);

    //...Create an xTide object
    if(!this->thisXTide.isNull())
        delete this->thisXTide;
    this->thisXTide = new XTide(ui->xtide_map,ui->xtide_graphics,ui->date_xtide_start,
                                ui->date_xtide_end,ui->combo_xtide_units,ui->statusBar,this);

    //...Call the plotting routine
    ierr = this->thisXTide->plotXTideStation();


    //...Clear the wait cursor
    QApplication::restoreOverrideCursor();


}

void MainWindow::on_button_xtide_resetzoom_clicked()
{
    if(!this->thisXTide.isNull())
        ui->xtide_graphics->resetZoom();
    return;
}


void MainWindow::on_combo_xtide_panto_activated(const QString &arg1)
{
    ui->xtide_map->page()->runJavaScript("panTo('"+ui->combo_xtide_panto->currentText()+"')");
    return;
}
