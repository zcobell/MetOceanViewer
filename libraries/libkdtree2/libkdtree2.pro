#-------------------------------GPL-------------------------------------#
#
# MetOcean Viewer - A simple interface for viewing hydrodynamic model data
# Copyright (C) 2015-2017  Zach Cobell
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#-----------------------------------------------------------------------#

QT          += core

TARGET       = movKdtree2

TEMPLATE     = lib

CONFIG      += staticlib

#...Include Boost
INCLUDEPATH += $$PWD/../../thirdparty/boost-library

#...Include KDTREE2
INCLUDEPATH += $$PWD/../../thirdparty/kdtree

DEFINES      = KDTREE2_LIBRARY

HEADERS += qkdtree2.h

SOURCES += qkdtree2.cpp \
           ../../thirdparty/kdtree/kdtree2.cpp
