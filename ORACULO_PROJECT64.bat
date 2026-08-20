@echo off
REM Executa a variante instrumentada Release do Project64.
REM Ela usa os plugins Release gerados do mesmo fonte (Dev-4.0), evitando
REM incompatibilidade com os plugins v3 da instalacao comum em E:\projetos\Project64.
REM Os arquivos novos de RDRAM ficam em temp\oraculo\pj64-rdram\.
setlocal
set "WPJ2_ORACLE_DUMP=%~dp0temp\oraculo\pj64-rdram"
if not exist "%WPJ2_ORACLE_DUMP%" mkdir "%WPJ2_ORACLE_DUMP%"
start "Project64 Oracle" "%~dp0tools\Project64-source\bin\Win32\Release\Project64.exe" "E:\projetos\n64-roms\Wonder Project J2 - Koruro no Mori no Jozet (Japan) [T-En by Ryu v1.0].z64"
