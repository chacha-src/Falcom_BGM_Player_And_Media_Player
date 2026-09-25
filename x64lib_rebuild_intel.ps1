$ErrorActionPreference = 'Continue'
$msb = 'd:\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'
if (!(Test-Path $msb)) {
  $msb = 'd:\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
}
if (!(Test-Path $msb)) { throw "MSBuild (VS2026) not found" }
Write-Host "MSB=$msb"

$dst = 'C:\projects\APPLICATION3\ogg_all2022\x64lib'
$opt = 'C:\projects\APPLICATION3\ogg_all2022\x64lib_intel_opt.props'
New-Item -ItemType Directory -Force $dst | Out-Null

$toolset = 'v145'
$toolsetDir = 'd:\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Platforms\x64\PlatformToolsets\v145'
if (!(Test-Path $toolsetDir)) { throw "MSVC v145 x64 toolset missing: $toolsetDir" }

function Build-Lib($name, $proj, $cfg, $plat, $extra) {
  if (!(Test-Path $proj)) {
    Write-Host "MISSING_PROJ $name $proj"
    return 99
  }
  Write-Host "==== $name $cfg|$plat MSVC AVX2 (AVX512 は専用 TU + CPUID) ===="
  $log = Join-Path $dst "_build_$name.log"
  $msargs = @(
    $proj, '/t:Rebuild',
    "/p:Configuration=$cfg", "/p:Platform=$plat",
    "/p:PlatformToolset=$toolset",
    '/p:WindowsTargetPlatformVersion=10.0',
    '/p:PreferredToolArchitecture=x64',
    "/p:ForceImportAfterCppTargets=$opt",
    '/m', '/v:m', '/nologo'
  )
  if ($extra) { $msargs += $extra }
  & $msb @msargs 2>&1 | Tee-Object -FilePath $log | Select-Object -Last 25
  $code = $LASTEXITCODE
  Write-Host "EXIT_$name=$code"
  return $code
}

function Copy-Named($srcGlob, $destName) {
  $f = Get-Item $srcGlob -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if ($f) {
    Copy-Item $f.FullName (Join-Path $dst $destName) -Force
    Write-Host "COPIED $($f.FullName) -> $destName ($($f.Length) bytes)"
    return $true
  }
  Write-Host "MISSING $srcGlob"
  return $false
}

$r = [ordered]@{}
$r.ogg = Build-Lib 'libogg' 'C:\ogg\libogg\win32\VS2015\libogg.vcxproj' 'Release' 'x64'
$r.vorbis = Build-Lib 'vorbis' 'C:\ogg\libvorbis\win32\VS2010\libvorbis\libvorbis_static.vcxproj' 'Release' 'x64'
$r.vorbisfile = Build-Lib 'vorbisfile' 'C:\ogg\libvorbis\win32\VS2010\libvorbisfile\libvorbisfile_static.vcxproj' 'Release' 'x64'
$r.flac = Build-Lib 'flac' 'C:\ogg\libflac\src\libFLAC\libFLAC_static.vcxproj' 'Release' 'x64' @('/p:SolutionDir=C:\ogg\libflac\', '/p:OutDir=C:\ogg\libflac\objs\x64\Release\lib\')
$r.opus = Build-Lib 'opus' 'C:\projects\APPLICATION3\opus-master\win32\VS2022\opus.vcxproj' 'releaseAVX' 'x64'
$r.mad = Build-Lib 'libmad' 'C:\projects\APPLICATION3\libmad-0.15.1b\msvc++\libmad.vcxproj' 'Release' 'x64'
$r.soxr = Build-Lib 'soxr' 'C:\projects\APPLICATION3\soxr-0.1.3-Source\msvc\libsoxr.vcxproj' 'Release' 'x64'
$r.zlib = Build-Lib 'zlib' 'C:\projects\APPLICATION3\zlib-develop\build-VS2026\libz-static\libz-static.vcxproj' 'Release' 'x64'
$r.zstd = Build-Lib 'zstd' 'C:\projects\APPLICATION4\zstd-dev\build\VS2010\libzstd\libzstd.vcxproj' 'Release' 'x64'
$r.rubber = Build-Lib 'rubberband' 'C:\projects\APPLICATION4\rubberband-4.0.0\otherbuilds\rubberband-library.vcxproj' 'Release' 'x64'

$opusInc = 'C:\projects\APPLICATION3\opus-master\include;C:\ogg\include;C:\projects\APPLICATION3\opusfile-0.12\include'
Write-Host '==== opusfile avx|x64 MSVC AVX2 ===='
$log = Join-Path $dst '_build_opusfile.log'
& $msb 'C:\projects\APPLICATION3\opusfile-0.12\win32\VS2015\opusfile.vcxproj' '/t:Rebuild' `
  '/p:Configuration=avx' '/p:Platform=x64' `
  "/p:PlatformToolset=$toolset" `
  '/p:WindowsTargetPlatformVersion=10.0' `
  '/p:PreferredToolArchitecture=x64' `
  "/p:ForceImportAfterCppTargets=$opt" `
  "/p:IncludePath=`"$opusInc`"" `
  '/m' '/v:m' '/nologo' 2>&1 | Tee-Object -FilePath $log | Select-Object -Last 25
$r.opusfile = $LASTEXITCODE
Write-Host "EXIT_opusfile=$($r.opusfile)"

Copy-Named 'C:\ogg\libogg\win32\VS2015\x64\Release\libogg.lib' 'libogg_static_avx2_2026.lib' | Out-Null
Copy-Named 'C:\ogg\libvorbis\win32\VS2010\libvorbis\x64\Release\libvorbis_static.lib' 'libvorbis_static_avx2_2026.lib' | Out-Null
Copy-Named 'C:\ogg\libvorbis\win32\VS2010\libvorbisfile\x64\Release\libvorbisfile_static.lib' 'libvorbisfile_static_avx2_2026.lib' | Out-Null
if (-not (Test-Path (Join-Path $dst 'libvorbisfile_static_avx2_2026.lib'))) {
  Copy-Named 'C:\ogg\libvorbis\win32\VS2010\libvorbisfile\x64\Release\libvorbisfile.lib' 'libvorbisfile_static_avx2_2026.lib' | Out-Null
}
Copy-Named 'C:\ogg\libflac\objs\x64\Release\lib\libFLAC_static.lib' 'libFLAC_static_avx2_2026.lib' | Out-Null
Copy-Named 'C:\projects\APPLICATION3\opus-master\win32\VS2022\x64\releaseAVX\opus.lib' 'opus_avx2_2026.lib' | Out-Null
# libmad の OutputFile は x64lib\libmad_avx2_2026.lib 直書き
Copy-Named 'C:\projects\APPLICATION3\libmad-0.15.1b\msvc++\x64\Release\libmad.lib' 'libmad_avx2_2026.lib' | Out-Null
Copy-Named 'C:\projects\APPLICATION3\soxr-0.1.3-Source\msvc\x64\Release\libsoxr.lib' 'libsoxr_avx2_2026.lib' | Out-Null
Copy-Named 'C:\projects\APPLICATION3\zlib-develop\build-VS2026\libz-static\x64\Release\libz-static.lib' 'libz-static.lib' | Out-Null
Copy-Named 'C:\projects\APPLICATION4\zstd-dev\build\VS2010\libzstd\bin\x64_Release\libzstd_static.lib' 'libzstd_static.lib' | Out-Null
Copy-Named 'C:\projects\APPLICATION4\rubberband-4.0.0\otherbuilds\x64\Release\rubberband-library.lib' 'rubberband-library_2026.lib' | Out-Null
Copy-Named 'C:\projects\APPLICATION3\opusfile-0.12\win32\VS2015\x64\avx\opusfile.lib' 'opusfile_avx2_2026.lib' | Out-Null

# rubberband alias used by older link lines
$rb = Join-Path $dst 'rubberband-library_2026.lib'
if (Test-Path $rb) { Copy-Item $rb (Join-Path $dst 'rubberband-library.lib') -Force }

Write-Host '==== x64lib ===='
Get-ChildItem $dst -File | Where-Object { $_.Extension -eq '.lib' } | Format-Table Name, Length, LastWriteTime
Write-Host '==== exits ===='
$r.GetEnumerator() | ForEach-Object { Write-Host "$($_.Key)=$($_.Value)" }
