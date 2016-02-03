#ifndef GENERAL_FUNCTIONS_H
#define GENERAL_FUNCTIONS_H

#include <QMessageBox>
#include <QColor>
#include <QtCore>
#include <QString>
#include <QDateTime>
#include <QNetworkInterface>

void splitPath(QString input,QString &filename,QString &directory);
void delay(int delayTime);
void delayM(int delayTime);
QString MakeColorString(QColor InputColor);
QColor GenerateRandomColor();
int NETCDF_ERR(int status);
bool isConnectedToNetwork();
double niceNumber(double number, bool round);
QVector<double> niceLabels(double inMin, double inMax, int nTicks, int &nFrac);

#endif // GENERAL_FUNCTIONS_H
