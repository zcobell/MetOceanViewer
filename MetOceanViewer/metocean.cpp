#include "metocean.h"

#include <QDir>
#include <QDateTime>

#ifdef __WIN32
static QString s_lastDirectory = QDir::homePath()+"/Desktop";
#else
static QString s_lastDirectory = QDir::homePath();
#endif

QString MetOcean::lastDirectory() { return s_lastDirectory; }

void MetOcean::setLastDirectory(const QString &directory) {
  s_lastDirectory = directory;
}

qint64 MetOcean::localMachineOffsetFromUTC(){
    QDateTime d = QDateTime::currentDateTime();
    return d.offsetFromUtc();
}
