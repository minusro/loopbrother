; Inno Setup script for LÖÖP BRÖTHER on Windows: the VST3 into the
; system-wide VST3 folder (C:\Program Files\Common Files\VST3), the app into
; Program Files, the logo font's licence beside it. Unsigned for now
; (docs/RELEASE.md). Names here are ASCII, like the files.
;
; Build: iscc /DVersion=<m.n.p> /DArtefacts=<...\junglemassive_artefacts\Release> loopbrother.iss

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Artefacts
  #define Artefacts "..\..\build\junglemassive_artefacts\Release"
#endif
#define Name "LOOP BROTHER"

[Setup]
; Keep AppId as it is: Windows recognises upgrades and the uninstaller by it.
AppId={{6D4C2E8A-3B71-4F0E-9A52-8C1D7E5B2F94}
AppName={#Name}
AppVersion={#Version}
AppPublisher=minus
DefaultDirName={autopf}\minus\{#Name}
DisableProgramGroupPage=yes
DisableReadyPage=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
Compression=lzma2
SolidCompression=yes
OutputBaseFilename=LOOP-BROTHER-{#Version}-windows
UninstallDisplayName={#Name}
UninstallDisplayIcon={app}\{#Name}.exe

[Files]
Source: "{#Artefacts}\VST3\{#Name}.vst3\*"; DestDir: "{commoncf64}\VST3\{#Name}.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#Artefacts}\Standalone\{#Name}.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\fonts\OFL-CherryBombOne.txt"; DestDir: "{app}\Licences"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#Name}"; Filename: "{app}\{#Name}.exe"
