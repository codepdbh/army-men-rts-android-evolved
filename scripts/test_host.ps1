param([string]$VisualStudio = 'C:/Program Files/Microsoft Visual Studio/18/Community')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$installer = 'C:/Program Files (x86)/Microsoft Visual Studio/Installer'
if (Test-Path -LiteralPath "$installer/vswhere.exe") { $env:PATH += ";$installer" }
$cpp = Join-Path $root 'android/app/src/main/cpp'
$work = Join-Path $root 'work/host-tests'
New-Item -ItemType Directory -Force $work | Out-Null
$headers = Join-Path $env:LOCALAPPDATA 'Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include'
$hostHeaders = Join-Path $work 'include'
New-Item -ItemType Directory -Force $hostHeaders | Out-Null
foreach ($directory in @('vulkan','vk_video')) { Copy-Item -LiteralPath (Join-Path $headers $directory) -Destination $hostHeaders -Recurse -Force }
# Build an import library for the system Vulkan loader; no SDK install is needed.
$source = Get-Content (Join-Path $cpp 'vulkan_renderer.cpp') -Raw
$exports = [regex]::Matches($source, '\b(vk[A-Z][A-Za-z0-9]+)\(') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
$exports = $exports | Where-Object { $_ -ne 'vkCreateAndroidSurfaceKHR' }
$definition = @('LIBRARY vulkan-1.dll', 'EXPORTS') + $exports
[IO.File]::WriteAllLines((Join-Path $work 'vulkan.def'), $definition)
$batch = @(
    '@echo off',
    "call `"$VisualStudio/VC/Auxiliary/Build/vcvars64.bat`" >nul",
    'if errorlevel 1 exit /b 1',
    "lib /nologo /def:`"$work/vulkan.def`" /machine:X64 /out:`"$work/vulkan-1.lib`"",
    'if errorlevel 1 exit /b 1',
    "cl /nologo /std:c++17 /EHsc /O2 /I`"$cpp`" /I`"$hostHeaders`" `"$root/tests/native_host_tests.cpp`" `"$cpp/vulkan_renderer.cpp`" /Fe:`"$work/native_host_tests.exe`" /Fo:`"$work/`" /link `"$work/vulkan-1.lib`"",
    'if errorlevel 1 exit /b 1',
    "`"$work/native_host_tests.exe`""
)
[IO.File]::WriteAllLines((Join-Path $work 'build.cmd'), $batch)
$ErrorActionPreference = 'Continue'
& (Join-Path $work 'build.cmd')
if ($LASTEXITCODE -ne 0) { throw 'Native host tests failed' }
# Miles mixer: synthetic WAV always; the menu MP3 from base.x when the game copy is beside the repo.
$audio = Join-Path $root 'work/audio-tests'
New-Item -ItemType Directory -Force $audio | Out-Null
$sample = @()
$base = Join-Path $root '../Army Men RTS/base.x'
if (Test-Path -LiteralPath $base) {
    $bytes = [IO.File]::ReadAllBytes($base)
    $name = [Text.Encoding]::ASCII.GetBytes('shell_theme.mp3')
    for ($i = 0; $i -lt $bytes.Length - $name.Length - 8; ++$i) {
        if ($bytes[$i] -ne $name[0]) { continue }
        $match = $true; for ($j = 1; $j -lt $name.Length; ++$j) { if ($bytes[$i + $j] -ne $name[$j]) { $match = $false; break } }
        if (!$match) { continue }
        $offset = [BitConverter]::ToUInt32($bytes, $i + $name.Length); $size = [BitConverter]::ToUInt32($bytes, $i + $name.Length + 4)
        if ($offset + $size -le $bytes.Length) { $mp3 = Join-Path $audio 'shell_theme.mp3'; [IO.File]::WriteAllBytes($mp3, $bytes[$offset..($offset + $size - 1)]); $sample = @("`"$mp3`"") }
        break
    }
}
$audioBatch = @(
    '@echo off',
    "call `"$VisualStudio/VC/Auxiliary/Build/vcvars64.bat`" >nul",
    'if errorlevel 1 exit /b 1',
    "cl /nologo /std:c++17 /EHsc /O2 /I`"$cpp`" `"$root/tests/audio_mixer_tests.cpp`" `"$cpp/audio_mixer.cpp`" /Fe:`"$audio/audio_mixer_tests.exe`" /Fo:`"$audio/`"",
    'if errorlevel 1 exit /b 1',
    "`"$audio/audio_mixer_tests.exe`" $sample"
)
[IO.File]::WriteAllLines((Join-Path $audio 'build.cmd'), $audioBatch)
& (Join-Path $audio 'build.cmd')
if ($LASTEXITCODE -ne 0) { throw 'Miles mixer tests failed' }
$java = 'C:/Program Files/Android/Android Studio/jbr/bin'
& "$java/javac.exe" -d "$work/java" "$root/android/app/src/main/java/com/codepdbh/armymenrts/RtsTouchController.java" "$root/android/app/src/main/java/com/codepdbh/armymenrts/GameInput.java" "$root/tests/RtsTouchControllerTest.java"
if ($LASTEXITCODE -ne 0) { throw 'Gesture test compilation failed' }
& "$java/java.exe" -cp "$work/java" RtsTouchControllerTest
if ($LASTEXITCODE -ne 0) { throw 'Gesture tests failed' }
