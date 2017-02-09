#!/bin/bash


winDeployQtBinary=/cygdrive/c/Qt/Qt5.8.0/5.8/msvc2015_64/bin/windeployqt.exe
binaryCreator=/cygdrive/c/Qt/QtIFW2.0.3/bin/binarycreator.exe
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
cp ../../build-MetOceanViewer-Desktop_Qt_5_8_0_MSVC2015_64bit-Release/MetOceanViewer_GUI/release/MetOceanViewer.exe packages_windows/com.zachcobell.metoceanviewer/data/.

#...Grab the proj4 library
cp ../../build-MetOceanViewer-Desktop_Qt_5_8_0_MSVC2015_64bit-Release/libproj4/release/proj4.dll packages_windows/com.zachcobell.metoceanviewer/data/.

#...Grab the Windows XTide executable
cp ../thirdparty/xtide-2.15.1/tide.exe packages_windows/com.zachcobell.metoceanviewer/data/.

#...Grab the database
cp ../thirdparty/xtide-2.15.1/harmonics.tcd packages_windows/com.zachcobell.metoceanviewer/data/.

#...Grab the icon
cp ../MetOceanViewer_GUI/img/mov.ico packages_windows/com.zachcobell.metoceanviewer/data/.

#...Grab some of the third party libraries
cp ../thirdparty/netcdf/bin_64/*.dll packages_windows/com.zachcobell.metoceanviewer/data/.
cp ../thirdparty/openssl/bin_64/*.dll packages_windows/com.zachcobell.metoceanviewer/data/.

#...Run the deployment script
cd packages_windows/com.zachcobell.metoceanviewer/data
$winDeployQtBinary --compiler-runtime -release MetOceanViewer.exe
$winDeployQtBinary --compiler-runtime -release proj4.dll
cd ../../..

#...Move the Visual C++ installer to its own directory
mkdir -p packages_windows/com.microsoft.vcredist/data
mv packages_windows/com.zachcobell.metoceanviewer/data/vcredist_x64.exe packages_windows/com.microsoft.vcredist/data/vcredist_x64.exe

#...Run the installer generator
$binaryCreator -c config/config.xml -p packages_windows MetOceanViewer_Windows64bit_Installer_$version.exe
