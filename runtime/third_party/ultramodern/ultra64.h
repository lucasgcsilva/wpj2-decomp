#pragma once

/* O caminho RSP recompilado so referencia OSTask por ponteiro no cabecalho
 * librecomp. O runtime leve deste projeto ja mantem a estrutura equivalente
 * na DMEM, portanto uma declaracao antecipada e suficiente e evita importar
 * o restante do frontend N64ModernRuntime. */
struct OSTask;
