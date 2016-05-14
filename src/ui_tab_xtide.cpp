
#include "MetOceanViewer.h"
#include "ui_MetOceanViewer_main.h"

void MainWindow::on_button_xtide_compute_clicked()
{
    QVariant eval = QVariant();
    ui->xtide_map->page()->runJavaScript("returnStationID()",[&eval](const QVariant &v){eval = v;});
    while(eval.isNull())
        delayM(5);
    QStringList evalList = eval.toString().split(";");

    QEventLoop loop;

    QString xTideExe = QString("/home/zcobell/Development/MetOceanViewer/mov_libs/bin/tide -l \""+evalList.value(0)+"\" -b \"2011-01-01 00:00\" -e \"2011-01-02 00:00\" -s \"00:30\" -z -m m");

    qDebug() << "Running xTide...";

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HFILE_PATH","/home/zcobell/Development/MetOceanViewer/thirdparty/xtide-2.15.1/harmonics.tcd");
    QProcess xTideRun(this);
    xTideRun.setEnvironment(env.toStringList());
    xTideRun.start(xTideExe);
    connect(&xTideRun, SIGNAL(finished(int , QProcess::ExitStatus )), &loop, SLOT(quit()));
    loop.exec();

    QString tideData = xTideRun.readAllStandardOutput();
    qDebug() << "OUT: " << tideData.split("\n");

}
