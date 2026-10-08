; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (c) 2026 Zach Cobell
;
; Sections the CPack INNOSETUP generator does not write, included through
; CPACK_INNOSETUP_EXTRA_SCRIPTS (packaging/CMakeLists.txt).

; An upgrade replaces the deployed tree instead of adding to it: a file the
; new version no longer ships (a Qt DLL, a plugin, a QML module) must not stay
; behind and be loaded beside the new ones.
[InstallDelete]
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\plugins"
Type: filesandordirs; Name: "{app}\qml"

; Session files (.mvs, plan §6.19), without taking over the extension: an
; OpenWithProgids entry plus the ProgID, as Microsoft recommends. HKA is HKLM
; for an administrative install and HKCU for a per-user one;
; ChangesAssociations=yes makes Explorer pick it up. The ProgID name is
; repeated in packaging/smoke-test.sh, which checks it (docs/packaging.md,
; "Keep in sync").
[Registry]
Root: HKA; Subkey: "Software\Classes\.mvs\OpenWithProgids"; ValueType: string; ValueName: "MetOceanViewer.Session"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\MetOceanViewer.Session"; ValueType: string; ValueName: ""; ValueData: "MetOceanViewer session"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\MetOceanViewer.Session\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\bin\metoceanviewer.exe,0"
Root: HKA; Subkey: "Software\Classes\MetOceanViewer.Session\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\metoceanviewer.exe"" ""%1"""
