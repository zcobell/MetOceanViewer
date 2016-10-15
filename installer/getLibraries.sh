#!/bin/bash

executable=../../build-MetOceanViewer-Desktop_Qt_5_7_0_GCC_64bit-Release/MetOcean_Viewer

ldd $executable > liblist.txt

cp $executable ./packages/com.zachcobell.metoceanviewer/data/.
cp ../mov_libs/bin/tide ./packages/com.zachcobell.metoceanviewer/data/.
cp ../mov_libs/bin/harmonics.tcd ./packages/com.zachcobell.metoceanviewer/data/.

while read LIB
do

    NAME=$(echo $LIB | cut -d= -f1)
    SHORTNAME=$(echo $LIB | cut -d= -f1 | cut -c1-5)
    SHORTNAME2=$(echo $LIB | cut -d= -f1 | cut -c1-9)
    SHORTNAME3=$(echo $LIB | cut -d= -f1 | cut -c1-7)
    LOCATION=$(echo $LIB | cut -d">" -f2 | cut -d"(" -f1)
    
    if [ $SHORTNAME == "libQt" ] ; then
        cp $LOCATION ./packages/com.qt.qtsharedlibraries/data/.
    elif [ $SHORTNAME2 == "libnetcdf" ] ; then
        cp $LOCATION ./packages/com.unidata.netcdf/data/.
    elif [ $SHORTNAME3 == "libhdf5" ] ; then
        cp $LOCATION ./packages/com.unidata.netcdf/data/.
    fi

done < liblist.txt


rm liblist.txt
