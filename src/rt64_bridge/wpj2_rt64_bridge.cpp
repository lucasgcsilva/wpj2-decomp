#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <objbase.h>
#include <oleauto.h>

#include <cstdint>
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
        const auto result = app->setup(config->window_thread_id);
        if (result != RT64::Application::SetupResult::Success) {
            set_error("RT64::Application::setup falhou");
            return 0;
        }

        // Fidelidade validada na referência: resolução interna 2x, proporção
        // original e filtro N64 de três pontos. MSAA fica desligado até termos
        // comparação isolada, pois o RT64 já resolve coverage/VI.
        app->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
        app->userConfig.resolutionMultiplier = 2;
        app->userConfig.aspectRatio = RT64::UserConfiguration::AspectRatio::Original;
        app->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::None;
        app->userConfig.threePointFiltering = true;
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
    try { g_app->updateScreen(); }
    catch (const std::exception& e) { set_error(e.what()); }
    catch (...) { set_error("excecao ao apresentar quadro"); }
}

extern "C" __declspec(dllexport)
void __cdecl wpj2_rt64_shutdown(void) {
    std::lock_guard lock(g_mutex);
    if (g_app) {
        try { g_app->end(); }
        catch (...) {}
    }
    g_app.reset();
    g_rdram = nullptr;
}

extern "C" __declspec(dllexport)
const char* __cdecl wpj2_rt64_error(void) {
    return g_error.c_str();
}
