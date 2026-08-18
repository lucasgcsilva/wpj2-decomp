#ifndef WPJ2_TRACE_H
#define WPJ2_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef RECOMP_TRACING
void recomp_trace(uint32_t vram);
#define RECOMP_TRACE(vram) recomp_trace(vram)
#else
#define RECOMP_TRACE(vram) ((void)0)
#endif

#ifdef RECOMP_POLLING
/* Ponto de interrupcao.
 *
 * O laco ocioso da libultra e `for(;;)` puro: nao chama nada, entao nenhum hook
 * por funcao consegue interromper. No C gerado esse laco vira um `goto` para um
 * rotulo, e e ali que um interrupt real chegaria - num limite de instrucao
 * qualquer, nao numa borda de chamada.
 *
 * O custo por iteracao e um decremento e um desvio previsto; a funcao so e
 * chamada quando o orcamento zera. */
extern long g_poll_budget;
void recomp_poll(void);
#define RECOMP_POLL() do { if (--g_poll_budget <= 0) recomp_poll(); } while (0)
#else
#define RECOMP_POLL() ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif
