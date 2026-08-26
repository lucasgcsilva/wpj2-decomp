#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "video.h"
#include "runtime.h"
#include "legendas.h"
#include "rt64_backend.h"

#define VIDEO_W 320u
#define VIDEO_H 240u
#define PRESENT_W 640u
#define PRESENT_H 480u

static HWND g_video_window = NULL;
static uint32_t g_pixels[VIDEO_W * VIDEO_H];
static uint32_t g_pixels_2x[PRESENT_W * PRESENT_H];
static BITMAPINFO g_bmi;
static ULONGLONG g_last_present = 0;
static int g_video_enabled = 0;
static int g_video_quit = 0;
static int g_hold_last_nonblack = 0;
static uint8_t* g_last_rdram = NULL;
static uint32_t g_last_origin = 0, g_last_width = 0, g_last_height = 0, g_last_format = 0;
static unsigned g_capture_count = 0;
static int g_tem_quadro_apresentado = 0;
static int g_present_smooth = 0;
static int g_present_coverage_2x = 0;
static int g_vi_gamma = -1;
static uint32_t g_pixels_filtrados[VIDEO_W * VIDEO_H];
static int g_vi_filter_2d = -1;
static char g_video_title[256];

/* Bookmark reproduzivel. Restaurar somente RDRAM por baixo das fibers antigas
 * era intrinsecamente inconsistente: pilhas C, filas, TMEM e audio permaneciam
 * no futuro e o jogo travava depois de um ou dois F4. F2 agora grava a entrada
 * indexada pelas leituras do PIF; F4 pede ao TESTAR.bat uma execucao nova, que
 * refaz o caminho em turbo e chega ao mesmo ponto com todas as threads validas. */
static int g_bookmark_restart = 0;

static int video_bookmark_path(char* path, size_t capacity,
                               const char* filename) {
    char pasta[MAX_PATH] = "sav\\bookmarks";
    DWORD n = GetEnvironmentVariableA("WPJ2_BOOKMARK_DIR", pasta,
                                      sizeof(pasta));
    if (n >= sizeof(pasta)) return 0;
    CreateDirectoryA("sav", NULL);
    CreateDirectoryA(pasta, NULL);
    snprintf(path, capacity, "%s\\%s", pasta, filename);
    return 1;
}

static void video_update_title(void) {
    if (!g_video_window) return;
    if (hle_fast_forward_active()) {
        char title[sizeof(g_video_title) + 16];
        snprintf(title, sizeof(title), "%s [8x]", g_video_title);
        SetWindowTextA(g_video_window, title);
    } else {
        SetWindowTextA(g_video_window, g_video_title);
    }
}

/* O VI do jogo liga OS_VI_DITHER_FILTER_ON, mas o filtro real depende dos
 * bits ocultos de cobertura do RDP. O framebuffer minimo guarda somente
 * RGB5551; uma media espacial de cinco pixels nao e equivalente ao VI e
 * mancha gradientes e texturas 3D. A aproximacao antiga fica disponivel
 * somente como lente diagnostica explicita. */
static int video_vi_filter_2d_ativo(void) {
    if (g_vi_filter_2d < 0) {
        const char* e = getenv("WPJ2_VI_FILTER_2D");
        g_vi_filter_2d = e && atoi(e) != 0;
    }
    return g_vi_filter_2d;
}

static unsigned video_luma(uint32_t p) {
    return (((p >> 16) & 255u) * 77u + ((p >> 8) & 255u) * 150u +
            (p & 255u) * 29u) >> 8;
}

static uint32_t video_filtro_5(uint32_t c, uint32_t n, uint32_t s,
                                uint32_t e, uint32_t w) {
    uint32_t r = (((c >> 16) & 255u) * 6u + ((n >> 16) & 255u) +
                  ((s >> 16) & 255u) + ((e >> 16) & 255u) + ((w >> 16) & 255u) + 5u) / 10u;
    uint32_t g = (((c >> 8) & 255u) * 6u + ((n >> 8) & 255u) +
                  ((s >> 8) & 255u) + ((e >> 8) & 255u) + ((w >> 8) & 255u) + 5u) / 10u;
    uint32_t b = ((c & 255u) * 6u + (n & 255u) + (s & 255u) +
                  (e & 255u) + (w & 255u) + 5u) / 10u;
    return (r << 16) | (g << 8) | b;
}

static void video_filtrar_vi_2d(uint32_t height) {
    if (height < 3u) return;
    memcpy(g_pixels_filtrados, g_pixels, sizeof(g_pixels));
    for (uint32_t y = 1; y + 1u < height; y++) for (uint32_t x = 1; x + 1u < VIDEO_W; x++) {
        uint32_t i = y * VIDEO_W + x;
        uint32_t c = g_pixels[i], n = g_pixels[i - VIDEO_W], s = g_pixels[i + VIDEO_W];
        uint32_t e = g_pixels[i + 1u], w = g_pixels[i - 1u];
        unsigned lo = video_luma(c), hi = lo, v[4] = { video_luma(n), video_luma(s),
                                                         video_luma(e), video_luma(w) };
        for (unsigned k = 0; k < 4u; k++) {
            if (v[k] < lo) lo = v[k];
            if (v[k] > hi) hi = v[k];
        }
        /* Filtro anti-aliasing VI: suaviza bordas serrilhadas (poligonos e sprites)
         * em variacoes de luma >= 2 sem esborrar areas de cor uniforme. */
        if (hi - lo >= 2u) g_pixels_filtrados[i] = video_filtro_5(c, n, s, e, w);
    }
    memcpy(g_pixels, g_pixels_filtrados, sizeof(g_pixels));
}

/* O VI pode habilitar a correcao gamma por quadro. Esta etapa pertence a
 * apresentacao, como no plugin do Project64: nunca modifica a RDRAM, F5/F6 ou
 * a entrada do rasterizador. A chave permite comparar sem recompilar. */
static int video_gamma_ativo(uint32_t vi_status) {
    if (g_vi_gamma < 0) {
        const char* e = getenv("WPJ2_VI_GAMMA");
        g_vi_gamma = !e || atoi(e) != 0;
    }
    return g_vi_gamma && (vi_status & 0x08u) != 0;
}

static uint8_t video_expand5(uint32_t v, int gamma) {
    uint32_t c = (v * 255u + 15u) / 31u;
    /* Curva gamma 2.0 do VI: sqrt no espaco normalizado, aproximada de forma
     * inteira para manter esta rotina pequena e deterministica. */
    if (gamma) c = (uint32_t)(sqrt((double)c * 255.0) + 0.5);
    return (uint8_t)c;
}

/* Historico curto de apresentacao: a falha de juncoes e transitória e F5
 * normalmente chega tarde demais para fotografa-la. Ele fica apenas em RAM e
 * só e escrito quando o usuario pede F6. */
#define VIDEO_HISTORY 8u
typedef struct {
    uint32_t pixels[VIDEO_W * VIDEO_H];
    uint32_t origin, width, height, format;
    uint64_t retrace;
} video_history_t;
static video_history_t g_history[VIDEO_HISTORY];
static unsigned g_history_next = 0, g_history_count = 0;

static void video_store_history(void) {
    video_history_t* h = &g_history[g_history_next];
    memcpy(h->pixels, g_pixels, sizeof(g_pixels));
    h->origin = g_last_origin;
    h->width = g_last_width;
    h->height = g_last_height;
    h->format = g_last_format;
    h->retrace = hle_retraces();
    g_history_next = (g_history_next + 1u) % VIDEO_HISTORY;
    if (g_history_count < VIDEO_HISTORY) g_history_count++;
}

static void video_write_bmp_size(const char* nome, const uint32_t* pixels,
                                 uint32_t width, uint32_t height) {
    FILE* imagem = fopen(nome, "wb");
    if (!imagem) return;
    BITMAPFILEHEADER arquivo = {0};
    BITMAPINFOHEADER cabecalho = {0};
    uint32_t bytes = width * height * sizeof(uint32_t);
    arquivo.bfType = 0x4D42;
    arquivo.bfOffBits = sizeof(arquivo) + sizeof(cabecalho);
    arquivo.bfSize = arquivo.bfOffBits + bytes;
    cabecalho.biSize = sizeof(cabecalho);
    cabecalho.biWidth = (LONG)width;
    cabecalho.biHeight = -(LONG)height;
    cabecalho.biPlanes = 1;
    cabecalho.biBitCount = 32;
    cabecalho.biCompression = BI_RGB;
    cabecalho.biSizeImage = bytes;
    fwrite(&arquivo, sizeof(arquivo), 1, imagem);
    fwrite(&cabecalho, sizeof(cabecalho), 1, imagem);
    fwrite(pixels, bytes, 1, imagem);
    fclose(imagem);
}

static void video_write_bmp(const char* nome, const uint32_t* pixels) {
    video_write_bmp_size(nome, pixels, VIDEO_W, VIDEO_H);
}

/* O swapchain Vulkan nao passa por g_pixels. Capturar o cliente ja composto
 * pelo DWM fotografa exatamente a saida RT64 que o usuario esta vendo,
 * incluindo upscale/VI, e exclui bordas e titulo da janela. */
static int video_capture_rt64_client(const char* nome, uint32_t* width,
                                     uint32_t* height) {
    if (!g_video_window || !IsWindowVisible(g_video_window)) return 0;
    RECT client;
    if (!GetClientRect(g_video_window, &client)) return 0;
    LONG w = client.right - client.left, h = client.bottom - client.top;
    if (w <= 0 || h <= 0) return 0;
    POINT origem = {0, 0};
    if (!ClientToScreen(g_video_window, &origem)) return 0;

    HDC screen = GetDC(NULL);
    HDC memory = screen ? CreateCompatibleDC(screen) : NULL;
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* pixels = NULL;
    HBITMAP bitmap = memory ? CreateDIBSection(memory, &bmi, DIB_RGB_COLORS,
                                               &pixels, NULL, 0) : NULL;
    HGDIOBJ anterior = bitmap ? SelectObject(memory, bitmap) : NULL;
    int ok = bitmap && pixels && BitBlt(memory, 0, 0, w, h, screen,
                                        origem.x, origem.y,
                                        SRCCOPY | CAPTUREBLT);
    if (ok) {
        video_write_bmp_size(nome, (const uint32_t*)pixels,
                             (uint32_t)w, (uint32_t)h);
        if (width) *width = (uint32_t)w;
        if (height) *height = (uint32_t)h;
    }
    if (anterior) SelectObject(memory, anterior);
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    if (screen) ReleaseDC(NULL, screen);
    return ok;
}

/* F5 salva o viewport que acabou de ser apresentado, nao uma foto da area de
 * trabalho do Windows. Isso produz um artefato 320x240 reproduzivel e evita
 * que escala da janela, bordas ou outra aplicacao contaminem a comparacao. */
static void video_capture_f5(void) {
    if (!g_last_rdram || !g_last_width || !g_last_height) return;
    char pasta[MAX_PATH] = "temp\\projeto\\testar";
    DWORD n = GetEnvironmentVariableA("WPJ2_CAPTURE_DIR", pasta, sizeof(pasta));
    if (n >= sizeof(pasta)) return;
    CreateDirectoryA(pasta, NULL);
    unsigned id = ++g_capture_count;
    char bmp[MAX_PATH], info[MAX_PATH];
    snprintf(bmp, sizeof(bmp), "%s\\f5_%03u.bmp", pasta, id);
    snprintf(info, sizeof(info), "%s\\f5_%03u.txt", pasta, id);

    uint32_t capture_width = VIDEO_W, capture_height = VIDEO_H;
    int capture_rt64 = rt64_backend_active() &&
        video_capture_rt64_client(bmp, &capture_width, &capture_height);
    if (!capture_rt64) video_write_bmp(bmp, g_pixels);
    /* Mantem a foto visual e arma, separadamente, a proxima tarefa de audio.
       O aperto pode ocorrer no meio do retrace; copiar a proxima AList evita
       um estado parcialmente atualizado e deixa a captura reproduzivel. */
    rsp_request_audio_state_capture();

    FILE* dados = fopen(info, "w");
    if (dados) {
        uint16_t estado = *(uint16_t*)(g_last_rdram + (0x001A7234u ^ 2u));
        uint16_t subestado = *(uint16_t*)(g_last_rdram + (0x001A723Cu ^ 2u));
        uint32_t vi_origin = *(uint32_t*)(g_last_rdram + (0xA4400004u - 0x80000000u));
        fprintf(dados, "captura=%u", id); fputc(10, dados);
        fprintf(dados, "estado=%d/%d", (int16_t)estado, (int16_t)subestado); fputc(10, dados);
        fprintf(dados, "origin_apresentado=%06X", g_last_origin); fputc(10, dados);
        fprintf(dados, "vi_origin_atual=%06X", vi_origin & 0x1FFFFFFFu); fputc(10, dados);
        fprintf(dados, "video=%ux%u formato=%u", g_last_width, g_last_height, g_last_format); fputc(10, dados);
        fprintf(dados, "captura_visual=%s %ux%u",
                capture_rt64 ? "rt64_dwm" : "cpu_rdram",
                capture_width, capture_height); fputc(10, dados);
        fprintf(dados, "retrace=%llu", (unsigned long long)hle_retraces()); fputc(10, dados);
        fprintf(dados, "gfx=%llu audio=%llu", (unsigned long long)rsp_tasks_tipo(1),
                (unsigned long long)rsp_tasks_tipo(2)); fputc(10, dados);
        fprintf(dados, "raster_us=%llu", (unsigned long long)rsp_gfx_raster_last_us()); fputc(10, dados);
        fprintf(dados, "gfx_ultima=%llu tri=%u recebidos/%u rasterizados cull=%u/%u camera=%u z=%llu aceitos/%llu recusados",
                (unsigned long long)rsp_last_gfx_index(), rsp_last_gfx_tri_received(),
                rsp_last_gfx_tri_drawn(), rsp_last_gfx_tri_cull_front(),
                rsp_last_gfx_tri_cull_back(), rsp_last_gfx_tri_camera_rejected(),
                (unsigned long long)rsp_last_gfx_z_accepted(),
                (unsigned long long)rsp_last_gfx_z_rejected()); fputc(10, dados);
        fprintf(dados, "prim=%08X othermode_l=%08X", rsp_prim_color(), rsp_othermode_l()); fputc(10, dados);
        fclose(dados);
    }
    legendas_capturar_rdram(g_last_rdram, pasta, id);
    printf("[captura] F5 -> %s e %s; proxima AList de audio armada", bmp, info); fputc(10, stdout);
    fflush(stdout);
}

static void video_capture_history_f6(void) {
    if (!g_history_count) return;
    char pasta[MAX_PATH] = "temp\\projeto\\testar";
    DWORD n = GetEnvironmentVariableA("WPJ2_CAPTURE_DIR", pasta, sizeof(pasta));
    if (n >= sizeof(pasta)) return;
    CreateDirectoryA(pasta, NULL);
    char info[MAX_PATH];
    snprintf(info, sizeof(info), "%s\\historico.txt", pasta);
    FILE* dados = fopen(info, "w");
    for (unsigned ordem = 0; ordem < g_history_count; ordem++) {
        unsigned indice = (g_history_next + VIDEO_HISTORY - g_history_count + ordem) % VIDEO_HISTORY;
        const video_history_t* h = &g_history[indice];
        char bmp[MAX_PATH];
        snprintf(bmp, sizeof(bmp), "%s\\historico_%02u.bmp", pasta, ordem + 1u);
        video_write_bmp(bmp, h->pixels);
        if (dados)
            fprintf(dados, "%02u retrace=%llu origin=%06X video=%ux%u formato=%u\n",
                    ordem + 1u, (unsigned long long)h->retrace, h->origin,
                    h->width, h->height, h->format);
    }
    if (dados) fclose(dados);
    printf("[captura] F6 -> %u quadro(s) anteriores em %s\\n", g_history_count, pasta);
    fflush(stdout);
}

static void video_checkpoint_f2(void) {
    if (!g_last_rdram) return;
    char replay[MAX_PATH], imagem[MAX_PATH], info[MAX_PATH];
    if (!video_bookmark_path(replay, sizeof(replay), "quick.replay") ||
        !video_bookmark_path(imagem, sizeof(imagem), "quick.bmp") ||
        !video_bookmark_path(info, sizeof(info), "quick.txt")) return;

    int gravou = pif_gravar_replay(replay);
    if (gravou) video_write_bmp(imagem, g_pixels);
    FILE* f = gravou ? fopen(info, "w") : NULL;
    if (f) {
        uint16_t estado = *(uint16_t*)(g_last_rdram + (0x001A7234u ^ 2u));
        uint16_t subestado = *(uint16_t*)(g_last_rdram + (0x001A723Cu ^ 2u));
        fprintf(f, "formato=WPJ2_BOOKMARK_1\n");
        fprintf(f, "retrace=%llu\n", (unsigned long long)hle_retraces());
        fprintf(f, "poll_controle=%llu\n",
                (unsigned long long)pif_polls_atuais());
        fprintf(f, "estado=%d/%d\n", (int16_t)estado, (int16_t)subestado);
        fprintf(f, "framebuffer=%06X %ux%u formato=%u\n", g_last_origin,
                g_last_width, g_last_height, g_last_format);
        fclose(f);
    }
    printf("[bookmark] F2: %s (%s; substitui o bookmark anterior)\n",
           replay, gravou ? "replay + imagem + metadados" : "falha");
    fflush(stdout);
}

static void video_checkpoint_f4(void) {
    char path[MAX_PATH];
    if (!video_bookmark_path(path, sizeof(path), "quick.replay")) return;
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        printf("[bookmark] F4: nenhum bookmark; grave primeiro com F2\n");
        fflush(stdout);
        return;
    }
    g_bookmark_restart = 1;
    printf("[bookmark] F4: reinicio seguro solicitado para %s\n", path);
    fflush(stdout);
}

static void video_blit(HDC dc) {
    if (!g_video_window) return;
    RECT r;
    GetClientRect(g_video_window, &r);
    /* A imagem interna e 320x240. COLORONCOLOR dobra os pixels sem filtro e
     * torna escadas/bordas de letras mais aparentes do que no Project64.
     * HALFTONE atua somente ao ampliar a janela: RDRAM, F5/F6 e o
     * rasterizador 3D permanecem inalterados. */
    SetStretchBltMode(dc, g_present_smooth ? HALFTONE : COLORONCOLOR);
    if (g_present_smooth) SetBrushOrgEx(dc, 0, 0, NULL);
    const uint32_t* pixels = g_present_coverage_2x ? g_pixels_2x : g_pixels;
    uint32_t sw = g_present_coverage_2x ? PRESENT_W : VIDEO_W;
    uint32_t sh = g_present_coverage_2x ? PRESENT_H : VIDEO_H;
    StretchDIBits(dc, 0, 0, r.right - r.left, r.bottom - r.top,
                  0, 0, sw, sh, pixels, &g_bmi,
                  DIB_RGB_COLORS, SRCCOPY);
}

/* A saída original é 4:3. O RT64 acompanha WM_SIZE e recria o swapchain, mas
 * deixar a borda livre deformaria a imagem antes de o renderer aplicar suas
 * barras. Ajustar o RECT durante o arraste mantém o cliente em 4:3. */
static void video_keep_4_3(HWND hwnd, WPARAM edge, RECT* rect) {
    if (!hwnd || !rect) return;
    RECT window, client;
    if (!GetWindowRect(hwnd, &window) || !GetClientRect(hwnd, &client)) return;
    LONG frame_w = (window.right - window.left) - (client.right - client.left);
    LONG frame_h = (window.bottom - window.top) - (client.bottom - client.top);
    LONG outer_w = rect->right - rect->left;
    LONG outer_h = rect->bottom - rect->top;
    LONG client_w = outer_w - frame_w;
    LONG client_h = outer_h - frame_h;
    if (client_w < 320) client_w = 320;
    if (client_h < 240) client_h = 240;

    if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM) {
        client_w = (client_h * 4 + 1) / 3;
        rect->right = rect->left + client_w + frame_w;
    } else {
        client_h = (client_w * 3 + 2) / 4;
        if (edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT)
            rect->top = rect->bottom - client_h - frame_h;
        else
            rect->bottom = rect->top + client_h + frame_h;
    }
}

static LRESULT CALLBACK video_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        video_blit(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        g_video_window = NULL;
        g_video_quit = 1;
        return 0;
    }
    if (msg == WM_SIZING) {
        video_keep_4_3(hwnd, wp, (RECT*)lp);
        return TRUE;
    }
    if (msg == WM_GETMINMAXINFO) {
        MINMAXINFO* limits = (MINMAXINFO*)lp;
        RECT minimum = {0, 0, 320, 240};
        DWORD style = (DWORD)GetWindowLongPtr(hwnd, GWL_STYLE);
        DWORD exstyle = (DWORD)GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        AdjustWindowRectEx(&minimum, style, FALSE, exstyle);
        limits->ptMinTrackSize.x = minimum.right - minimum.left;
        limits->ptMinTrackSize.y = minimum.bottom - minimum.top;
        return 0;
    }
    /* ---- Teclado como controle do N64 ----
     *
     * Ate aqui os botoes so existiam por variavel de ambiente (WPJ2_BUTTONS,
     * WPJ2_INPUT, WPJ2_INPUT_POLLS), o que serve para reproduzir uma sequencia
     * mas nao para explorar menus. Sem isto nao ha como confirmar visualmente
     * em que tela o jogo esta - so inferir pelo numero de estado.
     *
     * Estado acumulado num mapa de bits: teclas simultaneas viram botoes
     * simultaneos. WM_KEYDOWN repete enquanto a tecla fica presa, e o jogo
     * reage a transicao, entao repeticao nao atrapalha - o valor so muda
     * quando a tecla e solta. */
    if (msg == WM_KEYDOWN || msg == WM_KEYUP) {
        uint16_t bit = 0;
        int analogico = 0;
        static int state_up = 0, state_down = 0;
        static int state_left = 0, state_right = 0;

        switch (wp) {
            case VK_RETURN: bit = 0x1000; break;            /* START */
            case 'X': case VK_SPACE: bit = 0x8000; break;   /* A */
            case 'Z': bit = 0x4000; break;                  /* B */
            case 'C': bit = 0x2000; break;                  /* Z */
            case 'Q': bit = 0x0020; break;                  /* L */
            case 'E': bit = 0x0010; break;                  /* R */
            case 'W': bit = 0x0800; break;                  /* D-Pad cima */
            case 'S': bit = 0x0400; break;                  /* D-Pad baixo */
            case 'A': bit = 0x0200; break;                  /* D-Pad esquerda */
            case 'D': bit = 0x0100; break;                  /* D-Pad direita */
            case 'I': bit = 0x0008; break;                  /* C cima */
            case 'K': bit = 0x0004; break;                  /* C baixo */
            case 'J': bit = 0x0002; break;                  /* C esquerda */
            case 'L': bit = 0x0001; break;                  /* C direita */
            case VK_UP:    state_up = (msg == WM_KEYDOWN); analogico = 1; break;
            case VK_DOWN:  state_down = (msg == WM_KEYDOWN); analogico = 1; break;
            case VK_LEFT:  state_left = (msg == WM_KEYDOWN); analogico = 1; break;
            case VK_RIGHT: state_right = (msg == WM_KEYDOWN); analogico = 1; break;
            default: break;
        }
        if (bit || analogico) {
            static uint16_t pressionados = 0;
            uint16_t antes = pressionados;
            if (bit) {
                if (msg == WM_KEYDOWN) pressionados |= bit;
                else                   pressionados &= (uint16_t)~bit;
            }

            pif_update_stick_from_keys(state_up, state_down, state_left, state_right);

            if (pressionados != antes) pif_set_buttons(pressionados);
            printf("[controle] botoes=0x%04X analogico=%s%s%s%s%s\n",
                   pressionados,
                   (!state_up && !state_down && !state_left && !state_right) ? "CENTRO" : "",
                   state_up ? "CIMA " : "",
                   state_down ? "BAIXO " : "",
                   state_left ? "ESQ " : "",
                   state_right ? "DIR " : "");
            fflush(stdout);
            return 0;
        }
    }
    if (msg == WM_KEYDOWN && wp == VK_F5) {
        video_capture_f5();
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_F6) {
        video_capture_history_f6();
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_F2 && !(lp & (1L << 30))) {
        video_checkpoint_f2();
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_F4 && !(lp & (1L << 30))) {
        video_checkpoint_f4();
        return 0;
    }
    /* F10 fica reservado: elevar a cadencia parava fibers sem avancar a ROM.
     * F2/F4 usa reinicio com reproducao deterministica. */
    if (msg == WM_KEYDOWN && wp == VK_F10 && !(lp & (1L << 30))) {
        printf("[controle] F10: aceleracao indisponivel; use F2/F4\n");
        fflush(stdout);
        return 0;
    }
    /* Avanco momentaneo: pressionar muda a cadencia emulada para 8x e tira o
       audio hospedado do caminho critico; soltar restaura ambos imediatamente. */
    if ((msg == WM_KEYDOWN || msg == WM_KEYUP) && wp == VK_F11) {
        int enabled = msg == WM_KEYDOWN;
        hle_set_fast_forward(enabled);
        audio_set_fast_forward(enabled);
        video_update_title();
        if (!(lp & (1L << 30)) || msg == WM_KEYUP) {
            printf("[controle] F11: velocidade %s\n", enabled ? "8x" : "normal");
            fflush(stdout);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void video_pump_messages(void) {
    if (!g_video_enabled || !g_video_window) return;
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

int video_init(void) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = video_wndproc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "WPJ2RecompPreview";
    RegisterClassA(&wc); /* ja registrada em uma segunda execucao e normal */

    const char* titulo = getenv("WPJ2_WINDOW_TITLE");
    if (!titulo || !*titulo) titulo = "Wonder Project J2 - prototipo recompilado";
    snprintf(g_video_title, sizeof(g_video_title), "%s", titulo);
    {
        const char* e = getenv("WPJ2_PRESENT_COVERAGE_2X");
        g_present_coverage_2x = !e || atoi(e) != 0;
    }
    DWORD estilo = WS_OVERLAPPEDWINDOW;
    RECT wr = { 0, 0, (LONG)PRESENT_W, (LONG)PRESENT_H };
    AdjustWindowRect(&wr, estilo, FALSE);
    g_video_window = CreateWindowExA(0, wc.lpszClassName, titulo, estilo,
        CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
        NULL, NULL, wc.hInstance, NULL);
    if (!g_video_window) {
        printf("[video] nao foi possivel criar a janela (%lu)\n", GetLastError());
        return 0;
    }
    g_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bmi.bmiHeader.biWidth = g_present_coverage_2x ? PRESENT_W : VIDEO_W;
    g_bmi.bmiHeader.biHeight = -(LONG)(g_present_coverage_2x ? PRESENT_H : VIDEO_H);
    g_bmi.bmiHeader.biPlanes = 1;
    g_bmi.bmiHeader.biBitCount = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;
    ShowWindow(g_video_window, SW_SHOW);
    UpdateWindow(g_video_window);
    g_video_enabled = 1;
    g_video_quit = 0;
    g_hold_last_nonblack = getenv("WPJ2_WINDOW_HOLD_LAST") != NULL &&
                           atoi(getenv("WPJ2_WINDOW_HOLD_LAST")) != 0;
    /* O HALFTONE do GDI borra o quadro inteiro e nao corresponde ao filtro VI
     * nem ao coverage do RDP. Com janela 640x480 a escala e exatamente 2x;
     * COLORONCOLOR preserva o pixel resolvido pelo rasterizador. O modo suave
     * continua disponivel apenas para comparacao explicita. */
    {
        const char* e = getenv("WPJ2_PRESENT_SMOOTH");
        g_present_smooth = e && atoi(e) != 0;
    }
    printf("[video] janela aberta: %s (coverage %s, cliente 640x480)\n", titulo,
           g_present_coverage_2x ? "2x" : "nativo");
    fflush(stdout);
    return 1;
}

void video_present(uint8_t* rdram, uint32_t origin, uint32_t width,
                   uint32_t height, uint32_t format, uint32_t vi_status) {
    if (!g_video_enabled || !g_video_window || !rdram || format != 2 || !width ||
        !height || height > VIDEO_H)
        return;
    video_pump_messages();
    if (!g_video_window) return;

    /* RT64 recebe as listas GBI diretamente e apresenta pela mesma janela.
     * Não ler nem converter o framebuffer pela CPU nessa rota: além do custo,
     * isso sobrescreveria a swap chain que acabou de ser renderizada. */
    if (rt64_backend_active()) {
        /* F5 e bookmarks ainda precisam dos metadados do VI/RDRAM mesmo que
           os pixels finais pertençam ao swapchain da GPU. */
        g_last_rdram = rdram;
        g_last_origin = origin;
        g_last_width = width;
        g_last_height = height;
        g_last_format = format;
        g_tem_quadro_apresentado = 1;
        g_last_present = GetTickCount64();
        rt64_backend_present();
        return;
    }

    /* A tarefa gráfica pode terminar muitas vezes entre duas apresentações.
       O limitador é só do host: testá-lo antes de ler e converter o
       framebuffer evita cópias 320x240 que seriam descartadas e não altera
       RDRAM, RSP ou a cadência observada pela ROM. */
    ULONGLONG now = GetTickCount64();
    if (now - g_last_present < 16) return;

    /* A RDRAM nao representa sozinha a ordem entre os buffers frontal e de
       trabalho quando o RDP e concluido sincronicamente no host. O RSP marca
       uma troca observada no vídeo de referência: conservar o último quadro
       íntegro, exibir preto por alguns retraces e só então voltar ao VI. */
    int transicao = rsp_transition_presentation_mode();
    if (transicao == 1 && g_tem_quadro_apresentado) return;
    if (transicao == 2) {
        memset(g_pixels, 0, sizeof(g_pixels));
        memset(g_pixels_2x, 0, sizeof(g_pixels_2x));
        g_last_rdram = rdram;
        g_last_origin = origin;
        g_last_width = width;
        g_last_height = height;
        g_last_format = format;
        g_tem_quadro_apresentado = 1;
        goto apresentar;
    }

    /* A ROM alterna o alvo real com um buffer que acabou de ser limpo. Como o
       preview ainda nao usa o VI para selecionar a origem, esse buffer preto
       apagava uma cena valida poucos milissegundos depois de ela aparecer.
       No modo de demonstracao, conservar o ultimo quadro com conteudo permite
       inspecionar a cena; o renderizador/RDRAM continuam inalterados. */
    if (g_hold_last_nonblack) {
        uint32_t nonblack = 0;
        for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < VIDEO_W; x++) {
            uint16_t p = *(uint16_t*)(rdram + ((origin + (y * width + x) * 2) ^ 2));
            if (p & 0xFFFEu && ++nonblack >= 256u) break;
        }
        if (nonblack < 256u) return;
    }

    /* A RDRAM e armazenada no host com meias-palavras trocadas. */
    memset(g_pixels, 0, sizeof(g_pixels));
    int gamma = video_gamma_ativo(vi_status);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < VIDEO_W; x++) {
            uint16_t p = *(uint16_t*)(rdram + ((origin + (y * width + x) * 2) ^ 2));
            uint32_t r = video_expand5((p >> 11) & 31u, gamma);
            uint32_t g = video_expand5((p >> 6)  & 31u, gamma);
            uint32_t b = video_expand5((p >> 1)  & 31u, gamma);
            g_pixels[y * VIDEO_W + x] = (r << 16) | (g << 8) | b;
        }
    }
    /* Acionado pelo bit que o proprio jogo liga, nao por heuristica de cena.
     *
     * Antes isto era `estado_jogo == 8`, escolhido porque a abertura era onde
     * a granulacao incomodava. Mas o jogo declara a intencao explicitamente em
     * tools/wonder-source/src/main.c:
     *
     *     osViSetSpecialFeatures(OS_VI_DITHER_FILTER_ON |
     *                            OS_VI_GAMMA_DITHER_OFF | OS_VI_GAMMA_OFF);
     *
     * e o VI_CONTROL lido em execucao (0x13002) confirma: bit 16 ligado. Vale
     * para o jogo inteiro, nao so para a abertura.
     *
     * ATENCAO ao numero do bit. DITHER_FILTER_ENABLE e 0x10000 (bit 16); os
     * bits 12-15 sao PIXEL_ADVANCE, que em 0x13002 valem 3. Ja errei isto uma
     * vez lendo o bit 11.
     *
     * Isto ataca granulacao de dither. NAO ataca o serrilhado - o anti-alias
     * do N64 depende de coverage produzido na rasterizacao, que ainda nao
     * existe; ver RETOMADA.md. */
    #define VI_CTRL_DITHER_FILTER 0x10000u
    if (video_vi_filter_2d_ativo() && (vi_status & VI_CTRL_DITHER_FILTER))
        video_filtrar_vi_2d(height);
    if (g_present_coverage_2x) {
        if (!rsp_coverage_frame_2x(rdram, origin, width, height, g_pixels_2x)) {
            for (uint32_t y = 0; y < PRESENT_H; y++) for (uint32_t x = 0; x < PRESENT_W; x++)
                g_pixels_2x[y * PRESENT_W + x] = g_pixels[(y >> 1) * VIDEO_W + (x >> 1)];
        } else if (gamma) {
            for (uint32_t i = 0; i < PRESENT_W * PRESENT_H; i++) {
                uint32_t p = g_pixels_2x[i];
                uint32_t r = (uint32_t)(sqrt((double)((p >> 16) & 255u) * 255.0) + 0.5);
                uint32_t g = (uint32_t)(sqrt((double)((p >> 8) & 255u) * 255.0) + 0.5);
                uint32_t b = (uint32_t)(sqrt((double)(p & 255u) * 255.0) + 0.5);
                g_pixels_2x[i] = (r << 16) | (g << 8) | b;
            }
        }
    }
    g_last_rdram = rdram;
    g_last_origin = origin;
    g_last_width = width;
    g_last_height = height;
    g_last_format = format;
    g_tem_quadro_apresentado = 1;
    /* O renderer ainda e uma sonda CPU; limitar a pintura evita que a GUI
       altere a temporizacao da ROM enquanto ela esta sendo investigada. */
apresentar:
    video_store_history();
    g_last_present = now;
    HDC dc = GetDC(g_video_window);
    if (dc) {
        video_blit(dc);
        ReleaseDC(g_video_window, dc);
    }
}

void video_shutdown(void) {
    rt64_backend_shutdown();
    if (g_video_window) DestroyWindow(g_video_window);
    g_video_window = NULL;
    g_video_enabled = 0;
}

int video_quit_requested(void) { return g_video_quit; }
int video_bookmark_restart_requested(void) { return g_bookmark_restart; }
void* video_native_window(void) { return g_video_window; }
