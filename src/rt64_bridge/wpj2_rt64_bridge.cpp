#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <objbase.h>
#include <oleauto.h>

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <string>

#include "rt64_backend_api.h"
#include "rt64_application.h"

namespace {
    struct HardwareRegs {
        uint32_t miIntr = 0;
        uint32_t dpcStart = 0;
        uint32_t dpcEnd = 0;
        uint32_t dpcCurrent = 0;
        uint32_t dpcStatus = 0;
        uint32_t dpcClock = 0;
        uint32_t dpcBufBusy = 0;
        uint32_t dpcPipeBusy = 0;
        uint32_t dpcTmem = 0;
    } g_hw;

    uint8_t g_header[0x40] = {};
    std::unique_ptr<RT64::Application> g_app;
    uint8_t* g_rdram = nullptr;
    std::string g_error;
    std::recursive_mutex g_mutex;
    uint64_t g_present_count = 0;
    int g_perf_enabled = -1;
    uint16_t g_original_rate = 0;

    void check_interrupts() {
        // O runtime C entrega SP/DP pelas filas próprias. RT64 só renderiza.
    }

    void set_error(const char* message) {
        g_error = message ? message : "erro RT64 desconhecido";
        OutputDebugStringA(("[wpj2/rt64] " + g_error + "\n").c_str());
    }
}

extern "C" __declspec(dllexport)
int __cdecl wpj2_rt64_init(const wpj2_rt64_config* config) {
    std::lock_guard lock(g_mutex);
    if (g_app) return 1;
    if (!config || !config->window || !config->rdram || !config->dmem ||
        !config->imem || !config->window_thread_id) {
        set_error("configuracao incompleta");
        return 0;
    }

    try {
        RT64::Application::Core core{};
        core.window = static_cast<HWND>(config->window);
        core.HEADER = g_header;
        core.RDRAM = config->rdram;
        core.DMEM = config->dmem;
        core.IMEM = config->imem;
        core.MI_INTR_REG = &g_hw.miIntr;
        core.DPC_START_REG = &g_hw.dpcStart;
        core.DPC_END_REG = &g_hw.dpcEnd;
        core.DPC_CURRENT_REG = &g_hw.dpcCurrent;
        core.DPC_STATUS_REG = &g_hw.dpcStatus;
        core.DPC_CLOCK_REG = &g_hw.dpcClock;
        core.DPC_BUFBUSY_REG = &g_hw.dpcBufBusy;
        core.DPC_PIPEBUSY_REG = &g_hw.dpcPipeBusy;
        core.DPC_TMEM_REG = &g_hw.dpcTmem;
        core.VI_STATUS_REG = config->vi_status;
        core.VI_ORIGIN_REG = config->vi_origin;
        core.VI_WIDTH_REG = config->vi_width;
        core.VI_INTR_REG = config->vi_intr;
        core.VI_V_CURRENT_LINE_REG = config->vi_current;
        core.VI_TIMING_REG = config->vi_timing;
        core.VI_V_SYNC_REG = config->vi_v_sync;
        core.VI_H_SYNC_REG = config->vi_h_sync;
        core.VI_LEAP_REG = config->vi_leap;
        core.VI_H_START_REG = config->vi_h_start;
        core.VI_V_START_REG = config->vi_v_start;
        core.VI_V_BURST_REG = config->vi_v_burst;
        core.VI_X_SCALE_REG = config->vi_x_scale;
        core.VI_Y_SCALE_REG = config->vi_y_scale;
        core.checkInterrupts = check_interrupts;

        RT64::ApplicationConfiguration appConfig{};
        appConfig.appId = "wpj2-rt64-hybrid";
        appConfig.detectDataPath = false;
        appConfig.useConfigurationFile = false;

        auto app = std::make_unique<RT64::Application>(core, appConfig);
        const char* api = std::getenv("WPJ2_RT64_API");
        /* A/B no mesmo slot 1: Vulkan deixou 999/1074 listas abaixo de 2 ms
           e apenas 10 acima de 33 ms. O Automatic do Windows escolheu D3D12,
           com 61 listas acima de 33 ms. Vulkan também é a API usada pelo
           projeto wpj2-recomp de referência. */
        if (!api || !*api || _stricmp(api, "vulkan") == 0)
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Vulkan;
        else if (_stricmp(api, "d3d12") == 0)
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::D3D12;
        const auto result = app->setup(config->window_thread_id);
        if (result != RT64::Application::SetupResult::Success) {
            set_error("RT64::Application::setup falhou");
            return 0;
        }

        /* O RT64 cria os oito pipelines do ubershader em threads durante o
           setup. Se o jogo começa antes de eles terminarem, uma lista muito
           pequena pode bloquear dezenas de milissegundos esperando o primeiro
           uso de uma combinação de depth/coverage. Termine essa preparação
           aqui: hle_clock_resync() exclui o custo do relógio do N64 e a cena
           não paga compilações espalhadas pelos primeiros quadros. */
        if (app->rasterShaderCache &&
            app->rasterShaderCache->getGPUShaderUber()) {
            app->rasterShaderCache->getGPUShaderUber()->waitForPipelineCreation();
        }

        app->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
        app->userConfig.resolutionMultiplier = 2;
        app->userConfig.aspectRatio = RT64::UserConfiguration::AspectRatio::Original;
        app->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::None;
        app->userConfig.threePointFiltering = true;
        /* O runtime isolado nao envia o comando estendido usado pelos ports
           modernos para informar a taxa nativa; sem ele o RT64 registra zero
           e desliga a interpolacao mesmo em RefreshRate::Display. Permita que
           a ponte forneca a taxa observada no VI. Nos tres slots validados o
           framebuffer alterna a 60,00 Hz, independentemente do numero de DLs. */
        const char* originalRate = std::getenv("WPJ2_RT64_ORIGINAL_RATE");
        if (originalRate && *originalRate) {
            const int value = std::atoi(originalRate);
            if (value >= 1 && value <= 240 && app->state) {
                g_original_rate = static_cast<uint16_t>(value);
                app->state->setRefreshRate(g_original_rate);
            }
        }
        /* O jogo produz imagens novas em cadencias diferentes conforme a
           cena (o menu alterna sobretudo entre 20 e 30 fps), embora o VI seja
           atendido a 60 Hz. Em RefreshRate::Original o RT64 apenas reapresenta
           esses quadros, o que torna o deslocamento do cursor visivelmente
           irregular. Display conserva a cadencia/logica original e usa o
           interpolador nativo do RT64 ate a taxa real do monitor. */
        const char* refresh = std::getenv("WPJ2_RT64_REFRESH");
        app->userConfig.refreshRate =
            (refresh && _stricmp(refresh, "display") == 0)
                ? RT64::UserConfiguration::RefreshRate::Display
                : RT64::UserConfiguration::RefreshRate::Original;
        app->updateUserConfig(false);

        g_rdram = config->rdram;
        g_app = std::move(app);
        g_error.clear();
        return 1;
    }
    catch (const std::exception& e) {
        set_error(e.what());
        g_app.reset();
        return 0;
    }
    catch (...) {
        set_error("excecao desconhecida na inicializacao");
        g_app.reset();
        return 0;
    }
}

extern "C" __declspec(dllexport)
int __cdecl wpj2_rt64_submit(uint32_t dl_start, uint32_t dl_size,
                             uint32_t ucode, uint32_t ucode_data) {
    std::lock_guard lock(g_mutex);
    if (!g_app || !g_rdram || !dl_start || !dl_size) return 0;
    try {
        const uint32_t start = dl_start & 0x1FFFFFFFu;
        if (g_original_rate && g_app->state)
            g_app->state->setRefreshRate(g_original_rate);
        g_app->interpreter->loadUCodeGBI(ucode, ucode_data, true);
        g_app->processDisplayLists(g_rdram, start, start + dl_size, true);
        return 1;
    }
    catch (const std::exception& e) {
        set_error(e.what());
        return 0;
    }
    catch (...) {
        set_error("excecao ao processar display list");
        return 0;
    }
}

extern "C" __declspec(dllexport)
void __cdecl wpj2_rt64_present(void) {
    std::lock_guard lock(g_mutex);
    if (!g_app) return;
    try {
        g_app->updateScreen();
        if (g_perf_enabled < 0) {
            const char* perf = std::getenv("WPJ2_RT64_PERF");
            g_perf_enabled = perf && *perf && *perf != '0';
        }
        g_present_count++;
        if (g_perf_enabled && (g_present_count % 120u) == 0u &&
            g_app->sharedQueueResources) {
            auto* shared = g_app->sharedQueueResources.get();
            std::scoped_lock configLock(shared->configurationMutex);
            std::scoped_lock interpLock(shared->interpolatedMutex);
            const auto& a = shared->interpolatedFrames[0];
            const auto& b = shared->interpolatedFrames[1];
            std::fprintf(stderr,
                "[rt64-native-perf] present=%llu swap=%u target=%u original=%u "
                "fb=%zu interp0=%u/%u/%u skip=%u interp1=%u/%u/%u skip=%u\n",
                static_cast<unsigned long long>(g_present_count),
                shared->swapChainRate, shared->targetRate,
                shared->viOriginalRate, shared->colorImageAddressVector.size(),
                a.presented, a.available, a.count, a.skipped ? 1u : 0u,
                b.presented, b.available, b.count, b.skipped ? 1u : 0u);
        }
    }
    catch (const std::exception& e) { set_error(e.what()); }
    catch (...) { set_error("excecao ao apresentar quadro"); }
}

/* Mantidos na ABI para o snapshot poder sincronizar qualquer futura fila.
   O backend estável atual é síncrono, portanto não há pendência a consumir. */
extern "C" __declspec(dllexport)
int __cdecl wpj2_rt64_take_completed(void) { return 0; }

extern "C" __declspec(dllexport)
void __cdecl wpj2_rt64_sync(void) {}

extern "C" __declspec(dllexport)
void __cdecl wpj2_rt64_shutdown(void) {
    std::lock_guard lock(g_mutex);
    if (g_app) {
        try { g_app->end(); }
        catch (...) {}
    }
    g_app.reset();
    g_rdram = nullptr;
    g_present_count = 0;
    g_perf_enabled = -1;
}

extern "C" __declspec(dllexport)
const char* __cdecl wpj2_rt64_error(void) {
    return g_error.c_str();
}
