#!/bin/bash
redist=1
autoredist=1
redistexe='/cygdrive/c/Program Files (x86)/Microsoft Visual Studio 14.0/VC/redist/vcredist.x64.exe'
msvcVersion=2017
QtVersion=5_11_1
compileDirectory="../../build-MetOceanViewer-Desktop_Qt_"$QtVersion"_MSVC"$msvcVersion"_64bit-Release"
winDeployQtBinary=/cygdrive/c/Qt/5.11.1/msvc2017_64/bin/windeployqt.exe
binaryCreator=/cygdrive/c/Qt/Tools/QtInstallerFramework/3.0/bin/binarycreator.exe
version=$(git describe --always --tags)

if [ $redist == 1 ] ; then
    winPackDir=packages_windows_redist
else
    winPackDir=packages_windows
fi

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
cp $compileDirectory/MetOceanViewer_GUI/release/MetOceanViewer.exe $winPackDir/com.zachcobell.metoceanviewer/data/.
cp $compileDirectory/MetOceanViewer_CMD/release/MetOceanData.exe $winPackDir/com.zachcobell.metoceanviewer/data/.

#...Grab the icon
cp ../MetOceanViewer_GUI/img/mov.ico $winPackDir/com.zachcobell.metoceanviewer/data/.

#...Grab some of the third party libraries
cp ../thirdparty/netcdf/bin_64/*.dll $winPackDir/com.zachcobell.metoceanviewer/data/.
cp ../thirdparty/openssl/bin_64/*.dll $winPackDir/com.zachcobell.metoceanviewer/data/.

#...Run the deployment script
cd $winPackDir/com.zachcobell.metoceanviewer/data
if [ $autoredist == 1 ] ; then
    $winDeployQtBinary --compiler-runtime --qmldir=../../../../MetOceanViewer_GUI/qml -release MetOceanViewer.exe
    $winDeployQtBinary --compiler-runtime -release MetOceanData.exe
else
    $winDeployQtBinary -release --qmldir=../../../../MetOceanViewer_GUI/qml MetOceanViewer.exe
    $winDeployQtBinary -release MetOceanData.exe
fi
cd ../../..

#...Move the Visual C++ installer to its own directory
if [ $redist == 1 ] ; then
    mkdir -p $winPackDir/com.microsoft.vcredist/data
    if [ $autoredist == 1 ] ; then
        mv $winPackDir/com.zachcobell.metoceanviewer/data/vc_redist.x64.exe $winPackDir/com.microsoft.vcredist/data/vc_redist.x64.exe
    else
        cp "$redistexe" $winPackDir/com.microsoft.vcredist/data/.
        mv $winPackDir/com.microsoft.vcredist/data/$(basename "$redistexe") $winPackDir/com.microsoft.vcredist/data/vc_redist.x64.exe
    fi
fi

#...Run the installer generator
$binaryCreator -c config/config.xml -p $winPackDir MetOceanViewer_Windows64bit_Installer_$version.exe
