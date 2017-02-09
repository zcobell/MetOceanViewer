#!/bin/bash

#...Build script for libtcd and xTide

#...Step zero: install paths for libraries
mkdir ../MetOceanViewer_GUI/mov_libs
cd ../MetOceanViewer_GUI/mov_libs
libdir=$(pwd)
cd ../../thirdparty

#...Step one: Build libtcd
cd libtcd-2.2.7
./configure --prefix=$libdir
make
make install


#...Step two: Build xTide
cd ../xtide-2.15.1
./configure --prefix=$libdir CFLAGS="-I$libdir/include" CXXFLAGS="-I$libdir/include" LDFLAGS="-L$libdir/lib" --without-x
make
make install
cd ../../MetOceanViewer_GUI

#...Step three: Copy the harmonics file
cp ../thirdparty/xtide-2.15.1/harmonics.tcd mov_libs/bin/. 
