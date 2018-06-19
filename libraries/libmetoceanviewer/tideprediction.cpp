#include "tideprediction.h"
#include <QFile>
#include <QStringList>
#include "libxtide.hh"
#include "station.h"
#include "timezone.h"

TidePrediction::TidePrediction(QString root, QObject *parent)
    : QObject(parent) {
  this->m_harmonicsDatabase = root + "/harmonics.tcd";
  this->initHarmonicsDatabase();
}

TidePrediction::~TidePrediction() {
  if (this->m_deleteHarmonicsOnExit) {
    QFile file(this->m_harmonicsDatabase);
    if (file.exists()) file.remove();
  }
  return;
}

void TidePrediction::initHarmonicsDatabase() {
  QFile harm(this->m_harmonicsDatabase);
  if (!harm.exists()) {
    Q_INIT_RESOURCE(resource_files);
    QFile::copy(":/rsc/harmonics.tcd", this->m_harmonicsDatabase);
  }
  return;
}

void TidePrediction::deleteHarmonicsOnExit(bool b) {
  this->m_deleteHarmonicsOnExit = b;
}

int TidePrediction::get(Station s, QDateTime startDate, QDateTime endDate,
                        int interval, Hmdf *data) {
  HmdfStation *st = new HmdfStation(data);

  st->setName(s.name());
  st->setId(s.id());
  st->setCoordinate(s.coordinate());
  st->setStationIndex(0);

  const libxtide::StationRef *sr =
      libxtide::Global::stationIndex(
          this->m_harmonicsDatabase.toStdString().c_str())
          .getStationRefByName(s.name().toStdString().c_str());

  if (sr) {
    std::auto_ptr<libxtide::Station> station(sr->load());

    station->step = interval;

    startDate.setTime(QTime(0, 0, 0));
    endDate.setTime(QTime(0, 0, 0));

    libxtide::Timestamp startTime =
        libxtide::Timestamp(startDate.toString("yyyy-MM-dd hh:mm").toStdString().c_str(),
                  sr->timezone);
    libxtide::Timestamp endTime =
        libxtide::Timestamp(endDate.toString("yyyy-MM-dd hh:mm").toStdString().c_str(),
                  sr->timezone);

    station->setUnits(libxtide::Units::meters);

    Dstr text_out;
    station->print(text_out, startTime, endTime, libxtide::Mode::mediumRare,
                   libxtide::Format::text);

    QStringList tide = QString(text_out.aschar()).split("\n");

    for (int i = 0; i < tide.length(); i++) {
      QString datestr = tide[i].mid(0, 20).simplified();
      // QString tz = tide[i].mid(20, 3).simplified(); //(always UTC)
      QString val = tide[i].mid(23, tide[i].length()).simplified();
      QDateTime d = QDateTime::fromString(datestr, "yyyy-MM-dd h:mm AP");
      d.setTimeSpec(Qt::UTC);
      if (d.isValid()) {
        st->setNext(d.toMSecsSinceEpoch(), val.toDouble());
      }

      st->setIsNull(false);
      data->addStation(st);
    }

    return 0;
  } else {
    return 1;
  }
}
