#ifndef CRMSDATACONTAINER_H
#define CRMSDATACONTAINER_H

#include <string>

class CrmsDataContainer {
 public:
  CrmsDataContainer(size_t size);
  ~CrmsDataContainer();

  std::string id() const;
  void setId(const std::string &id);

  bool valid() const;
  void setValid(bool valid);

  long long datetime() const;
  void setDatetime(long long datetime);

  float value(size_t index) const;
  void setValue(size_t index, float value);

private:
  std::string m_id;
  bool m_valid;
  long long m_datetime;
  float *m_values;
};

#endif  // CRMSDATACONTAINER_H
