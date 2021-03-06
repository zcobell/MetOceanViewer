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
#ifndef CRMSDATABASE_H
#define CRMSDATABASE_H

#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "boost/progress.hpp"
#include "crmsdatacontainer.h"

class CrmsDatabase {
 public:
  explicit CrmsDatabase(const std::string &datafile,
                        const std::string &outputFile);

  bool showProgressBar() const;
  void setShowProgressBar(bool showProgressBar);

  static constexpr float fillValue() { return -9999.0f; }

  void parse();

 private:
  double getPercentComplete();
  void readHeader();
  void openCrmsFile();
  void prereadCrmsFile(std::vector<std::string> &stationNames,
                       std::vector<size_t> &stationLengths);
  void closeCrmsFile();
  CrmsDataContainer *splitToCrmsDataContainer(const std::string &line);
  bool getNextStation(std::vector<CrmsDataContainer *> &data, bool &finished);
  void putNextStation(std::vector<CrmsDataContainer *> &data, int varid_data,
                      int varid_time);
  void initializeOutputFile(std::vector<std::string> &stationNames,
                            std::vector<size_t> &length,
                            std::vector<int> &varid_data,
                            std::vector<int> &varid_time);
  void closeOutputFile(size_t numStations);
  bool fileExists(const std::string &filename);
  void deleteCrmsObjects(const std::vector<CrmsDataContainer *> &data);

  std::string m_databaseFile;
  std::string m_outputFile;
  std::ifstream m_file;
  size_t m_geoidIndex;
  int m_ncid;
  size_t m_maxLength;
  bool m_showProgressBar;
  unsigned long m_previousPercentComplete;
  std::unique_ptr<boost::progress_display> m_progressbar;
  std::vector<std::string> m_dataCategories;
  std::vector<std::string> m_stationNames;
  std::unordered_map<size_t, size_t> m_categoryMap;
  size_t m_fileLength;
};

#endif  // CRMSDATABASE_H
