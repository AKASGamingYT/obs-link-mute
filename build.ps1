$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$cmd = Get-Command gcc -ErrorAction SilentlyContinue
if ($cmd) {
    $gcc = $cmd.Source
} else {
    $gcc = Join-Path $env:LOCALAPPDATA "toolchains\w64devkit\bin\gcc.exe"
}
if (-not (Test-Path $gcc)) { throw "gcc was not found" }
$dlltool = Join-Path (Split-Path $gcc) "dlltool.exe"

$build = Join-Path $root "build"
New-Item -ItemType Directory -Force -Path $build | Out-Null
$flags = @("-O2", "-std=c11", "-fno-short-enums", "-mms-bitfields", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Isrc")

& $dlltool -m i386:x86-64 -d (Join-Path $root "obs-imports.def") -l (Join-Path $build "libobs.a") -D obs.dll
if ($LASTEXITCODE -ne 0) { throw "dlltool failed" }
& $dlltool -m i386:x86-64 -d (Join-Path $root "frontend-imports.def") -l (Join-Path $build "libobsfront.a") -D obs-frontend-api.dll
if ($LASTEXITCODE -ne 0) { throw "frontend dlltool failed" }

& $gcc @flags "-o" (Join-Path $build "test_decide.exe") (Join-Path $root "src\test_decide.c") (Join-Path $root "src\decide.c") "-static-libgcc"
if ($LASTEXITCODE -ne 0) { throw "test compile failed" }

& (Join-Path $build "test_decide.exe")
if ($LASTEXITCODE -ne 0) { throw "decision tests failed" }

& $gcc @flags "-shared" "-static-libgcc" "-o" (Join-Path $build "obs-link-mute.dll") (Join-Path $root "src\plugin.c") (Join-Path $root "src\decide.c") (Join-Path $root "src\dialog.c") "-L$build" "-lobs" "-lobsfront" "-lgdi32"
if ($LASTEXITCODE -ne 0) { throw "plugin compile failed" }

$objdump = Join-Path (Split-Path $gcc) "objdump.exe"
Write-Output "---- dependent DLLs ----"
& $objdump -p (Join-Path $build "obs-link-mute.dll") | Select-String "DLL Name"
Write-Output "---- exports ----"
& $objdump -p (Join-Path $build "obs-link-mute.dll") | Select-String "obs_module_"

$objcopy = Join-Path (Split-Path $gcc) "objcopy.exe"
Copy-Item (Join-Path $build "obs-link-mute.dll") (Join-Path $build "plugin.dll") -Force
Push-Location $build
& $objcopy -I binary -O pe-x86-64 -B i386:x86-64 plugin.dll plugin.o
$code = $LASTEXITCODE
Pop-Location
if ($code -ne 0) { throw "objcopy failed" }
& $gcc "-O2" "-std=c11" "-municode" "-mwindows" "-static" "-static-libgcc" "-o" (Join-Path $build "obs-link-mute-setup.exe") (Join-Path $root "src\setup.c") (Join-Path $build "plugin.o") "-lshell32" "-lole32"
if ($LASTEXITCODE -ne 0) { throw "installer compile failed" }

$dist = Join-Path $root "dist"
$zipRoot = Join-Path $build "zip\obs-link-mute\bin\64bit"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
New-Item -ItemType Directory -Force -Path $zipRoot | Out-Null
Copy-Item (Join-Path $build "obs-link-mute.dll") (Join-Path $zipRoot "obs-link-mute.dll") -Force
Copy-Item (Join-Path $build "obs-link-mute-setup.exe") (Join-Path $dist "obs-link-mute-setup.exe") -Force
$zip = Join-Path $dist "obs-link-mute-windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $build "zip\obs-link-mute") -DestinationPath $zip
Write-Output "packaged $zip"

$destDir = Join-Path $env:ProgramData "obs-studio\plugins\obs-link-mute\bin\64bit"
New-Item -ItemType Directory -Force -Path $destDir | Out-Null
try {
	Copy-Item (Join-Path $build "obs-link-mute.dll") (Join-Path $destDir "obs-link-mute.dll") -Force -ErrorAction Stop
	Write-Output "installed $destDir\obs-link-mute.dll"
} catch {
	Write-Warning "OBS is using the installed plugin, so the live copy was left in place. Restart OBS, then run the setup exe."
}
