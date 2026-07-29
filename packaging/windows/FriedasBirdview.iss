#ifndef SourceDir
  #error SourceDir must name the windeployqt staging directory.
#endif
#ifndef OutputDir
  #error OutputDir must name the installer output directory.
#endif
#ifndef AppVersion
  #error AppVersion must be supplied by package.ps1.
#endif

#define AppName "FriedasBirdview"

[Setup]
AppId={{4A9F2B11-2D9C-4A94-9B6C-59AD498F31D0}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=FriedasBirdview
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename={#AppName}-{#AppVersion}-Setup-x64
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\bin\friedasbirdview.exe

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "bin\vc_redist.x64.exe"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\bin\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\bin\friedasbirdview.exe"; WorkingDir: "{app}\bin"

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /passive /norestart"; StatusMsg: "Installing Microsoft Visual C++ runtime…"; Flags: waituntilterminated
Filename: "{app}\bin\friedasbirdview.exe"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
