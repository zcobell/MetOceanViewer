/*-------------------------------GPL-------------------------------------//
//
// QADCModules - A library for working with ADCIRC models
// Copyright (C) 2016  Zach Cobell
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
#include "qkdtree2.h"
#include "boost/array.hpp"
#include "boost/multi_array.hpp"
#include "kdtree2.hpp"

//-----------------------------------------------------------------------------------------//
// Constructor function. Initializes the qKdtree2 class
//-----------------------------------------------------------------------------------------//
/** \brief Constructor function for qKdtree2 class
 *
 * @param[in] parent Parent QObject used for memory management
 *
 * Constructor function for qKdtree2 class
 *
 */
//-----------------------------------------------------------------------------------------//
qKdtree2::qKdtree2(QObject *parent) : QObject(parent) {
  this->initialized = false;
  this->numDataPoints = 0;
  this->tree = nullptr;
}
//-----------------------------------------------------------------------------------------//

qKdtree2::~qKdtree2() {
  if (this->tree != nullptr) delete this->tree;
}

void qKdtree2::clear() {
  if (!(this->tree == nullptr)) delete this->tree;
}

//-----------------------------------------------------------------------------------------//
//...Function that constructs a kd-tree for a given X/Y pair
//-----------------------------------------------------------------------------------------//
/**
 * \fn qKdtree2::build(QVector<QPointF> &pointCloud)
 * \brief Generates a kdtree2 search tree
 *
 * @param[in] pointCloud Vector of QPointF used to generate the search tree
 *
 * This function uses the kdtree2 library to generate a search tree
 *
 **/
//-----------------------------------------------------------------------------------------//
int qKdtree2::build(QVector<QPointF> &pointCloud) {
  int i;
  typedef boost::multi_array<float, 2> array2d;

  this->numDataPoints = pointCloud.size();

  array2d data(boost::extents[pointCloud.size()][2]);

  for (i = 0; i < pointCloud.size(); i++) {
    data[i][0] = static_cast<float>(pointCloud[i].x());
    data[i][1] = static_cast<float>(pointCloud[i].y());
  }

  if (this->tree != nullptr) this->clear();
  this->tree = new kdtree2(data, true);

  return 0;
}
//-----------------------------------------------------------------------------------------//

//-----------------------------------------------------------------------------------------//
//...Function that constructs a kd-tree for a given X/Y pair
//-----------------------------------------------------------------------------------------//
/**
 * \fn qKdtree2::build(QList<QPointF> &pointCloud)
 * \brief Generates a kdtree2 search tree
 *
 * @param[in] pointCloud Vector of QPointF used to generate the search tree
 *
 * This function uses the kdtree2 library to generate a search tree
 *
 **/
//-----------------------------------------------------------------------------------------//
int qKdtree2::build(QList<QPointF> &pointCloud) {
  int i;
  typedef boost::multi_array<float, 2> array2d;

  this->numDataPoints = pointCloud.size();

  array2d data(boost::extents[pointCloud.size()][2]);

  for (i = 0; i < pointCloud.size(); i++) {
    data[i][0] = static_cast<float>(pointCloud[i].x());
    // data[i][1] = static_cast<float>(pointCloud[i].y());

    //...Note we override the y value to tie the mouse to the x position
    data[i][1] = 0.0;
  }

  if (this->tree != nullptr) this->clear();
  this->tree = new kdtree2(data, true);

  return 0;
}
//-----------------------------------------------------------------------------------------//

//-----------------------------------------------------------------------------------------//
//...Function that constructs a kd-tree for a given X/Y pair
//-----------------------------------------------------------------------------------------//
/**
 * \overload qKdtree2::build(QVector<qreal> &x, QVector<qreal> &y)
 * \brief Generates a kdtree2 search tree
 *
 * @param[in] x Vector of x-coordinates
 * @param[in] y Vector of y-coordinates
 *
 * This function uses the kdtree2 library to generate a search tree
 *
 **/
//-----------------------------------------------------------------------------------------//
int qKdtree2::build(QVector<qreal> &x, QVector<qreal> &y) {
  int i;
  typedef boost::multi_array<float, 2> array2d;

  if (x.size() != y.size()) return -1;

  this->numDataPoints = x.size();

  array2d data(boost::extents[x.size()][2]);

  for (i = 0; i < x.size(); i++) {
    data[i][0] = static_cast<float>(x[i]);
    data[i][1] = static_cast<float>(y[i]);
  }

  if (this->tree != nullptr) this->clear();
  this->tree = new kdtree2(data, true);

  return 0;
}
//-----------------------------------------------------------------------------------------//

//-----------------------------------------------------------------------------------------//
//...Function that constructs a kd-tree for a given X/Y pair
//-----------------------------------------------------------------------------------------//
/**
 * \overload qKdtree2::build(QVector<QVector3D> &pointCloud)
 * \brief Generates a kdtree2 search tree
 *
 * @param[in] pointCloud Vector of QVector3D used to generate the search tree
 *
 * This function uses the kdtree2 library to generate a search tree
 *
 **/
//-----------------------------------------------------------------------------------------//
int qKdtree2::build(QVector<QVector3D> &pointCloud) {
  int i;
  typedef boost::multi_array<float, 2> array2d;

  this->numDataPoints = pointCloud.size();

  array2d data(boost::extents[pointCloud.size()][2]);

  for (i = 0; i < pointCloud.size(); i++) {
    data[i][0] = pointCloud[i].x();
    data[i][1] = pointCloud[i].y();
  }

  if (this->tree != nullptr) this->clear();
  this->tree = new kdtree2(data, true);

  return 0;
}
//-----------------------------------------------------------------------------------------//

//-----------------------------------------------------------------------------------------//
//...Function that uses a kd-tree to find the nearest point to a given set of
// coordinates
//-----------------------------------------------------------------------------------------//
/**
 * \fn qKdtree2::findNearest(QPointF pointLocation, int &index)
 * \brief Function that uses a kd-tree to find the nearest point to a given set
 *of coordinates
 *
 * @param[in]  pointLocation  location of point to search for
 * @param[out] index          location in array that created kd-tree of nearest
 *point
 *
 * Function that uses a kd-tree to find the nearest point to a given set of
 *coordinates
 *
 **/
//-----------------------------------------------------------------------------------------//
int qKdtree2::findNearest(QPointF pointLocation, int &index) {
  kdtree2_result_vector result_vector;
  kdtree2_result result;
  vector<float> query(2);

  query[0] = static_cast<float>(pointLocation.x());
  query[1] = static_cast<float>(pointLocation.y());

  this->tree->n_nearest(query, 1, result_vector);

  result = result_vector.at(0);
  index = result.idx;

  return 0;
}
//-----------------------------------------------------------------------------------------//

//-----------------------------------------------------------------------------------------//
//...Function that uses a kd-tree to find the nearest point to a given set of
// coordinates
//-----------------------------------------------------------------------------------------//
/**
 * \overload qKdtree2::findNearest(qreal x, qreal y, int &index)
 * \brief Function that uses a kd-tree to find the nearest point to a given set
 *of coordinates
 *
 * @param[in]  x      x-coordinate of point to search for
 * @param[in]  y      y-coordinate of point to search for
 * @param[out] index  location in array that created kd-tree of nearest point
 *
 * Function that uses a kd-tree to find the nearest point to a given set of
 *coordinates
 *
 **/
//-----------------------------------------------------------------------------------------//
int qKdtree2::findNearest(qreal x, qreal y, int &index) {
  return this->findNearest(QPointF(x, y), index);
}
//-----------------------------------------------------------------------------------------//

//-----------------------------------------------------------------------------------------//
//...Function that uses a kd-tree to find the nearest point to a given set of
// coordinates
//-----------------------------------------------------------------------------------------//
/**
 * \overload qKdtree2::findNearest(QVector3D pointLocation, int &index)
 * \brief Function that uses a kd-tree to find the nearest point to a given set
 *of coordinates
 *
 * @param[in]  pointLocation location of point to search for
 * @param[out] index         location in array that created kd-tree of nearest
 *point
 *
 * Function that uses a kd-tree to find the nearest point to a given set of
 *coordinates
 *
 **/
//-----------------------------------------------------------------------------------------//
int qKdtree2::findNearest(QVector3D pointLocation, int &index) {
  return this->findNearest(pointLocation.toPointF(), index);
}
//-----------------------------------------------------------------------------------------//

//-----------------------------------------------------------------------------------------//
//...Function that uses a kd-tree to find the nearest point to a given set of
// coordinates
//-----------------------------------------------------------------------------------------//
/**
 * \fn qKdtree2::findXNearest(QPointF pointLocation, int nn, QVector<int>
 *&indicies) \brief Function that uses a kd-tree to find the N-nearest point to
 *a given set of coordinates
 *
 * @param[in]  pointLocation  location of point to search for
 * @param[in]  nn             number of locations to find
 * @param[out] indicies       vector of locations in array that created kd-tree
 *of nearest points
 *
 * Function that uses a kd-tree to find the N-nearest point to a given set of
 *coordinates
 *
 **/
//-----------------------------------------------------------------------------------------//
int qKdtree2::findXNearest(QPointF pointLocation, int nn,
                           QVector<int> &indicies) {
  int i;
  kdtree2_result_vector result_vector;
  kdtree2_result result;
  vector<float> query(2);

  if (nn > this->numDataPoints) nn = this->numDataPoints;

  query[0] = static_cast<float>(pointLocation.x());
  query[1] = static_cast<float>(pointLocation.y());

  this->tree->n_nearest(query, nn, result_vector);

  indicies.resize(nn);

  for (i = 0; i < nn; i++) {
    result = result_vector.at(i);
    indicies[i] = result.idx;
  }

  return 0;
}
//-----------------------------------------------------------------------------------------//

//-----------------------------------------------------------------------------------------//
//...Function that uses a kd-tree to find the nearest point to a given set of
// coordinates
//-----------------------------------------------------------------------------------------//
/**
 * \overload qKdtree2::findXNearest(qreal x, qreal y, int nn, QVector<int>
 *&indicies) \brief Function that uses a kd-tree to find the N-nearest point to
 *a given set of coordinates
 *
 * @param[in]  x              x-coordinate of point to search for
 * @param[in]  y              y-coordinate of point to search for
 * @param[in]  nn             number of locations to find
 * @param[out] indicies       vector of locations in array that created kd-tree
 *of nearest points
 *
 * Function that uses a kd-tree to find the N-nearest point to a given set of
 *coordinates
 *
 **/
//-----------------------------------------------------------------------------------------//
int qKdtree2::findXNearest(qreal x, qreal y, int nn, QVector<int> &indicies) {
  return this->findXNearest(QPointF(x, y), nn, indicies);
}
//-----------------------------------------------------------------------------------------//

//-----------------------------------------------------------------------------------------//
//...Function that uses a kd-tree to find the nearest point to a given set of
// coordinates
//-----------------------------------------------------------------------------------------//
/**
 * \overload qKdtree2::findXNearest(QVector3D pointLocation, int nn,
 *QVector<int> &indicies) \brief Function that uses a kd-tree to find the
 *N-nearest point to a given set of coordinates
 *
 * @param[in]  pointLocation  location of point to search for
 * @param[in]  nn             number of locations to find
 * @param[out] indicies       vector of locations in array that created kd-tree
 *of nearest points
 *
 * Function that uses a kd-tree to find the N-nearest point to a given set of
 *coordinates
 *
 **/
//-----------------------------------------------------------------------------------------//
int qKdtree2::findXNearest(QVector3D pointLocation, int nn,
                           QVector<int> &indicies) {
  return this->findXNearest(pointLocation.toPointF(), nn, indicies);
}
//-----------------------------------------------------------------------------------------//
