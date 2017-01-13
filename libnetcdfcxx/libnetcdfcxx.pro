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

TARGET       = netcdfcxx

TEMPLATE     = lib

CONFIG      += staticlib

NETCDFCXXPATH    = $$PWD/../thirdparty/netcdf-cxx/cxx4

INCLUDEPATH += $$NETCDFCXXPATH
INCLUDEPATH += $$PWD/../MetOceanViewer_GUI/thirdparty/netcdf/include
LIBS += -L$$PWD/thirdparty/netcdf/libs_vc64 -lnetcdf -lhdf5 -lzlib -llibcurl_imp

DEFINES      = NETCDFCXX_LIBRARY

#...PROJ Sources
SOURCES_NETCDFCXX = \
    ../thirdparty/netcdf-cxx/cxx4/ncAtt.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncByte.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncChar.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncCheck.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncCompoundType.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncDim.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncDouble.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncEnumType.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncException.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncFile.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncFloat.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncGroup.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncGroupAtt.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncInt.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncInt64.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncOpaqueType.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncShort.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncString.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncType.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncUbyte.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncUint.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncUint64.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncUshort.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncVar.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncVarAtt.cpp \
    ../thirdparty/netcdf-cxx/cxx4/ncVlenType.cpp 

SOURCES += $$SOURCES_NETCDFCXX
