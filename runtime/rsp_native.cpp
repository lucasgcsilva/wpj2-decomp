/* Ponte opt-in para o microcodigo de audio recompilado por RSPRecomp.
 * Ela recebe a DMEM ja montada pelo runtime C, executa o ucode original e
 * devolve apenas a DMEM modificada. O interpretador HLE continua padrao ate
 * que a comparacao de PCM valide este caminho. */
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "librecomp/rsp.hpp"

uint8_t dmem[0x1000];
uint16_t rspReciprocals[512];
uint16_t rspInverseSquareRoots[512];

static uint32_t g_rsp_dma_wrapped;

static uint32_t wpj2_rsp_dma_length(uint32_t raw_len) {
    /* SP_{RD,WR}_LEN: bits 0..11 armazenam LEN-1. Os campos COUNT/SKIP nao
     * sao usados por este microcodigo de audio e eram ignorados pelo helper
     * anterior tambem. */
    return (raw_len & 0xFFFu) + 1u;
}

extern "C" void wpj2_rsp_dma_read_safe(uint8_t* rdram, uint32_t dmem_addr,
                                        uint32_t dram_addr, uint32_t raw_len) {
    uint32_t length = wpj2_rsp_dma_length(raw_len);
    uint32_t memory = dmem_addr & 0xFFFu;
    dram_addr &= 0xFFFFF8u;
    if (memory + length > 0x1000u && g_rsp_dma_wrapped++ < 8u) {
        std::fprintf(stderr,
                     "[audio-rsp] DMA read com wrap: dmem=%04X len=%X dram=%06X\n",
                     dmem_addr, length, dram_addr);
    }
    for (uint32_t i = 0; i < length; i++)
        RSP_MEM_B(0, (memory + i) & 0xFFFu) =
            MEM_B(0, (int64_t)(int32_t)(dram_addr + i + 0x80000000u));
}

extern "C" void wpj2_rsp_dma_write_safe(uint8_t* rdram, uint32_t dmem_addr,
                                         uint32_t dram_addr, uint32_t raw_len) {
    uint32_t length = wpj2_rsp_dma_length(raw_len);
    uint32_t memory = dmem_addr & 0xFFFu;
    dram_addr &= 0xFFFFF8u;
    if (memory + length > 0x1000u && g_rsp_dma_wrapped++ < 8u) {
        std::fprintf(stderr,
                     "[audio-rsp] DMA write com wrap: dmem=%04X len=%X dram=%06X\n",
                     dmem_addr, length, dram_addr);
    }
    for (uint32_t i = 0; i < length; i++)
        MEM_B(0, (int64_t)(int32_t)(dram_addr + i + 0x80000000u)) =
            RSP_MEM_B(0, (memory + i) & 0xFFFu);
}

extern RspExitReason wpj2_audio_rsp(uint8_t* rdram, uint32_t ucode_addr);

/* Copia local da inicializacao de constantes do librecomp. Importar rsp.cpp
 * inteiro exigiria o frontend Ultramodern; esta rotina e a unica dependencia
 * dele usada pelo microcodigo de audio. */
static void wpj2_rsp_constants_init() {
    rspReciprocals[0] = uint16_t(~0u);
    for (uint16_t index = 1; index < 512; index++) {
        uint64_t a = uint64_t(index) + 512;
        uint64_t b = (uint64_t(1) << 34) / a;
        rspReciprocals[index] = uint16_t((b + 1) >> 8);
    }
    for (uint16_t index = 0; index < 512; index++) {
        uint64_t a = (uint64_t(index) + 512) >> ((index & 1) ? 1 : 0);
        uint64_t b = uint64_t(1) << 17;
        while (a * (b + 1) * (b + 1) < (uint64_t(1) << 44)) b++;
        rspInverseSquareRoots[index] = uint16_t(b >> 1);
    }
}

extern "C" int wpj2_native_audio_rsp(uint8_t* rdram, uint8_t* spmem) {
    static bool initialized = false;
    if (!initialized) {
        wpj2_rsp_constants_init();
        initialized = true;
    }
    std::memcpy(dmem, spmem, sizeof(dmem));
    /* run_task() do N64ModernRuntime carrega os 0xF80 bytes de ucode_data
     * depois de instalar a OSTask em 0xFC0. No runtime C a tarefa ja esta na
     * DMEM, mas o interpretador manual nao precisava desse bloco; sem esta DMA
     * a tabela de despacho em 0x360 fica zerada e o ucode salta para 0. */
    const uint32_t ucode_data = RSP_MEM_W_LOAD(0xFD8, 0);
    /* Captura pontual da tabela residente na DMEM. Ela contém os destinos
       indiretos dos opcodes de áudio e permite compilar apenas os blocos que
       esta ROM realmente alcança — sem adivinhar endereços. */
    if (const char* path = std::getenv("WPJ2_RSP_DATA_DUMP")) {
        if (*path) {
            if (FILE* out = std::fopen(path, "wb")) {
                for (uint32_t i = 0; i < 0xF80; ++i)
                    std::fputc(rdram[(ucode_data + i) ^ 3u], out);
                std::fclose(out);
            }
        }
    }
    dma_rdram_to_dmem(rdram, 0, ucode_data, 0xF80 - 1);
    RspExitReason reason = wpj2_audio_rsp(rdram, 0);
    if (reason != RspExitReason::Broke) return static_cast<int>(reason) + 1;
    std::memcpy(spmem, dmem, sizeof(dmem));
    return 0;
}
