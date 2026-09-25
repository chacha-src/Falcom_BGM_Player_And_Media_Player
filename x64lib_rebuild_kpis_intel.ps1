$ErrorActionPreference = 'Continue'
$env:KBMPLAY_BASEPATH = 'C:\KbMedia\_bin'
$root = 'C:\projects\APPLICATION3\kpi_sources_20240712'
$opt = 'C:\projects\APPLICATION3\ogg_all2022\x64lib_kpi_opt.props'
$dst = 'C:\projects\APPLICATION3\ogg_binary\Plugins'
$dst64 = 'C:\projects\APPLICATION3\ogg_binary\x64\Plugins'
$kb64 = 'C:\KbMedia\_bin\x64\Plugins'
$kb86 = 'C:\KbMedia\_bin\x86\Plugins'
$log = 'C:\projects\APPLICATION3\ogg_all2022\x64lib\_build_kpis_intel.log'
$msb18 = 'd:\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'
"KPI MSVC AVX2 x64/Win32 start $(Get-Date)" | Set-Content $log

$skipName = '(pmdwin|viopsf2|aodsf|in_vio2sf|wavext|mpg123asm|libwavpack|pxtone|opusfile|opus\.vcxproj|ogg_static|kblibpng|kbzlib|UnRARDll|GuruGuruSMF4|wsr_player_|dde_test)'
$kpiProjs = @()
Get-ChildItem $root -Recurse -Filter '*.vcxproj' | ForEach-Object {
  if ($_.FullName -match '\\(_temp|tmp|\.vs|bin|obj)\\') { return }
  if ($_.Name -match $skipName) { return }
  $t = Get-Content $_.FullName -Raw -ErrorAction SilentlyContinue
  if ($t -and $t -match '<TargetExt>\s*\.kpi\s*</TargetExt>') {
    $kpiProjs += [pscustomobject]@{ Path = $_.FullName; Text = $t; Name = $_.BaseName }
  }
}
"projects=$($kpiProjs.Count)" | Tee-Object -FilePath $log -Append

function Has-Cfg($text, $cfg, $plat) {
  return [bool]($text -match [regex]::Escape("Include=`"$cfg|$plat`""))
}

$ok = 0; $ng = 0; $skip = 0
$failList = @()

foreach ($p in $kpiProjs) {
  if (-not (Has-Cfg $p.Text 'Release' 'x64')) {
    "SKIP no Release|x64 $($p.Path)" | Tee-Object -FilePath $log -Append
    $skip++
    continue
  }
  "=== x64 MSVC $($p.Name) ===" | Tee-Object -FilePath $log -Append
  & $msb18 $p.Path '/t:Rebuild' '/p:Configuration=Release' '/p:Platform=x64' `
    '/p:PlatformToolset=v145' '/p:PreferredToolArchitecture=x64' `
    "/p:ForceImportAfterCppTargets=$opt" '/p:WholeProgramOptimization=false' `
    '/p:WindowsTargetPlatformVersion=10.0' '/p:EnableEnhancedInstructionSet=AdvancedVectorExtensions2' `
    '/m:1' '/v:m' '/nologo' 2>&1 |
    Select-Object -Last 12 | Tee-Object -FilePath $log -Append
  if ($LASTEXITCODE -eq 0) { $ok++ } else { $ng++; $failList += "x64 $($p.Name)"; "FAIL x64 $($p.Path)" | Tee-Object -FilePath $log -Append }
}

# Win32 も VS2026 MSVC AVX2（OpenMP なし）
if (Test-Path $msb18) {
  foreach ($p in $kpiProjs) {
    if (-not (Has-Cfg $p.Text 'Release' 'Win32')) {
      $skip++
      continue
    }
    "=== Win32 MSVC $($p.Name) ===" | Tee-Object -FilePath $log -Append
    & $msb18 $p.Path '/t:Rebuild' '/p:Configuration=Release' '/p:Platform=Win32' `
      '/p:PlatformToolset=v145' '/p:PreferredToolArchitecture=x64' `
      "/p:ForceImportAfterCppTargets=$opt" '/p:WholeProgramOptimization=false' `
      '/p:WindowsTargetPlatformVersion=10.0' '/p:EnableEnhancedInstructionSet=AdvancedVectorExtensions2' `
      '/m:1' '/v:m' '/nologo' 2>&1 |
      Select-Object -Last 8 | Tee-Object -FilePath $log -Append
    if ($LASTEXITCODE -eq 0) { $ok++ } else { $ng++; $failList += "Win32 $($p.Name)"; "FAIL Win32 $($p.Path)" | Tee-Object -FilePath $log -Append }
  }
} else {
  "NO VS18 MSBuild, skip Win32" | Tee-Object -FilePath $log -Append
}

# 同名ファイルを x64 ソースから x86 ツリーへ上書きすると viopsf.bin 等が潰れる。
# コピー先はソースと同じアーキの Plugins だけ。kbzlib は別途 Restore。
$skipCopyName = '^(kbzlib\.dll)$'

function Copy-KpiTree([string]$srcRoot, [string]$oggRoot) {
  if (-not $srcRoot -or -not $oggRoot) { return }
  if (-not (Test-Path $srcRoot)) { return }
  if (-not (Test-Path $oggRoot)) { return }
  Get-ChildItem $srcRoot -Recurse -Include *.kpi,*.dll,*.bin -File -ErrorAction SilentlyContinue | ForEach-Object {
    $rel = $_.FullName.Substring($srcRoot.Length).TrimStart('\','/')
    $name = $_.Name
    if ($name -match $skipCopyName) { return }
    $copied = $false
    $existing = Get-ChildItem $oggRoot -Recurse -Filter $name -ErrorAction SilentlyContinue
    foreach ($e in $existing) {
      Copy-Item $_.FullName $e.FullName -Force
      "COPIED $($_.FullName) -> $($e.FullName)" | Tee-Object -FilePath $log -Append
      $copied = $true
    }
    if (-not $copied) {
      $destDir = Join-Path $oggRoot (Split-Path $rel -Parent)
      New-Item -ItemType Directory -Force $destDir | Out-Null
      Copy-Item $_.FullName (Join-Path $destDir $name) -Force
      "COPIED_NEW $($_.FullName) -> $destDir\$name" | Tee-Object -FilePath $log -Append
    }
  }
}

function Restore-Kbzlib([string]$srcDll, [string]$root) {
  if (-not (Test-Path $srcDll)) { "KBZLIB MISS $srcDll" | Tee-Object -FilePath $log -Append; return }
  if (-not (Test-Path $root)) { return }
  Get-ChildItem $root -Recurse -Filter 'kbzlib.dll' -File -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item $srcDll $_.FullName -Force
    "KBZLIB $($_.FullName)" | Tee-Object -FilePath $log -Append
  }
}

"=== DEPLOY ===" | Tee-Object -FilePath $log -Append
Copy-KpiTree $kb64 $dst64
if (Test-Path $kb86) { Copy-KpiTree $kb86 $dst }
Restore-Kbzlib 'C:\KbMedia\_bin\x64\kbzlib.dll' $dst64
Restore-Kbzlib 'C:\KbMedia\_bin\x86\kbzlib.dll' $dst
$rootZlib = Join-Path $dst 'kbzlib.dll'
if ((Test-Path 'C:\KbMedia\_bin\x86\kbzlib.dll') -and (Test-Path $rootZlib)) {
  Copy-Item 'C:\KbMedia\_bin\x86\kbzlib.dll' $rootZlib -Force
  "KBZLIB $rootZlib" | Tee-Object -FilePath $log -Append
}

# dump 付き KPI を .ogg_kpi_fmmon にアーキ別で積む（SilentUpdate の供給元）
python 'C:\projects\APPLICATION3\ogg_all2022\.cursor\_fill_fmmon_bundle.py' 2>&1 | Tee-Object -FilePath $log -Append

# kbsasami は ogg リポジトリ側
$sas = 'C:\projects\APPLICATION3\ogg_binary\Plugins\kbsasami'
if (Test-Path $sas) {
  "kbsasami already under Plugins (ogg repo build)" | Tee-Object -FilePath $log -Append
}

"ok=$ok ng=$ng skip=$skip done $(Get-Date)" | Tee-Object -FilePath $log -Append
if ($failList.Count) {
  "FAILS:" | Tee-Object -FilePath $log -Append
  $failList | Tee-Object -FilePath $log -Append
}

"=== ZIP kbMonitorUsed_fix_src.zip（差分のみ。kpi_sources 一式は入れない） ===" | Tee-Object -FilePath $log -Append
python 'C:\projects\APPLICATION3\ogg_all2022\.cursor\_zip_kbpmd_fix_src.py' 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { "ZIP FAIL" | Tee-Object -FilePath $log -Append }
else { "ZIP ok $(Join-Path $dst 'kbMonitorUsed_fix_src.zip')" | Tee-Object -FilePath $log -Append }

