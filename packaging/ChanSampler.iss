; Windows installer (Inno Setup 6). Built by .github/workflows/release.yml:
;   iscc /DAppVersion=1.1.0 /DBuildDir=<build artefacts> packaging\ChanSampler.iss

#ifndef AppVersion
  #define AppVersion "1.1.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\ChanSampler_artefacts\Release"
#endif

[Setup]
AppId={{8C1B5E2A-4F3D-4B7A-9E61-6A2C0D5F3B11}
AppName=Chan Sampler
AppVersion={#AppVersion}
AppPublisher=Chan Sampler
AppPublisherURL=https://github.com/MaymiPR/ChanSampler
AppSupportURL=https://github.com/MaymiPR/ChanSampler/issues
DefaultDirName={autopf}\Chan Sampler
DefaultGroupName=Chan Sampler
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename=ChanSampler-{#AppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Chan Sampler
UninstallDisplayIcon={app}\Chan Sampler.exe

[Types]
Name: "full"; Description: "Plugin and standalone app"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (C:\Program Files\Common Files\VST3)"; Types: full custom
Name: "clap"; Description: "CLAP plugin (C:\Program Files\Common Files\CLAP)"; Types: full custom
Name: "standalone"; Description: "Standalone app"; Types: full

[Files]
Source: "{#BuildDir}\VST3\Chan Sampler.vst3\*"; DestDir: "{commoncf64}\VST3\Chan Sampler.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\CLAP\Chan Sampler.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#BuildDir}\Standalone\Chan Sampler.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\Chan Sampler"; Filename: "{app}\Chan Sampler.exe"; Components: standalone
Name: "{group}\Uninstall Chan Sampler"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\Chan Sampler.exe"; Description: "Open Chan Sampler"; Components: standalone; Flags: nowait postinstall skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Chan Sampler.vst3"
Type: files; Name: "{commoncf64}\CLAP\Chan Sampler.clap"
