param([string]$GameFolder, [string]$Serial)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (!$GameFolder) { $GameFolder = Join-Path $projectRoot '../Army Men RTS' }
$GameFolder = (Resolve-Path -LiteralPath $GameFolder).Path
$adb = Join-Path $env:LOCALAPPDATA 'Android/Sdk/platform-tools/adb.exe'
if (!$Serial) {
    $devices = @(& $adb devices | Where-Object { $_ -match '^([^\s]+)\s+device$' } | ForEach-Object { ($_ -split '\s+')[0] })
    if ($devices.Count -ne 1) { throw 'Connect one device or specify -Serial' }
    $Serial = $devices[0]
}
$required = @('AMRTS.EXE', 'base.x', 'base.x-s', 'binkw32.dll', 'mss32.dll')
foreach ($file in $required) {
    if (!(Test-Path -LiteralPath (Join-Path $GameFolder $file) -PathType Leaf)) { throw "Missing game file: $file" }
}
$ErrorActionPreference = 'Continue'
& $adb -s $Serial shell mkdir -p /sdcard/armymenrts
if ($LASTEXITCODE -ne 0) { throw 'Cannot create Android data folder' }
$data = $required + @('base', 'client', 'library', 'missions', 'mods', 'sides', 'worlds')
foreach ($entry in $data) {
    $source = Join-Path $GameFolder $entry
    if (!(Test-Path -LiteralPath $source)) { continue }
    $result = & $adb -s $Serial push --sync $source /sdcard/armymenrts/ 2>&1 | ForEach-Object { $_.ToString() }
    if ($LASTEXITCODE -ne 0) { throw "ADB copy failed: $entry" }
    $result | Select-Object -Last 1
}
# Preserve configuration/profile data already produced on Android.
foreach ($entry in @('settings.cfg', 'user/data.cfg')) {
    $source = Join-Path $GameFolder $entry
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) { continue }
    & $adb -s $Serial shell test -e "/sdcard/armymenrts/$entry"
    if ($LASTEXITCODE -eq 0) { continue }
    if ($entry -eq 'user/data.cfg') { & $adb -s $Serial shell mkdir -p /sdcard/armymenrts/user }
    & $adb -s $Serial push $source "/sdcard/armymenrts/$entry" 2>&1 | ForEach-Object { $_.ToString() }
    if ($LASTEXITCODE -ne 0) { throw "ADB configuration copy failed: $entry" }
}
Write-Output 'Game assets copied to Memoria interna/armymenrts'
