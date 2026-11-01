# Builds, tests and packages the Windows x64 release (VST3 ZIP + installer).
# Requires Visual Studio 2022 (C++), CMake and Inno Setup 6.
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
$version = (Select-String -Path CMakeLists.txt -Pattern 'project\(AutoPitchEff VERSION ([0-9.]+)').Matches[0].Groups[1].Value
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build build-windows --config Release --parallel
if ($LASTEXITCODE) { exit $LASTEXITCODE }
ctest --test-dir build-windows -C Release --output-on-failure
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# Load both plug-ins in a real host (pluginval) when requested (CI).
if ($env:APE_PLUGINVAL) {
  $pv = Join-Path $env:RUNNER_TEMP "pluginval"
  Invoke-WebRequest "https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Windows.zip" -OutFile "$pv.zip"
  Expand-Archive "$pv.zip" -DestinationPath $pv -Force
  foreach ($name in "AutoPitchEff", "AutoScaleLink") {
    & "$pv\pluginval.exe" --strictness-level 5 --validate-in-process --validate "build-windows/${name}_artefacts/Release/VST3/$name.vst3"
    if ($LASTEXITCODE) { Write-Error "pluginval failed for $name"; exit $LASTEXITCODE }
  }
}

New-Item -ItemType Directory -Force dist | Out-Null
$stage = Join-Path "dist" "AutoPitchEff-$version-Windows-x64"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item -Recurse "build-windows/AutoPitchEff_artefacts/Release/VST3/AutoPitchEff.vst3" $stage
Copy-Item -Recurse "build-windows/AutoScaleLink_artefacts/Release/VST3/AutoScaleLink.vst3" $stage
Copy-Item README.md, LICENSE, THIRD_PARTY.md, CHANGELOG.md $stage
Copy-Item -Recurse licenses $stage
Compress-Archive -Path $stage -DestinationPath "dist/AutoPitchEff-$version-Windows-x64.zip" -Force

$iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
if (-not (Test-Path $iscc)) { choco install innosetup -y --no-progress; $iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
& $iscc "/DAppVersion=$version" "/DBuildDir=..\build-windows" installer\AutoPitchEff.iss
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Get-ChildItem dist
