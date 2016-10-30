// $Id: Interval.cc 5748 2014-10-11 19:38:53Z flaterco $

// Interval:  what you get if you subtract two timestamps.

/*
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

#include "libxtide.hh"
namespace libxtide {


Interval::Interval (interval_rep_t s):
  seconds(s) {}


Interval::Interval ():
  seconds(0) {}


Interval::Interval (const NullableInterval &interval):
  seconds(interval.s()) {}


// Takes meridian string of the form [-]HH:MM
Interval::Interval (const Dstr &meridian) {
  int h, m;
  char s;
  char *hhmm = meridian.aschar();
  if (sscanf (hhmm, "%d:%d", &h, &m) != 2) {
    Dstr details ("The offending string was ");
    details += meridian;
    Global::barf (Error::BADHHMM, details);
  }
  if (sscanf (hhmm, "%c", &s) != 1) {
    Dstr details ("The offending string was ");
    details += meridian;
    Global::barf (Error::BADHHMM, details);
  }
  if (h < 0 || s == '-')
    m = -m;
  seconds = h*HOURSECONDS + m*60;
}


const Interval operator* (Interval a, unsigned b) {
  return Interval (a.s() * b);
}


const Interval operator* (Interval a, double b) {
  // Truncation toward zero is fine here.
  return Interval ((interval_rep_t)(a.s() * b));
}


const Interval abs (Interval a) {
  if (a.s() < 0)
    return -a;
  return a;
}


const Interval operator- (Interval a) {
  return Interval (-a.s());
}


const bool operator> (Interval a, Interval b) {
  return (a.s() > b.s());
}


const bool operator< (Interval a, Interval b) {
  return (a.s() < b.s());
}


const bool operator<= (Interval a, Interval b) {
  return (a.s() <= b.s());
}


const bool operator>= (Interval a, Interval b) {
  return (a.s() >= b.s());
}


const bool operator== (Interval a, Interval b) {
  return (a.s() == b.s());
}


const bool operator!= (Interval a, Interval b) {
  return (a.s() != b.s());
}


const Interval operator/ (Interval a, int b) {
  return Interval (a.s() / b);
}


const double operator/ (Interval a, Interval b) {
  return ((double)(a.s()) / (double)(b.s()));
}


const Interval operator+ (Interval a, Interval b) {
  return Interval (a.s() + b.s());
}


const Interval operator- (Interval a, Interval b) {
  return Interval (a.s() - b.s());
}


void Interval::operator*= (unsigned a) {
  seconds *= a;
}

}
