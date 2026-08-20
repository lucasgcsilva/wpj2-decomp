@echo off
REM ============================================================
REM  Wonder Project J2 - rodada de laboratorio
REM
REM  Um comando so: compila, dispara uma matriz de sondagens em lotes com
REM  configuracoes diferentes, roda a analise estatica e uma sonda
REM  dirigida de textura ao fim, e resume tudo em
REM  temp\projeto\laboratorio\RESULTADO.md.
REM
REM  Uso:
REM     RODAR.bat              20 s por sondagem (padrao)
REM     RODAR.bat 40           40 s por sondagem
REM     RODAR.bat 20 --sem-build   nao recompila
REM     RODAR.bat 20 --sem-profundo  pula somente a sonda dirigida de textura
REM     set WPJ2_LAB_WORKERS=1 & RODAR.bat 20  maxima reprodutibilidade
REM ============================================================
setlocal
call "%~dp0tools\env.cmd"
cd /d "%~dp0"

echo.
echo  Wonder Project J2 - laboratorio
echo  ------------------------------
echo  Isto vai compilar e rodar a matriz em lotes isolados.
echo  Cada uma abre e fecha sozinha; nao ha janela de jogo.
echo.

python "%~dp0tools\lab.py" %*
set RC=%ERRORLEVEL%

echo.
if %RC% NEQ 0 (
   echo  ALGO FALHOU. temp\projeto\laboratorio\RESULTADO.md tem o motivo.
) else (
   echo  Pronto. Resultados em temp\projeto\laboratorio\.
)
echo.
pause
