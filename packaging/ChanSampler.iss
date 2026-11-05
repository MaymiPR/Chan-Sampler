; Windows installer for Chan Sampler (Inno Setup 7). Built by .github/workflows/release.yml, or locally after a
; Release build of the _VST3, _CLAP and _Standalone targets:
;   pwsh packaging/make_iss_include.ps1 build      (writes packaging/plugins.iss.inc from plugins.txt)
;   iscc /DAppVersion=<version> packaging\ChanSampler.iss          (writes dist\ChanSampler-<version>-Windows-Setup.exe)
; /DTestRoot=<folder> makes a test installer that puts everything under that folder and needs no administrator rights
; (to check the packaging and the uninstaller without touching Program Files).
; The plug-ins install to the root VST3 and CLAP folders, where every release since 1.0 has put them (CONTRIBUTING.md).

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifdef TestRoot
  #define VstDir TestRoot + "\VST3"
  #define ClapDir TestRoot + "\CLAP"
  #define AppDir TestRoot + "\App"
#else
  #define VstDir "{commoncf64}\VST3"
  #define ClapDir "{commoncf64}\CLAP"
  #define AppDir "{autopf}\Chan Sampler"
#endif

[Setup]
AppName=Chan Sampler
AppVersion={#AppVersion}
AppPublisher=Chan Sampler
AppPublisherURL=https://github.com/MaymiPR/Chan-Sampler
AppSupportURL=https://github.com/MaymiPR/Chan-Sampler/issues
DefaultDirName={#AppDir}
DefaultGroupName=Chan Sampler
DisableProgramGroupPage=yes
DisableDirPage=yes
LicenseFile=..\LICENSE
InfoBeforeFile=ABOUT.txt
OutputDir=..\dist
#ifdef TestRoot
OutputBaseFilename=ChanSampler-{#AppVersion}-Windows-Setup-TEST
PrivilegesRequired=lowest
AppId={{8C1B5E2A-4F3D-4B7A-9E61-6A2C0D5F3B12}
#else
OutputBaseFilename=ChanSampler-{#AppVersion}-Windows-Setup
PrivilegesRequired=admin
AppId={{8C1B5E2A-4F3D-4B7A-9E61-6A2C0D5F3B11}
#endif
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Inno Setup 7: a 64-bit Setup.exe (the default would still be 32-bit); AppVerName and TimeStampsInUTC are
; stated because their defaults changed in 7 (name plus version with no localized word; UTC time stamps).
SetupArchitecture=x64
AppVerName=Chan Sampler {#AppVersion}
TimeStampsInUTC=yes
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Chan Sampler
UninstallDisplayIcon={app}\Chan Sampler.exe
CloseApplications=yes

[Types]
; The standalone app is part of the full installation, as in every release since 1.0 (CONTRIBUTING.md).
Name: "full"; Description: "Plug-in (VST3 and CLAP) and the standalone app"
Name: "custom"; Description: "Choose formats"; Flags: iscustom

; [Components], [Files], [Icons] and [UninstallDelete] for every plug-in in plugins.txt
#include "plugins.iss.inc"

[Files]
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "ABOUT.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Uninstall Chan Sampler"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\Chan Sampler.exe"; Description: "Open Chan Sampler"; Components: app\chansampler; Flags: nowait postinstall skipifsilent unchecked
