#ifndef METOCEAN_H
#define METOCEAN_H

#include <QString>

namespace MetOcean {

QString lastDirectory();

void setLastDirectory(const QString &directory);

qint64 localMachineOffsetFromUTC();

};  // namespace MetOcean

#endif  // METOCEAN_H
