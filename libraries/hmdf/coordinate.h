/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2018  Zach Cobell
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------*/
#ifndef COORDINATE_H
#define COORDINATE_H

class Coordinate {
 public:
  Coordinate();

  double x() const;
  void setX(double x);
  double longitude() const;
  void setLongitude(double longitude);

  double y() const;
  void setY(double y);
  double latitude() const;
  void setLatitude(double latitude);

 private:
  double m_x;
  double m_y;
};

#endif  // COORDINATE_H
