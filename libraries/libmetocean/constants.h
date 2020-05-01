#ifndef CONSTANTS_H
#define CONSTANTS_H

#include "metocean_global.h"

class Constants {
 public:
  Constants();
  static double METOCEANSHARED_EXPORT equitoralRadius();
  static double METOCEANSHARED_EXPORT polarRadius();
  static double METOCEANSHARED_EXPORT e();
  static double METOCEANSHARED_EXPORT pi();
  static double METOCEANSHARED_EXPORT twoPi();
  static double METOCEANSHARED_EXPORT halfPi();
  static double METOCEANSHARED_EXPORT deg2rad();
  static double METOCEANSHARED_EXPORT rad2deg();
  static double METOCEANSHARED_EXPORT toRadians(double degrees);
  static double METOCEANSHARED_EXPORT toDegrees(double radians);
  static double METOCEANSHARED_EXPORT radiusEarth(double latitude);
  static double METOCEANSHARED_EXPORT radiusEarth();
  static double METOCEANSHARED_EXPORT g();
  static double METOCEANSHARED_EXPORT rhoAir();
  static double METOCEANSHARED_EXPORT distance(double x1, double y1, double x2,
                                               double y2,
                                               bool geodesic = false);
  static double METOCEANSHARED_EXPORT azimuth(double x1, double y1, double x2,
                                              double y2);

 private:
  static double geodesic_distance(double x1, double y1, double x2, double y2);
  static double cartesian_distance(double x1, double y1, double x2, double y2);
};

#endif  // CONSTANTS_H
