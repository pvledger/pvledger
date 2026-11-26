Unicode true
RequestExecutionLevel admin
Name "PVLedger ${VERSION}"
OutFile "${OUTPUT}"
InstallDir "C:\pvledger"
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\PVLedger"
!include "MUI2.nsh"
!define MUI_ICON "${SOURCE}/windows/pvledger.ico"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "PVLedger"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "PVLedger installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright Vadim Prishlyak"

Function .onInit
  SetRegView 64
  ReadRegStr $0 HKLM "${UNINSTALL_KEY}" "InstallLocation"
  StrCmp $0 "" +2
  StrCpy $INSTDIR $0
FunctionEnd

!macro RequireClosed
  FindWindow $0 "PVLedger.WinHelper"
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONEXCLAMATION "Exit PVLedger from the tray before continuing." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
!macroend

Section "PVLedger"
  !insertmacro RequireClosed
  SetShellVarContext all
  ReadRegStr $0 HKLM "${UNINSTALL_KEY}" "InstallLocation"
  ${If} $0 != ""
  ${AndIf} $0 != $INSTDIR
    MessageBox MB_OK|MB_ICONEXCLAMATION "Uninstall the current copy before changing the installation folder." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  ${If} $0 == ""
  ${AndIf} ${FileExists} "$INSTDIR\."
    MessageBox MB_OK|MB_ICONEXCLAMATION "Choose a new installation folder." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}

  SetOutPath "$INSTDIR"
  ; Users run the binaries here and keep writable settings and SQLite files in Local AppData.
  nsExec::ExecToLog '"$SYSDIR\icacls.exe" "$INSTDIR" /grant:r "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F" "*S-1-5-32-545:(OI)(CI)RX"'
  Pop $1
  ${If} $1 == 0
    nsExec::ExecToLog '"$SYSDIR\icacls.exe" "$INSTDIR" /inheritance:r'
    Pop $1
  ${EndIf}
  ${If} $1 != 0
    RMDir "$INSTDIR"
    MessageBox MB_OK|MB_ICONSTOP "Could not set permissions on the PVLedger installation folder." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}

  File "${STAGE}/pvledger.exe"
  File "${STAGE}/pvledger-win-helper.exe"
  File /r "${STAGE}/share"

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\PVLedger"
  CreateShortcut "$SMPROGRAMS\PVLedger\PVLedger.lnk" "$INSTDIR\pvledger-win-helper.exe"
  CreateShortcut "$SMPROGRAMS\PVLedger\Uninstall PVLedger.lnk" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "PVLedger"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "Vadim Prishlyak"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\pvledger-win-helper.exe"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  !insertmacro RequireClosed
  SetRegView 64
  SetShellVarContext all
  ClearErrors
  Delete "$INSTDIR\pvledger.exe"
  Delete "$INSTDIR\pvledger-win-helper.exe"
  ${If} ${Errors}
    MessageBox MB_OK|MB_ICONSTOP "Close any running PVLedger processes and run the uninstaller again." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  RMDir /r "$INSTDIR\share\pvledger"
  RMDir "$INSTDIR\share"
  Delete "$SMPROGRAMS\PVLedger\PVLedger.lnk"
  Delete "$SMPROGRAMS\PVLedger\Uninstall PVLedger.lnk"
  RMDir "$SMPROGRAMS\PVLedger"
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
