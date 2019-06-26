#include <QCoreApplication>
#include <string>
#include "crmsdatabase.h"

int main(int argc, char *argv[]) {
  QCoreApplication a(argc, argv);

  std::string databasefile =
      "/Users/zcobell/Desktop/Full_Continuous_Hydrographic.csv";
  std::string outputfile =
      "/Users/zcobell/Desktop/Full_Continuous_Hydrographic.nc";
  CrmsDatabase d(databasefile, outputfile, &a);
  d.parse();

  return a.exec();
}
