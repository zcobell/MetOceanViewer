#include "cdate.h"
#include "boost/format.hpp"
#include "boost/spirit/include/phoenix.hpp"
#include "boost/spirit/include/qi.hpp"

CDate::CDate() {
  this->init();
}

CDate::CDate(long long &seconds) { 
  this->init();
  this->fromSeconds(seconds); 
}

CDate::CDate(long &seconds) { 
   this->init();
   this->fromSeconds(seconds); 
}

CDate::CDate(const std::string &dateString) {
  using namespace boost::phoenix;
  using namespace boost::spirit::qi;
  int year, month, day, hour, minute, second;
  this->init();
  bool r = parse(dateString.begin(), dateString.end(),
                 (int_ >> "/" >> int_ >> "/" >> int_ >> " " >> int_ >> ":" >>
                  int_ >> ":" >> int_ >> ":"),
                 month, day, year, hour, minute, second);
  if (!r) {
    std::cerr << "Error reading date string!" << std::endl;
  } else {
    this->set(year, month, day, hour, minute, second);
  }
}

CDate::CDate(const std::string &dateString, const std::string &timeString) {
  using namespace boost::phoenix;
  using namespace boost::spirit::qi;
  int year, month, day, hour, minute, second;
  this->init();
  bool r1 = parse(dateString.begin(), dateString.end(),
                  (int_ >> "/" >> int_ >> "/" >> int_), month, day, year);
  bool r2 = parse(timeString.begin(), timeString.end(),
                  (int_ >> ":" >> int_ >> ":" >> int_), hour, minute, second);
  if (!r1 || !r2) {
    std::cerr << "Error reading date string!" << std::endl;
  } else {
    this->set(year, month, day, hour, minute, second);
  }
}

void CDate::init(){
  struct tm defaultTime = {70,0,1,0,0,0};
  this->m_date = timegm(&defaultTime);
  this->m_epoch_tm = defaultTime;
  this->m_epoch = this->m_date;
}

void CDate::add(long seconds) {
  this->m_date = this->m_date + seconds;
  this->buildTm();
  this->buildDate();
  return;
}

void CDate::buildTm() {
  this->m_tm = *(gmtime(&this->m_date));
  return;
}

void CDate::buildDate(int year, int month, int day, int hour, int minute,
                      int second) {
  this->m_tm.tm_sec = second;
  this->m_tm.tm_min = minute;
  this->m_tm.tm_hour = hour;
  this->m_tm.tm_mday = day;
  this->m_tm.tm_mon = month - 1;
  this->m_tm.tm_year = year - 1900;
  this->m_date = timegm(&this->m_tm);
  return;
}

void CDate::buildDate() {
  this->m_date = timegm(&this->m_tm);
  return;
}

void CDate::set(int year, int month, int day, int hour, int minute,
                int second) {
  this->buildDate(year, month, day, hour, minute, second);
  this->buildTm();
  return;
}

void CDate::get(int &year, int &month, int &day, int &hour, int &minute,
                int &second) {
  year = this->year();
  month = this->month();
  day = this->day();
  hour = this->hour();
  minute = this->minute();
  second = this->second();
  return;
}

void CDate::fromSeconds(long seconds) {
  this->m_date = this->m_epoch + seconds;
  this->buildTm();
  this->buildDate();
  return;
}

long CDate::toSeconds() { return this->m_date - this->m_epoch; }

int CDate::year() { return this->m_tm.tm_year + 1900; }

void CDate::setYear(int year) {
  this->m_tm.tm_year = year - 1900;
  this->buildDate();
  this->buildTm();
  return;
}

int CDate::month() { return this->m_tm.tm_mon + 1; }

void CDate::setMonth(int month) {
  this->m_tm.tm_mon = month - 1;
  this->buildDate();
  this->buildTm();
  return;
}

int CDate::day() { return this->m_tm.tm_mday; }

void CDate::setDay(int day) {
  this->m_tm.tm_mday = day;
  this->buildDate();
  this->buildTm();
  return;
}

int CDate::hour() { return this->m_tm.tm_hour; }

void CDate::setHour(int hour) {
  this->m_tm.tm_hour = hour;
  this->buildDate();
  this->buildTm();
  return;
}

int CDate::minute() { return this->m_tm.tm_min; }

void CDate::setMinute(int minute) {
  this->m_tm.tm_min = minute;
  this->buildDate();
  this->buildTm();
  return;
}

int CDate::second() { return this->m_tm.tm_sec; }

void CDate::setSecond(int second) {
  this->m_tm.tm_sec = second;
  this->buildDate();
  this->buildTm();
  return;
}

std::string CDate::toString() {
  return boost::str(boost::format("%04i/%02i/%02i %02i:%02i:%02i") %
                    this->year() % this->month() % this->day() % this->hour() %
                    this->minute() % this->second());
}

std::string CDate::dateString(CDate &d) {
  return boost::str(boost::format("%04i/%02i/%02i %02i:%02i:%02i") %
                    d.year() % d.month() % d.day() % d.hour() % d.minute() %
                    d.second());
}
