; Inno Setup script for the Windows installer (x64).
; Build: iscc /DAppVersion=0.7.1 /DBuildDir=..\build-windows installer\AutoPitchEff.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build-windows"
#endif
#define Artefacts BuildDir + "\AutoPitchEff_artefacts\Release\VST3"
#define LinkArtefacts BuildDir + "\AutoScaleLink_artefacts\Release\VST3"

[Setup]
AppId={{6F1E2C1A-8B5D-4F7E-9C3A-2D4B5E6F7A81}
AppName=AutoPitchEff
AppVersion={#AppVersion}
AppPublisher=sawada256
AppPublisherURL=https://github.com/sawada256/AutoPitchEff
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
UninstallDisplayName=AutoPitchEff / AutoScaleLink {#AppVersion}
CreateUninstallRegKey=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=AutoPitchEff-{#AppVersion}-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
LicenseFile=..\LICENSE
WizardStyle=modern

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Components]
Name: "pitch"; Description: "AutoPitchEff (VST3)"; Types: full compact custom; Flags: fixed
Name: "link"; Description: "AutoScaleLink (VST3)"; Types: full custom

[Files]
Source: "{#Artefacts}\AutoPitchEff.vst3\*"; DestDir: "{commoncf64}\VST3\AutoPitchEff.vst3"; Components: pitch; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#LinkArtefacts}\AutoScaleLink.vst3\*"; DestDir: "{commoncf64}\VST3\AutoScaleLink.vst3"; Components: link; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\README.md"; DestDir: "{commonappdata}\AutoPitchEff"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{commonappdata}\AutoPitchEff"; Flags: ignoreversion
Source: "..\THIRD_PARTY.md"; DestDir: "{commonappdata}\AutoPitchEff"; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\AutoPitchEff.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\AutoScaleLink.vst3"
Type: filesandordirs; Name: "{commonappdata}\AutoPitchEff"
