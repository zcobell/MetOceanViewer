/* main.cpp
 *
 * $Author$
 * $Date$
 * $Rev$
 * $HeadURL$
 * $Id$
 *
*/
        
#include "ADCvalidator.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
