/* hwm.h
 *
 * $Author$
 * $Date$
 * $Rev$
 * $HeadURL$
 * $Id$
 *
*/
        
#ifndef HWM_H
#define HWM_H

#include <QMainWindow>
#include <QVector>
#include <QFile>
#include <QMessageBox>
#include <qmath.h>

struct hwm_data
{
    double lat;
    double lon;
    double bathy;
    double measured;
    double modeled;
    double error;
};

int ReadHWMData(QString Filename, QVector<hwm_data> &HWM);
int ComputeLinearRegression(QVector<hwm_data> HWM, double &M, double &Y);

#endif // HWM_H
