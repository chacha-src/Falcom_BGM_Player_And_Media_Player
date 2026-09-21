@echo off
setlocal
cd /d "%~dp0.."
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
mkdir shaders\cso 2>nul
mkdir res\cso 2>nul
for %%F in (
  gpu_vs_rect gpu_ps_rect gpu_vs_piano gpu_cs_hex gpu_cs_glyph
  maze_vst maze_hst maze_dst maze_psw maze_vss maze_pss maze_vsh maze_psh maze_vsq maze_ssr maze_dofp maze_fin maze_psline maze_psmirf maze_pscloud maze_csfx maze_gsw maze_vsskin
  race_vst race_hst race_dst race_psb race_vss race_pss race_vsh race_psh race_vsq race_ssr race_dofp race_fin race_csnoise race_psc race_psline race_pst race_vssi race_psw race_pscloud race_gsw race_vsw
) do if not exist res\cso\%%F.cso goto :needfxc
powershell -NoProfile -Command "if (-not (Get-Item -ErrorAction SilentlyContinue res\cso\*.cso)) { exit 1 }; $h = @(Get-Item shaders\*.hlsl); $c = @(Get-Item res\cso\*.cso); if (($c | Measure-Object LastWriteTime -Minimum).Minimum -ge ($h | Measure-Object LastWriteTime -Maximum).Maximum) { exit 0 } else { exit 1 }"
if not errorlevel 1 (
  echo cso up to date
  exit /b 0
)
:needfxc
if "%FXC%"=="" (
  echo fxc.exe not found
  exit /b 1
)
:compile

call :cc vs_5_0 VS_Rect shaders\gpu_rect.hlsl gpu_VS_Rect gpu_vs_rect
if errorlevel 1 exit /b 1
call :cc ps_5_0 PS_Rect shaders\gpu_rect.hlsl gpu_PS_Rect gpu_ps_rect
if errorlevel 1 exit /b 1
call :cc vs_5_0 VS_PianoKey shaders\gpu_piano.hlsl gpu_VS_PianoKey gpu_vs_piano
if errorlevel 1 exit /b 1
call :cc cs_5_0 CS_Hex shaders\gpu_hex.hlsl gpu_CS_Hex gpu_cs_hex
if errorlevel 1 exit /b 1
call :cc cs_5_0 CS_HexGlyph shaders\gpu_hex.hlsl gpu_CS_HexGlyph gpu_cs_glyph
if errorlevel 1 exit /b 1

call :cc vs_5_0 VST shaders\soft3d_maze.hlsl maze_VST maze_vst
if errorlevel 1 exit /b 1
call :cc hs_5_0 HST shaders\soft3d_maze.hlsl maze_HST maze_hst
if errorlevel 1 exit /b 1
call :cc ds_5_0 DST shaders\soft3d_maze.hlsl maze_DST maze_dst
if errorlevel 1 exit /b 1
call :cc ps_5_0 PSW shaders\soft3d_maze.hlsl maze_PSW maze_psw
if errorlevel 1 exit /b 1
call :cc vs_5_0 VSS shaders\soft3d_maze.hlsl maze_VSS maze_vss
if errorlevel 1 exit /b 1
call :cc ps_5_0 PSS shaders\soft3d_maze.hlsl maze_PSS maze_pss
if errorlevel 1 exit /b 1
call :cc vs_5_0 VSH shaders\soft3d_maze.hlsl maze_VSH maze_vsh
if errorlevel 1 exit /b 1
call :cc ps_5_0 PSH shaders\soft3d_maze.hlsl maze_PSH maze_psh
if errorlevel 1 exit /b 1
call :cc vs_5_0 VSQ shaders\soft3d_maze.hlsl maze_VSQ maze_vsq
if errorlevel 1 exit /b 1
call :cc ps_5_0 SSR shaders\soft3d_maze.hlsl maze_SSR maze_ssr
if errorlevel 1 exit /b 1
call :cc ps_5_0 DOFP shaders\soft3d_maze.hlsl maze_DOFP maze_dofp
if errorlevel 1 exit /b 1
call :cc ps_5_0 FIN shaders\soft3d_maze.hlsl maze_FIN maze_fin
if errorlevel 1 exit /b 1
call :cc ps_5_0 PSLINE shaders\soft3d_maze.hlsl maze_PSLINE maze_psline
if errorlevel 1 exit /b 1
call :cc ps_5_0 PSMIRF shaders\soft3d_maze.hlsl maze_PSMIRF maze_psmirf
if errorlevel 1 exit /b 1
call :cc ps_5_0 PSCLoud shaders\soft3d_maze.hlsl maze_PSCLoud maze_pscloud
if errorlevel 1 exit /b 1
call :cc cs_5_0 CSFx shaders\soft3d_maze.hlsl maze_CSFx maze_csfx
if errorlevel 1 exit /b 1
call :cc gs_5_0 GSW shaders\soft3d_maze.hlsl maze_GSW maze_gsw
if errorlevel 1 exit /b 1
call :cc vs_5_0 VSSKIN shaders\soft3d_maze.hlsl maze_VSSKIN maze_vsskin
if errorlevel 1 exit /b 1

call :cc vs_5_0 VST shaders\soft3d_race.hlsl race_VST race_vst
if errorlevel 1 exit /b 1
call :cc hs_5_0 HST shaders\soft3d_race.hlsl race_HST race_hst
if errorlevel 1 exit /b 1
call :cc ds_5_0 DST shaders\soft3d_race.hlsl race_DST race_dst
if errorlevel 1 exit /b 1
call :cc ps_5_0 PSB shaders\soft3d_race.hlsl race_PSB race_psb
if errorlevel 1 exit /b 1
call :cc vs_5_0 VSS shaders\soft3d_race.hlsl race_VSS race_vss
if errorlevel 1 exit /b 1
call :cc ps_5_0 PSS shaders\soft3d_race.hlsl race_PSS race_pss
if errorlevel 1 exit /b 1
call :cc vs_5_0 VSH shaders\soft3d_race.hlsl race_VSH race_vsh
if errorlevel 1 exit /b 1
call :cc ps_5_0 PSH shaders\soft3d_race.hlsl race_PSH race_psh
if errorlevel 1 exit /b 1
call :cc vs_5_0 VSQ shaders\soft3d_race.hlsl race_VSQ race_vsq
if errorlevel 1 exit /b 1
call :cc ps_5_0 SSR shaders\soft3d_race.hlsl race_SSR race_ssr
if errorlevel 1 exit /b 1
call :cc ps_5_0 DOFP shaders\soft3d_race.hlsl race_DOFP race_dofp
if errorlevel 1 exit /b 1
call :cc ps_5_0 FIN shaders\soft3d_race.hlsl race_FIN race_fin
if errorlevel 1 exit /b 1
call :cc cs_5_0 CSNoise shaders\soft3d_race.hlsl race_CSNoise race_csnoise
if errorlevel 1 exit /b 1
call :cc ps_5_0 PSC shaders\soft3d_race.hlsl race_PSC race_psc
if errorlevel 1 exit /b 1
call :cc ps_5_0 PSLINE shaders\soft3d_race.hlsl race_PSLINE race_psline
if errorlevel 1 exit /b 1
call :cc ps_5_0 PST shaders\soft3d_race.hlsl race_PST race_pst
if errorlevel 1 exit /b 1
call :cc vs_5_0 VSSI shaders\soft3d_race.hlsl race_VSSI race_vssi
if errorlevel 1 exit /b 1
call :cc ps_5_0 PSW shaders\soft3d_race.hlsl race_PSW race_psw
if errorlevel 1 exit /b 1
call :cc ps_5_0 PSCLoud shaders\soft3d_race.hlsl race_PSCLoud race_pscloud
if errorlevel 1 exit /b 1
call :cc gs_5_0 GSW shaders\soft3d_race.hlsl race_GSW race_gsw
if errorlevel 1 exit /b 1
call :cc vs_5_0 VSW shaders\soft3d_race.hlsl race_VSW race_vsw
if errorlevel 1 exit /b 1

echo cso ok
exit /b 0

:cc
"%FXC%" /nologo /T %1 /E %2 /O3 /Fo shaders\cso\%4.cso %3
if errorlevel 1 exit /b 1
copy /Y shaders\cso\%4.cso res\cso\%5.cso >nul
goto :eof
