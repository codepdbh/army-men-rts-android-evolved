param([string]$Serial, [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$adb = Join-Path $env:LOCALAPPDATA 'Android/Sdk/platform-tools/adb.exe'
if (!$Serial) {
    $devices = @(& $adb devices | Where-Object { $_ -match '^([^\s]+)\s+device$' } | ForEach-Object { ($_ -split '\s+')[0] })
    if ($devices.Count -ne 1) { throw 'Connect one device or specify -Serial' }
    $Serial = $devices[0]
}
Push-Location $root
try {
    $ErrorActionPreference = 'Continue'
    if (!$SkipBuild) {
        $env:JAVA_HOME = 'C:/Program Files/Android/Android Studio/jbr'
        & android/gradlew.bat -p android assembleDebug assembleDebugAndroidTest -PamrtsLinkGuest -PamrtsOptimizeGuest --console=plain
        if ($LASTEXITCODE -ne 0) { throw 'Touch test build failed' }
    }
    foreach ($apk in @('android/app/build/outputs/apk/debug/app-debug.apk','android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk')) {
        & $adb -s $Serial install -r $apk
        if ($LASTEXITCODE -ne 0) { throw "Cannot install $apk" }
    }
    $output = & $adb -s $Serial shell am instrument -w com.codepdbh.armymenrts.test/com.codepdbh.armymenrts.TouchInstrumentation 2>&1 | ForEach-Object { $_.ToString() }
    $exitCode = $LASTEXITCODE
    New-Item -ItemType Directory -Force work | Out-Null
    [IO.File]::WriteAllLines((Join-Path $root 'work/touch-device-test.log'), [string[]]$output)
    $output
    if ($exitCode -ne 0 -or !($output -match 'RTS device gestures passed')) { throw 'Device gesture test failed; inspect work/touch-device-test.log' }
} finally { Pop-Location }
