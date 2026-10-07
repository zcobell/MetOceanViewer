; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (c) 2026 Zach Cobell
;
; Registers MetOceanViewer for session files (.mvs, plan §6.19) without
; taking over the extension: an OpenWithProgids entry plus the ProgID, as
; Microsoft recommends. HKA is HKLM for an administrative install and HKCU for
; a per-user one; ChangesAssociations=yes (packaging/CMakeLists.txt) makes
; Explorer pick it up. Included by the CPack INNOSETUP generator
; (CPACK_INNOSETUP_EXTRA_SCRIPTS).

[Registry]
Root: HKA; Subkey: "Software\Classes\.mvs\OpenWithProgids"; ValueType: string; ValueName: "MetOceanViewer.Session"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\MetOceanViewer.Session"; ValueType: string; ValueName: ""; ValueData: "MetOceanViewer session"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\MetOceanViewer.Session\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\bin\metoceanviewer.exe,0"
Root: HKA; Subkey: "Software\Classes\MetOceanViewer.Session\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\metoceanviewer.exe"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Applications\metoceanviewer.exe\SupportedTypes"; ValueType: string; ValueName: ".mvs"; ValueData: ""; Flags: uninsdeletekey
