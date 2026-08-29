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
static wpj2_rt64_take_completed_fn g_rt64_take_completed;
static wpj2_rt64_sync_fn g_rt64_sync;
static wpj2_rt64_shutdown_fn g_rt64_shutdown;
static wpj2_rt64_error_fn g_rt64_error;
static int g_rt64_wanted = -1;
static int g_rt64_attempted;
static int g_rt64_active;
static int g_rt64_first_submit = 1;
static int g_rt64_perf = -1;
static LARGE_INTEGER g_perf_frequency, g_perf_first, g_perf_previous;
static uint64_t g_perf_presents, g_perf_slow20, g_perf_slow25, g_perf_slow33;
static double g_perf_interval_sum_ms, g_perf_interval_max_ms;
static double g_perf_call_sum_ms, g_perf_call_max_ms;

static void rt64_perf_report(void) {
    if (!g_rt64_perf || !g_perf_presents) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double elapsed = (double)(now.QuadPart - g_perf_first.QuadPart) /
                     (double)g_perf_frequency.QuadPart;
    printf("[rt64-perf-final] presents=%llu fps=%.3f intervalo_media=%.3fms max=%.3fms >20=%llu >25=%llu >33=%llu chamada_media=%.3fms max=%.3fms\n",
           (unsigned long long)g_perf_presents,
           elapsed > 0.0 ? (double)g_perf_presents / elapsed : 0.0,
           g_perf_presents > 1u ? g_perf_interval_sum_ms / (double)(g_perf_presents - 1u) : 0.0,
           g_perf_interval_max_ms,
           (unsigned long long)g_perf_slow20,
           (unsigned long long)g_perf_slow25,
           (unsigned long long)g_perf_slow33,
           g_perf_call_sum_ms / (double)g_perf_presents, g_perf_call_max_ms);
    fflush(stdout);
}

void rt64_backend_perf_report(void) {
    rt64_perf_report();
}

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
    LOAD_RT64(take_completed, wpj2_rt64_take_completed_fn);
    LOAD_RT64(sync, wpj2_rt64_sync_fn);
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
    /* A criação do Vulkan/RT64 pode levar centenas de milissegundos. Esse
       tempo é inicialização do host, não tempo do N64; não tente entregar em
       rajada todos os VIs cujo prazo venceu durante o setup. */
    hle_clock_resync();
    printf("[rt64] backend grafico nativo ativo; audio/input/PT-BR permanecem locais\n");
    fflush(stdout);
    return 1;
}

int rt64_backend_take_completed(void) {
    return g_rt64_active && g_rt64_take_completed ? g_rt64_take_completed() : 0;
}

void rt64_backend_sync(void) {
    if (g_rt64_active && g_rt64_sync) g_rt64_sync();
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
    /* Modo estável: o worker pode executar a chamada RT64, mas a CPU convidada
       só prossegue quando ele terminou de consumir a display list/RDRAM. Isso
       preserva a ordenação usada antes do experimento assíncrono. */
    if (g_rt64_sync) g_rt64_sync();
    if (g_rt64_take_completed) (void)g_rt64_take_completed();
    /* A primeira lista cria caches e recursos dependentes do conteúdo. No
       Vulkan isso pode custar segundos uma única vez. É preparação do host,
       não tempo transcorrido no N64; reiniciar o prazo evita uma rajada de VIs
       atrasados imediatamente depois do boot ou de carregar um estado. */
    if (g_rt64_first_submit) {
        g_rt64_first_submit = 0;
        hle_clock_resync();
    }
    return 1;
}

void rt64_backend_present(void) {
    if (!g_rt64_active || !g_rt64_present) return;
    if (g_rt64_perf < 0) {
        const char* value = getenv("WPJ2_RT64_PERF");
        g_rt64_perf = value && atoi(value) != 0;
        if (g_rt64_perf) QueryPerformanceFrequency(&g_perf_frequency);
    }
    if (!g_rt64_perf) {
        g_rt64_present();
        if (g_rt64_sync) g_rt64_sync();
        return;
    }
    LARGE_INTEGER before, after;
    QueryPerformanceCounter(&before);
    if (!g_perf_presents) g_perf_first = before;
    else {
        double interval_ms = (double)(before.QuadPart - g_perf_previous.QuadPart) *
                             1000.0 / (double)g_perf_frequency.QuadPart;
        g_perf_interval_sum_ms += interval_ms;
        if (interval_ms > g_perf_interval_max_ms) g_perf_interval_max_ms = interval_ms;
        if (interval_ms > 20.0) g_perf_slow20++;
        if (interval_ms > 25.0) g_perf_slow25++;
        if (interval_ms > 33.0) g_perf_slow33++;
    }
    g_rt64_present();
    if (g_rt64_sync) g_rt64_sync();
    QueryPerformanceCounter(&after);
    double call_ms = (double)(after.QuadPart - before.QuadPart) * 1000.0 /
                     (double)g_perf_frequency.QuadPart;
    g_perf_call_sum_ms += call_ms;
    if (call_ms > g_perf_call_max_ms) g_perf_call_max_ms = call_ms;
    g_perf_previous = before;
    g_perf_presents++;
    if ((g_perf_presents % 600u) == 0u) {
        double elapsed = (double)(before.QuadPart - g_perf_first.QuadPart) /
                         (double)g_perf_frequency.QuadPart;
        printf("[rt64-perf] presents=%llu fps=%.3f intervalo_media=%.3fms max=%.3fms >20=%llu >25=%llu >33=%llu chamada_media=%.3fms max=%.3fms\n",
               (unsigned long long)g_perf_presents,
               elapsed > 0.0 ? (double)(g_perf_presents - 1u) / elapsed : 0.0,
               g_perf_presents > 1u ? g_perf_interval_sum_ms / (double)(g_perf_presents - 1u) : 0.0,
               g_perf_interval_max_ms,
               (unsigned long long)g_perf_slow20,
               (unsigned long long)g_perf_slow25,
               (unsigned long long)g_perf_slow33,
               g_perf_call_sum_ms / (double)g_perf_presents, g_perf_call_max_ms);
        fflush(stdout);
    }
}

void rt64_backend_shutdown(void) {
    rt64_perf_report();
    if (g_rt64_active && g_rt64_shutdown) g_rt64_shutdown();
    g_rt64_active = 0;
    if (g_rt64_module) FreeLibrary(g_rt64_module);
    g_rt64_module = NULL;
    g_rt64_init = NULL;
    g_rt64_submit = NULL;
    g_rt64_present = NULL;
    g_rt64_take_completed = NULL;
    g_rt64_sync = NULL;
    g_rt64_shutdown = NULL;
    g_rt64_error = NULL;
    /* O swapchain e estado interno da ponte sao derivados do snapshot. Depois
     * de F4, permita que a proxima tarefa grafica carregue a DLL e reconstrua
     * o backend. Sem isto g_rt64_attempted mantinha a imagem no fallback CPU. */
    g_rt64_attempted = 0;
    g_rt64_first_submit = 1;
    g_rt64_perf = -1;
    g_perf_presents = g_perf_slow20 = g_perf_slow25 = g_perf_slow33 = 0;
    g_perf_interval_sum_ms = g_perf_interval_max_ms = 0.0;
    g_perf_call_sum_ms = g_perf_call_max_ms = 0.0;
}
