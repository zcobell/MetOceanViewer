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
#ifndef DATE_H
#define DATE_H

#include <ctime>
#include <string>

class CDate {
 public:
  explicit CDate();
  CDate(long &seconds);
  CDate(long long &seconds);
  CDate(const std::string &dateString);
  CDate(const std::string &dateString, const std::string &timeString);

  void add(long seconds);

  void get(int &year, int &month, int &day, int &hour, int &minute,
           int &second);

  void set(int year, int month, int day, int hour, int minute, int second);

  void fromSeconds(long seconds);

  long toSeconds();

  int year();
  void setYear(int year);

  int month();
  void setMonth(int month);

  int day();
  void setDay(int day);

  int hour();
  void setHour(int hour);

  int minute();
  void setMinute(int month);

  int second();
  void setSecond(int second);

  std::string toString();

  static std::string dateString(CDate &d);

 private:
  void init();
  void buildTm();
  void buildDate(int year, int month, int day, int hour, int minute,
                 int second);
  void buildDate();

  time_t m_date;
  time_t m_epoch;
  struct tm m_tm;
  struct tm m_epoch_tm;
};
#endif
