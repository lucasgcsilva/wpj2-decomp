/* Scheduler cooperativo e as funcoes de libultra substituidas nativamente.
 *
 * No codigo recompilado o "contexto" de uma thread do N64 e a propria pilha de
 * chamadas do host. Entao cada OSThread vira um fiber do Windows, e trocar de
 * thread e trocar de fiber. O contexto salvo na RDRAM so e lido uma vez, para
 * *criar* o fiber (pc, sp, gp, a0, ra); dali em diante o fiber preserva tudo.
 *
 * Existe um fiber dedicado a escolha da proxima thread, e ele nunca termina.
 * Essa parte nao e detalhe: a primeira versao devolvia o controle "a quem
 * despachou", e quando essa thread ja tinha acabado o controle voltava para um
 * fiber morto - o retorno da rotina de um fiber encerra a *thread do sistema*,
 * e o processo ficava vivo so por causa do watchdog, sem executar nada. Um
 * ponto fixo que sempre existe elimina a classe inteira de erro.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "runtime.h"
#include "funcs.h"
#include "wpj2_os.h"

#define MAX_THREADS      32
#define MAX_IDLE_ROUNDS  600      /* ~10 s de retraces a 60 Hz sem nada pronto */

typedef struct {
    uint32_t thread;
    uint32_t id;
    uint32_t entry_pc;
    void*    fiber;
    recomp_context ctx;
    int      started;
    int      finished;
    uint32_t yield_site;      /* funcao de onde a thread cedeu pela ultima vez */
    uint32_t parked_on;       /* fila em que __osEnqueueThread a colocou */
    uint64_t yields;
} sched_slot_t;

static sched_slot_t g_slots[MAX_THREADS];
static void* g_main_fiber = NULL;
static void* g_sched_fiber = NULL;
static uint32_t g_current = 0;
static uint64_t g_switches = 0;
static uint64_t g_dispatch_calls = 0;
static uint64_t g_empty_dispatch = 0;
static uint64_t g_idle_rounds = 0;
static uint64_t g_yield_requeued = 0;
static int g_verbose = 1;
static uint64_t g_pause_self_calls = 0;

uint64_t sched_yield_requeued(void) { return g_yield_requeued; }

uint64_t sched_switches(void)       { return g_switches; }
uint64_t sched_dispatch_calls(void) { return g_dispatch_calls; }
uint64_t sched_empty_dispatch(void) { return g_empty_dispatch; }
uint32_t sched_current(void) {
    if (g_current) return g_current;
    /* Normalmente g_current e definido pelo scheduler antes do SwitchToFiber.
       Durante uma chamada indireta de limpeza, porem, ele pode ter sido limpo
       pelo retorno de outro despacho mesmo que o fiber do jogo ainda esteja
       executando. O proprio fiber e a fonte de verdade nesse caso. */
    void* atual = GetCurrentFiber();
    for (int i = 0; i < MAX_THREADS; i++)
        if (g_slots[i].started && g_slots[i].fiber == atual)
            return g_slots[i].thread;
    return 0;
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

/* __osEnqueueThread: insercao ordenada por prioridade. O ponteiro de cabeca e
   tratado como o campo `next` de um no falso, o que so funciona porque `next`
   esta no deslocamento 0 - e e assim que a propria libultra faz. */
static void enqueue_by_priority(uint8_t* rdram, uint32_t queue_addr, uint32_t thread) {
    int32_t pri = (int32_t)rd32(rdram, thread + TH_PRIORITY);
    uint32_t pred = queue_addr;
    uint32_t cur = rd32(rdram, queue_addr);
    while (cur && pri <= (int32_t)rd32(rdram, cur + TH_PRIORITY)) {
        pred = cur;
        cur = rd32(rdram, cur + TH_NEXT);
    }
    wr32(rdram, thread + TH_NEXT, cur);
    wr32(rdram, pred + TH_NEXT, thread);
}

static sched_slot_t* slot_for(uint32_t thread) {
    sched_slot_t* free_slot = NULL;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_slots[i].started && g_slots[i].thread == thread) return &g_slots[i];
        if (!free_slot && !g_slots[i].started) free_slot = &g_slots[i];
    }
    return free_slot;
}

/* Depois da primeira troca de contexto, o fiber retem a pilha e os
 * registradores reais da chamada C. O campo PC da estrutura OSThread serve
 * para criar esse primeiro fiber, mas algumas rotas de __osDispatchThread o
 * deixam momentaneamente em zero. Rejeitar por isso uma thread ja iniciada
 * deixa uma entrada valida na fila de execucao sem nunca mais seleciona-la. */
static sched_slot_t* started_slot_for(uint32_t thread) {
    for (int i = 0; i < MAX_THREADS; i++)
        if (g_slots[i].started && g_slots[i].thread == thread)
            return &g_slots[i];
    return NULL;
}

/* Traduz o endereco de host que falhou de volta para o endereco que o codigo do
 * jogo tentou tocar.
 *
 * O acesso gerado e `rdram + ((reg + off) - 0xFFFFFFFF80000000)`. Invertendo a
 * conta a partir do endereco que o Windows reporta, sai o proprio `reg + off` —
 * e e isso que interessa. Sem essa inversao, um ponteiro nulo do jogo aparece no
 * log como um endereco de host gigante "fora do mapa", que nao sugere nada.
 */
static void print_fault_target(ULONG_PTR addr) {
    ULONG_PTR base = (ULONG_PTR)g_rdram;
    if (addr >= base && addr < base + 0x40000000ull) {
        printf(" ao tocar 0x%08X", (uint32_t)(0x80000000u + (addr - base)));
        return;
    }
    uint64_t target = (uint64_t)(addr - base) + 0xFFFFFFFF80000000ull;
    if (target < 0x10000ull) {
        printf(" ao seguir um ponteiro nulo (offset +0x%llX)",
               (unsigned long long)target);
    } else if ((target >> 32) == 0xFFFFFFFFull || (target >> 32) == 0) {
        printf(" ao tocar 0x%08X, fora da RDRAM", (uint32_t)target);
    } else {
        printf(" com um registrador que nao e endereco: 0x%016llX",
               (unsigned long long)target);
    }
}

static LONG thread_fault_filter(EXCEPTION_POINTERS* ep, sched_slot_t* s) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    ULONG_PTR addr = ep->ExceptionRecord->NumberParameters >= 2
                   ? ep->ExceptionRecord->ExceptionInformation[1] : 0;
    ULONG_PTR kind = ep->ExceptionRecord->NumberParameters >= 1
                   ? ep->ExceptionRecord->ExceptionInformation[0] : 0;

    /* Sem o endereco da instrucao nao da para saber se a falha esta no codigo
       gerado ou no runtime, e as duas hipoteses levam a investigacoes
       completamente diferentes. */
    /* O endereco absoluto nao diz nada com ASLR; o deslocamento dentro do modulo
       casa direto com o arquivo .map gerado no link. */
    ULONG_PTR mod = (ULONG_PTR)GetModuleHandleW(NULL);
    printf("\n  [falha] instrucao em %p (RVA 0x%llX), %s, base da RDRAM %p\n",
           ep->ExceptionRecord->ExceptionAddress,
           (unsigned long long)((ULONG_PTR)ep->ExceptionRecord->ExceptionAddress - mod),
           kind == 0 ? "leitura" : kind == 1 ? "escrita" : "execucao",
           (void*)g_rdram);
    printf("  [falha] contexto: a0=0x%08X a1=0x%08X sp=0x%08X ra=0x%08X\n",
           (uint32_t)s->ctx.r4, (uint32_t)s->ctx.r5,
           (uint32_t)s->ctx.r29, (uint32_t)s->ctx.r31);
    printf("  [falha] thread id=%u morreu com 0x%08lX", s->id, code);
    if (addr) {
        print_fault_target(addr);
    }
    printf("\n");
    trace_trail("caminho ate a falha");
    return EXCEPTION_EXECUTE_HANDLER;
}

static void CALLBACK thread_entry(void* param) {
    sched_slot_t* s = (sched_slot_t*)param;
    uint8_t* rdram = g_rdram;
    uint32_t th = s->thread;

    /* Contexto salvo e de 64 bits big-endian; ponteiros na metade baixa. */
    ctx_init(&s->ctx);
    s->ctx.r4  = (gpr)(int32_t)rd32(rdram, th + TH_CTX_A0 + LO_WORD);
    s->ctx.r28 = (gpr)(int32_t)rd32(rdram, th + TH_CTX_GP + LO_WORD);
    s->ctx.r29 = (gpr)(int32_t)rd32(rdram, th + TH_CTX_SP + LO_WORD);
    s->ctx.r30 = (gpr)(int32_t)rd32(rdram, th + TH_CTX_S8 + LO_WORD);
    s->ctx.r31 = (gpr)(int32_t)rd32(rdram, th + TH_CTX_RA + LO_WORD);
    s->ctx.status_reg = rd32(rdram, th + TH_CTX_SR);

    recomp_func_t* f = find_function(s->entry_pc);
    printf("  [sched] thread id=%u entra em 0x%08X (sp 0x%08X)%s\n",
           s->id, s->entry_pc, (uint32_t)s->ctx.r29, f ? "" : "  <- sem funcao!");
    fflush(stdout);

    /* Cada fiber precisa da propria protecao. O __try do main so cobre a pilha
       do fiber principal: uma falha dentro de uma thread do jogo matava o
       processo em silencio, sem relatorio e sem trilha - foi o que aconteceu
       assim que o jogo comecou a inicializar o audio. */
    if (f) {
        __try {
            f(rdram, &s->ctx);
        }
        __except (thread_fault_filter(GetExceptionInformation(), s)) {
            s->finished = 1;
            SwitchToFiber(g_sched_fiber);
        }
    }

    /* Uma thread que retorna cedo e sempre suspeita: o caminho que a levou ate
       ali diz mais do que o fato de ter acabado. */
    printf("  [sched] thread id=%u retornou de 0x%08X\n", s->id, s->entry_pc);
    trace_trail("antes de retornar");

    /* No hardware a funcao de entrada nao "retorna": ela cai no $ra que
       osCreateThread gravou, que e __osCleanupThread. */
    uint32_t ra = rd32(rdram, th + TH_CTX_RA + LO_WORD);
    recomp_func_t* cleanup = ra ? find_function(ra) : NULL;
    if (cleanup) {
        /* O retorno do entrypoint ainda ocorre no contexto desta OSThread.
           No modelo por fibers, entretanto, uma preempcao anterior pode ter
           deixado o espelho em RDRAM zerado quando a pilha C volta aqui. A
           limpeza da propria libultra (800CCC60 -> 800CB840) chama
           osDestroyThread(NULL) e busca exatamente esse ponteiro; no N64 ele
           necessariamente e a thread que esta retornando. Repor a referencia
           antes de executar o corpo recompilado conserva a remocao original
           da fila, em vez de pular ou simular a limpeza. */
        g_current = th;
        wr32(rdram, ADDR_RUNNING_THREAD, th);
        cleanup(rdram, &s->ctx);
    }

    s->finished = 1;
    printf("  [sched] thread id=%u terminou\n", s->id);
    fflush(stdout);
    SwitchToFiber(g_sched_fiber);      /* ponto fixo: sempre existe */
}

/* ------------------------------------------------------------------ */
/* O fiber que escolhe a proxima thread                                */
/* ------------------------------------------------------------------ */

static void CALLBACK scheduler_loop(void* param) {
    (void)param;
    uint8_t* rdram = g_rdram;

    for (;;) {
        uint32_t head = rd32(rdram, ADDR_RUN_QUEUE);
        /* A libultra fecha a fila com uma thread falsa de prioridade -1. */
        sched_slot_t* started = head ? started_slot_for(head) : NULL;
        int runnable = head != 0
                    && (int32_t)rd32(rdram, head + TH_PRIORITY) >= 0
                    && (rd32(rdram, head + TH_CTX_PC) != 0 ||
                        (started && !started->finished));
        if (!runnable) {
            g_empty_dispatch++;
            /* No hardware nao existe retorno ao boot quando todas as threads
               dormem: o CPU fica ocioso ate a proxima interrupcao. Voltar ao
               fiber principal apos dez segundos fazia a instrucao seguinte ao
               __osDispatchThread continuar fora do contexto salvo, justamente
               durante as trocas automaticas da abertura. */
            g_idle_rounds += hle_deliver_events(rdram);
            if (g_idle_rounds == MAX_IDLE_ROUNDS) {
                printf("  [sched] fila vazia por %d retraces; permanecendo ocioso\n",
                       MAX_IDLE_ROUNDS);
                fflush(stdout);
            }
            continue;
        }

        g_idle_rounds = 0;
        wr32(rdram, ADDR_RUN_QUEUE, rd32(rdram, head + TH_NEXT));
        wr32(rdram, ADDR_RUNNING_THREAD, head);
        wr16(rdram, head + TH_STATE, OS_STATE_RUNNING);

        sched_slot_t* s = slot_for(head);
        if (!s) {
            printf("  [sched] sem espaco para mais threads (limite %d)\n", MAX_THREADS);
            fflush(stdout);
            SwitchToFiber(g_main_fiber);
            continue;
        }
        if (s->finished) continue;      /* thread morta que voltou para a fila */

        if (!s->started) {
            s->thread   = head;
            s->id       = rd32(rdram, head + TH_ID);
            s->entry_pc = rd32(rdram, head + TH_CTX_PC);
            s->started  = 1;
            /* 1 MB por fiber: o codigo do jogo aninha fundo e uma pilha curta
               vira falha de acesso que parece bug de traducao. */
            s->fiber    = CreateFiber(1024 * 1024, thread_entry, s);
            if (!s->fiber) {
                printf("  [sched] CreateFiber falhou (%lu)\n", GetLastError());
                fflush(stdout);
                SwitchToFiber(g_main_fiber);
                continue;
            }
            if (g_verbose) {
                printf("  [sched] nova thread id=%u pri=%d pc=0x%08X\n", s->id,
                       (int32_t)rd32(rdram, head + TH_PRIORITY), s->entry_pc);
                fflush(stdout);
            }
        }

        g_current = head;
        g_switches++;
        SwitchToFiber(s->fiber);
        g_current = 0;
    }
}

void sched_init(void) {
    g_main_fiber = ConvertThreadToFiber(NULL);
    if (!g_main_fiber) {
        printf("  [sched] ConvertThreadToFiber falhou (%lu)\n", GetLastError());
        return;
    }
    g_sched_fiber = CreateFiber(256 * 1024, scheduler_loop, NULL);
    if (!g_sched_fiber) {
        printf("  [sched] CreateFiber (scheduler) falhou (%lu)\n", GetLastError());
    }
}

/* __osEnqueueThread (func_800CCA8C), $a0 = fila, $a1 = thread.
 *
 * Substituida nao por necessidade, e sim por informacao: e o unico ponto onde
 * se sabe *em qual fila* uma thread esta sendo estacionada. Sem isso, uma thread
 * bloqueada e so um estado ESPERANDO sem contexto nenhum. A logica e a mesma da
 * libultra - insercao ordenada por prioridade com a cabeca tratada como no. */
void func_800CCA8C(uint8_t* rdram, recomp_context* ctx) {
    uint32_t queue  = (uint32_t)ctx->r4;
    uint32_t thread = (uint32_t)ctx->r5;
    if (!queue || !thread) return;
    enqueue_by_priority(rdram, queue, thread);
    /* A lista encadeada so e metade do contrato de __osEnqueueThread: a
       rotina original tambem grava a fila no proprio OSThread (delay-slot de
       seu jr $ra). osStopThread/osDestroyThread usam esse campo depois para
       remover a thread. Sem ele, a primeira limpeza de uma thread bloqueada
       tenta desreferenciar NULL, embora ela esteja corretamente na lista. */
    wr32(rdram, thread + TH_QUEUE, queue);
    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_slots[i].started && g_slots[i].thread == thread) {
            g_slots[i].parked_on = queue;
            break;
        }
    }
}

/* N64Recomp emite pause_self() para um salto para si proprio.  Nao e um
 * yield normal: no MIPS a instrucao seguinte e inalcançavel.  Estacionar a
 * thread e devolver o controle ao scheduler preserva esse significado sem
 * permitir que o C caia no bloco que vem depois do laco. */
void sched_pause_current(uint8_t* rdram) {
    if (!g_current || !g_sched_fiber) return;

    uint32_t current = g_current;
    if (g_pause_self_calls++ < 32) {
        printf("  [pause-self] thread=0x%08X func=0x%08X\\n",
               current, trace_last_func());
    }
    wr16(rdram, current + TH_STATE, OS_STATE_WAITING);
    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_slots[i].started && g_slots[i].thread == current) {
            g_slots[i].yield_site = trace_last_func();
            g_slots[i].yields++;
            g_slots[i].parked_on = 0;
            break;
        }
    }
    for (;;) SwitchToFiber(g_sched_fiber);
}

/* A entrada de uma OSThread termina em __osCleanupThread (800CCC60). No ROM
 * essa rotina nao possui `jr $ra`: depois de retirar a thread das filas, ela
 * nunca pode voltar para a pilha que acabou de terminar. O C recompilado
 * naturalmente retornava, ressuscitando o fiber encerrado e repetindo a
 * limpeza. Encerramos o slot e transferimos de modo permanente ao scheduler. */
void sched_terminate_current(uint8_t* rdram) {
    (void)rdram;
    uint32_t current = sched_current();
    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_slots[i].started && g_slots[i].thread == current) {
            g_slots[i].finished = 1;
            g_slots[i].parked_on = 0;
            break;
        }
    }
    g_current = 0;
    if (g_sched_fiber && GetCurrentFiber() != g_sched_fiber) {
        for (;;) SwitchToFiber(g_sched_fiber);
    }
}

/* __osDispatchThread (func_800CCAE4). No hardware nunca retorna: quem chama ja
   colocou a thread corrente na fila certa - de execucao ou de espera. Aqui basta
   ceder ao scheduler; se ele reescolher esta mesma thread, o SwitchToFiber
   retorna e a execucao segue de dentro do proprio yield. */
void func_800CCAE4(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    g_dispatch_calls++;
    if (!g_sched_fiber || GetCurrentFiber() == g_sched_fiber) return;

    /* Onde a thread cedeu e a pergunta que importa quando ela nunca mais volta:
       o nome da funcao que chamou o despachante diz se ela esperava mensagem,
       DMA ou retrace. */
    if (g_current) {
        for (int i = 0; i < MAX_THREADS; i++) {
            if (g_slots[i].started && g_slots[i].thread == g_current) {
                g_slots[i].yield_site = trace_last_func();
                g_slots[i].yields++;
                if (g_verbose && g_dispatch_calls <= 24) {
                    printf("  [yield] thread id=%u (0x%08X) cedeu em func_%08X;"
                           " estado=0x%X fila=0x%08X\n",
                           g_slots[i].id, g_current, g_slots[i].yield_site,
                           (unsigned)MEM_HU(0, (gpr)(int32_t)(g_current + TH_STATE)),
                           rd32(rdram, ADDR_RUN_QUEUE));
                    fflush(stdout);
                }
                break;
            }
        }

        /* Uma thread que chega aqui ainda com estado RODANDO nao foi colocada em
           nenhuma fila por quem chamou - nem de espera, nem de execucao. Do
           ponto de vista da libultra ela continua executavel; e a semantica de
           osYieldThread. Sem devolve-la a fila de execucao ela some: foi
           exatamente o que aconteceu com a thread id=3, que ficava RODANDO para
           sempre sem nunca mais ser escolhida. */
        uint16_t st = MEM_HU(0, (gpr)(int32_t)(g_current + TH_STATE));
        if (st == OS_STATE_RUNNING) {
            wr16(rdram, g_current + TH_STATE, OS_STATE_RUNNABLE);
            enqueue_by_priority(rdram, ADDR_RUN_QUEUE, g_current);
            g_yield_requeued++;
        }
    }
    SwitchToFiber(g_sched_fiber);
}

/* Fim de __osException: devolve a thread interrompida para a fila de execucao e
   cede. Sem o reenfileiramento ela seria perdida no primeiro interrupt. */
void sched_preempt(uint8_t* rdram) {
    if (!g_sched_fiber || GetCurrentFiber() == g_sched_fiber) return;
    /* O fiber em execucao e a fonte de verdade. __osRunningThread e apenas um
       espelho na RDRAM e pode ainda apontar para a thread anterior durante o
       retorno de uma espera; preemptar esse espelho deixa o fiber real fora da
       fila, marcado PRONTO mas impossivel de despachar. */
    uint32_t cur = sched_current();
    if (cur) wr32(rdram, ADDR_RUNNING_THREAD, cur);
    /* Depois da primeira troca, a pilha/registradores vivem no fiber. O PC da
       estrutura OSThread pode estar transitoriamente em zero enquanto a thread
       ja iniciada executa; descartá-la nesse ponto a remove da fila para sempre
       e o resultado depende do instante exato do retrace do host. */
    sched_slot_t* started = cur ? started_slot_for(cur) : NULL;
    uint16_t state = cur ? (uint16_t)MEM_HU(0, (gpr)(int32_t)(cur + TH_STATE)) : 0;
    /* Uma rotina de espera pode mudar TH_STATE para ESPERANDO pouco antes de
       atingir um ponto de poll. Nesse caso a troca ja pertence ao scheduler;
       reenfileirar aqui ressuscita a thread (frequentemente a de VI, prioridade
       254), impede o gerenciador de tarefas de consumir SP/DP e enche a fila. */
    if (cur && state == OS_STATE_RUNNING
            && (int32_t)rd32(rdram, cur + TH_PRIORITY) >= 0
            && (rd32(rdram, cur + TH_CTX_PC) != 0 ||
                (started && !started->finished))) {
        wr16(rdram, cur + TH_STATE, OS_STATE_RUNNABLE);
        recomp_context c;
        ctx_init(&c);
        c.r29 = (gpr)(int32_t)0x803E0000;
        c.r4  = (gpr)(int32_t)ADDR_RUN_QUEUE;
        c.r5  = (gpr)(int32_t)cur;
        func_800CCA8C(rdram, &c);      /* __osEnqueueThread, da propria ROM */
    }
    SwitchToFiber(g_sched_fiber);
}

/* ------------------------------------------------------------------ */
/* osGetCount (func_800CBBB0)                                          */
/* ------------------------------------------------------------------ */

/* O contador do COP0 anda a metade do clock do CPU: 93,75 MHz / 2. Um laco que
   compare dois osGetCount precisa ver o valor crescer, e crescer numa taxa
   plausivel - senao esperas viram travamento ou passam instantaneamente. */
#define N64_COUNT_HZ 46875000.0

static LARGE_INTEGER g_qpc_freq, g_qpc_start;
static uint64_t g_count_calls = 0;
static int g_count_vi_clock = -1;

uint64_t sched_count_calls(void) { return g_count_calls; }

uint32_t sched_count_now(void) {
    if (g_count_vi_clock < 0) {
        const char* e = getenv("WPJ2_COUNT_VI_CLOCK");
        g_count_vi_clock = e && *e && *e != '0';
    }
    /* Para validar timers de gameplay, o Count pode seguir o tempo emulado
     * (VI), e nao o tempo que o host gastou renderizando um quadro. */
    if (g_count_vi_clock) {
        return (uint32_t)(hle_retraces() * 781250ull); /* 46.875 MHz / 60 */
    }
    if (g_qpc_freq.QuadPart == 0) {
        QueryPerformanceFrequency(&g_qpc_freq);
        QueryPerformanceCounter(&g_qpc_start);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double seconds = (double)(now.QuadPart - g_qpc_start.QuadPart)
                   / (double)g_qpc_freq.QuadPart;
    return (uint32_t)(seconds * N64_COUNT_HZ);
}

void func_800CBBB0(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    g_count_calls++;
    ctx->r2 = (gpr)(int32_t)sched_count_now();
}


/* ------------------------------------------------------------------ */
/* Situacao de cada thread no fim da execucao                          */
/* ------------------------------------------------------------------ */

static const char* state_name(uint16_t s) {
    switch (s) {
        case OS_STATE_STOPPED:  return "PARADA";
        case OS_STATE_RUNNABLE: return "PRONTA";
        case OS_STATE_RUNNING:  return "RODANDO";
        case OS_STATE_WAITING:  return "ESPERANDO";
        default:                return "?";
    }
}

void sched_report(uint8_t* rdram) {
    uint32_t run = rd32(rdram, ADDR_RUN_QUEUE);
    printf("threads (fila de execucao=0x%08X):\n", run);
    if (run) {
        sched_slot_t* head_slot = started_slot_for(run);
        printf("   cabeca: pri=%d pc=%08X estado=%s fiber=%s%s\n",
               (int32_t)rd32(rdram, run + TH_PRIORITY),
               rd32(rdram, run + TH_CTX_PC),
               state_name((uint16_t)MEM_HU(0, (gpr)(int32_t)(run + TH_STATE))),
               head_slot ? "existe" : "novo",
               head_slot && head_slot->finished ? "/terminou" : "");
    }
    int any = 0;
    for (int i = 0; i < MAX_THREADS; i++) {
        sched_slot_t* s = &g_slots[i];
        if (!s->started) continue;
        any = 1;
        uint16_t st = (uint16_t)MEM_HU(0, (gpr)(int32_t)(s->thread + TH_STATE));
        printf("   id=%-3u 0x%08X pri=%-4d entrada=0x%08X  %-9s fila=%08X  cedeu %llu x,"
               " ultima vez em func_%08X\n",
               s->id, s->thread, (int32_t)rd32(rdram, s->thread + TH_PRIORITY),
               s->entry_pc, s->finished ? "TERMINOU" : state_name(st),
               rd32(rdram, s->thread + TH_QUEUE),
               (unsigned long long)s->yields, s->yield_site);
        if (!s->finished && st == OS_STATE_WAITING && s->parked_on) {
            uint32_t mq = s->parked_on;   /* mtqueue esta no deslocamento 0 */
            printf("        parada na fila 0x%08X  validCount=%u msgCount=%u\n",
                   mq, rd32(rdram, mq + 0x08), rd32(rdram, mq + 0x10));
        }
    }
    if (!any) printf("   (nenhuma)\n");
}
