@echo off
setlocal

rem Este arquivo deve ser iniciado por cleanenv_launcher.exe. Ele evita que
rem variaveis Path/PATH duplicadas do host cheguem ao MSBuild.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64 >nul
if errorlevel 1 exit /b 1

set "PJ64=%~dp0Project64-source"
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

"%MSBUILD%" "%PJ64%\Source\Project64-core\Project64-core.vcxproj" /m /p:Configuration=Release /p:Platform=Win32 /v:minimal
if errorlevel 1 exit /b 1

"%MSBUILD%" "%PJ64%\Source\Project64\Project64.vcxproj" /m /p:Configuration=Release /p:Platform=Win32 /p:BuildProjectReferences=false /v:minimal
