#ifndef WPJ2_LEGENDAS_H
#define WPJ2_LEGENDAS_H

#include <stdint.h>
#include <stddef.h>

void legendas_aplicar_cartucho(uint8_t* cart, size_t rom_size);
void legendas_capturar_rdram(uint8_t* rdram, const char* directory, unsigned id);
/* Substitui em memoria um recurso textual que o carregador acabou de copiar.
 * O endereco permanece o mesmo: assim o formatador, a fonte e a digitacao do
 * jogo continuam sendo os responsaveis pela exibicao. */
int legendas_substituir_recurso(uint8_t* rdram, uint32_t pointer);
/* Reconstroi, no banco de objetos da fonte ASCII instalada pelo patch Ryu,
 * o glifo acentuado que sera consumido imediatamente por func_80094230. */
int legendas_compor_glifo_ryu(uint8_t* rdram, uint32_t indice_objeto);

#endif
