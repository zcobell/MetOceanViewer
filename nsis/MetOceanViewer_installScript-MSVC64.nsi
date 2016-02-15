#----GPL-----------------------------------------------------------------------
#
# This file is part of MetOceanViewer.
#
#    MetOceanViewer is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    MetOceanViewer is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with MetOceanViewer.  If not, see <http:#www.gnu.org/licenses/>.
#------------------------------------------------------------------------------

#Define some basic variables for the installer
!define APPNAME "MetOceanViewer"
!define COMPILER "MSVC-64bit"
!define COMPANYNAME "Zach Cobell"
!define DESCRIPTION "A simple GUI to view ADCIRC or other model data."
!define VERSIONMAJOR 2
!define VERSIONMINOR 1
!define VERSIONBUILD 0
!define HELPURL "https://github.com/zcobell/MetOceanViewer" # "Support Information" link
!define UPDATEURL "https://github.com/zcobell/MetOceanViewer" # "Product Updates" link
!define ABOUTURL "http://zachcobell.com/" # "Publisher" link
!define INSTALLSIZE 100760
!define MUI_ICON "..\img\mov.ico"

#Include files
!include MUI2.nsh
!include LogicLib.nsh

RequestExecutionLevel admin

#Get the git version
!tempfile StdOut
!system "git --git-dir ../.git --work-tree $$PWD describe --always --tags > ${StdOut}"
!define /file GITREV ${StdOut}
!delfile StdOut

# define installer name
OutFile "MetOceanViewer_${COMPILER}_installer_${GITREV}.exe"
 
# set desktop as install directory
InstallDir $PROGRAMFILES64\MetOceanViewer

# Set the name of the program
Name "${APPNAME}"

# Set the Icon
Icon "..\img\mov.ico"

;--------------------------------
;Macros
;--------------------------------

!macro VerifyUserIsAdmin
UserInfo::GetAccountType
pop $0
${If} $0 != "admin" ;Require admin rights on NT4+
        messageBox mb_iconstop "Administrator rights required!"
        setErrorLevel 740 ;ERROR_ELEVATION_REQUIRED
        quit
${EndIf}
!macroend
 
Function .onInit
	setShellVarContext all
	!insertmacro VerifyUserIsAdmin
     
  ReadRegStr $R0 HKLM \
  "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" \
  "UninstallString"
  StrCmp $R0 "" done
 
  MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION \
  "${APPNAME} is already installed. $\n$\nClick `OK` to remove the \
  previous version or `Cancel` to cancel this upgrade." \
  IDOK uninst
  Abort
 
;Run the uninstaller
uninst:
  ClearErrors
  Exec $INSTDIR\uninstaller.exe
done:
 
FunctionEnd
Function un.onInit
	SetShellVarContext all
	!insertmacro VerifyUserIsAdmin
FunctionEnd

;--------------------------------

;--------------------------------
;Interface Settings

  !define MUI_ABORTWARNING

;--------------------------------
;Pages
  !insertmacro MUI_PAGE_WELCOME
  !insertmacro MUI_PAGE_LICENSE "GNU_GPLv3.txt"
  !insertmacro MUI_PAGE_DIRECTORY
  !insertmacro MUI_PAGE_INSTFILES
  !insertmacro MUI_PAGE_FINISH
  
  !insertmacro MUI_UNPAGE_WELCOME
  !insertmacro MUI_UNPAGE_CONFIRM
  !insertmacro MUI_UNPAGE_INSTFILES
  !insertmacro MUI_UNPAGE_FINISH
  
;--------------------------------
;Languages
 
  !insertmacro MUI_LANGUAGE "English"

;--------------------------------
;Installer Sections

Section "Install"
 
    # define output path
    SetOutPath $INSTDIR

    # Output files in the main folder
    File ..\libs\msvc64\d3dcompiler_47.dll
    File ..\libs\msvc64\icudt54.dll
    File ..\libs\msvc64\icuin54.dll
    File ..\libs\msvc64\icuuc54.dll
    File ..\libs\msvc64\libEGL.dll
    File ..\libs\msvc64\Qt5Core.dll
    File ..\libs\msvc64\Qt5Charts.dll
    File ..\libs\msvc64\Qt5Gui.dll
    File ..\libs\msvc64\Qt5Multimedia.dll
    File ..\libs\msvc64\Qt5MultimediaWidgets.dll
    File ..\libs\msvc64\Qt5Network.dll
    File ..\libs\msvc64\Qt5OpenGL.dll
    File ..\libs\msvc64\Qt5Positioning.dll
    File ..\libs\msvc64\Qt5PrintSupport.dll
    File ..\libs\msvc64\Qt5Qml.dll
    File ..\libs\msvc64\Qt5Quick.dll
    File ..\libs\msvc64\Qt5Sensors.dll
    File ..\libs\msvc64\Qt5Sql.dll
    File ..\libs\msvc64\Qt5WebChannel.dll
    File ..\libs\msvc32\Qt5WebEngine.dll
    File ..\libs\msvc32\Qt5WebEngineCore.dll
    File ..\libs\msvc32\Qt5WebEngineWidgets.dll
    File ..\libs\msvc64\Qt5Widgets.dll
    File ..\libs\msvc64\Qt5Xml.dll
    File ..\libs\msvc64\msvcp120.dll
    File ..\libs\msvc64\msvcr120.dll
	
	File /r ..\libs\msvc64\imageformats
    File /r ..\libs\msvc64\platforms
	
	File ..\thirdparty\openssl\bin_64\libssl32.dll
	File ..\thirdparty\openssl\bin_64\libeay32.dll
    File ..\thirdparty\openssl\bin_64\ssleay32.dll
	
	File ..\thirdparty\netcdf\bin_64\netcdf.dll
	File ..\thirdparty\netcdf\bin_64\libcurl.dll
	File ..\thirdparty\netcdf\bin_64\hdf5.dll
    File ..\thirdparty\netcdf\bin_64\hdf5_hl.dll
    File ..\thirdparty\netcdf\bin_64\zlib1.dll
	
    File ..\img\mov.ico
	
    File ..\..\build-MetOceanViewer-Desktop_Qt_5_5_0_MSVC2013_64bit-Release\release\MetOcean_Viewer.exe
     
    # define uninstaller name
    WriteUninstaller $INSTDIR\uninstaller.exe

    # Start Menu
	CreateDirectory "$SMPROGRAMS\${APPNAME}"
	CreateShortCut "$SMPROGRAMS\${APPNAME}\MetOcean Viewer.lnk" "$INSTDIR\MetOcean_Viewer.exe" "" "$INSTDIR\mov.ico"
    CreateShortCut "$DESKTOP\MetOcean Viewer.lnk" "$INSTDIR\MetOcean_Viewer.exe" "" "$INSTDIR\mov.ico"
    CreateShortCut "$SMPROGRAMS\${APPNAME}\Uninstall MetOcean Viewer.lnk" "$INSTDIR\uninstaller.exe" ""
 
	# Registry information for add/remove programs
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayName" "${APPNAME}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "UninstallString" "$\"$INSTDIR\uninstaller.exe$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "QuietUninstallString" "$\"$INSTDIR\uninstaller.exe$\" /S"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "InstallLocation" "$\"$INSTDIR$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayIcon" "$\"$INSTDIR\mov.ico$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "Publisher" "$\"${COMPANYNAME}$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "HelpLink" "$\"${HELPURL}$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "URLUpdateInfo" "$\"${UPDATEURL}$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "URLInfoAbout" "$\"${ABOUTURL}$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayVersion" "$\"${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}$\""
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "VersionMajor" ${VERSIONMAJOR}
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "VersionMinor" ${VERSIONMINOR}
	# There is no option for modifying or repairing the install
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoModify" 1
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoRepair" 1
	# Set the INSTALLSIZE constant (!defined at the top of this script) so Add/Remove Programs can accurately report the size
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "EstimatedSize" ${INSTALLSIZE}
        
SectionEnd

# create a section to define what the uninstaller does.
# the section will always be named "Uninstall"
Section "Uninstall"

    # Remove Shortcuts
	Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
    Delete "$SMPROGRAMS\${APPNAME}\Uninstall MetOcean Viewer.lnk"
    Delete "$DESKTOP\MetOcean Viewer.lnk"
    
	# Try to remove the Start Menu folder - this will only happen if it is empty
	RMDir "$SMPROGRAMS\${APPNAME}"
    
    # now delete installed files
    Delete $INSTDIR\d3dcompiler_47.dll
    Delete $INSTDIR\hdf5.dll
    Delete $INSTDIR\hdf5_hl.dll
    Delete $INSTDIR\icudt54.dll
    Delete $INSTDIR\icuin54.dll
    Delete $INSTDIR\icuuc54.dll
    Delete $INSTDIR\libcurl.dll
    Delete $INSTDIR\libeay32.dll
    Delete $INSTDIR\libEGL.dll
    Delete $INSTDIR\libssl32.dll
    Delete $INSTDIR\netcdf.dll
    Delete $INSTDIR\Qt5Core.dll
    Delete $INSTDIR\Qt5Charts.dll
    Delete $INSTDIR\Qt5Gui.dll
    Delete $INSTDIR\Qt5Multimedia.dll
    Delete $INSTDIR\Qt5MultimediaWidgets.dll
    Delete $INSTDIR\Qt5Network.dll
    Delete $INSTDIR\Qt5OpenGL.dll
    Delete $INSTDIR\Qt5Positioning.dll
    Delete $INSTDIR\Qt5PrintSupport.dll
    Delete $INSTDIR\Qt5Qml.dll
    Delete $INSTDIR\Qt5Quick.dll
    Delete $INSTDIR\Qt5Sensors.dll
    Delete $INSTDIR\Qt5Sql.dll
    Delete $INSTDIR\Qt5WebChannel.dll
    Delete $INSTDIR\Qt5WebEngine.dll
    Delete $INSTDIR\Qt5WebEngineCore.dll
    Delete $INSTDIR\Qt5WebEngineWidgets.dll
    Delete $INSTDIR\Qt5Widgets.dll
    Delete $INSTDIR\Qt5Xml.dll
    Delete $INSTDIR\ssleay32.dll
    Delete $INSTDIR\zlib1.dll
    Delete $INSTDIR\msvcp120.dll
    Delete $INSTDIR\msvcr120.dll
    RMDir /r $INSTDIR\imageformats
    RMDir /r $INSTDIR\platforms
    Delete $INSTDIR\mov.ico
    Delete $INSTDIR\MetOcean_Viewer.exe
    Delete /REBOOTOK $INSTDIR\uninstaller.exe
    RMDir $INSTDIR
    
    # Remove uninstaller information from the registry
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
 
SectionEnd
