@echo off
REM ===========================================================================
REM  Lista unica das fontes do runtime.
REM
REM  Existe porque build_probe.cmd e build_visual.cmd mantinham a mesma lista
REM  duplicada e ela derrapou duas vezes seguidas: primeiro rt64_backend.c,
REM  depois perf_timeline.c entraram so no probe. Nos dois casos o visual
REM  quebrou no LINK, nao na compilacao - o erro aparece longe da causa e
REM  parece defeito de codigo, nao de script.
REM
REM  Quem acrescentar um .c ao runtime edita AQUI e os dois builds acompanham.
REM
REM  O que NAO entra: o escalonador. E justamente onde os dois divergem de
REM  proposito -- o probe usa sched_stateful.c + continuation.c +
REM  stateful_thread.c (continuacoes serializaveis, save state real) e o visual
REM  usa sched.c (fibers). Cada script declara o seu.
REM
REM  Tambem nao entram os fontes C++ (rsp_native.cpp e o microcodigo de audio):
REM  a linha de compilacao deles usa outro /std e outros includes.
REM ===========================================================================

set RUNTIME_COMUM="%PROJ%\runtime\runtime.c" "%PROJ%\runtime\hle.c" ^
 "%PROJ%\runtime\pif.c" "%PROJ%\runtime\mempak.c" "%PROJ%\runtime\rsp.c" ^
 "%PROJ%\runtime\video.c" "%PROJ%\runtime\audio.c" ^
 "%PROJ%\runtime\perf_timeline.c" "%PROJ%\runtime\rt64_backend.c" ^
 "%PROJ%\runtime\legendas.c" "%PROJ%\runtime\func_table.c"
