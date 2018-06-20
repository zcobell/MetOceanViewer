// $Id: HarmonicsFile.hh 5748 2014-10-11 19:38:53Z flaterco $

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

namespace libxtide {

class HarmonicsFile {
 public:
  // libtcd is stateful and cannot handle multiple harmonics files
  // simultaneously.  XTide currently has no need to open multiple
  // harmonics files simultaneously, so for now, the constructor will
  // barf if the attempt is made to have more than one instance at a
  // time.  If this class is modified to deal with different
  // databases, that trap can be removed.

  HarmonicsFile(const Dstr &filename);
  ~HarmonicsFile();

  // Starting from the beginning of the file, allocate and return a
  // new StationRef for the next station.  Each StationRef gets a
  // reference to the filename Dstr passed in the constructor, so
  // don't destroy it.  Returns NULL on end of file.
  StationRef *const getNextStationRef();

  // Load the reffed station.  This is allowed to invalidate the
  // iterator for getNextStationRef.
  Station *const getStation(const StationRef &stationRef);

  // Version string of the harmonics file.
  const Dstr &versionString();

 protected:
  const Dstr &_filename;
  Dstr _versionString;

 private:
  // Prohibited operations not implemented.
  HarmonicsFile(const HarmonicsFile &);
  HarmonicsFile &operator=(const HarmonicsFile &);
};

}  // namespace libxtide
