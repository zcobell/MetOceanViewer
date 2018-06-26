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
/**
 * \class qKdtree2
 *
 * \brief Class that serves as an interface to the kdtree2 library
 *
 * This function serves as the C++ interface to the kdtree2 library. Kdtree2
 * requires the boost library headers that are included with this code. This
 * implementatin currently only supports 2-dimensional searches, though the
 * kd-tree library this interfaces with does not have such a limitation
 *
 * \author Zach Cobell
 *
 * Contact: zcobell@gmail.com
 *
 */
#ifndef QKDTREE2_H
#define QKDTREE2_H

#include <QList>
#include <QObject>
#include <QPointF>
#include <QVector3D>
#include <QVector>

// Forward declaration of kdtree2 class included from kdtree2.hpp
class kdtree2;

class qKdtree2 : public QObject {
  Q_OBJECT
 public:
  explicit qKdtree2(QObject *parent = nullptr);

  ~qKdtree2();

  void clear();

  int build(QVector<QPointF> &pointCloud);
  int build(QVector<qreal> &x, QVector<qreal> &y);
  int build(QVector<QVector3D> &pointCloud);
  int build(QList<QPointF> &pointCloud);

  int findNearest(QPointF pointLocation, int &index);
  int findNearest(qreal x, qreal y, int &index);
  int findNearest(QVector3D pointLocation, int &index);

  int findXNearest(QPointF pointLocation, int nn, QVector<int> &indicies);
  int findXNearest(qreal x, qreal y, int nn, QVector<int> &indicies);
  int findXNearest(QVector3D pointLocation, int nn, QVector<int> &indicies);

  int numDataPoints() const;
  void setNumDataPoints(int numDataPoints);

  bool initialized() const;
  void setInitialized(bool initialized);

 private:
  /// Variable holding the total number of points in the search tree
  int m_numDataPoints;

  /// Variable that ensures the search tree is initialized
  bool m_initialized;

  /// Pointer to variable holding the kdtree search tree
  kdtree2 *m_tree;
};

#endif  // QKDTREE2_H
