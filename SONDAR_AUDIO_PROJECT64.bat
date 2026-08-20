@echo off
setlocal
REM Sonda profunda e ilimitada. O plugin copia o PCM final da AI; o script JS
REM registra todas as ALists e seus estados internos antes do RSP executa-las.
set "ROOT=%~dp0temp\oraculo\audio_deep"
set "WPJ2_AUDIO_ORACLE_WAV=%ROOT%\pj64_audio_oracle.wav"
set "WPJ2_AUDIO_ORACLE_DIR=%ROOT%\ai_plugin"
REM A copia-oraculo do Project64 le esta chave somente nesta execucao. Assim
REM ela abre sem Limit FPS, equivalente ao F4, sem salvar a alteracao.
set "WPJ2_ORACLE_UNLIMITED=1"
set "PJ64=%~dp0tools\Project64-source\Bin\Win32\Release\Project64.exe"
set "ROM=E:\projetos\n64-roms\Wonder Project J2 - Koruro no Mori no Jozet (Japan) [T-En by Ryu v1.0].z64"
set "PYTHON=C:\Users\lucas\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"

if not exist "%PJ64%" (
  echo ERRO: Project64 de teste nao encontrado: %PJ64%
  pause
  exit /b 1
)

echo Sonda profunda de audio ativa: sem limite de tempo ou de tarefas.
echo A sonda ja abre sem Limit FPS; nao e necessario apertar F4.
echo Deixe o jogo rodar pelo tempo que quiser e feche o Project64 para encerrar.
echo O WAV-oraculo, PCM, ALists e estados serao salvos em:
echo   %WPJ2_AUDIO_ORACLE_WAV%
echo   %WPJ2_AUDIO_ORACLE_DIR%
echo   %ROOT%
REM Uma nova coleta nao deve se misturar a anterior: preservamos a rodada em
REM uma pasta datada (movimento recuperavel), sem apagar arquivos antigos.
if exist "%ROOT%\manifest.csv" (
  for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "WPJ2_ARCHIVE_SUFFIX=%%I"
  echo Preservando a coleta anterior antes de iniciar a nova rodada...
  powershell -NoProfile -Command "$source=$env:ROOT; $dest=$source+'_anterior_'+$env:WPJ2_ARCHIVE_SUFFIX; Move-Item -LiteralPath $source -Destination $dest; Write-Host ('Anterior: '+$dest)"
)
if not exist "%ROOT%" mkdir "%ROOT%"
if not exist "%WPJ2_AUDIO_ORACLE_DIR%" mkdir "%WPJ2_AUDIO_ORACLE_DIR%"
REM Nao alteramos mais Project64.cfg: uma tentativa de autorun regravou o
REM arquivo e disparou o assistente de primeira execucao. Abra Debugger >
REM Scripts, selecione wpj2_audio_deep_oracle.js e clique Run apos a ROM abrir.
echo No Project64: Debugger -^> Scripts -^> wpj2_audio_deep_oracle.js -^> Run.
echo Em seguida use System -^> Reset para capturar desde a primeira musica.
echo A coleta continua ate fechar o emulador; nao reinicie o script durante a coleta.
start "" /wait "%PJ64%" "%ROM%"

if exist "%PYTHON%" if exist "%ROOT%\manifest.csv" (
  "%PYTHON%" "%~dp0tools\analisar_audio_deep.py" "%ROOT%"
)

if exist "%WPJ2_AUDIO_ORACLE_WAV%" (
  for %%I in ("%WPJ2_AUDIO_ORACLE_WAV%") do echo Captura concluida: %%~zI bytes
  if exist "%ROOT%\manifest.csv" echo Manifesto profundo criado: %ROOT%\manifest.csv
  if exist "%ROOT%\relatorio_audio_deep.md" echo Relatorio automatico criado: %ROOT%\relatorio_audio_deep.md
) else (
  echo Nenhum WAV foi criado. Verifique se o plugin Project64 Audio esta selecionado.
)
pause
