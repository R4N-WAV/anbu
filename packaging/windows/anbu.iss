; Anbu for Windows: setup wizard (Inno Setup 6.7 or newer: dark style, custom colours, PNG artwork).
; Artwork: packaging/windows/make_images.py (wizard.png, wizard-small.png, anbu.ico), cut from the rendered panel.
; Built by GitHub Actions (.github/workflows/build.yml) after the plugin has passed its checks:
;   iscc /DAppVersion=1.0.0 /DPluginDir=<folder holding Anbu.vst3> /DOutDir=<output folder> packaging\windows\anbu.iss
; Installs Anbu.vst3 into the VST3 folder (C:\Program Files\Common Files\VST3 unless chosen otherwise),
; replaces an earlier version cleanly, keeps the user's presets and registers an uninstaller (Settings -> Apps).

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef PluginDir
  #define PluginDir "..\..\dist"
#endif
#ifndef OutDir
  #define OutDir "..\..\installer"
#endif

[Setup]
AppId={{A7C4E1B2-3D5F-4A69-8B0E-2F1D9C6A4E57}
AppName=Anbu
AppVersion={#AppVersion}
AppVerName=Anbu {#AppVersion}
AppPublisher=R4N
AppPublisherURL=https://github.com/R4N-WAV/anbu
AppSupportURL=https://github.com/R4N-WAV/anbu/releases/tag/latest
VersionInfoVersion={#AppVersion}
VersionInfoCompany=R4N
VersionInfoDescription=Anbu setup
DefaultDirName={commoncf64}\VST3
DirExistsWarning=no
AppendDefaultDirName=no
DisableProgramGroupPage=yes
DisableWelcomePage=no
UsePreviousAppDir=yes
UninstallDisplayName=Anbu (VST3) by R4N
UninstallDisplayIcon={uninstallexe}
; the uninstaller and the guide live in Program Files\R4N\Anbu, not in the VST3 folder
UninstallFilesDir={commonpf64}\R4N\Anbu
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
WizardStyle=modern dark
WizardBackColor=#160808
WizardImageFile=wizard.png
WizardImageBackColor=#160808
WizardSmallImageFile=wizard-small.png
WizardSmallImageBackColor=#160808
SetupIconFile=anbu.ico
OutputDir={#OutDir}
OutputBaseFilename=Anbu-Setup-Windows
Compression=lzma2/max
SolidCompression=yes
CloseApplications=no

[Messages]
WelcomeLabel1=Welcome to Anbu
WelcomeLabel2=The analog warm color stage by R4N.%n%nAnbu is an optical leveler with a console colour stage, made for vocals: it smooths a performance the way an opto cell does, thickens it with silky harmonics and gives it depth, without ever sounding squashed.%n%nThis installs Anbu {#AppVersion} (VST3, 64-bit) for FL Studio and every other VST3 host, with 32 presets to start from.%n%nPlease close your music app before you continue.
SelectDirLabel3=Anbu.vst3 goes into this VST3 folder. The standard folder is right for almost every host; only change it if your host scans a different one.
FinishedHeadingLabel=Anbu is ready
FinishedLabelNoIcons=Open your music app and rescan your plugins.%n%nFL Studio: Options > Manage plugins > Find installed plugins. Anbu is listed under R4N.%n%nQuick start: put Anbu on a vocal, open the presets at the top (Vocals > Lead Vocal - Smooth) and turn on Auto Gain to hear what it does at equal loudness.%n%nEnjoy.

[InstallDelete]
; replace an earlier version completely (old and new files are never mixed); the user's presets in Documents stay
Type: filesandordirs; Name: "{app}\Anbu.vst3"

[Files]
Source: "{#PluginDir}\Anbu.vst3\*"; DestDir: "{app}\Anbu.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\INSTALL.md"; DestDir: "{commonpf64}\R4N\Anbu"; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{app}\Anbu.vst3"
Type: dirifempty; Name: "{commonpf64}\R4N\Anbu"
Type: dirifempty; Name: "{commonpf64}\R4N"

[Code]
// a host that has the plugin loaded keeps its file locked: say so instead of failing halfway
function PrepareToInstall (var NeedsRestart: Boolean): String;
var
  f: String;
begin
  Result := '';
  f := ExpandConstant ('{app}\Anbu.vst3\Contents\x86_64-win\Anbu.vst3');
  if FileExists (f) and not RenameFile (f, f + '.check') then
    Result := 'Anbu is in use (your music app has it loaded). Close the app and click Back, then Next to try again.'
  else if FileExists (f + '.check') then
    RenameFile (f + '.check', f);
end;
