// $Id: xxGlobal.hh 5749 2014-10-11 19:42:10Z flaterco $

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

class xxRoot;

// These few things used to be merged into the Global namespace, but that is
// now owned by libxtide.  Global::root was referenced everywhere.  To avoid
// a likely name clash, renamed that to xxroot.

extern xxRoot *xxroot;

// The remaining three things are used only by the location choosers.

// World Vector Shoreline decoder (in wvsrtv.cc).
int wvsrtv (constString WVSfileName,
	    constString WVSdir,
	    int latitudeDegrees,
	    int longitudeDegrees,
	    float **latitudes_out,    // C style array of floats out
	    float **longitudes_out,   // C style array of floats out
	    int **segmentCounts_out); // C style array of ints out

// The WVS path is resolved the first time it is needed.  Returns empty
// string if no information is available (never NULL).
constString WVSdir();

// GPS only used for start position in location chooser.  Returns null if
// GPS is unsupported, unavailable, or not giving a fix at the moment.
void PositioningSystem (Coordinates &currentPosition);
