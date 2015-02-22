#---- LGPL --------------------------------------------------------------------
#
# Copyright (C)  ARCADIS, 2011-2013.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation version 3.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# contact: Zachary Cobell, zachary.cobell@arcadis-us.com
# ARCADIS
# 4999 Pearl East Circle, Suite 200
# Boulder, CO 80301
#
# All indications and logos of, and references to, "ARCADIS"
# are registered trademarks of ARCADIS, and remain the property of
# ARCADIS. All rights reserved.
#
#------------------------------------------------------------------------------
# $Author$
# $Date$
# $Id$
# $HeadURL$
#------------------------------------------------------------------------------
#  File: adcvalidator_installScript.nsi
#
#------------------------------------------------------------------------------
!define APPNAME "ADCIRC Validator"
!define COMPANYNAME "ARCADIS"
!define DESCRIPTION "A simple GUI to view ADCIRC or other model data."

!define VERSIONMAJOR 1
!define VERSIONMINOR 5
!define VERSIONBUILD 97

!define HELPURL "http://www.zachcobell.com/" # "Support Information" link
!define UPDATEURL "http://www.zachcobell.com/" # "Product Updates" link
!define ABOUTURL "http://www.zachcobell.com/" # "Publisher" link

# This is the size (in kB) of all the files copied into "Program Files"
!define INSTALLSIZE 100760

RequestExecutionLevel admin ;Require admin rights on NT6+ (When UAC is turned on)

# define installer name
OutFile "adcvalidator_installer.exe"
 
# set desktop as install directory
InstallDir $PROGRAMFILES32\ADCIRCValidator

LicenseData "LGPL.txt"
Name "${COMPANYNAME} - ${APPNAME}"
Icon "..\img\icon.ico"

!include LogicLib.nsh

!macro VerifyUserIsAdmin
UserInfo::GetAccountType
pop $0
${If} $0 != "admin" ;Require admin rights on NT4+
        messageBox mb_iconstop "Administrator rights required!"
        setErrorLevel 740 ;ERROR_ELEVATION_REQUIRED
        quit
${EndIf}
!macroend
 
function .onInit
	setShellVarContext all
	!insertmacro VerifyUserIsAdmin
functionEnd

#pages
Page license
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

# default section start
Section "Install"
 
    # define output path
    SetOutPath $INSTDIR

    # Output files in the main folder
    File ..\libs\D3DCompiler_43.dll
    File ..\libs\hdf5.dll
    File ..\libs\hdf5_hl.dll
    File ..\libs\icudt53.dll
    File ..\libs\icuin53.dll
    File ..\libs\icuuc53.dll
    File ..\libs\libcurl.dll
    File ..\libs\libeay32.dll
    File ..\libs\libEGL.dll
    File ..\libs\libssl32.dll
    File ..\libs\libstdc++-6.dll
    File ..\libs\libwinpthread-1.dll
    File ..\libs\netcdf.dll
    File ..\libs\Qt5Core.dll
    File ..\libs\Qt5Gui.dll
    File ..\libs\Qt5Multimedia.dll
    File ..\libs\Qt5MultimediaWidgets.dll
    File ..\libs\Qt5Network.dll
    File ..\libs\Qt5OpenGL.dll
    File ..\libs\Qt5Positioning.dll
    File ..\libs\Qt5PrintSupport.dll
    File ..\libs\Qt5Qml.dll
    File ..\libs\Qt5Quick.dll
    File ..\libs\Qt5Sensors.dll
    File ..\libs\Qt5Sql.dll
    File ..\libs\Qt5WebChannel.dll
    File ..\libs\Qt5WebKit.dll
    File ..\libs\Qt5WebKitWidgets.dll
    File ..\libs\Qt5Widgets.dll
    File ..\libs\Qt5Xml.dll
    File ..\libs\ssleay32.dll
    File ..\libs\szip.dll
    File ..\libs\zlib.dll
    File ..\libs\zlib1.dll
    File /r ..\libs\imageformats
    File /r ..\libs\platforms
    File ..\img\icon.ico
    File ..\..\build-ADCValidator-Desktop_Qt_5_4_0_MinGW_32bit-Release\release\ADCValidator.exe
     
    # define uninstaller name
    WriteUninstaller $INSTDIR\uninstaller.exe

    # Start Menu
	CreateDirectory "$SMPROGRAMS\${COMPANYNAME}"
	CreateShortCut "$SMPROGRAMS\${COMPANYNAME}\${APPNAME}.lnk" "$INSTDIR\ADCValidator.exe" "" "$INSTDIR\icon.ico"
    CreateShortCut "$DESKTOP\ADCIRCValidator.lnk" "$INSTDIR\ADCValidator.exe" ""
 
	# Registry information for add/remove programs
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" "DisplayName" "${COMPANYNAME} - ${APPNAME} - ${DESCRIPTION}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" "QuietUninstallString" "$\"$INSTDIR\uninstall.exe$\" /S"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" "InstallLocation" "$\"$INSTDIR$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" "DisplayIcon" "$\"$INSTDIR\logo.ico$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" "Publisher" "$\"${COMPANYNAME}$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" "HelpLink" "$\"${HELPURL}$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" "URLUpdateInfo" "$\"${UPDATEURL}$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" "URLInfoAbout" "$\"${ABOUTURL}$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" "DisplayVersion" "$\"${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}$\""
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" "VersionMajor" ${VERSIONMAJOR}
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" "VersionMinor" ${VERSIONMINOR}
	# There is no option for modifying or repairing the install
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" "NoModify" 1
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" "NoRepair" 1
	# Set the INSTALLSIZE constant (!defined at the top of this script) so Add/Remove Programs can accurately report the size
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" "EstimatedSize" ${INSTALLSIZE}
 
SectionEnd

function un.onInit
	SetShellVarContext all
 
	#Verify the uninstaller - last chance to back out
	MessageBox MB_OKCANCEL "Permanantly remove ${APPNAME}?" IDOK next
		Abort
	next:
	!insertmacro VerifyUserIsAdmin
functionEnd
 
# create a section to define what the uninstaller does.
# the section will always be named "Uninstall"
Section "Uninstall"

    # Remove Start Menu launcher
	Delete "$SMPROGRAMS\${COMPANYNAME}\${APPNAME}.lnk"
	# Try to remove the Start Menu folder - this will only happen if it is empty
	RMDir "$SMPROGRAMS\${COMPANYNAME}"
    
    # now delete installed files
    Delete $INSTDIR\D3DCompiler_43.dll
    Delete $INSTDIR\hdf5.dll
    Delete $INSTDIR\hdf5_hl.dll
    Delete $INSTDIR\icudt53.dll
    Delete $INSTDIR\icuin53.dll
    Delete $INSTDIR\icuuc53.dll
    Delete $INSTDIR\libcurl.dll
    Delete $INSTDIR\libeay32.dll
    Delete $INSTDIR\libEGL.dll
    Delete $INSTDIR\libssl32.dll
    Delete $INSTDIR\libstdc++-6.dll
    Delete $INSTDIR\libwinpthread-1.dll
    Delete $INSTDIR\netcdf.dll
    Delete $INSTDIR\Qt5Core.dll
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
    Delete $INSTDIR\Qt5WebKit.dll
    Delete $INSTDIR\Qt5WebKitWidgets.dll
    Delete $INSTDIR\Qt5Widgets.dll
    Delete $INSTDIR\Qt5Xml.dll
    Delete $INSTDIR\ssleay32.dll
    Delete $INSTDIR\szip.dll
    Delete $INSTDIR\zlib.dll
    Delete $INSTDIR\zlib1.dll
    RMDir /r $INSTDIR\imageformats
    RMDir /r $INSTDIR\platforms
    Delete $INSTDIR\icon.ico
    Delete $INSTDIR\ADCValidator.exe
    Delete $INSTDIR\uninstaller.exe
    RMDir $INSTDIR
    
    # Remove uninstaller information from the registry
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}"
 
SectionEnd