#include "crmsdata.h"

CrmsData::CrmsData(Station &station, QDateTime startDate, QDateTime endDate,
                   QObject *parent)
    : WaterData(station, startDate, endDate, parent) {}
