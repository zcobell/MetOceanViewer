// $Id: xxGlobal.cc 5749 2014-10-11 19:42:10Z flaterco $	

/*  xxGlobal  xx additions to Global.

    Copyright (C) 1998  David Flater.

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

#include "xtide.hh"

#ifdef GPS_GOOD
#include <gps.h>
#endif

xxRoot *xxroot;


constString WVSdir() {
  static bool firstCall (true);
  static Dstr wvs_dir;
  if (firstCall) {
    firstCall = false;
    wvs_dir = getenv ("WVS_DIR");
    if (wvs_dir.isNull())
      wvs_dir = Global::getXtideConf(1U);
  }
  return wvs_dir.aschar();
}


void PositioningSystem (Coordinates &currentPosition) {
  currentPosition.makeNull();

#ifdef GPS_GOOD
  static struct gps_data_t gpsdata;
  static bool firstCall (true), gps_on (false);

  if (firstCall) {
    firstCall = false;
    if (gps_open ("127.0.0.1", "2947", &gpsdata) == 0) {
      if (gps_stream (&gpsdata, WATCH_ENABLE|WATCH_NEWSTYLE, NULL) != 0) {
	fprintf (stderr, "Warning: gps_stream failed; giving up on GPS\n");
        gps_close (&gpsdata);
      } else
        gps_on = true;
    }
    // Fail silently if gpsd not found
  }

  if (gps_on) {
    // gpsd-2.95, old fix keeps coming back if gpsd is killed.
    // This doesn't help.
    // gpsdata->status = STATUS_NO_FIX;
    // gps_clear_fix (&gpsdata->fix);
    time_t before (time(NULL));
    while (time(NULL) < before+2) {
      if (gps_read (&gpsdata) < 0) {
	fprintf (stderr, "Warning: gps_read failed; giving up on GPS\n");
        gps_close (&gpsdata);
	gps_on = false;
	return;
      }
      if (gpsdata.fix.mode >= MODE_2D) {
        const double &lat (gpsdata.fix.latitude);
        const double &lng (gpsdata.fix.longitude);
        if (!(isnan(lat) || isnan(lng))) {
          if (lat < -90.0 || lat > 90.0 || lng < -180.0 || lng > 180.0)
            fprintf (stderr, "Warning: GPS returned bogus fix (%f, %f)\n",
                     lat, lng);
          else
    	    currentPosition = Coordinates (lat, lng);
          return;
        }
      }
    }
    fprintf (stderr, "Warning: timeout waiting on fix from GPS\n");
  }
#endif
}
