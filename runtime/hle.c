/* Substituicoes nativas de dispositivo.
 *
 * Regra desta camada: so entra aqui funcao cuja interface eu li no
 * desassemblador, nunca uma que eu tenha suposto por analogia com outro jogo.
 * A assinatura abaixo veio da leitura de func_800CB090 (ROM 0xCBC90).
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"
#include "video.h"
#include "funcs.h"
#include "video.h"
#include "legendas.h"
#include "wpj2_os.h"

static uint64_t g_pi_transfers = 0;
static uint64_t g_pi_bytes = 0;
static uint64_t g_pi_rejected = 0;

/* O alocador e mantido recompilado; os wrappers abaixo so registram as
 * alocacoes que se tornam fonte dos LOADBLOCKs. */
void func_800BC6EC__replaced(uint8_t* rdram, recomp_context* ctx);
void func_800CCC60__replaced(uint8_t* rdram, recomp_context* ctx);
#define HEAP_LOG_MAX 64
typedef struct { uint32_t caller, bytes, result; } heap_log_t;
static heap_log_t g_heap_log[HEAP_LOG_MAX];
static uint32_t g_heap_log_n = 0, g_heap_calls = 0;
static uint32_t g_heap_outer_caller = 0;
static uint32_t g_destroy_thread_repairs = 0;
static uint32_t g_destroy_thread_logs = 0;

void func_800CCC60(uint8_t* rdram, recomp_context* ctx) {
    /* Este pequeno wrapper termina em osDestroyThread(NULL). A versao MIPS
       procura __osRunningThread; se o retorno de um fiber deixou apenas esse
       espelho zerado, a rotina seguinte (800CBFC0) dereferencia NULL. A thread
       ativa ainda e identificavel pelo fiber, logo restauramos somente esse
       espelho antes de deixar a limpeza original remover a thread da fila. */
    uint32_t atual = sched_current();
    uint32_t os_atual = (uint32_t)MEM_W(0, (gpr)(int32_t)ADDR_RUNNING_THREAD);
    if (g_destroy_thread_logs++ < 4) {
        uint32_t fila = os_atual ? (uint32_t)MEM_W(0x8, (gpr)(int32_t)os_atual) : 0;
        printf("[sched] osDestroyThread: fiber=0x%08X os=0x%08X fila=0x%08X\n",
               atual, os_atual, fila);
        fflush(stdout);
    }
    if (atual && os_atual == 0) {
        MEM_W(0, (gpr)(int32_t)ADDR_RUNNING_THREAD) = (int32_t)atual;
        if (g_destroy_thread_repairs++ < 4) {
            printf("[sched] espelho da thread ativa reposto: 0x%08X\n", atual);
            fflush(stdout);
        }
    }
    g_heap_outer_caller = (uint32_t)ctx->r31;
    func_800CCC60__replaced(rdram, ctx);
    /* No MIPS esta rotina e o ponto terminal de uma OSThread. Retornar daqui
       faz o mesmo fiber executar novamente apos um despacho futuro. */
    sched_terminate_current(rdram);
}

/* 800CB840 e a implementacao interna que seleciona a thread a remover. O
 * wrapper deixa o corpo MIPS intacto, mas torna visivel se a identidade do
 * fiber e a identidade de __osRunningThread se separaram antes de CBFC0. */
void func_800CB840__replaced(uint8_t* rdram, recomp_context* ctx);
static uint32_t g_remove_thread_logs = 0;
void func_800CB840(uint8_t* rdram, recomp_context* ctx) {
    if (g_remove_thread_logs++ < 8) {
        uint32_t os_atual = (uint32_t)MEM_W(0, (gpr)(int32_t)ADDR_RUNNING_THREAD);
        uint32_t alvo = (uint32_t)ctx->r4;
        uint32_t fila = alvo ? (uint32_t)MEM_W(0x8, (gpr)(int32_t)alvo) : 0;
        uint32_t fila_os = os_atual ? (uint32_t)MEM_W(0x8, (gpr)(int32_t)os_atual) : 0;
        printf("[sched] remove: arg=0x%08X fiber=0x%08X os=0x%08X fila(arg/os)=%08X/%08X\n",
               alvo, sched_current(), os_atual, fila, fila_os);
        fflush(stdout);
    }
    func_800CB840__replaced(rdram, ctx);
}

void func_800BC6EC(uint8_t* rdram, recomp_context* ctx) {
    uint32_t caller = (uint32_t)ctx->r31;
    uint32_t bytes = (uint32_t)ctx->r4;
    func_800BC6EC__replaced(rdram, ctx);
    uint32_t result = (uint32_t)ctx->r2;
    g_heap_calls++;
    if (g_heap_log_n < HEAP_LOG_MAX && result >= 0x802C0000u && result < 0x80300000u) {
        g_heap_log[g_heap_log_n++] = (heap_log_t){
            g_heap_outer_caller ? g_heap_outer_caller : caller, bytes, result };
    }
}

void hle_heap_report(void) {
    printf("alocador do heap         : %u chamada(s); blocos graficos candidatos:\n", g_heap_calls);
    for (uint32_t i = 0; i < g_heap_log_n; i++) {
        printf("   0x%08X bytes=%u <- func_%08X\n", g_heap_log[i].result,
               g_heap_log[i].bytes, g_heap_log[i].caller);
    }
}

/* func_80090784 e o rasterizador de indices CI que a sonda de pagina viu
 * escrevendo 0x10 no buffer entregue ao RDP. O corpo recompilado continua
 * sendo executado: este wrapper apenas conserva os argumentos das chamadas
 * cujo destino cai no atlas de 0x802CEF20. Assim a proxima rodada revela se o
 * preto vem de uma escolha de glifo/indice ou de um destino errado. */
void func_80090784__replaced(uint8_t* rdram, recomp_context* ctx);
#define RASTER_LOG_MAX 64
typedef struct {
    uint32_t parent, a0, a1, base, offset, mode, value, addr;
} raster_log_t;
static raster_log_t g_raster_log[RASTER_LOG_MAX];
static uint32_t g_raster_calls = 0;
static uint32_t g_raster_logged = 0;
static uint64_t g_raster_atlas_total = 0;
static uint64_t g_raster_atlas_indice[256];
static uint64_t g_raster_atlas_visivel[256];

static uint32_t kseg0(uint32_t address) {
    return 0x80000000u | (address & 0x1fffffffu);
}

static int texture_address(uint32_t address) {
    return address >= 0x802CEF20u && address < 0x802DDF20u;
}

/* Leituras de RDRAM na mesma ordem usada pelo C recompilado. */
static uint16_t rdram16(uint8_t* rdram, uint32_t phys) {
    return *(uint16_t*)(rdram + (phys ^ 2u));
}
static uint32_t rdram32(uint8_t* rdram, uint32_t phys) {
    return *(uint32_t*)(rdram + phys);
}

void func_80090784(uint8_t* rdram, recomp_context* ctx) {
    uint32_t a0 = (uint32_t)ctx->r4;
    uint32_t a1 = (uint32_t)ctx->r5;
    uint32_t base = kseg0((uint32_t)ctx->r6);
    uint32_t offset = (uint32_t)ctx->r7;
    uint32_t mode = rdram16(rdram, 0x0015B338u);
    uint32_t selector = rdram32(rdram, (mode & 1u) ? 0x0015B320u : 0x0015B328u);
    uint32_t table_index = a0;
    if (a0 == selector && (mode & 2u))
        table_index = rdram32(rdram, 0x0015B330u) + a1;
    uint32_t value = rdram16(rdram, 0x0015BB48u + table_index * 2u);

    /* O corpo original so escreve com bit 0 do valor ativo. Quando o modo
       0x400 esta ativo e byte; caso contrario e meio-pixel de 16 bits. A
       versao anterior listava os dois destinos possiveis e podia atribuir uma
       chamada ao atlas embora ela tenha escrito no outro. */
    uint32_t addr = kseg0(base + offset * ((mode & 0x400u) ? 1u : 2u));
    if (texture_address(addr)) {
        g_raster_atlas_total++;
        if (a0 < 256) g_raster_atlas_indice[a0]++;
        if ((value & 1u) && a0 < 256) g_raster_atlas_visivel[a0]++;
    }
    if ((value & 1u) && texture_address(addr)) {
        g_raster_calls++;
        if (g_raster_logged < RASTER_LOG_MAX) {
            g_raster_log[g_raster_logged++] = (raster_log_t){
                trace_last_func(), a0, a1, base, offset, mode, value, addr };
        }
    }
    func_80090784__replaced(rdram, ctx);
}

void hle_raster_report(void) {
    printf("rasterizador de indices : %u chamada(s) para a textura; primeiras %u:\n",
           g_raster_calls, g_raster_logged);
    for (uint32_t i = 0; i < g_raster_logged; i++) {
        const raster_log_t* r = &g_raster_log[i];
        printf("   pai=func_%08X a0=0x%08X a1=0x%08X base=0x%08X off=0x%08X mode=0x%04X valor=0x%04X dst=0x%08X\n",
               r->parent, r->a0, r->a1, r->base, r->offset,
               r->mode, r->value, r->addr);
    }
    printf("indices de objeto CI8    : %llu amostras no atlas; mais usados:\n",
           (unsigned long long)g_raster_atlas_total);
    for (int rank = 0; rank < 8; rank++) {
        int melhor = -1;
        for (int i = 0; i < 256; i++)
            if (g_raster_atlas_indice[i] && (melhor < 0
                || g_raster_atlas_indice[i] > g_raster_atlas_indice[melhor]))
                melhor = i;
        if (melhor < 0) break;
        printf("   0x%02X  %llu lido(s), %llu escrito(s) pelo mapa CI8\n", melhor,
               (unsigned long long)g_raster_atlas_indice[melhor],
               (unsigned long long)g_raster_atlas_visivel[melhor]);
        g_raster_atlas_indice[melhor] = 0;
    }
}

/* `func_80094230` e o compositor que preenche o atlas CI8 consumido pelos
 * TEXRECT. A sonda de pagina ja provou que ele escolhe o indice 0x10 (preto),
 * mas nao qual chamada ou relogio do jogo seleciona esse caminho. O wrapper
 * conserva o corpo recompilado e guarda somente as primeiras chamadas, para
 * tornar a proxima rodada uma observacao de estado e nao mais uma tentativa de
 * entrada. */
void func_80094230__replaced(uint8_t* rdram, recomp_context* ctx);
#define TEXTO_LOG_MAX 24
typedef struct {
    uint32_t caller, a0, a1, a2, a3;
    uint32_t tabela, fonte;
    uint8_t primeiro_byte, modo;
} texto_log_t;
static texto_log_t g_texto_log[TEXTO_LOG_MAX];
static uint32_t g_texto_chamadas = 0, g_texto_log_n = 0;

void func_80094230(uint8_t* rdram, recomp_context* ctx) {
    uint32_t a0 = (uint32_t)ctx->r4;
    /* O primeiro argumento e um indice de objeto, nao um endereco. O corpo
       multiplica-o por 12 e busca os pixels na tabela global 0x8015F880. */
    uint32_t tabela = rdram32(rdram, 0x0015F880u) & 0x1FFFFFFFu;
    uint32_t fonte = tabela + a0 * 12u;
    uint16_t modo = rdram16(rdram, 0x0015B338u);
    g_texto_chamadas++;
    if (g_texto_log_n < TEXTO_LOG_MAX && fonte < 0x800000u) {
        g_texto_log[g_texto_log_n++] = (texto_log_t){
            trace_last_func(), a0, (uint32_t)ctx->r5, (uint32_t)ctx->r6,
            (uint32_t)ctx->r7, tabela, fonte, rdram[fonte ^ 3u], (uint8_t)modo };
    }
    func_80094230__replaced(rdram, ctx);
}

/* O formatador da ROM atualiza o ponteiro de entrada a cada caractere. A
 * camada de legendas substitui-o somente durante a chamada e o restaura com
 * o deslocamento equivalente antes do retorno ao jogo. */
void func_80090E58__replaced(uint8_t* rdram, recomp_context* ctx);
void func_80090E58(uint8_t* rdram, recomp_context* ctx) {
    uint32_t args = (uint32_t)ctx->r4;
    legendas_antes(rdram, args);
    func_80090E58__replaced(rdram, ctx);
    legendas_depois(rdram, args);
}

void hle_texto_report(const char* prefixo) {
    printf("compositor CI8           : %u chamada(s); primeiras %u:\n",
           g_texto_chamadas, g_texto_log_n);
    for (uint32_t i = 0; i < g_texto_log_n; i++) {
        const texto_log_t* t = &g_texto_log[i];
        printf("   pai=func_%08X obj=0x%08X a1=%u a2=%u a3=%u tab=0x%08X"
               " src=0x%08X byte0=0x%02X modo=0x%02X\n",
               t->caller, t->a0, t->a1, t->a2, t->a3, t->tabela,
               t->fonte, t->primeiro_byte, t->modo);
    }
    /* O maior indice visto ja alcanca 0x11E0; 64 KiB cobrem a tabela inteira
       e permitem examina-la como imagem CI8 fora do ciclo de emulacao. */
    if (g_texto_log_n && g_texto_log[0].tabela + 0x10000u <= 0x800000u) {
        char nome[300];
        snprintf(nome, sizeof(nome), "%sobjetos_ci8.bin", prefixo);
        FILE* f = fopen(nome, "wb");
        if (f) {
            for (uint32_t i = 0; i < 0x10000u; i++)
                fputc(g_rdram[(g_texto_log[0].tabela + i) ^ 3u], f);
            fclose(f);
            printf("tabela CI8 exportada     : %s (64 KiB)\n", nome);
        }
    }
}

/* Definidos mais abaixo, junto da entrega de eventos; declarados aqui porque as
   substituicoes de DMA vem antes no arquivo. */
static uint32_t rd32(uint8_t* rdram, uint32_t addr);

/* O handler de VI da ROM programa o proximo origin durante a troca de
 * contexto. Esta leitura deve vir depois de sched_preempt(), nao depois de
 * desenhar a lista RSP: entre esses dois pontos fica justamente a imagem
 * residual que aparecia como cidade + corredor. */
static void video_present_pos_retrace(uint8_t* rdram) {
    if (!rsp_tasks_tipo(1)) return;
    uint32_t origin = *(uint32_t*)(rdram + (0xA4400004u - 0x80000000u)) & 0x1FFFFFFFu;
    uint32_t largura = *(uint32_t*)(rdram + (0xA4400008u - 0x80000000u)) & 0xFFFu;
    uint32_t formato = *(uint32_t*)(rdram + (0xA4400000u - 0x80000000u)) & 3u;
    uint32_t vstart = *(uint32_t*)(rdram + (0xA4400028u - 0x80000000u));
    uint32_t yscale = *(uint32_t*)(rdram + (0xA4400034u - 0x80000000u)) & 0xFFFu;
    uint32_t topo = (vstart >> 16) & 0x3FFu, base = vstart & 0x3FFu;
    uint32_t altura = (base > topo && yscale) ? ((base - topo) * yscale) / 2048u : 240u;
    if (!altura || altura > 240u) altura = 240u;
    if (origin && largura >= 320u && largura <= 640u && formato == 2u &&
        origin + largura * altura * 2u <= 0x800000u)
        video_present(rdram, origin, largura, altura, formato,
                      *(uint32_t*)(rdram + (0xA4400000u - 0x80000000u)));
}

/* Histograma dos blocos lidos do cartucho.
 *
 * O total de transferencias nao diz se o jogo esta carregando coisas novas ou
 * relendo a mesma coisa em laco. O endereco de origem diz. Um bloco lido
 * dezenas de vezes e uma tentativa que nao da certo, nao carregamento. */
#define DMA_HIST 64
static struct { uint32_t cart; uint32_t vezes; uint64_t bytes; } g_dma_hist[DMA_HIST];
static int g_dma_hist_n = 0;
static uint64_t g_dma_fora_hist = 0;

/* Faixas da RDRAM que o DMA de fato preencheu.
 *
 * O rasterizador le textura de 0x802CEF20 e encontra zeros. Escrever 2 KB e
 * ler 2 KB zerados so e possivel se a origem estiver zerada, entao a pergunta
 * deixou de ser "o LOADBLOCK funciona" e passou a ser "alguem ja escreveu
 * naquele endereco". Isto responde. */
#define FAIXA_MAX 32
static struct { uint32_t ini, fim; } g_faixa[FAIXA_MAX];
static int g_faixa_n = 0;

static void faixa_anotar(uint32_t ini, uint32_t bytes) {
    uint32_t fim = ini + bytes;
    for (int i = 0; i < g_faixa_n; i++) {
        if (ini <= g_faixa[i].fim + 0x1000 && fim + 0x1000 >= g_faixa[i].ini) {
            if (ini < g_faixa[i].ini) g_faixa[i].ini = ini;
            if (fim > g_faixa[i].fim) g_faixa[i].fim = fim;
            return;
        }
    }
    if (g_faixa_n < FAIXA_MAX) {
        g_faixa[g_faixa_n].ini = ini;
        g_faixa[g_faixa_n].fim = fim;
        g_faixa_n++;
    }
}

void hle_faixas_report(uint32_t consulta) {
    printf("faixas da RDRAM escritas por DMA (%d):\n", g_faixa_n);
    int cobre = 0;
    for (int i = 0; i < g_faixa_n; i++) {
        printf("   0x%08X - 0x%08X  (%u KB)\n", g_faixa[i].ini | 0x80000000u,
               g_faixa[i].fim | 0x80000000u, (g_faixa[i].fim - g_faixa[i].ini) / 1024);
        if (consulta >= g_faixa[i].ini && consulta < g_faixa[i].fim) cobre = 1;
    }
    printf("   endereco de textura 0x%08X: %s\n", consulta | 0x80000000u,
           cobre ? "DENTRO de uma faixa escrita" : "NUNCA escrito por DMA");
}

static void dma_anotar(uint32_t cart, uint32_t size) {
    for (int i = 0; i < g_dma_hist_n; i++) {
        if (g_dma_hist[i].cart == cart) {
            g_dma_hist[i].vezes++;
            g_dma_hist[i].bytes += size;
            return;
        }
    }
    if (g_dma_hist_n < DMA_HIST) {
        g_dma_hist[g_dma_hist_n].cart = cart;
        g_dma_hist[g_dma_hist_n].vezes = 1;
        g_dma_hist[g_dma_hist_n].bytes = size;
        g_dma_hist_n++;
    } else {
        g_dma_fora_hist++;
    }
}

/* O chamador usa 800BD218 para trazer pequenos cabecalhos de recursos. Para
 * fontes fisicas de ROM e destinos RDRAM, a rotina original enfileira a
 * requisicao no gerenciador de PI e espera a resposta. Nosso agendador ja
 * devolvia sucesso nessa espera, mas em alguns casos nao entregava o payload:
 * por exemplo, ROM 0x000FCD38 deveria produzir 04 7F e deixava 80 32.
 *
 * Esta substituicao cobre somente essa forma comprovada (ROM fisica -> RDRAM)
 * e mantem o corpo recompilado para ponteiros e modos que nao se encaixem.
 */
void func_800BD218__replaced(uint8_t* rdram, recomp_context* ctx);
static uint32_t g_resource_rom_copies = 0;

/* A tradução de Ryu acrescenta seu banco textual no final da ROM. Antes de
 * substituir bytes por um catálogo externo, é preciso comprovar por qual DMA
 * cada trecho chega à RDRAM e em qual buffer ele passa a ser consumido. O log
 * é totalmente opt-in: sem WPJ2_TEXT_ROM_TRACE não abre arquivo nem altera a
 * rota normal de cópia. */
#define WPJ2_TEXT_ROM_BEGIN 0x00800000u
#define WPJ2_TEXT_ROM_END   0x00820000u
static FILE* g_text_rom_trace = NULL;
static int g_text_rom_trace_ready = 0;
static uint32_t g_text_rom_trace_lines = 0;

static void text_rom_trace(uint32_t source, uint32_t target, uint32_t size,
                           const char* route) {
    uint64_t end = (uint64_t)source + size;
    if (!size || source >= WPJ2_TEXT_ROM_END || end <= WPJ2_TEXT_ROM_BEGIN)
        return;
    if (!g_text_rom_trace_ready) {
        const char* path = getenv("WPJ2_TEXT_ROM_TRACE");
        g_text_rom_trace_ready = 1;
        if (path && *path) {
            g_text_rom_trace = fopen(path, "w");
            if (g_text_rom_trace)
                fprintf(g_text_rom_trace, "route,rom_source,rdram_target,bytes\n");
        }
    }
    if (g_text_rom_trace && g_text_rom_trace_lines++ < 4096u) {
        fprintf(g_text_rom_trace, "%s,0x%08X,0x%08X,%u\n", route, source, target, size);
        fflush(g_text_rom_trace);
    }
}

/* A ROM e a RDRAM no host guardam cada palavra MIPS invertida. `memcpy` so
 * coincide quando endereco e tamanho cobrem palavras inteiras; cabecalhos de
 * 2 bytes precisam preservar a ordem logica de cada byte. */
static void copy_cart_logical(uint8_t* rdram, uint32_t target, uint32_t source,
                              uint32_t size) {
    uint8_t* cart = rdram + CART_WINDOW_OFFSET;
    for (uint32_t i = 0; i < size; i++)
        rdram[(target + i) ^ 3u] = cart[(source + i) ^ 3u];
}

void func_800BD218(uint8_t* rdram, recomp_context* ctx) {
    uint32_t source = (uint32_t)ctx->r4;
    uint32_t target = (uint32_t)ctx->r5;
    uint32_t size = (uint32_t)ctx->r6;
    uint32_t target_phys = target & 0x1FFFFFFFu;

    if (source < 0x04000000u && size != 0 && size <= 0x04000000u - source &&
        target_phys < 0x00800000u && size <= 0x00800000u - target_phys) {
        copy_cart_logical(rdram, target_phys, source, size);
        faixa_anotar(target_phys, size);
        text_rom_trace(source, target_phys, size, "resource");
        g_resource_rom_copies++;
        if (g_resource_rom_copies <= 12) {
            printf("  [res]  rom 0x%08X -> ram 0x%08X  %u bytes\\n", source, target, size);
            fflush(stdout);
        }
        ctx->r2 = 0;
        return;
    }
    func_800BD218__replaced(rdram, ctx);
}

void hle_dma_report(void) {
    printf("leituras do cartucho por bloco (%d distintos%s):\n", g_dma_hist_n,
           g_dma_fora_hist ? ", tabela cheia" : "");
    /* Ordena na hora, por repeticao: sao poucos e so imprime uma vez. */
    for (int mostrados = 0; mostrados < 12; mostrados++) {
        int melhor = -1;
        for (int i = 0; i < g_dma_hist_n; i++) {
            if (g_dma_hist[i].vezes && (melhor < 0
                || g_dma_hist[i].vezes > g_dma_hist[melhor].vezes)) melhor = i;
        }
        if (melhor < 0) break;
        printf("   cart 0x%08X  %u vez(es), %llu bytes\n", g_dma_hist[melhor].cart,
               g_dma_hist[melhor].vezes, (unsigned long long)g_dma_hist[melhor].bytes);
        g_dma_hist[melhor].vezes = 0;
    }
}

uint64_t hle_pi_transfers(void) { return g_pi_transfers; }
uint64_t hle_pi_bytes(void)     { return g_pi_bytes; }
uint64_t hle_pi_rejected(void)  { return g_pi_rejected; }

/* osPiRawStartDma(s32 dir, u32 devAddr, void* dramAddr, u32 size)
 *
 * Lido de func_800CB090:
 *   $a0 direcao   0 -> escreve PI_WR_LEN (cartucho para RDRAM)
 *                 1 -> escreve PI_RD_LEN (RDRAM para cartucho)
 *   $a1 devAddr   combinado com osRomBase (0x80000308) e mascarado com 0x1FFFFFFF
 *   $a2 dramAddr  passado por osVirtualToPhysical (func_800C79C0)
 *   $a3 size      escrito como size-1, como manda o hardware
 *   $v0 retorno   0 em sucesso, -1 se a direcao nao for 0 nem 1
 *
 * No hardware isto so *inicia* a transferencia; a conclusao chega por
 * interrupcao. Aqui ela e instantanea, o que e uma diferenca real de
 * comportamento e esta anotada no relatorio.
 */
void func_800CB090(uint8_t* rdram, recomp_context* ctx) {
    int32_t  dir  = (int32_t)ctx->r4;
    uint32_t dev  = (uint32_t)ctx->r5;
    uint32_t dram = (uint32_t)ctx->r6;
    uint32_t size = (uint32_t)ctx->r7;

    if (dir != 0 && dir != 1) {
        g_pi_rejected++;
        ctx->r2 = (gpr)(int32_t)-1;
        return;
    }

    /* Enderecos fisicos: a RDRAM comeca em 0, o cartucho em 0x10000000. */
    uint32_t phys_dram = dram & 0x1FFFFFFFu;
    uint32_t phys_dev  = (0xB0000000u | dev) & 0x1FFFFFFFu;

    uint8_t* ram  = rdram + phys_dram;
    uint8_t* cart = rdram + CART_WINDOW_OFFSET + (phys_dev - 0x10000000u);

    if (dir == 0) memcpy(ram, cart, size);
    else          memcpy(cart, ram, size);

    g_pi_transfers++;
    g_pi_bytes += size;
    dma_anotar(dev, size);
    if (dir == 0) faixa_anotar(phys_dram, size);
    if (dir == 0) text_rom_trace(dev, phys_dram, size, "pi_raw");
    if (g_pi_transfers <= 8) {
        printf("  [pi]   %s  cart 0x%08X -> ram 0x%08X  %u bytes\n",
               dir == 0 ? "leitura " : "escrita ", dev, dram, size);
        fflush(stdout);
    }
    ctx->r2 = 0;
}


/* __osEPiRawStartDma(OSPiHandle* handle, s32 dir, u32 devAddr, void* dram, u32 size)
 *
 * Lido de func_800D5060 (ROM 0xD5C60). Difere de osPiRawStartDma em dois pontos,
 * ambos visiveis no desassemblador:
 *   - o endereco base do dispositivo vem de `handle->baseAddress`, em 0xC($a0),
 *     em vez da constante 0xB0000000 lida de osRomBase;
 *   - `size` e o quinto argumento, entao chega na pilha em 0x10($sp) - o espaco
 *     que a ABI o32 reserva para $a0-$a3.
 *
 * Este e o caminho que o gerenciador de PI usa. Sem ele, o gerenciador escrevia
 * nos registradores de PI (que aqui sao memoria comum), esperava a interrupcao e
 * notificava o pedinte - com o buffer de destino intacto. O jogo recebia
 * "transferencia concluida" e lia zeros. */
void func_800D5060(uint8_t* rdram, recomp_context* ctx) {
    uint32_t handle = (uint32_t)ctx->r4;
    int32_t  dir    = (int32_t)ctx->r5;
    uint32_t dev    = (uint32_t)ctx->r6;
    uint32_t dram   = (uint32_t)ctx->r7;
    uint32_t size   = rd32(rdram, (uint32_t)ctx->r29 + 0x10);

    if ((dir != 0 && dir != 1) || !handle) {
        g_pi_rejected++;
        ctx->r2 = (gpr)(int32_t)-1;
        return;
    }
    uint32_t base = rd32(rdram, handle + 0x0C);
    uint32_t phys_dev = (base | dev) & 0x1FFFFFFFu;

    /* So o dominio do cartucho e atendido; qualquer outro e recusado em vez de
       copiar lixo de um lugar que nao existe. */
    if (phys_dev < 0x10000000u) {
        g_pi_rejected++;
        ctx->r2 = (gpr)(int32_t)-1;
        return;
    }

    uint8_t* ram  = rdram + (dram & 0x1FFFFFFFu);
    uint8_t* cart = rdram + CART_WINDOW_OFFSET + (phys_dev - 0x10000000u);
    if (dir == 0) memcpy(ram, cart, size);
    else          memcpy(cart, ram, size);

    g_pi_transfers++;
    g_pi_bytes += size;
    dma_anotar(dev, size);
    if (dir == 0) faixa_anotar(dram & 0x1FFFFFFFu, size);
    if (dir == 0) text_rom_trace(phys_dev - 0x10000000u,
                                 dram & 0x1FFFFFFFu, size, "pi_epi");
    if (g_pi_transfers <= 8) {
        printf("  [pi]   epi %s base=0x%08X cart 0x%08X -> ram 0x%08X  %u bytes\n",
               dir == 0 ? "leitura" : "escrita", base, dev, dram, size);
        fflush(stdout);
    }
    ctx->r2 = 0;
}

/* ------------------------------------------------------------------ */
/* Entrega de interrupcao                                              */
/* ------------------------------------------------------------------ */

/* A tabela de eventos e a rotina que posta neles foram lidas em func_800CC8A4
 * (ROM 0xCD4A4), a sub-rotina que __osException chama 14 vezes:
 *
 *   lui $t2,0x801B ; addiu $t2,$t2,-0x580   ->  __osEventStateTab = 0x801AFA80
 *   addu $t2,$t2,$a0                            $a0 e o indice ja em bytes
 *   lw $t1,0x0($t2)                             a OSMesgQueue do evento
 *   lw $t3,0x8($t1) / lw $t4,0x10($t1)          validCount / msgCount
 *   lw $t5,0xC($t1) / lw $t4,0x14($t1)          first / vetor de mensagens
 *   jal 0x800CCAD4 ; jal 0x800CCA8C             __osPopThread / __osEnqueueThread
 *
 * Ou seja: postar um evento e chamar essa funcao com $a0 = evento * 8. Fazemos
 * exatamente isso - a rotina que acorda a thread e a da propria ROM, nao uma
 * reimplementacao nossa. So o *momento* da interrupcao e que vem do host.
 */
#define ADDR_EVENT_STATE_TAB  0x801AFA80u
#define OS_EVENT_COUNT        15
#define OS_EVENT_COUNTER      3
#define OS_EVENT_SP           4
#define OS_EVENT_SI           5
#define OS_EVENT_AI           6
#define OS_EVENT_DP           9
#define OS_EVENT_VI           7
#define OS_EVENT_PI           8

/* Taxa de retrace, configuravel: o jogo pode depender de quantos quadros passam
   por segundo para decidir carregar a proxima coisa, e testar isso em paralelo
   custa o mesmo que testar uma taxa so. */
static double g_retrace_hz = 60.0;
static double g_retrace_normal_hz = 60.0;
/* F10 continua visivel como controle, mas nao altera a emulacao ate existir
 * um scheduler que suporte salvar/restaurar suas fibers. */
static int g_fast_forward = 0;
/* O prazo e double em ticks QPC para conservar a fracao de 1/60. Um contador
 * inteiro arredondado cria uma deriva pequena, porem mensuravel em cutscenes
 * longas. */
static LARGE_INTEGER g_poll_freq;
static double g_poll_deadline;

void hle_set_retrace(double hz) {
    if (hz <= 0.5 || hz >= 2000.0) return;
    g_retrace_normal_hz = hz;
    g_retrace_hz = hz;
    if (g_retrace_hz > 1000.0) g_retrace_hz = 1000.0;
}

int hle_toggle_fast_forward(void) {
    /* Confirmado pelo teste manual: elevar o retrace suspende uma fiber e a
     * segunda tecla a devolve exatamente ao mesmo ponto; nao houve avancar
     * oculto. Portanto nao fingimos fast-forward. */
    g_fast_forward = 0;
    return 0;
}

int hle_fast_forward_active(void) { return g_fast_forward; }
#define RETRACE_PERIOD_S      (1.0 / g_retrace_hz)
#define POLL_BUDGET           4096

long g_poll_budget = POLL_BUDGET;

/* Quais eventos entregar, um bit por indice. O padrao e o conjunto que a
   experiencia mostrou correto; poder mudar por ambiente e o que permite rodar
   varias combinacoes ao mesmo tempo e comparar cobertura entre elas em vez de
   testar uma por vez. */
static uint32_t g_event_mask = (1u << OS_EVENT_COUNTER) | (1u << OS_EVENT_SI)
                              | (1u << OS_EVENT_AI)      | (1u << OS_EVENT_VI)
                              | (1u << OS_EVENT_PI)
                             | (1u << OS_EVENT_SP)      | (1u << OS_EVENT_DP);

void hle_set_event_mask(uint32_t m) { g_event_mask = m; }
uint32_t hle_event_mask(void)       { return g_event_mask; }

#define EVENT_ON(ev) (g_event_mask & (1u << (ev)))

static int g_in_poll = 0;
static int g_table_dumped = 0;
static uint64_t g_retraces = 0;
static uint64_t g_polls = 0;
static uint64_t g_force_state2_after = 0;
static int g_force_state2_done = 0;
static uint64_t g_force_active_after = 0;
static int32_t g_force_active_index = 0;
static int g_force_active_done = 0;
static int g_force_next_scene = 0;
static int g_force_next_scene_done = 0;
static int g_hold_state_8_1 = 0;
static int g_hold_state_8_1_armed = 0;
static int g_hold_state_8_1_reported = 0;
static int g_hold_state_12_50 = 0;
static int g_hold_state_12_50_armed = 0;
static int g_hold_state_12_50_reported = 0;
static unsigned g_title_update_calls = 0;
static uint32_t g_title_last_flags = UINT32_MAX;
static const char* g_status_path = NULL;
static const char* g_status_timeline_path = NULL;
static ULONGLONG g_status_last_ms = 0;
static int g_status_timeline_first = 1;
static int g_rsp_dp_pending = 0;
/* So para a sonda visual acelerada: permite alternar fibers em cada poll,
 * reproduzindo a cadencia historica das capturas de 3D. A emulacao normal
 * continua preemptando apenas quando um retrace/evento foi entregue. */
static int g_preempt_every_poll = 0;
static int g_high_res_timer = 0;
/* O ucode recompilado nao materializa `mtc0 Compare`. Sem esta ponte o
 * temporizador de libultra recebia COUNTER em todo VI, independentemente do
 * prazo armado. Mantemos a variante antiga como fallback parametrizavel ate a
 * cadencia ser validada pela AList de audio. */
static int g_counter_compare_mode = -1;
static int g_counter_compare_armed = 0;
static uint32_t g_counter_compare = 0;

static int hle_counter_compare_mode(void) {
    if (g_counter_compare_mode < 0) {
        const char* e = getenv("WPJ2_COUNTER_COMPARE");
        g_counter_compare_mode = e && *e && *e != '0';
    }
    return g_counter_compare_mode;
}

/* __osSetCompare: a0 contem o proximo valor absoluto do COP0 Count. */
void func_800D54F0(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    if (hle_counter_compare_mode()) {
        g_counter_compare = (uint32_t)ctx->r4;
        g_counter_compare_armed = 1;
    }
}

/* Sleep(1) segue a granularidade configurada do Windows; sem elevar a
 * resolucao ele costuma dormir 15,6 ms. Como o poll ja chega depois de parte
 * do periodo NTSC, isso transformava cada VI em ~25 ms (~39 Hz). */
void hle_clock_init(void) {
    if (!g_high_res_timer && timeBeginPeriod(1) == TIMERR_NOERROR)
        g_high_res_timer = 1;
}
void hle_clock_shutdown(void) {
    if (g_high_res_timer) {
        timeEndPeriod(1);
        g_high_res_timer = 0;
    }
}

uint64_t hle_retraces(void) { return g_retraces; }
uint64_t hle_polls(void)    { return g_polls; }
void hle_force_state2_after(uint64_t polls) { g_force_state2_after = polls ? polls : 1; }
void hle_force_active_after(uint64_t polls, int32_t index) {
    g_force_active_after = polls ? polls : 1;
    g_force_active_index = index;
}
void hle_force_next_scene(void) { g_force_next_scene = 1; }
void hle_hold_state_8_1(void) { g_hold_state_8_1 = 1; }
void hle_hold_state_12_50(void) { g_hold_state_12_50 = 1; }
void hle_set_preempt_every_poll(int enabled) { g_preempt_every_poll = enabled != 0; }

static uint32_t rd32(uint8_t* rdram, uint32_t addr);

/* Sonda leve para builds de liberacao. So existe quando WPJ2_STATUS_FILE e
 * fornecido; sobrescreve um unico arquivo no maximo quatro vezes por segundo e
 * nao percorre display lists nem despeja imagens. */
static void status_note(uint8_t* rdram) {
    if (!g_status_path) {
        const char* e = getenv("WPJ2_STATUS_FILE");
        g_status_path = e ? e : "";
    }
    if (!g_status_timeline_path) {
        const char* e = getenv("WPJ2_STATUS_TIMELINE");
        g_status_timeline_path = e ? e : "";
    }
    if (!g_status_path[0] && !g_status_timeline_path[0]) return;
    ULONGLONG now = GetTickCount64();
    if (now - g_status_last_ms < 250) return;
    g_status_last_ms = now;
    FILE* f = g_status_path[0] ? fopen(g_status_path, "w") : NULL;
    uint32_t spq = rd32(rdram, ADDR_EVENT_STATE_TAB + OS_EVENT_SP * 8);
    uint32_t dpq = rd32(rdram, ADDR_EVENT_STATE_TAB + OS_EVENT_DP * 8);
    uint32_t sp_wait = spq ? rd32(rdram, spq) : 0;
    uint32_t task_thread = 0x80153F00u; /* gerente de OSTask observado na abertura */
    int16_t estado = (int16_t)rdram16(rdram, 0x001A7234u);
    int16_t subestado = (int16_t)rdram16(rdram, 0x001A723Cu);
    if (f) {
        fprintf(f, "ms=%llu\nretrace=%llu\npolls=%llu\ntitle_update=%u\n"
                   "title_flags=%08X\nestado=%d/%d\ntarefas_rsp=%llu\n"
                   "graficas=%llu\naudio=%llu\nraster_ultimo_us=%llu\n"
                   "raster_pico_us=%llu\nraster_amostras=%llu\n"
                   "audio_alist_ultimo_us=%llu\naudio_alist_pico_us=%llu\n"
                   "audio_alist_amostras=%llu\n"
                   "camera_descartados=%llu\ntriangulos_culled=%llu\n"
                   "prim=%08X\nenv=%08X\nothermode_l=%08X\n"
                   "alpha_texrects=%llu\nalpha_rect=%u,%u,%u,%u\n"
                   "running=%08X\nrun_queue=%08X\nrsp_done=%d\ndp_pendente=%d\n"
                   "sp_fila=%08X %u/%u\nsp_espera=%08X prox=%08X\n"
                   "dp_fila=%08X %u/%u\ntask_thread=%08X estado=%u fila=%08X\n",
            (unsigned long long)now,
            (unsigned long long)g_retraces, (unsigned long long)g_polls,
            g_title_update_calls, g_title_last_flags,
             estado, subestado,
             (unsigned long long)rsp_tasks(),
             (unsigned long long)rsp_tasks_tipo(1),
             (unsigned long long)rsp_tasks_tipo(2),
             (unsigned long long)rsp_gfx_raster_last_us(),
             (unsigned long long)rsp_gfx_raster_peak_us(),
             (unsigned long long)rsp_gfx_raster_samples(),
             (unsigned long long)rsp_acmd_last_us(),
             (unsigned long long)rsp_acmd_peak_us(),
             (unsigned long long)rsp_acmd_samples(),
             (unsigned long long)rsp_camera_discarded_vertices(),
             (unsigned long long)rsp_culled_triangles(),
             rsp_prim_color(), rsp_env_color(), rsp_othermode_l(),
             (unsigned long long)rsp_alpha_texrects(),
             rsp_alpha_rect_x0(), rsp_alpha_rect_y0(), rsp_alpha_rect_x1(), rsp_alpha_rect_y1(),
             rd32(rdram, ADDR_RUNNING_THREAD), rd32(rdram, ADDR_RUN_QUEUE),
            rsp_peek_task_done(), g_rsp_dp_pending,
            spq, spq ? rd32(rdram, spq + 0x08) : 0, spq ? rd32(rdram, spq + 0x10) : 0,
            sp_wait, sp_wait ? rd32(rdram, sp_wait) : 0,
            dpq, dpq ? rd32(rdram, dpq + 0x08) : 0, dpq ? rd32(rdram, dpq + 0x10) : 0,
            task_thread, (unsigned)MEM_HU(0, (gpr)(int32_t)(task_thread + TH_STATE)),
             rd32(rdram, task_thread + TH_QUEUE));
        fclose(f);
    }
    if (g_status_timeline_path[0]) {
        FILE* t = fopen(g_status_timeline_path, g_status_timeline_first ? "w" : "a");
        if (t) {
            if (g_status_timeline_first)
                fprintf(t, "ms,retrace,estado,subestado,gfx,audio,raster_ultimo_us,raster_pico_us,audio_alist_ultimo_us,audio_alist_pico_us,camera_descartados,triangulos_culled,prim,env,othermode_l,alpha_texrects,alpha_x0,alpha_y0,alpha_x1,alpha_y1\n");
            fprintf(t, "%llu,%llu,%d,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%08X,%08X,%08X,%llu,%u,%u,%u,%u\n",
                    (unsigned long long)now, (unsigned long long)g_retraces, estado, subestado,
                    (unsigned long long)rsp_tasks_tipo(1), (unsigned long long)rsp_tasks_tipo(2),
                    (unsigned long long)rsp_gfx_raster_last_us(),
                    (unsigned long long)rsp_gfx_raster_peak_us(),
                    (unsigned long long)rsp_acmd_last_us(),
                    (unsigned long long)rsp_acmd_peak_us(),
                    (unsigned long long)rsp_camera_discarded_vertices(),
                    (unsigned long long)rsp_culled_triangles(),
                    rsp_prim_color(), rsp_env_color(), rsp_othermode_l(),
                    (unsigned long long)rsp_alpha_texrects(),
                    rsp_alpha_rect_x0(), rsp_alpha_rect_y0(),
                    rsp_alpha_rect_x1(), rsp_alpha_rect_y1());
            fclose(t);
            g_status_timeline_first = 0;
        }
    }
}

static uint32_t rd32(uint8_t* rdram, uint32_t addr) {
    return (uint32_t)MEM_W(0, (gpr)(int32_t)addr);
}
static void wr32(uint8_t* rdram, uint32_t addr, uint32_t val) {
    MEM_W(0, (gpr)(int32_t)addr) = (int32_t)val;
}
static void wr16(uint8_t* rdram, uint32_t addr, uint16_t val) {
    MEM_HU(0, (gpr)(int32_t)addr) = val;
}

static const char* k_event_names[OS_EVENT_COUNT] = {
    "SW1", "SW2", "CART", "COUNTER", "SP", "SI", "AI", "VI",
    "PI", "DP", "CPU_BREAK", "SP_BREAK", "FAULT", "THREADSTATUS", "PRENMI",
};
static unsigned g_event_trace_lines = 0;
static unsigned g_event_trace_by_type[OS_EVENT_COUNT];

static int event_trace_enabled(void) {
    return getenv("WPJ2_EVENT_TRACE") != NULL;
}
static int event_trace_match(int event) {
    return event == OS_EVENT_PI || event == OS_EVENT_SI ||
           event == OS_EVENT_SP || event == OS_EVENT_DP || event == OS_EVENT_AI;
}

/* Uma PI cheia acontece em praticamente todo retrace neste estado. Limitar por
 * tipo preserva as primeiras transicoes de SI/SP/DP no mesmo arquivo. */
static int event_trace_take(int event) {
    if (!event_trace_enabled() || !event_trace_match(event)) return 0;
    if (g_event_trace_lines >= 80 || g_event_trace_by_type[event] >= 12) return 0;
    g_event_trace_lines++;
    g_event_trace_by_type[event]++;
    return 1;
}

/* Quais eventos o jogo realmente registrou. Sem isto, entregar uma interrupcao
   e chutar: uma fila nula significa que ninguem espera aquele evento. */
static void dump_event_table(uint8_t* rdram) {
    printf("  [evt]  __osEventStateTab 0x%08X:\n", ADDR_EVENT_STATE_TAB);
    int any = 0;
    for (int i = 0; i < OS_EVENT_COUNT; i++) {
        uint32_t q = rd32(rdram, ADDR_EVENT_STATE_TAB + i * 8);
        if (!q) continue;
        printf("  [evt]    %-12s fila 0x%08X  validCount=%u msgCount=%u"
               " mtqueue=0x%08X\n",
               k_event_names[i], q, rd32(rdram, q + 0x08), rd32(rdram, q + 0x10),
               rd32(rdram, q + 0x00));
        any = 1;
    }
    if (!any) printf("  [evt]    (nenhum evento registrado ainda)\n");
    fflush(stdout);
}

/* __osEnqueueThread: insercao ordenada por prioridade, com o ponteiro de cabeca
   tratado como o campo `next` de um no falso - possivel porque `next` esta no
   deslocamento 0. */
static void enqueue_thread(uint8_t* rdram, uint32_t queue_addr, uint32_t thread) {
    int32_t pri = (int32_t)rd32(rdram, thread + TH_PRIORITY);
    uint32_t pred = queue_addr;
    uint32_t cur = rd32(rdram, queue_addr);
    while (cur && pri <= (int32_t)rd32(rdram, cur + TH_PRIORITY)) {
        pred = cur;
        cur = rd32(rdram, cur + TH_NEXT);
    }
    wr32(rdram, thread + TH_NEXT, cur);
    wr32(rdram, pred + TH_NEXT, thread);
    /* Mesmo contrato de __osEnqueueThread: a fila precisa ficar espelhada no
       OSThread. Sem isso uma thread acordada por VI/SI parece ainda pertencer
       a uma fila antiga (ou nula) quando uma rotina posterior a remove. */
    wr32(rdram, thread + TH_QUEUE, queue_addr);
}

/* `func_800C4AA0` e osSendMesg. A atribuicao anterior para 0x800CC370 estava
 * errada: esse endereco e o manipulador de excecoes, e sequer e alcançado pelo
 * jogo neste caminho. Mantemos o corpo recompilado de osSendMesg e registramos
 * apenas postagens para a fila em que o carregamento inicial esta bloqueado. */
#define FILA_INICIAL 0x801ACB64u
#define FILA_TRANSICAO 0x800F9C20u
#define MSG_LOG_MAX 16
typedef struct { uint32_t pai, mensagem, antes, depois; } msg_log_t;
static msg_log_t g_msg_log[MSG_LOG_MAX];
static uint32_t g_msg_enviadas = 0, g_msg_log_n = 0;

void func_800C4AA0__replaced(uint8_t* rdram, recomp_context* ctx);

void func_800C4AA0(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t mq = (uint32_t)ctx->r4;
    const uint32_t mensagem = (uint32_t)ctx->r5;
    const uint32_t antes = mq ? rd32(rdram, mq + 0x08) : 0;
    if (mq == FILA_INICIAL && g_msg_log_n < MSG_LOG_MAX) {
        g_msg_log[g_msg_log_n++] = (msg_log_t){ trace_last_func(), mensagem, antes, antes };
    }
    func_800C4AA0__replaced(rdram, ctx);
    if (mq == FILA_INICIAL) {
        g_msg_enviadas++;
        if (g_msg_log_n) g_msg_log[g_msg_log_n - 1].depois = rd32(rdram, mq + 0x08);
    }
    if (mq == FILA_TRANSICAO) {
        static unsigned transicao_envios = 0;
        if (transicao_envios++ < 24) {
            printf("[transicao-msg] envia msg=%08X retorno=%d fila=%u/%u por=%08X\n",
                   mensagem, (int32_t)ctx->r2, rd32(rdram, mq + 0x08),
                   rd32(rdram, mq + 0x10), (uint32_t)ctx->r31);
            fflush(stdout);
        }
    }
}

/* Cada objeto do gerenciador tem 0x48 bytes; os campos 0x1A0/0x1A4 guardam
 * flags e o indice do par init/update.  A consulta e chamada pela thread de
 * roteiro antes de executar a atualizacao.  O log mostra se o objeto do par
 * que contem 8000DB14 sequer foi ativado. */
void func_800045AC__replaced(uint8_t* rdram, recomp_context* ctx);
static unsigned g_callback_queries = 0;
void func_800045AC(uint8_t* rdram, recomp_context* ctx) {
    uint32_t grupo = (uint32_t)ctx->r4;
    if (g_callback_queries++ < 12) {
        printf("[callback] consulta grupo=%u; objetos ativos:", grupo);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t obj = 0x80156C20u + i * 0x48u;
            uint32_t flags = rd32(rdram, obj + 0x1A0u);
            uint32_t id = rd32(rdram, obj + 0x1A4u);
            if (flags & 1u) {
                uint32_t init = rd32(rdram, 0x800DDB70u + id * 8u);
                uint32_t update = rd32(rdram, 0x800DDB74u + id * 8u);
                printf(" [slot%u id=%u f=%X init=%08X update=%08X]",
                       i, id, flags, init, update);
            }
        }
        printf("\n");
        fflush(stdout);
    }
    func_800045AC__replaced(rdram, ctx);
}

/* 0x8000BDDC chega a este carregador uma vez na inicializacao.  O retorno
 * negativo encerra a rotina sem criar o proximo objeto; registrar ambos os
 * lados separa uma tabela de recursos ausente de um erro posterior. */
void func_8000C584__replaced(uint8_t* rdram, recomp_context* ctx);
static unsigned g_scene_loads = 0;
void func_8000C584(uint8_t* rdram, recomp_context* ctx) {
    uint32_t destino = (uint32_t)ctx->r4;
    int32_t indice = (int32_t)ctx->r5;
    func_8000C584__replaced(rdram, ctx);
    if (g_scene_loads++ < 8) {
        printf("[cena] C584 destino=%08X indice=%d retorno=%d\n",
               destino, indice, (int32_t)ctx->r2);
        fflush(stdout);
    }
}

/* O despachante executa a metade de inicializacao do par solicitado.  A
 * sequencia e curta no boot; ela mostra se o par D580/DB14 foi registrado
 * corretamente antes de investigar sua reativacao. */
void func_80004654__replaced(uint8_t* rdram, recomp_context* ctx);
static unsigned g_dispatches = 0;
void func_80004654(uint8_t* rdram, recomp_context* ctx) {
    uint32_t grupo = (uint32_t)ctx->r4;
    func_80004654__replaced(rdram, ctx);
    if (g_dispatches++ < 24) {
        printf("[despacho] grupo=%u retorno=%d; ativos:", grupo, (int32_t)ctx->r2);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t obj = 0x80156C20u + i * 0x48u;
            uint32_t flags = rd32(rdram, obj + 0x1A0u);
            uint32_t id = rd32(rdram, obj + 0x1A4u);
            if (flags & 1u) printf(" [%u:%u/%X]", i, id, flags);
        }
        printf("\n");
        fflush(stdout);
    }
}

/* O loop principal recebe eventos nesta rotina; o valor devolvido determina os
 * casos de 0x80000C90, incluindo o unico caminho para 0x80021ED0. */
void func_800C4C40__replaced(uint8_t* rdram, recomp_context* ctx);
static unsigned g_receives = 0;
void func_800C4C40(uint8_t* rdram, recomp_context* ctx) {
    uint32_t fila = (uint32_t)ctx->r4;
    uint32_t saida = (uint32_t)ctx->r5;
    uint32_t bloqueia = (uint32_t)ctx->r6;
    uint32_t caller = (uint32_t)ctx->r31;
    func_800C4C40__replaced(rdram, ctx);
    if (fila == FILA_TRANSICAO) {
        static unsigned transicao_recebes = 0;
        if (transicao_recebes++ < 24) {
            printf("[transicao-msg] recebe retorno=%d bloqueia=%u msg=%08X fila=%u/%u por=%08X\n",
                   (int32_t)ctx->r2, bloqueia, saida ? rd32(rdram, saida) : 0,
                   rd32(rdram, fila + 0x08), rd32(rdram, fila + 0x10), caller);
            fflush(stdout);
        }
    }
    /* A fila VI domina o inicio. Para investigar a passagem apos o titulo,
       reservamos o log para as filas de PI e para filas nao-VI. */
    if (g_receives++ < 96 && (int32_t)ctx->r2 == 0 && fila != 0x801AFA10u) {
        printf("[recebe] fila=%08X bloqueia=%u msg=%08X valid=%u/%u\n",
               fila, bloqueia, saida ? rd32(rdram, saida) : 0,
               fila ? rd32(rdram, fila + 0x08) : 0,
               fila ? rd32(rdram, fila + 0x10) : 0);
        fflush(stdout);
    }
}

/* O oraculo real confirmou que 00C90 chega a 01F54 e chama 21ED0. No
 * recompilado, a telemetria de entrada indicou que essa cadeia se interrompe
 * antes da guarda. Estes quatro wrappers preservam integralmente os corpos
 * recompilados e registram a ordem/retorno, sem depender do hook automatico
 * de entrada (que nao diferencia uma chamada C aninhada de uma troca de
 * fiber). */
void func_8009908C__replaced(uint8_t* rdram, recomp_context* ctx);
void func_80098D24__replaced(uint8_t* rdram, recomp_context* ctx);
void func_80099450__replaced(uint8_t* rdram, recomp_context* ctx);
void func_80001F54__replaced(uint8_t* rdram, recomp_context* ctx);
static unsigned g_gate_path_log = 0;

static void gate_path_mark(const char* stage, uint8_t* rdram, recomp_context* ctx) {
    if (g_gate_path_log++ < 48) {
        printf("[gate-path] %s pai=%08X r2=%08X flags=%04X pending=%04X active=%08X\n",
               stage, trace_last_func(), (uint32_t)ctx->r2,
               rdram16(rdram, 0x001A8D88u), rdram16(rdram, 0x001A8D8Eu),
               rd32(rdram, 0x801ACC44u));
        fflush(stdout);
    }
}

void func_8009908C(uint8_t* rdram, recomp_context* ctx) {
    gate_path_mark("enter 9908C", rdram, ctx);
    func_8009908C__replaced(rdram, ctx);
    gate_path_mark("exit  9908C", rdram, ctx);
}

void func_80098D24(uint8_t* rdram, recomp_context* ctx) {
    gate_path_mark("enter 98D24", rdram, ctx);
    func_80098D24__replaced(rdram, ctx);
    gate_path_mark("exit  98D24", rdram, ctx);
}

void func_80099450(uint8_t* rdram, recomp_context* ctx) {
    gate_path_mark("enter 99450", rdram, ctx);
    func_80099450__replaced(rdram, ctx);
    gate_path_mark("exit  99450", rdram, ctx);
}

void func_80001F54(uint8_t* rdram, recomp_context* ctx) {
    gate_path_mark("enter 01F54", rdram, ctx);
    func_80001F54__replaced(rdram, ctx);
    gate_path_mark("exit  01F54", rdram, ctx);
}

void func_80002F20__replaced(uint8_t* rdram, recomp_context* ctx);
static unsigned g_2f20_log = 0;
static unsigned g_2f20_depth = 0;

void func_80098820__replaced(uint8_t* rdram, recomp_context* ctx);
void func_800C0A40__replaced(uint8_t* rdram, recomp_context* ctx);

void func_80098820(uint8_t* rdram, recomp_context* ctx) {
    if (g_2f20_depth) printf("[2f20] -> 98820\\n");
    func_80098820__replaced(rdram, ctx);
    if (g_2f20_depth) printf("[2f20] <- 98820\\n");
}

void func_800C0A40(uint8_t* rdram, recomp_context* ctx) {
    if (g_2f20_depth) printf("[2f20] -> C0A40\\n");
    func_800C0A40__replaced(rdram, ctx);
    if (g_2f20_depth) printf("[2f20] <- C0A40\\n");
}

void func_8000BDDC__replaced(uint8_t* rdram, recomp_context* ctx);
void func_800C143C__replaced(uint8_t* rdram, recomp_context* ctx);
void func_800C7730__replaced(uint8_t* rdram, recomp_context* ctx);
void func_800C7A40__replaced(uint8_t* rdram, recomp_context* ctx);

/* Atalho de diagnostico, desligado por padrao. O Project64 mostra que a
 * abertura automatica passa 0x3 -> 0xB no objeto do titulo; no runtime atual
 * o despachante para antes dessa notificacao. Isto permite testar o proximo
 * caminho real (inclusive a lista 3D) sem confundir START com essa transicao. */
static int g_autocutscene = -1;
void func_8000BDDC(uint8_t* rdram, recomp_context* ctx) {
    uint32_t host = (uint32_t)ctx->r4;
    uint32_t list = host ? rd32(rdram, host + 0x20Cu) : 0;
    uint32_t object = list ? rd32(rdram, list) : 0;
    uint32_t flags = object ? rd32(rdram, object + 0x1A0u) : 0;
    if (g_autocutscene < 0) {
        const char* env = getenv("WPJ2_AUTOCUTSCENE");
        g_autocutscene = env && atoi(env) != 0;
    }
    if (g_autocutscene && object && g_title_update_calls >= 7 &&
        (flags & 0xBu) == 0x3u) {
        flags |= 0x8u;
        wr32(rdram, object + 0x1A0u, flags);
        g_autocutscene = 0; /* uma unica transicao por execucao */
        printf("[autocutscene] bit 0x8 aplicado ao titulo; seguindo rota automatica nativa\n");
        fflush(stdout);
    }
    /* Esta atualizacao so carrega a proxima cena depois que o objeto atual
       marca o bit 0x8. O log por transicao nao muda a temporizacao. */
    if (g_title_update_calls < 8 || flags != g_title_last_flags ||
        (g_title_update_calls != 0 && (g_title_update_calls % 300u) == 0u)) {
        printf("[titulo-update] n=%u host=%08X obj=%08X flags=%08X estado=%d\n",
               g_title_update_calls, host, object, flags,
               (int16_t)rdram16(rdram, 0x001A8C40u));
        fflush(stdout);
        g_title_last_flags = flags;
    }
    g_title_update_calls++;
    func_8000BDDC__replaced(rdram, ctx);
}

void func_800C143C(uint8_t* rdram, recomp_context* ctx) {
    func_800C143C__replaced(rdram, ctx);
}

/* osAiSetFrequency. A versao da ROM divide pelo clock armazenado pela
 * inicializacao de libultra; como esse registrador de hardware ainda nao era
 * materializado no runtime, ele permanecia zero e a rotina devolvia 0. Isso
 * prendia o construtor de AList ao minimo de 720 frames. Reproduzimos a
 * rotina com o clock NTSC real e os mesmos registradores AI. */
void func_800C7730(uint8_t* rdram, recomp_context* ctx) {
    enum { AI_MMIO_OFFSET = 0x24500000u, AI_DACRATE = 0x10u,
           AI_BITRATE = 0x14u, AI_CONTROL = 0x08u };
    const uint32_t ntsc_clock = 48681812u;
    int32_t requested = (int32_t)ctx->r4;
    if (requested <= 0) { ctx->r2 = (gpr)(int32_t)-1; return; }

    /* O COP1 do original soma 0,5 e converte para inteiro com arredondamento
       configurado pelo OS; para este clock/22.050 Hz o resultado observado e
       o teto, 2208, que produz precisamente 22.047 Hz no Project64. */
    uint32_t divisor = (ntsc_clock + (uint32_t)requested - 1u) /
                       (uint32_t)requested;
    if (divisor < 132u) { ctx->r2 = (gpr)(int32_t)-1; return; }
    uint32_t bitrate = divisor / 66u;
    if (!bitrate) bitrate = 1;
    if (bitrate > 16u) bitrate = 16u;
    *(uint32_t*)(rdram + AI_MMIO_OFFSET + AI_DACRATE) = divisor - 1u;
    *(uint32_t*)(rdram + AI_MMIO_OFFSET + AI_BITRATE) = bitrate - 1u;
    *(uint32_t*)(rdram + AI_MMIO_OFFSET + AI_CONTROL) = 1u;
    ctx->r2 = ntsc_clock / divisor;
    audio_set_frequency((uint32_t)ctx->r2);
    {
        const char* trace = getenv("WPJ2_AI_LEN_TRACE");
        if (trace && *trace && *trace != '0') {
            printf("[ai-rate] pedido=%d divisor=%u efetiva=%u\n", requested,
                   divisor, (uint32_t)ctx->r2);
            fflush(stdout);
        }
    }
}

/* osAiGetLength: expor o restante fisico do DMA, nao o ultimo valor escrito
 * em AI_LEN. A imagem MMIO ainda recebe as escritas da libultra; esta leitura
 * e a semantica que ela espera do dispositivo. */
void func_800C79B0(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = audio_ai_length();
    /* Sonda curta, opt-in: a proxima decisao da AList depende diretamente
       deste restante. Ela separa um problema de PCM de um problema de relogio
       virtual sem despejar o log normal do executavel. */
    static int trace_enabled = -1;
    static uint32_t trace_count;
    if (trace_enabled < 0) {
        const char* e = getenv("WPJ2_AI_LEN_TRACE");
        trace_enabled = e && *e && *e != '0';
    }
    if (trace_enabled && trace_count++ < 96) {
        printf("[ai-len] %u\n", (uint32_t)ctx->r2);
        fflush(stdout);
    }
}

/* O buffer ja foi produzido pela tarefa de audio da RSP; capturar aqui e o
 * equivalente host de o AI iniciar seu DMA, sem alterar as escritas MMIO que o
 * jogo ainda le para controlar a propria fila. */
void func_800C7A40(uint8_t* rdram, recomp_context* ctx) {
    func_800C7A40__replaced(rdram, ctx);
    if ((int32_t)ctx->r2 == 0) {
        /* A rotina libultra nao usa necessariamente os argumentos crus: ela
         * normaliza KSEG0/KSEG1 e, em alguns blocos, aplica o deslocamento do
         * buffer circular antes de gravar AI_DRAM_ADDR/AI_LEN. A captura host
         * precisa observar os registradores finais, como faria o dispositivo,
         * e nao a entrada da API. */
        enum { AI_MMIO_OFFSET = 0x24500000u };
        uint32_t address = *(uint32_t*)(rdram + AI_MMIO_OFFSET + 0x00u);
        uint32_t bytes = *(uint32_t*)(rdram + AI_MMIO_OFFSET + 0x04u);
        audio_queue_ai_buffer(rdram, address, bytes);
    }
}

void func_80002F20(uint8_t* rdram, recomp_context* ctx) {
    if (g_2f20_log < 8) {
        printf("[2f20] entrada flags=%04X estado=%d/%d pendente=%d/%d\n",
               rdram16(rdram, 0x001A8D88u),
               (int16_t)rdram16(rdram, 0x001A7234u), (int16_t)rdram16(rdram, 0x001A723Cu),
               (int16_t)rdram16(rdram, 0x001A7254u), (int16_t)rdram16(rdram, 0x001A725Cu));
    }
    g_2f20_depth++;
    func_80002F20__replaced(rdram, ctx);
    g_2f20_depth--;
    if (g_2f20_log++ < 8) {
        printf("[2f20] retorno=%d flags=%04X estado=%d/%d pendente=%d/%d\n", (int32_t)ctx->r2,
               rdram16(rdram, 0x001A8D88u),
               (int16_t)rdram16(rdram, 0x001A7234u), (int16_t)rdram16(rdram, 0x001A723Cu),
               (int16_t)rdram16(rdram, 0x001A7254u), (int16_t)rdram16(rdram, 0x001A725Cu));
        fflush(stdout);
    }
}

void hle_mesg_report(void) {
    printf("mensagens para fila inicial: %u enviada(s); primeiras %u:\n",
           g_msg_enviadas, g_msg_log_n);
    for (uint32_t i = 0; i < g_msg_log_n; i++) {
        const msg_log_t* m = &g_msg_log[i];
        printf("   pai=func_%08X msg=0x%08X validCount %u -> %u\n",
               m->pai, m->mensagem, m->antes, m->depois);
    }
}

/* Postagem de evento, transcrita de func_800CC8A4 (ROM 0xCD4A4).
 *
 * Chamar a rotina da ROM diretamente nao funciona: ela guarda $ra em $s2 e
 * retorna com `jr $s2`. O N64Recomp nao reconhece isso como retorno e emite um
 * salto indireto, que com um contexto zerado vira uma chamada para 0x00000000.
 * Transcrever e a saida honesta - a logica abaixo e a mesma instrucao por
 * instrucao, so que sem depender do idioma de retorno. */
static int post_event(uint8_t* rdram, int event) {
    uint32_t tab = ADDR_EVENT_STATE_TAB + event * 8;
    uint32_t mq = rd32(rdram, tab);
    if (!mq) {
        if (event_trace_take(event))
            printf("[evt-trace] %-7s sem fila\n", k_event_names[event]);
        return 0;
    }

    uint32_t valid = rd32(rdram, mq + 0x08);
    uint32_t count = rd32(rdram, mq + 0x10);
    if (!count || (int32_t)valid >= (int32_t)count) {
        if (event_trace_take(event))
            printf("[evt-trace] %-7s fila=%08X cheia %u/%u\n",
                   k_event_names[event], mq, valid, count);
        return 0;
    }

    uint32_t first = rd32(rdram, mq + 0x0C);
    uint32_t msgs  = rd32(rdram, mq + 0x14);
    wr32(rdram, msgs + ((first + valid) % count) * 4, rd32(rdram, tab + 4));
    wr32(rdram, mq + 0x08, valid + 1);

    /* Acorda quem estava esperando: a fila termina numa thread falsa cujo
       `next` e zero, entao head->next == 0 significa "ninguem esperando". */
    uint32_t head = rd32(rdram, mq + 0x00);
    if (head && rd32(rdram, head + TH_NEXT) != 0) {
        wr32(rdram, mq + 0x00, rd32(rdram, head + TH_NEXT));
        wr16(rdram, head + TH_STATE, OS_STATE_RUNNABLE);
        enqueue_thread(rdram, ADDR_RUN_QUEUE, head);
        if (g_retraces < 4) {
            printf("  [evt]  evento %s acordou a thread 0x%08X\n",
                   k_event_names[event], head);
            fflush(stdout);
        }
        if (event_trace_take(event)) {
            printf("[evt-trace] %-7s fila=%08X %u->%u acordou=%08X\n",
                   k_event_names[event], mq, valid, valid + 1, head);
            fflush(stdout);
        }
        return 1;
    }
    if (event_trace_take(event)) {
        printf("[evt-trace] %-7s fila=%08X %u->%u sem espera\n",
               k_event_names[event], mq, valid, valid + 1);
        fflush(stdout);
    }
    /* A mensagem foi posta mesmo sem uma thread dormindo nela. O chamador usa
       este retorno para saber se pode consumir uma conclusao pendente do
       dispositivo; ele nao deve confundir "ninguem acordou" com "fila cheia". */
    return 1;
}

/* `post_event` replica osMensg: fila cheia nao descarta um sinal de hardware.
 * O chamador de uma conclusao pendente consulta a capacidade antes de removê-la
 * da fila do dispositivo e tenta de novo no proximo ponto de escalonamento. */
static int event_can_post(uint8_t* rdram, int event) {
    uint32_t tab = ADDR_EVENT_STATE_TAB + event * 8;
    uint32_t mq = rd32(rdram, tab);
    if (!mq) return 1; /* sem destinatario: nada precisa ficar pendente */
    uint32_t valid = rd32(rdram, mq + 0x08);
    uint32_t count = rd32(rdram, mq + 0x10);
    return !count || (int32_t)valid < (int32_t)count;
}

/* A RSP do host conclui a OSTask no instante em que ela e submetida. Isso nao
 * torna a interrupcao SP/DP um retrace: no N64 ela pode chegar entre dois VIs.
 * Antes esta entrega ficava atras do portao de 60 Hz de hle_deliver_events(),
 * criando uma fila artificial de tarefas durante a abertura. A thread do
 * roteiro deixava de voltar ao comando automatico C001 e a transicao parava
 * depois de sete updates. Entregar uma conclusao pendente no proximo POLL
 * preserva a ordem das tarefas sem acelerar contador, VI ou animações. */
/* A lista grafica completa primeiro no RSP e depois no RDP. Ambos os eventos
 * usam a mesma fila neste jogo, mas seus payloads sao diferentes e o gerenciador
 * precisa ve-los em ordem. Mantemos DP pendente para o proximo ponto seguro, em
 * vez de enfileirar SP e DP na mesma troca de contexto. */
static int deliver_rsp_task_done(uint8_t* rdram) {
    if (g_rsp_dp_pending) {
        if (EVENT_ON(OS_EVENT_DP) && !event_can_post(rdram, OS_EVENT_DP))
            return 0;
        g_rsp_dp_pending = 0;
        return EVENT_ON(OS_EVENT_DP) ? post_event(rdram, OS_EVENT_DP) : 0;
    }

    int done = rsp_peek_task_done();
    if (!done) return 0;

    if (EVENT_ON(OS_EVENT_SP) && !event_can_post(rdram, OS_EVENT_SP))
        return 0;

    /* DP so existe para lista grafica (tipo 1) e sera entregue numa chamada
       posterior, depois de a thread ter a chance de observar SP. */
    done = rsp_take_task_done();
    if (done == 1 && EVENT_ON(OS_EVENT_DP)) g_rsp_dp_pending = 1;
    return EVENT_ON(OS_EVENT_SP) ? post_event(rdram, OS_EVENT_SP) : 0;
}

/* Entrega os eventos de hardware pendentes. Chamado de dois lugares: do laco do
   scheduler, quando nao ha nada pronto (que e quando um interrupt chegaria no
   hardware ocioso), e de um ponto de laco, quando o jogo gira sem ceder. */
int hle_deliver_events(uint8_t* rdram) {
    /* O retrace tem uma taxa: 60 Hz. Sem esse portao o laco ocioso do scheduler
       entrega interrupcoes o mais rapido que o host consegue, e o jogo roda
       centenas de vezes acelerado - o que nao e so feio, e falso: temporizacao
       baseada em contagem de frames passa a medir outra coisa. */
    if (g_poll_freq.QuadPart == 0) {
        LARGE_INTEGER initial;
        QueryPerformanceFrequency(&g_poll_freq);
        QueryPerformanceCounter(&initial);
        g_poll_deadline = (double)initial.QuadPart
                        + (double)g_poll_freq.QuadPart * RETRACE_PERIOD_S;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double restante_ticks = g_poll_deadline - (double)now.QuadPart;
    /* SP/DP nao esperam o proximo retrace; vide deliver_rsp_task_done acima. */
    int rsp_woke = deliver_rsp_task_done(rdram);
    if (restante_ticks > 0.0) {
        /* A resolucao foi fixada em 1 ms no inicio. Dormir apenas o inteiro
         * estritamente anterior ao prazo evita uma espera extra de 15,6 ms e
         * deixa o ultimo milissegundo para o proximo poll cooperativo. */
        double restante_ms = restante_ticks * 1000.0 / (double)g_poll_freq.QuadPart;
        if (restante_ms > 1.5) Sleep((DWORD)restante_ms - 1u);
        else Sleep(0);
        return rsp_woke;
    }
    /* Preserve sempre o prazo absoluto. A versao anterior reiniciava o
     * relogio quando um quadro excedia dois VIs; cada pico eliminava VIs da
     * linha do tempo e a cutscene terminava lentamente. Aqui um atraso gera,
     * no maximo, alguns polls cooperativos sem sono ate o prazo ser alcancado;
     * cada um ainda entrega um unico VI, preservando a ordem das mensagens. */
    g_poll_deadline += (double)g_poll_freq.QuadPart * RETRACE_PERIOD_S;

    if (!g_table_dumped) {
        g_table_dumped = 1;
        dump_event_table(rdram);
    }
    if (EVENT_ON(OS_EVENT_VI)) post_event(rdram, OS_EVENT_VI);
    if (EVENT_ON(OS_EVENT_PI)) post_event(rdram, OS_EVENT_PI);
    /* COUNTER nasce ao atingir COP0 Compare. O fallback historico entrega em
       todo VI; a rota opt-in usa o mesmo contador que osGetCount e preserva
       timers de audio que nao sao multiplos exatos de um retrace. */
    if (EVENT_ON(OS_EVENT_COUNTER)) {
        if (!hle_counter_compare_mode()) {
            post_event(rdram, OS_EVENT_COUNTER);
        } else if (g_counter_compare_armed &&
                   (int32_t)(sched_count_now() - g_counter_compare) >= 0 &&
                   post_event(rdram, OS_EVENT_COUNTER)) {
            g_counter_compare_armed = 0;
        }
    }
    /* SI e diferente do retrace: a interrupcao existe somente quando um
       __osSiRawStartDma acabou. Antes ela era postada a cada quadro e enchia a
       fila de oito mensagens do jogo; a leitura seguinte do controle entao
       ficava sem sua propria conclusao. O PIF minimo conclui a transferencia
       imediatamente, mas a notificacao ainda e entregue aqui, no proximo ponto
       seguro de escalonamento. */
    /* Drenar, nao entregar uma por passagem. Uma leitura de controle sao DUAS
     * operacoes SI (dir=1 monta e processa a fita, dir=0 devolve as respostas)
     * e cada uma conclui de imediato. Com um `if`, as conclusoes se acumulavam
     * e escoavam a uma por retrace: medimos ~10 leituras/s com retrace de 60 Hz
     * e ~20 com 120 Hz - seis retraces por leitura nos dois casos, ou seja a
     * latencia vinha daqui, nao do jogo.
     *
     * Isto nao repete o defeito que o comentario acima descreve: la o evento
     * era postado a cada quadro, inventando conclusoes que nenhuma
     * transferencia produziu. Aqui cada evento continua correspondendo a uma
     * transferencia real ja concluida - so paramos de segura-las na fila.
     *
     * O limite evita que um acumulo grande sature de uma vez a fila de oito
     * mensagens do jogo. */
    if (EVENT_ON(OS_EVENT_SI)) {
        for (int n = 0; n < 8 && pif_si_done_pending(); n++) {
            if (!post_event(rdram, OS_EVENT_SI)) break;
            pif_take_si_done();
        }
    }
    /* A conclusao AI e determinada pela duracao PCM do DMA primario, e nao
       por um retrace arbitrario. Isso reproduz BUSY/FIFO do hardware e evita
       que a thread de audio avance quase duas vezes cedo demais. */
    if (EVENT_ON(OS_EVENT_AI) && audio_ai_done_pending()
        && post_event(rdram, OS_EVENT_AI)) {
        audio_take_ai_done();
    }
    /* O post incondicional de SP e DP fica de fora, e a decisao e deliberada.
     *
     * Testado: entregar conclusao de tarefa do RSP e do RDP destrava a thread
     * que espera o quadro, mas ela morre em seguida com falha de acesso - ela
     * passa a ler o resultado de uma tarefa que nunca rodou. A cobertura caiu de
     * 112 para 109 funcoes. Anunciar "terminou" para trabalho que nao aconteceu
     * nao adianta um port; so troca um bloqueio visivel por uma corrupcao
     * silenciosa. Estes dois eventos so sao entregues por
     * deliver_rsp_task_done(), quando existe uma tarefa real terminada. */
    g_retraces++;
    status_note(rdram);
    return 1;
}

void recomp_poll(void) {
    g_poll_budget = POLL_BUDGET;
    g_polls++;
    if (g_in_poll) return;              /* nao interrompemos a propria entrega */

    uint8_t* rdram = g_rdram;
    /* No modo sem timeout, fechar a janela e o encerramento normal. Fazemos a
     * limpeza aqui, em ponto cooperativo, para o cabeçalho WAV ser finalizado
     * antes de terminar o processo. */
    if (video_quit_requested()) {
        audio_shutdown();
        video_shutdown();
        hle_clock_shutdown();
        ExitProcess(0);
    }

    if (!g_force_state2_done && g_force_state2_after &&
        g_polls >= g_force_state2_after) {
        /* 0x800E8CF0 e a global conferida nos dumps reais do Project64. */
        wr32(rdram, 0x800E8CF0u, 2);
        g_force_state2_done = 1;
        printf("[diagnostico] estado forcado 1 -> 2 no poll %llu\n",
               (unsigned long long)g_polls);
        fflush(stdout);
    }
    if (!g_force_active_done && g_force_active_after &&
        g_polls >= g_force_active_after) {
        /* `func_80021ED0` deveria escrever este indice antes de BA9D4. */
        wr32(rdram, 0x801AE838u, (uint32_t)g_force_active_index);
        g_force_active_done = 1;
        printf("[diagnostico] indice ativo forcado para %d no poll %llu\n",
               g_force_active_index, (unsigned long long)g_polls);
        fflush(stdout);
    }

    /* O oraculo nativo sai de 12/50 para 8/13. Este gatilho existe somente
       para testar o carregamento posterior enquanto a notificacao automatica
       que faz essa troca ainda nao foi reproduzida pelo runtime. */
    if (g_force_next_scene && !g_force_next_scene_done &&
        (int16_t)rdram16(rdram, 0x001A7234u) == 12 &&
        (int16_t)rdram16(rdram, 0x001A723Cu) == 50) {
        wr16(rdram, 0x801A7234u, 8);
        wr16(rdram, 0x801A723Cu, 13);
        wr16(rdram, 0x801A7254u, 8);
        wr16(rdram, 0x801A725Cu, 13);
        g_force_next_scene_done = 1;
        printf("[diagnostico] estado 12/50 -> 8/13 aplicado; rota nativa seguinte\n");
        fflush(stdout);
    }

    /* O oraculo nativo mostra a cena do tunel/dialogo mantendo 8/1 por
       centenas de tasks graficos. Esta trava e somente uma lente diagnostica:
       ela e armada apos a ROM alcancar 8/1 sozinha, e evita que a cadencia
       acelerada de teste atravesse a cena antes de podermos renderiza-la. */
    if (g_hold_state_8_1) {
        int16_t estado = (int16_t)rdram16(rdram, 0x001A7234u);
        int16_t sub = (int16_t)rdram16(rdram, 0x001A723Cu);
        if (estado == 8 && sub == 1) g_hold_state_8_1_armed = 1;
        if (g_hold_state_8_1_armed && (estado != 8 || sub != 1)) {
            wr16(rdram, 0x801A7234u, 8); wr16(rdram, 0x801A723Cu, 1);
            wr16(rdram, 0x801A7254u, 8); wr16(rdram, 0x801A725Cu, 1);
            if (!g_hold_state_8_1_reported) {
                printf("[diagnostico] retencao da cena nativa 8/1 ativada\n");
                fflush(stdout);
                g_hold_state_8_1_reported = 1;
            }
        }
    }

    /* A observacao passiva do Project64 corrige a classificacao anterior:
       8/1 e a abertura 2D, 8/26 e a transicao, e a primeira lista do corredor
       3D aparece em 12/50 (task grafico ~1444). Portanto a retencao util para
       o prototipo e armada somente quando a ROM chega a 12/50 pelos proprios
       eventos; ela nunca pula recursos, camera ou dialogo. */
    if (g_hold_state_12_50) {
        int16_t estado = (int16_t)rdram16(rdram, 0x001A7234u);
        int16_t sub = (int16_t)rdram16(rdram, 0x001A723Cu);
        if (estado == 12 && sub == 50) g_hold_state_12_50_armed = 1;
        if (g_hold_state_12_50_armed && (estado != 12 || sub != 50)) {
            wr16(rdram, 0x801A7234u, 12); wr16(rdram, 0x801A723Cu, 50);
            wr16(rdram, 0x801A7254u, 12); wr16(rdram, 0x801A725Cu, 50);
            if (!g_hold_state_12_50_reported) {
                printf("[diagnostico] retencao da cena nativa 12/50 ativada\n");
                fflush(stdout);
                g_hold_state_12_50_reported = 1;
            }
        }
    }

    /* Antes de o SO subir nao ha nada a preemptar: o laco de limpeza de memoria
       do proprio entrypoint sao ~98 mil iteracoes, e ceder ali so faz o
       scheduler girar em falso ate desistir. Interromper so faz sentido quando
       existe uma thread corrente. */
    if (rd32(rdram, ADDR_RUNNING_THREAD) == 0) return;

    g_in_poll = 1;
    /* Uma interrupcao de CPU so existe quando o periodo do retrace venceu e
     * um evento foi postado. Preemptar tambem nos polls que apenas dormiram
     * por 1 ms fragmentava a thread do jogo milhares de vezes entre dois
     * quadros; alem de custar desempenho, isso quebra a cadencia das rotinas
     * que o titulo espera receber a cada VI. */
    int retrace = hle_deliver_events(rdram);
    if (retrace || g_preempt_every_poll) {
        /* O VI comeca a varrer o framebuffer ja escolhido no retrace anterior.
         * Copia-lo antes de acordar as threads da ROM evita que varias listas
         * SP/DP submetidas no mesmo intervalo sejam vistas como uma composicao
         * impossivel (cidade 2D sobre o corredor 3D). A nova origem sera
         * preparada pelo handler e aparecera no retrace seguinte, como no
         * double-buffering do N64. */
        if (retrace) video_present_pos_retrace(rdram);
        sched_preempt(rdram);
    }
    g_in_poll = 0;
}
