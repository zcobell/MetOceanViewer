#-------------------------------GPL-------------------------------------#
#
# MetOceanViewer - A library for working with ADCIRC models
# Copyright (C) 2016  Zach Cobell
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


QT       -= core gui

TARGET = movNefis
TEMPLATE = lib
CONFIG += staticlib

unix:!macx{
QMAKE_CFLAGS = -D_GNU_SOURCE -DSTR_LEN_AT_END -DFTN_UNDERSCORE \
               -D_FILE_OFFSET_BITS=64 -DPTR8 -DHAVE_CONFIG_H \
               -DLINUX64 -w

QMAKE_CXXFLAGS = -D_GNU_SOURCE -DSTR_LEN_AT_END -DFTN_UNDERSCORE \
               -D_FILE_OFFSET_BITS=64 -DPTR8 -DHAVE_CONFIG_H \
               -DLINUX64 -w
}

*msvc* {
QMAKE_CFLAGS_DEBUG = -D_GNU_SOURCE -DSTR_LEN_AT_END -DFTN_UNDERSCORE \
               -D_FILE_OFFSET_BITS=64 -DPTR8  \
               -DW64 -w /FS

QMAKE_CXXFLAGS_DEBUG = -D_GNU_SOURCE -DSTR_LEN_AT_END -DFTN_UNDERSCORE \
               -D_FILE_OFFSET_BITS=64 -DPTR8 \
               -DW64 -w /FS

QMAKE_CFLAGS_RELEASE = -D_GNU_SOURCE -DSTR_LEN_AT_END -DFTN_UNDERSCORE \
               -D_FILE_OFFSET_BITS=64 -DPTR8  \
               -DW64 -w

QMAKE_CXXFLAGS_RELEASE = -D_GNU_SOURCE -DSTR_LEN_AT_END -DFTN_UNDERSCORE \
               -D_FILE_OFFSET_BITS=64 -DPTR8 \
               -DW64 -w
}

INCLUDEPATH += ../thirdparty/nefis/include

SOURCES += ../thirdparty/nefis/src/c2c.c \
           ../thirdparty/nefis/src/df.c  \
           ../thirdparty/nefis/src/er.c  \
           ../thirdparty/nefis/src/f2c.c \
           ../thirdparty/nefis/src/gp.c  \
           ../thirdparty/nefis/src/gt.c  \
           ../thirdparty/nefis/src/hs.c  \
           ../thirdparty/nefis/src/nefis_version.cpp \
           ../thirdparty/nefis/src/oc.c  \
           ../thirdparty/nefis/src/pt.c  \
           ../thirdparty/nefis/src/rt.c  \
           ../thirdparty/nefis/src/wl-xdr.c

HEADERS += ../thirdparty/nefis/include/btps.h \
           ../thirdparty/nefis/include/c2c.h \
           ../thirdparty/nefis/include/config.h \
           ../thirdparty/nefis/include/df.h \
           ../thirdparty/nefis/include/f2c.h \
           ../thirdparty/nefis/include/gp.h \
           ../thirdparty/nefis/include/gt.h \
           ../thirdparty/nefis/include/hs.h \
           ../thirdparty/nefis/include/nef-def.h \
           ../thirdparty/nefis/include/nef-tag.h \
           ../thirdparty/nefis/include/nef-xdf.h \
           ../thirdparty/nefis/include/nefis.h \
           ../thirdparty/nefis/include/nefis_version.h \
           ../thirdparty/nefis/include/oc.h \
           ../thirdparty/nefis/include/pt.h \
           ../thirdparty/nefis/include/resource.h \
           ../thirdparty/nefis/include/rt.h \
           ../thirdparty/nefis/include/wl-xdr.h
