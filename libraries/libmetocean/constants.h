#ifndef CONSTANTS_H
#define CONSTANTS_H

class Constants {
 public:
  Constants();
  static double equitoralRadius();
  static double polarRadius();
  static double e();
  static double pi();
  static double twoPi();
  static double halfPi();
  static double deg2rad();
  static double rad2deg();
  static double toRadians(double degrees);
  static double toDegrees(double radians);
  static double radiusEarth(double latitude);
  static double radiusEarth();
  static double g();
  static double rhoAir();
  static double distance(double x1, double y1, double x2, double y2, bool geodesic = false);
  static double azimuth(double x1, double y1, double  x2, double y2);

private:
  static double geodesic_distance(double x1, double y1, double x2, double y2);
  static double cartesian_distance(double x1, double y1, double x2, double y2);
};

#endif // CONSTANTS_H
