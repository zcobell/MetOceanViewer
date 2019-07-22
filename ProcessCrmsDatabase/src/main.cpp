
#include <iostream>
#include <string>
#include "crmsdatabase.h"

int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: ./processCrmsDatabase [input] [output]" << std::endl;
    return 1;
  }

  std::string input = argv[1];
  std::string output = argv[2];

  CrmsDatabase crms(input,output);
  crms.parse();

  return 0;
}
