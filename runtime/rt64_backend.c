#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt64_backend.h"
#include "rt64_backend_api.h"
#include "video.h"

#define VBASE 0x80000000u
#define MMIO_PTR(rdram, addr) ((uint32_t*)((rdram) + ((addr) - VBASE)))

static HMODULE g_rt64_module;
static wpj2_rt64_init_fn g_rt64_init;
static wpj2_rt64_submit_fn g_rt64_submit;
static wpj2_rt64_present_fn g_rt64_present;
static wpj2_rt64_shutdown_fn g_rt64_shutdown;
static wpj2_rt64_error_fn g_rt64_error;
static int g_rt64_wanted = -1;
static int g_rt64_attempted;
static int g_rt64_active;

int rt64_backend_requested(void) {
    if (g_rt64_wanted < 0) {
        const char* value = getenv("WPJ2_GFX_BACKEND");
        /* RT64 e o backend de producao. CPU continua como fallback automatico
           e pode ser pedido explicitamente para comparacoes/diagnostico. */
        g_rt64_wanted = !value || !*value || !_stricmp(value, "rt64") ||
                        !_stricmp(value, "gpu") || !_stricmp(value, "default");
    }
    return g_rt64_wanted;
}

int rt64_backend_active(void) {
    return g_rt64_active;
}

static int rt64_backend_start(uint8_t* rdram, uint8_t* spmem) {
    if (g_rt64_attempted) return g_rt64_active;
    g_rt64_attempted = 1;
    if (!rt64_backend_requested()) return 0;

    HWND window = (HWND)video_native_window();
    if (!window) {
        printf("[rt64] janela Win32 ainda nao existe; usando backend CPU\n");
        return 0;
    }

    /* Mantem as DLLs pesadas fora da raiz. SetDllDirectory vale somente
       durante a carga inicial; depois disso os modulos ja estao resolvidos. */
    SetDllDirectoryA("build\\rt64_runtime");
    g_rt64_module = LoadLibraryA("build\\rt64_runtime\\wpj2_rt64_bridge.dll");
    SetDllDirectoryA(NULL);
    if (!g_rt64_module) {
        printf("[rt64] build/rt64_runtime/wpj2_rt64_bridge.dll ausente/indisponivel (%lu); usando CPU\n",
               GetLastError());
        return 0;
    }

#define LOAD_RT64(name, type) do { \
    g_rt64_##name = (type)GetProcAddress(g_rt64_module, "wpj2_rt64_" #name); \
    if (!g_rt64_##name) { \
        printf("[rt64] export wpj2_rt64_%s ausente; usando CPU\n", #name); \
        FreeLibrary(g_rt64_module); g_rt64_module = NULL; return 0; \
    } \
} while (0)
    LOAD_RT64(init, wpj2_rt64_init_fn);
    LOAD_RT64(submit, wpj2_rt64_submit_fn);
    LOAD_RT64(present, wpj2_rt64_present_fn);
    LOAD_RT64(shutdown, wpj2_rt64_shutdown_fn);
    LOAD_RT64(error, wpj2_rt64_error_fn);
#undef LOAD_RT64

    wpj2_rt64_config config;
    memset(&config, 0, sizeof(config));
    config.window = window;
    config.window_thread_id = GetWindowThreadProcessId(window, NULL);
    config.rdram = rdram;
    config.dmem = spmem;
    config.imem = spmem + 0x1000;
    config.vi_status  = MMIO_PTR(rdram, 0xA4400000u);
    config.vi_origin  = MMIO_PTR(rdram, 0xA4400004u);
    config.vi_width   = MMIO_PTR(rdram, 0xA4400008u);
    config.vi_intr    = MMIO_PTR(rdram, 0xA440000Cu);
    config.vi_current = MMIO_PTR(rdram, 0xA4400010u);
    config.vi_timing  = MMIO_PTR(rdram, 0xA4400014u);
    config.vi_v_sync  = MMIO_PTR(rdram, 0xA4400018u);
    config.vi_h_sync  = MMIO_PTR(rdram, 0xA440001Cu);
    config.vi_leap    = MMIO_PTR(rdram, 0xA4400020u);
    config.vi_h_start = MMIO_PTR(rdram, 0xA4400024u);
    config.vi_v_start = MMIO_PTR(rdram, 0xA4400028u);
    config.vi_v_burst = MMIO_PTR(rdram, 0xA440002Cu);
    config.vi_x_scale = MMIO_PTR(rdram, 0xA4400030u);
    config.vi_y_scale = MMIO_PTR(rdram, 0xA4400034u);

    if (!g_rt64_init(&config)) {
        printf("[rt64] inicializacao falhou: %s; usando CPU\n",
               g_rt64_error ? g_rt64_error() : "erro desconhecido");
        FreeLibrary(g_rt64_module);
        g_rt64_module = NULL;
        return 0;
    }

    g_rt64_active = 1;
    printf("[rt64] backend grafico nativo ativo; audio/input/PT-BR permanecem locais\n");
    fflush(stdout);
    return 1;
}

int rt64_backend_submit(uint8_t* rdram, uint8_t* spmem,
                        uint32_t dl_start, uint32_t dl_size,
                        uint32_t ucode, uint32_t ucode_data) {
    if (!rt64_backend_requested()) return 0;
    if (!g_rt64_active && !rt64_backend_start(rdram, spmem)) return 0;
    if (!g_rt64_submit(dl_start, dl_size, ucode, ucode_data)) {
        printf("[rt64] display list rejeitada: %s; voltando ao CPU nesta execucao\n",
               g_rt64_error ? g_rt64_error() : "erro desconhecido");
        rt64_backend_shutdown();
        return 0;
    }
    return 1;
}

void rt64_backend_present(void) {
    if (g_rt64_active && g_rt64_present) g_rt64_present();
}

void rt64_backend_shutdown(void) {
    if (g_rt64_active && g_rt64_shutdown) g_rt64_shutdown();
    g_rt64_active = 0;
    if (g_rt64_module) FreeLibrary(g_rt64_module);
    g_rt64_module = NULL;
}
