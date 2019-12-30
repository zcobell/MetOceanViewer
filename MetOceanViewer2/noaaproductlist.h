#ifndef NOAAPRODUCTLIST_H
#define NOAAPRODUCTLIST_H

#include <QStringList>
#include <string>
#include <vector>

class NoaaProductList {
 public:
  struct NoaaProduct {
   public:
    NoaaProduct(const std::string &name,
                const std::vector<std::string> &noaaString,
                const std::vector<std::string> &seriesName,
                const std::string &metricUnits, const std::string &englishUnits,
                const std::string axisLabel, bool useDatum,
                unsigned short nProducts)
        : m_productName(name),
          m_noaaDataString(noaaString),
          m_seriesName(seriesName),
          m_metricUnits(metricUnits),
          m_englishUnits(englishUnits),
          m_axisLabel(axisLabel),
          m_useDatum(useDatum),
          m_nProducts(nProducts) {}

    std::string productName() const { return this->m_productName; }
    std::string noaaDataString(unsigned short index) const {
      return this->m_noaaDataString[index];
    }
    std::string seriesName(unsigned short index) const {
      return this->m_seriesName[index];
    }
    std::string metricUnits() const { return this->m_metricUnits; }
    std::string englishUnits() const { return this->m_englishUnits; }
    std::string axisLabel() const { return this->m_axisLabel; }
    bool useDatum() const { return this->m_useDatum; }
    unsigned short nProducts() const { return this->m_nProducts; }

   private:
    const std::string m_productName;
    const std::vector<std::string> m_noaaDataString;
    const std::vector<std::string> m_seriesName;
    const std::string m_metricUnits;
    const std::string m_englishUnits;
    const std::string m_axisLabel;
    const bool m_useDatum;
    const unsigned short m_nProducts;
  };

  NoaaProductList();
  const NoaaProduct &product(size_t index) const;

  QStringList productList() const;

  size_t nProducts() { return this->m_products.size(); }

 private:
  const std::vector<NoaaProduct> m_products = {
      NoaaProduct("6 minute water level vs predicted water level",
                  {"water_level", "predictions"}, {"Observed", "Predicted"},
                  "meters", "feet", "Water Level", true, 2),
      NoaaProduct("6 minute water level", {"water_level"}, {"Observed"},
                  "meters", "feet", "Water Level", true, 1),
      NoaaProduct("Predicted water level", {"predictions"}, {"Predicted"},
                  "meters", "feet", "Water Level", true, 1),
      NoaaProduct("Hourly water level", {"hourly_height"}, {"Observed"},
                  "meters", "feet", "Water Level", true, 1),
      NoaaProduct("Air temperature", {"air_temperature"}, {"Observed"},
                  "celcius", "fahrenheit", "Temperature", false, 1),
      NoaaProduct("Water temperature", {"water_temperature"}, {"Observed"},
                  "celcius", "fahrenheit", "Temperature", false, 1),
      NoaaProduct("Wind speed", {"wind:speed"}, {"Observed"}, "m/s", "knots",
                  "Wind Speed", false, 1),
      NoaaProduct("Humidity", {"humidity"}, {"Observed"}, "percent", "percent",
                  "Relative Humidity", false, 1),
      NoaaProduct("Air pressure", {"air pressure"}, {"Observed"}, "mb", "mb",
                  "Barometric Pressure", false, 1),
      NoaaProduct("Wind Direction", {"wind:direction"}, {"Observed"}, "deg",
                  "Direction", "deg", false, 1),
      NoaaProduct("Wind Gusts", {"wind:gusts"}, {"Observed"}, "m/s", "knots",
                  "Gust Speed", false, 1)};
};

#endif  // NOAAPRODUCTLIST_H
