@echo off
REM Cadeia completa: simbolos -> recompilacao -> instrumentacao -> binario -> execucao.
REM
REM Um alvo so, para que nenhuma etapa fique para tras depois de mexer nos
REM simbolos. O `cl /MP` ja usa todos os nucleos na parte cara (3.649 funcoes);
REM a analise estatica, quando roda, se distribui em processos.
call "E:\projetos\project-wonder-j2-decomp\tools\env.cmd"
cd /d "%PROJ%"

echo === recompilando (com a lista de stubs atual) ===
python "%PROJ%\tools\autostub.py" "%PROJ%\tools\N64Recomp-build-official\N64Recomp.exe" ^
       "%PROJ%\wpj2.toml" "%PROJ%\stubs.txt" --limit 128
if errorlevel 1 (echo FALHOU A RECOMPILACAO & exit /b 1)

call "%PROJ%\tools\build_probe.cmd"
if errorlevel 1 exit /b 1

echo === executando a sondagem ===
"%PROJ%\wpj2_probe.exe" > "%PROJ%\probe_run.log" 2>&1
type "%PROJ%\probe_run.log"
