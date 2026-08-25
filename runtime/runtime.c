/* Runtime de sondagem para o CPU recompilado do Wonder Project J2.
 *
 * Isto nao e um port. E o minimo de maquina para o MIPS recompilado executar:
 * um espaco de enderecos, a imagem da ROM, os callbacks que recomp.h declara e
 * instrumentacao para mostrar ate onde o boot chega antes de exigir hardware
 * de verdade.
 *
 * Mapa (indexado como base[vaddr - 0x80000000]):
 *   +0x00000000  8 MB de RDRAM                    (KSEG0 0x80000000)
 *   +0x20000000  os mesmos 8 MB, mapeados de novo (KSEG1 0xA0000000)
 *   +0x24000000  MMIO, comprometido ao ser tocado (0xA4000000+)
 *   +0x30000000  a imagem do cartucho             (0xB0000000)
 *
 * KSEG1 precisa ser um alias real de KSEG0 - a libultra escreve buffers de DMA
 * pela janela nao cacheada e le pela cacheada - por isso as duas vistas usam um
 * unico file mapping em vez de duas alocacoes.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"
#include "funcs.h"
#include "trace.h"
#include "legendas.h"

#define VBASE           0x80000000u
#define TOTAL_SIZE      0x40000000ull   /* cobre vaddr 0x80000000..0xC0000000 */
#define RDRAM_SIZE      0x00800000ull   /* 8 MB; o alias cobre o pak de expansao */
#define KSEG1_OFFSET    0x20000000ull
#define CART_OFFSET     0x30000000ull

/* A primeira textura do caminho START e usada pelo RSP a partir deste endereco.
 * As faixas de PI DMA mostram que ela nao vem diretamente do cartucho. Para
 * distinguir "nao foi carregada" de "foi descompactada/copiada pela CPU", a
 * pagina fica somente-leitura e registramos as primeiras escritas do MIPS.
 * KSEG0 e KSEG1 sao duas views do mesmo backing, mas a protecao pertence a
 * cada view; por isso ambas sao armadas. */
#define TEXTURE_WATCH_ADDR 0x802CEF20u
#define WATCH_PAGE_SIZE    0x1000u
#define WATCH_WRITE_MAX    32
#define WATCH_FAULT_MAX    8192

uint8_t* g_rdram = NULL;
static HANDLE g_map = NULL;
static uint8_t* g_rom = NULL;
static size_t g_rom_size = 0;
static volatile LONG g_reported = 0;
static double g_timeout_s = 20.0;
static unsigned g_frame_samples = 4;    /* quadros periodicos do watchdog */
static const char* g_out_prefix = "";     /* separa saidas de corridas paralelas */
static int g_video_preview = 0;
#ifdef WPJ2_RELEASE
static int g_diagnostics = 0;
#else
static int g_diagnostics = 1;
#endif


/* ------------------------------------------------------------------ */
/* Instrumentacao                                                      */
/* ------------------------------------------------------------------ */

static const struct { uint32_t base; const char* name; } k_mmio[] = {
    { 0xA3F00000u, "RDRAM regs" },
    { 0xA4000000u, "RSP DMEM/IMEM" },
    { 0xA4040000u, "SP regs" },
    { 0xA4080000u, "SP_PC" },
    { 0xA4100000u, "DP command" },
    { 0xA4200000u, "DP span" },
    { 0xA4300000u, "MI (interrupcoes)" },
    { 0xA4400000u, "VI (video)" },
    { 0xA4500000u, "AI (audio)" },
    { 0xA4600000u, "PI (DMA do cart)" },
    { 0xA4700000u, "RI (controle RDRAM)" },
    { 0xA4800000u, "SI (controles)" },
};

#define MMIO_COUNT (sizeof(k_mmio) / sizeof(k_mmio[0]))
static int g_mmio_hits[MMIO_COUNT];
static uint32_t g_mmio_first[MMIO_COUNT];
static uint64_t g_lookup_calls = 0;
static uint32_t g_last_lookup = 0;

static int g_texture_watch_armed = 0;
static volatile LONG g_texture_watch_count = 0;
static volatile LONG g_texture_watch_faults = 0;
static volatile LONG g_texture_watch_zeroes = 0;
static uint32_t g_texture_watch_addr[WATCH_WRITE_MAX];
static uint32_t g_texture_watch_func[WATCH_WRITE_MAX];
static uint8_t g_texture_watch_value[WATCH_WRITE_MAX];
static uint8_t* g_texture_watch_rearm = NULL;
static uint8_t* g_texture_watch_pending_addr = NULL;
static uint32_t g_texture_watch_pending_vaddr = 0;
static uint32_t g_texture_watch_pending_func = 0;
static int g_texture_watch_pending = 0;

static const char* mmio_name_for(uint32_t vaddr, int* index_out) {
    int best = -1;
    for (size_t i = 0; i < MMIO_COUNT; i++) {
        if (vaddr >= k_mmio[i].base && (best < 0 || k_mmio[i].base > k_mmio[best].base)) {
            best = (int)i;
        }
    }
    if (best >= 0 && vaddr < k_mmio[best].base + 0x100000u) {
        *index_out = best;
        return k_mmio[best].name;
    }
    *index_out = -1;
    return NULL;
}

#ifdef RECOMP_TRACING
/* Um contador por funcao recompilada, indexado como a tabela de lookup, mais a
   ordem em que cada funcao foi alcancada pela primeira vez. */
static uint64_t* g_trace_counts = NULL;
static uint64_t g_trace_total = 0;
static size_t g_trace_distinct = 0;
#define TRACE_ORDER_MAX 64
static uint32_t g_trace_order[TRACE_ORDER_MAX];
static uint32_t g_last_traced = 0;

/* Trilha circular das ultimas funcoes alcancadas. Um contador por funcao diz o
   *quanto*; so a ordem diz *onde* uma thread desistiu. */
#define TRAIL_MAX 32
static uint32_t g_trail[TRAIL_MAX];
static uint64_t g_trail_pos = 0;

void trace_trail(const char* label) {
    printf("  [trilha] %s:", label);
    uint64_t n = g_trail_pos < TRAIL_MAX ? g_trail_pos : TRAIL_MAX;
    for (uint64_t i = n; i > 0; i--) {
        printf(" %08X", g_trail[(g_trail_pos - i) % TRAIL_MAX]);
    }
    printf("\n");
    fflush(stdout);
}

uint32_t trace_last_func(void) { return g_last_traced; }

void recomp_trace(uint32_t vram) {
    g_last_traced = vram;
    g_trail[g_trail_pos++ % TRAIL_MAX] = vram;
    g_trace_total++;
    if (!g_trace_counts) return;
    size_t lo = 0, hi = g_func_table_size;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g_func_table[mid].vram < vram) lo = mid + 1;
        else hi = mid;
    }
    if (lo < g_func_table_size && g_func_table[lo].vram == vram) {
        if (g_trace_counts[lo]++ == 0) {
            if (g_trace_distinct < TRACE_ORDER_MAX) {
                g_trace_order[g_trace_distinct] = vram;
            }
            g_trace_distinct++;
        }
    }
}

static void trace_init(void) {
    g_trace_counts = (uint64_t*)calloc(g_func_table_size, sizeof(uint64_t));
}

/* Grava toda funcao que executou pelo menos uma vez, com a contagem.
 *
 * A lista das 64 primeiras mostra por onde o boot passou; esta mostra o conjunto
 * inteiro, que e o que permite calcular a *fronteira*: o que o codigo alcancado
 * chama mas nunca foi alcancado. E ali que o jogo para de se ramificar. */
static void trace_dump_executed(const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# funcao  chamadas\n");
    size_t n = 0;
    for (size_t i = 0; i < g_func_table_size; i++) {
        if (g_trace_counts && g_trace_counts[i]) {
            fprintf(f, "%08X %llu\n", g_func_table[i].vram,
                    (unsigned long long)g_trace_counts[i]);
            n++;
        }
    }
    fclose(f);
    printf("executadas gravadas em  : %s (%zu funcoes)\n", path, n);
}

static void trace_report(void) {
    printf("funcoes executadas      : %zu de %zu (%.1f%%)\n",
           g_trace_distinct, g_func_table_size,
           100.0 * (double)g_trace_distinct / (double)g_func_table_size);
    printf("total de chamadas       : %llu\n", (unsigned long long)g_trace_total);
    printf("ultima funcao alcancada : 0x%08X\n", g_last_traced);

    /* Antes do ranking: o laco que escolhe as mais chamadas zera os contadores
       conforme os consome, entao a gravacao tem de vir primeiro. */
    {
        char path[256];
        snprintf(path, sizeof(path), "%sexecutadas.txt", g_out_prefix);
        trace_dump_executed(path);
    }

    size_t shown = g_trace_distinct < TRACE_ORDER_MAX ? g_trace_distinct : TRACE_ORDER_MAX;
    printf("primeiras %zu funcoes, em ordem de entrada:\n   ", shown);
    for (size_t i = 0; i < shown; i++) {
        printf("%08X%s", g_trace_order[i], (i + 1 < shown) ? " " : "\n");
        if ((i + 1) % 8 == 0 && i + 1 < shown) printf("\n   ");
    }

    printf("funcoes mais chamadas:\n");
    for (int rank = 0; rank < 10; rank++) {
        size_t best = (size_t)-1;
        uint64_t best_n = 0;
        for (size_t i = 0; i < g_func_table_size; i++) {
            if (g_trace_counts[i] > best_n) { best_n = g_trace_counts[i]; best = i; }
        }
        if (best == (size_t)-1) break;
        printf("   func_%08X  %llu chamada(s)\n",
               g_func_table[best].vram, (unsigned long long)best_n);
        g_trace_counts[best] = 0;
    }
}
#else
uint32_t trace_last_func(void) { return 0; }
void trace_trail(const char* label) { (void)label; }
static void trace_init(void) {}
static void trace_report(void) {
    printf("funcoes executadas      : (compile com tracing para medir)\n");
}
#endif

/* ------------------------------------------------------------------ */
/* Relatorio, watchdog e handler de falta de pagina                    */
/* ------------------------------------------------------------------ */

static void report(const char* why);

/* Globais sob observacao.
 *
 * A analise de fronteira mostrou funcoes chamadas centenas de vezes que nunca
 * tomam desvio nenhum. `func_800BA9D4` e o caso mais claro: ela le uma variavel
 * de estado e so age se o valor for 2 ou 4. Saber *qual* valor ela ve, e se ele
 * muda ao longo da execucao, separa "maquina de estado parada" de "maquina de
 * estado girando num caso que ainda nao interessa".
 *
 * Editar esta tabela e o jeito barato de acompanhar qualquer global suspeita. */
static const struct { uint32_t addr; const char* nome; int halfword; } k_watch[] = {
    { 0x800E8CF0u, "estado lido por func_800BA9D4", 0 },
    /* A transicao inicial 1 -> 2 e comandada por esta pequena fila em
       func_800BA9D4. O Project64 a alcancou; o recompilado ficou em 1. */
    { 0x800E8CF4u, "flag de movimento da fila inicial", 0 },
    { 0x800E8CF8u, "flag de entrada da fila inicial", 0 },
    { 0x800E8CFCu, "passo/velocidade da fila inicial", 0 },
    { 0x801A8D88u, "flags do roteiro", 1 },
    /* Entrada de controle. Enderecos vindos da decompilacao de referencia
     * (tools/wonder-source/libultra_symbols.txt e symbol_addrs.txt), que e do
     * MESMO jogo e cujos enderecos conferem com os nossos.
     *
     * Ja esta medido que o PIF entrega 0x1000 em toda leitura. O que faltava
     * era ver o outro lado: se o valor chega as variaveis que a logica do jogo
     * consulta. O primeiro campo do OSContPad e `u16 button`, entao a leitura
     * de meia-palavra pega os botoes direto: START aparece como 0x1000. */
    { 0x80182558u, "gContPad.button", 1 },
    { 0x80180DA8u, "gControllerRaw", 1 },
    { 0x801AFB40u, "__osContPifRam (fita joybus)", 0 },

    { 0x801A7234u, "estado principal de func_80002F20", 1 },
    { 0x801A723Cu, "subestado principal de func_80002F20", 1 },
    { 0x801A7254u, "estado pendente de func_80002F20", 1 },
    { 0x801A725Cu, "subestado pendente de func_80002F20", 1 },
    { 0x801AE818u, "objeto auxiliar da fila inicial", 0 },
    { 0x801AE824u, "objeto principal da fila inicial", 0 },
    { 0x801AE828u, "alvo de copia da fila inicial", 0 },
    { 0x801AE82Cu, "origem de copia da fila inicial", 0 },
    { 0x801AE830u, "limite atual da fila inicial", 0 },
    { 0x801AE834u, "indice anterior da fila inicial", 0 },
    { 0x801AE838u, "indice ativo da fila inicial", 0 },
    { 0x801AE83Cu, "ponteiro de tarefa da fila inicial", 0 },
    { 0x801561CCu, "contador de animacao passado ao compositor", 0 },
    { 0x80156B98u, "acumulador horizontal do compositor", 0 },
    { 0x80156BA0u, "acumulador vertical do compositor", 0 },
    { 0x80156C14u, "deslocamento calculado do compositor", 0 },
    { 0x8015F880u, "ponteiro da tabela de objetos CI8", 0 },
    { 0x8015B328u, "indice CI8 selecionado (modo par)", 0 },
    { 0x8015B320u, "indice CI8 selecionado (modo impar)", 0 },
    { 0x8015B330u, "base CI8 para animacao", 0 },
    /* VI_ORIGIN e o endereco do framebuffer que o gerenciador de video esta
       apresentando. Se ficar em zero, o jogo nunca entregou um buffer - o que
       decide, sozinho, se faz sentido procurar renderizador ou procurar antes o
       motivo de o lado grafico nao ter comecado. */
    { 0xA4400004u, "VI_ORIGIN (framebuffer apresentado)", 0 },
    { 0xA4400008u, "VI_WIDTH", 0 },
    { 0xA4400000u, "VI_STATUS", 0 },
    { 0xA4400028u, "VI_V_START (janela vertical)", 0 },
    { 0xA4400034u, "VI_Y_SCALE", 0 },
};
#define WATCH_COUNT (sizeof(k_watch) / sizeof(k_watch[0]))

static void watch_dump(const char* quando) {
    for (size_t i = 0; i < WATCH_COUNT; i++) {
        uint32_t raw = *(uint32_t*)(g_rdram + (k_watch[i].addr - VBASE));
        uint32_t v = k_watch[i].halfword ? (raw >> 16) & 0xFFFFu : raw;
        printf("  [watch] %s: 0x%08X = %u (%s)\n",
               quando, k_watch[i].addr, v, k_watch[i].nome);
    }
    fflush(stdout);
}

static void texture_watch_report(void) {
    LONG n = g_texture_watch_count;
    printf("escritas CPU nao-zero na textura: %ld", n);
    if (n == 0) {
        printf(" (nenhuma em 0x%08X; %ld escrita(s) zero ignorada(s))\n",
               TEXTURE_WATCH_ADDR, g_texture_watch_zeroes);
        return;
    }
    printf(" (primeiras %d; %ld escrita(s) zero ignorada(s)):\n",
           n < WATCH_WRITE_MAX ? (int)n : WATCH_WRITE_MAX, g_texture_watch_zeroes);
    for (LONG i = 0; i < n && i < WATCH_WRITE_MAX; i++) {
        printf("   0x%08X = 0x%02X por func_%08X\n", g_texture_watch_addr[i],
               g_texture_watch_value[i], g_texture_watch_func[i]);
    }
    printf("   (%ld escrita(s) na pagina observada; limite de seguranca %d)\n",
           g_texture_watch_faults, WATCH_FAULT_MAX);
}

/* Despeja o framebuffer que o VI esta apresentando, em PPM.
 *
 * O gerenciador de video ja programa VI_ORIGIN com enderecos reais e alterna
 * entre dois deles - o jogo faz double buffering. Isso muda a pergunta "ha
 * imagem?": ha um quadro sendo apresentado a cada retrace; a duvida e se alguem
 * escreveu alguma coisa nele. Um arquivo resolve isso sem precisar de janela.
 *
 * VI_STATUS bits 0-1 dizem o formato: 2 = 16 bits (RGBA5551), 3 = 32 bits. */
static void dump_buffer_em(const char* prefixo, const char* rotulo, uint32_t origin);

static void dump_framebuffer(const char* prefixo) {
    uint32_t origin = *(uint32_t*)(g_rdram + (0xA4400004u - VBASE)) & 0x1FFFFFFFu;
    uint32_t largura = *(uint32_t*)(g_rdram + (0xA4400008u - VBASE)) & 0xFFF;
    uint32_t status = *(uint32_t*)(g_rdram + (0xA4400000u - VBASE));
    uint32_t vstart = *(uint32_t*)(g_rdram + (0xA4400028u - VBASE));
    uint32_t yscale = *(uint32_t*)(g_rdram + (0xA4400034u - VBASE)) & 0xFFF;
    uint32_t tipo = status & 3;

    if (!origin || !largura || (tipo != 2 && tipo != 3)) {
        printf("framebuffer             : nada a despejar"
               " (origin=0x%X largura=%u tipo=%u)\n", origin, largura, tipo);
        return;
    }
    /* A janela vertical do VI esta em half-lines; yscale usa base 2048.
     * Nao assumir 240: neste boot 0x002501FF com escala 1024 produz 237
     * linhas. As linhas extras pertencem ao buffer, mas nao a imagem exibida. */
    uint32_t topo = (vstart >> 16) & 0x3FF;
    uint32_t base = vstart & 0x3FF;
    uint32_t altura = (base > topo && yscale) ? ((base - topo) * yscale) / 2048 : 240;
    if (!altura || altura > 240) altura = 240;
    uint32_t bpp = (tipo == 3) ? 4 : 2;
    if (origin + largura * altura * bpp > 0x800000u) {
        printf("framebuffer             : fora da RDRAM (origin=0x%X)\n", origin);
        return;
    }

    char nome[256];
    snprintf(nome, sizeof(nome), "%sframe.ppm", prefixo);
    FILE* f = fopen(nome, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", largura, altura);

    uint64_t nao_zero = 0;
    for (uint32_t y = 0; y < altura; y++) {
        for (uint32_t x = 0; x < largura; x++) {
            uint32_t off = origin + (y * largura + x) * bpp;
            uint8_t r, g, b;
            if (bpp == 2) {
                /* Meia palavra: o `^2` desfaz a troca de bytes da RDRAM. */
                uint16_t p = *(uint16_t*)(g_rdram + (off ^ 2));
                r = (uint8_t)(((p >> 11) & 0x1F) << 3);
                g = (uint8_t)(((p >> 6) & 0x1F) << 3);
                b = (uint8_t)(((p >> 1) & 0x1F) << 3);
                if (p & 0xFFFE) nao_zero++;
            } else {
                uint32_t p = *(uint32_t*)(g_rdram + off);
                r = (uint8_t)(p >> 24); g = (uint8_t)(p >> 16); b = (uint8_t)(p >> 8);
                if (p & 0xFFFFFF00u) nao_zero++;
            }
            fputc(r, f); fputc(g, f); fputc(b, f);
        }
    }
    fclose(f);
    printf("framebuffer             : 0x%08X %ux%u %u bits, %llu de %u pixels"
           " nao pretos -> %s\n", origin | 0x80000000u, largura, altura, bpp * 8,
           (unsigned long long)nao_zero, largura * altura, nome);
}

/* Despeja um buffer arbitrario de 320x240 em 16 bits. Usado para os alvos de
   SETCIMG, que o VI nao esta apresentando mas o jogo desenhou. */
static void dump_buffer_em(const char* prefixo, const char* rotulo, uint32_t origin) {
    const uint32_t larg = 320, alt = 240;
    if (!origin || origin + larg * alt * 2 > 0x800000u) {
        printf("%-24s: fora da RDRAM (0x%08X)\n", rotulo, origin);
        return;
    }
    char nome[256];
    snprintf(nome, sizeof(nome), "%s%s.ppm", prefixo, rotulo);
    FILE* f = fopen(nome, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", larg, alt);
    uint64_t nao_preto = 0, distintas = 0;
    uint16_t vistas[16]; int nv = 0;
    for (uint32_t y = 0; y < alt; y++) {
        for (uint32_t x = 0; x < larg; x++) {
            uint16_t p = *(uint16_t*)(g_rdram + ((origin + (y * larg + x) * 2) ^ 2));
            fputc(((p >> 11) & 0x1F) << 3, f);
            fputc(((p >> 6)  & 0x1F) << 3, f);
            fputc(((p >> 1)  & 0x1F) << 3, f);
            if (p & 0xFFFE) nao_preto++;
            int achou = 0;
            for (int k = 0; k < nv; k++) if (vistas[k] == p) { achou = 1; break; }
            if (!achou && nv < 16) { vistas[nv++] = p; distintas++; }
        }
    }
    fclose(f);
    printf("%-24s: 0x%08X  %llu de %u pixels nao pretos, %llu cor(es) distintas"
           " -> %s\n", rotulo, origin | 0x80000000u, (unsigned long long)nao_preto,
           larg * alt, (unsigned long long)distintas, nome);
}

/* Sem HLE nao ha interrupcoes: qualquer espera por hardware vira laco infinito.
   O watchdog transforma isso em um resultado observavel em vez de um travamento. */
static DWORD WINAPI watchdog(LPVOID param) {
    (void)param;
    /* Amostra periodica: se o boot travar, o que interessa e *onde*. Sem isto o
       relatorio final so diz que o tempo acabou. */
    for (unsigned i = 1; i <= g_frame_samples; i++) {
        Sleep((DWORD)(g_timeout_s * 1000.0 / g_frame_samples));
        if (!g_diagnostics) continue;
        /* `polls` cresce so quando codigo recompilado passa por um rotulo. Se
           ficar parado, o processo nao esta girando no jogo - esta parado em
           outro lugar, e o diagnostico e completamente diferente. */
        printf("  [wdog] %.2f s: ultima funcao 0x%08X, polls=%llu, thread=0x%08X\n",
               g_timeout_s * i / g_frame_samples, trace_last_func(),
               (unsigned long long)hle_polls(), sched_current());
        trace_trail("laco corrente");
        watch_dump("amostra");
        /* Um quadro por amostra: se o conteudo mudar ao longo da corrida, o jogo
           esta desenhando coisas diferentes; se nao mudar, esta parado numa
           tela so. A quantidade e configuravel para animacoes curtas. */
        char pref[256];
        snprintf(pref, sizeof(pref), "%st%d_", g_out_prefix, i);
        dump_framebuffer(pref);
        fflush(stdout);
    }
    if (g_diagnostics) {
        report("tempo limite atingido (o boot ficou preso esperando hardware)");
        fflush(stdout);
    }
    TerminateProcess(GetCurrentProcess(), 6);
    return 0;
}

static ULONG_PTR g_fault_addr = 0;
static DWORD g_fault_code = 0;

static LONG fault_filter(EXCEPTION_POINTERS* ep) {
    g_fault_code = ep->ExceptionRecord->ExceptionCode;
    g_fault_addr = (ep->ExceptionRecord->NumberParameters >= 2)
        ? ep->ExceptionRecord->ExceptionInformation[1] : 0;
    return EXCEPTION_EXECUTE_HANDLER;
}

static uint8_t* texture_watch_page_for(ULONG_PTR addr) {
    ULONG_PTR base = (ULONG_PTR)g_rdram;
    ULONG_PTR off = (ULONG_PTR)((TEXTURE_WATCH_ADDR - VBASE) & ~(WATCH_PAGE_SIZE - 1));
    if (addr >= base + off && addr < base + off + WATCH_PAGE_SIZE) {
        return (uint8_t*)(base + off);
    }
    if (addr >= base + KSEG1_OFFSET + off &&
        addr < base + KSEG1_OFFSET + off + WATCH_PAGE_SIZE) {
        return (uint8_t*)(base + KSEG1_OFFSET + off);
    }
    return NULL;
}

/* A protecao e por pagina, mas so a cauda a partir de TEXTURE_WATCH_ADDR e
 * fonte do primeiro LOADBLOCK. O inicio da pagina tambem contem contexto de
 * thread, que nao deve esgotar a amostra. */
static int texture_watch_is_source(ULONG_PTR addr) {
    ULONG_PTR base = (ULONG_PTR)g_rdram;
    ULONG_PTR off = (ULONG_PTR)(TEXTURE_WATCH_ADDR - VBASE);
    ULONG_PTR page_end = (off & ~(WATCH_PAGE_SIZE - 1)) + WATCH_PAGE_SIZE;
    return (addr >= base + off && addr < base + page_end) ||
           (addr >= base + KSEG1_OFFSET + off &&
            addr < base + KSEG1_OFFSET + page_end);
}

static void texture_watch_disarm(void) {
    DWORD old;
    ULONG_PTR off = (ULONG_PTR)((TEXTURE_WATCH_ADDR - VBASE) & ~(WATCH_PAGE_SIZE - 1));
    VirtualProtect(g_rdram + off, WATCH_PAGE_SIZE, PAGE_READWRITE, &old);
    VirtualProtect(g_rdram + KSEG1_OFFSET + off, WATCH_PAGE_SIZE, PAGE_READWRITE, &old);
    g_texture_watch_armed = 0;
    g_texture_watch_rearm = NULL;
}

static int texture_watch_arm(void) {
    DWORD old;
    ULONG_PTR off = (ULONG_PTR)((TEXTURE_WATCH_ADDR - VBASE) & ~(WATCH_PAGE_SIZE - 1));
    if (!VirtualProtect(g_rdram + off, WATCH_PAGE_SIZE, PAGE_READONLY, &old) ||
        !VirtualProtect(g_rdram + KSEG1_OFFSET + off, WATCH_PAGE_SIZE, PAGE_READONLY, &old)) {
        texture_watch_disarm();
        printf("[watch] nao consegui proteger a pagina da textura (%lu)\n", GetLastError());
        return 0;
    }
    g_texture_watch_armed = 1;
    printf("[watch] rastreando escritas da CPU em 0x%08X (pagina 0x%X)\n",
           TEXTURE_WATCH_ADDR, (TEXTURE_WATCH_ADDR - VBASE) & ~(WATCH_PAGE_SIZE - 1));
    return 1;
}

static LONG CALLBACK commit_on_demand(EXCEPTION_POINTERS* info) {
    /* A instrucao que escreveu numa pagina somente-leitura deve executar uma
       vez com escrita liberada. O trap de single-step seguinte rearma a pagina
       antes da proxima instrucao, sem penalizar leituras do RSP/CPU. */
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP &&
        g_texture_watch_rearm != NULL) {
        if (g_texture_watch_pending && g_texture_watch_pending_addr != NULL) {
            /* Agora a instrucao protegida ja executou. Zeros sao inicializacao
               de estrutura; o que interessa e a primeira escrita que traz
               dados para o buffer que o RDP vai consumir. */
            uint8_t value = *g_texture_watch_pending_addr;
            if (value != 0) {
                LONG n = InterlockedIncrement(&g_texture_watch_count) - 1;
                if (n < WATCH_WRITE_MAX) {
                    g_texture_watch_addr[n] = g_texture_watch_pending_vaddr;
                    g_texture_watch_func[n] = g_texture_watch_pending_func;
                    g_texture_watch_value[n] = value;
                }
            } else {
                InterlockedIncrement(&g_texture_watch_zeroes);
            }
        }
        g_texture_watch_pending = 0;
        g_texture_watch_pending_addr = NULL;
        DWORD old;
        VirtualProtect(g_texture_watch_rearm, WATCH_PAGE_SIZE, PAGE_READONLY, &old);
        g_texture_watch_rearm = NULL;
        info->ContextRecord->EFlags &= ~0x100u;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    ULONG_PTR addr = info->ExceptionRecord->ExceptionInformation[1];

    if (g_texture_watch_armed && info->ExceptionRecord->NumberParameters >= 2 &&
        info->ExceptionRecord->ExceptionInformation[0] == 1) {
        uint8_t* page = texture_watch_page_for(addr);
        if (page != NULL) {
            LONG faults = InterlockedIncrement(&g_texture_watch_faults);
            if (texture_watch_is_source(addr)) {
                g_texture_watch_pending_vaddr = (uint32_t)(VBASE + (addr - (ULONG_PTR)g_rdram));
                g_texture_watch_pending_func = trace_last_func();
                g_texture_watch_pending_addr = (uint8_t*)addr;
                g_texture_watch_pending = 1;
            }
            if (faults >= WATCH_FAULT_MAX) {
                texture_watch_disarm();
            } else {
                DWORD old;
                VirtualProtect(page, WATCH_PAGE_SIZE, PAGE_READWRITE, &old);
                g_texture_watch_rearm = page;
                info->ContextRecord->EFlags |= 0x100u;
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }

    ULONG_PTR base = (ULONG_PTR)g_rdram;
    if (addr < base || addr >= base + TOTAL_SIZE) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    uint64_t off = (uint64_t)(addr - base);
    uint32_t vaddr = (uint32_t)(VBASE + off);
    int idx;
    const char* name = mmio_name_for(vaddr, &idx);
    if (idx >= 0) {
        if (g_mmio_hits[idx]++ == 0) {
            g_mmio_first[idx] = vaddr;
            printf("  [mmio] primeiro acesso a %-20s (0x%08X) vindo de 0x%08X\n",
                   name, vaddr, trace_last_func());
            fflush(stdout);
        }
    }

    void* page = (void*)(addr & ~(ULONG_PTR)0xFFFF);
    if (VirtualAlloc(page, 0x10000, MEM_COMMIT, PAGE_READWRITE) == NULL) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

/* ------------------------------------------------------------------ */
/* Espaco de enderecos                                                 */
/* ------------------------------------------------------------------ */

static int reserve_gap(uint8_t* at, uint64_t size) {
    if (size == 0) return 1;
    return VirtualAlloc(at, (SIZE_T)size, MEM_RESERVE, PAGE_NOACCESS) != NULL;
}

static int setup_memory(void) {
    uint8_t* base = (uint8_t*)VirtualAlloc(NULL, (SIZE_T)TOTAL_SIZE, MEM_RESERVE, PAGE_NOACCESS);
    if (!base) {
        fprintf(stderr, "nao foi possivel reservar %llu MB de espaco\n", TOTAL_SIZE >> 20);
        return 0;
    }
    VirtualFree(base, 0, MEM_RELEASE);

    g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                               (DWORD)(RDRAM_SIZE >> 32), (DWORD)RDRAM_SIZE, NULL);
    if (!g_map) {
        fprintf(stderr, "CreateFileMapping falhou (%lu)\n", GetLastError());
        return 0;
    }

    /* KSEG0 e KSEG1 sao duas vistas do mesmo mapping, entao escritas por uma
       janela sao visiveis pela outra - como no console real. */
    if (!MapViewOfFileEx(g_map, FILE_MAP_ALL_ACCESS, 0, 0, (SIZE_T)RDRAM_SIZE, base)) {
        fprintf(stderr, "mapear KSEG0 falhou (%lu)\n", GetLastError());
        return 0;
    }
    if (!MapViewOfFileEx(g_map, FILE_MAP_ALL_ACCESS, 0, 0, (SIZE_T)RDRAM_SIZE,
                         base + KSEG1_OFFSET)) {
        fprintf(stderr, "mapear o alias KSEG1 falhou (%lu)\n", GetLastError());
        return 0;
    }

    /* O resto fica reservado e e comprometido no primeiro toque. */
    if (!reserve_gap(base + RDRAM_SIZE, KSEG1_OFFSET - RDRAM_SIZE) ||
        !reserve_gap(base + KSEG1_OFFSET + RDRAM_SIZE,
                     TOTAL_SIZE - KSEG1_OFFSET - RDRAM_SIZE)) {
        fprintf(stderr, "reservar o restante do espaco falhou (%lu)\n", GetLastError());
        return 0;
    }

    g_rdram = base;
    AddVectoredExceptionHandler(1, commit_on_demand);
    return 1;
}

/* Valores de ligar do RCP.
 *
 * Uma pagina de MMIO recem-comprometida le zero, e zero nao e o estado de
 * repouso de todo registrador. O caso que custou caro: `__osSpSetPc`
 * (func_800CD020) le SP_STATUS, exige o bit 0 - "RSP parado" - e devolve -1 se
 * ele estiver limpo. Com SP_STATUS lendo zero, *nenhuma* tarefa de RSP podia ser
 * carregada, e a thread que esperava o quadro esperava para sempre. */
static void init_mmio_defaults(void) {
    static const struct { uint32_t addr; uint32_t value; const char* what; } k_defaults[] = {
        { 0xA4040010u, 0x00000001u, "SP_STATUS = parado"      },
        { 0xA4300004u, 0x02020102u, "MI_VERSION"             },
    };
    for (size_t i = 0; i < sizeof(k_defaults) / sizeof(k_defaults[0]); i++) {
        uint8_t* p = g_rdram + (k_defaults[i].addr - VBASE);
        /* Comprometer aqui evita que estes acessos aparecam no log como se
           fossem o primeiro toque do jogo naquele bloco. */
        VirtualAlloc((void*)((ULONG_PTR)p & ~(ULONG_PTR)0xFFFF), 0x10000,
                     MEM_COMMIT, PAGE_READWRITE);
        *(uint32_t*)p = k_defaults[i].value;
        printf("rcp  : %-24s (0x%08X = 0x%08X)\n",
               k_defaults[i].what, k_defaults[i].addr, k_defaults[i].value);
    }
}

/* ------------------------------------------------------------------ */
/* Carga da ROM                                                        */
/* ------------------------------------------------------------------ */

/* A RDRAM guarda cada palavra de 32 bits na ordem de bytes do host: recomp.h le
   palavras nativamente e chega aos bytes via `^3`, entao os dados da ROM sao
   trocados na entrada. */
static void copy_swapped(uint8_t* dst, const uint8_t* src, size_t bytes) {
    for (size_t i = 0; i + 3 < bytes; i += 4) {
        dst[i + 0] = src[i + 3];
        dst[i + 1] = src[i + 2];
        dst[i + 2] = src[i + 1];
        dst[i + 3] = src[i + 0];
    }
}

/* O IPL3 preenche um bloco de globais em 0x80000300 antes de saltar para o
   entrypoint, e a libultra os le pelo resto da execucao. Um runtime que pula
   isso deixa o jogo lendo zeros - o pior deles osMemSize, de onde o heap sai. */
#define OS_TV_PAL   0
#define OS_TV_NTSC  1
#define OS_TV_MPAL  2

static uint32_t g_mem_size = 0x00400000;   /* 4 MB; sem pak de expansao */

static void write_word(uint32_t vaddr, uint32_t val) {
    *(uint32_t*)(g_rdram + (vaddr - VBASE)) = val;
}

static void init_ipl3_globals(void) {
    /* O codigo de pais no cabecalho decide o padrao de video. */
    char country = (char)g_rom[0x3E];
    uint32_t tv;
    switch (country) {
        case 'E': case 'A': case 'J': tv = OS_TV_NTSC; break;
        case 'B':                     tv = OS_TV_MPAL; break;
        default:                      tv = OS_TV_PAL;  break;
    }

    write_word(0x80000300, tv);           /* osTvType       */
    write_word(0x80000304, 0);            /* osRomType: cartucho */
    write_word(0x80000308, 0xB0000000);   /* osRomBase      */
    write_word(0x8000030C, 0);            /* osResetType: boot frio */
    write_word(0x80000310, 6102);         /* osCicId        */
    write_word(0x80000314, 0);            /* osVersion      */
    write_word(0x80000318, g_mem_size);   /* osMemSize      */
    write_word(0x8000031C, 0);            /* osAppNMIBuffer */

    printf("ipl3 : osTvType=%s osMemSize=%u MB (pais '%c')\n",
           tv == OS_TV_NTSC ? "NTSC" : tv == OS_TV_MPAL ? "MPAL" : "PAL",
           g_mem_size >> 20, country);
}

static int load_rom(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "nao consegui abrir a ROM: %s\n", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    g_rom_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    g_rom = (uint8_t*)malloc(g_rom_size);
    if (!g_rom || fread(g_rom, 1, g_rom_size, f) != g_rom_size) {
        fprintf(stderr, "falha ao ler a ROM\n");
        fclose(f);
        return 0;
    }
    fclose(f);

    if (g_rom_size < 0x1000 || g_rom[0] != 0x80 || g_rom[1] != 0x37) {
        fprintf(stderr, "nao e uma imagem .z64 big-endian\n");
        return 0;
    }

    uint32_t crc1 = ((uint32_t)g_rom[0x10] << 24) | ((uint32_t)g_rom[0x11] << 16) |
                    ((uint32_t)g_rom[0x12] << 8)  | (uint32_t)g_rom[0x13];
    printf("ROM  : %s\n", path);
    printf("       %.2f MB, nome '%.20s', CRC1 %08X\n",
           g_rom_size / 1048576.0, g_rom + 0x20, crc1);
    if (crc1 != 0x4F1E88F7u) {
        printf("       AVISO: CRC1 diferente do registrado para a build Ryu v1.0\n");
    }

    /* Expoe o cartucho em 0xB0000000. */
    uint8_t* cart = g_rdram + CART_OFFSET;
    VirtualAlloc(cart, g_rom_size, MEM_COMMIT, PAGE_READWRITE);
    copy_swapped(cart, g_rom, g_rom_size);
    /* A legenda opcional altera somente esta imagem em memoria; o arquivo ROM
       original continua intacto. Assim tanto leituras diretas quanto PI DMA
       recebem a mesma cadeia PT-BR. */
    legendas_aplicar_cartucho(cart, g_rom_size);

    /* O IPL3 copia o segmento de boot da ROM 0x1000 para o entrypoint. */
    uint32_t entry = ((uint32_t)g_rom[8] << 24) | ((uint32_t)g_rom[9] << 16) |
                     ((uint32_t)g_rom[10] << 8) | (uint32_t)g_rom[11];
    size_t boot_bytes = 0x100000;
    if (0x1000 + boot_bytes > g_rom_size) boot_bytes = g_rom_size - 0x1000;
    copy_swapped(g_rdram + (entry - VBASE), g_rom + 0x1000, boot_bytes);
    printf("boot : copiei 0x%zX bytes de ROM 0x1000 para 0x%08X\n", boot_bytes, entry);
    init_ipl3_globals();
    return 1;
}

/* ------------------------------------------------------------------ */
/* Callbacks do recomp.h                                               */
/* ------------------------------------------------------------------ */

static int32_t g_section_addresses[8] = { 0 };
int32_t* section_addresses = g_section_addresses;

void cop0_status_write(recomp_context* ctx, gpr value) { ctx->status_reg = (uint32_t)value; }
gpr cop0_status_read(recomp_context* ctx) { return (gpr)ctx->status_reg; }

void switch_error(const char* func, uint32_t vram, uint32_t jtbl) {
    printf("\n[parou] jump table caiu fora em %s em 0x%08X (tabela 0x%08X)\n",
           func, vram, jtbl);
    report("jump table sem destino valido");
    fflush(stdout);
    exit(2);
}

void do_break(uint32_t vram) {
    printf("\n[parou] instrucao break em 0x%08X\n", vram);
    report("break");
    fflush(stdout);
    exit(3);
}

void recomp_syscall_handler(uint8_t* rdram, recomp_context* ctx, int32_t instruction_vram) {
    (void)rdram; (void)ctx;
    printf("\n[parou] syscall em 0x%08X\n", (uint32_t)instruction_vram);
    report("syscall (provavelmente osException / troca de contexto)");
    fflush(stdout);
    exit(4);
}

void pause_self(uint8_t* rdram) { sched_pause_current(rdram); }

recomp_func_t* find_function(uint32_t target) {
    size_t lo = 0, hi = g_func_table_size;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g_func_table[mid].vram < target) lo = mid + 1;
        else hi = mid;
    }
    if (lo < g_func_table_size && g_func_table[lo].vram == target) {
        return g_func_table[lo].func;
    }
    return NULL;
}

recomp_func_t* get_function(int32_t vram) {
    uint32_t target = (uint32_t)vram;
    g_lookup_calls++;
    g_last_lookup = target;

    recomp_func_t* f = find_function(target);
    if (f) return f;

    printf("\n[parou] chamada indireta para 0x%08X, que nao e uma funcao conhecida\n", target);
    printf("        vinda de 0x%08X; %llu chamadas indiretas resolvidas antes desta\n",
           trace_last_func(), (unsigned long long)g_lookup_calls - 1);
    if (target >= 0x80000400u && target < 0x800D6B70u) {
        printf("        o alvo esta dentro do segmento boot: limite de funcao errado\n");
        printf("        nos simbolos, nao um overlay ausente\n");
    } else {
        printf("        o alvo esta fora do segmento boot: provavel overlay ainda\n");
        printf("        nao mapeado, ou ponteiro lido de memoria nao inicializada\n");
    }
    report("chamada indireta sem destino conhecido");
    fflush(stdout);
    exit(5);
}

/* ------------------------------------------------------------------ */
/* Relatorio                                                           */
/* ------------------------------------------------------------------ */

static void report(const char* why) {
    if (InterlockedExchange(&g_reported, 1) != 0) return;

    printf("\n--- estado da maquina ---\n");
    printf("motivo da parada        : %s\n", why);
    printf("despachos de thread     : %llu (%llu com a fila vazia)\n",
           (unsigned long long)sched_dispatch_calls(),
           (unsigned long long)sched_empty_dispatch());
    printf("trocas de contexto      : %llu; thread corrente 0x%08X\n",
           (unsigned long long)sched_switches(), sched_current());
    printf("cessoes reenfileiradas  : %llu (thread cedeu ainda RODANDO)\n",
           (unsigned long long)sched_yield_requeued());
    printf("leituras de osGetCount  : %llu\n",
           (unsigned long long)sched_count_calls());
    printf("interrupcoes entregues  : %llu retrace(s) em %llu ponto(s) de laco\n",
           (unsigned long long)hle_retraces(), (unsigned long long)hle_polls());
    printf("DMA do cartucho         : %llu transferencia(s), %llu bytes, %llu recusada(s)\n",
           (unsigned long long)hle_pi_transfers(),
           (unsigned long long)hle_pi_bytes(),
           (unsigned long long)hle_pi_rejected());
    printf("RSP                     : %llu tarefa(s), %llu transferencia(s) de SP\n",
           (unsigned long long)rsp_tasks(), (unsigned long long)rsp_sp_dma());
    printf("tarefas por tipo        : graficas=%llu audio=%llu outras=%llu\n",
           (unsigned long long)rsp_tasks_tipo(1), (unsigned long long)rsp_tasks_tipo(2),
           (unsigned long long)(rsp_tasks_tipo(0) + rsp_tasks_tipo(3)));
    printf("comandos de audio       : %llu executado(s), %llu bytes gravados na RDRAM\n",
           (unsigned long long)rsp_acmd_run(), (unsigned long long)rsp_bytes_saved());
    printf("picos ACMD              : carregado=%u mixado=%u salvo=%u\n",
           rsp_acmd_load_peak(), rsp_acmd_mix_peak(), rsp_acmd_save_peak());
    printf("sintese ACMD            : ADPCM=%llu RESAMPLE=%llu ENVMIX=%llu SETVOL=%llu\n",
           (unsigned long long)rsp_acmd_opcode(1),
           (unsigned long long)rsp_acmd_opcode(5),
           (unsigned long long)rsp_acmd_opcode(3),
           (unsigned long long)rsp_acmd_opcode(9));
    printf("AI/WAV                  : %llu buffer(s), %llu bytes, pico PCM=%u\n",
           (unsigned long long)audio_buffers_queued(),
           (unsigned long long)audio_bytes_queued(), audio_peak_sample());
    printf("PIF                     : %llu escrita(s), %llu leitura(s), %llu comando(s), %llu leitura(s) de controle\n",
           (unsigned long long)pif_si_writes(), (unsigned long long)pif_si_reads(),
           (unsigned long long)pif_commands(), (unsigned long long)pif_controller_polls());
    rsp_gfx_report(g_out_prefix);
    rsp_tlut_report();
    watch_dump("final");
    texture_watch_report();
    dump_framebuffer(g_out_prefix);
    /* O VI mostra um buffer por vez, e o jogo desenha em varios. Despejar todos
       os alvos de SETCIMG e o que responde "algum deles tem conteudo?" sem
       depender de qual estava sendo apresentado no instante da parada. */
    for (int i = 0; i < rsp_num_alvos(); i++) {
        char rot[32];
        snprintf(rot, sizeof(rot), "alvo%d", i);
        dump_buffer_em(g_out_prefix, rot, rsp_alvo(i));
    }
    hle_dma_report();
    hle_heap_report();
    hle_raster_report();
    hle_texto_report(g_out_prefix);
    hle_mesg_report();
    hle_faixas_report(rsp_ultima_textura());
    sched_report(g_rdram);
    trace_report();
    printf("chamadas indiretas      : %llu (ultimo alvo 0x%08X)\n",
           (unsigned long long)g_lookup_calls, g_last_lookup);
    printf("blocos de hardware tocados:\n");
    int any = 0;
    for (size_t i = 0; i < MMIO_COUNT; i++) {
        if (g_mmio_hits[i]) {
            printf("   %-22s %d falta(s) de pagina, primeiro em 0x%08X\n",
                   k_mmio[i].name, g_mmio_hits[i], g_mmio_first[i]);
            any = 1;
        }
    }
    if (!any) printf("   (nenhum)\n");
    fflush(stdout);
}

/* Le o arquivo de reproducao gravado pelo F2.
 *
 * Formato de duas chaves, uma por linha, escolhido para ser legivel e colavel:
 *
 *     alvo=1234
 *     roteiro=0:0000;812:1000;830:0000
 *
 * `roteiro` tem exatamente a sintaxe de WPJ2_INPUT_POLLS, indexada por leitura
 * de controle e nao por tempo - e isso que torna a reproducao repetivel. */
static void carregar_replay(const char* caminho) {
    FILE* f = fopen(caminho, "r");
    if (!f) {
        printf("[replay] nao consegui abrir %s\n", caminho);
        return;
    }
    static char linha[8192];
    unsigned long long alvo = 0;
    while (fgets(linha, sizeof(linha), f)) {
        char* fim = linha + strlen(linha);
        while (fim > linha && (fim[-1] == '\n' || fim[-1] == '\r')) *--fim = '\0';
        if (linha[0] == '#' || !linha[0]) continue;
        if (!strncmp(linha, "alvo=", 5)) {
            alvo = strtoull(linha + 5, NULL, 10);
        } else if (!strncmp(linha, "roteiro=", 8)) {
            pif_set_poll_script(linha + 8);
        }
    }
    fclose(f);
    printf("[replay] %s carregado\n", caminho);
    /* O turbo so vale a pena um pouco antes do alvo: parar exatamente nele
       deixaria a cena aparecer no mesmo instante em que a velocidade normaliza,
       sem margem para observar a entrada. */
    if (alvo > 30ull) hle_definir_alvo_turbo(alvo - 30ull);
    else if (alvo) hle_definir_alvo_turbo(alvo);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */

int main(int argc, char** argv) {
    const char* rom = (argc > 1) ? argv[1]
        : "E:/projetos/n64-roms/Wonder Project J2 - Koruro no Mori no Jozet"
          " (Japan) [T-En by Ryu v1.0].z64";
    if (argc > 2) g_timeout_s = atof(argv[2]);

    /* Configuracao por ambiente, para que varias sondagens rodem ao mesmo tempo
       sem se atrapalhar. Cada instancia recebe o seu proprio prefixo de saida; o
       resto muda o que se quer comparar entre elas. */
    const char* e;
    if ((e = getenv("WPJ2_TIMEOUT"))  != NULL) g_timeout_s = atof(e);
    if ((e = getenv("WPJ2_FRAME_SAMPLES")) != NULL) {
        unsigned n = (unsigned)strtoul(e, NULL, 0);
        if (n >= 1 && n <= 32) g_frame_samples = n;
    }
    if ((e = getenv("WPJ2_MEMSIZE"))  != NULL) g_mem_size  = (uint32_t)strtoul(e, NULL, 0);
    if ((e = getenv("WPJ2_OUT"))      != NULL) { g_out_prefix = e; rsp_set_prefix(e); }
    if ((e = getenv("WPJ2_EVENTS"))   != NULL) hle_set_event_mask((uint32_t)strtoul(e, NULL, 0));
    if ((e = getenv("WPJ2_BUTTONS"))  != NULL) pif_set_buttons((uint16_t)strtoul(e, NULL, 0));
    if ((e = getenv("WPJ2_INPUT"))    != NULL) pif_set_script(e);
    if ((e = getenv("WPJ2_INPUT_POLLS")) != NULL) pif_set_poll_script(e);
    /* Reproducao gravada: substitui o savestate. Um arquivo com duas chaves,
       `alvo=` e `roteiro=`, produzido pelo F2. Ver o comentario extenso em
       runtime/pif.c sobre por que um savestate ao estilo Project64 nao e
       alcancavel com fibers, e por que a reproducao resolve melhor o problema
       real, que e voltar depressa a uma cena para analisar. */
    if ((e = getenv("WPJ2_REPLAY")) != NULL && *e) carregar_replay(e);
    if ((e = getenv("WPJ2_STICK"))    != NULL) pif_set_stick(e);
    if ((e = getenv("WPJ2_RETRACE"))  != NULL) hle_set_retrace(atof(e));
    if ((e = getenv("WPJ2_WINDOW"))   != NULL) g_video_preview = atoi(e) != 0;
    if ((e = getenv("WPJ2_DEBUG"))    != NULL) g_diagnostics = atoi(e) != 0;
    rsp_set_debug(g_diagnostics);
    /* O pipeline F3DEX aplica G_MTX/MOVEMEM aos vertices por padrao. A chave
       existe para diagnostico comparativo contra a antiga projecao ortografica. */
    if ((e = getenv("WPJ2_F3D_MATRIX")) != NULL) rsp_set_f3d_matrix_mode(atoi(e) != 0);
    if ((e = getenv("WPJ2_F3D_Z")) != NULL) rsp_set_f3d_z_mode(atoi(e) != 0);
    if ((e = getenv("WPJ2_F3D_CULL")) != NULL) rsp_set_f3d_cull_mode(atoi(e) != 0);
    if ((e = getenv("WPJ2_F3D_MATRIX_CONVENTIONAL")) != NULL)
        rsp_set_f3d_matrix_conventional(atoi(e) != 0);
    if ((e = getenv("WPJ2_TMEM_INTERLEAVE")) != NULL)
        rsp_set_tmem_interleave(atoi(e) != 0);
    if ((e = getenv("WPJ2_RDP_COMBINER")) != NULL)
        rsp_set_rdp_combine_mode(atoi(e) != 0);
    /* Desvio estritamente diagnostico: mede o ramo posterior ao bloqueio
       1 -> 2; nao faz parte da emulacao normal. */
    if ((e = getenv("WPJ2_FORCE_STATE2_AFTER")) != NULL)
        hle_force_state2_after(strtoull(e, NULL, 0));
    if ((e = getenv("WPJ2_FORCE_ACTIVE_AFTER")) != NULL)
        hle_force_active_after(strtoull(e, NULL, 0), 0);
    if ((e = getenv("WPJ2_FORCE_NEXT_SCENE")) != NULL && atoi(e) != 0)
        hle_force_next_scene();
    if ((e = getenv("WPJ2_HOLD_STATE_8_1")) != NULL && atoi(e) != 0)
        hle_hold_state_8_1();
    if ((e = getenv("WPJ2_HOLD_STATE_12_50")) != NULL && atoi(e) != 0)
        hle_hold_state_12_50();
    if ((e = getenv("WPJ2_PREEMPT_EVERY_POLL")) != NULL)
        hle_set_preempt_every_poll(atoi(e) != 0);

#ifdef WPJ2_RELEASE
    /* Release interativa: nenhuma telemetria no console nem arquivos de
     * diagnostico por padrao. WPJ2_DEBUG=1 preserva o comportamento de sonda. */
    if (!g_diagnostics) freopen("NUL", "w", stdout);
#endif

    printf("Wonder Project J2 - nucleo de CPU recompilado (sondagem)\n");
    printf("=======================================================\n");

    if (!setup_memory()) return 1;
    printf("mem  : espaco de 1 GB em %p, 8 MB de RDRAM em KSEG0 e KSEG1\n",
           (void*)g_rdram);
    init_mmio_defaults();
    if (!load_rom(rom)) return 1;
    if (g_video_preview) video_init();
    audio_init();
    /* O watchpoint usa uma excecao por escrita e muda a temporizacao; ele e
       diagnostico dirigido, nunca parte da matriz normal de RODAR.bat. */
    if (getenv("WPJ2_WATCH_TEXTURE") != NULL) texture_watch_arm();
    printf("funcs: %zu funcoes recompiladas na tabela de lookup\n", g_func_table_size);
    if (g_timeout_s > 0.0) printf("wdog : limite de %.0f s\n\n", g_timeout_s);
    else                   printf("wdog : desativado; feche a janela para encerrar\n\n");
    trace_init();
    hle_clock_init();
    sched_init();

    recomp_context ctx;
    ctx_init(&ctx);
    ctx.r29 = (gpr)(int32_t)0x80400000;  /* o entrypoint define o seu proprio sp */

    if (g_timeout_s > 0.0) CreateThread(NULL, 0, watchdog, NULL, 0, NULL);

    printf("--- entrando em recomp_entrypoint ---\n");
    fflush(stdout);

    __try {
        recomp_entrypoint(g_rdram, &ctx);
        report("o entrypoint retornou (sem interrupcoes, o boot nao tem para onde ir)");
    }
    __except (fault_filter(GetExceptionInformation())) {
        printf("\n[parou] falha 0x%08lX apos %llu chamadas indiretas"
               " (ultimo alvo 0x%08X)\n",
               g_fault_code, (unsigned long long)g_lookup_calls, g_last_lookup);
        ULONG_PTR base = (ULONG_PTR)g_rdram;
        if (g_fault_addr >= base && g_fault_addr < base + TOTAL_SIZE) {
            printf("        tocou o endereco emulado 0x%08X (dentro do mapa)\n",
                   (uint32_t)(VBASE + (g_fault_addr - base)));
        } else {
            int64_t rel = (int64_t)g_fault_addr - (int64_t)base;
            printf("        endereco de host %p esta fora do mapa de 1 GB\n",
                   (void*)g_fault_addr);
            printf("        isto e 0x80000000 %s 0x%llX no espaco emulado\n",
                   rel < 0 ? "-" : "+",
                   (unsigned long long)(rel < 0 ? -rel : rel));
        }
        report("falha de acesso");
    }
    video_shutdown();
    audio_shutdown();
    hle_clock_shutdown();
    return 0;
}
