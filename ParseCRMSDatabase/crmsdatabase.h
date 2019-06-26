#ifndef READCRMSDATABASE_H
#define READCRMSDATABASE_H

#include <QDateTime>
#include <QObject>
#include <QVector>
#include <fstream>
#include <string>

class CrmsDatabase : public QObject {
  Q_OBJECT
 public:
  explicit CrmsDatabase(const std::string &datafile,
                        const std::string &outputFile,
                        QObject *parent = nullptr);

  int parse();

 private:
  struct CrmsDataContainer {
    std::string id;
    QDateTime datetime;
    std::vector<double> values;
  };

  void readHeader();
  void openCrmsFile();
  void closeCrmsFile();
  CrmsDataContainer splitToCrmsDataContainer(const std::string &line);
  void getNextStation(std::vector<CrmsDataContainer> &data);
  void putNextStation(int ncid, size_t stationNumber,
                      std::vector<CrmsDataContainer> &data);
  void initializeOutputFile(int &ncid);
  void closeOutputFile(int ncid, size_t numStations);
  bool fileExists(const std::string &filename);
  double fillValue() const;

  std::string m_databaseFile;
  std::string m_outputFile;
  std::ifstream m_file;
  std::vector<std::string> m_dataCategories;
  std::vector<std::string> m_stationNames;
};

#endif  // READCRMSDATABASE_H
