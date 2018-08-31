#!/bin/bash

static=1
QtHome=/opt/Qt
QtLibDir=/opt/Qt/5.11.0/gcc_64/lib
gui=../../build-MetOcean-Desktop_Qt_5_11_0_Static-Release/MetOceanViewer/MetOceanViewer
cmd=../../build-MetOcean-Desktop_Qt_5_11_0_Static-Release/MetOceanData/MetOceanData
version=$(git describe --always --tags)

if [ $static == 1 ] ; then
    bindir=packages_unix_static
else
    bindir=packages_unix
fi

if [ ! -s $gui ] ; then
    echo "ERROR: gui executable not found."
    exit 1
fi

if [ ! -s $cmd ] ; then
    echo "ERROR: command line executable not found."
    exit 1
fi

if [ ! -s $QtHome/Tools/QtInstallerFramework/3.0/bin/binarycreator ] ; then
    echo "ERROR: Qt Installer Framework not found"
    exit 1
fi

echo "Gathering libraries..."
ldd $gui >  liblist.txt
ldd $cmd >> liblist.txt

mkdir -p ./$bindir/com.zachcobell.metoceanviewer/data
mkdir -p ./$bindir/com.unidata.netcdf/data 
if [ $static == 0 ] ; then
    mkdir -p ./$bindir/com.qt.qtsharedlibraries/data
fi

cp $gui ./$bindir/com.zachcobell.metoceanviewer/data/.
cp $cmd ./$bindir/com.zachcobell.metoceanviewer/data/.
cp ../MetOceanViewer/img/logo_small.png ./$bindir/com.zachcobell.metoceanviewer/data/MetOceanViewer.png

while read LIB
do

    NAME=$(echo $LIB | cut -d= -f1)
    SHORTNAME=$(echo $LIB | cut -d= -f1 | cut -c1-5)
    SHORTNAME2=$(echo $LIB | cut -d= -f1 | cut -c1-9)
    SHORTNAME3=$(echo $LIB | cut -d= -f1 | cut -c1-7)
    LOCATION=$(echo $LIB | cut -d">" -f2 | cut -d"(" -f1)
    LIBDIR=$(dirname $LOCATION 2>/dev/null)
    
    if [ "x$LIBDIR" == "x$QtLibDir" ] ; then
        if [ $static == 0 ] ; then
            cp $LOCATION ./$bindir/com.qt.qtsharedlibraries/data/.
        fi
    elif [ $SHORTNAME2 == "libnetcdf" ] ; then
        cp $LOCATION ./$bindir/com.unidata.netcdf/data/.
    elif [ $SHORTNAME3 == "libhdf5" ] ; then
        cp $LOCATION ./$bindir/com.unidata.netcdf/data/.
    fi

done < liblist.txt

rm liblist.txt

echo "Building Installer..."
$QtHome/Tools/QtInstallerFramework/3.0/bin/binarycreator -c config/config.xml -p $bindir MetOceanViewer_Unix_Installer_$version.bin
