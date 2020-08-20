#ifndef TIMEFUNC_H
#define TIMEFUNC_H

#include <QDateTime>

#include "date.h"

namespace Timefunc {
inline Hmdf::Date fromQDateTime(const QDateTime &d) {
  return Hmdf::Date(d.date().year(), d.date().month(), d.date().day(),
                    d.time().hour(), d.time().minute(), d.time().second());
}

inline QDateTime fromDate(const Hmdf::Date &d) {
  return QDateTime(QDate(d.year(), d.month(), d.day()),
                   QTime(d.hour(), d.minute(), d.second()));
}
}  // namespace Timefunc

#endif  // TIMEFUNC_H
