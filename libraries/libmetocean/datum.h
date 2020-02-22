#ifndef VDATUM_H
#define VDATUM_H

#include <QString>
#include <QStringList>

namespace Datum {
enum VDatum { NullDatum, MLLW, MLW, MSL, MHW, MHHW, NGVD29, NAVD88 };

static QStringList noaaDatumList() {
  return {"MHHW", "MHW", "MTL", "MSL",  "MLW", "MLLW",
          "NAVD", "LWI", "HWI", "IGLD", "STND"};
}

static QStringList vDatumList() {
  return {"MHHW", "MHW", "MSL", "MLW", "MLLW", "NGVD29", "NAVD88"};
}

static QString datumName(VDatum datum) {
  if (datum == NullDatum)
    return "none";
  else if (datum == MLLW)
    return "MLLW";
  else if (datum == MLW)
    return "MLW";
  else if (datum == MSL)
    return "MSL";
  else if (datum == MHW)
    return "MHW";
  else if (datum == MHHW)
    return "MHHW";
  else if (datum == NGVD29)
    return "NGVD29";
  else if (datum == NAVD88)
    return "NAVD88";
  else
    return "none";
}

static VDatum datumID(QString datum) {
  if (datum == "none")
    return NullDatum;
  else if (datum == "MLLW")
    return MLLW;
  else if (datum == "MLW")
    return MLW;
  else if (datum == "MSL")
    return MSL;
  else if (datum == "MHHW")
    return MHHW;
  else if (datum == "NAVD88")
    return NAVD88;
  else if (datum == "NGVD29")
    return NGVD29;
  else
    return NullDatum;
}

}  // namespace Datum

#endif  // VDATUM_H
