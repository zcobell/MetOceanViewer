#ifndef VDATUM_H
#define VDATUM_H

#include <array>
#include <string>
#include <string_view>

namespace Datum {
enum VDatum { NullDatum, MLLW, MLW, MSL, MHW, MHHW, NGVD29, NAVD88 };

constexpr std::array<std::string_view, 11> noaaDatumList() {
  return {"MHHW", "MHW", "MTL", "MSL",  "MLW", "MLLW",
          "NAVD", "LWI", "HWI", "IGLD", "STND"};
}

constexpr std::array<std::string_view, 8> vDatumList() {
  return {"none", "MLLW", "MLW", "MSL", "MHW", "MHHW", "NGVD29", "NAVD88"};
}

static std::string datumName(const VDatum &datum) {
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

static VDatum datumID(const std::string &datum) {
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
