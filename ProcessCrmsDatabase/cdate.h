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
