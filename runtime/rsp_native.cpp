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
