#ifndef WPJ2_RSP_SAFE_DMA_H
#define WPJ2_RSP_SAFE_DMA_H

/* Incluido antes do fonte gerado do RSP. O helper do librecomp usa assert
 * para uma suposicao de compilacao (DMA nunca cruza 0x1000); o registrador
 * real do RSP mascara o endereco para 12 bits e o comprimento para o campo
 * LEN. Reproduzir essa semantica impede que uma AList excepcional encerre o
 * processo inteiro. */
#include "librecomp/rsp.hpp"

extern "C" void wpj2_rsp_dma_read_safe(uint8_t* rdram, uint32_t dmem_addr,
                                        uint32_t dram_addr, uint32_t raw_len);
extern "C" void wpj2_rsp_dma_write_safe(uint8_t* rdram, uint32_t dmem_addr,
                                         uint32_t dram_addr, uint32_t raw_len);

#undef DO_DMA_READ
#undef DO_DMA_WRITE
#define DO_DMA_READ(rd_len) \
    wpj2_rsp_dma_read_safe(rdram, dma_mem_address, dma_dram_address, (rd_len))
#define DO_DMA_WRITE(wr_len) \
    wpj2_rsp_dma_write_safe(rdram, dma_mem_address, dma_dram_address, (wr_len))

#endif
