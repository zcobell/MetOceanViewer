// $Id: HarmonicsFile.cc 5748 2014-10-11 19:38:53Z flaterco $

/*  HarmonicsFile  Hide details of interaction with libtcd.

    Although a few method signatures elsewhere in XTide have been
    optimized for compatibility with libtcd, at least 95% of the work
    of integrating XTide with a different database should be
    concentrated in re-implementing this class.

    Copyright (C) 1998  David Flater w/ contributions by Jan Depner.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "libxtide.hh"
#include "HarmonicsFile.hh"
#include "SubordinateStation.hh"
#include <tcd.h>

namespace libxtide {


static bool haveInstance = false;


HarmonicsFile::HarmonicsFile (const Dstr &filename):
  _filename(filename) {

  // libtcd is stateful and cannot handle multiple harmonics files
  // simultaneously.  XTide currently has no need to open multiple
  // harmonics files simultaneously, so for now, the constructor will
  // barf if the attempt is made to have more than one instance at a
  // time.  If this class is modified to deal with different
  // databases, that trap can be removed.
  assert (!haveInstance);
  haveInstance = true;

  // Do some sanity checks before invoking libtcd.  (open_tide_db just
  // returns false regardless of what the problem was.)
  {
    FILE *fp = fopen (filename.aschar(), "rb");
    if (fp) {
      char c = fgetc (fp);
      if (c != '[') {
        Dstr details (filename);
        details += " is apparently not a TCD file.\n\
We do not use harmonics.txt or offsets.xml anymore.  Please see\n\
http://www.flaterco.com/xtide/files.html for a link to the current data.";
        Global::barf (Error::CORRUPT_HARMONICS_FILE, details);
      }
      fclose (fp);
    } else {
      // This should never happen.  We statted this file in Global.cc.
      Global::cantOpenFile (filename, Error::fatal);
    }
  }

  if (!open_tide_db (_filename.aschar())) {
    Dstr details (_filename);
    details += ": libtcd returned generic failure.";
    Global::barf (Error::CORRUPT_HARMONICS_FILE, details);
  }
  DB_HEADER_PUBLIC db = get_tide_db_header();
  _versionString = _filename;
  {
    int i;
    if ((i = _versionString.strrchr ('/')) != -1)
      _versionString /= (i+1);
  }
  _versionString += ' ';
  _versionString += (char *) db.last_modified;
  _versionString += " / ";
  _versionString += (char *) db.version;
}


const Dstr &HarmonicsFile::versionString() {
  return _versionString;
}


// libtcd's open_tide_db is called each time a HarmonicsFile is
// created.  Intentionally, there is no matching call to
// close_tide_db.  See http://www.flaterco.com/xtide/tcd_notes.html
// and XTide changelog for version 2.6.3.

HarmonicsFile::~HarmonicsFile() {
  assert (haveInstance);
  haveInstance = false;
}


StationRef * const HarmonicsFile::getNextStationRef () {
  TIDE_STATION_HEADER rec;
  long i;
  if ((i = get_next_partial_tide_record (&rec)) == -1) return NULL;
  assert (i >= 0);

  // FIXME:  currents kludge
  // We don't get units in the libtcd header (partial tide record), so this
  // is a kludge that depends on the naming convention.
  char *name = (char *)rec.name;
  bool isCurrent (false);
  size_t l = strlen (name);
  if ((l >= 8) &&
      ((strstr (name, " Current") == name + l - 8) ||
       (strstr (name, " Current "))))
    isCurrent = true;

  StationRef *sr = new StationRef (_filename,
                                   i,
                                   name,
        ((rec.latitude != 0.0 || rec.longitude != 0.0) ?
        Coordinates(rec.latitude, rec.longitude) : Coordinates()),
                                   (char *)get_tzfile(rec.tzfile),
                                   (rec.record_type == REFERENCE_STATION),
                                   isCurrent);
  return sr;
}


static void parse_xfields (MetaFieldVector &metadata,
                           constCharPointer xfields) {
  assert (xfields);
  Dstr x (xfields);
  Dstr linebuf, name, value;
  x.getline (linebuf);
  while (!(linebuf.isNull())) {
    if (linebuf[0] == ' ') {
      if (!(name.isNull())) {
        linebuf /= 1;
        value += '\n';
        value += linebuf;
      }
    } else {
      if (!(name.isNull())) {
        metadata.push_back (MetaField (name, value));
        name = (char *)NULL;
        value = (char *)NULL;
      }
      int i = linebuf.strchr (':');
      if (i > 0) {
        name = linebuf;
        name -= (unsigned)i;
        value = linebuf.ascharfrom((unsigned)i+1);
      }
    }
    x.getline (linebuf);
  }
  if (!(name.isNull()))
    metadata.push_back (MetaField (name, value));
}


// Analogous to datum, levelAdds for hydraulic currents are always in
// knots not knots^2.  The database can specify either knots or
// knots^2 in the sub station record, but there is only one sensible
// interpretation.
static const Units::PredictionUnits levelAddUnits (const TIDE_RECORD &rec) {
  return Units::flatten (Units::parse (get_level_units(rec.level_units)));
}


static void appendOffsetsMetadata (MetaFieldVector &metadata,
				   const TIDE_RECORD &rec) {
  char tmp[80];
  Units::PredictionUnits lu = levelAddUnits(rec);

  metadata.push_back (MetaField ("Min time add",
    rec.min_time_add ? ret_time_neat (rec.min_time_add) : "NULL"));
  sprintf (tmp, "%+2.2f %s", rec.min_level_add, Units::shortName(lu));
  metadata.push_back (MetaField ("Min level add",
    rec.min_level_add ? tmp : "NULL"));
  sprintf (tmp, "%0.3f", rec.min_level_multiply);
  metadata.push_back (MetaField ("Min level mult",
    rec.min_level_multiply > 0.0 ? tmp : "NULL"));

  metadata.push_back (MetaField ("Max time add",
    rec.max_time_add ? ret_time_neat (rec.max_time_add) : "NULL"));
  sprintf (tmp, "%+2.2f %s", rec.max_level_add, Units::shortName(lu));
  metadata.push_back (MetaField ("Max level add",
    rec.max_level_add ? tmp : "NULL"));
  sprintf (tmp, "%0.3f", rec.max_level_multiply);
  metadata.push_back (MetaField ("Max level mult",
    rec.max_level_multiply > 0.0 ? tmp : "NULL"));

  if (Units::isCurrent(lu)) {
    metadata.push_back (MetaField ("Flood begins",
      rec.flood_begins == NULLSLACKOFFSET ? "NULL"
	: ret_time_neat (rec.flood_begins)));
    metadata.push_back (MetaField ("Ebb begins",
      rec.ebb_begins == NULLSLACKOFFSET ? "NULL"
	: ret_time_neat (rec.ebb_begins)));
  }
}


// refStationNativeUnits:  determination of hydraulic vs. regular
// current is made based on units at reference station.  Units are
// flattened for levelAdd offsets.
static void buildMetadata (const StationRef &sr,
                           MetaFieldVector &metadata,
                           const TIDE_RECORD &rec,
                           Units::PredictionUnits refStationNativeUnits,
                           CurrentBearing minCurrentBearing,
                           CurrentBearing maxCurrentBearing) {
  Dstr tmpbuf;

  metadata.push_back (MetaField ("Name", sr.name));
  metadata.push_back (MetaField ("In file", sr.harmonicsFileName));
  if (rec.legalese)
    metadata.push_back (MetaField ("Legalese", get_legalese(rec.legalese)));
  if (rec.station_id_context[0])
    metadata.push_back (MetaField ("Station ID context",
				     rec.station_id_context));
  if (rec.station_id[0])
    metadata.push_back (MetaField ("Station ID", rec.station_id));
  if (rec.date_imported)
    metadata.push_back (MetaField ("Date imported",
				    ret_date(rec.date_imported)));
  sr.coordinates.print (tmpbuf);
  metadata.push_back (MetaField ("Coordinates", tmpbuf));
  metadata.push_back (MetaField ("Country", get_country(rec.country)));
  metadata.push_back (MetaField ("Time zone", sr.timezone));
  metadata.push_back (MetaField ("Native units",
                                 Units::longName(refStationNativeUnits)));
  if (!(maxCurrentBearing.isNull())) {
    maxCurrentBearing.print (tmpbuf);
    metadata.push_back (MetaField ("Flood direction", tmpbuf));
  }
  if (!(minCurrentBearing.isNull())) {
    minCurrentBearing.print (tmpbuf);
    metadata.push_back (MetaField ("Ebb direction", tmpbuf));
  }
  if (rec.source[0])
    metadata.push_back (MetaField ("Source", rec.source));
  metadata.push_back (MetaField ("Restriction",
                                 get_restriction(rec.restriction)));
  if (rec.comments[0])
    metadata.push_back (MetaField ("Comments", rec.comments));
  if (rec.notes[0])
    metadata.push_back (MetaField ("Notes", rec.notes));
  parse_xfields (metadata, rec.xfields);

  switch (rec.header.record_type) {
  case REFERENCE_STATION:
    metadata.push_back (MetaField ("Type",
      (Units::isCurrent(refStationNativeUnits) ?
        (Units::isHydraulicCurrent(refStationNativeUnits) ?
	  "Reference station, hydraulic current" :
 	  "Reference station, current")
        : "Reference station, tide")));
    metadata.push_back (MetaField ("Meridian",
                                   ret_time_neat(rec.zone_offset)));
    if (!Units::isCurrent(refStationNativeUnits))
      metadata.push_back (MetaField ("Datum", get_datum(rec.datum)));
    if (rec.months_on_station)
      metadata.push_back (MetaField ("Months on station",
                                     rec.months_on_station));
    if (rec.last_date_on_station)
      metadata.push_back (MetaField ("Last date on station",
				      ret_date(rec.last_date_on_station)));
    if (rec.expiration_date)
      metadata.push_back (MetaField ("Expiration",
				      ret_date(rec.expiration_date)));
    metadata.push_back (MetaField ("Confidence", rec.confidence));
    break;

  case SUBORDINATE_STATION:
    metadata.push_back (MetaField ("Type",
      (Units::isCurrent(refStationNativeUnits) ?
        (Units::isHydraulicCurrent(refStationNativeUnits) ?
	  "Subordinate station, hydraulic current" :
 	  "Subordinate station, current")
        : "Subordinate station, tide")));
    metadata.push_back (MetaField ("Reference",
                                   get_station(rec.header.reference_station)));
    appendOffsetsMetadata (metadata, rec);
    break;

  default:
    assert (false);
  }
}


// Get constituents from a TIDE_RECORD, adjusting if needed.
static const ConstituentSet getConstituents (const TIDE_RECORD &rec,
					     SimpleOffsets adjustments) {
  assert (rec.header.record_type == REFERENCE_STATION);

  DB_HEADER_PUBLIC db = get_tide_db_header();
  Units::PredictionUnits amp_units
    (Units::parse (get_level_units (rec.level_units)));
  SafeVector<Constituent> constituents;

  // Null constituents are eliminated here.
  for (unsigned i=0; i<db.constituents; ++i)
    if (rec.amplitude[i] > 0.0)
      constituents.push_back (
        Constituent (get_speed(i),
                     db.start_year,
                     db.number_of_years,
		     get_equilibriums(i),
                     get_node_factors(i),
                     Amplitude (amp_units, rec.amplitude[i]),
                     rec.epoch[i]));

  assert (!constituents.empty());

  PredictionValue datum (Units::flatten(amp_units), rec.datum_offset);

  // Normalize the meridian to UTC.
  // To compensate for a negative meridian requires a positive offset.
  // (This adjustment is combined with any that were passed in.)

  // This is the only place where mutable offsets would make even a
  // little bit of sense.
  adjustments = SimpleOffsets (
    adjustments.timeAdd() - Interval(ret_time_neat(rec.zone_offset)),
    adjustments.levelAdd(),
    adjustments.levelMultiply());

  ConstituentSet cs (constituents, datum, adjustments);

  Dstr u (Global::settings["u"].s);
  if (u != "x")
    cs.setUnits (Units::parse (u));

  return cs;
}


static void getTideRecord (uint32_t recordNumber, TIDE_RECORD &rec) {
  require (read_tide_record ((NV_INT32)recordNumber, &rec)
           == (NV_INT32)recordNumber);
  if (Global::settings["in"].c == 'y' &&
      rec.header.record_type == REFERENCE_STATION)
    infer_constituents (&rec);
}


Station * const HarmonicsFile::getStation (const StationRef &stationRef) {
  Station *s = NULL;
  TIDE_RECORD rec;
  getTideRecord (stationRef.recordNumber, rec);

  Dstr note;
  if (rec.notes[0])
    note = rec.notes;
  CurrentBearing minCurrentBearing, maxCurrentBearing;
  bool isDegreesTrue = (!(strcmp((char *)get_dir_units(rec.direction_units),
    "degrees true")));
  if (rec.min_direction >= 0 && rec.min_direction < 360)
    minCurrentBearing = CurrentBearing (rec.min_direction, isDegreesTrue);
  if (rec.max_direction >= 0 && rec.max_direction < 360)
    maxCurrentBearing = CurrentBearing (rec.max_direction, isDegreesTrue);
  Dstr name ((char *)rec.header.name);
  if (rec.legalese) {
    name += " - ";
    name += get_legalese(rec.legalese);
  }

  switch (rec.header.record_type) {
  case REFERENCE_STATION:
    {
      MetaFieldVector metadata;
      buildMetadata (stationRef,
                     metadata,
                     rec,
		     Units::parse(get_level_units(rec.level_units)),
		     minCurrentBearing,
                     maxCurrentBearing);
      s = new Station (name,
                       stationRef,
                       getConstituents (rec, SimpleOffsets()),
                       note,
                       minCurrentBearing,
                       maxCurrentBearing,
                       metadata);
    }
    break;

  case SUBORDINATE_STATION:
    {
      TIDE_RECORD referenceStationRec;
      assert (rec.header.reference_station >= 0);
      getTideRecord (rec.header.reference_station, referenceStationRec);
      Units::PredictionUnits refStationNativeUnits
        (Units::parse(get_level_units(referenceStationRec.level_units)));

      MetaFieldVector metadata;
      buildMetadata (stationRef,
                     metadata,
                     rec,
                     refStationNativeUnits,
		     minCurrentBearing,
                     maxCurrentBearing);

      NullableInterval tempFloodBegins, tempEbbBegins;

      // For these, zero is not the same as null.
      if (rec.flood_begins != NULLSLACKOFFSET)
	tempFloodBegins = Interval (ret_time_neat (rec.flood_begins));
      if (rec.ebb_begins != NULLSLACKOFFSET)
	tempEbbBegins = Interval (ret_time_neat (rec.ebb_begins));

      Units::PredictionUnits lu = levelAddUnits(rec);
      HairyOffsets ho (
	SimpleOffsets (Interval(ret_time_neat(rec.max_time_add)),
		       PredictionValue(lu, rec.max_level_add),
		       rec.max_level_multiply),
	SimpleOffsets (Interval(ret_time_neat(rec.min_time_add)),
		       PredictionValue(lu, rec.min_level_add),
		       rec.min_level_multiply),
	tempFloodBegins,
        tempEbbBegins);

      // If the offsets can be reduced to simple, then we can adjust
      // the constituents and be done with it.  However, in the case
      // of hydraulic currents, level multipliers cannot be treated as
      // simple and applied to the constants because of the square
      // root operation--i.e., it's nonlinear.

      SimpleOffsets so;
      if ((!Units::isHydraulicCurrent(refStationNativeUnits) ||
           ho.maxLevelMultiply() == 1.0) && ho.trySimplify (so))

	// Can simplify.
        s = new Station (name,
                         stationRef,
                         getConstituents (referenceStationRec, so),
                         note,
                         minCurrentBearing,
                         maxCurrentBearing,
                         metadata);

      else

        // Cannot simplify.
        s = new SubordinateStation (name,
                                    stationRef,
                                    getConstituents (referenceStationRec,
                                                     SimpleOffsets()),
            	                    note,
                                    minCurrentBearing,
                                    maxCurrentBearing,
                                    metadata,
                                    ho);
    }
    break;

  default:
    assert (false);
  }

  // If this sanity check is deferred until Graph::draw it has
  // unhealthy consequences (clock windows can be killed while only
  // partially constructed; graph windows can double-barf due to
  // events queued on the X server).
  if (s->minLevelHeuristic() >= s->maxLevelHeuristic())
    Global::barf (Error::ABSURD_OFFSETS, s->name);

  return s;
}

}
