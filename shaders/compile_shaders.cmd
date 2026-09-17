@echo off
setlocal
cd /d "%~dp0.."
set FXC=
if exist "D:\Windows Kits\10\bin\10.0.26100.0\x86\fxc.exe" set "FXC=D:\Windows Kits\10\bin\10.0.26100.0\x86\fxc.exe"
if exist "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\fxc.exe" set "FXC=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\fxc.exe"
if "%FXC%"=="" (
  for /d %%D in ("C:\Program Files (x86)\Windows Kits\10\bin\10.*") do (
    if exist "%%D\x86\fxc.exe" set "FXC=%%D\x86\fxc.exe"
  )
)
if "%FXC%"=="" (
  echo fxc.exe not found, skipping cso.
  exit /b 0
)
mkdir shaders\cso 2>nul
mkdir res\cso 2>nul
"%FXC%" /nologo /T vs_5_0 /E VS_Rect /O3 /Fo shaders\cso\gpu_VS_Rect.cso shaders\gpu_rect.hlsl
if errorlevel 1 exit /b 1
"%FXC%" /nologo /T ps_5_0 /E PS_Rect /O3 /Fo shaders\cso\gpu_PS_Rect.cso shaders\gpu_rect.hlsl
if errorlevel 1 exit /b 1
"%FXC%" /nologo /T vs_5_0 /E VS_PianoKey /O3 /Fo shaders\cso\gpu_VS_PianoKey.cso shaders\gpu_piano.hlsl
if errorlevel 1 exit /b 1
"%FXC%" /nologo /T cs_5_0 /E CS_Hex /O3 /Fo shaders\cso\gpu_CS_Hex.cso shaders\gpu_hex.hlsl
if errorlevel 1 exit /b 1
"%FXC%" /nologo /T cs_5_0 /E CS_HexGlyph /O3 /Fo shaders\cso\gpu_CS_HexGlyph.cso shaders\gpu_hex.hlsl
if errorlevel 1 exit /b 1
copy /Y shaders\cso\gpu_VS_Rect.cso res\cso\gpu_vs_rect.cso >nul
copy /Y shaders\cso\gpu_PS_Rect.cso res\cso\gpu_ps_rect.cso >nul
copy /Y shaders\cso\gpu_VS_PianoKey.cso res\cso\gpu_vs_piano.cso >nul
copy /Y shaders\cso\gpu_CS_Hex.cso res\cso\gpu_cs_hex.cso >nul
copy /Y shaders\cso\gpu_CS_HexGlyph.cso res\cso\gpu_cs_glyph.cso >nul
echo cso ok
exit /b 0
