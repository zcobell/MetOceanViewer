#ifndef READCRMSDATABASE_H
#define READCRMSDATABASE_H

#include <QDateTime>
#include <QObject>
#include <QVector>
#include <fstream>
#include <string>
#include <unordered_map>

class CrmsDatabase : public QObject {
  Q_OBJECT
 public:
  explicit CrmsDatabase(const std::string &datafile,
                        const std::string &outputFile,
                        QObject *parent = nullptr);

 public slots:
  void parse();

 signals:
  void percentComplete(int);
  void complete();
  void success();
  void error();

 private:
  struct Position {
    double latitude;
    double longitude;
  };

  struct CrmsDataContainer {
    std::string id;
    std::string geoid;
    Position location;
    bool valid;
    QDateTime datetime;
    std::vector<float> values;
  };

  double getPercentComplete();
  void readHeader();
  void openCrmsFile();
  void closeCrmsFile();
  CrmsDataContainer splitToCrmsDataContainer(const std::string &line);
  bool getNextStation(std::vector<CrmsDataContainer> &data, bool &finished);
  void putNextStation(size_t stationNumber,
                      std::vector<CrmsDataContainer> &data);
  void initializeOutputFile();
  void closeOutputFile(size_t numStations);
  bool fileExists(const std::string &filename);
  float fillValue() const;
  void exitCleanly();

  std::string m_databaseFile;
  std::string m_outputFile;
  std::ifstream m_file;
  size_t m_geoidIndex;
  int m_ncid;
  bool m_hasError;
  std::vector<std::string> m_dataCategories;
  std::vector<std::string> m_stationNames;
  std::vector<Position> m_stationLocations;
  std::unordered_map<size_t, size_t> m_categoryMap;
  size_t m_fileLength;
};

#endif  // READCRMSDATABASE_H
