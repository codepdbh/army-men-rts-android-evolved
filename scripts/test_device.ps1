param([string]$Serial, [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sdkRoot = Join-Path $env:LOCALAPPDATA 'Android/Sdk'
$adb = Join-Path $sdkRoot 'platform-tools/adb.exe'
if (!(Test-Path -LiteralPath $adb)) { throw 'Android platform-tools not found' }
if (!$Serial) {
    $devices = @(& $adb devices | Where-Object { $_ -match '^([^\s]+)\s+device$' } | ForEach-Object { ($_ -split '\s+')[0] })
    if ($devices.Count -ne 1) { throw 'Connect one device or specify -Serial' }
    $Serial = $devices[0]
}
function Invoke-Adb {
    param([string[]]$Arguments)
    $ErrorActionPreference = 'Continue'
    $result = & $adb -s $Serial @Arguments
    if ($LASTEXITCODE -ne 0) { throw "ADB failed: $Arguments" }
    return $result
}
Push-Location $projectRoot
try {
    if (!$SkipBuild) {
        $studioJdk = 'C:/Program Files/Android/Android Studio/jbr'
        if (Test-Path -LiteralPath "$studioJdk/bin/java.exe") { $env:JAVA_HOME = $studioJdk }
        $ErrorActionPreference = 'Continue'
        & android/gradlew.bat -p android assembleDebug -PamrtsLinkGuest --console=plain
        $ErrorActionPreference = 'Stop'
        if ($LASTEXITCODE -ne 0) { throw 'Android build failed' }
    }
    Invoke-Adb -Arguments @('install', '-r', 'android/app/build/outputs/apk/debug/app-debug.apk') | Out-Null
    $requestId = [guid]::NewGuid().ToString('N')
    Invoke-Adb -Arguments @('shell', 'am', 'broadcast', '-n', 'com.codepdbh.armymenrts/.BootProbeReceiver', '--es', 'request_id', $requestId) | Out-Null
    $bootResult = $null
    for ($attempt = 0; $attempt -lt 20; ++$attempt) {
        Start-Sleep -Milliseconds 500
        $log = Invoke-Adb -Arguments @('logcat', '-d', '-s', 'AMRTS:I')
        $bootResult = $log | Where-Object { $_.Contains("Probe result [$requestId]:") } | Select-Object -Last 1
        if ($bootResult) { break }
    }
    if (!$bootResult) { throw 'No headless boot result: inspect logcat for native crash or timeout' }
    New-Item -ItemType Directory -Force work | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $projectRoot 'work/device-boot-result.txt'), "$bootResult`n", [System.Text.UTF8Encoding]::new($false))
    Write-Output $bootResult
} finally { Pop-Location }
