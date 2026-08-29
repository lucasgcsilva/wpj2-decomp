/* Semantica de SP_STATUS, e a conclusao instantanea de tarefa que ela permite.
 *
 * `SP_STATUS` nao e um registrador de dado: na leitura ele reporta estado, mas na
 * escrita cada bit e um *comando* de por ou tirar outro bit. Com a MMIO sendo
 * memoria crua, `__osSpSetStatus(0x2B00)` gravava 0x2B00 por cima do estado e
 * apagava o bit 0, "RSP parado". Dali em diante `__osSpSetPc` (func_800CD020)
 * devolvia -1 para sempre e `osSpTaskLoad` girava num laco de nova tentativa:
 * 3,5 milhoes de chamadas em 20 segundos, sem sair do lugar.
 *
 * Aqui o estado e mantido a parte e so o resultado vai para a memoria emulada.
 * E como nao ha RSP, uma tarefa iniciada termina no mesmo instante: o bit de
 * parado volta, o de `broke` e posto, e fica pendente uma interrupcao de SP.
 * Isso e diferente de anunciar conclusao a cada quadro - o que ja foi testado e
 * piorou o resultado. A diferenca e a condicao: agora so se anuncia o fim de uma
 * tarefa que alguem de fato iniciou.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

#include "runtime.h"
#include "funcs.h"
#include "rt64_backend.h"

#define SP_STATUS_ADDR   0xA4040010u
#define SP_PC_ADDR       0xA4080000u

/* Bits de leitura. */
#define ST_HALT          (1u << 0)
#define ST_BROKE         (1u << 1)
#define ST_INTR_ON_BREAK (1u << 6)

#define SPMEM_SIZE   0x2000        /* 4 KB de DMEM + 4 KB de IMEM */
#define TASK_OFFSET  0x0FC0        /* onde a libultra deposita a OSTask */
/* Na ABI1 os offsets nas ALists de audio sao relativos a este ponto da DMEM,
   nao ao inicio fisico 0x000. */
#define AUDIO_DMEM_BASE 0x05C0u

/* Definidos adiante, junto da memoria do RSP. */
static uint32_t sp_word(uint32_t off);
/* Precisa existir antes do handler SP: a ponte RT64 recebe DMEM/IMEM da
 * própria tarefa no instante em que o RSP é solto. */
static uint8_t g_spmem[SPMEM_SIZE];
static uint16_t rsp_rdram16(uint8_t* rdram, uint32_t phys) {
    return *(uint16_t*)(rdram + (phys ^ 2u));
}
static void run_acmd_list(uint8_t* rdram, uint32_t phys, uint32_t bytes);
static void acmd_capture_janela(uint8_t* rdram, uint32_t phys, uint32_t bytes);
static void andar_dl(uint8_t* rdram, uint32_t phys, int nivel, FILE* saida);
static void desenhar_dl(uint8_t* rdram, uint32_t phys, int nivel);
void rsp_dump_alvo(uint8_t* rdram, const char* nome_base, uint32_t origin);
static uint64_t g_gfx_listas = 0;
static uint32_t g_gfx_lista_min = UINT32_MAX, g_gfx_lista_max = 0;
static int g_gfx_lista_grande_dumpada = 0;
static int g_cena_3d_fotografada = 0;
static int g_estado_8_1_fotografado = 0;
static int g_estado_8_26_fotografado = 0;
static unsigned g_estado_12_tarefas = 0, g_estado_12_fotos = 0;
/* Contexto da tarefa para o CSV opt-in de alpha; precisa existir antes de
 * func_800CD010, que prepara esses valores logo antes do rasterizador. */
static FILE* g_alpha_trace = NULL;
static int g_alpha_trace_inicializado = 0;
/* TRI1 com alfa variavel: diagnostico opt-in para separar transicoes de HUD
 * de sprites/modelos. O arquivo e limitado e nunca e aberto em release. */
static FILE* g_tri_alpha_trace = NULL;
static int g_tri_alpha_trace_inicializado = 0;
static unsigned g_tri_alpha_trace_linhas = 0;
static int g_tri_alpha_trace_gfx_min = -1;
static int g_tri_alpha_trace_gfx_max = -1;
static int g_tri_alpha_trace_todos = -1;
typedef struct {
    uint32_t prim, othermode, combine0, combine1, timg;
    uint8_t on, tile, fmt, siz, linha;
} tri_alpha_chave_t;
static uint64_t g_tri_alpha_trace_gfx_atual = UINT64_MAX;
static tri_alpha_chave_t g_tri_alpha_trace_chaves[96];
static unsigned g_tri_alpha_trace_chaves_n = 0;
static uint64_t g_alpha_trace_tarefa = 0;
static int16_t g_alpha_trace_estado = 0, g_alpha_trace_subestado = 0;
static uint8_t g_texture_definida_na_tarefa = 0;
/* Sonda curta da logo ENIX: o zoom que mostra juncoes acontece em 8/1. */
static FILE* g_texrect_trace = NULL;
static int g_texrect_trace_inicializado = 0;
static unsigned g_texrect_trace_linhas = 0;
/* Por padrão a sonda cobre a abertura 8/1. Em uma revisão posterior pode-se
 * escolher outro estado (por exemplo "8/19") sem tornar o log global. */
static int g_texrect_trace_estado = -1, g_texrect_trace_subestado = -1;
static int g_texrect_trace_estado_lido = 0;
static int g_texrect_trace_gfx_min = -1, g_texrect_trace_gfx_max = -1;
/* Sonda curta para a passagem 2D -> 3D. O Project64 confirmou que a ROM
 * emite um FILLRECT preto entre as duas fases; este CSV confirma se o nosso
 * rasterizador escreveu o mesmo alvo, sem despejar listas ou quadros. */
static FILE* g_transicao_trace = NULL;
static int g_transicao_trace_inicializado = 0;
/* Captura opt-in de tarefas graficas exatas. Serve para ligar uma tela vista
 * pelo usuario aos recursos/TEXRECT daquele mesmo quadro sem criar imagens na
 * raiz. Ex.: WPJ2_CAPTURE_GFX=1529,1545,1700; WPJ2_OUT aponta para temp/. */
static int g_capture_gfx_inicializado = 0;
static uint64_t g_capture_gfx[32];
static unsigned g_capture_gfx_n = 0;
/* A lista de entrada envia pequenos lotes 2D e 3D sob o mesmo estado lógico.
 * Depois do primeiro TRI da nova cena, os mosaicos RGBA16 da imagem anterior
 * não podem mais voltar ao framebuffer: no hardware já pertencem ao quadro
 * anterior, enquanto no rasterizador CPU síncrono chegavam tarde. */
static int g_transicao_3d_iniciada = 0;
static int g_transicao_3d_limpou_tarefa = 0;
static int g_transicao_mosaico_2d = 0;
static uint64_t g_transicao_preto_ate = 0;
static int g_transicao_apresentacao = 0;
/* Balanco de textura da lista corrente.  A agregacao da corrida inteira nao
 * responde se um quadro 3D especifico recebeu os seus recursos antes de ser
 * rasterizado; estes campos so alimentam o diagnostico limitado de 12/50. */
static uint64_t g_tarefa_lb_cargas = 0, g_tarefa_lb_bytes = 0;
static uint64_t g_tarefa_lb_nao_zero = 0;
static uint32_t g_tarefa_lb_primeira = 0, g_tarefa_lb_ultima = 0;
static unsigned g_estado_12_texturas = 0;
/* SETOTHERMODE_L e o estado de blend/coverage do RDP. A primeira imagem do
 * tunel contem uma mascara escura que nao pode ser explicada so por textura
 * opaca; registramos os poucos modos emitidos antes de implementar blend. */
static uint32_t g_tarefa_oml_w0[8], g_tarefa_oml_w1[8];
static unsigned g_tarefa_oml_n = 0, g_tarefa_oml_total = 0;
static uint32_t g_tarefa_fog = 0, g_tarefa_blend = 0;
/* Comparacao direta com o oraculo: somente a matriz MODELVIEW dinamica
 * param=02 da raiz da cena em 12/50. Ela pode representar camera ou mundo;
 * para o rasterizador a transformacao resultante e a mesma. */
static int g_rastrear_camera_12 = 0;
static unsigned g_camera_12_amostra = 0;
static uint32_t g_cimg_addr = 0;      /* alvo de desenho corrente */
static uint32_t g_cimg_larg = 320, g_cimg_siz = 2;
static uint32_t g_zimg_addr = 0;      /* alvo de profundidade (G_SETZIMG) */
/* G_MW_FOG carrega dois valores fixos 8.8.  A lista do tunel usa
 * 0x0500/0xFC67 e F8=000000FF: neblina preta que encobre o trono no inicio. */
static int16_t g_fog_multiplicador = 0, g_fog_offset = 0;
static uint32_t g_fog_color = 0;
static int g_tmem_interleave = 1;
/* A alternancia DXT vale tambem para RGBA16 no RDP. Mantemos esta chave
 * separada enquanto validamos o material do corredor contra o Project64. */
static int g_tmem_interleave_rgba16 = -1;
static int g_tmem_interleave_ci = -1;
#ifdef WPJ2_RELEASE
static int g_rsp_debug = 0;
#else
static int g_rsp_debug = 1;
#endif

static const char* g_prefixo = "";    /* prefixo de saida da corrida */
void rsp_set_prefix(const char* p) { g_prefixo = p; }

static int capturar_gfx_atual(void) {
    if (!g_capture_gfx_inicializado) {
        g_capture_gfx_inicializado = 1;
        const char* e = getenv("WPJ2_CAPTURE_GFX");
        while (e && *e && g_capture_gfx_n < 32u) {
            char* fim = NULL;
            unsigned long long v = strtoull(e, &fim, 0);
            if (fim == e) break;
            g_capture_gfx[g_capture_gfx_n++] = (uint64_t)v;
            e = (*fim == ',') ? fim + 1 : fim;
        }
    }
    for (unsigned i = 0; i < g_capture_gfx_n; i++)
        if (g_capture_gfx[i] == g_gfx_listas) return 1;
    return 0;
}

static uint32_t g_status = ST_HALT;   /* liga parado */
static uint64_t g_tasks = 0;
/* Resumo da lista grafica corrente/recem-concluida. Fica antes do handler SP,
 * que precisa zerar e publicar estes valores. */
static uint32_t g_tarefa_tri_recebidos = 0, g_tarefa_tri_desenhados = 0;
static uint32_t g_tarefa_tri_cull_frente = 0, g_tarefa_tri_cull_tras = 0;
static uint32_t g_tarefa_tri_camera_recusados = 0;
static uint64_t g_tarefa_z_aceitos = 0, g_tarefa_z_recusados = 0;
static uint64_t g_tarefa_tri_pixels = 0;
static uint64_t g_ultima_gfx_indice = 0;
static uint32_t g_ultima_gfx_tri_recebidos = 0, g_ultima_gfx_tri_desenhados = 0;
static uint32_t g_ultima_gfx_tri_cull_frente = 0, g_ultima_gfx_tri_cull_tras = 0;
static uint32_t g_ultima_gfx_tri_camera_recusados = 0;
static uint64_t g_ultima_gfx_z_aceitos = 0, g_ultima_gfx_z_recusados = 0;
static uint64_t g_ultima_gfx_tri_pixels = 0;
static int g_ultima_gfx_tem_triangulos = 0;
/* Custo real da passagem CPU que substitui o RDP.  Essa medicao fica fora do
 * caminho de pixel e permite distinguir uma queda de FPS do host de uma lista
 * 3D que simplesmente ficou cara demais para os 16,67 ms do VI. */
static uint64_t g_gfx_raster_last_us = 0;
static uint64_t g_gfx_raster_peak_us = 0;
static uint64_t g_gfx_raster_total_us = 0;
static uint64_t g_gfx_raster_samples = 0;
/* Histograma barato do custo do backend. Permanece sempre em memória, mas só
 * imprime ocorrências individuais quando WPJ2_RT64_PERF está ativo. Assim a
 * própria instrumentação não altera a cadência do perfil normal. */
static uint64_t g_gfx_raster_buckets[7]; /* <1, <2, <4, <8, <16, <33, >=33 ms */
static uint64_t g_gfx_raster_slow_logged;
static LARGE_INTEGER g_gfx_task_prev, g_gfx_task_freq;
static uint64_t g_gfx_task_interval_buckets[5]; /* <10, <20, <25, <40, >=40 ms */
static uint64_t g_gfx_task_interval_logged;
static uint64_t g_gfx_fast_skipped = 0;
static unsigned g_gfx_fast_phase = 0;
/* Mesmo tipo de telemetria para a AList: o chiado e a cadencia precisam ser
 * medidos separadamente do raster 3D para nao se atribuir ao video um atraso
 * que venha do mixer de audio. */
static uint64_t g_acmd_last_us = 0;
static uint64_t g_acmd_peak_us = 0;
static uint64_t g_acmd_total_us = 0;
static uint64_t g_acmd_samples = 0;

uint64_t rsp_gfx_raster_last_us(void) { return g_gfx_raster_last_us; }
uint64_t rsp_gfx_raster_peak_us(void) { return g_gfx_raster_peak_us; }
uint64_t rsp_gfx_raster_total_us(void) { return g_gfx_raster_total_us; }
uint64_t rsp_gfx_raster_samples(void) { return g_gfx_raster_samples; }
uint64_t rsp_acmd_last_us(void) { return g_acmd_last_us; }
uint64_t rsp_acmd_peak_us(void) { return g_acmd_peak_us; }
uint64_t rsp_acmd_total_us(void) { return g_acmd_total_us; }
uint64_t rsp_acmd_samples(void) { return g_acmd_samples; }
/* Uma unica flag perdia conclusoes: audio e grafico podem terminar no mesmo
 * intervalo entre retraces, e a ultima (frequentemente audio) sobrescrevia a
 * interrupcao SP da lista grafica. O hardware gera uma interrupcao por tarefa;
 * esta fila reproduz essa ordem, sem criar conclusoes que nao aconteceram. */
#define TASK_DONE_CAP 256
static uint8_t g_task_done[TASK_DONE_CAP];
static uint32_t g_task_done_first = 0, g_task_done_count = 0;
static uint32_t g_task_done_peak = 0;
static uint64_t g_task_done_lost = 0;

static void rsp_enqueue_task_done(int type) {
    if (g_task_done_count < TASK_DONE_CAP) {
        uint32_t at = (g_task_done_first + g_task_done_count) % TASK_DONE_CAP;
        g_task_done[at] = (uint8_t)type;
        g_task_done_count++;
        if (g_task_done_count > g_task_done_peak) g_task_done_peak = g_task_done_count;
    } else {
        g_task_done_lost++;
        if (g_task_done_lost == 1) {
            printf("  [rsp] fila de conclusoes cheia; proximas tarefas serao descartadas\n");
            fflush(stdout);
        }
    }
}

static void rsp_poll_rt64_completions(void) {
    while (rt64_backend_take_completed()) rsp_enqueue_task_done(1);
}

void rsp_sync_rt64_completions(void) {
    rt64_backend_sync();
    rsp_poll_rt64_completions();
}

/* Contagem por tipo de tarefa.
 *
 * Antes eu so imprimia o tipo das quatro primeiras tarefas, e concluia dali que
 * nenhuma era grafica. Isso nao se sustenta: uma tarefa grafica pode aparecer na
 * centesima. Contar todas e a unica forma de a afirmacao valer. */
#define TIPO_MAX 4
static uint64_t g_por_tipo[TIPO_MAX];

uint64_t rsp_tasks(void) { return g_tasks; }
uint64_t rsp_tasks_tipo(int t) { return (t >= 0 && t < TIPO_MAX) ? g_por_tipo[t] : 0; }

/* Devolve o *tipo* da proxima tarefa concluida, nao apenas "houve uma".
 *
 * SP, DP e PRENMI usam a mesma OSMesgQueue (0x80153E90), mas SP e DP sao
 * mensagens diferentes da mesma tarefa grafica. O HLE entrega SP primeiro e
 * posterga DP para a proxima troca de contexto. Uma tarefa de audio nao usa o
 * RDP e nao gera DP. */
int rsp_take_task_done(void) {
    rsp_poll_rt64_completions();
    if (g_task_done_count == 0) return 0;
    int d = g_task_done[g_task_done_first];
    g_task_done_first = (g_task_done_first + 1) % TASK_DONE_CAP;
    g_task_done_count--;
    return d;
}

/* A conclusao deve permanecer pendente enquanto a fila de eventos do jogo
 * estiver cheia. Espiar antes de remover evita perder uma interrupcao de RSP
 * por uma condicao de corrida do host. */
int rsp_peek_task_done(void) {
    rsp_poll_rt64_completions();
    return g_task_done_count ? g_task_done[g_task_done_first] : 0;
}

static void publish(uint8_t* rdram) {
    MEM_W(0, (gpr)(int32_t)SP_STATUS_ADDR) = (int32_t)g_status;
}

void rsp_state_capture(wpj2_rsp_state_image* image) {
    if (!image) return;
    memset(image, 0, sizeof(*image));
    memcpy(image->spmem, g_spmem, sizeof(g_spmem));
    memcpy(image->task_done, g_task_done, sizeof(g_task_done));
    image->status = g_status;
    image->task_done_first = g_task_done_first;
    image->task_done_count = g_task_done_count;
}

void rsp_state_restore(uint8_t* rdram, const wpj2_rsp_state_image* image) {
    if (!image || image->task_done_first >= TASK_DONE_CAP ||
        image->task_done_count > TASK_DONE_CAP) return;
    memcpy(g_spmem, image->spmem, sizeof(g_spmem));
    memcpy(g_task_done, image->task_done, sizeof(g_task_done));
    g_status = image->status;
    g_task_done_first = image->task_done_first;
    g_task_done_count = image->task_done_count;
    publish(rdram);
}

/* __osSpSetPc e __osSpDeviceBusy precisam observar o estado do dispositivo,
 * nao a pagina MMIO usada como espelho. Numa execucao longa da loja o espelho
 * terminou com bits DMA/IO ocupados e osSpTaskLoad ficou preso antes de
 * submeter a proxima tarefa; imagem e audio pararam juntos. Isso nao pode ser
 * uma DMA real pendente: func_800CD060 executa a copia sincronicamente e
 * devolve somente depois de conclui-la.
 *
 * A referencia em libreultra confirma os contratos: __osSpSetPc so aceita a
 * escrita com HALT ativo e __osSpDeviceBusy consulta exclusivamente
 * DMA_BUSY/DMA_FULL/IO_FULL. Nesta ponte esses tres bits nunca ficam pendentes.
 */
void func_800CD020(uint8_t* rdram, recomp_context* ctx) {
    if (!(g_status & ST_HALT)) {
        ctx->r2 = (gpr)(int32_t)-1;
        return;
    }
    MEM_W(0, (gpr)(int32_t)SP_PC_ADDR) = (int32_t)(uint32_t)ctx->r4;
    ctx->r2 = 0;
}

void func_800CD0F0(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = 0;
}

/* __osSpSetStatus(u32 value). Cada par de bits da escrita e "tirar" e "por". */
void func_800CD010(uint8_t* rdram, recomp_context* ctx) {
    uint32_t v = (uint32_t)ctx->r4;

    if (v & (1u << 0)) g_status &= ~ST_HALT;          /* soltar o RSP     */
    if (v & (1u << 1)) g_status |=  ST_HALT;
    if (v & (1u << 2)) g_status &= ~ST_BROKE;
    if (v & (1u << 7)) g_status &= ~ST_INTR_ON_BREAK;
    if (v & (1u << 8)) g_status |=  ST_INTR_ON_BREAK;

    /* Sinais 0 a 7: bits 9/10, 11/12, ... 23/24 na escrita; 7 a 14 na leitura. */
    for (int s = 0; s < 8; s++) {
        if (v & (1u << (9 + s * 2)))     g_status &= ~(1u << (7 + s));
        if (v & (1u << (9 + s * 2 + 1))) g_status |=  (1u << (7 + s));
    }

    if (!(g_status & ST_HALT)) {
        /* O RSP foi solto. Aqui e onde o microcodigo rodaria; no lugar dele,
           executamos os comandos da lista que movem dados. A tarefa comeca e
           acaba entre uma instrucao e outra. */
        uint32_t type = sp_word(TASK_OFFSET + 0x00);
        uint32_t lista = sp_word(TASK_OFFSET + 0x30) & 0x1FFFFFFFu;
        if (type == 2) {
            run_acmd_list(rdram, lista, sp_word(TASK_OFFSET + 0x34));
        } else if (type == 1 && lista && lista < 0x800000u) {
            LARGE_INTEGER task_now;
            QueryPerformanceCounter(&task_now);
            if (!g_gfx_task_freq.QuadPart) QueryPerformanceFrequency(&g_gfx_task_freq);
            if (g_gfx_task_prev.QuadPart && g_gfx_task_freq.QuadPart) {
                double interval_ms = (double)(task_now.QuadPart - g_gfx_task_prev.QuadPart) *
                                     1000.0 / (double)g_gfx_task_freq.QuadPart;
                unsigned ib = interval_ms < 10.0 ? 0u : interval_ms < 20.0 ? 1u :
                              interval_ms < 25.0 ? 2u : interval_ms < 40.0 ? 3u : 4u;
                g_gfx_task_interval_buckets[ib]++;
                const char* perf_interval = getenv("WPJ2_RT64_PERF");
                if (perf_interval && *perf_interval && *perf_interval != '0' &&
                    interval_ms >= 25.0 && g_gfx_task_interval_logged < 96u) {
                    printf("[gfx-gap] task=%llu intervalo=%.3fms func=%08X\n",
                           (unsigned long long)(g_gfx_listas + 1u), interval_ms,
                           trace_last_func());
                    fflush(stdout);
                    g_gfx_task_interval_logged++;
                }
            }
            g_gfx_task_prev = task_now;
            uint32_t bytes = sp_word(TASK_OFFSET + 0x34);
            if (bytes < g_gfx_lista_min) g_gfx_lista_min = bytes;
            if (bytes > g_gfx_lista_max) g_gfx_lista_max = bytes;
            /* Nao desenhamos ainda, mas percorrer a lista e barato e responde a
               pergunta que dimensiona o rasterizador: quais comandos aparecem,
               quantas vezes, e para quais buffers. A primeira lista tambem vai
               inteira para arquivo, sublistas incluidas. */
            FILE* saida = NULL;
            static unsigned menu_dl_dumpadas;
            const char* dump_menu = getenv("WPJ2_DUMP_MENU_DL");
            int16_t estado_dl = (int16_t)rsp_rdram16(rdram, 0x001A7234u);
            int16_t subestado_dl = (int16_t)rsp_rdram16(rdram, 0x001A723Cu);
            if (dump_menu && *dump_menu && *dump_menu != '0' &&
                estado_dl == 11 && subestado_dl == 24 && menu_dl_dumpadas < 32u) {
                char nome[256];
                snprintf(nome, sizeof(nome), "%smenu_dl_%03u.txt", g_prefixo,
                         menu_dl_dumpadas++);
                saida = fopen(nome, "w");
            } else if (g_rsp_debug && g_gfx_listas == 0) {
                char nome[256];
                snprintf(nome, sizeof(nome), "%sdisplaylist.txt", g_prefixo);
                saida = fopen(nome, "w");
            } else if (g_rsp_debug && bytes >= 7000 && !g_gfx_lista_grande_dumpada) {
                char nome[256];
                snprintf(nome, sizeof(nome), "%scena_maior_displaylist.txt", g_prefixo);
                saida = fopen(nome, "w");
                g_gfx_lista_grande_dumpada = 1;
            }
            g_gfx_listas++;
            if (g_rsp_debug || saida) andar_dl(rdram, lista, 0, saida); /* conta/documenta */
            if (saida) fclose(saida);
            /* A matriz param=02 no nivel superior de 12/50 e a transformacao
             * raiz observada no Project64. Registrar 18 amostras locais
             * permite comparar Z/rotacao sem introduzir um estado forcado. */
            int16_t estado_antes = (int16_t)rsp_rdram16(rdram, 0x001A7234u);
            int16_t subestado_antes = (int16_t)rsp_rdram16(rdram, 0x001A723Cu);
            /* Contexto da tarefa para o CSV opt-in de alpha. A escrita fica
             * fora do prototipo e nunca ocorre sem WPJ2_ALPHA_TRACE. */
            g_alpha_trace_tarefa = g_gfx_listas;
            g_alpha_trace_estado = estado_antes;
            g_alpha_trace_subestado = subestado_antes;
            g_transicao_3d_limpou_tarefa = 0;
            if (estado_antes != 12 || subestado_antes != 50) {
                g_transicao_3d_iniciada = 0;
                g_transicao_mosaico_2d = 0;
                g_transicao_preto_ate = 0;
                g_transicao_apresentacao = 0;
            }
            g_rastrear_camera_12 = g_rsp_debug && estado_antes == 12 && subestado_antes == 50 &&
                                     g_camera_12_amostra < 18;
            int rastreando_camera = g_rastrear_camera_12;
            g_tarefa_lb_cargas = g_tarefa_lb_bytes = g_tarefa_lb_nao_zero = 0;
            g_tarefa_lb_primeira = g_tarefa_lb_ultima = 0;
            g_tarefa_oml_n = g_tarefa_oml_total = 0;
            g_tarefa_fog = g_tarefa_blend = 0;
            g_ultima_gfx_tem_triangulos = 0;
            g_tarefa_tri_recebidos = g_tarefa_tri_desenhados = 0;
            g_tarefa_tri_cull_frente = g_tarefa_tri_cull_tras = g_tarefa_tri_camera_recusados = 0;
            g_tarefa_z_aceitos = g_tarefa_z_recusados = 0;
            g_tarefa_tri_pixels = 0;
            g_texture_definida_na_tarefa = 0;
            LARGE_INTEGER raster_inicio, raster_fim, raster_freq;
            QueryPerformanceCounter(&raster_inicio);
            /* O backend alternativo recebe a mesma GBI/OSTask da ROM. Se a
             * DLL não existir ou rejeitar a lista, o rasterizador CPU continua
             * sendo fallback por tarefa e mantém o executável recuperável. */
            /* Fast-forward com frame skip real. F11 conserva uma imagem em
             * oito para ainda servir como navegacao visual. O replay F4 nao
             * precisa exibir o caminho: omite 100% da rasterizacao ate o poll
             * marcado. A ROM continua recebendo SP/DP para TODAS as tarefas;
             * portanto logica, audio e estado emulado seguem sendo executados. */
            int pular_grafico = 0;
            if (hle_replay_turbo_active()) {
                pular_grafico = 1;
            } else if (hle_navigation_turbo_active()) {
                pular_grafico = (g_gfx_fast_phase++ & 7u) != 0u;
            } else {
                g_gfx_fast_phase = 0;
            }
            int desenhou_rt64 = pular_grafico;
            if (pular_grafico) {
                g_gfx_fast_skipped++;
            } else {
                perf_timeline_mark("gfx_begin", (uint32_t)g_gfx_listas, bytes);
                int submit_result = rt64_backend_submit(
                    rdram, g_spmem, lista, bytes,
                    sp_word(TASK_OFFSET + 0x10),
                    sp_word(TASK_OFFSET + 0x18));
                perf_timeline_mark("gfx_end", (uint32_t)g_gfx_listas,
                                   (uint32_t)submit_result);
                desenhou_rt64 = submit_result != 0;
                if (!desenhou_rt64) desenhar_dl(rdram, lista, 0);
            }
            QueryPerformanceCounter(&raster_fim);
            if (g_cimg_addr && capturar_gfx_atual()) {
                char rot[96];
                snprintf(rot, sizeof(rot), "%scaptura_gfx_%llu_cimg_%06X",
                         g_prefixo, (unsigned long long)g_gfx_listas, g_cimg_addr);
                rsp_dump_alvo(rdram, rot, g_cimg_addr);
            }
            g_ultima_gfx_indice = g_gfx_listas;
            g_ultima_gfx_tri_recebidos = g_tarefa_tri_recebidos;
            g_ultima_gfx_tri_desenhados = g_tarefa_tri_desenhados;
            g_ultima_gfx_tri_cull_frente = g_tarefa_tri_cull_frente;
            g_ultima_gfx_tri_cull_tras = g_tarefa_tri_cull_tras;
            g_ultima_gfx_tri_camera_recusados = g_tarefa_tri_camera_recusados;
            g_ultima_gfx_z_aceitos = g_tarefa_z_aceitos;
            g_ultima_gfx_z_recusados = g_tarefa_z_recusados;
            g_ultima_gfx_tri_pixels = g_tarefa_tri_pixels;
            QueryPerformanceFrequency(&raster_freq);
            if (raster_freq.QuadPart > 0) {
                uint64_t us = (uint64_t)(((raster_fim.QuadPart - raster_inicio.QuadPart) *
                                          1000000ull) / raster_freq.QuadPart);
                g_gfx_raster_last_us = us;
                g_gfx_raster_total_us += us;
                g_gfx_raster_samples++;
                if (us > g_gfx_raster_peak_us) g_gfx_raster_peak_us = us;
                unsigned bucket = us < 1000u ? 0u : us < 2000u ? 1u :
                                  us < 4000u ? 2u : us < 8000u ? 3u :
                                  us < 16000u ? 4u : us < 33000u ? 5u : 6u;
                g_gfx_raster_buckets[bucket]++;
                const char* perf = getenv("WPJ2_RT64_PERF");
                if (perf && *perf && *perf != '0' && us >= 8000u &&
                    g_gfx_raster_slow_logged < 96u) {
                    printf("[gfx-slow] task=%llu dl=%08X bytes=%u ucode=%08X data=%08X custo=%.3fms\n",
                           (unsigned long long)g_gfx_listas, lista, bytes,
                           sp_word(TASK_OFFSET + 0x10),
                           sp_word(TASK_OFFSET + 0x18), (double)us / 1000.0);
                    fflush(stdout);
                    g_gfx_raster_slow_logged++;
                }
            }
            g_rastrear_camera_12 = 0;
            /* A contagem da camera e a mesma unidade observada no dump do
             * Project64. Fotografar tres marcos, em vez de uma tarefa fixa,
             * permite comparar o inicio, meio e fim da descida mesmo quando a
             * quantidade de tarefas de audio varia entre execucoes. */
            if (g_rsp_debug && rastreando_camera && g_cimg_addr &&
                (g_camera_12_amostra == 1 || g_camera_12_amostra == 8 ||
                 g_camera_12_amostra == 18)) {
                char rot[64];
                snprintf(rot, sizeof(rot), "%scamera12_amostra%u_tarefa%llu",
                         g_prefixo, g_camera_12_amostra,
                         (unsigned long long)g_gfx_listas);
                rsp_dump_alvo(rdram, rot, g_cimg_addr);
            }
            /* A apresentacao do preview ocorre depois do handler de retrace,
             * em recomp_poll(). Nesse momento a ROM ja pode ter trocado
             * VI_ORIGIN para o framebuffer que acabou de ficar pronto. */

            /* Fotografa o buffer no instante em que a lista acabou de ser
               desenhada. Despejar no fim da corrida pegava o quadro logo depois
               de uma limpeza e antes dos sprites do quadro seguinte - por isso
               o buffer aparecia preto mesmo com 1,2 milhao de texels escritos
               nele. O instante da medicao era o problema, nao o desenho. */
            if (g_rsp_debug && g_gfx_listas <= 6 && g_cimg_addr) {
                char rot[64];
                snprintf(rot, sizeof(rot), "%spos_tarefa%llu", g_prefixo,
                         (unsigned long long)g_gfx_listas);
                rsp_dump_alvo(rdram, rot, g_cimg_addr);
            }
            /* A rota 8/1 finalmente submete listas grandes de geometria. A
             * captura final pode ocorrer depois de uma limpeza 2D; registrar o
             * alvo imediatamente apos a primeira lista >= 9 KiB separa isso de
             * um erro de rasterizacao/camera. */
            if (g_rsp_debug && bytes >= 8500 && !g_cena_3d_fotografada && g_cimg_addr) {
                char rot[64];
                snprintf(rot, sizeof(rot), "%scena_3d_pos_tarefa%llu", g_prefixo,
                         (unsigned long long)g_gfx_listas);
                rsp_dump_alvo(rdram, rot, g_cimg_addr);
                g_cena_3d_fotografada = 1;
            }
            /* Estados observados no Project64 durante a abertura sem START:
             * 1/1 -> 8/1 -> 8/26 -> 12/50. Captura-los no fim do task, em vez
             * de inferir a cena por uma escrita manual de estado, preserva a
             * camera, os recursos e o timing que a ROM realmente escolheu. */
            int16_t estado = (int16_t)rsp_rdram16(rdram, 0x001A7234u);
            int16_t subestado = (int16_t)rsp_rdram16(rdram, 0x001A723Cu);
            int* ja_fotografado = NULL;
            const char* nome_estado = NULL;
            if (estado == 8 && subestado == 1) {
                ja_fotografado = &g_estado_8_1_fotografado;
                nome_estado = "estado_8_1";
            } else if (estado == 8 && subestado == 26) {
                ja_fotografado = &g_estado_8_26_fotografado;
                nome_estado = "estado_8_26";
            }
            if (g_rsp_debug && ja_fotografado && !*ja_fotografado && g_cimg_addr) {
                char rot[64];
                snprintf(rot, sizeof(rot), "%s%s_pos_tarefa%llu", g_prefixo,
                         nome_estado, (unsigned long long)g_gfx_listas);
                rsp_dump_alvo(rdram, rot, g_cimg_addr);
                *ja_fotografado = 1;
            }
            /* A descida 3D e os dialogos automaticos compartilham 12/50, mas
             * chegam em tasks diferentes. Espacar as fotos ao longo da fase
             * permite ver se o quadro some num ponto especifico da animacao. */
            if (g_rsp_debug && estado == 12 && subestado == 50 && g_cimg_addr) {
                if (g_estado_12_texturas < 8) {
                    unsigned taxa = g_tarefa_lb_bytes
                        ? (unsigned)((g_tarefa_lb_nao_zero * 100u) / g_tarefa_lb_bytes) : 0;
                    printf("  [tex12] tarefa=%llu cargas=%llu bytes=%llu nao_zero=%u%%"
                           " fonte=0x%08X..0x%08X\n",
                           (unsigned long long)g_gfx_listas,
                           (unsigned long long)g_tarefa_lb_cargas,
                           (unsigned long long)g_tarefa_lb_bytes, taxa,
                           g_tarefa_lb_primeira | 0x80000000u,
                           g_tarefa_lb_ultima | 0x80000000u);
                    fflush(stdout);
                    g_estado_12_texturas++;
                }
                /* Uma linha agregada por task preserva a cadencia; despejar
                 * cada opcode B9 mudaria justamente o timing observado. */
                if (g_estado_12_tarefas >= 38 && g_estado_12_tarefas < 50 &&
                    g_tarefa_oml_total) {
                    printf("  [rdp12] tarefa=%llu oml=%u", (unsigned long long)g_gfx_listas,
                           g_tarefa_oml_total);
                    for (unsigned mi = 0; mi < g_tarefa_oml_n; mi++)
                        printf(" %08X/%08X", g_tarefa_oml_w0[mi], g_tarefa_oml_w1[mi]);
                    printf(" fog=%08X blend=%08X\n", g_tarefa_fog, g_tarefa_blend);
                    fflush(stdout);
                }
                /* A imagem do oraculo aparece entre os antigos marcos 32 e
                 * 64. Amostrar quatro listas adicionais ali separa a entrada
                 * do tunel da plataforma/personagem posterior sem despejar
                 * todos os frames da cutscene. */
                static const unsigned pontos[] = { 1, 8, 16, 32, 40, 42, 44, 46,
                                                   48, 56, 64, 128, 256 };
                g_estado_12_tarefas++;
                if (g_estado_12_fotos < sizeof(pontos) / sizeof(pontos[0]) &&
                    g_estado_12_tarefas >= pontos[g_estado_12_fotos]) {
                    char rot[64];
                    snprintf(rot, sizeof(rot), "%sestado_12_50_%u_tarefa%llu",
                             g_prefixo, g_estado_12_fotos + 1,
                             (unsigned long long)g_gfx_listas);
                    rsp_dump_alvo(rdram, rot, g_cimg_addr);
                    g_estado_12_fotos++;
                }
            }
        }
        g_status |= ST_HALT | ST_BROKE;
        g_tasks++;
        g_por_tipo[type < TIPO_MAX ? type : 0]++;
        rsp_enqueue_task_done((int)type);
        if (g_tasks <= 4) {
            printf("  [rsp]  tarefa %llu (tipo %u) executada\n",
                   (unsigned long long)g_tasks, type);
            fflush(stdout);
        }
    }
    publish(rdram);
}


/* ------------------------------------------------------------------ */
/* Memoria do RSP e a OSTask que passa por ela                         */
/* ------------------------------------------------------------------ */

/* __osSpRawStartDma(s32 dir, void* spAddr, void* dramAddr, u32 size)
 *
 * Lido de func_800CD060 (ROM 0xCDC60):
 *   $a0 direcao   0 -> escreve SP_RD_LEN: RDRAM para a memoria do RSP
 *                 1 -> escreve SP_WR_LEN: memoria do RSP para a RDRAM
 *   $a1 spAddr    vai direto para SP_MEM_ADDR; bit 12 escolhe DMEM ou IMEM
 *   $a2 dramAddr  passa por osVirtualToPhysical
 *   $a3 size      escrito como size-1
 *
 * A libultra copia a OSTask para a DMEM em 0x04000FC0 antes de soltar o RSP.
 * Interceptar aqui e o unico jeito de ver a tarefa inteira: tipo, microcodigo,
 * lista de exibicao e buffers de saida. */
static uint64_t g_sp_dma = 0;
static int g_vistas[TIPO_MAX];        /* quantas ja foram mostradas, por tipo */

static uint32_t sp_word(uint32_t off) {
    return *(uint32_t*)(g_spmem + off);
}

/* ------------------------------------------------------------------ */
/* Comandos graficos: F3DEX                                            */
/* ------------------------------------------------------------------ */

/* Nomes dos opcodes que aparecem numa lista de exibicao F3DEX. Saber *quais*
   comandos este jogo usa - e nao quais existem - e o que dimensiona o
   rasterizador: implementar os 20 que aparecem e trabalho, implementar os 90 do
   padrao seria desperdicio. */
static const char* nome_gfx(uint8_t op) {
    switch (op) {
        case 0x00: return "SPNOOP";       case 0x01: return "MTX";
        case 0x03: return "MOVEMEM";      case 0x04: return "VTX";
        case 0x06: return "DL";           case 0xB1: return "TRI2";
        case 0xB2: return "MODIFYVTX";    case 0xB3: return "RDPHALF_2";
        case 0xB4: return "RDPHALF_1";    case 0xB5: return "QUAD";
        case 0xB6: return "CLEARGEOMODE"; case 0xB7: return "SETGEOMODE";
        case 0xB8: return "ENDDL";        case 0xB9: return "SETOTHERMODE_L";
        case 0xBA: return "SETOTHERMODE_H"; case 0xBB: return "TEXTURE";
        case 0xBC: return "MOVEWORD";     case 0xBD: return "POPMTX";
        case 0xBE: return "CULLDL";       case 0xBF: return "TRI1";
        case 0xE4: return "TEXRECT";      case 0xE5: return "TEXRECTFLIP";
        case 0xE6: return "RDPLOADSYNC";  case 0xE7: return "RDPPIPESYNC";
        case 0xE8: return "RDPTILESYNC";  case 0xE9: return "RDPFULLSYNC";
        case 0xED: return "SETSCISSOR";   case 0xEF: return "RDPSETOTHERMODE";
        case 0xF0: return "LOADTLUT";     case 0xF2: return "SETTILESIZE";
        case 0xF3: return "LOADBLOCK";    case 0xF4: return "LOADTILE";
        case 0xF5: return "SETTILE";      case 0xF6: return "FILLRECT";
        case 0xF7: return "SETFILLCOLOR"; case 0xF8: return "SETFOGCOLOR";
        case 0xF9: return "SETBLENDCOLOR"; case 0xFA: return "SETPRIMCOLOR";
        case 0xFB: return "SETENVCOLOR";  case 0xFC: return "SETCOMBINE";
        case 0xFD: return "SETTIMG";      case 0xFE: return "SETZIMG";
        case 0xFF: return "SETCIMG";      default:   return NULL;
    }
}

static uint64_t g_gfx_op[256];
static uint64_t g_gfx_cmds = 0, g_gfx_profundas = 0;

/* Enderecos em uma display list F3DEX nao sao sempre fisicos: 0x01000058
 * significa "offset 0x58 do segmento 1". O jogo configura os segmentos com
 * G_MOVEWORD antes de chamar as sublistas. Tratar esse endereco como fisico
 * fazia o interpretador pular exatamente as listas de sprites, pois 0x01000058
 * fica fora dos 8 MiB de RDRAM. */
static uint32_t g_segmento[16];
static uint64_t g_segmentos_definidos = 0;
static uint32_t g_num_luzes = 0;

static uint32_t resolver_endereco(uint32_t endereco) {
    uint32_t seg = endereco >> 24;
    uint32_t off = endereco & 0x00FFFFFFu;
    if (seg && seg < 16 && g_segmento[seg]) return g_segmento[seg] + off;
    return endereco & 0x1FFFFFFFu;
}

static void move_word(uint32_t w0, uint32_t w1) {
    /* Formato Fast3DEX: BC oo oo ii dddddddd. O indice fica no byte baixo e
     * o deslocamento em bits 8..23. Para G_MW_SEGMENT (0x06), off/4 e o
     * numero do segmento. Ex.: BC000406 001B20A0 define seg. 1. */
    uint32_t indice = w0 & 0xFFu;
    if (indice == 0x08u) { /* G_MW_FOG: fator/offset assinados em 8.8 */
        g_fog_multiplicador = (int16_t)(w1 >> 16);
        g_fog_offset = (int16_t)w1;
        return;
    }
    if (indice == 0x02u) { /* G_MW_NUMLIGHT do F3D/F3DEX */
        /* O microcodigo recebe 0x80000000 + ((N + 1) << 5), onde N e o
         * numero de luzes direcionais. A proxima entrada MOVEMEM e a luz
         * ambiente. Interpretar o byte baixo como sizeof(Light) transformava
         * o ambiente em uma terceira/quarta luz direcional. */
        if (w1 >= 0x80000020u) g_num_luzes = ((w1 - 0x80000000u) >> 5) - 1u;
        else g_num_luzes = 0;
        if (g_num_luzes > 7) g_num_luzes = 7;
        return;
    }
    if (indice != 0x06u) return;
    uint32_t seg = ((w0 >> 8) & 0xFFFFu) >> 2;
    if (seg >= 16) return;
    g_segmento[seg] = w1 & 0x00FFFFFFu;
    g_segmentos_definidos++;
}

/* Alvos de desenho distintos vistos em G_SETCIMG: quantos buffers o jogo usa. */
#define CIMG_MAX 8
static uint32_t g_cimg[CIMG_MAX];
static int g_cimg_n = 0;

static void anotar_cimg(uint32_t addr) {
    for (int i = 0; i < g_cimg_n; i++) if (g_cimg[i] == addr) return;
    if (g_cimg_n < CIMG_MAX) g_cimg[g_cimg_n++] = addr;
}

/* Percorre uma lista de exibicao contando opcodes, entrando nas sublistas.
   `saida` != NULL faz o dump completo em texto. */
static void andar_dl(uint8_t* rdram, uint32_t phys, int nivel, FILE* saida)
{
    if (nivel > 8) { g_gfx_profundas++; return; }
    for (uint32_t i = 0; i < 4096; i++) {
        uint32_t off = phys + i * 8;
        if (off + 8 > 0x800000u) return;
        uint32_t w0 = *(uint32_t*)(rdram + off);
        uint32_t w1 = *(uint32_t*)(rdram + off + 4);
        uint8_t op = (uint8_t)(w0 >> 24);
        g_gfx_op[op]++;
        g_gfx_cmds++;

        if (saida) {
            const char* n = nome_gfx(op);
            fprintf(saida, "%*s%04u  %-16s w0=%08X w1=%08X\n", nivel * 2, "",
                    i, n ? n : "???", w0, w1);
        }
        if (op == 0xBC) move_word(w0, w1);
        if (op == 0xFF) anotar_cimg(w1 & 0x1FFFFFFFu);
        if (op == 0xB8) return;                       /* ENDDL */
        if (op == 0x06) {                             /* DL: entra ou salta */
            uint32_t alvo = resolver_endereco(w1);
            if (alvo && alvo < 0x800000u) andar_dl(rdram, alvo, nivel + 1, saida);
            if ((w0 >> 16) & 0xFF) return;            /* branch, nao chamada */
        }
    }
}

/* ------------------------------------------------------------------ */
/* Rasterizador minimo                                                 */
/* ------------------------------------------------------------------ */

/* O jogo usa treze opcodes, e nesta fase so tres deles escrevem pixel:
 * SETCIMG diz onde desenhar, SETFILLCOLOR diz a cor, FILLRECT pinta o
 * retangulo. Nao ha VTX nem TRI1 - nenhum triangulo - entao um rasterizador
 * completo seria desproporcional. Estes tres produzem exatamente a imagem certa
 * para o que o jogo esta pedindo agora, que e limpar a tela.
 *
 * Quando aparecerem triangulos e texturas, isto vira insuficiente de forma
 * obvia: os comandos vao passar pelo `default` e o contador de ignorados sobe.
 */
static uint32_t g_fill = 0;
static uint32_t g_scis_x0 = 0, g_scis_y0 = 0, g_scis_x1 = 320, g_scis_y1 = 240;
static uint64_t g_rects = 0, g_pixels_pintados = 0, g_ignorados = 0;
static uint64_t g_texel_transparente = 0, g_texel_opaco = 0;
static uint64_t g_texel_colorido = 0;
static uint32_t g_texel_cor[65536];
/* Ultimo TEXRECT que pediu alpha de PRIMITIVE. A timeline compacta usa estes
 * dados para identificar a camada de transicao sem gravar listas completas. */
static uint64_t g_alpha_texrects = 0;
static uint32_t g_alpha_rect_x0 = 0, g_alpha_rect_y0 = 0;
static uint32_t g_alpha_rect_x1 = 0, g_alpha_rect_y1 = 0;

/* --- estado de textura ---
 *
 * O jogo desenha 3.952 TEXRECT e nenhum triangulo: e 2D puro, sprite por
 * sprite. Isso dispensa um RDP completo e pede um blitter - carregar o texel na
 * TMEM e copiar para o framebuffer. O que segue e so o suficiente para isso.
 */
#define TMEM_BYTES 0x1000
static uint8_t g_tmem[TMEM_BYTES];
static uint32_t g_timg_addr = 0, g_timg_fmt = 0, g_timg_siz = 0, g_timg_larg = 1;
/* A ENIX usa a paleta diretamente, mas a tela seguinte pode colorir uma
 * mascara IA/CI pelo combinador. Guardamos o estado RDP para descobrir qual
 * combinacao de textura/cor primaria ela pede antes de tentar emula-la. */
static uint32_t g_prim_color = 0xFFFFFFFFu;
static uint32_t g_env_color = 0xFFFFFFFFu;
static uint32_t g_combine_w0 = 0, g_combine_w1 = 0;
/* Parte baixa de SETOTHERMODE.  O tunel liga AA_EN; guardar o valor composto
 * (os comandos escrevem campos parciais) permite aplicar cobertura apenas
 * onde a ROM realmente a solicitou. */
static uint32_t g_othermode_l = 0;
/* Parte alta de SETOTHERMODE: CYCLETYPE esta nos bits 20..21. O caminho 2D
 * usa COPY em alguns mosaicos e esse modo tem borda de TEXRECT propria. */
static uint32_t g_othermode_h = 0;
/* G_MDSFT_TEXTPERSP fica no bit 19 de OtherMode_H. O RT64 nao escolhe a
 * interpolacao pela presenca de matriz: ele obedece ao estado emitido pela
 * display list e, em G_TP_NONE, aplica a compensacao 0,5 observada no RDP. */
#define G_MDSFT_TEXTPERSP 19u
#define G_TP_PERSP_BIT (1u << G_MDSFT_TEXTPERSP)
static uint64_t g_tex_persp_pixels = 0;
static uint64_t g_tex_affine_pixels = 0;
/* Cobertura 2x2 da cor-alvo atual. O RDP conserva cobertura entre triangulos;
 * sem isso duas metades de uma face deixam uma costura escura ao aplicar AA. */
static uint8_t g_aa_cobertura[320 * 240];
static uint16_t g_aa_amostras[320 * 240 * 4];
static uint32_t g_aa_amostras_rgb[320 * 240 * 4];
static uint8_t g_aa_amostras_hires[320 * 240];
static uint8_t g_aa_z_valida[320 * 240];
static uint16_t g_aa_z_amostras[320 * 240 * 4];
static uint32_t g_aa_cimg = UINT32_MAX;
static uint32_t g_aa_zimg = UINT32_MAX;
static int g_aa_experimental = -1;

/* O microcodigo pode manter G_FOG no modo geometrico enquanto uma sublista
 * troca o blender para desenhar um objeto de primeiro plano. Project64 so
 * habilita fog quando algum seletor A do blender referencia FOG (valor 3).
 * Na entrada 3D isto separa o tunel C811... do objeto/trono 0055..., que nao
 * deve receber novamente a mascara preta. */
static int fog_blender_ativo(void) {
    uint32_t blender = g_othermode_l >> 16;
    return g_fog_multiplicador > 0 &&
           (((blender >> 14) & 3u) == 3u || ((blender >> 12) & 3u) == 3u ||
            ((blender >>  6) & 3u) == 3u || ((blender >>  4) & 3u) == 3u);
}
/* A familia de render modes que a ROM usa para superficies translucidas tem
 * o primeiro ciclo de blender em 0x0050xxxx: IN * A_IN sobre MEM * (1-A_IN).
 * Isso e o "source-over" do RDP. A mesma assinatura aparece em legendas,
 * fundos em fade e sprites; nao depende de coordenadas ou da textura. */
static int rdp_blender_source_over(void) {
    return (g_othermode_l & 0x00F00000u) == 0x00500000u;
}
static uint32_t g_tex_filter = 0;       /* G_TF_POINT/BILERP */
/* Lente de comparacao para a cena 3D: -1 preserva o comando da ROM; 0 e 2
 * permitem separar point sampling e bilerp sem alterar o prototipo 2D. */
static int g_tex_filter_forcado = -2;
static int g_tex_filter_3d_forcado = -2;
/* O combinador generico ainda e experimental. O caminho historico
 * TEXEL0*SHADE permanece padrao ate cada modo usado pela cena ser validado
 * contra o Project64. */
/* -1 = modo historico; 1 = combinador RDP generico restrito ao 3D 12/50.
 * Mantemos a abertura no caminho validado enquanto comparamos materiais. */
static int g_rdp_combine_mode = -1;

typedef struct {
    uint32_t fmt, siz, linha, tmem, paleta;
    uint32_t cmt, maskt, shiftt, cms, masks, shifts;
    uint32_t uls, ult, lrs, lrt;          /* cantos do tile, em 10.2 */
} tile_t;
static tile_t g_tile[8];

/* O F3DEX desta ROM guarda vertices ja no espaco ortografico do HUD. Os
 * triangulos da abertura usam uma malha de quads para logos, enquanto os
 * TEXRECTs anteriores desenham apenas o fundo. Nesta etapa preservamos os
 * atributos que chegam do microcodigo e aplicamos a projecao fixa do viewport;
 * a pilha de matrizes completa fica para as cenas 3D, que ainda nao aparecem
 * neste boot. */
typedef struct {
    /* Vtx do F3DEX: x, y, z, flag, s, t, cor/normal. Manter Z separado
       e indispensavel para a primeira cena 3D; trata-la como zero achatava
       toda a geometria no mesmo plano antes da projecao. */
    int16_t x, y, z, s, t;
    uint8_t r, g, b, a;
    float cx, cy, cz, cw;          /* espaco de recorte homogeneo */
    float sx, sy, sz, invw, fog;   /* viewport, 1/W e fator de neblina */
    uint8_t usa_matriz;
    uint8_t descartado_camera;
    uint8_t valido;
} f3d_vertex_t;
static f3d_vertex_t g_vtx[64];
static f3d_vertex_t recortar_lerp(const f3d_vertex_t* a, const f3d_vertex_t* b, float t);
static uint64_t g_triangulos = 0, g_triangulo_pixels = 0;

/* Estado minimo do transformador F3DEX. A ROM fornece matrizes 16.16 em
 * G_MTX e o viewport em MOVEMEM. Guardar esse estado mesmo com o modo desligado
 * permite validar primeiro os resultados sem mudar o caminho ja conhecido. */
static float g_mtx_projection[4][4], g_mtx_model[10][4][4], g_mtx_combined[4][4];
static int g_mtx_model_i = 0, g_mtx_pronto = 0, g_mtx_sujo = 1;
static float g_vscale_x = 160.0f, g_vscale_y = 120.0f, g_vscale_z = 511.0f;
static float g_vtrans_x = 160.0f, g_vtrans_y = 120.0f, g_vtrans_z = 511.0f;
static int g_f3d_matrix_mode = 1;
static int g_f3d_z_mode = 1;
static int g_f3d_cull_mode = 1;
static int g_f3d_w_clip = -1;
static int g_f3d_matrix_conventional = 0;
static uint64_t g_z_aceitos = 0, g_z_recusados = 0;
static uint64_t g_tri_cull_frente = 0, g_tri_cull_tras = 0;
static uint64_t g_mtx_vertices = 0, g_mtx_cw_nulo = 0, g_mtx_atras = 0, g_mtx_fora_view = 0;
static float g_mtx_sx_min = 1.0e30f, g_mtx_sx_max = -1.0e30f;
static float g_mtx_sy_min = 1.0e30f, g_mtx_sy_max = -1.0e30f;
/* G_TEXTURE escolhe qual dos oito tiles a geometria triangulada consulta.
 * TEXRECT ja traz o tile no proprio comando, mas TRI1 depende deste estado. */
static uint8_t g_texture_tile = 0;
static uint8_t g_texture_ligada = 0;
/* Alguns lotes 2D partem do estado inicial da nova OSTask e nao repetem
 * gSPTexture(G_ON). Um OFF emitido pelo quad de realce nao pode contaminar a
 * tarefa seguinte: isso deixa a imagem estagnada enquanto logica, VI e audio
 * continuam. Dentro da mesma tarefa, contudo, ON/OFF deve valer normalmente. */
/* G_TEXTURE leva duas escalas 16.16. Os vertices continuam em 5.10, logo a
 * conversao para a coordenada que chega ao tile e feita por triangulo. */
static uint16_t g_texture_scale_s = 0xFFFFu, g_texture_scale_t = 0xFFFFu;
static uint32_t g_geom_mode = 0;

uint64_t rsp_camera_discarded_vertices(void) { return g_mtx_cw_nulo + g_mtx_atras; }
uint64_t rsp_culled_triangles(void) { return g_tri_cull_frente + g_tri_cull_tras; }
uint32_t rsp_prim_color(void) { return g_prim_color; }
uint32_t rsp_env_color(void) { return g_env_color; }
uint32_t rsp_othermode_l(void) { return g_othermode_l; }
int rsp_last_gfx_had_triangles(void) { return g_ultima_gfx_tem_triangulos; }
uint64_t rsp_last_gfx_index(void) { return g_ultima_gfx_indice; }
uint32_t rsp_last_gfx_tri_received(void) { return g_ultima_gfx_tri_recebidos; }
uint32_t rsp_last_gfx_tri_drawn(void) { return g_ultima_gfx_tri_desenhados; }
uint32_t rsp_last_gfx_tri_cull_front(void) { return g_ultima_gfx_tri_cull_frente; }
uint32_t rsp_last_gfx_tri_cull_back(void) { return g_ultima_gfx_tri_cull_tras; }
uint32_t rsp_last_gfx_tri_camera_rejected(void) { return g_ultima_gfx_tri_camera_recusados; }
uint64_t rsp_last_gfx_z_accepted(void) { return g_ultima_gfx_z_aceitos; }
uint64_t rsp_last_gfx_z_rejected(void) { return g_ultima_gfx_z_recusados; }
int rsp_transition_presentation_mode(void) { return g_transicao_apresentacao; }
int rsp_coverage_frame_2x(uint8_t* rdram, uint32_t origin,
                          uint32_t width, uint32_t height,
                          uint32_t* rgb_out) {
    if (!rdram || !rgb_out || width < 320u || height > 240u ||
        g_aa_cimg != origin)
        return 0;
    int algum_hires = 0;
    for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < 320u; x++) {
        uint32_t ci = y * 320u + x;
        uint32_t base_rgb;
        int usar_hires = g_aa_cobertura[ci] && g_aa_amostras_hires[ci];
        if (!usar_hires) {
            uint16_t p = *(uint16_t*)(rdram + ((origin + (y * width + x) * 2u) ^ 2u));
            uint32_t r = ((p >> 11) & 31u) * 255u / 31u;
            uint32_t g = ((p >> 6) & 31u) * 255u / 31u;
            uint32_t b = ((p >> 1) & 31u) * 255u / 31u;
            base_rgb = (r << 16) | (g << 8) | b;
        } else {
            base_rgb = 0;
            algum_hires = 1;
        }
        uint32_t* a = &g_aa_amostras_rgb[ci * 4u];
        uint32_t linha0 = (y * 2u) * 640u + x * 2u;
        uint32_t linha1 = linha0 + 640u;
        rgb_out[linha0] = usar_hires ? a[0] : base_rgb;
        rgb_out[linha0 + 1u] = usar_hires ? a[1] : base_rgb;
        rgb_out[linha1] = usar_hires ? a[2] : base_rgb;
        rgb_out[linha1 + 1u] = usar_hires ? a[3] : base_rgb;
    }
    for (uint32_t y = height * 2u; y < 480u; y++)
        memset(rgb_out + y * 640u, 0, 640u * sizeof(uint32_t));
    return algum_hires;
}
uint64_t rsp_alpha_texrects(void) { return g_alpha_texrects; }
uint32_t rsp_alpha_rect_x0(void) { return g_alpha_rect_x0; }
uint32_t rsp_alpha_rect_y0(void) { return g_alpha_rect_y0; }
uint32_t rsp_alpha_rect_x1(void) { return g_alpha_rect_x1; }
uint32_t rsp_alpha_rect_y1(void) { return g_alpha_rect_y1; }

static void alpha_trace_texrect(uint32_t tile, const tile_t* t,
                                uint32_t ulx, uint32_t uly, uint32_t lrx, uint32_t lry) {
    if (!g_alpha_trace_inicializado) {
        const char* caminho = getenv("WPJ2_ALPHA_TRACE");
        g_alpha_trace_inicializado = 1;
        if (caminho && *caminho) {
            g_alpha_trace = fopen(caminho, "w");
            if (g_alpha_trace) {
                fprintf(g_alpha_trace,
                        "gfx,estado,subestado,prim,env,othermode,combine0,combine1,"
                        "tile,fmt,siz,linha,timg,ulx,uly,lrx,lry");
                fputc(10, g_alpha_trace);
            }
        }
    }
    if (!g_alpha_trace) return;
    fprintf(g_alpha_trace,
            "%llu,%d,%d,%08X,%08X,%08X,%08X,%08X,%u,%u,%u,%u,%06X,%u,%u,%u,%u",
            (unsigned long long)g_alpha_trace_tarefa,
            g_alpha_trace_estado, g_alpha_trace_subestado,
            g_prim_color, g_env_color, g_othermode_l, g_combine_w0, g_combine_w1,
            tile & 7u, t->fmt, t->siz, t->linha, g_timg_addr,
            ulx, uly, lrx, lry);
    fputc(10, g_alpha_trace);
}

static void alpha_trace_triangulo(const f3d_vertex_t* a, const f3d_vertex_t* b,
                                  const f3d_vertex_t* c) {
    if (g_alpha_trace_estado != 12 || g_alpha_trace_subestado != 50 ||
        g_tri_alpha_trace_linhas >= 12000u)
        return;
    unsigned amin = a->a;
    if (b->a < amin) amin = b->a;
    if (c->a < amin) amin = c->a;
    unsigned amax = a->a;
    if (b->a > amax) amax = b->a;
    if (c->a > amax) amax = c->a;
    if (g_tri_alpha_trace_gfx_min < 0) {
        const char* e = getenv("WPJ2_TRI_ALPHA_GFX_MIN");
        g_tri_alpha_trace_gfx_min = e ? atoi(e) : 0;
        if (g_tri_alpha_trace_gfx_min < 0) g_tri_alpha_trace_gfx_min = 0;
        e = getenv("WPJ2_TRI_ALPHA_GFX_MAX");
        g_tri_alpha_trace_gfx_max = e ? atoi(e) : INT_MAX;
        if (g_tri_alpha_trace_gfx_max < g_tri_alpha_trace_gfx_min)
            g_tri_alpha_trace_gfx_max = g_tri_alpha_trace_gfx_min;
        e = getenv("WPJ2_TRI_ALPHA_ALL");
        g_tri_alpha_trace_todos = e && atoi(e) != 0;
    }
    /* A entrada do personagem possui milhares de TRI1 normais; registrar
     * apenas a janela tardia e materiais cujo alfa realmente varia evita que
     * ela esgote a sonda antes do fade da caixa. */
    if (g_alpha_trace_tarefa < (uint64_t)g_tri_alpha_trace_gfx_min ||
        g_alpha_trace_tarefa > (uint64_t)g_tri_alpha_trace_gfx_max ||
        (!g_tri_alpha_trace_todos &&
         ((g_prim_color & 0xFFu) == 255u && amin == 255u && amax == 255u)))
        return;
    if (!g_tri_alpha_trace_inicializado) {
        const char* caminho = getenv("WPJ2_TRI_ALPHA_TRACE");
        g_tri_alpha_trace_inicializado = 1;
        if (caminho && *caminho) {
            g_tri_alpha_trace = fopen(caminho, "w");
            if (g_tri_alpha_trace)
                fputs("gfx,prim,othermode,combine0,combine1,on,tile,timg,fmt,siz,linha,amina,amaxa,"
                      "minx,miny,maxx,maxy\n", g_tri_alpha_trace);
        }
    }
    if (!g_tri_alpha_trace) return;
    const tile_t* t = &g_tile[g_texture_tile & 7u];
    /* Uma linha por material em cada display list. A versao por triangulo
     * esgotava o limite na primeira malha do personagem e nunca chegava ao
     * fade tardio da caixa. */
    if (g_tri_alpha_trace_gfx_atual != g_alpha_trace_tarefa) {
        g_tri_alpha_trace_gfx_atual = g_alpha_trace_tarefa;
        g_tri_alpha_trace_chaves_n = 0;
    }
    tri_alpha_chave_t chave = { g_prim_color, g_othermode_l, g_combine_w0,
        g_combine_w1, g_timg_addr, g_texture_ligada, g_texture_tile & 7u,
        t->fmt, t->siz, t->linha };
    for (unsigned i = 0; i < g_tri_alpha_trace_chaves_n; i++)
        if (!memcmp(&g_tri_alpha_trace_chaves[i], &chave, sizeof(chave))) return;
    if (g_tri_alpha_trace_chaves_n < sizeof(g_tri_alpha_trace_chaves) / sizeof(g_tri_alpha_trace_chaves[0]))
        g_tri_alpha_trace_chaves[g_tri_alpha_trace_chaves_n++] = chave;
    float minx = a->sx, maxx = a->sx, miny = a->sy, maxy = a->sy;
#define TRI_EXTREMO(v) do { if ((v)->sx < minx) minx = (v)->sx; if ((v)->sx > maxx) maxx = (v)->sx; \
                             if ((v)->sy < miny) miny = (v)->sy; if ((v)->sy > maxy) maxy = (v)->sy; } while (0)
    TRI_EXTREMO(b); TRI_EXTREMO(c);
#undef TRI_EXTREMO
    fprintf(g_tri_alpha_trace,
            "%llu,%08X,%08X,%08X,%08X,%u,%u,%06X,%u,%u,%u,%u,%u,%.1f,%.1f,%.1f,%.1f\n",
            (unsigned long long)g_alpha_trace_tarefa, g_prim_color, g_othermode_l,
            g_combine_w0, g_combine_w1, g_texture_ligada, g_texture_tile & 7u,
            g_timg_addr, t->fmt, t->siz, t->linha, amin, amax, minx, miny, maxx, maxy);
    g_tri_alpha_trace_linhas++;
    if ((g_tri_alpha_trace_linhas & 31u) == 0) fflush(g_tri_alpha_trace);
}

static void trace_texrect_abertura(uint32_t tile, const tile_t* t,
                                   uint32_t ulx, uint32_t uly, uint32_t lrx, uint32_t lry,
                                   int32_t s0, int32_t t0, int32_t dsdx, int32_t dtdy) {
    if (!g_texrect_trace_estado_lido) {
        const char* alvo = getenv("WPJ2_TEXRECT_TRACE_STATE");
        g_texrect_trace_estado = 8;
        g_texrect_trace_subestado = 1;
        if (alvo && *alvo) {
            int estado, subestado;
            if (sscanf(alvo, "%d/%d", &estado, &subestado) == 2) {
                g_texrect_trace_estado = estado;
                g_texrect_trace_subestado = subestado;
            }
        }
        g_texrect_trace_gfx_min = 0;
        g_texrect_trace_gfx_max = INT_MAX;
        const char* min = getenv("WPJ2_TEXRECT_TRACE_GFX_MIN");
        const char* max = getenv("WPJ2_TEXRECT_TRACE_GFX_MAX");
        if (min && *min) g_texrect_trace_gfx_min = atoi(min);
        if (max && *max) g_texrect_trace_gfx_max = atoi(max);
        g_texrect_trace_estado_lido = 1;
    }
    if (g_alpha_trace_estado != g_texrect_trace_estado ||
        g_alpha_trace_subestado != g_texrect_trace_subestado ||
        g_alpha_trace_tarefa < (uint64_t)g_texrect_trace_gfx_min ||
        g_alpha_trace_tarefa > (uint64_t)g_texrect_trace_gfx_max ||
        g_texrect_trace_linhas >= 4096u)
        return;
    if (!g_texrect_trace_inicializado) {
        const char* caminho = getenv("WPJ2_TEXRECT_TRACE");
        g_texrect_trace_inicializado = 1;
        if (caminho && *caminho) {
            g_texrect_trace = fopen(caminho, "w");
            if (g_texrect_trace) {
                fputs("gfx,cimg,timg,tile,fmt,siz,linha,tmem,cmt,maskt,cms,masks,uls,ult,lrs,lrt,filter,prim,othermode,"
                      "ulx,uly,lrx,lry,s0,t0,dsdx,dtdy\n", g_texrect_trace);
            }
        }
    }
    if (!g_texrect_trace) return;
    fprintf(g_texrect_trace,
            "%llu,%06X,%06X,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%08X,%08X,%u,%u,%u,%u,%d,%d,%d,%d\n",
            (unsigned long long)g_alpha_trace_tarefa, g_cimg_addr, g_timg_addr, tile & 7u,
            t->fmt, t->siz, t->linha, t->tmem, t->cmt, t->maskt, t->cms, t->masks,
            t->uls, t->ult, t->lrs, t->lrt,
            g_tex_filter, g_prim_color, g_othermode_l, ulx, uly, lrx, lry,
            s0, t0, dsdx, dtdy);
    g_texrect_trace_linhas++;
    fflush(g_texrect_trace);
}

typedef struct {
    uint8_t r, g, b;
    int8_t x, y, z;
    uint8_t valido;
} f3d_light_t;
static f3d_light_t g_luz[8];
static uint64_t g_luzes_carregadas = 0, g_vertices_iluminados = 0;
void rsp_set_f3d_matrix_mode(int enabled) { g_f3d_matrix_mode = enabled; }
void rsp_set_f3d_z_mode(int enabled) { g_f3d_z_mode = enabled; }
void rsp_set_f3d_cull_mode(int enabled) { g_f3d_cull_mode = enabled; }
void rsp_set_tmem_interleave(int enabled) { g_tmem_interleave = enabled != 0; }
void rsp_set_rdp_combine_mode(int enabled) { g_rdp_combine_mode = enabled != 0; }
void rsp_set_debug(int enabled) { g_rsp_debug = enabled != 0; }
void rsp_set_f3d_matrix_conventional(int enabled) { g_f3d_matrix_conventional = enabled; }

/* Destino real de cada sprite, contado no momento do desenho. */
static struct {
    uint32_t addr;
    uint64_t texrects, texels;
    uint64_t triangulos, pixels_triangulos;
} g_destino[CIMG_MAX];

static void anotar_triangulo_destino(uint64_t pixels) {
    for (int k = 0; k < CIMG_MAX; k++) {
        if (g_destino[k].addr == g_cimg_addr) {
            g_destino[k].triangulos++;
            g_destino[k].pixels_triangulos += pixels;
            return;
        }
        if (g_destino[k].addr == 0) {
            g_destino[k].addr = g_cimg_addr;
            g_destino[k].triangulos = 1;
            g_destino[k].pixels_triangulos = pixels;
            return;
        }
    }
}

static uint64_t g_texrects = 0, g_texels = 0;
static uint32_t g_vtx_amostras = 0, g_tri_amostras = 0;
static uint32_t g_mtx_amostras = 0, g_movemem_amostras = 0;

/* TRI1 pode trocar a imagem entre quads; a amostra textual inicial pega so a
 * logo ENIX. Esta tabela retem todas as fontes para identificar a proxima tela
 * sem despejar cada triangulo. */
#define TRI_SOURCE_MAX 32
typedef struct {
    uint32_t addr, fmt, siz, tile_fmt, tile_siz, linha, lrs, lrt;
    uint32_t prim_color, combine_w0, combine_w1;
    uint64_t tris;
    int16_t min_s, max_s, min_t, max_t;
    uint8_t min_r, max_r, min_g, max_g, min_b, max_b, min_a, max_a;
} tri_source_t;
static tri_source_t g_tri_source[TRI_SOURCE_MAX];
static int g_tri_source_n = 0;

static void registrar_tri_source(const tile_t* t, const f3d_vertex_t* a,
                                 const f3d_vertex_t* b, const f3d_vertex_t* c) {
    int i;
    for (i = 0; i < g_tri_source_n; i++)
        if (g_tri_source[i].addr == g_timg_addr && g_tri_source[i].tile_fmt == t->fmt &&
            g_tri_source[i].tile_siz == t->siz && g_tri_source[i].linha == t->linha &&
            g_tri_source[i].prim_color == g_prim_color &&
            g_tri_source[i].combine_w0 == g_combine_w0 &&
            g_tri_source[i].combine_w1 == g_combine_w1) break;
    if (i == g_tri_source_n) {
        if (i >= TRI_SOURCE_MAX) return;
        g_tri_source[i] = (tri_source_t){ g_timg_addr, g_timg_fmt, g_timg_siz,
            t->fmt, t->siz, t->linha, t->lrs, t->lrt,
            g_prim_color, g_combine_w0, g_combine_w1, 0,
            a->s, a->s, a->t, a->t,
            a->r, a->r, a->g, a->g, a->b, a->b, a->a, a->a };
        g_tri_source_n++;
    }
    const f3d_vertex_t* v[3] = { a, b, c };
    for (int k = 0; k < 3; k++) {
        if (v[k]->s < g_tri_source[i].min_s) g_tri_source[i].min_s = v[k]->s;
        if (v[k]->s > g_tri_source[i].max_s) g_tri_source[i].max_s = v[k]->s;
        if (v[k]->t < g_tri_source[i].min_t) g_tri_source[i].min_t = v[k]->t;
        if (v[k]->t > g_tri_source[i].max_t) g_tri_source[i].max_t = v[k]->t;
        if (v[k]->r < g_tri_source[i].min_r) g_tri_source[i].min_r = v[k]->r;
        if (v[k]->r > g_tri_source[i].max_r) g_tri_source[i].max_r = v[k]->r;
        if (v[k]->g < g_tri_source[i].min_g) g_tri_source[i].min_g = v[k]->g;
        if (v[k]->g > g_tri_source[i].max_g) g_tri_source[i].max_g = v[k]->g;
        if (v[k]->b < g_tri_source[i].min_b) g_tri_source[i].min_b = v[k]->b;
        if (v[k]->b > g_tri_source[i].max_b) g_tri_source[i].max_b = v[k]->b;
        if (v[k]->a < g_tri_source[i].min_a) g_tri_source[i].min_a = v[k]->a;
        if (v[k]->a > g_tri_source[i].max_a) g_tri_source[i].max_a = v[k]->a;
    }
    g_tri_source[i].tris++;
}
static uint64_t g_fmt_visto[8][4];        /* [fmt][siz], para dimensionar */
static uint64_t g_fmt_sem_suporte = 0;
static uint64_t g_fora_do_tile = 0;       /* coordenada negativa apos o offset */
static uint64_t g_indice_zero = 0;        /* texel caiu na entrada 0 da paleta */

uint64_t rsp_rects(void)     { return g_rects; }
uint64_t rsp_pixels(void)    { return g_pixels_pintados; }
uint64_t rsp_ignorados(void) { return g_ignorados; }
uint64_t rsp_texrects(void)  { return g_texrects; }
uint64_t rsp_texels(void)    { return g_texels; }

static void aa_preparar_alvo(void) {
    if (g_aa_cimg == g_cimg_addr && g_aa_zimg == g_zimg_addr) return;
    memset(g_aa_cobertura, 0, sizeof(g_aa_cobertura));
    memset(g_aa_amostras_hires, 0, sizeof(g_aa_amostras_hires));
    memset(g_aa_z_valida, 0, sizeof(g_aa_z_valida));
    g_aa_cimg = g_cimg_addr;
    g_aa_zimg = g_zimg_addr;
}

static int aa_habilitado(void) {
    if (g_aa_experimental < 0) {
        const char* e = getenv("WPJ2_RDP_AA");
        /* A cobertura 2x2 conserva as quatro cores por pixel, portanto faces
         * adjacentes nao deixam mais as costuras pretas da versao antiga.
         * Continua obedecendo AA_EN da ROM e pode ser desligada para A/B. */
        g_aa_experimental = !e || atoi(e) != 0;
    }
    return g_aa_experimental;
}

static void trace_transicao_fill(uint8_t* rdram, uint32_t ulx, uint32_t uly,
                                 uint32_t lrx, uint32_t lry) {
    if (g_alpha_trace_estado != 12 || g_alpha_trace_subestado != 50) return;
    if (!g_transicao_trace_inicializado) {
        const char* caminho = getenv("WPJ2_TRANSITION_TRACE");
        g_transicao_trace_inicializado = 1;
        if (caminho && *caminho) {
            g_transicao_trace = fopen(caminho, "w");
            if (g_transicao_trace) {
                fprintf(g_transicao_trace,
                        "gfx,cimg,largura,siz,fill,ulx,uly,lrx,lry,scx0,scy0,scx1,scy1,p00,pcentro,pfim");
                fputc(10, g_transicao_trace);
            }
        }
    }
    if (!g_transicao_trace || !g_cimg_addr || g_cimg_larg < 320u) return;
    uint32_t amostras[3] = { 0u, 120u * g_cimg_larg + 160u, 239u * g_cimg_larg + 319u };
    uint16_t p[3];
    for (int i = 0; i < 3; i++)
        p[i] = *(uint16_t*)(rdram + ((g_cimg_addr + amostras[i] * 2u) ^ 2u));
    fprintf(g_transicao_trace,
            "%llu,%06X,%u,%u,%08X,%u,%u,%u,%u,%u,%u,%u,%u,%04X,%04X,%04X",
            (unsigned long long)g_alpha_trace_tarefa, g_cimg_addr, g_cimg_larg,
            g_cimg_siz, g_fill, ulx, uly, lrx, lry, g_scis_x0, g_scis_y0,
            g_scis_x1, g_scis_y1, p[0], p[1], p[2]);
    fputc(10, g_transicao_trace);
    fflush(g_transicao_trace);
}

static void pintar_retangulo(uint8_t* rdram, uint32_t ulx, uint32_t uly,
                             uint32_t lrx, uint32_t lry) {
    if (!g_cimg_addr || g_cimg_siz != 2) { g_ignorados++; return; }
    if (ulx < g_scis_x0) ulx = g_scis_x0;
    if (uly < g_scis_y0) uly = g_scis_y0;
    if (lrx > g_scis_x1) lrx = g_scis_x1;
    if (lry > g_scis_y1) lry = g_scis_y1;
    if (lrx <= ulx || lry <= uly) return;
    aa_preparar_alvo();

    /* Em 16 bits, SETFILLCOLOR traz dois pixels empacotados; para retangulo
       cheio os dois sao iguais, entao a metade baixa basta. */
    uint16_t cor = (uint16_t)(g_fill & 0xFFFF);
    for (uint32_t y = uly; y < lry; y++) {
        for (uint32_t x = ulx; x < lrx; x++) {
            uint32_t off = g_cimg_addr + (y * g_cimg_larg + x) * 2;
            if (off + 2 > 0x800000u) return;
            *(uint16_t*)(rdram + (off ^ 2)) = cor;   /* `^2`: ordem da RDRAM */
            if (x < 320u && y < 240u) {
                uint32_t ci = y * 320u + x;
                g_aa_cobertura[ci] = 0;
                g_aa_amostras_hires[ci] = 0;
                g_aa_z_valida[ci] = 0;
            }
            g_pixels_pintados++;
        }
    }
    g_rects++;
    trace_transicao_fill(rdram, ulx, uly, lrx, lry);
}

/* O RDP do N64 continua trabalhando enquanto a CPU envia a lista seguinte.
 * Nesta implementação os dois lotes acabam sincronicamente, de modo que um
 * triângulo 3D podia herdar a cidade 2D que ainda era o framebuffer frontal.
 * Ao cruzar a fronteira detectada pelos próprios TRI, começa-se o alvo em
 * RGBA5551 preto opaco; é o mesmo resultado do FILLRECT da ROM, no instante
 * em que o hardware o teria tornado visível. */
static void limpar_alvo_entrada_3d(uint8_t* rdram) {
    if (!g_cimg_addr || g_cimg_siz != 2 || g_cimg_larg < 320u) return;
    for (uint32_t y = 0; y < 240u; y++) {
        uint32_t linha = g_cimg_addr + y * g_cimg_larg * 2u;
        if (linha + 320u * 2u > 0x800000u) return;
        for (uint32_t x = 0; x < 320u; x++)
            *(uint16_t*)(rdram + ((linha + x * 2u) ^ 2u)) = 0x0001u;
    }
    aa_preparar_alvo();
    memset(g_aa_cobertura, 0, sizeof(g_aa_cobertura));
    memset(g_aa_amostras_hires, 0, sizeof(g_aa_amostras_hires));
    memset(g_aa_z_valida, 0, sizeof(g_aa_z_valida));
}

/* Le da RDRAM respeitando a troca de bytes por palavra. */
static uint8_t ram8(uint8_t* rdram, uint32_t off)  { return rdram[off ^ 3]; }
static uint16_t ram16(uint8_t* rdram, uint32_t off) {
    return *(uint16_t*)(rdram + (off ^ 2));
}

static void mtx_preparar(void);

/* F3DEX deposita Light em MOVEMEM 0x86, 0x88, ...: RGB em 0..2 e vetor
 * direcional assinado em 8..10. A cutscene usa tres luzes direcionais e uma
 * entrada ambiente implícita. */
static void f3d_carregar_luz(uint8_t* rdram, uint32_t indice, uint32_t src) {
    if (indice < 0x86u || (indice & 1u)) return;
    uint32_t n = (indice - 0x86u) >> 1;
    if (n >= 8 || src + 11 >= 0x800000u) return;
    g_luz[n].r = ram8(rdram, src);
    g_luz[n].g = ram8(rdram, src + 1);
    g_luz[n].b = ram8(rdram, src + 2);
    g_luz[n].x = (int8_t)ram8(rdram, src + 8);
    g_luz[n].y = (int8_t)ram8(rdram, src + 9);
    g_luz[n].z = (int8_t)ram8(rdram, src + 10);
    g_luz[n].valido = 1;
    g_luzes_carregadas++;
}

/* Nos vertices iluminados, os quatro bytes finais sao normal XYZ e alpha,
 * nao RGB. A aproximacao Lambertiana deixa a cena legivel e respeita as cores
 * definidas pelo jogo, sem substituir o combinador RDP completo. */
static void f3d_iluminar_vertice(f3d_vertex_t* v) {
    if (!(g_geom_mode & 0x00020000u)) return; /* G_LIGHTING */
    float nx = (float)(int8_t)v->r, ny = (float)(int8_t)v->g, nz = (float)(int8_t)v->b;
    float norm = nx * nx + ny * ny + nz * nz;
    if (norm < 1.0f) return;
    float inv = 1.0f / (float)sqrt(norm);
    nx *= inv; ny *= inv; nz *= inv;
    uint32_t limite = g_num_luzes;
    if (limite > 7) limite = 7;
    /* Light[N] e a luz ambiente da libultra; ela nao tem direcao. */
    float rr = 0.0f, gg = 0.0f, bb = 0.0f;
    if (g_luz[limite].valido) {
        rr = (float)g_luz[limite].r;
        gg = (float)g_luz[limite].g;
        bb = (float)g_luz[limite].b;
    }
    for (uint32_t i = 0; i < limite; i++) {
        if (!g_luz[i].valido) continue;
        float lx = (float)g_luz[i].x, ly = (float)g_luz[i].y, lz = (float)g_luz[i].z;
        /* Fast3D leva a direcao ao espaco da normal pelo inverso da
         * modelview. Esta e a mesma operacao de gSPUpdateLightVectors no
         * GLideN64 para a convencao de matriz usada por este runtime. */
        mtx_preparar();
        const float (*m)[4] = g_mtx_model[g_mtx_model_i];
        float tx = m[0][0] * lx + m[0][1] * ly + m[0][2] * lz;
        float ty = m[1][0] * lx + m[1][1] * ly + m[1][2] * lz;
        float tz = m[2][0] * lx + m[2][1] * ly + m[2][2] * lz;
        lx = tx; ly = ty; lz = tz;
        float ln = lx * lx + ly * ly + lz * lz;
        if (ln < 1.0f) continue;
        float dot = (nx * lx + ny * ly + nz * lz) / (float)sqrt(ln);
        if (dot <= 0.0f) continue;
        rr += (float)g_luz[i].r * dot;
        gg += (float)g_luz[i].g * dot;
        bb += (float)g_luz[i].b * dot;
    }
    v->r = (uint8_t)(rr > 255.0f ? 255 : rr);
    v->g = (uint8_t)(gg > 255.0f ? 255 : gg);
    v->b = (uint8_t)(bb > 255.0f ? 255 : bb);
    g_vertices_iluminados++;
}

static void mtx_identidade(float m[4][4]) {
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++)
        m[r][c] = (r == c) ? 1.0f : 0.0f;
}

/* Esta e a convencao de vetores-linha do microcodigo F3D: o mesmo produto
 * usado pelo GLideN64, em que a posicao e calculada como v * matriz. */
static void mtx_mult(const float a[4][4], const float b[4][4], float out[4][4]) {
    float tmp[4][4];
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
        if (g_f3d_matrix_conventional)
            tmp[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] +
                        a[r][2] * b[2][c] + a[r][3] * b[3][c];
        else
            tmp[r][c] = a[0][c] * b[r][0] + a[1][c] * b[r][1] +
                        a[2][c] * b[r][2] + a[3][c] * b[r][3];
    }
    memcpy(out, tmp, sizeof(tmp));
}

static void mtx_preparar(void) {
    if (g_mtx_pronto) return;
    mtx_identidade(g_mtx_projection);
    for (int i = 0; i < 10; i++) mtx_identidade(g_mtx_model[i]);
    g_mtx_pronto = 1;
    g_mtx_sujo = 1;
}

static void mtx_combinar(void) {
    mtx_preparar();
    if (!g_mtx_sujo) return;
    if (g_f3d_matrix_conventional)
        mtx_mult(g_mtx_model[g_mtx_model_i], g_mtx_projection, g_mtx_combined);
    else
        mtx_mult(g_mtx_projection, g_mtx_model[g_mtx_model_i], g_mtx_combined);
    g_mtx_sujo = 0;
}

static void mtx_carregar(uint8_t* rdram, uint32_t src, uint8_t param) {
    if (src + 63 >= 0x800000u) return;
    mtx_preparar();
    float novo[4][4];
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
        uint32_t at = (uint32_t)(r * 4 + c) * 2;
        novo[r][c] = (float)(int16_t)ram16(rdram, src + at)
                   + (float)ram16(rdram, src + 0x20 + at) / 65536.0f;
    }
    if (param & 1u) {                                  /* PROJECTION */
        if (param & 2u) memcpy(g_mtx_projection, novo, sizeof(novo));
        else mtx_mult(g_mtx_projection, novo, g_mtx_projection);
    } else {                                           /* MODELVIEW */
        if ((param & 4u) && g_mtx_model_i < 9) {
            memcpy(g_mtx_model[g_mtx_model_i + 1], g_mtx_model[g_mtx_model_i],
                   sizeof(g_mtx_model[0]));
            g_mtx_model_i++;
        }
        if (param & 2u) memcpy(g_mtx_model[g_mtx_model_i], novo, sizeof(novo));
        else mtx_mult(g_mtx_model[g_mtx_model_i], novo, g_mtx_model[g_mtx_model_i]);
    }
    g_mtx_sujo = 1;
}

static void mtx_popar(uint32_t bytes) {
    /* F3DEX codifica em w1 o numero de bytes de matrizes a retirar (64 por
       matriz). As listas desta ROM usam o valor convencional 64; aceitar zero
       como uma retirada tambem preserva listas antigas que omitem o tamanho. */
    unsigned n = bytes / 64u;
    if (n == 0) n = 1;
    if (n > (unsigned)g_mtx_model_i) n = (unsigned)g_mtx_model_i;
    g_mtx_model_i -= (int)n;
    g_mtx_sujo = 1;
}

static void mtx_viewport(uint8_t* rdram, uint32_t src) {
    if (src + 15 >= 0x800000u) return;
    /* Viewport N64 usa 10.2 e Y cresce para baixo no framebuffer. */
    /* Vp = { vscale[x,y,z,w], vtrans[x,y,z,w] }.  O acesso a RDRAM ja
       normaliza a palavra do N64 em ram16; portanto X esta nos offsets 0 e
       8, e Y nos offsets 2 e 10. Inverte-los deslocava cada logo de
       (160,120) para (120,160). */
    g_vscale_x = (float)(int16_t)ram16(rdram, src + 0) / 4.0f;
    g_vscale_y = (float)(int16_t)ram16(rdram, src + 2) / 4.0f;
    g_vscale_z = (float)(int16_t)ram16(rdram, src + 4) / 4.0f;
    g_vtrans_x = (float)(int16_t)ram16(rdram, src + 8) / 4.0f;
    g_vtrans_y = (float)(int16_t)ram16(rdram, src + 10) / 4.0f;
    g_vtrans_z = (float)(int16_t)ram16(rdram, src + 12) / 4.0f;
}

static void mtx_projetar_recorte(f3d_vertex_t* v, int contar_descartado) {
    float cx = v->cx, cy = v->cy, cz = v->cz, cw = v->cw;
    /* Vertice com W negativo esta atras da camera. Sem clipping ele vira uma
     * projecao enorme no framebuffer (e liga paredes opostas do corredor).
     * Triangulos que cruzam o plano ainda precisarao de recorte completo,
     * mas rejeitar o caso inteiro ja e geometricamente seguro e evita a
     * contaminacao da imagem. */
    if (cw <= 0.01f) {
        v->descartado_camera = 1;
        if (contar_descartado) {
            if (cw > -0.01f) g_mtx_cw_nulo++;
            else g_mtx_atras++;
        }
        return;
    }
    v->sx = cx / cw * g_vscale_x + g_vtrans_x;
    v->sy = -cy / cw * g_vscale_y + g_vtrans_y;
    v->sz = cz / cw * g_vscale_z + g_vtrans_z;
    v->invw = 1.0f / cw;
    /* Project64 (CalculateFog) satura o fator em CADA vertice antes de o
     * hardware interpolar. Deixar o valor bruto passar pela interpolacao
     * prolongava artificialmente os extremos acima de 255: no corredor isso
     * mantinha o nucleo preto mesmo depois de a camera chegar ao trono.
     * Armazenamos o mesmo intervalo, normalizado para o rasterizador local. */
    float fog_bruto = cz / cw * (float)g_fog_multiplicador + (float)g_fog_offset;
    if (fog_bruto <= 0.0f) v->fog = 0.0f;
    else if (fog_bruto >= 255.0f) v->fog = 1.0f;
    else v->fog = fog_bruto / 255.0f;
    v->usa_matriz = 1;
    if (v->sx < g_mtx_sx_min) g_mtx_sx_min = v->sx;
    if (v->sx > g_mtx_sx_max) g_mtx_sx_max = v->sx;
    if (v->sy < g_mtx_sy_min) g_mtx_sy_min = v->sy;
    if (v->sy > g_mtx_sy_max) g_mtx_sy_max = v->sy;
    if (v->sx < 0.0f || v->sx >= 320.0f || v->sy < 0.0f || v->sy >= 240.0f)
        g_mtx_fora_view++;
}

static void mtx_transformar_vertice(f3d_vertex_t* v) {
    v->usa_matriz = 0;
    v->descartado_camera = 0;
    v->invw = 1.0f;
    v->fog = 0.0f;
    if (!g_f3d_matrix_mode) return;
    g_mtx_vertices++;
    mtx_combinar();
    float x = (float)v->x, y = (float)v->y, z = (float)v->z;
    v->cx = x * g_mtx_combined[0][0] + y * g_mtx_combined[1][0] + z * g_mtx_combined[2][0] + g_mtx_combined[3][0];
    v->cy = x * g_mtx_combined[0][1] + y * g_mtx_combined[1][1] + z * g_mtx_combined[2][1] + g_mtx_combined[3][1];
    v->cz = x * g_mtx_combined[0][2] + y * g_mtx_combined[1][2] + z * g_mtx_combined[2][2] + g_mtx_combined[3][2];
    v->cw = x * g_mtx_combined[0][3] + y * g_mtx_combined[1][3] + z * g_mtx_combined[2][3] + g_mtx_combined[3][3];
    mtx_projetar_recorte(v, 1);
}

/* LOADBLOCK: copia bruta da imagem de textura para a TMEM. */
static uint64_t g_lb_chamadas = 0, g_lb_bytes = 0, g_lb_recusas = 0;
static uint32_t g_ultima_textura = 0;
static int g_enix_dump_inicializado = 0;
static unsigned g_enix_dump_tiles = 0;
static char g_enix_dump_dir[MAX_PATH];
uint32_t rsp_ultima_textura(void) { return g_ultima_textura; }

static void dump_enix_tile(uint8_t* rdram, uint32_t addr, uint32_t bytes) {
    if (!g_enix_dump_inicializado) {
        const char* dir = getenv("WPJ2_DUMP_ENIX_DIR");
        g_enix_dump_inicializado = 1;
        if (dir && *dir) {
            snprintf(g_enix_dump_dir, sizeof(g_enix_dump_dir), "%s", dir);
            CreateDirectoryA(g_enix_dump_dir, NULL);
        }
    }
    if (!g_enix_dump_dir[0] || g_enix_dump_tiles >= 30u || bytes < 2048u) return;
    if (g_enix_dump_tiles == 0u) {
        char pal[MAX_PATH];
        snprintf(pal, sizeof(pal), "%s\\palette.rgba5551", g_enix_dump_dir);
        FILE* f = fopen(pal, "wb");
        if (f) { fwrite(g_tmem + 0x800, 1, 0x200, f); fclose(f); }
    }
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\tile_%02u.ci8", g_enix_dump_dir,
             g_enix_dump_tiles);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    for (uint32_t i = 0; i < 2048u; i++) fputc(ram8(rdram, addr + i), f);
    fclose(f);
    g_enix_dump_tiles++;
}

/* Amostra agregada das origens reais de LOADBLOCK. O ultimo SETTIMG nao e
 * necessariamente aquele que desenhou os sprites; esta tabela conserva cada
 * origem observada e mede se ela continha dados antes de entrar na TMEM. */
#define LB_SOURCE_MAX 32
typedef struct { uint32_t addr; uint64_t loads, bytes, nao_zero; } lb_source_t;
static lb_source_t g_lb_source[LB_SOURCE_MAX];
static int g_lb_source_n = 0;

static void registrar_fonte_loadblock(uint32_t addr, uint32_t bytes, uint32_t nao_zero) {
    int i;
    for (i = 0; i < g_lb_source_n; i++) if (g_lb_source[i].addr == addr) break;
    if (i == g_lb_source_n) {
        if (g_lb_source_n >= LB_SOURCE_MAX) return;
        g_lb_source[i].addr = addr;
        g_lb_source_n++;
    }
    g_lb_source[i].loads++;
    g_lb_source[i].bytes += bytes;
    g_lb_source[i].nao_zero += nao_zero;
}

/* Em LOADBLOCK, cada virada de linha indicada por DXT troca as duas palavras
 * de cada qword na TMEM. O RDP faz isso para bancos alternados; a copia linear
 * deixava linhas pares corretas e linhas impares com pares de texels trocados. */
static void tmem_intercalar_qwords(uint32_t primeira_palavra, uint32_t qwords) {
    while (qwords--) {
        uint32_t a = primeira_palavra * 4u;
        uint32_t b = a + 4u;
        if (b + 4u > TMEM_BYTES) return;
        uint8_t temp[4];
        memcpy(temp, g_tmem + a, 4);
        memcpy(g_tmem + a, g_tmem + b, 4);
        memcpy(g_tmem + b, temp, 4);
        primeira_palavra += 2;
    }
}

/* LOADBLOCK CI8 do mosaico 8/1: DXT=0x100 muda de banco a cada linha de
 * 64 bytes. O RDP nao troca os indices CI, mas os dois words de 32 bits de
 * cada qword nas linhas impares. O oraculo Project64 confirma essa permuta;
 * deixar a carga linear faz os blocos 64x32 se encontrarem com uma borda
 * diferente durante o zoom. */
static void tmem_intercalar_ci8_linhas(uint32_t inicio, uint32_t bytes,
                                       uint32_t bytes_linha) {
    if (bytes_linha < 8u) return;
    for (uint32_t linha = 1; linha * bytes_linha < bytes; linha += 2u) {
        uint32_t base = inicio + linha * bytes_linha;
        for (uint32_t p = base; p + 7u < inicio + bytes && p + 7u < TMEM_BYTES; p += 8u) {
            uint8_t tmp[4];
            memcpy(tmp, g_tmem + p, 4u);
            memcpy(g_tmem + p, g_tmem + p + 4u, 4u);
            memcpy(g_tmem + p + 4u, tmp, 4u);
        }
    }
}

static void carregar_bloco(uint8_t* rdram, uint32_t tile, uint32_t lrs, uint32_t dxt) {
    g_lb_chamadas++;
    uint32_t bits = (g_timg_siz == 0) ? 4 : (g_timg_siz == 1) ? 8
                  : (g_timg_siz == 2) ? 16 : 32;
    uint32_t bytes = ((lrs + 1) * bits) / 8;
    uint32_t dst = g_tile[tile & 7].tmem * 8;

    if (g_lb_chamadas <= 4) {
        printf("  [load] bloco: tile=%u timg=0x%08X siz=%u lrs=%u -> tmem 0x%X,"
               " %u bytes\n", tile, g_timg_addr, g_timg_siz, lrs, dst, bytes);
        fflush(stdout);
    }
    g_ultima_textura = g_timg_addr;
    if (dst >= TMEM_BYTES || !bytes || !g_timg_addr
        || g_timg_addr + bytes > 0x800000u) { g_lb_recusas++; return; }
    if (dst + bytes > TMEM_BYTES) bytes = TMEM_BYTES - dst;
    uint32_t nao_zero = 0;
    for (uint32_t i = 0; i < bytes; i++) {
        uint8_t v = ram8(rdram, g_timg_addr + i);
        g_tmem[dst + i] = v;
        if (v) nao_zero++;
    }
    /* A permuta de bancos e especifica do LOADBLOCK/DXT. A regra generica
     * permanece isolada no corredor; a excecao CI8 abaixo e estrita ao
     * mosaico 8/1, validada contra os comandos do Project64. */
    int textura_ci = (g_timg_fmt == 2u);
    int textura_rgba16 = (g_timg_fmt == 0u && g_timg_siz == 2u);
    int corredor_3d = (int16_t)rsp_rdram16(rdram, 0x001A7234u) == 12 &&
                      (int16_t)rsp_rdram16(rdram, 0x001A723Cu) == 50;
    int mosaico_ci8_8_1 = (int16_t)rsp_rdram16(rdram, 0x001A7234u) == 8 &&
                           (int16_t)rsp_rdram16(rdram, 0x001A723Cu) == 1 &&
                           g_timg_addr == 0x002B64B0u && bytes == 2048u &&
                           dxt == 0x100u;
    if (mosaico_ci8_8_1) {
        dump_enix_tile(rdram, g_timg_addr, bytes);
        const char* e = getenv("WPJ2_CI8_DXT_8_1");
        if (!e || atoi(e) != 0)
            tmem_intercalar_ci8_linhas(dst, bytes, g_tile[tile & 7u].linha * 8u);
    }
    if (g_tmem_interleave_rgba16 < 0) {
        const char* e = getenv("WPJ2_TMEM_INTERLEAVE_RGBA16");
        g_tmem_interleave_rgba16 = e && atoi(e) != 0;
    }
    if (g_tmem_interleave_ci < 0) {
        const char* e = getenv("WPJ2_TMEM_INTERLEAVE_CI");
        g_tmem_interleave_ci = e && atoi(e) != 0;
    }
    /* LOADBLOCK usa DXT para trocar bancos a cada linha; RGBA16 tambem usa
     * esses bancos. A exclusao antiga preservava a abertura, mas deixava os
     * materiais RGBA16 do corredor com words pares trocados. */
    if (g_tmem_interleave && corredor_3d && dxt && (!textura_ci || g_tmem_interleave_ci) &&
        (!textura_rgba16 || g_tmem_interleave_rgba16)) {
        uint32_t tmem_qword = g_tile[tile & 7].tmem;
        uint32_t restante = bytes >> 3;
        uint32_t contador = 0, linha = 0;
        while (restante) {
            do {
                tmem_qword++; restante--;
                if (!restante) break;
                contador += dxt;
            } while ((contador & 0x800u) == 0);
            if (!restante) { tmem_intercalar_qwords(tmem_qword << 1, linha); break; }
            do {
                linha++; restante--;
                if (!restante) break;
                contador += dxt;
            } while ((contador & 0x800u) != 0);
            tmem_intercalar_qwords(tmem_qword << 1, linha);
            tmem_qword += linha;
            linha = 0;
        }
    }
    registrar_fonte_loadblock(g_timg_addr, bytes, nao_zero);
    g_lb_bytes += bytes;
    if (!g_tarefa_lb_cargas) g_tarefa_lb_primeira = g_timg_addr;
    g_tarefa_lb_ultima = g_timg_addr;
    g_tarefa_lb_cargas++;
    g_tarefa_lb_bytes += bytes;
    g_tarefa_lb_nao_zero += nao_zero;
}

/* LOADTILE transfere um retangulo da textura-fonte para a TMEM. A abertura
 * inicial usa predominantemente LOADBLOCK, mas os sprites posteriores usam
 * tiles CI8 pequenos: ignorar F4 fazia cada TEXRECT ler sobras da carga
 * anterior. As coordenadas da RDP estao em 10.2; SETTIMG informa o stride da
 * fonte e SETTILE o stride de destino em qwords. */
static void carregar_tile(uint8_t* rdram, uint32_t tile_n,
                          uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt) {
    tile_t* t = &g_tile[tile_n & 7u];
    uint32_t bits = (g_timg_siz == 0u) ? 4u : (g_timg_siz == 1u) ? 8u :
                    (g_timg_siz == 2u) ? 16u : 32u;
    uint32_t su = uls >> 2, st = ult >> 2;
    uint32_t eu = lrs >> 2, et = lrt >> 2;
    if (!g_timg_addr || !g_timg_larg || eu < su || et < st || bits == 4u)
        return;
    uint32_t largura = eu - su + 1u;
    uint32_t altura = et - st + 1u;
    uint32_t bytes_pixel = bits >> 3;
    uint32_t bytes_linha = largura * bytes_pixel;
    uint32_t dst_linha = t->linha * 8u;
    uint32_t dst_base = t->tmem * 8u;
    if (!dst_linha) dst_linha = bytes_linha;
    if (dst_base >= TMEM_BYTES || !bytes_linha) return;
    for (uint32_t y = 0; y < altura; y++) {
        uint64_t src64 = (uint64_t)g_timg_addr +
            ((uint64_t)(st + y) * g_timg_larg + su) * bytes_pixel;
        uint64_t dst64 = (uint64_t)dst_base + (uint64_t)y * dst_linha;
        if (src64 + bytes_linha > 0x800000u || dst64 >= TMEM_BYTES) break;
        uint32_t copiar = bytes_linha;
        if (dst64 + copiar > TMEM_BYTES) copiar = TMEM_BYTES - (uint32_t)dst64;
        for (uint32_t x = 0; x < copiar; x++)
            g_tmem[(uint32_t)dst64 + x] = ram8(rdram, (uint32_t)src64 + x);
        if (copiar < bytes_linha) break;
    }
    g_ultima_textura = g_timg_addr;
}

/* LOADTLUT: a paleta mora na metade alta da TMEM, 16 bits por entrada. */
static uint64_t g_tlut_chamadas = 0;
static void carregar_tlut(uint8_t* rdram, uint32_t tile, uint32_t contagem) {
    uint32_t dst = g_tile[tile & 7].tmem * 8;
    uint32_t nao_zero = 0;
    for (uint32_t i = 0; i <= contagem && dst + i * 2 + 1 < TMEM_BYTES; i++) {
        uint16_t c = ram16(rdram, g_timg_addr + i * 2);
        g_tmem[dst + i * 2]     = (uint8_t)(c >> 8);
        g_tmem[dst + i * 2 + 1] = (uint8_t)c;
        if (c) nao_zero++;
    }
    g_tlut_chamadas++;
    if (g_tlut_chamadas <= 4) {
        uint32_t amostra = dst + 0x10 * 2;
        uint16_t cor10 = (amostra + 1 < TMEM_BYTES)
            ? (uint16_t)((g_tmem[amostra] << 8) | g_tmem[amostra + 1]) : 0;
        printf("  [tlut] timg=0x%08X tile=%u tmem=0x%X entradas=%u nao-zero=%u"
               " cor[10]=0x%04X\n", g_timg_addr, tile, dst, contagem + 1,
               nao_zero, cor10);
    }
}

/* Busca um texel e devolve RGBA5551. `ok` sai zero se o formato nao tem suporte. */
static uint16_t texel(const tile_t* t, uint32_t s, uint32_t t_, int* ok) {
    *ok = 1;
    uint32_t base = t->tmem * 8;
    uint32_t linha = t->linha * 8;
    if (t->fmt == 2 && t->siz == 0) {                  /* CI4 */
        uint32_t off = base + t_ * linha + s / 2;
        if (off >= TMEM_BYTES) { *ok = 0; return 0; }
        uint8_t b = g_tmem[off];
        uint32_t idx = (s & 1) ? (b & 0xF) : (b >> 4);
        uint32_t p = 0x800 + (t->paleta * 16 + idx) * 2;
        if (p + 1 >= TMEM_BYTES) { *ok = 0; return 0; }
        return (uint16_t)((g_tmem[p] << 8) | g_tmem[p + 1]);
    }
    if (t->fmt == 2 && t->siz == 1) {                  /* CI8 */
        uint32_t off = base + t_ * linha + s;
        if (off >= TMEM_BYTES) { *ok = 0; return 0; }
        uint32_t p = 0x800 + g_tmem[off] * 2;
        if (p + 1 >= TMEM_BYTES) { *ok = 0; return 0; }
        return (uint16_t)((g_tmem[p] << 8) | g_tmem[p + 1]);
    }
    if (t->siz == 2) {                                 /* RGBA16 e afins */
        uint32_t off = base + t_ * linha + s * 2;
        if (off + 1 >= TMEM_BYTES) { *ok = 0; return 0; }
        return (uint16_t)((g_tmem[off] << 8) | g_tmem[off + 1]);
    }
    *ok = 0;
    return 0;
}

/* G_SETTILE define se cada eixo repete (mask), espelha ou faz clamp. Os
 * corredores usam coordenadas fora da primeira copia do tile; o clamp global
 * anterior colava sempre a ultima coluna/linha e criava paineis escuros. */
static int tile_coord(int v, int tamanho, uint32_t mask, uint32_t modo) {
    if (tamanho <= 0) return -1;
    if (modo & 2u) { /* G_TX_CLAMP */
        if (v < 0) return 0;
        return v >= tamanho ? tamanho - 1 : v;
    }
    if (!mask) { /* sem mascara nao ha periodo programado */
        if (v < 0) return 0;
        return v >= tamanho ? tamanho - 1 : v;
    }
    int periodo = 1 << (mask > 10u ? 10u : mask);
    int modulo = v % periodo;
    if (modulo < 0) modulo += periodo;
    if (modo & 1u) { /* G_TX_MIRROR: segunda metade volta */
        int periodo2 = periodo << 1;
        int m = v % periodo2;
        if (m < 0) m += periodo2;
        modulo = m < periodo ? m : periodo2 - 1 - m;
    }
    return modulo >= tamanho ? tamanho - 1 : modulo;
}

/* G_SETTILE tambem carrega um deslocamento por eixo.  As coordenadas do
 * vertice permanecem em 5.10; portanto este ajuste precisa acontecer antes
 * de converter para texel (/64).  Valores 0..10 dividem, e 11..15 multiplicam
 * pelo complemento ate 16, conforme a codificacao do RDP. */
static int tile_aplicar_shift(int coord, uint32_t shift) {
    shift &= 15u;
    if (shift <= 10u) return coord >> shift;
    return coord << (16u - shift);
}

/* Vtx::tc e S10.5 e gSPTexture fornece escala unsigned 0.16. Conservamos a
 * coordenada em 1/64 de texel ate consultar o tile. Com 0x8000, usado pelos
 * modelos desta ROM, o valor permanece igual; os demais valores deixam de
 * depender da antiga coincidencia de dividir sempre por 64. */
static int texture_aplicar_escala(int coord, uint16_t escala) {
    uint32_t e = escala ? (uint32_t)escala : 0x10000u;
    int64_t produto = (int64_t)coord * (int64_t)e;
    return (int)(produto >= 0 ? (produto + 0x4000) / 0x8000
                              : (produto - 0x4000) / 0x8000);
}

static uint8_t cor_3p_canal8(uint16_t c00, uint16_t c10, uint16_t c01,
                              uint16_t c11, unsigned desloc,
                              uint32_t fs, uint32_t ft) {
    int a = (int)(((c00 >> desloc) & 31u) * 255u / 31u);
    int b = (int)(((c10 >> desloc) & 31u) * 255u / 31u);
    int c = (int)(((c01 >> desloc) & 31u) * 255u / 31u);
    int d = (int)(((c11 >> desloc) & 31u) * 255u / 31u);
    int valor;
    if (fs + ft < 64u)
        valor = a + ((int)fs * (b - a) + (int)ft * (c - a) + 32) / 64;
    else
        valor = d + ((int)(64u - fs) * (c - d) +
                     (int)(64u - ft) * (b - d) + 32) / 64;
    return (uint8_t)(valor < 0 ? 0 : valor > 255 ? 255 : valor);
}

static int tex_filter_forcado(void) {
    if (g_tex_filter_forcado == -2) {
        const char* e = getenv("WPJ2_TEX_FILTER");
        g_tex_filter_forcado = e ? atoi(e) : -1;
        if (g_tex_filter_forcado != 0 && g_tex_filter_forcado != 2)
            g_tex_filter_forcado = -1;
    }
    return g_tex_filter_forcado;
}

static int tex_filter_3d_forcado(void) {
    if (g_tex_filter_3d_forcado == -2) {
        const char* e = getenv("WPJ2_TEX_FILTER_3D");
        g_tex_filter_3d_forcado = e ? atoi(e) : -1;
        if (g_tex_filter_3d_forcado != 0 && g_tex_filter_3d_forcado != 2)
            g_tex_filter_3d_forcado = -1;
    }
    return g_tex_filter_3d_forcado;
}

/* O RDP nao usa uma unica regra de cor: cada SETCOMBINE escolhe fontes para
 * (A - B) * C + D, duas vezes.  A primeira versao do rasterizador aplicava
 * sempre TEXEL0 * SHADE; isso preservou as logos, mas deixa materiais 3D que
 * usam PRIMITIVE, ENVIRONMENT ou a saida do primeiro ciclo excessivamente
 * claros/escuros. Esta e uma implementacao compacta das fontes RGB usadas
 * pela ROM; as fontes de chave/ruido ainda convergem para zero, como no caso
 * neutro do RDP. */
typedef struct { int r, g, b; } comb_rgb_t;

static int comb_clamp(int v) {
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

static comb_rgb_t comb_cor_word(uint32_t rgba) {
    comb_rgb_t c = { (int)(rgba >> 24), (int)((rgba >> 16) & 0xFFu),
                     (int)((rgba >> 8) & 0xFFu) };
    return c;
}

static comb_rgb_t comb_cor_5551(uint16_t c) {
    comb_rgb_t r = { (int)(((c >> 11) & 31u) * 255u / 31u),
                     (int)(((c >> 6) & 31u) * 255u / 31u),
                     (int)(((c >> 1) & 31u) * 255u / 31u) };
    return r;
}

static comb_rgb_t comb_fonte_abd(uint32_t sel, comb_rgb_t combined,
                                  comb_rgb_t texel0, comb_rgb_t prim,
                                  comb_rgb_t shade, comb_rgb_t env) {
    switch (sel) {
        case 0: return combined;
        case 1: return texel0;
        case 3: return prim;
        case 4: return shade;
        case 5: return env;
        case 6: { comb_rgb_t one = { 255, 255, 255 }; return one; }
        default: { comb_rgb_t zero = { 0, 0, 0 }; return zero; }
    }
}

static comb_rgb_t comb_fonte_c(uint32_t sel, comb_rgb_t combined,
                                comb_rgb_t texel0, comb_rgb_t prim,
                                comb_rgb_t shade, comb_rgb_t env,
                                int texel_alpha, int shade_alpha,
                                int prim_alpha, int env_alpha) {
    switch (sel) {
        case 0: return combined;
        case 1: return texel0;
        case 3: return prim;
        case 4: return shade;
        case 5: return env;
        case 7: { comb_rgb_t a = { texel_alpha, texel_alpha, texel_alpha }; return a; }
        case 8: { comb_rgb_t a = { texel_alpha, texel_alpha, texel_alpha }; return a; }
        case 10:{ comb_rgb_t a = { prim_alpha, prim_alpha, prim_alpha }; return a; }
        case 11:{ comb_rgb_t a = { shade_alpha, shade_alpha, shade_alpha }; return a; }
        case 12:{ comb_rgb_t a = { env_alpha, env_alpha, env_alpha }; return a; }
        case 6: case 13: case 14: case 15: { comb_rgb_t zero = { 0, 0, 0 }; return zero; }
        default: { comb_rgb_t zero = { 0, 0, 0 }; return zero; }
    }
}

static comb_rgb_t comb_ciclo(uint32_t a_sel, uint32_t b_sel, uint32_t c_sel,
                             uint32_t d_sel, comb_rgb_t combined,
                             comb_rgb_t texel0, comb_rgb_t prim,
                             comb_rgb_t shade, comb_rgb_t env,
                             int texel_alpha, int shade_alpha,
                             int prim_alpha, int env_alpha) {
    comb_rgb_t a = comb_fonte_abd(a_sel, combined, texel0, prim, shade, env);
    comb_rgb_t b = comb_fonte_abd(b_sel, combined, texel0, prim, shade, env);
    comb_rgb_t c = comb_fonte_c(c_sel, combined, texel0, prim, shade, env,
                                 texel_alpha, shade_alpha, prim_alpha, env_alpha);
    comb_rgb_t d = comb_fonte_abd(d_sel, combined, texel0, prim, shade, env);
    comb_rgb_t out = {
        comb_clamp(((a.r - b.r) * c.r + 127) / 255 + d.r),
        comb_clamp(((a.g - b.g) * c.g + 127) / 255 + d.g),
        comb_clamp(((a.b - b.b) * c.b + 127) / 255 + d.b)
    };
    return out;
}

static uint16_t comb_aplicar(uint16_t texel, int sr, int sg, int sb, int sa,
                             int* out_r, int* out_g, int* out_b) {
    comb_rgb_t texture = comb_cor_5551(texel);
    comb_rgb_t shade = { comb_clamp(sr), comb_clamp(sg), comb_clamp(sb) };
    comb_rgb_t prim = comb_cor_word(g_prim_color);
    comb_rgb_t env = comb_cor_word(g_env_color);
    int tex_a = (texel & 1u) ? 255 : 0;
    int prim_a = (int)(g_prim_color & 0xFFu);
    int env_a = (int)(g_env_color & 0xFFu);
    /* Campos documentados de G_SETCOMBINE, ignorando o opcode FC no topo de w0. */
    uint32_t a0 = (g_combine_w0 >> 20) & 0xFu;
    uint32_t c0 = (g_combine_w0 >> 15) & 0x1Fu;
    uint32_t a1 = (g_combine_w0 >>  5) & 0xFu;
    uint32_t c1 =  g_combine_w0        & 0x1Fu;
    uint32_t b0 = (g_combine_w1 >> 28) & 0xFu;
    uint32_t b1 = (g_combine_w1 >> 24) & 0xFu;
    uint32_t d0 = (g_combine_w1 >> 15) & 0x7u;
    uint32_t d1 = (g_combine_w1 >>  6) & 0x7u;
    comb_rgb_t first = comb_ciclo(a0, b0, c0, d0, (comb_rgb_t){0, 0, 0},
                                   texture, prim, shade, env, tex_a, sa, prim_a, env_a);
    comb_rgb_t out = comb_ciclo(a1, b1, c1, d1, first,
                                 texture, prim, shade, env, tex_a, sa, prim_a, env_a);
    if (out_r) *out_r = out.r;
    if (out_g) *out_g = out.g;
    if (out_b) *out_b = out.b;
    return (uint16_t)(((out.r * 31u / 255u) << 11) |
                      ((out.g * 31u / 255u) <<  6) |
                      ((out.b * 31u / 255u) <<  1) | (texel & 1u));
}

/* TRI1 do F3DEX antigo referencia cada entrada pelo indice multiplicado por
 * dez. A rota 3D respeita G_TP_PERSP; quads 2D continuam afins. */
static void pintar_triangulo_raster(uint8_t* rdram, const f3d_vertex_t* a,
                                    const f3d_vertex_t* b, const f3d_vertex_t* c) {
    g_tarefa_tri_recebidos++;
    if (!a || !b || !c || !g_cimg_addr || g_cimg_siz != 2)
        return;
    if (g_alpha_trace_estado == 12 && g_alpha_trace_subestado == 50) {
        /* Enquanto o mosaico anterior ainda estiver ativo, a GPU real o
         * exibe no buffer frontal; a geometria nova trabalha no buffer de
         * trás. Não a rasterizar aqui impede a sobreposição artificial. */
        if (g_transicao_mosaico_2d &&
            (!g_transicao_preto_ate || hle_retraces() < g_transicao_preto_ate))
            return;
        g_transicao_apresentacao = 0;
        g_transicao_3d_iniciada = 1;
        if (!g_transicao_3d_limpou_tarefa) {
            limpar_alvo_entrada_3d(rdram);
            g_transicao_3d_limpou_tarefa = 1;
        }
    }
    const tile_t* T = &g_tile[g_texture_tile & 7u];
    registrar_tri_source(T, a, b, c);
    /* O RDP conserva coordenadas subpixel; arredondar cada vertice para um
     * pixel inteiro antes de calcular coverage criava escadas mesmo com
     * quatro amostras. Quarto de pixel coincide com a grade 2x2 usada abaixo
     * e preserva a geometria projetada ate a decisao de cobertura. */
    int ax = a->usa_matriz ? (int)lrintf(a->sx * 4.0f) : (160 + a->x) * 4;
    int ay = a->usa_matriz ? (int)lrintf(a->sy * 4.0f) : (120 - a->y) * 4;
    int bx = b->usa_matriz ? (int)lrintf(b->sx * 4.0f) : (160 + b->x) * 4;
    int by = b->usa_matriz ? (int)lrintf(b->sy * 4.0f) : (120 - b->y) * 4;
    int cx = c->usa_matriz ? (int)lrintf(c->sx * 4.0f) : (160 + c->x) * 4;
    int cy = c->usa_matriz ? (int)lrintf(c->sy * 4.0f) : (120 - c->y) * 4;
    int64_t area = (int64_t)(bx - ax) * (cy - ay) -
                   (int64_t)(by - ay) * (cx - ax);
    if (!area) return;
    /* G_CULL_FRONT/BACK nos modos geometricos do F3DEX. As coordenadas Y ja
     * foram convertidas para o framebuffer (crescem para baixo), portanto uma
     * face de frente tem area positiva nesta convencao. A malha da descida usa
     * CULL_FRONT para manter a superficie interna do tunel. */
    if (g_f3d_cull_mode && (g_geom_mode & 0x00000200u) && area > 0) {
        g_tri_cull_frente++;
        g_tarefa_tri_cull_frente++;
        return;
    }
    if (g_f3d_cull_mode && (g_geom_mode & 0x00000400u) && area < 0) {
        g_tri_cull_tras++;
        g_tarefa_tri_cull_tras++;
        return;
    }
    int minx4 = ax; if (bx < minx4) minx4 = bx; if (cx < minx4) minx4 = cx;
    int maxx4 = ax; if (bx > maxx4) maxx4 = bx; if (cx > maxx4) maxx4 = cx;
    int miny4 = ay; if (by < miny4) miny4 = by; if (cy < miny4) miny4 = cy;
    int maxy4 = ay; if (by > maxy4) maxy4 = by; if (cy > maxy4) maxy4 = cy;
    int minx = minx4 >= 0 ? minx4 / 4 : (minx4 - 3) / 4;
    int maxx = maxx4 >= 0 ? maxx4 / 4 : (maxx4 - 3) / 4;
    int miny = miny4 >= 0 ? miny4 / 4 : (miny4 - 3) / 4;
    int maxy = maxy4 >= 0 ? maxy4 / 4 : (maxy4 - 3) / 4;
    if (minx < (int)g_scis_x0) minx = (int)g_scis_x0;
    if (miny < (int)g_scis_y0) miny = (int)g_scis_y0;
    if (maxx >= (int)g_scis_x1) maxx = (int)g_scis_x1 - 1;
    if (maxy >= (int)g_scis_y1) maxy = (int)g_scis_y1 - 1;
    int aa_ativo = (g_othermode_l & 0x08u) != 0 && aa_habilitado();
    if (aa_ativo) aa_preparar_alvo();
    uint64_t pixels_antes = g_triangulo_pixels;
    for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
        int64_t px4_centro = (int64_t)x * 4 + 2;
        int64_t py4_centro = (int64_t)y * 4 + 2;
        int64_t wa = ((int64_t)bx - px4_centro) * ((int64_t)cy - py4_centro) -
                     ((int64_t)by - py4_centro) * ((int64_t)cx - px4_centro);
        int64_t wb = ((int64_t)cx - px4_centro) * ((int64_t)ay - py4_centro) -
                     ((int64_t)cy - py4_centro) * ((int64_t)ax - px4_centro);
        int64_t wc = area - wa - wb;
        int centro_dentro = (area > 0)
            ? (wa >= 0 && wb >= 0 && wc >= 0)
            : (wa <= 0 && wb <= 0 && wc <= 0);
        if (!aa_ativo && !centro_dentro) continue;
        /* Quatro subamostras cobrem inclusive pixels cujo centro fica fora da
         * face. O caminho antigo descartava esses pixels antes deste teste e
         * por isso o AA apenas escurecia o lado interno de cada triangulo. */
        int cobertura = 4;
        uint8_t mascara_cobertura = 0x0Fu;
        if (aa_ativo) {
            static const int sub[] = { 1, 3 };
            cobertura = 0;
            mascara_cobertura = 0;
            for (unsigned sy = 0; sy < 2; sy++) for (unsigned sx = 0; sx < 2; sx++) {
                int64_t px4 = (int64_t)x * 4 + sub[sx];
                int64_t py4 = (int64_t)y * 4 + sub[sy];
                int64_t wa4 = ((int64_t)bx - px4) * ((int64_t)cy - py4) -
                              ((int64_t)by - py4) * ((int64_t)cx - px4);
                int64_t wb4 = ((int64_t)cx - px4) * ((int64_t)ay - py4) -
                              ((int64_t)cy - py4) * ((int64_t)ax - px4);
                int64_t wc4 = area - wa4 - wb4;
                if ((area > 0 && wa4 >= 0 && wb4 >= 0 && wc4 >= 0) ||
                    (area < 0 && wa4 <= 0 && wb4 <= 0 && wc4 <= 0))
                {
                    cobertura++;
                    mascara_cobertura |= (uint8_t)(1u << (sy * 2u + sx));
                }
            }
            if (!cobertura) continue;
        }
        int si, ti, s_coord, t_coord;
        /* RT64/TextureSampler consulta OtherMode::textPersp(): matriz sozinha
         * nao autoriza a divisao. Isso importa em listas que alternam objetos
         * 3D e quads de interface usando o mesmo microcodigo. */
        int textura_perspectiva = a->usa_matriz && b->usa_matriz && c->usa_matriz &&
                                  (g_othermode_h & G_TP_PERSP_BIT) != 0;
        if (textura_perspectiva &&
            (fabsf(a->invw - b->invw) > 1.0e-5f ||
              fabsf(a->invw - c->invw) > 1.0e-5f)) {
            float invw = ((float)wa * a->invw + (float)wb * b->invw +
                          (float)wc * c->invw) / (float)area;
            if (invw <= 1.0e-12f) continue;
            float sp = (((float)wa * (float)a->s * a->invw +
                         (float)wb * (float)b->s * b->invw +
                         (float)wc * (float)c->s * c->invw) / (float)area) / invw;
            float tp = (((float)wa * (float)a->t * a->invw +
                         (float)wb * (float)b->t * b->invw +
                         (float)wc * (float)c->t * c->invw) / (float)area) / invw;
            s_coord = (int)sp;
            t_coord = (int)tp;
            g_tex_persp_pixels++;
        } else {
            int64_t ss = (int64_t)wa * a->s + (int64_t)wb * b->s + (int64_t)wc * c->s;
            int64_t tt = (int64_t)wa * a->t + (int64_t)wb * b->t + (int64_t)wc * c->t;
            s_coord = (int)(ss / area);
            t_coord = (int)(tt / area);
            /* O shader do RT64 compensa a ausencia de divisao de perspectiva
             * por 0,5 em triangulos (nao em TEXRECT). Reproduzir aqui evita
             * que G_TP_NONE leia a textura com o dobro da frequencia. */
            if (a->usa_matriz && b->usa_matriz && c->usa_matriz &&
                !(g_othermode_h & G_TP_PERSP_BIT)) {
                s_coord /= 2;
                t_coord /= 2;
            }
            g_tex_affine_pixels++;
        }
        /* A escala de gSPTexture e parte do estado Fast3D; ela e aplicada
         * abaixo somente ao caminho triangulado. TEXRECT traz seus proprios
         * incrementos 10.5 e nao consulta esta escala. */
        int semantica_texture_3d = g_alpha_trace_estado == 12 &&
                                   g_alpha_trace_subestado == 50;
        int64_t aa = (int64_t)wa * a->a + (int64_t)wb * b->a + (int64_t)wc * c->a;
        int64_t rr = (int64_t)wa * a->r + (int64_t)wb * b->r + (int64_t)wc * c->r;
        int64_t gg = (int64_t)wa * a->g + (int64_t)wb * b->g + (int64_t)wc * c->g;
        int64_t bb = (int64_t)wa * a->b + (int64_t)wb * b->b + (int64_t)wc * c->b;
        int va = (int)(aa / area);
        if (va <= 0) continue;
        if (va > 255) va = 255;
        /* A abertura configura e limpa um Z-image antes da malha 3D. Sem
         * o teste, a ultima face submetida atravessa todas as outras. O RDP
         * usa uma codificacao nao linear; a profundidade de viewport abaixo
         * preserva, por enquanto, a ordem de oclusao que interessa ao prototipo. */
        if (g_f3d_z_mode && g_zimg_addr && (g_othermode_l & (0x10u | 0x20u))) {
            uint32_t zoff = g_zimg_addr + ((uint32_t)y * g_cimg_larg + (uint32_t)x) * 2u;
            if (zoff + 2 > 0x800000u) continue;
            uint16_t anterior = *(uint16_t*)(rdram + (zoff ^ 2u));
            if (aa_ativo) {
                uint32_t ci = (uint32_t)y * 320u + (uint32_t)x;
                uint16_t* za = &g_aa_z_amostras[ci * 4u];
                if (!g_aa_z_valida[ci]) {
                    za[0] = za[1] = za[2] = za[3] = anterior;
                    g_aa_z_valida[ci] = 1;
                }
                uint8_t passa = 0;
                static const int subz[] = { 1, 3 };
                for (unsigned s = 0; s < 4; s++) {
                    if (!(mascara_cobertura & (1u << s))) continue;
                    int64_t px4 = (int64_t)x * 4 + subz[s & 1u];
                    int64_t py4 = (int64_t)y * 4 + subz[s >> 1u];
                    int64_t zwa = ((int64_t)bx - px4) * ((int64_t)cy - py4) -
                                  ((int64_t)by - py4) * ((int64_t)cx - px4);
                    int64_t zwb = ((int64_t)cx - px4) * ((int64_t)ay - py4) -
                                  ((int64_t)cy - py4) * ((int64_t)ax - px4);
                    int64_t zwc = area - zwa - zwb;
                    float zf = ((float)zwa * a->sz + (float)zwb * b->sz +
                                (float)zwc * c->sz) / (float)area;
                    uint16_t zu = (uint16_t)(zf <= 0.0f ? 0u :
                        zf >= 65535.0f ? 65535u : (uint32_t)(zf + 0.5f));
                    if ((g_othermode_l & 0x10u) && zu > za[s]) continue;
                    passa |= (uint8_t)(1u << s);
                    if (g_othermode_l & 0x20u) za[s] = zu;
                }
                if (!passa) { g_z_recusados++; g_tarefa_z_recusados++; continue; }
                mascara_cobertura = passa;
                if (g_othermode_l & 0x20u) {
                    uint16_t zmin = za[0];
                    for (unsigned s = 1; s < 4; s++) if (za[s] < zmin) zmin = za[s];
                    *(uint16_t*)(rdram + (zoff ^ 2u)) = zmin;
                }
                g_z_aceitos++; g_tarefa_z_aceitos++;
            } else {
                float zf = ((float)wa * a->sz + (float)wb * b->sz + (float)wc * c->sz) /
                           (float)area;
                uint16_t zu = (uint16_t)(zf <= 0.0f ? 0u :
                    zf >= 65535.0f ? 65535u : (uint32_t)(zf + 0.5f));
                if ((g_othermode_l & 0x10u) && zu > anterior) {
                    g_z_recusados++; g_tarefa_z_recusados++; continue;
                }
                if (g_othermode_l & 0x20u)
                    *(uint16_t*)(rdram + (zoff ^ 2u)) = zu;
                g_z_aceitos++; g_tarefa_z_aceitos++;
            }
        }
        /* Vtx::tc usa 5.10, ao contrario dos parametros 10.5 de TEXRECT.
         * A logo mede 32x64 no SETTILESIZE e traz extremos 2048/4096; portanto
         * a conversao correta e /64. O /32 anterior lia 64x128 e invadia as
         * linhas vizinhas da TMEM. */
        int smax = (int)((T->lrs - T->uls) >> 2);
        int tmax = (int)((T->lrt - T->ult) >> 2);
        s_coord = texture_aplicar_escala(s_coord, g_texture_scale_s);
        t_coord = texture_aplicar_escala(t_coord, g_texture_scale_t);
        s_coord = tile_aplicar_shift(s_coord, T->shifts);
        t_coord = tile_aplicar_shift(t_coord, T->shiftt);
        si = tile_coord((s_coord >> 6) - (int)(T->uls >> 2), smax + 1, T->masks, T->cms);
        ti = tile_coord((t_coord >> 6) - (int)(T->ult >> 2), tmax + 1, T->maskt, T->cmt);
        if (si < 0 || ti < 0) continue;
        int ok = 1;
        /* Quando G_TEXTURE desliga a unidade, SHADE/PRIMITIVE ainda podem
         * formar gradientes no fundo. Amostrar o ultimo tile neste caso deixa
         * manchas que nao existem no RDP. */
        /* G_TEXTURE vale para qualquer lista F3DEX, nao apenas para o 3D.
         * Os menus desenham o realce da opcao com quatro vertices SHADE,
         * gSPTexture(G_OFF) e G_RM_AA_XLU_SURF. Ignorar o G_OFF nas cenas 2D
         * fazia o quad amostrar o ultimo tile, apagando o retangulo vermelho
         * ou transformando-o em lixo de sprite sobre o cursor. */
        int textura_ativa = semantica_texture_3d
            ? (g_texture_ligada != 0)
            : (!g_texture_definida_na_tarefa || g_texture_ligada != 0);
        uint16_t cor = textura_ativa ? texel(T, (uint32_t)si, (uint32_t)ti, &ok) : 0xFFFFu;
        if (!ok || (textura_ativa && !(cor & 1))) continue;
        int tex_r8 = (int)(((cor >> 11) & 31u) * 255u / 31u);
        int tex_g8 = (int)(((cor >> 6) & 31u) * 255u / 31u);
        int tex_b8 = (int)(((cor >> 1) & 31u) * 255u / 31u);
        int filtro_tri = g_tex_filter;
        if (semantica_texture_3d) {
            int forcar_3d = tex_filter_3d_forcado();
            if (forcar_3d >= 0) filtro_tri = forcar_3d;
        }
        if (textura_ativa && filtro_tri == 2u) {
            /* G_TF_BILERP: o trono e CI8 e o RDP filtra apos consultar a
             * paleta. Antes este bloco aceitava somente RGBA16, portanto o
             * material dominante do corredor ficava em point sampling. */
            int si1 = tile_coord(((s_coord >> 6) + 1) - (int)(T->uls >> 2),
                                 smax + 1, T->masks, T->cms);
            int ti1 = tile_coord(((t_coord >> 6) + 1) - (int)(T->ult >> 2),
                                 tmax + 1, T->maskt, T->cmt);
            int ok10, ok01, ok11;
            uint16_t c10 = texel(T, (uint32_t)si1, (uint32_t)ti, &ok10);
            uint16_t c01 = texel(T, (uint32_t)si, (uint32_t)ti1, &ok01);
            uint16_t c11 = texel(T, (uint32_t)si1, (uint32_t)ti1, &ok11);
            if (ok10 && ok01 && ok11 && (c10 & 1u) && (c01 & 1u) && (c11 & 1u)) {
                uint32_t fs = (uint32_t)(s_coord & 63), ft = (uint32_t)(t_coord & 63);
                tex_r8 = cor_3p_canal8(cor, c10, c01, c11, 11, fs, ft);
                tex_g8 = cor_3p_canal8(cor, c10, c01, c11, 6, fs, ft);
                tex_b8 = cor_3p_canal8(cor, c10, c01, c11, 1, fs, ft);
                uint32_t r = ((uint32_t)tex_r8 * 31u + 127u) / 255u;
                uint32_t g = ((uint32_t)tex_g8 * 31u + 127u) / 255u;
                uint32_t b = ((uint32_t)tex_b8 * 31u + 127u) / 255u;
                cor = (uint16_t)((r << 11) | (g << 6) | (b << 1) | (cor & 1u));
            }
        }
        /* Os atributos RGB chegam como SHADE; a escolha de como mistura-los
         * com TEXEL0, PRIMITIVE e ENVIRONMENT pertence ao SETCOMBINE ativo. */
        int vr = (int)(rr / area), vg = (int)(gg / area), vb = (int)(bb / area);
        int combinar_rdp = g_rdp_combine_mode > 0 && g_alpha_trace_estado == 12 &&
                            g_alpha_trace_subestado == 50;
        int r8, g8, b8;
        if (combinar_rdp) {
            cor = comb_aplicar(cor, vr, vg, vb, va, &r8, &g8, &b8);
        } else {
            r8 = (tex_r8 * vr + 127) / 255;
            g8 = (tex_g8 * vg + 127) / 255;
            b8 = (tex_b8 * vb + 127) / 255;
            uint16_t r5_legacy = (uint16_t)(((uint32_t)r8 * 31u + 127u) / 255u);
            uint16_t g5_legacy = (uint16_t)(((uint32_t)g8 * 31u + 127u) / 255u);
            uint16_t b5_legacy = (uint16_t)(((uint32_t)b8 * 31u + 127u) / 255u);
            cor = (uint16_t)((r5_legacy << 11) | (g5_legacy << 6) |
                             (b5_legacy << 1) | (cor & 1u));
        }
        uint16_t r5 = (uint16_t)((cor >> 11) & 31u);
        uint16_t g5 = (uint16_t)((cor >>  6) & 31u);
        uint16_t b5 = (uint16_t)((cor >>  1) & 31u);
        uint32_t off = g_cimg_addr + ((uint32_t)y * g_cimg_larg + (uint32_t)x) * 2;
        if (off + 2 > 0x800000u) continue;
        /* Alpha no vertice tambem e usado pela cena 3D como atributo de
         * iluminacao/fog. Ele so representa transparencia quando o blender
         * RDP seleciona source-over; mistura-lo incondicionalmente deixava o
         * tunel aparecer sobre o ultimo quadro 2D, em vez de sobre o clear
         * preto que a propria lista da ROM ja emite. */
        if (va < 255 && rdp_blender_source_over()) {
            uint16_t dst = *(uint16_t*)(rdram + (off ^ 2));
            uint32_t dr = (dst >> 11) & 31u, dg = (dst >> 6) & 31u, db = (dst >> 1) & 31u;
            r5 = (uint16_t)((r5 * (uint32_t)va + dr * (uint32_t)(255 - va) + 127u) / 255u);
            g5 = (uint16_t)((g5 * (uint32_t)va + dg * (uint32_t)(255 - va) + 127u) / 255u);
            b5 = (uint16_t)((b5 * (uint32_t)va + db * (uint32_t)(255 - va) + 127u) / 255u);
            int dr8 = (int)(dr * 255u / 31u), dg8 = (int)(dg * 255u / 31u);
            int db8 = (int)(db * 255u / 31u);
            r8 = (r8 * va + dr8 * (255 - va) + 127) / 255;
            g8 = (g8 * va + dg8 * (255 - va) + 127) / 255;
            b8 = (b8 * va + db8 * (255 - va) + 127) / 255;
            cor = (uint16_t)((r5 << 11) | (g5 << 6) | (b5 << 1) | 1u);
        }
        /* Neste caminho o fator programado pela cena e usado como peso da
         * neblina. A cor configurada e preta e encobre o centro do tunel;
         * a evolucao temporal da mascara sera tratada separadamente. */
        if ((g_geom_mode & 0x00010000u) && fog_blender_ativo()) {
            /* vShadeColor (onde o RDP/Project64 leva o fator de fog) e um
             * atributo smooth: em triangulos 3D sua interpolacao e corrigida
             * por perspectiva. A forma afim ampliava artificialmente o nucleo
             * preto quando uma parede do tunel ficava muito inclinada. */
            float ff;
            if (a->usa_matriz && b->usa_matriz && c->usa_matriz) {
                float iw = ((float)wa * a->invw + (float)wb * b->invw +
                            (float)wc * c->invw) / (float)area;
                ff = iw > 1.0e-12f
                    ? (((float)wa * a->fog * a->invw + (float)wb * b->fog * b->invw +
                        (float)wc * c->fog * c->invw) / (float)area) / iw
                    : 0.0f;
            } else {
                ff = ((float)wa * a->fog + (float)wb * b->fog + (float)wc * c->fog) /
                     (float)area;
            }
            int fog = ff <= 0.0f ? 0 : ff >= 1.0f ? 255 : (int)(ff * 255.0f + 0.5f);
            if (fog) {
                uint16_t fr = (uint16_t)((g_fog_color >> 27) & 31u);
                uint16_t fg = (uint16_t)((g_fog_color >> 19) & 31u);
                uint16_t fb = (uint16_t)((g_fog_color >> 11) & 31u);
                r5 = (uint16_t)((r5 * (255 - fog) + fr * fog + 127) / 255);
                g5 = (uint16_t)((g5 * (255 - fog) + fg * fog + 127) / 255);
                b5 = (uint16_t)((b5 * (255 - fog) + fb * fog + 127) / 255);
                int fr8 = (int)(fr * 255u / 31u), fg8 = (int)(fg * 255u / 31u);
                int fb8 = (int)(fb * 255u / 31u);
                r8 = (r8 * (255 - fog) + fr8 * fog + 127) / 255;
                g8 = (g8 * (255 - fog) + fg8 * fog + 127) / 255;
                b8 = (b8 * (255 - fog) + fb8 * fog + 127) / 255;
                cor = (uint16_t)((r5 << 11) | (g5 << 6) | (b5 << 1) | 1u);
            }
        }
        if (aa_ativo) {
            uint32_t ci = (uint32_t)y * 320u + (uint32_t)x;
            g_aa_amostras_hires[ci] = semantica_texture_3d ? 1u : 0u;
            uint16_t* amostra = &g_aa_amostras[ci * 4u];
            uint32_t* amostra_rgb = &g_aa_amostras_rgb[ci * 4u];
            if (!g_aa_cobertura[ci]) {
                uint16_t dst = *(uint16_t*)(rdram + (off ^ 2));
                amostra[0] = amostra[1] = amostra[2] = amostra[3] = dst;
                uint32_t dr8 = ((dst >> 11) & 31u) * 255u / 31u;
                uint32_t dg8 = ((dst >> 6) & 31u) * 255u / 31u;
                uint32_t db8 = ((dst >> 1) & 31u) * 255u / 31u;
                uint32_t dst_rgb = (dr8 << 16) | (dg8 << 8) | db8;
                amostra_rgb[0] = amostra_rgb[1] = amostra_rgb[2] = amostra_rgb[3] = dst_rgb;
                g_aa_cobertura[ci] = 1;
            }
            uint32_t cor_rgb = ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | (uint32_t)b8;
            for (unsigned s = 0; s < 4; s++) if (mascara_cobertura & (1u << s)) {
                amostra[s] = cor;
                amostra_rgb[s] = cor_rgb;
            }
            uint32_t sr = 0, sg = 0, sb = 0;
            for (unsigned s = 0; s < 4; s++) {
                sr += (amostra[s] >> 11) & 31u;
                sg += (amostra[s] >> 6) & 31u;
                sb += (amostra[s] >> 1) & 31u;
            }
            r5 = (uint16_t)((sr + 2u) >> 2);
            g5 = (uint16_t)((sg + 2u) >> 2);
            b5 = (uint16_t)((sb + 2u) >> 2);
            cor = (uint16_t)((r5 << 11) | (g5 << 6) | (b5 << 1) | 1u);
        } else if ((uint32_t)x < 320u && (uint32_t)y < 240u) {
            /* Uma escrita sem cobertura torna qualquer conjunto antigo de
             * subamostras deste pixel obsoleto. */
            uint32_t ci = (uint32_t)y * 320u + (uint32_t)x;
            g_aa_cobertura[ci] = 0;
            g_aa_amostras_hires[ci] = 0;
            g_aa_z_valida[ci] = 0;
        }
        *(uint16_t*)(rdram + (off ^ 2)) = cor;
        g_triangulo_pixels++;
    }
    g_triangulos++;
    g_tarefa_tri_desenhados++;
    g_tarefa_tri_pixels += g_triangulo_pixels - pixels_antes;
    g_ultima_gfx_tem_triangulos = 1;
    anotar_triangulo_destino(g_triangulo_pixels - pixels_antes);
}

/* Recorte homogeneo contra o plano W minimo usado pelo rasterizador. Antes,
 * bastava um vertice passar atras da camera para descartar a face inteira;
 * ao cruzar a base do trono isso removia paredes e materiais por um quadro e
 * os devolvia no seguinte. Preservamos os atributos interpolados para que a
 * parte visivel da face continue com textura, Z e fog coerentes. */
static f3d_vertex_t recortar_lerp(const f3d_vertex_t* a, const f3d_vertex_t* b, float t) {
    f3d_vertex_t v = *a;
#define RECORTE_LERP(field) (a->field + (b->field - a->field) * t)
    v.cx = RECORTE_LERP(cx); v.cy = RECORTE_LERP(cy); v.cz = RECORTE_LERP(cz);
    v.cw = RECORTE_LERP(cw);
    /* Evita que arredondamento deixe o ponto de intersecao no lado rejeitado. */
    if (v.cw <= 0.01f) v.cw = 0.0101f;
    v.x = (int16_t)lrintf(RECORTE_LERP(x));
    v.y = (int16_t)lrintf(RECORTE_LERP(y));
    v.z = (int16_t)lrintf(RECORTE_LERP(z));
    v.s = (int16_t)lrintf(RECORTE_LERP(s));
    v.t = (int16_t)lrintf(RECORTE_LERP(t));
    v.r = (uint8_t)lrintf(RECORTE_LERP(r));
    v.g = (uint8_t)lrintf(RECORTE_LERP(g));
    v.b = (uint8_t)lrintf(RECORTE_LERP(b));
    v.a = (uint8_t)lrintf(RECORTE_LERP(a));
#undef RECORTE_LERP
    v.descartado_camera = 0;
    v.valido = 1;
    mtx_projetar_recorte(&v, 0);
    return v;
}

/* A primeira tentativa recortava somente W. Isto impede a face de
 * desaparecer, mas deixa a intersecao muito proxima da camera projetar para
 * fora da tela e o rasterizador preenche um leque enorme. O RSP trabalha em
 * coordenadas homogeneas: depois do plano W, tambem precisamos limitar X e Y
 * a -W..W antes da divisao de perspectiva. */
static float distancia_plano_frustum(const f3d_vertex_t* v, unsigned plano) {
    switch (plano) {
        case 0: return v->cw - 0.01f; /* plano proximo */
        case 1: return v->cx + v->cw; /* esquerda */
        case 2: return v->cw - v->cx; /* direita */
        case 3: return v->cy + v->cw; /* cima */
        default: return v->cw - v->cy; /* baixo */
    }
}

static unsigned recortar_plano_frustum(const f3d_vertex_t* entrada, unsigned nentrada,
                                       f3d_vertex_t* saida, unsigned plano) {
    unsigned nsaida = 0;
    if (!nentrada) return 0;
    for (unsigned i = 0; i < nentrada; i++) {
        const f3d_vertex_t* anterior = &entrada[(i + nentrada - 1u) % nentrada];
        const f3d_vertex_t* atual = &entrada[i];
        float da = distancia_plano_frustum(anterior, plano);
        float db = distancia_plano_frustum(atual, plano);
        int dentro_a = da >= 0.0f;
        int dentro_b = db >= 0.0f;
        if (dentro_a != dentro_b) {
            float denom = da - db;
            float t = fabsf(denom) > 0.000001f ? da / denom : 0.0f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            if (nsaida < 8u) saida[nsaida++] = recortar_lerp(anterior, atual, t);
        }
        if (dentro_b && nsaida < 8u) saida[nsaida++] = *atual;
    }
    return nsaida;
}

static unsigned recortar_frustum_visivel(const f3d_vertex_t* a, const f3d_vertex_t* b,
                                          const f3d_vertex_t* c, f3d_vertex_t* saida) {
    f3d_vertex_t buffer_a[8] = { *a, *b, *c };
    f3d_vertex_t buffer_b[8];
    f3d_vertex_t* origem = buffer_a;
    f3d_vertex_t* destino = buffer_b;
    unsigned n = 3;
    for (unsigned plano = 0; plano < 5u && n >= 3u; plano++) {
        n = recortar_plano_frustum(origem, n, destino, plano);
        f3d_vertex_t* troca = origem;
        origem = destino;
        destino = troca;
    }
    if (n >= 3u) memcpy(saida, origem, n * sizeof(*saida));
    return n;
}

static void pintar_triangulo(uint8_t* rdram, uint32_t ia, uint32_t ib, uint32_t ic) {
    if (ia >= 64 || ib >= 64 || ic >= 64 || !g_vtx[ia].valido ||
        !g_vtx[ib].valido || !g_vtx[ic].valido) {
        g_tarefa_tri_camera_recusados++;
        return;
    }
    alpha_trace_triangulo(&g_vtx[ia], &g_vtx[ib], &g_vtx[ic]);
    if (g_f3d_w_clip < 0) {
        const char* e = getenv("WPJ2_F3D_W_CLIP");
        g_f3d_w_clip = e && atoi(e) != 0;
    }
    /* A travessia final passa por dentro das faces do corredor. Rejeitar um
     * TRI1 inteiro quando apenas um vertice fica atras da camera abre buracos
     * de uma tarefa; recortamos o frustum homogeneo inteiro para preservar
     * somente a parte efetivamente visivel. Fica opt-in e limitado a 12/50
     * ate comparar a cadencia toda contra o Project64. */
    if (g_f3d_w_clip && g_alpha_trace_estado == 12 && g_alpha_trace_subestado == 50 &&
        (g_vtx[ia].descartado_camera || g_vtx[ib].descartado_camera || g_vtx[ic].descartado_camera)) {
        f3d_vertex_t saida[8];
        unsigned nsaida = recortar_frustum_visivel(&g_vtx[ia], &g_vtx[ib], &g_vtx[ic], saida);
        if (nsaida < 3u) { g_tarefa_tri_camera_recusados++; return; }
        for (unsigned i = 1; i + 1u < nsaida; i++)
            pintar_triangulo_raster(rdram, &saida[0], &saida[i], &saida[i + 1u]);
        return;
    }
    if (g_vtx[ia].descartado_camera || g_vtx[ib].descartado_camera || g_vtx[ic].descartado_camera) {
        g_tarefa_tri_camera_recusados++;
        return;
    }
    pintar_triangulo_raster(rdram, &g_vtx[ia], &g_vtx[ib], &g_vtx[ic]);
}

/* TEXRECT: o retangulo texturizado, que neste jogo e o desenho inteiro. */
static void pintar_texrect(uint8_t* rdram, uint32_t tile,
                           uint32_t ulx, uint32_t uly, uint32_t lrx, uint32_t lry,
                           int32_t s0, int32_t t0, int32_t dsdx, int32_t dtdy) {
    g_texrects++;
    if (!g_cimg_addr || g_cimg_siz != 2) { g_ignorados++; return; }
    const tile_t* T = &g_tile[tile & 7];
    g_fmt_visto[T->fmt & 7][T->siz & 3]++;
    trace_texrect_abertura(tile, T, ulx, uly, lrx, lry, s0, t0, dsdx, dtdy);

    /* Para qual buffer este sprite esta indo? Com 946 mil texels escritos e
       nenhum visivel, deduzir nao adianta - o destino tem de ser medido. */
    for (int k = 0; k < CIMG_MAX; k++) {
        if (g_destino[k].addr == g_cimg_addr) { g_destino[k].texrects++; break; }
        if (g_destino[k].addr == 0) {
            g_destino[k].addr = g_cimg_addr;
            g_destino[k].texrects = 1;
            break;
        }
    }

    /* G_CYC_COPY fecha o retangulo no pixel final. Project64 aplica esse
     * +1 antes de recortar; sem ele, a grade 5x6 da abertura deixa uma linha
     * vazia/escura no limite de cada bloco 64x32. */
    if (((g_othermode_h >> 20) & 3u) == 2u) {
        if (lrx < 0xFFFu) lrx++;
        if (lry < 0xFFFu) lry++;
    }
    uint32_t x0 = ulx < g_scis_x0 ? g_scis_x0 : ulx;
    uint32_t y0 = uly < g_scis_y0 ? g_scis_y0 : uly;
    uint32_t x1 = lrx > g_scis_x1 ? g_scis_x1 : lrx;
    uint32_t y1 = lry > g_scis_y1 ? g_scis_y1 : lry;
    /* A imagem imediatamente anterior ao corredor vem como mosaico RGBA16
     * 64x32 (ultima faixa 64x16). O vídeo do Project64 mostra-a opaca até
     * trocar para preto, apesar de o contador PRIMITIVE variar: esses lotes
     * são hand-off entre buffers, não um fade visível. */
    int mosaico_transicao = 0;
    (void)mosaico_transicao;
    if ((g_prim_color & 0xFFu) < 255u && g_othermode_l) {
        g_alpha_texrects++;
        g_alpha_rect_x0 = x0; g_alpha_rect_y0 = y0;
        g_alpha_rect_x1 = x1; g_alpha_rect_y1 = y1;
        alpha_trace_texrect(tile, T, ulx, uly, lrx, lry);
    }

    /* As tres primeiras contas ficam registradas: se o texel sai preto, a duvida
       e se o indice esta errado ou se a TMEM esta vazia, e sem os numeros as
       duas hipoteses parecem iguais. */
    if (g_texrects <= 3) {
        printf("  [tex] rect (%u,%u)-(%u,%u) tile=%u fmt=%u siz=%u linha=%u"
               " tmem=%u pal=%u uls=%u ult=%u s0=%d t0=%d dsdx=%d dtdy=%d\n",
               ulx, uly, lrx, lry, tile, T->fmt, T->siz, T->linha, T->tmem,
               T->paleta, T->uls, T->ult, s0, t0, dsdx, dtdy);
        fflush(stdout);
    }

    for (uint32_t y = y0; y < y1; y++) {
        for (uint32_t x = x0; x < x1; x++) {
            /* s e t em 10.5; dsdx e dtdy em 5.10. Em int32: se a conta der
               negativo, converter para uint32 antes vira um indice enorme que
               le TMEM zerada e devolve a entrada 0 da paleta - preto. */
            /* s/t usam 10.5 e dsdx/dtdy usam 5.10. Para somar o delta na
             * unidade de s/t, desloca-se cinco bits (10 - 5), nao dez. A
             * formula anterior fazia dsdx=1024 avancar 1/32 de texel por
             * pixel e cada retangulo repetia quase so o primeiro indice. */
            int32_t s = s0 + (int32_t)(((int64_t)(x - ulx) * dsdx) >> 5);
            int32_t tt = t0 + (int32_t)(((int64_t)(y - uly) * dtdy) >> 5);
            /* A coordenada bruta nao pode ir direto para TMEM. CMS/CMT
             * descrevem repeticao, clamp e espelho do tile: os sprites do
             * passarinho e do Gepetto usam CMS=G_TX_MIRROR com s0=2048 para
             * iniciar na segunda metade do periodo. O caminho antigo aplicava
             * essa regra somente ao vizinho do bilerp, deixando o texel
             * principal ler a faixa seguinte da TMEM. */
            int32_t si_bruto = (s >> 5) - (int32_t)(T->uls >> 2);
            int32_t ti_bruto = (tt >> 5) - (int32_t)(T->ult >> 2);
            int smax = (T->lrs >= T->uls) ? (int)((T->lrs - T->uls) >> 2) : -1;
            int tmax = (T->lrt >= T->ult) ? (int)((T->lrt - T->ult) >> 2) : -1;
            int si = tile_coord(si_bruto, smax + 1, T->masks, T->cms);
            int ti = tile_coord(ti_bruto, tmax + 1, T->maskt, T->cmt);
            if (si < 0 || ti < 0) { g_fora_do_tile++; continue; }
            int ok;
            uint16_t c = texel(T, (uint32_t)si, (uint32_t)ti, &ok);
            if (!ok) { g_fmt_sem_suporte++; return; }
            if (!c) g_indice_zero++;
            if (!(c & 1)) { g_texel_transparente++; continue; }
            /* TEXRECT e o caminho da abertura 2D. A ROM seleciona
             * G_TF_BILERP tambem aqui, mas antes ele era ignorado e cada
             * pixel escolhia um unico texel: isso deixava degraus e pequenas
             * frestas entre paineis/sprites vizinhos. Mantemos o filtro
             * somente se os quatro texels sao opacos; nas bordas de glifos e
             * sprites indexados isso evita a aureola escura que uma mistura
             * com a cor transparente criaria. s/t de TEXRECT usam 10.5. */
            if (g_tex_filter == 2u && T->lrs >= T->uls && T->lrt >= T->ult) {
                int si1 = tile_coord(((s >> 5) + 1) - (int)(T->uls >> 2),
                                     smax + 1, T->masks, T->cms);
                int ti1 = tile_coord(((tt >> 5) + 1) - (int)(T->ult >> 2),
                                     tmax + 1, T->maskt, T->cmt);
                if (si1 >= 0 && ti1 >= 0) {
                    int ok10, ok01, ok11;
                    uint16_t c10 = texel(T, (uint32_t)si1, (uint32_t)ti, &ok10);
                    uint16_t c01 = texel(T, (uint32_t)si, (uint32_t)ti1, &ok01);
                    uint16_t c11 = texel(T, (uint32_t)si1, (uint32_t)ti1, &ok11);
                    if (ok10 && ok01 && ok11 && (c10 & 1u) && (c01 & 1u) && (c11 & 1u)) {
                        uint32_t fs = (uint32_t)(s & 31), ft = (uint32_t)(tt & 31);
                        uint32_t w00 = (32u - fs) * (32u - ft), w10 = fs * (32u - ft);
                        uint32_t w01 = (32u - fs) * ft, w11 = fs * ft;
                        uint32_t r = (((c >> 11) & 31u) * w00 + ((c10 >> 11) & 31u) * w10 +
                                      ((c01 >> 11) & 31u) * w01 + ((c11 >> 11) & 31u) * w11 + 512u) >> 10;
                        uint32_t g = (((c >> 6) & 31u) * w00 + ((c10 >> 6) & 31u) * w10 +
                                      ((c01 >> 6) & 31u) * w01 + ((c11 >> 6) & 31u) * w11 + 512u) >> 10;
                        uint32_t b = (((c >> 1) & 31u) * w00 + ((c10 >> 1) & 31u) * w10 +
                                      ((c01 >> 1) & 31u) * w01 + ((c11 >> 1) & 31u) * w11 + 512u) >> 10;
                        c = (uint16_t)((r << 11) | (g << 6) | (b << 1) | 1u);
                    }
                }
            }
            g_texel_opaco++;
            if (c & 0xFFFEu) g_texel_colorido++;
            g_texel_cor[c]++;
            if (g_texrects <= 3 && x == x0 && y == y0) {
                uint32_t indice = g_tmem[T->tmem * 8 + (uint32_t)ti * T->linha * 8 + (uint32_t)si];
                printf("  [tex] amostra: si=%d ti=%d indice=0x%02X cor=0x%04X\n",
                       si, ti, indice, c);
            }
            uint32_t off = g_cimg_addr + (y * g_cimg_larg + x) * 2;
            if (off + 2 > 0x800000u) return;
            /* A transicao 8/26 nao troca a textura por um quadro preto: ela
             * varia o alpha de PRIMITIVE enquanto o blender esta ligado
             * (0F0A4004 no RDP). Copiar TEXEL0 diretamente ignorava essa
             * composicao e deixava a ultima imagem 2D opaca por varios
             * segundos. Os sprites normais usam alpha FF e continuam no
             * caminho de copia identico ao anterior. */
            uint32_t prim_a = mosaico_transicao ? 255u : (g_prim_color & 0xFFu);
            /* O compositor source-over e compartilhado por textos, sprites e
             * camadas CI8. A separacao precisa entre as animacoes 2D sera
             * feita a partir das capturas F5, sem remover o fade ja validado
             * das legendas. */
            if (prim_a < 255u && rdp_blender_source_over()) {
                uint16_t dst = *(uint16_t*)(rdram + (off ^ 2u));
                uint32_t sr = (c >> 11) & 31u, sg = (c >> 6) & 31u, sb = (c >> 1) & 31u;
                uint32_t dr = (dst >> 11) & 31u, dg = (dst >> 6) & 31u, db = (dst >> 1) & 31u;
                uint32_t inv = 255u - prim_a;
                sr = (sr * prim_a + dr * inv + 127u) / 255u;
                sg = (sg * prim_a + dg * inv + 127u) / 255u;
                sb = (sb * prim_a + db * inv + 127u) / 255u;
                c = (uint16_t)((sr << 11) | (sg << 6) | (sb << 1) | 1u);
            }
            *(uint16_t*)(rdram + (off ^ 2)) = c;
            if (x < 320u && y < 240u) {
                uint32_t ci = y * 320u + x;
                g_aa_cobertura[ci] = 0;
                g_aa_amostras_hires[ci] = 0;
                g_aa_z_valida[ci] = 0;
            }
            g_texels++;
            for (int k = 0; k < CIMG_MAX; k++)
                if (g_destino[k].addr == g_cimg_addr) { g_destino[k].texels++; break; }
        }
    }
}

/* Executa o que da para executar de uma lista de exibicao. */
static void desenhar_dl(uint8_t* rdram, uint32_t phys, int nivel) {
    if (nivel > 8) return;
    if (nivel == 0) {
        g_scis_x0 = 0; g_scis_y0 = 0; g_scis_x1 = 320; g_scis_y1 = 240;
    }
    for (uint32_t i = 0; i < 4096; i++) {
        uint32_t off = phys + i * 8;
        if (off + 8 > 0x800000u) return;
        uint32_t w0 = *(uint32_t*)(rdram + off);
        uint32_t w1 = *(uint32_t*)(rdram + off + 4);
        switch (w0 >> 24) {
            case 0xFF:                                     /* SETCIMG */
                g_cimg_addr = w1 & 0x1FFFFFFFu;
                g_cimg_larg = (w0 & 0xFFF) + 1;
                g_cimg_siz  = (w0 >> 19) & 3;
                break;
            case 0xFE: g_zimg_addr = w1 & 0x1FFFFFFFu; break; /* SETZIMG */
            case 0xF7: g_fill = w1; break;                 /* SETFILLCOLOR */
            case 0xF8: g_tarefa_fog = g_fog_color = w1; break; /* SETFOGCOLOR */
            case 0xF9: g_tarefa_blend = w1; break;          /* SETBLENDCOLOR */
            case 0xFA: g_prim_color = w1; break;           /* SETPRIMCOLOR */
            case 0xFB: g_env_color = w1; break;            /* SETENVCOLOR */
            case 0xFC:                                     /* SETCOMBINE */
                g_combine_w0 = w0; g_combine_w1 = w1;
                break;
            /* Os dois usam campos de 12 bits em 10.2, nos bits 12 e 0 - nao nos
               bits 14 e 2, que era o que eu tinha. E os cantos ficam invertidos
               entre eles: SETSCISSOR poe o canto superior em w0 e o inferior em
               w1; FILLRECT faz o contrario. */
            case 0xED:                                     /* SETSCISSOR */
                g_scis_x0 = ((w0 >> 12) & 0xFFF) >> 2;
                g_scis_y0 = ( w0        & 0xFFF) >> 2;
                g_scis_x1 = ((w1 >> 12) & 0xFFF) >> 2;
                g_scis_y1 = ( w1        & 0xFFF) >> 2;
                break;
            case 0xF6:                                     /* FILLRECT */
                pintar_retangulo(rdram,
                    ((w1 >> 12) & 0xFFF) >> 2, ( w1        & 0xFFF) >> 2,
                    (((w0 >> 12) & 0xFFF) >> 2) + 1, (( w0  & 0xFFF) >> 2) + 1);
                break;
            case 0xFD:                                     /* SETTIMG */
                g_timg_addr = resolver_endereco(w1);
                g_timg_fmt  = (w0 >> 21) & 7;
                g_timg_siz  = (w0 >> 19) & 3;
                g_timg_larg = (w0 & 0xFFF) + 1;
                break;
            case 0xF5: {                                   /* SETTILE */
                tile_t* T = &g_tile[(w1 >> 24) & 7];
                T->fmt    = (w0 >> 21) & 7;
                T->siz    = (w0 >> 19) & 3;
                T->linha  = (w0 >> 9) & 0x1FF;
                T->tmem   =  w0 & 0x1FF;
                T->paleta = (w1 >> 20) & 0xF;
                T->cmt    = (w1 >> 18) & 3;
                T->maskt  = (w1 >> 14) & 0xF;
                T->shiftt = (w1 >> 10) & 0xF;
                T->cms    = (w1 >> 8) & 3;
                T->masks  = (w1 >> 4) & 0xF;
                T->shifts = w1 & 0xF;
                break;
            }
            case 0xF2: {                                   /* SETTILESIZE */
                tile_t* T = &g_tile[(w1 >> 24) & 7];
                T->uls = (w0 >> 12) & 0xFFF;
                T->ult =  w0        & 0xFFF;
                T->lrs = (w1 >> 12) & 0xFFF;
                T->lrt =  w1        & 0xFFF;
                break;
            }
            case 0xF3: carregar_bloco(rdram, (w1 >> 24) & 7,
                                      (w1 >> 12) & 0xFFF, w1 & 0xFFF); break;   /* LOADBLOCK */
            case 0xF4: carregar_tile(rdram, (w1 >> 24) & 7,
                                      (w0 >> 12) & 0xFFF, w0 & 0xFFF,
                                      (w1 >> 12) & 0xFFF, w1 & 0xFFF); break;   /* LOADTILE */
            case 0xF0: carregar_tlut(rdram, (w1 >> 24) & 7,
                                     (w1 >> 14) & 0x3FF); break; /* LOADTLUT */
            case 0x01: {                                    /* MTX (F3DEX) */
                uint32_t src = resolver_endereco(w1);
                /* As matrizes 0x00156xxx sao identidades de camadas 2D. A
                 * transformacao dinamica da camera 3D fica no heap (>=2 MiB);
                 * registrar so ela deixa o log comparavel ao oraculo. */
                if (g_rastrear_camera_12 && nivel == 0 &&
                    ((w0 >> 16) & 0xFFu) == 0x02u && src >= 0x00200000u &&
                    src + 63 < 0x800000u) {
                    g_camera_12_amostra++;
                    printf("  [cam12] amostra=%u src=%06X m0=(%.5f %.5f %.5f)"
                           " m1=(%.5f %.5f %.5f) m2=(%.5f %.5f %.5f)"
                           " pos=(%.5f %.5f %.5f)\n",
                           g_camera_12_amostra, src,
                           (float)(int16_t)ram16(rdram, src + 0) + ram16(rdram, src + 32) / 65536.0f,
                           (float)(int16_t)ram16(rdram, src + 2) + ram16(rdram, src + 34) / 65536.0f,
                           (float)(int16_t)ram16(rdram, src + 4) + ram16(rdram, src + 36) / 65536.0f,
                           (float)(int16_t)ram16(rdram, src + 8) + ram16(rdram, src + 40) / 65536.0f,
                           (float)(int16_t)ram16(rdram, src + 10) + ram16(rdram, src + 42) / 65536.0f,
                           (float)(int16_t)ram16(rdram, src + 12) + ram16(rdram, src + 44) / 65536.0f,
                           (float)(int16_t)ram16(rdram, src + 16) + ram16(rdram, src + 48) / 65536.0f,
                           (float)(int16_t)ram16(rdram, src + 18) + ram16(rdram, src + 50) / 65536.0f,
                           (float)(int16_t)ram16(rdram, src + 20) + ram16(rdram, src + 52) / 65536.0f,
                           (float)(int16_t)ram16(rdram, src + 24) + ram16(rdram, src + 56) / 65536.0f,
                           (float)(int16_t)ram16(rdram, src + 26) + ram16(rdram, src + 58) / 65536.0f,
                           (float)(int16_t)ram16(rdram, src + 28) + ram16(rdram, src + 60) / 65536.0f);
                }
                /* Antes de ativar a pilha de matrizes, registrar o formato e
                 * as matrizes reais evita supor ordem/escala erradas para a
                 * proxima tela 3D. A matriz e 4x4 fixa 16.16: inteiros nas
                 * primeiras 32 bytes e fracoes nas 32 seguintes. */
                if (g_mtx_amostras++ < 10 && src + 63 < 0x800000u) {
                    printf("  [mtx] param=%02X src=%06X diag=%d.%04X,%d.%04X,%d.%04X,%d.%04X"
                           " trans=%d.%04X,%d.%04X,%d.%04X\n",
                           (w0 >> 16) & 0xFFu, src,
                           (int16_t)ram16(rdram, src + 0),  ram16(rdram, src + 32),
                           (int16_t)ram16(rdram, src + 10), ram16(rdram, src + 42),
                           (int16_t)ram16(rdram, src + 20), ram16(rdram, src + 52),
                           (int16_t)ram16(rdram, src + 30), ram16(rdram, src + 62),
                           (int16_t)ram16(rdram, src + 24), ram16(rdram, src + 56),
                           (int16_t)ram16(rdram, src + 26), ram16(rdram, src + 58),
                           (int16_t)ram16(rdram, src + 28), ram16(rdram, src + 60));
                    fflush(stdout);
                }
                mtx_carregar(rdram, src, (uint8_t)((w0 >> 16) & 0xFFu));
                break;
            }
            case 0x03:                                      /* MOVEMEM */
            {
                uint32_t src = resolver_endereco(w1);
                if (g_movemem_amostras++ < 10)
                    printf("  [movemem] w0=%08X src=%06X\n", w0, src);
                uint32_t indice = (w0 >> 16) & 0xFFu;
                if (indice == 0x80u) mtx_viewport(rdram, src);
                else f3d_carregar_luz(rdram, indice, src);
                break;
            }
            case 0x04:                                      /* VTX (F3DEX) */
            {
                uint32_t src = resolver_endereco(w1);
                uint32_t n = ((w0 >> 20) & 0x0Fu) + 1u;
                uint32_t v0 = (w0 >> 16) & 0x0Fu;
                for (uint32_t vi = 0; vi < n && v0 + vi < 64 &&
                                      src + vi * 16 + 15 < 0x800000u; vi++) {
                    uint32_t q = src + vi * 16;
                    f3d_vertex_t* v = &g_vtx[v0 + vi];
                    v->x = (int16_t)ram16(rdram, q);     v->y = (int16_t)ram16(rdram, q + 2);
                    v->z = (int16_t)ram16(rdram, q + 4);
                    v->s = (int16_t)ram16(rdram, q + 8); v->t = (int16_t)ram16(rdram, q + 10);
                    v->r = ram8(rdram, q + 12); v->g = ram8(rdram, q + 13);
                    v->b = ram8(rdram, q + 14); v->a = ram8(rdram, q + 15);
                    f3d_iluminar_vertice(v);
                    mtx_transformar_vertice(v);
                    v->valido = 1;
                }
                if (g_vtx_amostras++ < 8) {
                    printf("  [vtx] w0=%08X w1=%08X n=%u v0=%u src=%06X"
                           " p0=(%d,%d,%d) st=(%d,%d) rgba=%02X%02X%02X%02X\n",
                           w0, w1, n, v0, src,
                           (int16_t)ram16(rdram, src), (int16_t)ram16(rdram, src + 2),
                           (int16_t)ram16(rdram, src + 4),
                           (int16_t)ram16(rdram, src + 8), (int16_t)ram16(rdram, src + 10),
                           ram8(rdram, src + 12), ram8(rdram, src + 13),
                           ram8(rdram, src + 14), ram8(rdram, src + 15));
                    for (uint32_t vi = 1; vi < n && vi < 8 && src + vi * 16 + 15 < 0x800000u; vi++) {
                        uint32_t q = src + vi * 16;
                        printf("        p%u=(%d,%d,%d) st=(%d,%d) rgba=%02X%02X%02X%02X\n",
                               vi, (int16_t)ram16(rdram, q), (int16_t)ram16(rdram, q + 2),
                               (int16_t)ram16(rdram, q + 4), (int16_t)ram16(rdram, q + 8),
                               (int16_t)ram16(rdram, q + 10), ram8(rdram, q + 12),
                               ram8(rdram, q + 13), ram8(rdram, q + 14), ram8(rdram, q + 15));
                    }
                    fflush(stdout);
                }
                break;
            }
            case 0xBF:                                      /* TRI1 (F3DEX) */
            {
                uint32_t ia = ((w1 >> 16) & 0xFFu) / 10u;
                uint32_t ib = ((w1 >>  8) & 0xFFu) / 10u;
                uint32_t ic = ( w1        & 0xFFu) / 10u;
                pintar_triangulo(rdram, ia, ib, ic);
                if (g_tri_amostras++ < 12) {
                    const tile_t* T = &g_tile[g_texture_tile & 7u];
                    printf("  [tri] w0=%08X w1=%08X idx=(%u,%u,%u) img=%06X"
                           " fmt/siz=%u/%u tile0(fmt/siz=%u/%u line=%u tmem=%u pal=%u"
                           " ul=%u,%u lr=%u,%u)\n",
                           w0, w1, ia, ib, ic, g_timg_addr, g_timg_fmt, g_timg_siz,
                           T->fmt, T->siz, T->linha, T->tmem, T->paleta,
                           T->uls, T->ult, T->lrs, T->lrt);
                    fflush(stdout);
                }
                break;
            }
            case 0xE4: {                                   /* TEXRECT */
                /* Os dois comandos seguintes sao RDPHALF_1 e RDPHALF_2, e
                   carregam as coordenadas de textura com s0, t0, dsdx e dtdy. */
                int16_t s0 = 0, t0 = 0, dsdx = 1024, dtdy = 1024;
                if (off + 24 <= 0x800000u) {
                    s0   = (int16_t)ram16(rdram, off + 12);
                    t0   = (int16_t)ram16(rdram, off + 14);
                    dsdx = (int16_t)ram16(rdram, off + 20);
                    dtdy = (int16_t)ram16(rdram, off + 22);
                }
                pintar_texrect(rdram, (w1 >> 24) & 7,
                    ((w1 >> 12) & 0xFFF) >> 2, ( w1        & 0xFFF) >> 2,
                    (((w0 >> 12) & 0xFFF) >> 2), (( w0     & 0xFFF) >> 2),
                    s0, t0, dsdx, dtdy);
                i += 2;                                    /* consome os halves */
                break;
            }
            case 0xB8: return;                             /* ENDDL */
            case 0x06: {                                   /* DL */
                uint32_t alvo = resolver_endereco(w1);
                if (alvo && alvo < 0x800000u) desenhar_dl(rdram, alvo, nivel + 1);
                if ((w0 >> 16) & 0xFF) return;
                break;
            }
            case 0xBC: move_word(w0, w1); break;            /* MOVEWORD */
            case 0xBB:                                      /* TEXTURE */
                g_texture_tile = (uint8_t)((w0 >> 8) & 7u);
                g_texture_definida_na_tarefa = 1;
                g_texture_ligada = (uint8_t)(w0 & 1u);
                g_texture_scale_s = (uint16_t)(w1 >> 16);
                g_texture_scale_t = (uint16_t)w1;
                break;
            case 0xBD:                                      /* POPMTX */
                mtx_popar(w1);
                break;
            case 0xB6: g_geom_mode &= ~w1; break;            /* CLEARGEOMODE */
            case 0xB7: g_geom_mode |=  w1; break;            /* SETGEOMODE */
            case 0xB9: {                                   /* SETOTHERMODE_L */
                int visto = 0;
                uint32_t desloc = (w0 >> 8) & 0xFFu;
                uint32_t largura = w0 & 0xFFu;
                if (desloc < 32u && largura) {
                    if (largura > 32u - desloc) largura = 32u - desloc;
                    uint64_t mascara64 = ((1ull << largura) - 1ull) << desloc;
                    uint32_t mascara = (uint32_t)mascara64;
                    g_othermode_l = (g_othermode_l & ~mascara) | (w1 & mascara);
                }
                g_tarefa_oml_total++;
                for (unsigned mi = 0; mi < g_tarefa_oml_n; mi++)
                    if (g_tarefa_oml_w0[mi] == w0 && g_tarefa_oml_w1[mi] == w1) {
                        visto = 1;
                        break;
                    }
                if (!visto && g_tarefa_oml_n < 8) {
                    unsigned mi = g_tarefa_oml_n++;
                    g_tarefa_oml_w0[mi] = w0;
                    g_tarefa_oml_w1[mi] = w1;
                }
                break;
            }
            case 0xB3: case 0xB4:
            case 0xE6: case 0xE7: case 0xE8: case 0xE9: case 0xEE:
                break;                                     /* sem efeito visivel */
            case 0xBA: {                                   /* SETOTHERMODE_H */
                uint32_t desloc = (w0 >> 8) & 0xFFu;
                uint32_t largura = w0 & 0xFFu;
                if (desloc < 32u && largura) {
                    if (largura > 32u - desloc) largura = 32u - desloc;
                    uint64_t mascara64 = ((1ull << largura) - 1ull) << desloc;
                    uint32_t mascara = (uint32_t)mascara64;
                    g_othermode_h = (g_othermode_h & ~mascara) | (w1 & mascara);
                }
                if (desloc == 12u && largura == 2u) {
                    int forcado = tex_filter_forcado();
                    g_tex_filter = forcado >= 0 ? (uint32_t)forcado : (w1 >> 12) & 3u;
                }
                break;
            }
            default: g_ignorados++; break;
        }
    }
}

/* Grava um buffer de 320x240 em 16 bits como PPM e resume o conteudo.
   Fica aqui, e nao no runtime, porque quem sabe *quando* vale a pena fotografar
   e o rasterizador: logo depois de desenhar. */
void rsp_dump_alvo(uint8_t* rdram, const char* nome_base, uint32_t origin) {
    const uint32_t larg = 320, alt = 240;
    if (!origin || origin + larg * alt * 2 > 0x800000u) return;

    char nome[300];
    snprintf(nome, sizeof(nome), "%s.ppm", nome_base);
    FILE* f = fopen(nome, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", larg, alt);

    uint64_t nao_preto = 0;
    uint16_t vistas[32]; int nv = 0;
    for (uint32_t y = 0; y < alt; y++) {
        for (uint32_t x = 0; x < larg; x++) {
            uint16_t p = *(uint16_t*)(rdram + ((origin + (y * larg + x) * 2) ^ 2));
            fputc(((p >> 11) & 0x1F) << 3, f);
            fputc(((p >> 6)  & 0x1F) << 3, f);
            fputc(((p >> 1)  & 0x1F) << 3, f);
            if (p & 0xFFFE) nao_preto++;
            int achou = 0;
            for (int k = 0; k < nv; k++) if (vistas[k] == p) { achou = 1; break; }
            if (!achou && nv < 32) vistas[nv++] = p;
        }
    }
    fclose(f);
    printf("  [quadro] 0x%08X  %llu de %u pixels com cor, %d tom(ns) distintos"
           " -> %s\n", origin | 0x80000000u, (unsigned long long)nao_preto,
           larg * alt, nv, nome);
    fflush(stdout);
}

/* Quantos alvos de SETCIMG foram vistos, e quais. O runtime despeja um PPM de
   cada um: o VI mostra so um buffer por vez, e o que interessa saber e se
   *algum* deles tem conteudo. */
int rsp_num_alvos(void) { return g_cimg_n; }
uint32_t rsp_alvo(int i) { return (i >= 0 && i < g_cimg_n) ? g_cimg[i] : 0; }

/* Paleta corrente, para conferir se o TLUT chegou. Um TLUT zerado faz todo
   texel sair preto com alfa zero, o que e indistinguivel de "nao desenhou". */
void rsp_tlut_report(void) {
    printf("paleta (%llu LOADTLUT; 8 primeiras entradas da TMEM em 0x800):",
           (unsigned long long)g_tlut_chamadas);
    int nao_zero = 0;
    for (int i = 0; i < 256; i++) {
        uint32_t p = 0x800 + i * 2;
        if (p + 1 < TMEM_BYTES && (g_tmem[p] | g_tmem[p + 1])) nao_zero++;
    }
    for (int i = 0; i < 8; i++) {
        uint32_t p = 0x800 + i * 2;
        printf(" %04X", (g_tmem[p] << 8) | g_tmem[p + 1]);
    }
    printf("  (%d de 256 entradas nao nulas)\n", nao_zero);

    /* `josette` (Ruin0x11) documenta 96 paletas RGB5551 do jogo. Exportar a
       TLUT que o microcodigo acabou usando permite comparar bytes reais de
       execucao com essa referencia, sem inferir paleta a partir do PPM preto. */
    char nome[300];
    snprintf(nome, sizeof(nome), "%stlut.bin", g_prefixo);
    FILE* f = fopen(nome, "wb");
    if (f) {
        fwrite(g_tmem + 0x800, 1, 0x200, f);
        fclose(f);
        printf("TLUT exportada          : %s\n", nome);
    }
}

void rsp_gfx_report(const char* prefixo) {
    printf("fila de conclusoes RSP : atual=%u pico=%u/%u descartadas=%llu\n",
           g_task_done_count, g_task_done_peak, TASK_DONE_CAP,
           (unsigned long long)g_task_done_lost);
    printf("tempo backend grafico  : amostras=%llu media=%.3f ms pico=%.3f ms ultimo=%.3f ms\n",
           (unsigned long long)g_gfx_raster_samples,
           g_gfx_raster_samples ?
               (double)g_gfx_raster_total_us / (double)g_gfx_raster_samples / 1000.0 : 0.0,
           (double)g_gfx_raster_peak_us / 1000.0,
           (double)g_gfx_raster_last_us / 1000.0);
    printf("histograma backend (ms): <1=%llu <2=%llu <4=%llu <8=%llu <16=%llu <33=%llu >=33=%llu\n",
           (unsigned long long)g_gfx_raster_buckets[0],
           (unsigned long long)g_gfx_raster_buckets[1],
           (unsigned long long)g_gfx_raster_buckets[2],
           (unsigned long long)g_gfx_raster_buckets[3],
           (unsigned long long)g_gfx_raster_buckets[4],
           (unsigned long long)g_gfx_raster_buckets[5],
           (unsigned long long)g_gfx_raster_buckets[6]);
    printf("intervalos entre GFX   : <10=%llu <20=%llu <25=%llu <40=%llu >=40=%llu\n",
           (unsigned long long)g_gfx_task_interval_buckets[0],
           (unsigned long long)g_gfx_task_interval_buckets[1],
           (unsigned long long)g_gfx_task_interval_buckets[2],
           (unsigned long long)g_gfx_task_interval_buckets[3],
           (unsigned long long)g_gfx_task_interval_buckets[4]);
    printf("rasterizador            : %llu retangulo(s), %llu pixel(es) pintado(s),"
           " %llu comando(s) ignorado(s)\n", (unsigned long long)g_rects,
           (unsigned long long)g_pixels_pintados, (unsigned long long)g_ignorados);
    printf("sprites                 : %llu texrect(s), %llu texel(es) escrito(s),"
           " %llu com formato sem suporte\n", (unsigned long long)g_texrects,
           (unsigned long long)g_texels, (unsigned long long)g_fmt_sem_suporte);
    printf("profundidade TRI1       : %llu pixel(es) aceito(s), %llu oculto(s)\n",
           (unsigned long long)g_z_aceitos, (unsigned long long)g_z_recusados);
    printf("culling TRI1            : %llu frente, %llu tras\n",
           (unsigned long long)g_tri_cull_frente,
           (unsigned long long)g_tri_cull_tras);
    printf("camera F3DEX            : %llu vertice(s), %llu W~0, %llu atras, %llu fora; "
           "viewport X=%.1f..%.1f Y=%.1f..%.1f\n",
           (unsigned long long)g_mtx_vertices,
           (unsigned long long)g_mtx_cw_nulo,
           (unsigned long long)g_mtx_atras,
           (unsigned long long)g_mtx_fora_view,
           g_mtx_sx_min == 1.0e30f ? 0.0f : g_mtx_sx_min,
           g_mtx_sx_max == -1.0e30f ? 0.0f : g_mtx_sx_max,
           g_mtx_sy_min == 1.0e30f ? 0.0f : g_mtx_sy_min,
           g_mtx_sy_max == -1.0e30f ? 0.0f : g_mtx_sy_max);
    printf("segmentos F3DEX         : %llu definicao(oes)",
           (unsigned long long)g_segmentos_definidos);
    for (int i = 1; i < 16; i++)
        if (g_segmento[i]) printf(" s%d=0x%06X", i, g_segmento[i]);
    printf("\n");
    int tmem_cheia = 0;
    for (int i = 0; i < 0x800; i++) if (g_tmem[i]) tmem_cheia++;
    printf("diagnostico do texel     : %d de 2048 bytes de TMEM com dado,"
           " %llu coordenada(s) negativa(s), %llu texel(es) na entrada 0\n",
           tmem_cheia, (unsigned long long)g_fora_do_tile,
           (unsigned long long)g_indice_zero);
    printf("perspectiva de textura   : %llu pixel(es) G_TP_PERSP,"
           " %llu pixel(es) afim(ns)\n",
           (unsigned long long)g_tex_persp_pixels,
           (unsigned long long)g_tex_affine_pixels);
    printf("alfa dos texeis          : %llu opaco(s), %llu transparente(s)\n",
           (unsigned long long)g_texel_opaco,
           (unsigned long long)g_texel_transparente);
    printf("imagem dos sprites       : %s (%llu texel(es) com cor RGB)\n",
           g_texel_colorido ? "SIM" : "NAO", (unsigned long long)g_texel_colorido);
    printf("cores RGBA5551 mais usadas:\n");
    for (int rank = 0; rank < 6; rank++) {
        uint32_t melhor = 0, quant = 0;
        for (uint32_t c = 1; c < 65536; c++)
            if (g_texel_cor[c] > quant) { melhor = c; quant = g_texel_cor[c]; }
        if (!quant) break;
        printf("   0x%04X  %u texel(es)\n", melhor, quant);
        g_texel_cor[melhor] = 0;
    }
    printf("carga de textura         : %llu LOADBLOCK, %llu bytes para a TMEM,"
           " %llu recusada(s)\n", (unsigned long long)g_lb_chamadas,
           (unsigned long long)g_lb_bytes, (unsigned long long)g_lb_recusas);
    printf("fontes de LOADBLOCK (ate %d; bytes nao-zero):\n", LB_SOURCE_MAX);
    for (int i = 0; i < g_lb_source_n; i++) {
        printf("   0x%08X  %llu carga(s), %llu/%llu byte(s) nao-zero\n",
               g_lb_source[i].addr | 0x80000000u,
               (unsigned long long)g_lb_source[i].loads,
               (unsigned long long)g_lb_source[i].nao_zero,
               (unsigned long long)g_lb_source[i].bytes);
    }
    printf("fontes de TRI1 (ate %d; imagem -> tile):\n", TRI_SOURCE_MAX);
    for (int i = 0; i < g_tri_source_n; i++)
        printf("   0x%08X  %llu tri(s), imagem %u/%u tile %u/%u linha=%u tamanho=%ux%u"
               " ST=%d..%d,%d..%d vRGB=%u..%u/%u..%u/%u..%u alfa=%u..%u"
               " prim=%08X combine=%08X/%08X\n",
               g_tri_source[i].addr | 0x80000000u,
               (unsigned long long)g_tri_source[i].tris, g_tri_source[i].fmt,
               g_tri_source[i].siz, g_tri_source[i].tile_fmt,
               g_tri_source[i].tile_siz, g_tri_source[i].linha,
               (g_tri_source[i].lrs >> 2) + 1, (g_tri_source[i].lrt >> 2) + 1,
               g_tri_source[i].min_s, g_tri_source[i].max_s,
               g_tri_source[i].min_t, g_tri_source[i].max_t,
               g_tri_source[i].min_r, g_tri_source[i].max_r,
               g_tri_source[i].min_g, g_tri_source[i].max_g,
               g_tri_source[i].min_b, g_tri_source[i].max_b,
               g_tri_source[i].min_a, g_tri_source[i].max_a,
               g_tri_source[i].prim_color, g_tri_source[i].combine_w0,
               g_tri_source[i].combine_w1);
    printf("destino real dos sprites/geometria:\n");
    for (int k = 0; k < CIMG_MAX && g_destino[k].addr; k++)
        printf("   0x%08X  %llu texrect(s), %llu texel(es); %llu TRI1, %llu pixel(es) TRI1\n",
               g_destino[k].addr | 0x80000000u,
               (unsigned long long)g_destino[k].texrects,
               (unsigned long long)g_destino[k].texels,
               (unsigned long long)g_destino[k].triangulos,
               (unsigned long long)g_destino[k].pixels_triangulos);
    printf("formatos de textura usados:\n");
    static const char* nf[8] = { "RGBA", "YUV", "CI", "IA", "I", "?", "?", "?" };
    static const char* ns[4] = { "4b", "8b", "16b", "32b" };
    for (int f = 0; f < 8; f++)
        for (int s = 0; s < 4; s++)
            if (g_fmt_visto[f][s])
                printf("   %s%s  %llu texrect(s)\n", nf[f], ns[s],
                       (unsigned long long)g_fmt_visto[f][s]);
    printf("comandos graficos       : %llu em %llu lista(s), %llu cortadas por"
           " profundidade\n", (unsigned long long)g_gfx_cmds,
           (unsigned long long)g_gfx_listas, (unsigned long long)g_gfx_profundas);
    printf("quadros descartados F11 : %llu\n",
           (unsigned long long)g_gfx_fast_skipped);
    printf("tamanho das listas GFX  : %u..%u bytes\n",
           g_gfx_lista_min == UINT32_MAX ? 0 : g_gfx_lista_min, g_gfx_lista_max);
    printf("alvos de desenho (SETCIMG): ");
    for (int i = 0; i < g_cimg_n; i++) printf("0x%08X ", g_cimg[i] | 0x80000000u);
    printf("%s\n", g_cimg_n ? "" : "(nenhum)");

    printf("opcodes usados pelo jogo:\n");
    for (int op = 0; op < 256; op++) {
        if (!g_gfx_op[op]) continue;
        const char* n = nome_gfx((uint8_t)op);
        printf("   0x%02X %-16s %llu\n", op, n ? n : "DESCONHECIDO",
               (unsigned long long)g_gfx_op[op]);
    }
    (void)prefixo;
}

/* Comandos da ABI de audio. Cada um ocupa 8 bytes: o byte mais alto da primeira
   palavra e o codigo, o resto sao parametros. */
static const char* k_acmd[] = {
    "SPNOOP", "ADPCM", "CLEARBUFF", "ENVMIXER", "LOADBUFF", "RESAMPLE",
    "SAVEBUFF", "SEGMENT", "SETBUFF", "SETVOL", "DMEMMOVE", "LOADADPCM",
    "MIXER", "INTERLEAVE", "POLEF", "SETLOOP",
};
#define ACMD_COUNT ((int)(sizeof(k_acmd) / sizeof(k_acmd[0])))

/* Percorre a lista de comandos e conta o que aparece. Os que interessam sao os
   que tocam a RDRAM: SAVEBUFF escreve o resultado que o jogo vai ler, LOADBUFF e
   ADPCM leem material de origem. Sem microcodigo, nenhum deles acontece - e e
   por isso que o jogo encontra um ponteiro nulo logo depois. */
static void dump_acmd_list(uint8_t* rdram, uint32_t phys, uint32_t bytes) {
    if (!phys || bytes < 8 || bytes > 0x8000) return;
    int counts[ACMD_COUNT + 1];
    int interesting = 0;
    memset(counts, 0, sizeof(counts));

    uint32_t n = bytes / 8;
    printf("  [acmd] %u comandos em 0x%08X:\n", n, phys | 0x80000000u);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t w0 = *(uint32_t*)(rdram + phys + i * 8);
        uint32_t w1 = *(uint32_t*)(rdram + phys + i * 8 + 4);
        int op = (int)(w0 >> 24);
        counts[op < ACMD_COUNT ? op : ACMD_COUNT]++;
        if (i < 12 || (i >= 56 && i <= 70) || op == 14 || ((op == 4 || op == 6 || op == 8 || op == 12 || op == 13) && interesting++ < 20)) {
            printf("  [acmd]   %2u  %-11s w0=0x%08X w1=0x%08X\n", i,
                   op < ACMD_COUNT ? k_acmd[op] : "?", w0, w1);
        }
    }
    printf("  [acmd] resumo:");
    for (int op = 0; op < ACMD_COUNT; op++) {
        if (counts[op]) printf("  %s x%d", k_acmd[op], counts[op]);
    }
    if (counts[ACMD_COUNT]) printf("  desconhecidos x%d", counts[ACMD_COUNT]);
    printf("\n");
    fflush(stdout);
}

/* Interpretador minimo da ABI de audio.
 *
 * Nao sintetiza som nenhum: executa apenas os comandos que *movem dados*, que
 * sao os que o jogo consegue observar. O resto e ignorado de proposito, e o
 * efeito disso e silencio - a DMEM permanece zerada onde a sintese escreveria.
 *
 * A diferenca em relacao a nao fazer nada e grande: o jogo le os buffers que o
 * SAVEBUFF preenche. Sem eles, ele encontra memoria nunca escrita.
 */
static uint64_t g_acmd_run = 0, g_acmd_saved = 0, g_acmd_loaded = 0;
/* A lista completa e deliberadamente despejada apenas nas primeiras tarefas,
 * para o log nao crescer sem limite. Esta contagem acumulada preserva a
 * informacao decisiva: se o sintetizador entrou em cena mais tarde. */
static uint64_t g_acmd_op[16];
/* Medicao do SETBUFF: quantos comandos cairam fora dos ramos 0x00/0x08 e
 * quais valores de flags apareceram (mapa de bits dos 32 primeiros). */
static uint64_t g_setbuff_ignorado;
static uint32_t g_setbuff_flags_vistos;
static unsigned g_setbuff_aviso;
/* Censo de flags por opcode em toda a execucao. O dump [acmd] so dava as
 * primeiras tarefas, que nao tem vozes, e por isso nunca mostrou as listas
 * musicais. Reportar cada par (opcode, flags) na primeira ocorrencia cobre a
 * execucao inteira, sai no log mesmo se o processo for terminado a forca, e
 * custa uma linha por combinacao distinta. */
static uint8_t g_flags_vistos[16][256];
/* Quantas vezes cada destino do ENVMIXER foi realmente configurado. Se o
 * ramo A_AUX do SETBUFF nunca rodar, dry_r/wet_l/wet_r ficam em zero e o
 * ENVMIXER escreve o canal direito no offset 0 da DMEM. */
static uint64_t g_env_destino_dry_l, g_env_destino_aux;
static uint32_t g_acmd_load_peak = 0, g_acmd_mix_peak = 0, g_acmd_save_peak = 0;
static unsigned g_acmd_mix_trace = 0;
static unsigned g_acmd_load_trace = 0;
/* LOADADPCM carrega um livro de 8 preditores, com 16 coeficientes cada.
 * A lista curta de efeitos usava apenas 0x20 bytes e escondia o problema;
 * a musica carrega os 0x80 bytes completos. */
static int16_t g_pole_coeff[128];
static uint32_t g_env_dry_l, g_env_dry_r, g_env_wet_l, g_env_wet_r;
/* Estado da ABI de audio padrao (Audio ABI / ABI1).  A ROM usa esta ABI:
 * SETBUFF aponta para buffers relativos a 0x5C0 e ADPCM/RESAMPLE guardam
 * historicos completos na RDRAM entre uma AList e outra. */
static int16_t g_env_dry, g_env_wet;
static int16_t g_env_vol_l, g_env_vol_r;
static int16_t g_env_target_l, g_env_target_r;
static int32_t g_env_rate_l, g_env_rate_r;
static uint32_t g_audio_segment[16], g_audio_loop;
static int g_acmd_fast = -1;
static int g_acmd_bypass_polef = -1;
static int g_acmd_no_wet = -1;
static int g_acmd_dry_only = -1;
static int g_acmd_music_only = -1;
static int g_acmd_native_rsp = -1;
static int g_acmd_force_hle = 0;
static unsigned g_acmd_musical_tasks = 0;
static int g_acmd_capture_requested = 0;
static int g_acmd_capture_task = -2;
static unsigned g_acmd_audio_tasks = 0;

void rsp_request_audio_state_capture(void) {
    g_acmd_capture_requested = 1;
}

static int acmd_capture_task_target(void) {
    if (g_acmd_capture_task == -2) {
        const char* text = getenv("WPJ2_AUDIO_STATE_TASK");
        g_acmd_capture_task = (text && *text) ? atoi(text) : -1;
    }
    return g_acmd_capture_task;
}
/* Isolamento de voz para diagnostico de chiado. A faixa audivel no N64 e
 * formada quando ENVMIXER soma cada voz aos buses seco/auxiliar; o identificador
 * persistente da voz e o bloco de estado em RDRAM, nao a ordem bruta dos
 * comandos (a mesma voz pode aparecer varias vezes na AList). -1 conserva a
 * mistura completa; 0..N deixam apenas uma voz chegar aos buses. */
static int g_acmd_voice = -2;
#define ACMD_VOICE_MAX 32
static uint32_t g_acmd_voice_state[ACMD_VOICE_MAX];
static uint32_t g_acmd_voice_count;

uint64_t rsp_acmd_run(void)   { return g_acmd_run; }
uint64_t rsp_bytes_saved(void){ return g_acmd_saved; }
uint32_t rsp_acmd_load_peak(void) { return g_acmd_load_peak; }
uint32_t rsp_acmd_mix_peak(void) { return g_acmd_mix_peak; }
uint32_t rsp_acmd_save_peak(void) { return g_acmd_save_peak; }
uint64_t rsp_setbuff_ignorado(void) { return g_setbuff_ignorado; }
uint32_t rsp_setbuff_flags_vistos(void) { return g_setbuff_flags_vistos; }
uint64_t rsp_acmd_opcode(uint32_t opcode) {
    return opcode < 16 ? g_acmd_op[opcode] : 0;
}

static void acmd_peak(const uint8_t* p, uint32_t n, uint32_t* peak) {
    for (uint32_t i = 0; i + 1 < n; i += 2) {
        /* N64Recomp mantem palavras em ordem nativa e aplica XOR 2 a LH/SH.
           A DMEM copiada pelo DMA conserva exatamente esse layout. */
        int32_t s = *(const int16_t*)((uintptr_t)(p + i) ^ 2u);
        uint32_t mag = (uint32_t)(s < 0 ? -s : s);
        if (mag > *peak) *peak = mag;
    }
}

static int16_t acmd_read_s16(const uint8_t* p) {
    return *(const int16_t*)((uintptr_t)p ^ 2u);
}
static void acmd_write_s16(uint8_t* p, int32_t v) {
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    *(int16_t*)((uintptr_t)p ^ 2u) = (int16_t)v;
}

static uint8_t acmd_read_u8(const uint8_t* p) {
    return *(const uint8_t*)((uintptr_t)p ^ 3u);
}

static uint32_t acmd_align(uint32_t n, uint32_t a) {
    return (n + a - 1u) & ~(a - 1u);
}
static int16_t acmd_clamp16(int64_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}
/* O acumulador do ucode e de 32 bits. Os filtros ADPCM/RESAMPLE/POLEF contam
 * com o transbordo em complemento de dois antes do shift final; promovê-los a
 * 64 bits parece mais seguro, mas altera a assinatura sonora dos picos. */
static int32_t acmd_mac32(int32_t accum, int32_t a, int32_t b) {
    return (int32_t)((uint32_t)accum + (uint32_t)(a * b));
}
static uint32_t acmd_address(uint32_t segmented) {
    uint32_t seg = (segmented >> 24) & 0x3Fu;
    uint32_t off = segmented & 0xFFFFFFu;
    return seg < 16 ? ((g_audio_segment[seg] + off) & 0x1FFFFFFFu) : off;
}

/* Extracao opt-in do IMEM de audio para validar o caminho RSPRecomp. A RDRAM
 * do runtime fica em ordem de palavras do host, portanto o arquivo e escrito
 * em bytes logicos big-endian, exatamente como uma ROM .z64. */
static void acmd_dump_ucode_once(uint8_t* rdram) {
    static int dumped;
    const char* path = getenv("WPJ2_RSP_UCODE_DUMP");
    const char* source_path = getenv("WPJ2_RSP_UCODE_SOURCE_DUMP");
    const char* boot_path = getenv("WPJ2_RSP_BOOT_DUMP");
    if (dumped || !path || !*path) return;
    /* Esta ROM reaproveita o microcodigo: nas ALists de audio, ucode_size e
     * zero e o codigo efetivamente executavel ja esta no IMEM. Capturar a
     * RDRAM a partir do ponteiro da OSTask produz apenas a antiga imagem de
     * carga, que pode nem ser a que esta ativa. */
    FILE* f = fopen(path, "wb");
    if (!f) return;
    for (uint32_t i = 0; i < 0x1000u; i++) fputc(g_spmem[(0x1000u + i) ^ 3u], f);
    fclose(f);
    /* Diagnostico complementar: quando a OSTask reaproveita IMEM, manter a
     * imagem de origem separada permite determinar onde termina o codigo e
     * comecam as tabelas, sem confundi-la com o IMEM ainda nao modelado. */
    if (source_path && *source_path) {
        uint32_t source = sp_word(TASK_OFFSET + 0x10) & 0x1FFFFFFFu;
        if (source && source + 0x1000u <= 0x800000u &&
            (f = fopen(source_path, "wb")) != NULL) {
            for (uint32_t i = 0; i < 0x1000u; i++) fputc(rdram[(source + i) ^ 3u], f);
            fclose(f);
        }
    }
    if (boot_path && *boot_path) {
        uint32_t boot = sp_word(TASK_OFFSET + 0x08) & 0x1FFFFFFFu;
        uint32_t boot_size = sp_word(TASK_OFFSET + 0x0C);
        if (boot && boot_size && boot_size <= 0x1000u && boot + boot_size <= 0x800000u &&
            (f = fopen(boot_path, "wb")) != NULL) {
            for (uint32_t i = 0; i < boot_size; i++) fputc(rdram[(boot + i) ^ 3u], f);
            fclose(f);
        }
    }
    dumped = 1;
}

/* Comparacao direta com a sonda JS do Project64. O debugger captura a segunda
 * AList com ENVMIX antes de o RSP executa-la; nela os historicos das cinco
 * vozes ja foram produzidos pela passagem anterior. Gravamos a mesma entrada
 * logica N64, convertendo o layout de palavras trocadas do runtime de volta
 * para bytes sequenciais. E opt-in e ocorre uma unica vez. */
static void acmd_write_logical(FILE* f, const uint8_t* src, uint32_t bytes) {
    for (uint32_t i = 0; i < bytes; i++) fputc(src[i ^ 3u], f);
}

static void acmd_capture_state_dir(uint8_t* rdram, uint32_t phys, uint32_t bytes,
                                   const char* dir, const char* label) {
    /* A mesma rotina atende ao HLE C e ao RSP recompilado. Este ultimo usa
       uma variavel separada para que a captura de referencia nao altere o
       caminho normal de instrumentacao. */
    if (!dir || !*dir || bytes < 8 || bytes > 0x8000u) return;
    char task_dir[MAX_PATH], path[MAX_PATH];
    snprintf(task_dir, sizeof(task_dir), "%s\\%s", dir, label);
    CreateDirectoryA(task_dir, NULL);
    uint32_t segs[16];
    memcpy(segs, g_audio_segment, sizeof(segs));
    snprintf(path, sizeof(path), "%s\\alist.bin", task_dir);
    FILE* f = fopen(path, "wb");
    if (f) { acmd_write_logical(f, rdram + phys, bytes); fclose(f); }
    unsigned adpcm = 0, resample = 0, envmix = 0;
    uint32_t seen_adpcm[8] = {0}, seen_resample[8] = {0}, seen_envmix[8] = {0};
    for (uint32_t i = 0; i < bytes / 8; i++) {
        uint32_t w0 = *(uint32_t*)(rdram + phys + i * 8);
        uint32_t w1 = *(uint32_t*)(rdram + phys + i * 8 + 4);
        uint32_t op = w0 >> 24;
        if (op == 7) { segs[(w0 >> 16) & 15u] = w1 & 0x1FFFFFFFu; continue; }
        uint32_t segment = (w1 >> 24) & 0x3Fu;
        uint32_t state = (segment < 16 ? segs[segment] : 0) + (w1 & 0xFFFFFFu);
        uint32_t* seen = NULL; unsigned* count = NULL; uint32_t size = 0; const char* type = NULL;
        if (op == 1) { seen = seen_adpcm; count = &adpcm; size = 32; type = "adpcm"; }
        else if (op == 5) { seen = seen_resample; count = &resample; size = 10; type = "resample"; }
        else if (op == 3) { seen = seen_envmix; count = &envmix; size = 80; type = "envmix"; }
        if (!seen || *count >= 8 || state + size > 0x800000u) continue;
        int duplicate = 0;
        for (unsigned j = 0; j < *count; j++) if (seen[j] == state) duplicate = 1;
        if (duplicate) continue;
        snprintf(path, sizeof(path), "%s\\%s_%u_%08X.bin", task_dir, type, *count, state);
        f = fopen(path, "wb");
        if (f) { acmd_write_logical(f, rdram + state, size); fclose(f); }
        seen[(*count)++] = state;
    }
}

/* Traço profundo sem perturbar a cadência.
 *
 * Abrir milhares de arquivos de estado durante a música muda o escalonamento
 * que se deseja medir. Este formato grava em um único CSV somente FNV-1a da
 * AList e dos estados persistentes anteriores a ela. O conteúdo bruto pode
 * ser coletado depois, apenas na primeira tarefa divergente. */
static uint64_t acmd_fnv1a_logical(const uint8_t* src, uint32_t bytes) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (uint32_t i = 0; i < bytes; ++i) {
        hash ^= src[i ^ 3u];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int acmd_seen_state(uint32_t* states, unsigned count, uint32_t state) {
    for (unsigned i = 0; i < count; ++i) if (states[i] == state) return 1;
    return 0;
}

static void acmd_deep_hash_trace(uint8_t* rdram, uint32_t phys, uint32_t bytes,
                                 unsigned task_index) {
    static FILE* out;
    static int initialized;
    static unsigned rows_written;
    if (!initialized) {
        initialized = 1;
        const char* path = getenv("WPJ2_NATIVE_AUDIO_DEEP_TRACE");
        if (path && *path) out = fopen(path, "wb");
        if (out) {
            setvbuf(out, NULL, _IOFBF, 64u * 1024u);
            fprintf(out, "task,bytes,alist_fnv,adpcm,resample,envmix\n");
            fflush(out);
        }
    }
    if (!out) return;

    uint32_t segs[16];
    memcpy(segs, g_audio_segment, sizeof(segs));
    uint32_t adpcm[8] = {0}, resample[8] = {0}, envmix[8] = {0};
    unsigned n_adpcm = 0, n_resample = 0, n_envmix = 0;
    for (uint32_t i = 0; i < bytes / 8u; ++i) {
        uint32_t w0 = *(uint32_t*)(rdram + phys + i * 8u);
        uint32_t w1 = *(uint32_t*)(rdram + phys + i * 8u + 4u);
        uint32_t op = w0 >> 24;
        if (op == 7u) {
            segs[(w0 >> 16) & 15u] = w1 & 0x1FFFFFFFu;
            continue;
        }
        uint32_t segment = (w1 >> 24) & 0x3Fu;
        uint32_t state = (segment < 16u ? segs[segment] : 0u) + (w1 & 0xFFFFFFu);
        uint32_t* states = NULL;
        unsigned* count = NULL;
        uint32_t size = 0;
        if (op == 1u) { states = adpcm; count = &n_adpcm; size = 32u; }
        else if (op == 5u) { states = resample; count = &n_resample; size = 10u; }
        else if (op == 3u) { states = envmix; count = &n_envmix; size = 80u; }
        if (!states || *count >= 8u || state + size > 0x800000u ||
            acmd_seen_state(states, *count, state)) continue;
        states[(*count)++] = state;
    }

    fprintf(out, "%u,%u,%016llX,", task_index, bytes,
            (unsigned long long)acmd_fnv1a_logical(rdram + phys, bytes));
    const uint32_t* groups[] = { adpcm, resample, envmix };
    const unsigned counts[] = { n_adpcm, n_resample, n_envmix };
    const uint32_t sizes[] = { 32u, 10u, 80u };
    for (unsigned group = 0; group < 3u; ++group) {
        for (unsigned i = 0; i < counts[group]; ++i) {
            uint32_t state = groups[group][i];
            fprintf(out, "%s%08X=%016llX", i ? "/" : "", state,
                    (unsigned long long)acmd_fnv1a_logical(
                        rdram + state, sizes[group]));
        }
        fprintf(out, "%s", group == 2u ? "\n" : ",");
    }
    /* Uma sincronização a cada ~0,5 s mantém o arquivo recuperável mesmo se
       a janela for encerrada pelo botão X, sem I/O por tarefa. */
    if ((++rows_written & 15u) == 0u) fflush(out);
}

/* Sonda de continuidade dos históricos de áudio.
 *
 * O replay offline demonstrou que o RSP recompilado produz o PCM correto
 * quando recebe a mesma memória do Project64. A pergunta restante é onde a
 * execução ao vivo perde essa memória. Para cada estado persistente usado por
 * ADPCM, RESAMPLE e ENVMIXER, guardamos o valor logo após uma tarefa e o
 * confrontamos com o valor logo antes da próxima tarefa que reutiliza o mesmo
 * endereço. Uma diferença nesse intervalo não foi causada pelo RSP atual.
 *
 * O CSV é bufferizado e os dados brutos são gravados somente nas primeiras
 * oito mudanças entre tarefas. Assim uma rodada responde várias hipóteses sem
 * despejar milhares de arquivos nem mudar sensivelmente a cadência. */
#define ACMD_PROBE_CURRENT_MAX 24u
#define ACMD_PROBE_TRACKED_MAX 128u
#define ACMD_PROBE_RAW_MAX 80u
#define ACMD_PROBE_PCM_RING 16u
#define ACMD_PROBE_CAPTURE_MAX 8u

typedef struct {
    uint32_t address, size, previous_task, capture_index;
    uint8_t type;
    uint64_t previous_hash, before_hash;
    int had_previous, changed_between;
} acmd_probe_current_t;

typedef struct {
    uint32_t address, size, last_task;
    uint8_t type, valid, raw[ACMD_PROBE_RAW_MAX];
    uint64_t last_hash;
} acmd_probe_tracked_t;

typedef struct {
    uint32_t task, address, bytes;
    uint64_t hash;
    int valid, consumed;
} acmd_probe_pcm_t;

static int g_acmd_probe_initialized, g_acmd_probe_enabled;
static char g_acmd_probe_dir[MAX_PATH];
static FILE* g_acmd_probe_states;
static FILE* g_acmd_probe_pcm;
static unsigned g_acmd_probe_rows, g_acmd_probe_captures;
static int g_acmd_probe_baseline_captured;
static uint32_t g_acmd_probe_baseline_task, g_acmd_probe_suspect_task;
static int g_acmd_probe_suspect_captured;
static acmd_probe_current_t g_acmd_probe_current[ACMD_PROBE_CURRENT_MAX];
static unsigned g_acmd_probe_current_count;
static uint32_t g_acmd_probe_current_task;
static uint64_t g_acmd_probe_current_alist;
static acmd_probe_tracked_t g_acmd_probe_tracked[ACMD_PROBE_TRACKED_MAX];
static unsigned g_acmd_probe_tracked_count;
static acmd_probe_pcm_t g_acmd_probe_pcm_ring[ACMD_PROBE_PCM_RING];
static unsigned g_acmd_probe_pcm_head;

static const char* acmd_probe_type_name(uint8_t type) {
    switch (type) {
        case 1: return "adpcm";
        case 2: return "resample";
        case 3: return "envmix";
        default: return "unknown";
    }
}

static void acmd_probe_init(void) {
    if (g_acmd_probe_initialized) return;
    g_acmd_probe_initialized = 1;
    const char* dir = getenv("WPJ2_AUDIO_FIRST_DIVERGENCE_DIR");
    if (!dir || !*dir) return;
    strncpy(g_acmd_probe_dir, dir, sizeof(g_acmd_probe_dir) - 1u);
    g_acmd_probe_dir[sizeof(g_acmd_probe_dir) - 1u] = 0;
    CreateDirectoryA(g_acmd_probe_dir, NULL);
    char path[MAX_PATH + 48];
    snprintf(path, sizeof(path), "%s\\state_continuity.csv", g_acmd_probe_dir);
    g_acmd_probe_states = fopen(path, "wb");
    snprintf(path, sizeof(path), "%s\\pcm_lifetime.csv", g_acmd_probe_dir);
    g_acmd_probe_pcm = fopen(path, "wb");
    if (!g_acmd_probe_states || !g_acmd_probe_pcm) {
        if (g_acmd_probe_states) fclose(g_acmd_probe_states);
        if (g_acmd_probe_pcm) fclose(g_acmd_probe_pcm);
        g_acmd_probe_states = g_acmd_probe_pcm = NULL;
        return;
    }
    setvbuf(g_acmd_probe_states, NULL, _IOFBF, 64u * 1024u);
    setvbuf(g_acmd_probe_pcm, NULL, _IOFBF, 16u * 1024u);
    fprintf(g_acmd_probe_states,
            "task,alist_fnv,result,type,address,bytes,previous_task,previous_after_fnv,before_fnv,after_fnv,changed_between,changed_by_rsp,capture\n");
    fprintf(g_acmd_probe_pcm,
            "stage,task,address,bytes,rsp_after_fnv,observed_fnv,changed_or_unmatched\n");
    fflush(g_acmd_probe_states);
    fflush(g_acmd_probe_pcm);
    g_acmd_probe_enabled = 1;
}

static acmd_probe_tracked_t* acmd_probe_find_tracked(uint8_t type,
                                                     uint32_t address,
                                                     uint32_t size) {
    for (unsigned i = 0; i < g_acmd_probe_tracked_count; ++i) {
        acmd_probe_tracked_t* item = &g_acmd_probe_tracked[i];
        if (item->type == type && item->address == address && item->size == size)
            return item;
    }
    if (g_acmd_probe_tracked_count >= ACMD_PROBE_TRACKED_MAX) return NULL;
    acmd_probe_tracked_t* item = &g_acmd_probe_tracked[g_acmd_probe_tracked_count++];
    memset(item, 0, sizeof(*item));
    item->type = type;
    item->address = address;
    item->size = size;
    return item;
}

static int acmd_probe_current_seen(uint8_t type, uint32_t address) {
    for (unsigned i = 0; i < g_acmd_probe_current_count; ++i)
        if (g_acmd_probe_current[i].type == type &&
            g_acmd_probe_current[i].address == address) return 1;
    return 0;
}

static void acmd_probe_write_logical_file(const char* path, const uint8_t* data,
                                          uint32_t bytes) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    acmd_write_logical(f, data, bytes);
    fclose(f);
}

static void acmd_probe_capture_between(uint8_t* rdram, uint32_t phys,
                                       uint32_t alist_bytes,
                                       acmd_probe_current_t* current,
                                       const acmd_probe_tracked_t* tracked) {
    if (g_acmd_probe_captures >= ACMD_PROBE_CAPTURE_MAX) return;
    unsigned capture = ++g_acmd_probe_captures;
    current->capture_index = capture;
    char dir[MAX_PATH + 64], path[MAX_PATH + 96];
    snprintf(dir, sizeof(dir), "%s\\div_%03u_task_%u_%s_%08X",
             g_acmd_probe_dir, capture, g_acmd_probe_current_task,
             acmd_probe_type_name(current->type), current->address);
    CreateDirectoryA(dir, NULL);
    snprintf(path, sizeof(path), "%s\\alist.bin", dir);
    acmd_probe_write_logical_file(path, rdram + phys, alist_bytes);
    snprintf(path, sizeof(path), "%s\\expected_after_previous.bin", dir);
    acmd_probe_write_logical_file(path, tracked->raw, current->size);
    snprintf(path, sizeof(path), "%s\\before_current.bin", dir);
    acmd_probe_write_logical_file(path, rdram + current->address, current->size);
    snprintf(path, sizeof(path), "%s\\metadata.txt", dir);
    FILE* f = fopen(path, "wb");
    if (f) {
        fprintf(f, "task=%u\nprevious_task=%u\ntype=%s\naddress=%08X\nbytes=%u\n",
                g_acmd_probe_current_task, tracked->last_task,
                acmd_probe_type_name(current->type), current->address,
                current->size);
        fprintf(f, "previous_after_fnv=%016llX\nbefore_fnv=%016llX\n",
                (unsigned long long)tracked->last_hash,
                (unsigned long long)current->before_hash);
        fclose(f);
    }
}

static void acmd_probe_capture_baseline(uint8_t* rdram, uint32_t phys,
                                        uint32_t alist_bytes) {
    if (g_acmd_probe_baseline_captured || !g_acmd_probe_current_count) return;
    g_acmd_probe_baseline_captured = 1;
    g_acmd_probe_baseline_task = g_acmd_probe_current_task;
    /* Na sequência alinhada mais recente, a primeira diferença é produzida
     * pela décima AList depois da inicial: local 43 / Project64 58. Usar a
     * distância relativa tolera o deslizamento do índice absoluto. */
    g_acmd_probe_suspect_task = g_acmd_probe_baseline_task + 10u;
    char dir[MAX_PATH + 64], path[MAX_PATH + 112];
    snprintf(dir, sizeof(dir), "%s\\baseline_task_%u", g_acmd_probe_dir,
             g_acmd_probe_current_task);
    CreateDirectoryA(dir, NULL);
    snprintf(path, sizeof(path), "%s\\alist.bin", dir);
    acmd_probe_write_logical_file(path, rdram + phys, alist_bytes);
    for (unsigned i = 0; i < g_acmd_probe_current_count; ++i) {
        acmd_probe_current_t* current = &g_acmd_probe_current[i];
        snprintf(path, sizeof(path), "%s\\before_%s_%08X.bin", dir,
                 acmd_probe_type_name(current->type), current->address);
        acmd_probe_write_logical_file(path, rdram + current->address,
                                      current->size);
    }
    snprintf(path, sizeof(path), "%s\\metadata.txt", dir);
    FILE* f = fopen(path, "wb");
    if (f) {
        fprintf(f, "task=%u\nalist_bytes=%u\nalist_fnv=%016llX\nstates=%u\n",
                g_acmd_probe_current_task, alist_bytes,
                (unsigned long long)g_acmd_probe_current_alist,
                g_acmd_probe_current_count);
        fclose(f);
    }
}

static void acmd_probe_capture_suspect_before(uint8_t* rdram, uint32_t phys,
                                               uint32_t alist_bytes) {
    if (g_acmd_probe_suspect_captured || !g_acmd_probe_suspect_task ||
        g_acmd_probe_current_task != g_acmd_probe_suspect_task) return;
    g_acmd_probe_suspect_captured = 1;
    char dir[MAX_PATH + 64], path[MAX_PATH + 112];
    snprintf(dir, sizeof(dir), "%s\\suspect_task_%u", g_acmd_probe_dir,
             g_acmd_probe_current_task);
    CreateDirectoryA(dir, NULL);
    snprintf(path, sizeof(path), "%s\\alist.bin", dir);
    acmd_probe_write_logical_file(path, rdram + phys, alist_bytes);
    /* O oráculo da tarefa 58 preserva cada LOADBUFF/LOADADPCM. A RDRAM
     * integral permite extrair os mesmos intervalos localmente depois da
     * execução, sem abrir dezenas de arquivos no instante crítico. */
    snprintf(path, sizeof(path), "%s\\rdram_before.bin", dir);
    acmd_probe_write_logical_file(path, rdram, 0x800000u);
    for (unsigned i = 0; i < g_acmd_probe_current_count; ++i) {
        acmd_probe_current_t* current = &g_acmd_probe_current[i];
        snprintf(path, sizeof(path), "%s\\before_%s_%08X.bin", dir,
                 acmd_probe_type_name(current->type), current->address);
        acmd_probe_write_logical_file(path, rdram + current->address,
                                      current->size);
    }
    snprintf(path, sizeof(path), "%s\\metadata.txt", dir);
    FILE* f = fopen(path, "wb");
    if (f) {
        fprintf(f,
                "baseline_task=%u\nsuspect_task=%u\nalist_bytes=%u\n"
                "alist_fnv=%016llX\nstates=%u\n",
                g_acmd_probe_baseline_task, g_acmd_probe_current_task,
                alist_bytes, (unsigned long long)g_acmd_probe_current_alist,
                g_acmd_probe_current_count);
        fclose(f);
    }
}

static void acmd_probe_before(uint8_t* rdram, uint32_t phys, uint32_t bytes,
                              uint32_t task) {
    acmd_probe_init();
    if (!g_acmd_probe_enabled) return;
    g_acmd_probe_current_count = 0;
    g_acmd_probe_current_task = task;
    g_acmd_probe_current_alist = acmd_fnv1a_logical(rdram + phys, bytes);
    uint32_t segs[16] = {0};
    for (uint32_t i = 0; i < bytes / 8u; ++i) {
        uint32_t w0 = *(uint32_t*)(rdram + phys + i * 8u);
        uint32_t w1 = *(uint32_t*)(rdram + phys + i * 8u + 4u);
        uint32_t op = w0 >> 24;
        if (op == 7u) {
            segs[(w0 >> 16) & 15u] = w1 & 0x1FFFFFFFu;
            continue;
        }
        uint8_t type = 0;
        uint32_t size = 0;
        if (op == 1u) { type = 1; size = 32u; }
        else if (op == 5u) { type = 2; size = 10u; }
        else if (op == 3u) { type = 3; size = 80u; }
        if (!type || g_acmd_probe_current_count >= ACMD_PROBE_CURRENT_MAX) continue;
        uint32_t segment = (w1 >> 24) & 0x3Fu;
        uint32_t address = (((segment < 16u ? segs[segment] : 0u) +
                             (w1 & 0xFFFFFFu)) & 0x1FFFFFFFu);
        if (!address || address + size > 0x800000u ||
            acmd_probe_current_seen(type, address)) continue;
        acmd_probe_current_t* current =
            &g_acmd_probe_current[g_acmd_probe_current_count++];
        memset(current, 0, sizeof(*current));
        current->type = type;
        current->address = address;
        current->size = size;
        current->before_hash = acmd_fnv1a_logical(rdram + address, size);
        acmd_probe_tracked_t* tracked = acmd_probe_find_tracked(type, address, size);
        if (tracked && tracked->valid) {
            current->had_previous = 1;
            current->previous_task = tracked->last_task;
            current->previous_hash = tracked->last_hash;
            current->changed_between = current->before_hash != tracked->last_hash;
            if (current->changed_between)
                acmd_probe_capture_between(rdram, phys, bytes, current, tracked);
        }
    }
    /* A primeira AList musical é a fronteira mais importante: nesta rodada os
     * estados iniciais ainda eram idênticos aos do Project64, mas a lista já
     * diferia. Uma única captura bruta permite localizar o primeiro comando
     * divergente sem despejar milhares de tarefas. */
    acmd_probe_capture_baseline(rdram, phys, bytes);
    acmd_probe_capture_suspect_before(rdram, phys, bytes);
}

static void acmd_probe_after(uint8_t* rdram, uint32_t phys, uint32_t bytes,
                             uint32_t task, int result) {
    if (!g_acmd_probe_enabled || g_acmd_probe_current_task != task) return;
    for (unsigned i = 0; i < g_acmd_probe_current_count; ++i) {
        acmd_probe_current_t* current = &g_acmd_probe_current[i];
        uint64_t after = acmd_fnv1a_logical(rdram + current->address,
                                            current->size);
        fprintf(g_acmd_probe_states,
                "%u,%016llX,%d,%s,%08X,%u,%u,%016llX,%016llX,%016llX,%d,%d,%u\n",
                task, (unsigned long long)g_acmd_probe_current_alist, result,
                acmd_probe_type_name(current->type), current->address,
                current->size, current->previous_task,
                (unsigned long long)current->previous_hash,
                (unsigned long long)current->before_hash,
                (unsigned long long)after, current->changed_between,
                after != current->before_hash, current->capture_index);
        acmd_probe_tracked_t* tracked = acmd_probe_find_tracked(
            current->type, current->address, current->size);
        if (tracked) {
            tracked->valid = 1;
            tracked->last_task = task;
            tracked->last_hash = after;
            memcpy(tracked->raw, rdram + current->address, current->size);
        }
        if (current->capture_index) {
            char dir[MAX_PATH + 64], path[MAX_PATH + 96];
            snprintf(dir, sizeof(dir), "%s\\div_%03u_task_%u_%s_%08X",
                     g_acmd_probe_dir, current->capture_index, task,
                     acmd_probe_type_name(current->type), current->address);
            snprintf(path, sizeof(path), "%s\\after_current.bin", dir);
            acmd_probe_write_logical_file(path, rdram + current->address,
                                          current->size);
        }
        if (task == g_acmd_probe_suspect_task && g_acmd_probe_suspect_captured) {
            char dir[MAX_PATH + 64], path[MAX_PATH + 112];
            snprintf(dir, sizeof(dir), "%s\\suspect_task_%u", g_acmd_probe_dir,
                     task);
            snprintf(path, sizeof(path), "%s\\after_%s_%08X.bin", dir,
                     acmd_probe_type_name(current->type), current->address);
            acmd_probe_write_logical_file(path, rdram + current->address,
                                          current->size);
        }
    }

    /* Uma AList salva tanto historicos internos (nesta ROM em uma regiao mais
     * baixa) quanto o PCM final, geralmente em varios fragmentos proximos.
     * Guardamos os SAVEBUFF e, depois, unimos somente o grupo de enderecos
     * mais alto. Isso evita misturar estados 0x22xxxx com PCM 0x26xxxx. */
    struct { uint32_t address, end; } saves[128];
    unsigned save_count = 0;
    uint32_t highest_save = 0, setbuff_bytes = 0;
    for (uint32_t i = 0; i < bytes / 8u; ++i) {
        uint32_t w0 = *(uint32_t*)(rdram + phys + i * 8u);
        uint32_t w1 = *(uint32_t*)(rdram + phys + i * 8u + 4u);
        if ((w0 >> 24) == 8u) setbuff_bytes = w1 & 0xFFFFu;
        else if ((w0 >> 24) == 6u) {
            uint32_t save_address = w1 & 0x1FFFFFFFu;
            uint32_t save_end = save_address + setbuff_bytes;
            if (setbuff_bytes && save_end >= save_address &&
                save_end <= 0x800000u) {
                if (save_count < sizeof(saves) / sizeof(saves[0])) {
                    saves[save_count].address = save_address;
                    saves[save_count].end = save_end;
                    ++save_count;
                }
                if (save_address > highest_save) highest_save = save_address;
            }
        }
    }
    uint32_t pcm_address = UINT32_MAX, pcm_end = 0;
    for (unsigned i = 0; i < save_count; ++i) {
        /* O maior buffer AI observado tem poucos KiB. Uma janela de 64 KiB
         * acomoda seus fragmentos e exclui com folga os estados da ABI. */
        if (highest_save - saves[i].address <= 0x10000u) {
            if (saves[i].address < pcm_address) pcm_address = saves[i].address;
            if (saves[i].end > pcm_end) pcm_end = saves[i].end;
        }
    }
    uint32_t pcm_bytes = pcm_address != UINT32_MAX && pcm_end > pcm_address
                             ? pcm_end - pcm_address
                             : 0;
    if (pcm_address != UINT32_MAX && pcm_bytes &&
        pcm_address + pcm_bytes <= 0x800000u) {
        acmd_probe_pcm_t* pcm =
            &g_acmd_probe_pcm_ring[g_acmd_probe_pcm_head++ % ACMD_PROBE_PCM_RING];
        pcm->valid = 1;
        pcm->consumed = 0;
        pcm->task = task;
        pcm->address = pcm_address;
        pcm->bytes = pcm_bytes;
        pcm->hash = acmd_fnv1a_logical(rdram + pcm_address, pcm_bytes);
        fprintf(g_acmd_probe_pcm, "rsp,%u,%08X,%u,%016llX,%016llX,0\n",
                task, pcm_address, pcm_bytes, (unsigned long long)pcm->hash,
                (unsigned long long)pcm->hash);
        if (task == g_acmd_probe_suspect_task && g_acmd_probe_suspect_captured) {
            char dir[MAX_PATH + 64], path[MAX_PATH + 112];
            snprintf(dir, sizeof(dir), "%s\\suspect_task_%u", g_acmd_probe_dir,
                     task);
            snprintf(path, sizeof(path), "%s\\pcm_after_%08X_%u.bin", dir,
                     pcm_address, pcm_bytes);
            acmd_probe_write_logical_file(path, rdram + pcm_address, pcm_bytes);
        }
    }
    if ((++g_acmd_probe_rows & 15u) == 0u) {
        fflush(g_acmd_probe_states);
        fflush(g_acmd_probe_pcm);
    }
}

void rsp_audio_probe_ai_buffer(uint8_t* rdram, uint32_t address, uint32_t bytes) {
    acmd_probe_init();
    if (!g_acmd_probe_enabled || !rdram || !address || !bytes ||
        address + bytes > 0x800000u) return;
    acmd_probe_pcm_t* match = NULL;
    for (unsigned age = 0; age < ACMD_PROBE_PCM_RING; ++age) {
        unsigned pos = (g_acmd_probe_pcm_head - 1u - age) % ACMD_PROBE_PCM_RING;
        acmd_probe_pcm_t* candidate = &g_acmd_probe_pcm_ring[pos];
        if (candidate->valid && !candidate->consumed &&
            candidate->address == address && candidate->bytes == bytes) {
            match = candidate;
            break;
        }
    }
    uint64_t observed = acmd_fnv1a_logical(rdram + address, bytes);
    if (match) {
        match->consumed = 1;
        fprintf(g_acmd_probe_pcm, "ai,%u,%08X,%u,%016llX,%016llX,%d\n",
                match->task, address, bytes, (unsigned long long)match->hash,
                (unsigned long long)observed, observed != match->hash);
    } else {
        fprintf(g_acmd_probe_pcm, "ai,0,%08X,%u,0000000000000000,%016llX,1\n",
                address, bytes, (unsigned long long)observed);
    }
}

void rsp_audio_probe_flush(void) {
    if (g_acmd_probe_states) fflush(g_acmd_probe_states);
    if (g_acmd_probe_pcm) fflush(g_acmd_probe_pcm);
}

/* Assinatura de cada AList, com o instante em que ela foi executada.
 *
 * A comparacao com o oraculo mostrou uma janela degradada entre 12 s e 17 s que
 * se recupera sozinha, sem perder alinhamento. Isso descarta sincronismo e
 * aponta para o *conteudo*: algum caminho de sintese que so e exercitado ali.
 *
 * Para achar qual, e preciso saber o que cada AList pediu e quando. Uma linha
 * por AList com o tempo e a contagem por opcode permite cruzar a janela ruim
 * com os comandos que so aparecem nela.
 */
static void acmd_alist_assinatura(uint8_t* rdram, uint32_t phys, uint32_t bytes) {
    static FILE* f;
    static LARGE_INTEGER freq, inicio;
    static int tentou;
    if (!tentou) {
        tentou = 1;
        const char* caminho = getenv("WPJ2_ALIST_LOG");
        if (caminho && *caminho) {
            f = fopen(caminho, "w");
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&inicio);
            if (f) {
                fprintf(f, "# tempo_s;n_cmds");
                for (int op = 0; op < ACMD_COUNT; op++) fprintf(f, ";%s", k_acmd[op]);
                fprintf(f, ";vozes_adpcm;vozes_envmix;dmem_max;estouros;taxas\n");
            }
        }
    }
    if (!f) return;

    LARGE_INTEGER agora;
    QueryPerformanceCounter(&agora);
    double t = (double)(agora.QuadPart - inicio.QuadPart) / (double)freq.QuadPart;

    int cont[ACMD_COUNT + 1];
    memset(cont, 0, sizeof(cont));
    /* Taxas de reamostragem distintas: uma voz com taxa fora do comum e o tipo
       de coisa que degrada um trecho e some depois. */
    uint16_t taxas[8]; int n_taxas = 0;

    /* Alcance da DMEM pedido pela AList.
     *
     * A DMEM do RSP tem 4 KiB. Se a lista enderecar alem disso, o hardware
     * enrola e duas implementacoes diferentes enrolam de formas diferentes -
     * que e exatamente o sintoma: HLE em C e microcodigo nativo divergindo
     * entre si so quando o numero de vozes dobra. Medir o alcance separa
     * "estourou" de "nao estourou" sem hipotese no meio. */
    uint32_t dmem_max = 0, dmem_in = 0, dmem_out = 0, contagem = 0;
    int estouros = 0;

    for (uint32_t i = 0; i < bytes / 8; i++) {
        uint32_t w0 = *(uint32_t*)(rdram + phys + i * 8);
        uint32_t w1 = *(uint32_t*)(rdram + phys + i * 8 + 4);
        uint32_t op = w0 >> 24;
        cont[op < (uint32_t)ACMD_COUNT ? op : ACMD_COUNT]++;
        if (op == 5) {                                   /* RESAMPLE */
            uint16_t taxa = (uint16_t)(w0 & 0xFFFFu);
            int visto = 0;
            for (int k = 0; k < n_taxas; k++) if (taxas[k] == taxa) visto = 1;
            if (!visto && n_taxas < 8) taxas[n_taxas++] = taxa;
        }
        if (op == 8) {                                   /* SETBUFF */
            dmem_in  =  w0 & 0xFFFFu;
            dmem_out = (w1 >> 16) & 0xFFFFu;
            contagem =  w1 & 0xFFFFu;
            uint32_t fim_in = dmem_in + contagem, fim_out = dmem_out + contagem;
            if (fim_in > dmem_max) dmem_max = fim_in;
            if (fim_out > dmem_max) dmem_max = fim_out;
            if (fim_in > 0x1000u || fim_out > 0x1000u) estouros++;
        }
        if (op == 12) {                                  /* MIXER */
            uint32_t mi = (w1 >> 16) & 0xFFFFu, mo = w1 & 0xFFFFu;
            uint32_t c = w0 & 0xFFFFu;
            if (mi + c > dmem_max) dmem_max = mi + c;
            if (mo + c > dmem_max) dmem_max = mo + c;
            if (mi + c > 0x1000u || mo + c > 0x1000u) estouros++;
        }
    }
    fprintf(f, "%.3f;%u", t, bytes / 8u);
    for (int op = 0; op < ACMD_COUNT; op++) fprintf(f, ";%d", cont[op]);
    fprintf(f, ";%d;%d;%u;%d;", cont[1], cont[3], dmem_max, estouros);
    for (int k = 0; k < n_taxas; k++) fprintf(f, "%s%u", k ? "/" : "", taxas[k]);
    fprintf(f, "\n");
    fflush(f);
}

static void acmd_capture_state_oracle(uint8_t* rdram, uint32_t phys, uint32_t bytes,
                                      const char* label) {
    const char* dir = getenv("WPJ2_AUDIO_STATE_CAPTURE");
    if (!dir || !*dir) dir = getenv("WPJ2_NATIVE_AUDIO_STATE_CAPTURE");
    acmd_capture_state_dir(rdram, phys, bytes, dir, label);
}

/* Captura de estado disparada por *tempo*, nao por indice de AList.
 *
 * A captura existente pega as duas primeiras ALists musicais, que caem no
 * trecho onde a correlacao e 1,000 - justamente onde nao ha nada a investigar.
 * Para alimentar os dois oraculos com material da janela degradada e preciso
 * escolher pelo instante: WPJ2_STATE_JANELA="12.0:17.0" captura as ALists
 * executadas nesse intervalo, uma por vez, ate o limite pedido.
 *
 * A entrada gravada e identica para os dois harnesses. Rodar a mesma AList no
 * HLE e no microcodigo e comparar o PCM resultante e o que localiza a
 * divergencia sem passar por fila, taxa ou API de audio. */
static void acmd_capture_janela(uint8_t* rdram, uint32_t phys, uint32_t bytes) {
    static int configurado;
    static double ini_s, fim_s;
    static unsigned limite, gravadas;
    static LARGE_INTEGER freq, t0;

    if (!configurado) {
        configurado = 1;
        const char* janela = getenv("WPJ2_STATE_JANELA");
        if (janela && *janela) {
            if (sscanf(janela, "%lf:%lf", &ini_s, &fim_s) != 2) ini_s = fim_s = -1.0;
            const char* n = getenv("WPJ2_STATE_JANELA_N");
            limite = (n && *n) ? (unsigned)strtoul(n, NULL, 10) : 3u;
            if (!limite || limite > 16u) limite = 3u;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&t0);
        } else {
            ini_s = fim_s = -1.0;
        }
    }
    if (ini_s < 0.0 || gravadas >= limite) return;

    LARGE_INTEGER agora;
    QueryPerformanceCounter(&agora);
    double t = (double)(agora.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
    if (t < ini_s || t > fim_s) return;

    const char* dir = getenv("WPJ2_STATE_JANELA_DIR");
    if (!dir || !*dir) return;
    char rotulo[64];
    snprintf(rotulo, sizeof(rotulo), "janela_%u_t%.2f", gravadas, t);
    acmd_capture_state_dir(rdram, phys, bytes, dir, rotulo);

    /* Os dois harnesses de oraculo esperam a RDRAM inteira em ordem logica do
       N64, mais a OSTask. Sem isso eles recusam a captura, e sem eles nao da
       para rodar a mesma AList no HLE e no microcodigo. Oito MiB por captura e
       barato perto de mais um ciclo de investigacao. */
    char caminho[MAX_PATH];
    snprintf(caminho, sizeof(caminho), "%s\\%s\\rdram.bin", dir, rotulo);
    FILE* fr = fopen(caminho, "wb");
    if (fr) {
        enum { BLOCO = 1u << 16 };
        static uint8_t buf[BLOCO];
        for (uint32_t base = 0; base < 0x800000u; base += BLOCO) {
            for (uint32_t i = 0; i < BLOCO; i++) buf[i] = rdram[(base + i) ^ 3u];
            fwrite(buf, 1, BLOCO, fr);
        }
        fclose(fr);
    }
    snprintf(caminho, sizeof(caminho), "%s\\%s\\task.bin", dir, rotulo);
    FILE* ft = fopen(caminho, "wb");
    if (ft) {
        for (uint32_t i = 0; i < 0x40u; i++) fputc(g_spmem[(TASK_OFFSET + i) ^ 3u], ft);
        fclose(ft);
    }

    /* O harness precisa saber onde o PCM final foi parar e quanto ele mede.
       O ultimo SAVEBUFF da AList e esse destino. O `ai_pcm.bin` de referencia
       vai zerado de proposito: aqui a comparacao util e entre as saidas do HLE
       e do microcodigo, nao contra o Project64 - o objetivo e localizar onde
       as duas implementacoes discordam com a mesma entrada. */
    uint32_t ai_addr = 0, ai_bytes = 0, sb_count = 0;
    for (uint32_t i = 0; i < bytes / 8; i++) {
        uint32_t w0 = *(uint32_t*)(rdram + phys + i * 8);
        uint32_t w1 = *(uint32_t*)(rdram + phys + i * 8 + 4);
        if ((w0 >> 24) == 8u) sb_count = w1 & 0xFFFFu;      /* SETBUFF */
        if ((w0 >> 24) == 6u) {                              /* SAVEBUFF */
            ai_addr = w1 & 0x1FFFFFFFu;
            ai_bytes = sb_count;
        }
    }
    if (ai_addr && ai_bytes && ai_addr + ai_bytes <= 0x800000u) {
        snprintf(caminho, sizeof(caminho), "%s\\%s\\manifest.txt", dir, rotulo);
        FILE* fm = fopen(caminho, "w");
        if (fm) {
            fprintf(fm, "AI buffer=0x%08X\nbytes=%u\n", ai_addr, ai_bytes);
            fclose(fm);
        }
        snprintf(caminho, sizeof(caminho), "%s\\%s\\ai_pcm.bin", dir, rotulo);
        FILE* fp = fopen(caminho, "wb");
        if (fp) {
            for (uint32_t i = 0; i < ai_bytes; i++) fputc(0, fp);
            fclose(fp);
        }
    }
    gravadas++;
    printf("  [estado] AList da janela ruim gravada em %s (t=%.2f s)\n", rotulo, t);
    fflush(stdout);
}

static int16_t acmd_nibble(uint8_t byte, uint8_t mask, unsigned lshift,
                           unsigned rshift) {
    uint16_t raw = (uint16_t)(byte & mask) << lshift;
    return (int16_t)((int16_t)raw >> rshift);
}

static void acmd_adpcm_half(int16_t* dst, const int16_t* src,
                            const int16_t* book, const int16_t* previous) {
    for (uint32_t i = 0; i < 8; i++) {
        int32_t accum = (int32_t)((uint32_t)(int32_t)src[i] << 11);
        accum = acmd_mac32(accum, book[i], previous[0]);
        accum = acmd_mac32(accum, book[8 + i], previous[1]);
        /* Produto reverso do segundo preditor: book2[0] multiplica a
           amostra anterior, book2[1] a anterior a ela, etc. A ordem oposta
           preservava a primeira amostra mas desviava o restante do frame. */
        for (uint32_t j = 0; j < i; j++)
            accum = acmd_mac32(accum, book[8 + j], src[i - 1 - j]);
        dst[i] = acmd_clamp16(accum >> 11);
    }
}

/* A sintese e propositalmente cara enquanto ainda e interpretada em C. Para
 * sondar somente a progressao visual, WPJ2_AUDIO_FAST=1 mantem a AList, os
 * buffers e as conclusoes de RSP, mas pula ADPCM/RESAMPLE/ENVMIX. Nao e um
 * modo de prototipo: o lote visual o usa apenas para avancar cenas. */
static int acmd_fast_mode(void) {
    if (g_acmd_fast < 0) {
        const char* e = getenv("WPJ2_AUDIO_FAST");
        g_acmd_fast = e && *e && *e != '0';
    }
    return g_acmd_fast;
}

static int acmd_native_rsp_mode(void) {
    if (g_acmd_force_hle) return 0;
    if (g_acmd_native_rsp < 0) {
        const char* e = getenv("WPJ2_NATIVE_AUDIO_RSP");
        g_acmd_native_rsp = e && *e && *e != '0';
    }
    return g_acmd_native_rsp;
}

/* Registro compacto, opt-in, da entrada entregue ao RSP nativo. Ele permite
 * alinhar a construcao da AList com a sonda do Project64 sem despejar PCM ou
 * alterar a cadencia. O contador inclui inclusive listas silenciosas. */
static void acmd_trace_native_list(uint8_t* rdram, uint32_t phys, uint32_t bytes,
                                   int result) {
    static int initialized = 0;
    static uint32_t task_index = 0;
    static FILE* out = NULL;
    if (!initialized) {
        initialized = 1;
        const char* path = getenv("WPJ2_NATIVE_AUDIO_LIST_TRACE");
        if (path && *path) out = fopen(path, "wb");
        if (out) {
            fprintf(out, "task,bytes,op1_adpcm,op3_envmix,op5_resample,op8_setbuf,op9_setvol,native_result\n");
            fflush(out);
        }
    }
    task_index++;
    if (!out) return;
    uint32_t count[16] = {0};
    for (uint32_t i = 0; i < bytes / 8u; ++i) {
        uint32_t op = *(uint32_t*)(rdram + phys + i * 8u) >> 24;
        if (op < 16u) count[op]++;
    }
    fprintf(out, "%u,%u,%u,%u,%u,%u,%u,%d\n", task_index, bytes,
            count[1], count[3], count[5], count[8], count[9], result);
    fflush(out);
}

static int acmd_bypass_polef(void) {
    if (g_acmd_bypass_polef < 0) {
        const char* e = getenv("WPJ2_AUDIO_BYPASS_POLEF");
        g_acmd_bypass_polef = e && *e && *e != '0';
    }
    return g_acmd_bypass_polef;
}

/* PALIATIVO, ligado por padrao. Medicao: o caminho wet responde por ~70% do
 * offset DC (+278 com ele, +84 sem; o Project64 fica em -32), e na comparacao
 * A/B de ouvido o `ab_no_wet.wav` foi o que menos chiou, sobretudo nos trechos
 * de mais volume.
 *
 * Isto NAO e a correcao: desligar o reverb remove tambem o efeito legitimo, e
 * o defeito real continua aberto (ver ANALISE_AUDIO.md). E uma troca
 * consciente - menos chiado agora, menos fidelidade - enquanto a causa e
 * investigada.
 *
 * WPJ2_AUDIO_NO_WET=0 restaura o caminho completo, que e o que deve ser usado
 * ao medir divergencia contra o microcodigo real. */
static int acmd_no_wet(void) {
    if (g_acmd_no_wet < 0) {
        const char* e = getenv("WPJ2_AUDIO_NO_WET");
        g_acmd_no_wet = (e && *e) ? (*e != '0') : 1;
    }
    return g_acmd_no_wet;
}

/* Saida de diagnostico sem efeitos: alem de nao injetar novas amostras no
 * bus wet, impede que o anel de atraso ja salvo volte ao bus seco. A ROM usa
 * C80/DC0 como dois buffers auxiliares e os mistura de volta somente no fim
 * da AList; filtrar ali preserva a musica seca, sem alterar ADPCM/RESAMPLE
 * nem o andamento do jogo. */
static int acmd_dry_only(void) {
    if (g_acmd_dry_only < 0) {
        const char* e = getenv("WPJ2_AUDIO_DRY_ONLY");
        g_acmd_dry_only = e && *e && *e != '0';
    }
    return g_acmd_dry_only;
}

static int acmd_effect_range(uint32_t p, uint32_t bytes) {
    if (!bytes || !g_env_wet_l || !g_env_wet_r) return 0;
    /* Os buffers wet reais iniciam 0x20 depois do inicio dos dois blocos de
       trabalho. Incluimos essa margem porque MIX/SAVEBUFF operam no bloco
       inteiro, nao apenas nas amostras escritas pelo ENVMIXER. */
    uint32_t first = g_env_wet_l >= 0x20u ? g_env_wet_l - 0x20u : g_env_wet_l;
    uint32_t last = g_env_wet_r + 0x140u;
    return p < last && p + bytes > first;
}

/* Na abertura, os cinco estados permanentes abaixo pertencem ao BGM. Sons
 * transitórios e falas usam outros blocos; silenciá-los no ENVMIXER deixa os
 * decodificadores atualizarem seus históricos, mas não os envia à saída. */
static int acmd_music_only(void) {
    if (g_acmd_music_only < 0) {
        const char* e = getenv("WPJ2_AUDIO_MUSIC_ONLY");
        g_acmd_music_only = e && *e && *e != '0';
    }
    return g_acmd_music_only;
}

static int acmd_is_bgm_state(uint32_t state) {
    return state >= 0x00229BD0u && state <= 0x00229E90u &&
           ((state - 0x00229BD0u) % 0xB0u) == 0;
}

static int acmd_voice_filter(uint32_t state) {
    if (g_acmd_voice == -2) {
        const char* e = getenv("WPJ2_AUDIO_VOICE");
        g_acmd_voice = (e && *e) ? atoi(e) : -1;
    }
    if (g_acmd_voice < 0) return 1;
    uint32_t slot = g_acmd_voice_count;
    for (uint32_t i = 0; i < g_acmd_voice_count; i++) {
        if (g_acmd_voice_state[i] == state) { slot = i; break; }
    }
    if (slot == g_acmd_voice_count && slot < ACMD_VOICE_MAX)
        g_acmd_voice_state[g_acmd_voice_count++] = state;
    /* Se uma lista excepcional ultrapassar o mapa, nao silencie dados sem
       identificacao: conserva a voz em vez de diagnosticar um falso negativo. */
    return slot >= ACMD_VOICE_MAX || (int)slot == g_acmd_voice;
}

void rsp_cycle_audio_voice(void) {
    if (g_acmd_voice == -2) {
        const char* e = getenv("WPJ2_AUDIO_VOICE");
        g_acmd_voice = (e && *e) ? atoi(e) : -1;
    }
    /* -1 e a mistura completa; oito slots cobrem as vozes observadas na
       abertura e evita percorrer todos os 32 slots vazios no teclado. */
    g_acmd_voice = (g_acmd_voice < 7) ? g_acmd_voice + 1 : -1;
    printf("[audio] F11: %s", g_acmd_voice < 0 ? "mistura completa" : "somente voz ");
    if (g_acmd_voice >= 0) printf("%d", g_acmd_voice);
    printf("\n");
    fflush(stdout);
}

static void acmd_mix_into(uint32_t dst, const uint8_t* src, uint32_t bytes,
                          int32_t gain) {
    if (dst + bytes > SPMEM_SIZE) return;
    for (uint32_t i = 0; i + 1 < bytes; i += 2) {
        int32_t a = acmd_read_s16(src + i);
        int32_t b = acmd_read_s16(g_spmem + dst + i);
        acmd_write_s16(g_spmem + dst + i, b + ((a * gain) >> 15));
    }
}

static int64_t acmd_read_s64(uint8_t* p) {
    int64_t lo = (uint32_t)*(uint32_t*)(p + 0);
    int64_t hi = (int32_t)*(uint32_t*)(p + 4);
    return lo | (hi << 32);
}
static void acmd_write_s64(uint8_t* p, int64_t v) {
    *(uint32_t*)(p + 0) = (uint32_t)v;
    *(uint32_t*)(p + 4) = (uint32_t)(v >> 32);
}

/* ENVMIXER da ABI padrao. O estado salvo tem 80 bytes; reproduzimos o
 * envelope exponencial para que volumes e instrumentos continuem entre listas. */
static void acmd_envmix(uint8_t* rdram, uint32_t flags, uint32_t state,
                        uint32_t dmem_in, uint32_t count) {
    if (!count || dmem_in + count > SPMEM_SIZE || state + 80 > 0x800000u) return;
    /* Os quatro destinos vem do SETBUFF. O ramo A_AUX define tres deles; se
     * ele nunca rodar, dry_r/wet_l/wet_r ficam em zero e este mixer escreve o
     * canal direito no inicio da DMEM. Reportado uma vez, com os valores
     * realmente em uso, para nao depender de inferencia. */
    {
        static int relatado;
        if (!relatado) {
            relatado = 1;
            printf("  [envmix-destinos] dry_l=0x%03X dry_r=0x%03X wet_l=0x%03X wet_r=0x%03X"
                   " (SETBUFF normal=%llu aux=%llu)\n",
                   g_env_dry_l, g_env_dry_r, g_env_wet_l, g_env_wet_r,
                   (unsigned long long)g_env_destino_dry_l,
                   (unsigned long long)g_env_destino_aux);
            fflush(stdout);
        }
    }
    /* Rampas em 32 bits, com transbordo, como o acumulador do microcodigo
     * real. Em 64 bits nao ha volta, e onde o hardware transborda nos
     * seguiriamos crescendo.
     *
     * REVALIDADO sobre a base correta (depois de 5k):
     *     int32: media 25,31%  223 listas  RMS 4915
     *     int64: media 30,92%  224 listas  RMS 4848
     * Ganha 5,6 pontos de media, acima do ruido de ~1,5, sem atenuar o sinal.
     * A contagem nao move - a mudanca age nos casos extremos, nao em quantos
     * divergem. */
    int32_t value_l, value_r, target_l, target_r, step_l, step_r;
    int32_t exp_rate_l, exp_rate_r, exp_seq_l, exp_seq_r;
    int16_t dry = g_env_dry, wet = g_env_wet;
    if (flags & 1u) {
        value_l = (int64_t)g_env_vol_l << 16;
        value_r = (int64_t)g_env_vol_r << 16;
        target_l = (int64_t)g_env_target_l << 16;
        target_r = (int64_t)g_env_target_r << 16;
        exp_rate_l = g_env_rate_l; exp_rate_r = g_env_rate_r;
        exp_seq_l = (int32_t)((int64_t)g_env_vol_l * exp_rate_l);
        exp_seq_r = (int32_t)((int64_t)g_env_vol_r * exp_rate_r);
    } else {
        /* O save-buffer da ABI e indexado como s16 no microcodigo: os
           campos seguintes estao em +4, +8, +12... bytes, nao contiguos. */
        /* ENVMIXER salva este bloco com MOVEMEM/DMA, nao com LH/SH. Os dois
           ganhos devem portanto ser lidos como meia-palavra bruta, igual ao
           bloco que o HLE do Project64 copia por memcpy. */
        /* LEITURA CRUA - correta, medida.
         *
         * Houve uma tentativa de ler com swap ^2 "por consistencia com o resto
         * do codigo de audio". Ela ZERA os dois ganhos:
         *
         *     [ganhos] swap: dry=0 wet=0 | cru: dry=26698 wet=18997
         *
         * Com os ganhos em zero as vozes que retomam estado ficam mudas, o
         * sinal cai ~10x, e a divergencia contra o microcodigo "melhora"
         * porque silencio diverge menos em valor absoluto. Foi artefato, nao
         * correcao - ver ANALISE_AUDIO.md, 5k.
         *
         * Estes dois campos sao os unicos do bloco lidos como meia-palavra, e
         * a forma crua e a que devolve valor. Nao mexer sem medir o RMS junto
         * com a divergencia: olhar so a divergencia esconde atenuacao. */
        dry = *(int16_t*)(rdram + state + 4);
        wet = *(int16_t*)(rdram + state + 0);
        /* Leitura direta, confirmada por medicao. Testada a variante com as
         * meias-palavras trocadas (ver 5f) e ela PIORA: 52 listas contra 43-46,
         * acima do ruido de ~4. Faz sentido: numa RDRAM word-swapped, um u32
         * alinhado ja sai correto - so os acessos de meia-palavra precisam do
         * ^2. A correcao 5c nao revelou "layout diferente do PJ64"; corrigiu
         * uma inconsistencia nossa, onde wet/dry nao usavam o helper com swap
         * que todo o resto do codigo de audio usa. */
        target_l = (int32_t)*(uint32_t*)(rdram + state + 8);
        target_r = (int32_t)*(uint32_t*)(rdram + state + 12);
        exp_rate_l = (int32_t)*(uint32_t*)(rdram + state + 16);
        exp_rate_r = (int32_t)*(uint32_t*)(rdram + state + 20);
        exp_seq_l = (int32_t)*(uint32_t*)(rdram + state + 24);
        exp_seq_r = (int32_t)*(uint32_t*)(rdram + state + 28);
        value_l = (int32_t)*(uint32_t*)(rdram + state + 32);
        value_r = (int32_t)*(uint32_t*)(rdram + state + 36);
    }
    step_l = target_l - value_l; step_r = target_r - value_r;
    for (uint32_t base = 0; base < count; base += 16) {
        /* Observacao aberta: quando (exp_seq - value) e pequeno, o `>> 3`
         * trunca o passo para zero mesmo com a rampa longe do alvo, e este
         * guarda entao trava - exp_seq nao decai mais e a voz congela no ganho
         * corrente. O microcodigo real, no mesmo estado, leva a voz a zero e
         * libera o bloco. O travamento e real, mas nao e a causa isolada:
         *
         * TENTATIVA REVERTIDA: trocar este guarda por `value != target` levou a
         * divergencia da lista ruim de 15,84% para 50,25%. O travamento do
         * passo em zero nao e, sozinho, o defeito - ou e compensado por outra
         * coisa. Medido, nao deduzido; nao repetir sem nova evidencia. */
        if (step_l) { exp_seq_l = (int32_t)(((int64_t)exp_seq_l * exp_rate_l) >> 16); step_l = (exp_seq_l - value_l) >> 3; }
        if (step_r) { exp_seq_r = (int32_t)(((int64_t)exp_seq_r * exp_rate_r) >> 16); step_r = (exp_seq_r - value_r) >> 3; }
        for (uint32_t i = 0; i < 8 && base + i * 2 + 1 < count; i++) {
            value_l += step_l; value_r += step_r;
            if ((step_l <= 0 && value_l <= target_l) || (step_l >= 0 && value_l >= target_l)) { value_l = target_l; step_l = 0; }
            if ((step_r <= 0 && value_r <= target_r) || (step_r >= 0 && value_r >= target_r)) { value_r = target_r; step_r = 0; }
            int32_t sample = acmd_read_s16(g_spmem + dmem_in + base + i * 2);
            int32_t lv = (int32_t)(value_l >> 16), rv = (int32_t)(value_r >> 16);
            /* Testado em 32 bits puro (sem o cast para int64) e revertido: nao
             * muda nada. `lv` e value>>16, no maximo ~32767; vezes `dry` (s16)
             * da ~1e9, que cabe em int32 com folga. Nao ha transbordo a
             * reproduzir aqui, ao contrario das rampas (5d). */
            int16_t gl = acmd_clamp16(((int64_t)lv * dry + 0x4000) >> 15);
            int16_t gr = acmd_clamp16(((int64_t)rv * dry + 0x4000) >> 15);
            int16_t gwl = acmd_clamp16(((int64_t)lv * wet + 0x4000) >> 15);
            int16_t gwr = acmd_clamp16(((int64_t)rv * wet + 0x4000) >> 15);
            /* VMULF/VMACF do RSP arredondam: (a*b*2 + 0x8000) >> 16, que e o
             * mesmo que (a*b + 0x4000) >> 15. O HLE do Project64 usa (a*b)>>15
             * puro, e >> em complemento de dois trunca para baixo. Um LSB
             * sempre no mesmo sentido, por voz e por amostra, acumula como
             * offset DC - foi o que a bissecao contra o microcodigo real
             * flagrou aqui (nativo=27, hle=26). */
            /* TESTE: sem o +0x4000 na acumulacao por amostra.
             *
             * Medicao de 5j: vozes isoladas tem DC entre -7 e -14 (centradas,
             * como o PJ64), mas somadas dao +278. O offset e CRIADO pela soma.
             * Arredondar para cima injeta ~+0,5 LSB por acumulacao; com
             * dezenas de vozes e o anel de reverb realimentando, vira o
             * offset medido. O sample_mix do PJ64 nao arredonda. */
            if (g_env_dry_l + base + i * 2 < SPMEM_SIZE) acmd_write_s16(g_spmem + g_env_dry_l + base + i * 2, acmd_read_s16(g_spmem + g_env_dry_l + base + i * 2) + ((sample * gl) >> 15));
            if (g_env_dry_r + base + i * 2 < SPMEM_SIZE) acmd_write_s16(g_spmem + g_env_dry_r + base + i * 2, acmd_read_s16(g_spmem + g_env_dry_r + base + i * 2) + ((sample * gr) >> 15));
            if ((flags & 8u) && !acmd_no_wet() && !acmd_dry_only()) {
                if (g_env_wet_l + base + i * 2 < SPMEM_SIZE) acmd_write_s16(g_spmem + g_env_wet_l + base + i * 2, acmd_read_s16(g_spmem + g_env_wet_l + base + i * 2) + ((sample * gwl) >> 15));
                if (g_env_wet_r + base + i * 2 < SPMEM_SIZE) acmd_write_s16(g_spmem + g_env_wet_r + base + i * 2, acmd_read_s16(g_spmem + g_env_wet_r + base + i * 2) + ((sample * gwr) >> 15));
            }
        }
    }
    /* ASSIMETRIA DELIBERADA, confirmada por medicao. Le-se com swap ^2 (5c) e
     * grava-se CRU. Tornar a escrita simetrica parece obvio e PIORA muito:
     * 224 listas contra 43-46, de volta ao patamar original (ver 5g).
     *
     * A leitura de meia-palavra precisa do ^2 porque nosso acesso e por
     * halfword; a escrita do bloco reproduz o que o microcodigo faz com
     * MOVEMEM/DMA, que copia bytes sem semantica de halfword. As duas coisas
     * sao diferentes, e o par assimetrico e o que bate com o oraculo.
     *
     * Nao "corrigir" isto sem medir. */
    *(int16_t*)(rdram + state + 0) = wet;
    *(int16_t*)(rdram + state + 4) = dry;
    *(uint32_t*)(rdram + state + 8) = (uint32_t)target_l; *(uint32_t*)(rdram + state + 12) = (uint32_t)target_r;
    *(uint32_t*)(rdram + state + 16) = (uint32_t)exp_rate_l; *(uint32_t*)(rdram + state + 20) = (uint32_t)exp_rate_r;
    *(uint32_t*)(rdram + state + 24) = (uint32_t)exp_seq_l; *(uint32_t*)(rdram + state + 28) = (uint32_t)exp_seq_r;
    *(uint32_t*)(rdram + state + 32) = (uint32_t)value_l; *(uint32_t*)(rdram + state + 36) = (uint32_t)value_r;
}

/* Decodificador ADPCM ABI1. O livro e o estado pertencem a RDRAM; a fonte
 * comprimida e o PCM pertencem a DMEM. O filtro de duas amostras segue a
 * estrutura do livro (coeficientes Q11) sem depender de codigo de emulador. */
static void acmd_adpcm(uint8_t* rdram, uint32_t dmem_in, uint32_t dmem_out,
                       uint32_t bytes, uint32_t flags, uint32_t state) {
    uint32_t count = acmd_align(bytes, 32);
    if (!count || dmem_in >= SPMEM_SIZE || dmem_out + count + 32 > SPMEM_SIZE ||
        state + 32 > 0x800000u) return;

    int16_t last[16], frame[16];
    if (flags & 1u) memset(last, 0, sizeof(last));
    else {
        /* A_LOOP nao reutiliza o estado normal do instrumento: ele inicia o
         * bloco com o historico configurado por SETLOOP. O HLE do Project64
         * faz a mesma selecao antes de escrever o novo estado em `state`. */
        uint32_t history = (flags & 2u) ? g_audio_loop : state;
        if (history + 32 > 0x800000u) return;
        for (uint32_t i = 0; i < 16; i++)
            last[i] = acmd_read_s16(rdram + history + i * 2);
    }

    /* A ABI escreve deliberadamente estas 16 amostras antes do PCM novo.
       RESAMPLE volta quatro amostras a partir do dmem_in e depende delas. */
    for (uint32_t i = 0; i < 16; i++) acmd_write_s16(g_spmem + dmem_out + i * 2, last[i]);
    uint32_t src = dmem_in, dst = dmem_out + 32;
    while (count) {
        uint8_t code = acmd_read_u8(g_spmem + src++);
        uint32_t predictor = code & 0x0Fu;
        unsigned rshift = (code >> 4) < 12 ? 12 - (code >> 4) : 0;
        if (predictor >= 8) predictor = 0;
        const int16_t* book = g_pole_coeff + predictor * 16;
        for (uint32_t i = 0; i < 8; i++) {
            uint8_t packed = acmd_read_u8(g_spmem + src++);
            frame[i * 2]     = acmd_nibble(packed, 0xF0, 8, rshift);
            frame[i * 2 + 1] = acmd_nibble(packed, 0x0F, 12, rshift);
        }
        acmd_adpcm_half(last, frame, book, last + 14);
        acmd_adpcm_half(last + 8, frame + 8, book, last + 6);
        for (uint32_t i = 0; i < 16; i++) acmd_write_s16(g_spmem + dst + i * 2, last[i]);
        dst += 32;
        count -= 32;
    }
    for (uint32_t i = 0; i < 16; i++) acmd_write_s16(rdram + state + i * 2, last[i]);
}

/* Primeira metade da tabela FIR do RSP. A segunda e o espelho da primeira;
 * assim o filtro completo de 64 fases cabe aqui sem aproximacoes de ponto
 * flutuante. Cada fase combina quatro amostras em Q15. */
static const int16_t g_resample_lut_half[32][4] = {
    {0x0c39,0x66ad,0x0d46,(int16_t)0xffdf}, {0x0b39,0x6696,0x0e5f,(int16_t)0xffd8},
    {0x0a44,0x6669,0x0f83,(int16_t)0xffd0}, {0x095a,0x6626,0x10b4,(int16_t)0xffc8},
    {0x087d,0x65cd,0x11f0,(int16_t)0xffbf}, {0x07ab,0x655e,0x1338,(int16_t)0xffb6},
    {0x06e4,0x64d9,0x148c,(int16_t)0xffac}, {0x0628,0x643f,0x15eb,(int16_t)0xffa1},
    {0x0577,0x638f,0x1756,(int16_t)0xff96}, {0x04d1,0x62cb,0x18cb,(int16_t)0xff8a},
    {0x0435,0x61f3,0x1a4c,(int16_t)0xff7e}, {0x03a4,0x6106,0x1bd7,(int16_t)0xff71},
    {0x031c,0x6007,0x1d6c,(int16_t)0xff64}, {0x029f,0x5ef5,0x1f0b,(int16_t)0xff56},
    {0x022a,0x5dd0,0x20b3,(int16_t)0xff48}, {0x01be,0x5c9a,0x2264,(int16_t)0xff3a},
    {0x015b,0x5b53,0x241e,(int16_t)0xff2c}, {0x0101,0x59fc,0x25e0,(int16_t)0xff1e},
    {0x00ae,0x5896,0x27a9,(int16_t)0xff10}, {0x0063,0x5720,0x297a,(int16_t)0xff02},
    {0x001f,0x559d,0x2b50,(int16_t)0xfef4}, {(int16_t)0xffe2,0x540d,0x2d2c,(int16_t)0xfee8},
    {(int16_t)0xffac,0x5270,0x2f0d,(int16_t)0xfedb}, {(int16_t)0xff7c,0x50c7,0x30f3,(int16_t)0xfed0},
    {(int16_t)0xff53,0x4f14,0x32dc,(int16_t)0xfec6}, {(int16_t)0xff2e,0x4d57,0x34c8,(int16_t)0xfebd},
    {(int16_t)0xff0f,0x4b91,0x36b6,(int16_t)0xfeb6}, {(int16_t)0xfef5,0x49c2,0x38a5,(int16_t)0xfeb0},
    {(int16_t)0xfedf,0x47ed,0x3a95,(int16_t)0xfeac}, {(int16_t)0xfece,0x4611,0x3c85,(int16_t)0xfeab},
    {(int16_t)0xfec0,0x4430,0x3e74,(int16_t)0xfeac}, {(int16_t)0xfeb6,0x424a,0x4060,(int16_t)0xfeaf}
};

static int16_t acmd_resample_coeff(uint32_t phase, uint32_t tap) {
    uint32_t row = (phase & 0xFC00u) >> 10;
    if (row < 32) return g_resample_lut_half[row][tap];
    return g_resample_lut_half[63u - row][3u - tap];
}

/* A taxa da ABI e Q16.16; o RSP consulta uma FIR de quatro amostras para cada
 * uma das 64 fases, e preserva quatro amostras e o acumulador entre ALists. */
static void acmd_resample(uint8_t* rdram, uint32_t dmem_in, uint32_t dmem_out,
                          uint32_t bytes, uint32_t pitch, uint32_t flags,
                          uint32_t state) {
    uint32_t count = acmd_align(bytes, 16);
    if (!count || dmem_in < 8 || dmem_in >= SPMEM_SIZE || dmem_out + count > SPMEM_SIZE ||
        state + 10 > 0x800000u) return;
    uint32_t ipos = dmem_in - 8, phase = 0;
    if (flags & 1u) {
        for (uint32_t i = 0; i < 4; i++) acmd_write_s16(g_spmem + ipos + i * 2, 0);
    } else {
        for (uint32_t i = 0; i < 4; i++)
            acmd_write_s16(g_spmem + ipos + i * 2, acmd_read_s16(rdram + state + i * 2));
        phase = (uint16_t)acmd_read_s16(rdram + state + 8);
    }
    for (uint32_t out = 0; out < count; out += 2) {
        int32_t accum = 0;
        for (uint32_t tap = 0; tap < 4; tap++)
            accum = acmd_mac32(accum, acmd_read_s16(g_spmem + ipos + tap * 2),
                               acmd_resample_coeff(phase, tap));
        acmd_write_s16(g_spmem + dmem_out + out, accum >> 15);
        phase += pitch << 1;
        ipos += (phase >> 16) * 2;
        phase &= 0xFFFFu;
    }
    for (uint32_t i = 0; i < 4; i++)
        acmd_write_s16(rdram + state + i * 2, acmd_read_s16(g_spmem + ipos + i * 2));
    /* Os quatro primeiros valores são PCM e portanto saturam; o quinto é o
     * acumulador Q16 do reamostrador. Ele é um word bruto: gravá-lo por
     * acmd_write_s16 truncava qualquer fase acima de 0x7FFF para 0x7FFF e
     * fazia as vozes entrarem fora de fase nas ALists seguintes. */
    *(uint16_t*)((uintptr_t)(rdram + state + 8) ^ 2u) = (uint16_t)phase;
}

static void run_acmd_list(uint8_t* rdram, uint32_t phys, uint32_t bytes) {
    if (!phys || bytes < 8 || bytes > 0x8000) return;
    acmd_alist_assinatura(rdram, phys, bytes);
    acmd_capture_janela(rdram, phys, bytes);
    acmd_dump_ucode_once(rdram);
    /* A variante nativa executa o microcodigo real da ROM, traduzido pelo
     * RSPRecomp. Mantemos o HLE C abaixo como fallback e como referencia para
     * regressao; a chave impede que uma integracao ainda em validacao mude o
     * executavel de demonstracao. */
    if (acmd_native_rsp_mode()) {
        /* Índice absoluto da tarefa nativa, incluindo as listas silenciosas.
           O oráculo Project64 usa a mesma convenção; isso permite descobrir
           a primeira voz cujo histórico diverge sem alinhar pelo relógio. */
        static unsigned native_all_tasks;
        const unsigned native_task_index = ++native_all_tasks;
        /* AList e estados antes do RSP, somente quando pedido. A segunda
           AList musical e comparavel diretamente com a captura Project64:
           ambas ja possuem os historicos produzidos pela primeira passagem. */
        int has_envmix = 0;
        for (uint32_t i = 0; i < bytes / 8; ++i) {
            if ((*(uint32_t*)(rdram + phys + i * 8) >> 24) == 3u) {
                has_envmix = 1;
                break;
            }
        }
        const char* native_capture = getenv("WPJ2_NATIVE_AUDIO_STATE_CAPTURE");
        if (native_capture && *native_capture) {
            static unsigned native_musical;
            /* Normalmente duas amostras bastam; a quantidade pode ser
               ampliada na investigacao de cadencia sem recompilar. */
            unsigned native_limit = 2u;
            const char* capture_count = getenv("WPJ2_NATIVE_AUDIO_STATE_CAPTURE_COUNT");
            if (capture_count && *capture_count) {
                unsigned parsed = (unsigned)strtoul(capture_count, NULL, 10);
                if (parsed > 0 && parsed <= 32u) native_limit = parsed;
            }
            if (has_envmix && ++native_musical <= native_limit) {
                char label[32];
                CreateDirectoryA(native_capture, NULL);
                snprintf(label, sizeof(label), "native_task_%02u", native_musical);
                acmd_capture_state_oracle(rdram, phys, bytes, label);
            }
        }
        /* F5 deve capturar a proxima AList musical tambem na rota RSP
           nativa. Antes este pedido ficava abaixo do retorno antecipado e a
           imagem era salva sem os dados que explicam o chiado. Esta captura
           usa WPJ2_AUDIO_STATE_CAPTURE, independente da coleta automatica. */
        if (has_envmix && g_acmd_capture_requested) {
            static unsigned native_f5_capture_count;
            char label[32];
            snprintf(label, sizeof(label), "f5_audio_%03u", ++native_f5_capture_count);
            acmd_capture_state_oracle(rdram, phys, bytes, label);
            g_acmd_capture_requested = 0;
            printf("[captura] AList nativa de audio armada por F5 salva (%s)\n", label);
            fflush(stdout);
        }
        /* Hashes compactos dos estados persistentes antes de cada AList. */
        if (has_envmix) {
            acmd_probe_before(rdram, phys, bytes, native_task_index);
            unsigned deep_limit = 430u;
            const char* deep_count = getenv("WPJ2_NATIVE_AUDIO_DEEP_CAPTURE_COUNT");
            if (deep_count && *deep_count) {
                unsigned parsed = (unsigned)strtoul(deep_count, NULL, 10);
                if (parsed > 0u && parsed <= 10000u) deep_limit = parsed;
            }
            if (native_task_index <= deep_limit)
                acmd_deep_hash_trace(rdram, phys, bytes, native_task_index);
        }
        /* A mesma selecao de ENVMIXER usada pelo HLE agora vale para o
           microcodigo real. Somente os comandos que injetam a voz nos buses
           sao anulados; ADPCM e RESAMPLE das demais continuam atualizando
           seus estados, evitando que trocar F11 crie um falso estalo. */
        g_acmd_voice_count = 0;
        if (g_acmd_voice != -1) {
            for (uint32_t i = 0; i < bytes / 8; ++i) {
                uint32_t* w = (uint32_t*)(rdram + phys + i * 8);
                if ((w[0] >> 24) == 3u && !acmd_voice_filter(w[1] & 0x1FFFFFFFu))
                    w[0] &= 0x00FFFFFFu; /* SPNOOP */
            }
        }
        int result = wpj2_native_audio_rsp(rdram, g_spmem);
        if (has_envmix)
            acmd_probe_after(rdram, phys, bytes, native_task_index, result);
        /* O perfil audio_rsp_exato usa este resultado para demonstrar se
         * cada AList saiu pelo microcodigo ou caiu no HLE aproximado. */
        acmd_trace_native_list(rdram, phys, bytes, result);
        if (result == 0) return;
        if (g_rsp_debug) {
            printf("[audio-rsp] ucode nativo retornou %d; HLE C preservado\n", result);
            fflush(stdout);
        }
    }
    LARGE_INTEGER inicio, fim, freq;
    QueryPerformanceCounter(&inicio);
    uint32_t dmem_in = 0, dmem_out = 0, count = 0;
    /* A ABI reconstroi os segmentos para cada AList. O HLE do Project64
     * chama clear_segments antes de interpreta-la; conservar os valores da
     * lista anterior faz um endereco segmentado apontar para uma amostra
     * antiga e deixa a mistura com aspecto de eco. */
    memset(g_audio_segment, 0, sizeof(g_audio_segment));
    g_acmd_voice_count = 0;
    g_acmd_audio_tasks++;

    int tem_envmix = 0;
    for (uint32_t i = 0; i < bytes / 8; i++) {
        uint32_t w0 = *(uint32_t*)(rdram + phys + i * 8);
        if ((w0 >> 24) == 3u) { tem_envmix = 1; break; }
    }
    if (tem_envmix) {
        g_acmd_musical_tasks++;
        /* A lista muda de tamanho conforme os eventos da musica. Capturar a
         * janela inicial permite selecionar depois a lista literalmente igual
         * ao oraculo de 5120 bytes, nao apenas a "proxima" por estimativa. */
        if (g_acmd_musical_tasks <= 16u) {
            char label[32];
            snprintf(label, sizeof(label), "task_%02u", g_acmd_musical_tasks);
            acmd_capture_state_oracle(rdram, phys, bytes, label);
        }
        if (g_acmd_capture_requested) {
            acmd_capture_state_oracle(rdram, phys, bytes, "f5_audio");
            g_acmd_capture_requested = 0;
            printf("[captura] AList de audio armada por F5 salva\n");
            fflush(stdout);
        }
        if (acmd_capture_task_target() > 0 &&
            g_acmd_audio_tasks == (unsigned)acmd_capture_task_target()) {
            char label[32];
            snprintf(label, sizeof(label), "task_%u", g_acmd_audio_tasks);
            acmd_capture_state_oracle(rdram, phys, bytes, label);
            printf("[captura] AList de audio da tarefa %u salva\n", g_acmd_audio_tasks);
            fflush(stdout);
        }
    }

    /* ---------- Bissecao diferencial: microcodigo real x HLE ----------
     * WPJ2_BISSECAO=<n> escolhe a n-esima tarefa de audio. Guardamos o estado
     * inicial, fotografamos a DMEM depois de cada comando do HLE e, no fim,
     * executamos o microcodigo real truncado em N comandos, por busca binaria,
     * ate achar o menor N cuja DMEM diverge. Localiza o comando culpado em
     * ~log2(N) execucoes do nativo em vez de uma por comando, e substitui a
     * leitura de codigo por medicao. */
    static int bis_alvo = -2;
    static uint32_t bis_ini = AUDIO_DMEM_BASE, bis_fim = 0xFC0u;
    /* Limiar de magnitude, em LSB de amostra. Comparar por igualdade exata faz
     * a busca parar na primeira diferenca qualquer - e uma divergencia de 1
     * LSB (-90 dB, inaudivel) mascara um erro grosso que venha depois. Com
     * limiar, a bissecao procura a primeira divergencia que possa ser ouvida.
     * WPJ2_BISSECAO_LIMIAR=0 volta ao comportamento exato. */
    static int32_t bis_limiar = 64;
    if (bis_alvo == -2) {
        const char* t = getenv("WPJ2_BISSECAO");
        bis_alvo = (t && *t) ? atoi(t) : -1;
        /* WPJ2_BISSECAO_FAIXA="ini:fim" em hexadecimal restringe a comparacao
         * a um trecho da DMEM. Comparar a faixa inteira dilui o sinal: uma
         * divergencia irrelevante em outro buffer mascara a que interessa.
         * Sabendo que 70% do DC nasce no caminho wet, olhar so os buses wet
         * faz a primeira divergencia apontar o comando certo. */
        const char* faixa = getenv("WPJ2_BISSECAO_FAIXA");
        if (faixa && *faixa) {
            unsigned a = 0, b = 0;
            if (sscanf(faixa, "%x:%x", &a, &b) == 2 && b > a && b <= SPMEM_SIZE) {
                bis_ini = a; bis_fim = b;
            }
        }
        const char* lim = getenv("WPJ2_BISSECAO_LIMIAR");
        if (lim && *lim) bis_limiar = (int32_t)atoi(lim);
    }
    /* WPJ2_BISSECAO=0 varre: testa toda lista musical e para na primeira que
     * divergir. Como a numeracao de tarefas nao e estavel entre execucoes (o
     * timing varia), mirar uma tarefa fixa nao reproduz o mesmo alvo; varrer
     * reproduz. */
    static int bis_encontrado = 0;
    const uint32_t bis_total = bytes / 8;
    /* Identidade da lista pelo CONTEUDO, nao pela posicao. O indice da tarefa
     * desliza entre execucoes porque o timing varia - "tarefa 30" numa rodada
     * nao e a mesma lista da outra. Sem isto, comparar antes/depois de uma
     * correcao avalia conteudos diferentes e a conclusao nao vale nada.
     * FNV-1a sobre os bytes da AList da um nome estavel. */
    uint32_t bis_hash = 2166136261u;
    for (uint32_t k = 0; k < bytes; k++)
        bis_hash = (bis_hash ^ rdram[(phys + k) ^ 3u]) * 16777619u;
    /* WPJ2_BISSECAO_HASH=<hex> fixa a bissecao numa lista especifica, para
     * iterar correcoes sempre contra o mesmo alvo. */
    static uint32_t bis_hash_alvo = 0;
    static int bis_hash_lido = 0;
    if (!bis_hash_lido) {
        bis_hash_lido = 1;
        const char* h = getenv("WPJ2_BISSECAO_HASH");
        if (h && *h) bis_hash_alvo = (uint32_t)strtoul(h, NULL, 16);
    }
    /* Modo populacao (WPJ2_BISSECAO=-2): mede a divergencia da lista INTEIRA
     * para todas as listas musicais e acumula estatisticas, sem parar na
     * primeira nem fazer busca binaria. E a unica metrica reproduzivel aqui:
     * uma lista especifica nao se repete entre execucoes (o conteudo carrega
     * ponteiros de estado que evoluem), mas a distribuicao se repete. Assim
     * uma correcao pode ser avaliada de verdade em antes/depois. */
    const int bis_pop = (bis_alvo == -2);
    int bis_ativo = (bis_total > 0 && bis_total <= 4096u) &&
                    (bis_hash_alvo ? (bis_hash == bis_hash_alvo)
                                   : (bis_pop ? tem_envmix
                                      : ((bis_alvo > 0 && g_acmd_audio_tasks == (unsigned)bis_alvo) ||
                                         (bis_alvo == 0 && tem_envmix && !bis_encontrado))));
    uint8_t *bis_rdram0 = NULL, *bis_dmem0 = NULL, *bis_snaps = NULL;
    if (bis_ativo) {
        bis_rdram0 = (uint8_t*)malloc(0x800000u);
        bis_dmem0  = (uint8_t*)malloc(SPMEM_SIZE);
        bis_snaps  = (uint8_t*)malloc((size_t)bis_total * SPMEM_SIZE);
        if (!bis_rdram0 || !bis_dmem0 || !bis_snaps) {
            free(bis_rdram0); free(bis_dmem0); free(bis_snaps);
            bis_rdram0 = bis_dmem0 = bis_snaps = NULL;
            bis_ativo = 0;
        }
    }
    if (bis_ativo) {
        /* Igualar o ponto de partida. O nativo recarrega 0xF80 bytes de
         * ucode_data por cima da DMEM antes de executar; sem replicar isso, o
         * HLE parte de outro estado e a comparacao acusaria divergencia ja no
         * comando zero, por um motivo que nao e o defeito procurado. */
        uint32_t ucode_data = *(uint32_t*)(g_spmem + 0xFD8) & 0x1FFFFFFFu;
        if (ucode_data + 0xF80u <= 0x800000u)
            memcpy(g_spmem, rdram + ucode_data, 0xF80u);
        memcpy(bis_rdram0, rdram, 0x800000u);
        memcpy(bis_dmem0, g_spmem, SPMEM_SIZE);
        printf("  [bissecao] tarefa de audio %u: %u comandos; fotografando DMEM por comando\n",
               g_acmd_audio_tasks, bis_total);
        fflush(stdout);
    }

    for (uint32_t i = 0; i < bytes / 8; i++) {
        uint32_t w0 = *(uint32_t*)(rdram + phys + i * 8);
        uint32_t w1 = *(uint32_t*)(rdram + phys + i * 8 + 4);
        uint32_t op = w0 >> 24;
        uint32_t addr = w1 & 0x1FFFFFFFu;
        g_acmd_run++;
        if (op < 16) g_acmd_op[op]++;
        if (op < 16) {
            uint32_t flags = (w0 >> 16) & 0xFFu;
            if (!g_flags_vistos[op][flags]) {
                g_flags_vistos[op][flags] = 1;
                printf("  [acmd-flags] %-10s flags=0x%02X (1a vez, tarefa de audio %u)\n",
                       k_acmd[op], (unsigned)flags, g_acmd_audio_tasks);
                fflush(stdout);
            }
        }

        switch (op) {
            case 2: { /* CLEARBUFF: offset relativo a AUDIO_DMEM_BASE. */
                uint32_t dmem = (w0 + AUDIO_DMEM_BASE) & 0xFFFFu;
                uint32_t n = acmd_align(w1 & 0xFFFFu, 16);
                if (dmem + n <= SPMEM_SIZE) {
                    memset(g_spmem + dmem, 0, n);
                }
                break;
            }
            case 8:  /* SETBUFF: normal e quatro destinos do ENVMIXER. */
                if (((w0 >> 16) & 0xFFu) == 0) {
                    dmem_in  = (w0 + AUDIO_DMEM_BASE) & 0xFFFFu;
                    dmem_out = ((w1 >> 16) + AUDIO_DMEM_BASE) & 0xFFFFu;
                    count    = w1 & 0xFFFFu;
                    g_env_dry_l = dmem_out;
                    g_env_destino_dry_l++;
                } else if (((w0 >> 16) & 0xFFu) == 8) {
                    g_env_destino_aux++;
                    /* A_AUX completa o par configurado pelo SETBUFF normal:
                     * seco L/R e reverberacao L/R. */
                    g_env_dry_r = (w0 + AUDIO_DMEM_BASE) & 0xFFFFu;
                    g_env_wet_l = ((w1 >> 16) + AUDIO_DMEM_BASE) & 0xFFFFu;
                    g_env_wet_r = (w1 + AUDIO_DMEM_BASE) & 0xFFFFu;
                } else {
                    /* MEDICAO: o Project64 decide o ramo por teste de bit
                     * (flags & A_AUX), nunca por igualdade. Se a ROM emitir
                     * qualquer flags fora de 0x00/0x08, os ramos acima nao
                     * rodam e dmem_in/out/count e os ponteiros dry/wet ficam
                     * com o valor da AList anterior. Este log existe para
                     * provar se isso acontece de fato nesta ROM. */
                    g_setbuff_ignorado++;
                    g_setbuff_flags_vistos |= (1u << (((w0 >> 16) & 0xFFu) & 31u));
                    if (g_setbuff_aviso++ < 32u) {
                        printf("  [acmd-setbuff] flags=0x%02X IGNORADO w0=%08X w1=%08X\n",
                               (unsigned)((w0 >> 16) & 0xFFu), w0, w1);
                        fflush(stdout);
                    }
                }
                break;
            case 4: { /* A lista desta ROM usa SETBUFF antes de LOADBUFF. */
                /* A ABI sempre transfere blocos de RDRAM alinhados a 8 bytes.
                 * O Project64 mascara tambem o endereco de origem; sem isso,
                 * uma lista com ponteiro desalinhado desloca a amostra inteira
                 * e contamina os estados das vozes seguintes. */
                uint32_t n = acmd_align(count, 8), src = acmd_address(w1) & ~7u;
                uint32_t dst = dmem_in & ~3u;
                if (acmd_dry_only() && acmd_effect_range(dst, n)) break;
                if (dst + n <= SPMEM_SIZE && src + n <= 0x800000u) {
                    memcpy(g_spmem + dst, rdram + src, n);
                    g_acmd_loaded += n;
                    uint32_t peak = 0;
                    acmd_peak(g_spmem + dst, n, &peak);
                    if (peak > g_acmd_load_peak) g_acmd_load_peak = peak;
                    if (peak && g_acmd_load_trace++ < 16) {
                        printf("  [acmd-load] ram=%06X -> dmem=%03X n=%u pico=%u\n",
                               src, dst, n, peak);
                    }
                }
                break;
            }
            case 6: { /* Nesta ABI, SETBUFF fixa origem/tamanho do SAVEBUFF. */
                /* SAVE usa a mesma regra de alinhamento de LOADBUFF. */
                uint32_t n = acmd_align(count, 8), dst = acmd_address(w1) & ~7u;
                uint32_t src = dmem_out & ~3u;
                if (acmd_dry_only() && acmd_effect_range(src, n)) break;
                if (src + n <= SPMEM_SIZE && dst + n <= 0x800000u) {
                    memcpy(rdram + dst, g_spmem + src, n);
                    g_acmd_saved += n;
                    acmd_peak(g_spmem + src, n, &g_acmd_save_peak);
                }
                break;
            }
            case 10: { /* DMEMMOVE */
                uint32_t src = (w0 + AUDIO_DMEM_BASE) & 0xFFFFu;
                uint32_t dst = ((w1 >> 16) + AUDIO_DMEM_BASE) & 0xFFFFu;
                uint32_t n = acmd_align(w1 & 0xFFFFu, 16);
                if (src + n <= SPMEM_SIZE && dst + n <= SPMEM_SIZE) {
                    /* O microcodigo realiza os acessos em ordem crescente
                     * no espaco logico do N64. A DMEM fica byte-swapped na
                     * memoria do host, portanto cada acesso de byte equivale
                     * a endereco ^ 3 (alist_u8 do Project64). */
                    for (uint32_t j = 0; j < n; j++)
                        g_spmem[(dst + j) ^ 3u] = g_spmem[(src + j) ^ 3u];
                }
                break;
            }
            case 11: { /* LOADADPCM: nesta ABI tambem carrega a tabela do POLEF. */
                uint32_t n = acmd_align(w0 & 0xFFFFu, 8);
                if (n > sizeof(g_pole_coeff)) n = sizeof(g_pole_coeff);
                uint32_t src = acmd_address(w1);
                if (src + n <= 0x800000u) {
                    for (uint32_t j = 0; j + 1 < n; j += 2)
                        g_pole_coeff[j / 2] = acmd_read_s16(rdram + src + j);
                }
                break;
            }
            case 1:
                if (!acmd_fast_mode())
                    acmd_adpcm(rdram, dmem_in, dmem_out, count,
                               (w0 >> 16) & 0xFFu, acmd_address(w1));
                break;
            case 5:
                if (!acmd_fast_mode())
                    acmd_resample(rdram, dmem_in, dmem_out, count, w0 & 0xFFFFu,
                                  (w0 >> 16) & 0xFFu, acmd_address(w1));
                break;
            case 9: { /* SETVOL: L, R, respectivas rampas e envio auxiliar. */
                uint32_t f = (w0 >> 16) & 0xFFu;
                int32_t value = (int16_t)(w0 & 0xFFFFu);
                if (f & 8u) { g_env_dry = (int16_t)value; g_env_wet = (int16_t)w1; }
                else if (f & 2u) {
                    if (f & 4u) g_env_vol_l = (int16_t)value;
                    else { g_env_target_l = (int16_t)value; g_env_rate_l = (int32_t)w1; }
                } else {
                    if (f & 4u) g_env_vol_r = (int16_t)value;
                    else { g_env_target_r = (int16_t)value; g_env_rate_r = (int32_t)w1; }
                }
                break;
            }
            case 3: { /* ENVMIXER: mistura o PCM reamostrado nos quatro buses. */
                if (acmd_fast_mode()) break;
                uint32_t state = acmd_address(w1);
                if ((!acmd_music_only() || acmd_is_bgm_state(state)) &&
                    acmd_voice_filter(state))
                    acmd_envmix(rdram, (w0 >> 16) & 0xFFu, state, dmem_in,
                                acmd_align(count, 16));
                break;
            }
            case 12: { /* MIXER, ganho Q15, soma saturada de PCM16 big-endian. */
                uint32_t n = acmd_align(count, 32);
                int32_t gain = (int16_t)(w0 & 0xFFFFu);
                uint32_t src = ((w1 >> 16) + AUDIO_DMEM_BASE) & 0xFFFFu;
                uint32_t dst = (w1 + AUDIO_DMEM_BASE) & 0xFFFFu;
                if (acmd_dry_only() &&
                    (acmd_effect_range(src, n) || acmd_effect_range(dst, n)))
                    break;
                if (src + n <= SPMEM_SIZE && dst + n <= SPMEM_SIZE) {
                    uint32_t src_peak = 0, dst_peak = 0;
                    acmd_peak(g_spmem + src, n, &src_peak);
                    acmd_peak(g_spmem + dst, n, &dst_peak);
                    if (src_peak && g_acmd_mix_trace++ < 16) {
                        printf("  [acmd-mix] n=%u ganho=%d src=%03X pico=%u dst=%03X pico=%u\n",
                               n, gain, src, src_peak, dst, dst_peak);
                    }
                    /* MIXER e a etapa final de INTERLEAVE operam na ordem
                       bruta da DMEM. Diferente de ADPCM/ENVMIX, aqui nao ha
                       LH/SH: e o mesmo int16* direto usado pelo HLE do PJ64. */
                    int16_t* s = (int16_t*)(g_spmem + src);
                    int16_t* d = (int16_t*)(g_spmem + dst);
                    for (uint32_t j = 0; j + 1 < n; j += 2) {
                        uint32_t k = j >> 1;
                        d[k] = acmd_clamp16((int32_t)d[k] + (((int32_t)s[k] * gain) >> 15));
                    }
                }
                if (dst + n <= SPMEM_SIZE) acmd_peak(g_spmem + dst, n, &g_acmd_mix_peak);
                break;
            }
            case 13: { /* INTERLEAVE: canais mono esquerdo/direito -> stereo. */
                uint32_t n = acmd_align(count, 16);
                uint32_t dst = dmem_out;
                uint32_t left = ((w1 >> 16) + AUDIO_DMEM_BASE) & 0xFFFFu;
                uint32_t right = (w1 + AUDIO_DMEM_BASE) & 0xFFFFu;
                if (dst + n * 2 <= SPMEM_SIZE && left + n <= SPMEM_SIZE && right + n <= SPMEM_SIZE) {
                    uint16_t* d = (uint16_t*)(g_spmem + dst);
                    const uint16_t* l = (const uint16_t*)(g_spmem + left);
                    const uint16_t* r = (const uint16_t*)(g_spmem + right);
                    /* Host little-endian: a palavra N64 em memoria exige a
                       ordem r2,l2,r1,l1, conforme alist_interleave do PJ64. */
                    for (uint32_t j = 0; j < n / 4; j++) {
                        uint16_t l1 = *(l++), l2 = *(l++);
                        uint16_t r1 = *(r++), r2 = *(r++);
                        *(d++) = r2; *(d++) = l2; *(d++) = r1; *(d++) = l1;
                    }
                }
                break;
            }
            case 7: { /* SEGMENT: tabela de enderecos da Audio ABI. */
                uint32_t seg = (w0 >> 16) & 0x0Fu;
                g_audio_segment[seg] = w1 & 0x1FFFFFFFu;
                break;
            }
            case 15:
                g_audio_loop = acmd_address(w1);
                break;
            case 14: { /* POLEF: filtro recursivo de 8 amostras, estado na RDRAM. */
                uint32_t n = (count + 15u) & ~15u;
                uint32_t src = dmem_in, dst = dmem_out;
                int32_t gain = (int16_t)(w0 & 0xFFFFu);
                uint32_t state = acmd_address(w1);
                if (!n || src + n > SPMEM_SIZE || dst + n > SPMEM_SIZE || state + 8 > 0x800000u) break;
                /* Perfil diagnostico: mantem a topologia de buffers, apenas
                 * transforma POLEF em passagem direta. Se o chiado sumir,
                 * a divergencia esta no filtro recursivo, nao nas vozes ou na
                 * saida WinMM. */
                if (acmd_bypass_polef()) {
                    memmove(g_spmem + dst, g_spmem + src, n);
                    break;
                }
                int16_t last1 = ((w0 >> 16) & 1u) ? 0 : acmd_read_s16(rdram + state + 4);
                int16_t last2 = ((w0 >> 16) & 1u) ? 0 : acmd_read_s16(rdram + state + 6);
                /* O HLE de referencia conserva a segunda metade original para
                 * o termo de atraso, mas escala a tabela ativa antes do rdot.
                 * A versao anterior so simulava a escala por frame e gravava
                 * o estado com swizzle de halfword: isso realimentava o filtro
                 * com quatro amostras fora de ordem e se manifesta como eco. */
                int16_t h2_before[8];
                for (uint32_t i = 0; i < 8; i++) {
                    h2_before[i] = g_pole_coeff[8 + i];
                    g_pole_coeff[8 + i] = (int16_t)(((int32_t)g_pole_coeff[8 + i] * gain) >> 14);
                }
                for (uint32_t base = 0; base < n; base += 16) {
                    int16_t frame[8], out[8];
                    for (uint32_t i = 0; i < 8; i++) {
                        frame[i] = acmd_read_s16(g_spmem + src + base + i * 2);
                    }
                    for (uint32_t i = 0; i < 8; i++) {
                        int32_t accum = (int32_t)frame[i] * gain;
                        accum = acmd_mac32(accum, g_pole_coeff[i], last1);
                        accum = acmd_mac32(accum, h2_before[i], last2);
                        for (uint32_t j = 0; j < i; j++)
                            accum = acmd_mac32(accum, g_pole_coeff[8 + j], frame[i - 1 - j]);
                        int32_t v = accum >> 14;
                        if (v > 32767) v = 32767;
                        if (v < -32768) v = -32768;
                        out[i] = (int16_t)v;
                        acmd_write_s16(g_spmem + dst + base + i * 2, v);
                    }
                    last1 = out[6]; last2 = out[7];
                }
                /* A ABI salva dois words de 32 bits crus, nao quatro SH.
                 * Copiar os ultimos oito bytes preserva a mesma disposicao que
                 * o proximo POLEF le por LH na RDRAM. */
                memcpy(rdram + state, g_spmem + dst + n - 8, 8);
                break;
            }
            default: break;
        }
        if (bis_ativo)
            memcpy(bis_snaps + (size_t)i * SPMEM_SIZE, g_spmem, SPMEM_SIZE);
    }
    if (bis_ativo) {
        /* Busca binaria pelo menor N cuja DMEM, apos N comandos, difira entre
         * o microcodigo real e o HLE. Comparamos so 0x000..0xFBF: a OSTask
         * mora em 0xFC0 e nos alteramos data_size ali para truncar a lista,
         * entao inclui-la acusaria uma diferenca que nos mesmos criamos. */
        uint8_t* dmem_fim = (uint8_t*)malloc(SPMEM_SIZE);
        uint8_t* rdram_fim = (uint8_t*)malloc(0x800000u);
        if (dmem_fim && rdram_fim) {
            memcpy(dmem_fim, g_spmem, SPMEM_SIZE);
            memcpy(rdram_fim, rdram, 0x800000u);
            /* Validacao do instrumento antes de confiar no veredito: a lista
             * inteira, sem nenhum SPNOOP, e o caso conhecido. Se o nativo
             * falhar aqui, nada abaixo tem significado. Tambem medimos a
             * divergencia total - ela e o teto do que a busca pode achar. */
            uint32_t lo = 1, hi = bis_total, primeiro = 0, testes = 0;
            memcpy(rdram, bis_rdram0, 0x800000u);
            memcpy(g_spmem, bis_dmem0, SPMEM_SIZE);
            int rc_cheio = wpj2_native_audio_rsp(rdram, g_spmem);
            int32_t pior_cheio = 0;
            {
                const int16_t* na = (const int16_t*)(g_spmem + bis_ini);
                const int16_t* hl = (const int16_t*)(bis_snaps +
                                     (size_t)(bis_total - 1) * SPMEM_SIZE + bis_ini);
                for (uint32_t k = 0; k < (bis_fim - bis_ini) / 2; k++) {
                    int32_t d = (int32_t)na[k] - (int32_t)hl[k];
                    if (d < 0) d = -d;
                    if (d > pior_cheio) pior_cheio = d;
                }
            }
            if (bis_pop) {
                /* Estatistica acumulada: e o numero que permite comparar
                 * antes/depois de uma correcao. Impresso a cada 10 listas
                 * para sair no log mesmo com o processo terminado a forca. */
                static unsigned pop_n, pop_falhas, pop_acima1;
                static double pop_soma;
                static int32_t pop_max;
                if (rc_cheio != 0) pop_falhas++;
                else {
                    pop_n++;
                    pop_soma += pior_cheio;
                    if (pior_cheio > pop_max) pop_max = pior_cheio;
                    if (pior_cheio > 328) pop_acima1++;   /* >1% do fundo */
                }
                if (pop_n && pop_n % 10u == 0u)
                    printf("  [populacao] %u listas: media=%.0f LSB (%.2f%%),"
                           " max=%d (%.2f%%), acima de 1%%=%u (%.0f%%), falhas=%u\n",
                           pop_n, pop_soma / pop_n,
                           pop_soma / pop_n * 100.0 / 32768.0,
                           pop_max, pop_max * 100.0 / 32768.0,
                           pop_acima1, pop_acima1 * 100.0 / pop_n, pop_falhas);
            } else
            printf("  [bissecao] lista %08X (tarefa %u, %u cmds):"
                   " rc=%d, maior diferenca=%d LSB (%.2f%%)\n",
                   bis_hash, g_acmd_audio_tasks, bis_total,
                   rc_cheio, pior_cheio, pior_cheio * 100.0 / 32768.0);
            /* Censo das vozes desta lista. Como duas listas rodam o MESMO
             * codigo nosso e uma acerta (0,02%) enquanto a outra erra 15,84%,
             * a diferenca esta no conteudo, nao no filtro. Listar cada
             * ENVMIXER com seu alvo e estado mostra o gatilho diretamente -
             * a suspeita e uma voz com target=0 (em extincao) que so a lista
             * ruim possui. */
            if (!bis_pop) {
                unsigned nvozes = 0, nzeradas = 0;
                for (uint32_t i = 0; i < bis_total; i++) {
                    uint32_t cw0 = *(uint32_t*)(bis_rdram0 + phys + (size_t)i * 8);
                    uint32_t cw1 = *(uint32_t*)(bis_rdram0 + phys + (size_t)i * 8 + 4);
                    if ((cw0 >> 24) != 3u) continue;
                    uint32_t st = cw1 & 0x1FFFFFFFu;
                    if (st + 40u > 0x800000u) continue;
                    int32_t tl = *(int32_t*)(bis_rdram0 + st + 8);
                    int32_t tr = *(int32_t*)(bis_rdram0 + st + 12);
                    int32_t vl = *(int32_t*)(bis_rdram0 + st + 32);
                    int32_t sq = *(int32_t*)(bis_rdram0 + st + 24);
                    nvozes++;
                    if (tl == 0 && tr == 0) nzeradas++;
                    if (nvozes <= 12u)
                        printf("  [bissecao]   voz cmd=%3u fl=0x%02X st=0x%06X"
                               " target=%d/%d value=%d exp_seq-value=%d\n",
                               i, (unsigned)((cw0 >> 16) & 0xFFu), st,
                               tl, tr, vl, sq - vl);
                }
                printf("  [bissecao]   vozes=%u, com target zerado=%u\n",
                       nvozes, nzeradas);
            }
            fflush(stdout);
            if (rc_cheio != 0) {
                printf("  [bissecao] ABORTADO: o nativo nao roda nem a lista"
                       " completa; a busca nao teria significado.\n");
                lo = 1; hi = 0;   /* pula a busca */
            }
            /* No modo populacao so interessa a divergencia da lista inteira;
             * a busca binaria custa ~10 execucoes do nativo por lista e nao
             * acrescenta nada a estatistica. */
            if (bis_pop) { lo = 1; hi = 0; }
            while (lo <= hi) {
                uint32_t mid = lo + (hi - lo) / 2;
                memcpy(rdram, bis_rdram0, 0x800000u);
                memcpy(g_spmem, bis_dmem0, SPMEM_SIZE);
                /* Truncar por data_size alterava a estrutura da tarefa e fazia
                 * o microcodigo abortar, deixando a RDRAM zerada. O harness
                 * lia isso como divergencia e a busca convergia para "onde o
                 * nativo falha", nao para "onde o resultado difere". Aqui a
                 * lista mantem o tamanho e os comandos apos N viram SPNOOP. */
                for (uint32_t k = mid; k < bis_total; k++) {
                    *(uint32_t*)(rdram + phys + (size_t)k * 8) = 0;
                    *(uint32_t*)(rdram + phys + (size_t)k * 8 + 4) = 0;
                }
                int rc = wpj2_native_audio_rsp(rdram, g_spmem);
                testes++;
                if (rc != 0) {
                    /* Sem resultado comparavel. Antes isto contava como
                     * divergencia e contaminava o veredito. */
                    printf("  [bissecao] AVISO: nativo falhou (rc=%d) em N=%u;"
                           " par nao comparavel\n", rc, mid);
                    fflush(stdout);
                    break;
                }
                /* So a faixa dos buffers de audio (0x5C0..0xFBF). Abaixo de
                 * AUDIO_DMEM_BASE fica o estado privado do microcodigo, que o
                 * HLE legitimamente mantem em variaveis C: comparar ali acusa
                 * diferenca de implementacao, nao o defeito procurado. */
                /* Comparacao por magnitude de amostra, nao por igualdade de
                 * bytes. Ambos os lados tem o mesmo layout word-swapped, entao
                 * ler int16 cru dos dois e valido para medir diferenca. */
                const int16_t* na = (const int16_t*)(g_spmem + bis_ini);
                const int16_t* hl = (const int16_t*)(bis_snaps +
                                     (size_t)(mid - 1) * SPMEM_SIZE + bis_ini);
                int32_t pior = 0;
                for (uint32_t k = 0; k < (bis_fim - bis_ini) / 2; k++) {
                    int32_t d = (int32_t)na[k] - (int32_t)hl[k];
                    if (d < 0) d = -d;
                    if (d > pior) pior = d;
                }
                int difere = (rc != 0) || (pior > bis_limiar);
                if (difere) { primeiro = mid; if (mid == 1) break; hi = mid - 1; }
                else lo = mid + 1;
            }
            if (primeiro) {
                uint32_t w0 = *(uint32_t*)(bis_rdram0 + phys + (size_t)(primeiro - 1) * 8);
                uint32_t w1 = *(uint32_t*)(bis_rdram0 + phys + (size_t)(primeiro - 1) * 8 + 4);
                uint32_t op = w0 >> 24;
                /* Reexecuta o nativo exatamente em N para poder apontar onde. */
                memcpy(rdram, bis_rdram0, 0x800000u);
                memcpy(g_spmem, bis_dmem0, SPMEM_SIZE);
                for (uint32_t k = primeiro; k < bis_total; k++) {
                    *(uint32_t*)(rdram + phys + (size_t)k * 8) = 0;
                    *(uint32_t*)(rdram + phys + (size_t)k * 8 + 4) = 0;
                }
                int rc_rep = wpj2_native_audio_rsp(rdram, g_spmem);
                if (rc_rep != 0)
                    printf("  [bissecao]   AVISO: rc=%d na reexecucao\n", rc_rep);
                const uint8_t* hle = bis_snaps + (size_t)(primeiro - 1) * SPMEM_SIZE;
                uint32_t off = bis_ini, difs = 0;
                while (off < bis_fim && g_spmem[off] == hle[off]) off++;
                for (uint32_t k = bis_ini; k < bis_fim; k++)
                    if (g_spmem[k] != hle[k]) difs++;
                /* Magnitude, que e o que decide se e audivel: 1 LSB e -90 dB,
                 * 3277 e 10% do fundo de escala. */
                int32_t pior = 0; uint32_t pior_off = bis_ini;
                for (uint32_t k = bis_ini; k + 1 < bis_fim; k += 2) {
                    int32_t d = (int32_t)*(const int16_t*)(g_spmem + k) -
                                (int32_t)*(const int16_t*)(hle + k);
                    if (d < 0) d = -d;
                    if (d > pior) { pior = d; pior_off = k; }
                }
                printf("  [bissecao]   maior diferenca de amostra: %d LSB"
                       " (%.2f%% do fundo de escala) em 0x%03X\n",
                       pior, pior * 100.0 / 32768.0, pior_off);
                bis_encontrado = 1;   /* para a varredura na primeira falha */
                printf("  [bissecao] PRIMEIRA DIVERGENCIA na tarefa de audio %u,"
                       " comando %u de %u (%u testes)\n",
                       g_acmd_audio_tasks, primeiro, bis_total, testes);
                printf("  [bissecao]   %s flags=0x%02X w0=%08X w1=%08X\n",
                       op < 16 ? k_acmd[op] : "?", (unsigned)((w0 >> 16) & 0xFFu), w0, w1);
                printf("  [bissecao]   DMEM difere em %u bytes; primeiro em 0x%03X"
                       " (nativo=%02X hle=%02X)\n", difs, off, g_spmem[off], hle[off]);
                printf("  [bissecao]   buses: dry_l=0x%03X dry_r=0x%03X wet_l=0x%03X wet_r=0x%03X\n",
                       g_env_dry_l, g_env_dry_r, g_env_wet_l, g_env_wet_r);
                if (op == 3u) {
                    /* O bloco de 80 bytes que a voz retoma. O PJ64 tem duas
                     * variantes de envmix com layouts diferentes nos mesmos
                     * offsets: a exponencial guarda target como int32 em +4 e
                     * exp_rate em +8; a outra guarda target>>16 como int16 em
                     * +4 e step em +8. Os valores distinguem as duas sem
                     * precisar adivinhar - basta ver se +4 tem magnitude de
                     * 32 bits ou de 16. */
                    uint32_t st = w1 & 0x1FFFFFFFu;
                    if (st + 80u <= 0x800000u) {
                        printf("  [bissecao]   estado da voz em 0x%06X:\n", st);
                        printf("  [bissecao]     como u32:");
                        for (uint32_t k = 0; k < 40u; k += 4)
                            printf(" +%u=%d", k, *(int32_t*)(bis_rdram0 + st + k));
                        printf("\n  [bissecao]     como s16:");
                        for (uint32_t k = 0; k < 24u; k += 2)
                            printf(" +%u=%d", k,
                                   (int)*(int16_t*)((uintptr_t)(bis_rdram0 + st + k) ^ 2u));
                        /* `rdram` acabou de receber a execucao do microcodigo
                         * real truncada neste comando, entao contem o estado
                         * que ELE gravou. Comparar com o de entrada mostra a
                         * semantica correta da rampa sem precisar deduzi-la. */
                        printf("\n  [bissecao]     APOS o microcodigo real:");
                        for (uint32_t k = 0; k < 40u; k += 4)
                            printf(" +%u=%d", k, *(int32_t*)(rdram + st + k));
                        printf("\n  [bissecao]     delta (real - entrada):");
                        for (uint32_t k = 0; k < 40u; k += 4)
                            printf(" +%u=%d", k,
                                   *(int32_t*)(rdram + st + k) -
                                   *(int32_t*)(bis_rdram0 + st + k));
                        printf("\n");
                    }
                }
            } else if (bis_alvo > 0) {
                /* Na varredura isto sairia a cada lista musical e afogaria o
                 * log; so reportamos ausencia quando a tarefa foi pedida. */
                printf("  [bissecao] tarefa %u: nenhuma divergencia em %u comandos (%u testes)\n",
                       g_acmd_audio_tasks, bis_total, testes);
            }
            /* Devolve o estado que o HLE produziu: o jogo continua dali. */
            memcpy(rdram, rdram_fim, 0x800000u);
            memcpy(g_spmem, dmem_fim, SPMEM_SIZE);
            fflush(stdout);
        }
        free(dmem_fim); free(rdram_fim);
        free(bis_rdram0); free(bis_dmem0); free(bis_snaps);
    }

    /* A captura comum registra a AList *antes* do HLE, o que e ideal para
       comparar comandos. Para localizar chiados cumulativos precisamos tambem
       ver o estado que ADPCM/RESAMPLE/ENVMIXER deixou para a proxima lista.
       Esta segunda captura e estritamente opt-in: nao abre arquivo nem toca
       no caminho de release sem WPJ2_AUDIO_STATE_POST_CAPTURE. */
    if (tem_envmix) {
        static unsigned post_musical_tasks;
        const char* post_dir = getenv("WPJ2_AUDIO_STATE_POST_CAPTURE");
        if (post_dir && *post_dir && post_musical_tasks < 16u) {
            char label[32];
            snprintf(label, sizeof(label), "post_task_%02u", ++post_musical_tasks);
            acmd_capture_state_dir(rdram, phys, bytes, post_dir, label);
        }
    }
    QueryPerformanceCounter(&fim);
    QueryPerformanceFrequency(&freq);
    if (freq.QuadPart > 0) {
        uint64_t us = (uint64_t)(((fim.QuadPart - inicio.QuadPart) * 1000000ull) /
                                 (uint64_t)freq.QuadPart);
        g_acmd_last_us = us;
        g_acmd_total_us += us;
        g_acmd_samples++;
        if (us > g_acmd_peak_us) g_acmd_peak_us = us;
    }
}

/* Harness offline para confrontar exatamente uma AList do Project64. A
 * aplicação normal continua chamando run_acmd_list pelo fluxo de OSTask; esta
 * função existe para a ferramenta de regressão e força o caminho HLE C. */
int wpj2_hle_audio_rsp(uint8_t* rdram, uint8_t* spmem) {
    if (!rdram || !spmem) return -1;
    memset(g_spmem, 0, sizeof(g_spmem));
    memcpy(g_spmem, spmem, 0x1000);
    if (sp_word(TASK_OFFSET) != 2u) return -2;
    uint32_t list = sp_word(TASK_OFFSET + 0x30) & 0x1FFFFFFFu;
    uint32_t bytes = sp_word(TASK_OFFSET + 0x34);
    if (!list || bytes < 8u || bytes > 0x8000u || list + bytes > 0x800000u) return -3;
    g_acmd_force_hle = 1;
    run_acmd_list(rdram, list, bytes);
    g_acmd_force_hle = 0;
    memcpy(spmem, g_spmem, 0x1000);
    return 0;
}

/* Campos da OSTask, na ordem em que a libultra os declara. */
static void dump_task(uint8_t* rdram) {
    static const char* k_type[] = { "?", "graficos (M_GFXTASK)", "audio (M_AUDTASK)" };
    uint32_t type = sp_word(TASK_OFFSET + 0x00);

    printf("  [task] tipo=%u %s  flags=0x%X\n", type,
           type < 3 ? k_type[type] : "desconhecido", sp_word(TASK_OFFSET + 0x04));
    printf("  [task] ucode      boot=0x%08X (%u B)  main=0x%08X (%u B)  data=0x%08X (%u B)\n",
           sp_word(TASK_OFFSET + 0x08), sp_word(TASK_OFFSET + 0x0C),
           sp_word(TASK_OFFSET + 0x10), sp_word(TASK_OFFSET + 0x14),
           sp_word(TASK_OFFSET + 0x18), sp_word(TASK_OFFSET + 0x1C));
    printf("  [task] pilha=0x%08X (%u B)  saida=0x%08X (%u B)\n",
           sp_word(TASK_OFFSET + 0x20), sp_word(TASK_OFFSET + 0x24),
           sp_word(TASK_OFFSET + 0x28), sp_word(TASK_OFFSET + 0x2C));
    printf("  [task] lista=0x%08X (%u B)  yield=0x%08X (%u B)\n",
           sp_word(TASK_OFFSET + 0x30), sp_word(TASK_OFFSET + 0x34),
           sp_word(TASK_OFFSET + 0x38), sp_word(TASK_OFFSET + 0x3C));
    fflush(stdout);

    if (type == 2) {
        uint32_t udata = sp_word(TASK_OFFSET + 0x18) & 0x1FFFFFFFu;
        if (udata + 0x34 <= 0x800000u) {
            printf("  [audio-abi] dados=%06X assinatura=%08X +10=%08X +28=%08X +30=%08X\n",
                   udata, *(uint32_t*)(rdram + udata), *(uint32_t*)(rdram + udata + 0x10),
                   *(uint32_t*)(rdram + udata + 0x28), *(uint32_t*)(rdram + udata + 0x30));
        }
        dump_acmd_list(rdram, sp_word(TASK_OFFSET + 0x30) & 0x1FFFFFFFu,
                       sp_word(TASK_OFFSET + 0x34));
    } else if (type == 1) {
        /* Tarefa grafica: a lista de exibicao e uma sequencia de comandos de 8
           bytes, e o byte mais alto e o opcode. Mostrar os primeiros ja diz qual
           microcodigo o jogo usa e o que ele esta desenhando. */
        uint32_t p = sp_word(TASK_OFFSET + 0x30) & 0x1FFFFFFFu;
        uint32_t n = sp_word(TASK_OFFSET + 0x34);
        if (p && n >= 8 && n <= 0x20000 && p + n < 0x800000u) {
            printf("  [gfx]  %u comandos em 0x%08X; primeiros:\n", n / 8,
                   p | 0x80000000u);
            for (uint32_t i = 0; i < n / 8 && i < 10; i++) {
                printf("  [gfx]    %2u  op=0x%02X  w0=0x%08X w1=0x%08X\n", i,
                       *(uint8_t*)(rdram + ((p + i * 8) ^ 3)),
                       *(uint32_t*)(rdram + p + i * 8),
                       *(uint32_t*)(rdram + p + i * 8 + 4));
            }
            fflush(stdout);
        }
    }
}

void func_800CD060(uint8_t* rdram, recomp_context* ctx) {
    int32_t  dir  = (int32_t)ctx->r4;
    uint32_t sp   = (uint32_t)ctx->r5 & 0x1FFFu;
    uint32_t dram = (uint32_t)ctx->r6 & 0x1FFFFFFFu;
    uint32_t size = (uint32_t)ctx->r7;

    if (sp + size > SPMEM_SIZE) {
        printf("  [rsp]  DMA fora da memoria do RSP: sp=0x%X size=%u\n", sp, size);
        fflush(stdout);
        ctx->r2 = (gpr)(int32_t)-1;
        return;
    }

    /* Cuidado com o delay slot ao ler o desvio: `bnez $t9` em 0x800CD0AC salta
       para 0x800CD0C8, que escreve SP_RD_LEN. Ou seja, dir != 0 traz da RDRAM
       para o RSP, e dir == 0 leva do RSP para a RDRAM. Eu tinha lido ao
       contrario, e o efeito foi pior que nao copiar nada: a primeira coisa que o
       jogo faz e mandar a OSTask para a DMEM, e a copia invertida gravava zeros
       por cima da tarefa dele. O `ucode_boot` nulo que apareceu em seguida era
       obra minha, nao do jogo. */
    if (dir == 0) memcpy(rdram + dram, g_spmem + sp, size);   /* RSP -> RDRAM */
    else          memcpy(g_spmem + sp, rdram + dram, size);   /* RDRAM -> RSP */
    g_sp_dma++;

    if (g_sp_dma <= 8) {
        printf("  [rsp]  dma %llu: %s sp 0x%04X %s ram 0x%08X  %u bytes\n",
               (unsigned long long)g_sp_dma,
               sp < 0x1000 ? "DMEM" : "IMEM", sp,
               dir == 0 ? "->" : "<-", dram | 0x80000000u, size);
        fflush(stdout);
    }

    /* A tarefa chegou inteira na DMEM: e a primeira vez que da para ver o que o
       jogo esta pedindo ao RSP. */
    /* Mostrar as N primeiras tarefas nao serve: as primeiras sao todas de audio,
       e foi assim que eu conclui que nao existiam tarefas graficas. Agora as
       duas primeiras de *cada tipo* aparecem. */
    if (dir != 0 && sp == TASK_OFFSET && size >= 0x40) {
        uint32_t t = sp_word(TASK_OFFSET + 0x00);
        int idx = (t < 4) ? (int)t : 0;
        if (g_vistas[idx] < 2) {
            g_vistas[idx]++;
            printf("  [task] tarefa de tipo %u, %da vista:\n", t, g_vistas[idx]);
            dump_task(rdram);
        }
    }
    ctx->r2 = 0;
}

uint64_t rsp_sp_dma(void) { return g_sp_dma; }
