@echo off
setlocal
cd /d "%~dp0.."
mkdir shaders\cso 2>nul
mkdir res\cso 2>nul
set "JOBS=%TEMP%\ogg_cso_jobs.txt"
set "STALE=%TEMP%\ogg_cso_stale.txt"
del /q "%JOBS%" "%STALE%" 2>nul

call :job vs_5_0 VS_Rect shaders\gpu_rect.hlsl gpu_VS_Rect gpu_vs_rect
call :job ps_5_0 PS_Rect shaders\gpu_rect.hlsl gpu_PS_Rect gpu_ps_rect
call :job vs_5_0 VS_PianoKey shaders\gpu_piano.hlsl gpu_VS_PianoKey gpu_vs_piano
call :job cs_5_0 CS_Hex shaders\gpu_hex.hlsl gpu_CS_Hex gpu_cs_hex
call :job cs_5_0 CS_HexGlyph shaders\gpu_hex.hlsl gpu_CS_HexGlyph gpu_cs_glyph

call :job vs_5_0 VST shaders\soft3d_maze.hlsl maze_VST maze_vst
call :job hs_5_0 HST shaders\soft3d_maze.hlsl maze_HST maze_hst
call :job ds_5_0 DST shaders\soft3d_maze.hlsl maze_DST maze_dst
call :job ps_5_0 PSW shaders\soft3d_maze.hlsl maze_PSW maze_psw
call :job vs_5_0 VSS shaders\soft3d_maze.hlsl maze_VSS maze_vss
call :job ps_5_0 PSS shaders\soft3d_maze.hlsl maze_PSS maze_pss
call :job vs_5_0 VSH shaders\soft3d_maze.hlsl maze_VSH maze_vsh
call :job ps_5_0 PSH shaders\soft3d_maze.hlsl maze_PSH maze_psh
call :job vs_5_0 VSQ shaders\soft3d_maze.hlsl maze_VSQ maze_vsq
call :job ps_5_0 SSR shaders\soft3d_maze.hlsl maze_SSR maze_ssr
call :job ps_5_0 DOFP shaders\soft3d_maze.hlsl maze_DOFP maze_dofp
call :job ps_5_0 FIN shaders\soft3d_maze.hlsl maze_FIN maze_fin
call :job ps_5_0 PSLINE shaders\soft3d_maze.hlsl maze_PSLINE maze_psline
call :job ps_5_0 PSMIRF shaders\soft3d_maze.hlsl maze_PSMIRF maze_psmirf
call :job ps_5_0 PSCLoud shaders\soft3d_maze.hlsl maze_PSCLoud maze_pscloud
call :job cs_5_0 CSFx shaders\soft3d_maze.hlsl maze_CSFx maze_csfx
call :job gs_5_0 GSW shaders\soft3d_maze.hlsl maze_GSW maze_gsw
call :job vs_5_0 VSSKIN shaders\soft3d_maze.hlsl maze_VSSKIN maze_vsskin
call :job cs_5_0 CSSub shaders\soft3d_maze.hlsl maze_CSSub maze_cssub

call :job vs_5_0 VST shaders\soft3d_race.hlsl race_VST race_vst
call :job hs_5_0 HST shaders\soft3d_race.hlsl race_HST race_hst
call :job ds_5_0 DST shaders\soft3d_race.hlsl race_DST race_dst
call :job ps_5_0 PSB shaders\soft3d_race.hlsl race_PSB race_psb
call :job vs_5_0 VSS shaders\soft3d_race.hlsl race_VSS race_vss
call :job ps_5_0 PSS shaders\soft3d_race.hlsl race_PSS race_pss
call :job vs_5_0 VSH shaders\soft3d_race.hlsl race_VSH race_vsh
call :job ps_5_0 PSH shaders\soft3d_race.hlsl race_PSH race_psh
call :job vs_5_0 VSQ shaders\soft3d_race.hlsl race_VSQ race_vsq
call :job ps_5_0 SSR shaders\soft3d_race.hlsl race_SSR race_ssr
call :job ps_5_0 DOFP shaders\soft3d_race.hlsl race_DOFP race_dofp
call :job ps_5_0 FIN shaders\soft3d_race.hlsl race_FIN race_fin
call :job cs_5_0 CSNoise shaders\soft3d_race.hlsl race_CSNoise race_csnoise
call :job ps_5_0 PSC shaders\soft3d_race.hlsl race_PSC race_psc
call :job ps_5_0 PSLINE shaders\soft3d_race.hlsl race_PSLINE race_psline
call :job ps_5_0 PST shaders\soft3d_race.hlsl race_PST race_pst
call :job vs_5_0 VSSI shaders\soft3d_race.hlsl race_VSSI race_vssi
call :job ps_5_0 PSW shaders\soft3d_race.hlsl race_PSW race_psw
call :job ps_5_0 PSCLoud shaders\soft3d_race.hlsl race_PSCLoud race_pscloud
call :job gs_5_0 GSW shaders\soft3d_race.hlsl race_GSW race_gsw
call :job vs_5_0 VSW shaders\soft3d_race.hlsl race_VSW race_vsw
call :job cs_5_0 CSGridCount shaders\soft3d_race.hlsl race_CSGridCount race_csgridc
call :job cs_5_0 CSGridWrite shaders\soft3d_race.hlsl race_CSGridWrite race_csgridw

call :job vs_5_0 VSS shaders\omake_tactics.hlsl tac_VSS tac_vss
call :job vs_5_0 VSSB shaders\omake_tactics.hlsl tac_VSSB tac_vssb
call :job vs_5_0 VSSSh shaders\omake_tactics.hlsl tac_VSSSh tac_vsssh
call :job vs_5_0 VSSBSh shaders\omake_tactics.hlsl tac_VSSBSh tac_vssbsh
call :job vs_5_0 VSKSh shaders\omake_tactics.hlsl tac_VSKSh tac_vsksh
call :job vs_5_0 VSK shaders\omake_tactics.hlsl tac_VSK tac_vsk
call :job ps_5_0 PSM shaders\omake_tactics.hlsl tac_PSM tac_psm
call :job ps_5_0 PSO shaders\omake_tactics.hlsl tac_PSO tac_pso
call :job ps_5_0 PSOV shaders\omake_tactics.hlsl tac_PSOV tac_psov
call :job vs_5_0 VSH shaders\omake_tactics.hlsl tac_VSH tac_vsh
call :job ps_5_0 PSH shaders\omake_tactics.hlsl tac_PSH tac_psh
call :job vs_5_0 VSP shaders\omake_tactics.hlsl tac_VSP tac_vsp
call :job vs_5_0 VSPT shaders\omake_tactics.hlsl tac_VSPT tac_vspt
call :job gs_5_0 GSP shaders\omake_tactics.hlsl tac_GSP tac_gsp
call :job ps_5_0 PSP shaders\omake_tactics.hlsl tac_PSP tac_psp
call :job vs_5_0 VSQ shaders\omake_tactics.hlsl tac_VSQ tac_vsq
call :job ps_5_0 PSSKY shaders\omake_tactics.hlsl tac_PSSKY tac_pssky
call :job ps_5_0 PSSKY0 shaders\omake_tactics.hlsl tac_PSSKY0 tac_pssky0
call :job ps_5_0 PSSKY1 shaders\omake_tactics.hlsl tac_PSSKY1 tac_pssky1
call :job ps_5_0 PSSKY2 shaders\omake_tactics.hlsl tac_PSSKY2 tac_pssky2
call :job ps_5_0 PSSKY3 shaders\omake_tactics.hlsl tac_PSSKY3 tac_pssky3
call :job ps_5_0 PSSKY4 shaders\omake_tactics.hlsl tac_PSSKY4 tac_pssky4
call :job ps_5_0 PSSKY5 shaders\omake_tactics.hlsl tac_PSSKY5 tac_pssky5
call :job ps_5_0 PSSKY6 shaders\omake_tactics.hlsl tac_PSSKY6 tac_pssky6
call :job ps_5_0 PSSKY7 shaders\omake_tactics.hlsl tac_PSSKY7 tac_pssky7
call :job ps_5_0 PSSKY8 shaders\omake_tactics.hlsl tac_PSSKY8 tac_pssky8
call :job ps_5_0 PSSKY9 shaders\omake_tactics.hlsl tac_PSSKY9 tac_pssky9
call :job cs_5_0 CSSkin shaders\omake_tactics_skin.hlsl tac_CSSkin tac_csskin
call :job cs_5_0 CSClothStep shaders\omake_tactics_cloth.hlsl tac_CSClothStep tac_csclths
call :job cs_5_0 CSClothRelax shaders\omake_tactics_cloth.hlsl tac_CSClothRelax tac_csclthr
call :job cs_5_0 CSClothWrite shaders\omake_tactics_cloth.hlsl tac_CSClothWrite tac_csclthw
call :job ps_5_0 PSBloom shaders\omake_tactics.hlsl tac_PSBloom tac_psbloom
call :job ps_5_0 PSBlur shaders\omake_tactics.hlsl tac_PSBlur tac_psblur
call :job ps_5_0 PSGrade shaders\omake_tactics.hlsl tac_PSGrade tac_psgrade
call :job ps_5_0 PSUI shaders\omake_tactics.hlsl tac_PSUI tac_psui
call :job ps_5_0 PSG0 shaders\omake_tactics.hlsl tac_PSG0 tac_psg0
call :job ps_5_0 PSG1 shaders\omake_tactics.hlsl tac_PSG1 tac_psg1
call :job ps_5_0 PSG2 shaders\omake_tactics.hlsl tac_PSG2 tac_psg2
call :job ps_5_0 PSG3 shaders\omake_tactics.hlsl tac_PSG3 tac_psg3
call :job ps_5_0 PSG4 shaders\omake_tactics.hlsl tac_PSG4 tac_psg4
call :job ps_5_0 PSG5 shaders\omake_tactics.hlsl tac_PSG5 tac_psg5
call :job ps_5_0 PSG6 shaders\omake_tactics.hlsl tac_PSG6 tac_psg6
call :job ps_5_0 PSG7 shaders\omake_tactics.hlsl tac_PSG7 tac_psg7
call :job ps_5_0 PSG8 shaders\omake_tactics.hlsl tac_PSG8 tac_psg8
call :job ps_5_0 PSG9 shaders\omake_tactics.hlsl tac_PSG9 tac_psg9
call :job ps_5_0 PSGX shaders\omake_tactics.hlsl tac_PSGX tac_psgx
call :job ps_5_0 PSProp shaders\omake_tactics.hlsl tac_PSProp tac_psprop
call :job ps_5_0 PSWater shaders\omake_tactics.hlsl tac_PSWater tac_pswater
call :job ps_5_0 PSLava shaders\omake_tactics.hlsl tac_PSLava tac_pslava
call :job vs_5_0 VST shaders\omake_tactics_tess.hlsl tac_VST tac_vst
call :job hs_5_0 HST shaders\omake_tactics_tess.hlsl tac_HST tac_hst
call :job ds_5_0 DST shaders\omake_tactics_tess.hlsl tac_DST tac_dst
call :job ps_5_0 PSGradeTiny shaders\omake_tactics.hlsl tac_PSGradeTiny tac_psgrtiny
call :job ps_5_0 PSDofH shaders\omake_tactics_dof.hlsl tac_PSDofH tac_psdofh
call :job ps_5_0 PSDofU shaders\omake_tactics_dof.hlsl tac_PSDofU tac_psdofu
call :job cs_5_0 CSSoft shaders\omake_tactics_soft.hlsl tac_CSSoft tac_cssoft
call :job cs_5_0 CSSoftWrite shaders\omake_tactics_soft.hlsl tac_CSSoftWrite tac_cssoftw
call :job cs_5_0 CSHair shaders\omake_tactics_hair.hlsl tac_CSHair tac_cshair
call :job cs_5_0 CSHairWrite shaders\omake_tactics_hair.hlsl tac_CSHairWrite tac_cshairw

powershell -NoProfile -Command ^
  "$jobs = Get-Content -LiteralPath $env:JOBS; $n = 0; $sw = [System.IO.StreamWriter]::new($env:STALE, $false);" ^
  "foreach ($line in $jobs) {" ^
  "  $p = $line.Trim() -split '\s+'; if ($p.Length -lt 5) { continue };" ^
  "  $src = Get-Item -LiteralPath $p[2]; $mid = 'shaders\cso\' + $p[3] + '.cso'; $dst = 'res\cso\' + $p[4] + '.cso';" ^
  "  $need = -not ((Test-Path -LiteralPath $mid) -and (Test-Path -LiteralPath $dst));" ^
  "  if (-not $need) { $t = $src.LastWriteTimeUtc; if ($t -gt (Get-Item -LiteralPath $mid).LastWriteTimeUtc -or $t -gt (Get-Item -LiteralPath $dst).LastWriteTimeUtc) { $need = $true } };" ^
  "  if ($need) { $sw.WriteLine($line); $n++ }" ^
  "}; $sw.Close(); if ($n -eq 0) { exit 0 } else { exit 2 }"
if %errorlevel%==0 (
  echo cso up to date
  del /q "%JOBS%" "%STALE%" 2>nul
  exit /b 0
)
if not %errorlevel%==2 (
  echo shader stamp check failed
  exit /b 1
)

set FXC=
if defined WindowsSdkDir (
  if exist "%WindowsSdkDir%bin\%WindowsSDKVersion%x86\fxc.exe" set "FXC=%WindowsSdkDir%bin\%WindowsSDKVersion%x86\fxc.exe"
)
if exist "D:\Windows Kits\10\bin\10.0.26100.0\x86\fxc.exe" set "FXC=D:\Windows Kits\10\bin\10.0.26100.0\x86\fxc.exe"
if exist "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\fxc.exe" set "FXC=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\fxc.exe"
if "%FXC%"=="" (
  for /d %%D in ("C:\Program Files (x86)\Windows Kits\10\bin\10.*") do (
    if exist "%%D\x86\fxc.exe" set "FXC=%%D\x86\fxc.exe"
  )
)
if "%FXC%"=="" (
  echo fxc.exe not found
  exit /b 1
)

for /f "usebackq tokens=1-5" %%A in ("%STALE%") do (
  echo compile %%B
  call :one %%A %%B %%C %%D %%E
  if errorlevel 1 (
    del /q "%JOBS%" "%STALE%" 2>nul
    exit /b 1
  )
)
echo cso ok
del /q "%JOBS%" "%STALE%" 2>nul
exit /b 0

REM fxc は既存の .cso がマップされていると "failed writing" で落ちる。
REM 未使用の一時ファイルへ出してから、短い再試行で置き換える。
:one
set "TMPO=%TEMP%\oggcso_%RANDOM%_%RANDOM%.cso"
"%FXC%" /nologo /T %1 /E %2 /O3 /Fo "%TMPO%" %3
if errorlevel 1 (
  echo fxc failed %2
  del /q "%TMPO%" 2>nul
  exit /b 1
)
call :place "%TMPO%" "shaders\cso\%~4.cso"
if errorlevel 1 (
  del /q "%TMPO%" 2>nul
  exit /b 1
)
call :place "%TMPO%" "res\cso\%~5.cso"
if errorlevel 1 (
  del /q "%TMPO%" 2>nul
  exit /b 1
)
del /q "%TMPO%" 2>nul
exit /b 0

:place
set /a N=0
:place_try
copy /Y "%~1" "%~2" >nul 2>&1
if not errorlevel 1 exit /b 0
set /a N+=1
if %N% GEQ 12 (
  echo could not replace %~2
  exit /b 1
)
ping -n 2 127.0.0.1 >nul
goto place_try

:job
echo %1 %2 %3 %4 %5>>"%JOBS%"
exit /b 0
