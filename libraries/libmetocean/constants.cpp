#include "constants.h"
#include <cmath>

const double m_equitoralRadius = 6378137.0;
const double m_polarRadius = 6356752.3;
const double m_meanRadiusEarth = 6378206.4;
const double m_e = exp(1.0);
const double m_pi = 4.0 * atan2(1.0, 1.0);
const double m_2pi = 2.0 * m_pi;
const double m_halfPi = 0.5 * m_pi;
const double m_deg2rad = m_pi / 180.0;
const double m_rad2deg = 180.0 / m_pi;
const double m_rhoAir = 1.15;
const double m_g = 9.80665;

Constants::Constants() = default;

double Constants::equitoralRadius() { return m_equitoralRadius; }
double Constants::polarRadius() { return m_polarRadius; }
double Constants::g() { return m_g; }
double Constants::e() { return m_e; }
double Constants::pi() { return m_pi; }
double Constants::deg2rad() { return m_deg2rad; }
double Constants::rad2deg() { return m_rad2deg; }
double Constants::twoPi() { return m_2pi; }
double Constants::halfPi() { return m_halfPi; }
double Constants::rhoAir() { return m_rhoAir; }
double Constants::radiusEarth() { return m_meanRadiusEarth; }
double Constants::radiusEarth(double latitude) {
  double l = Constants::toRadians(latitude);
  return sqrt((pow(Constants::equitoralRadius(), 4.0) * cos(l) * cos(l) +
               pow(Constants::polarRadius(), 4.0) * sin(l) * sin(l)) /
              (pow(Constants::equitoralRadius(), 2.0) * cos(l) * cos(l) +
               pow(Constants::polarRadius(), 2.0) * sin(l) * sin(l)));
}

double Constants::distance(double x1, double y1, double x2, double y2,
                           bool geodesic) {
  if (geodesic) {
    return Constants::geodesic_distance(x1, y1, x2, y2);
  } else {
    return Constants::cartesian_distance(x1, y2, x2, y2);
  }
}

double Constants::geodesic_distance(double x1, double y1, double x2,
                                    double y2) {
  double lat1 = Constants::toRadians(y1);
  double lon1 = Constants::toRadians(x1);
  double lat2 = Constants::toRadians(y2);
  double lon2 = Constants::toRadians(x2);
  double r = Constants::radiusEarth((y1 + y2) / 2.0);
  return 2.0 * r *
         asin(sqrt(pow(sin((lat2 - lat1) / 2.0), 2.0) +
                   cos(lat1) * cos(lat2) * pow(sin((lon2 - lon1) / 2.0), 2.0)));
}

double Constants::cartesian_distance(double x1, double y1, double x2,
                                     double y2) {
  return sqrt(pow(x2 - x1, 2.0) + pow(y2 - y1, 2.0));
}

double Constants::toDegrees(double radians) {
  return radians * Constants::rad2deg();
}

double Constants::toRadians(double degrees) {
  return degrees * Constants::deg2rad();
}
