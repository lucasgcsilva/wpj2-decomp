#ifndef MEMPAK_H
#define MEMPAK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEMPAK_SIZE 32768

/* Inicializa a imagem do Controller Pak. */
void mempak_init(void);

/* Forca o salvamento do buffer RAM no disco (sav/mempak_port1.mpk). */
void mempak_flush(void);

/* Retorna 1 se o MemPak estiver habilitado/presente. */
int mempak_is_present(void);

/* Le 32 bytes do MemPak a partir do endereco N64 (0x0000 - 0x7FFF).
   Calcula e preenche o datacrc no final do buffer Joybus. */
int mempak_read_block(uint16_t n64_address, uint8_t* out_data32, uint8_t* out_crc);

/* Escreve 32 bytes no MemPak a partir do endereco N64 (0x0000 - 0x7FFF).
   Verifica o datacrc e persiste as alteracoes no disco. */
int mempak_write_block(uint16_t n64_address, const uint8_t* in_data32, uint8_t in_crc, uint8_t* out_crc);

/* Calcula o CRC8 dos dados de controle N64. */
uint8_t mempak_calc_data_crc(const uint8_t* data, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* MEMPAK_H */
