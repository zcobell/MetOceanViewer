// $Id: Units.hh 5748 2014-10-11 19:38:53Z flaterco $
/*
    Units

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

class Dstr;
namespace libxtide {

namespace Units {

  // Zulu units are a special case used only for the initial state of
  // PredictionValues (0 Zulu).  They are not permissible as input to
  // any of the functions below.
  enum PredictionUnits {feet=0,
                        meters=1,
                        knots=2,
                        knotsSquared=3,
                        zulu=4};

  enum AngleUnits {degrees=0, radians=1};

  constString shortName (PredictionUnits u);
  constString longName  (PredictionUnits u);
  const PredictionUnits parse   (const Dstr &unitsName);
  const bool isCurrent          (PredictionUnits u);
  const bool isHydraulicCurrent (PredictionUnits u);

  // Demote hydraulic current units; return others unchanged.
  const PredictionUnits flatten (PredictionUnits u);
}

}
