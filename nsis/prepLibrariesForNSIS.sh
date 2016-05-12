#!/bin/bash


#...Find the deployment script
deployqt=$(which windeployqt)
if [ $? != 0 ] ; then
    echo "ERROR: Could not find windeployqt"
    exit 1
fi

#...Make a deployment directory
if [ -d deployment ] ; then
    rm -rf deployment
fi
mkdir deployment

cd deployment

#...Grab the MetOceanViewer executable
cp ../../../build-MetOceanViewer-Desktop_Qt_5_6_0_MSVC2015_64bit-Release/release/MetOcean_Viewer.exe .

#...Grab some of the third party libraries
cp ../../thirdparty/netcdf/bin_64/*.dll .
cp ../../thirdparty/openssl/bin_64/*.dll .

#...Run the deployment script
windeployqt --compiler-runtime -release MetOcean_Viewer.exe
