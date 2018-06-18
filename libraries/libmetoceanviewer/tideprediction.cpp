#include "tideprediction.h"
#include <QStringList>
#include "libxtide.hh"
#include "timezone.h"

TidePrediction::TidePrediction(QObject *parent) : QObject(parent) {}

int TidePrediction::get(QString stationName, QDateTime startDate,
                        QDateTime endDate, int interval, QVector<qint64> &date,
                        QVector<double> &data) {
  using namespace libxtide;

  const StationRef *sr = Global::stationIndex().getStationRefByName(
      stationName.toStdString().c_str());

  if (sr) {
    std::auto_ptr<Station> station(sr->load());
    station->step = interval;

    startDate.setTime(QTime(0, 0, 0));
    endDate.setTime(QTime(0, 0, 0));

    Timestamp startTime =
        Timestamp(startDate.toString("yyyy-MM-dd hh:mm").toStdString().c_str(),
                  sr->timezone);
    Timestamp endTime =
        Timestamp(endDate.toString("yyyy-MM-dd hh:mm").toStdString().c_str(),
                  sr->timezone);

    station->setUnits(Units::meters);

    Dstr text_out;
    station->print(text_out, startTime, endTime, Mode::mediumRare,
                   Format::text);

    QStringList tide = QString(text_out.aschar()).split("\n");

    for (int i = 0; i < tide.length(); i++) {
      QString datestr = tide[i].mid(0, 20).simplified();
      // QString tz = tide[i].mid(20, 3).simplified(); //(always UTC)
      QString val = tide[i].mid(23, tide[i].length()).simplified();
      QDateTime d = QDateTime::fromString(datestr, "yyyy-MM-dd hh:mm AP");
      d.setTimeSpec(Qt::UTC);
      if (d.isValid()) {
        date.push_back(d.toMSecsSinceEpoch());
        data.push_back(val.toDouble());
      }
    }

    return 0;
  } else {
    return 1;
  }
}
