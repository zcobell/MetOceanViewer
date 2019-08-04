#!/bin/bash
redist=1
QtVersion=5_13_0
compileDirectory="../../build-MetOcean-Desktop_Qt_"$QtVersion"_MinGW_64_bit-Release"
mingwPath=/cygdrive/c/Qt/5.13.0/mingw73_64/bin
winDeployQtBinary=$mingwPath/windeployqt.exe
binaryCreator=/cygdrive/c/Qt/Tools/QtInstallerFramework/3.0/bin/binarycreator.exe
version=$(git describe --always --tags)
winPackDir=packages_windows

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
mkdir -p $winPackDir/com.zachcobell.metoceanviewer/data 

#...Grab the MetOceanViewer executable
cp $compileDirectory/MetOceanViewer/release/MetOceanViewer.exe $winPackDir/com.zachcobell.metoceanviewer/data/.
cp $compileDirectory/MetOceanData/release/MetOceanData.exe $winPackDir/com.zachcobell.metoceanviewer/data/.

#...Grab the icon
cp ../MetOceanViewer/img/mov.ico $winPackDir/com.zachcobell.metoceanviewer/data/.

#...Grab some of the third party libraries
cp ../thirdparty/netcdf/bin_64/*.dll $winPackDir/com.zachcobell.metoceanviewer/data/.
cp ../thirdparty/openssl/bin_64/*.dll $winPackDir/com.zachcobell.metoceanviewer/data/.

#...Run the deployment script
cd $winPackDir/com.zachcobell.metoceanviewer/data
$winDeployQtBinary --compiler-runtime -release MetOceanData.exe
$winDeployQtBinary --compiler-runtime --qmldir=../../../../MetOceanViewer/qml -release MetOceanViewer.exe

#...Replace cygwin libs
rm cyg*.dll
cp $mingwPath/libstdc++-6.dll .
cp $mingwPath/libgcc_s_seh-1.dll .
cp $mingwPath/libwinpthread-1.dll .

cd ../../..

#...Move the Visual C++ installer to its own directory
#if [ $redist == 1 ] ; then
#    mkdir -p $winPackDir/com.microsoft.vcredist/data
#    mv $winPackDir/com.zachcobell.metoceanviewer/data/vc_redist.x64.exe $winPackDir/com.microsoft.vcredist/data/vc_redist.x64.exe
#fi

#...Run the installer generator
$binaryCreator -c config/config.xml -p $winPackDir MetOceanViewer_Windows64bitiMinGW_Installer_$version.exe
