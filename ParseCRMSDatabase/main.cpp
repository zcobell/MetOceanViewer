#include <QCoreApplication>
#include <string>
#include "crmsdatabase.h"

int main(int argc, char *argv[]) {
  QCoreApplication a(argc, argv);

  std::string databasefile =
      "C:/Users/zcobell/Desktop/Full_Continuous_Hydrographic.csv";
  std::string outputfile =
      "C:/Users/zcobell/Desktop/Full_Continuous_Hydrographic.nc";
  CrmsDatabase d(databasefile, outputfile, &a);
  d.parse();

  return a.exec();
}
