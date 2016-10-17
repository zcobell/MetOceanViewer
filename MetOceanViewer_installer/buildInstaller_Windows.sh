#!/bin/bash


windDeployQtBinary=/cygdrive/c/Qt/Qt5.7.0/5.7/msvc2015_64/bin/windeployqt.exe
binaryCreator=/cygdrive/c/Qt/Qt5.7.0/5.7-static/qt-everywhere-opensource-src-5.7.0/installer-framework/bin/binarycreator.exe
version=$(git describe --always --tags)


#...Check for WindDeployQt
if [ ! -s $windDeployQtBinary ] ; then
    echo "ERROR: Missing winddeployqt.exe"
    exit 1
fi

#...Check for BinaryCreator
if [ ! -s $binaryCreator ] ; then
    echo "ERROR: Missing binarycreator.exe"
    exit 1
fi

#...Make the data directory
mkdir -p packages_windows/com.zachcobell.metoceanviewer/data 

#...Grab the MetOceanViewer executable
cp ../../build-MetOceanViewer-Desktop_Qt_5_7_0_MSVC2015_64bit-Release/MetOceanViewer_GUI/release/MetOcean_Viewer.exe packages_windows/com.zachcobell.metoceanviewer/data/.

#...Grab the XTide executable
cp ../MetOceanViewer_GUI/mov_libs/bin/tide.exe packages_windows/com.zachcobell.metoceanviewer/data/.

#...Grab the database
cp ../MetOceanViewer_GUI/mov_libs/bin/harmonics.tcd packages_windows/com.zachcobell.metoceanviewer/data/.

#...Grab the icon
cp ../MetOceanViewer_GUI/img/mov.ico packages_windows/com.zachcobell.metoceanviewer/data/.

#...Grab some of the third party libraries
cp ../MetOceanViewer_GUI/thirdparty/netcdf/bin_64/*.dll packages_windows/com.zachcobell.metoceanviewer/data/.
cp ../MetOceanViewer_GUI/thirdparty/openssl/bin_64/*.dll packages_windows/com.zachcobell.metoceanviewer/data/.

#...Run the deployment script
cd packages_windows/com.zachcobell.metoceanviewer/data
$windDeployQtBinary --compiler-runtime -release MetOcean_Viewer.exe
cd ../../..

#...Run the installer generator
$binaryCreator -c config/config.xml -p packages_windows MetOceanViewer_Windows64bit_Installer_$version.exe
