#ifndef WPJ2_LEGENDAS_H
#define WPJ2_LEGENDAS_H

#include <stdint.h>
#include <stddef.h>

/* Intercepta, de forma opcional, a cadeia entregue ao formatador do jogo.
 * As duas chamadas devem envolver imediatamente o corpo recompilado. */
void legendas_antes(uint8_t* rdram, uint32_t args);
void legendas_depois(uint8_t* rdram, uint32_t args);
void legendas_aplicar_cartucho(uint8_t* cart, size_t rom_size);
void legendas_capturar_rdram(uint8_t* rdram, const char* directory, unsigned id);

#endif
