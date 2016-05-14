#!/bin/bash

#...Build script for MetOceanViewer

#...Step zero: install paths for libraries
mkdir mov_libs
cd mov_libs
libdir=$(pwd)

#...Step one: Build libtcd
cd ../thirdparty/libtcd-2.2.7
./configure --prefix=$libdir
make
make install


#...Step two: Build xTide
cd ../xtide-2.15.1
./configure --prefix=$libdir CFLAGS="-I$libdir/include" CXXFLAGS="-I$libdir/include" LDFLAGS="-L$libdir/lib" --without-x
make
make install
