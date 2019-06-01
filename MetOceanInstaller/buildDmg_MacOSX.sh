#!/bin/bash

QtVersion=5_12_3
version=$(git describe --always --tags)
deploy="/Users/zcobell/Qt/5.12.3/clang_64/bin/macdeployqt"
appPath="../../build-MetOcean-Desktop_Qt_"$QtVersion"_clang_64bit-Release/MetOceanViewer"

$deploy $appPath/MetOceanViewer.app -dmg -qmldir=../MetOceanViewer/qml -always-overwrite

mv $appPath/MetOceanViewer.dmg MetOceanViewer_MacOSX_$version.dmg 
