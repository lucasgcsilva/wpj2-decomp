/* Scheduler serializavel do Wonder Project J2.
 *
 * Diferente de sched.c, nenhuma continuacao vive numa fiber do Windows. Cada
 * OSThread guarda contexto MIPS e a cadeia de callsites recompilados em dados
 * comuns, que podem ser gravados e reconstruidos em outro processo. Este
 * arquivo entra somente no build RECOMP_STATEFUL ate substituir com seguranca
 * o scheduler historico.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"
#include "funcs.h"
#include "wpj2_os.h"
#include "stateful_thread.h"
#include "rt64_backend.h"

#define MAX_THREADS 32
#define MAX_IDLE_ROUNDS 600
#define SNAPSHOT_MAGIC 0x57505353u /* WPSS */
#define SNAPSHOT_VERSION 6u
#define SNAPSHOT_RDRAM_SIZE (8u * 1024u * 1024u)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t rdram_size;
    uint32_t slot_count;
    uint32_t payload_hash;
    uint64_t switches;
    uint64_t dispatch_calls;
    uint64_t empty_dispatch;
    uint64_t yield_requeued;
} snapshot_header_t;

typedef struct {
    uint32_t used;
    uint32_t finished;
    uint32_t yield_site;
    uint32_t parked_on;
    uint64_t yields;
    wpj2_thread_image image;
} snapshot_slot_t;

typedef struct {
    wpj2_hle_state_image hle;
    wpj2_rsp_state_image rsp;
    wpj2_pif_state_image pif;
    wpj2_audio_state_image audio;
} snapshot_devices_t;

typedef struct {
    int used;
    int finished;
    uint32_t yield_site;
    uint32_t parked_on;
    uint64_t yields;
    wpj2_stateful_thread thread;
} sched_slot_t;

static sched_slot_t g_slots[MAX_THREADS];
static sched_slot_t* g_current_slot;
static uint32_t g_current;
static uint64_t g_switches, g_dispatch_calls, g_empty_dispatch;
static uint64_t g_idle_rounds, g_yield_requeued, g_pause_self_calls;
static uint64_t g_count_calls;
static uint32_t g_debug_yields;
static LARGE_INTEGER g_qpc_freq, g_qpc_start;
static int g_count_vi_clock = -1;
static uint64_t g_auto_save_switch;
static uint64_t g_auto_load_delta;
static int g_auto_save_done, g_auto_load_done, g_auto_initialized;
static int g_auto_load_on_start, g_auto_start_attempted;
static int g_auto_slot = 1;

static uint32_t hash_bytes(uint32_t hash, const void* data, size_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static int snapshot_path(char* path, size_t capacity, const char* leaf) {
    char directory[MAX_PATH] = "sav\\bookmarks";
    DWORD n = GetEnvironmentVariableA("WPJ2_BOOKMARK_DIR", directory,
                                      (DWORD)sizeof(directory));
    if (n >= sizeof(directory)) return 0;
    CreateDirectoryA("sav", NULL);
    CreateDirectoryA(directory, NULL);
    return snprintf(path, capacity, "%s\\%s", directory, leaf) > 0;
}

static int snapshot_save(uint8_t* rdram, int state_slot) {
    if (state_slot < 1 || state_slot > 9) return 0;
    /* O RT64 lê display lists, vértices e texturas diretamente da RDRAM numa
       thread própria. Espere a fila e publique suas conclusões antes de
       congelar memória/dispositivos no arquivo. */
    rsp_sync_rt64_completions();
    snapshot_slot_t* slots = (snapshot_slot_t*)calloc(MAX_THREADS, sizeof(*slots));
    if (!slots) return 0;
    for (int i = 0; i < MAX_THREADS; i++) {
        slots[i].used = g_slots[i].used;
        slots[i].finished = g_slots[i].finished;
        slots[i].yield_site = g_slots[i].yield_site;
        slots[i].parked_on = g_slots[i].parked_on;
        slots[i].yields = g_slots[i].yields;
        if (g_slots[i].used &&
            !wpj2_stateful_thread_capture(&g_slots[i].thread, &slots[i].image)) {
            printf("[savestate] captura recusada pela thread slot=%d id=%u depth=%u\n",
                   i, g_slots[i].thread.thread_id,
                   g_slots[i].thread.continuation.depth);
            free(slots);
            return 0;
        }
    }
    snapshot_devices_t devices;
    memset(&devices, 0, sizeof(devices));
    hle_state_capture(&devices.hle);
    rsp_state_capture(&devices.rsp);
    pif_state_capture(&devices.pif);
    audio_state_capture(&devices.audio);
    snapshot_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = SNAPSHOT_MAGIC;
    header.version = SNAPSHOT_VERSION;
    header.header_size = sizeof(header);
    header.rdram_size = SNAPSHOT_RDRAM_SIZE;
    header.slot_count = MAX_THREADS;
    header.switches = g_switches;
    header.dispatch_calls = g_dispatch_calls;
    header.empty_dispatch = g_empty_dispatch;
    header.yield_requeued = g_yield_requeued;
    uint32_t hash = hash_bytes(2166136261u, rdram, SNAPSHOT_RDRAM_SIZE);
    hash = hash_bytes(hash, slots, MAX_THREADS * sizeof(*slots));
    header.payload_hash = hash_bytes(hash, &devices, sizeof(devices));

    char final_path[MAX_PATH], temp_path[MAX_PATH];
    char leaf[32], temp_leaf[36];
    snprintf(leaf, sizeof(leaf), "slot%d.wpstate", state_slot);
    snprintf(temp_leaf, sizeof(temp_leaf), "slot%d.wpstate.tmp", state_slot);
    if (!snapshot_path(final_path, sizeof(final_path), leaf) ||
        !snapshot_path(temp_path, sizeof(temp_path), temp_leaf)) {
        free(slots); return 0;
    }
    FILE* file = fopen(temp_path, "wb");
    int ok = file && fwrite(&header, sizeof(header), 1, file) == 1 &&
        fwrite(rdram, SNAPSHOT_RDRAM_SIZE, 1, file) == 1 &&
        fwrite(slots, MAX_THREADS * sizeof(*slots), 1, file) == 1 &&
        fwrite(&devices, sizeof(devices), 1, file) == 1;
    if (file && fclose(file) != 0) ok = 0;
    free(slots);
    if (ok) ok = MoveFileExA(temp_path, final_path,
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileA(temp_path);
    printf("[savestate] slot %d: %s -> %s\n", state_slot,
           ok ? "gravado" : "FALHOU", final_path);
    fflush(stdout);
    return ok;
}

static int snapshot_load(uint8_t* rdram, int state_slot) {
    if (state_slot < 1 || state_slot > 9) return 0;
    char path[MAX_PATH];
    char leaf[32];
    snprintf(leaf, sizeof(leaf), "slot%d.wpstate", state_slot);
    if (!snapshot_path(path, sizeof(path), leaf)) return 0;
    FILE* file = fopen(path, "rb");
    if (!file) {
        printf("[savestate] slot %d: nenhum estado encontrado\n", state_slot);
        return 0;
    }
    snapshot_header_t header;
    uint8_t* new_rdram = (uint8_t*)malloc(SNAPSHOT_RDRAM_SIZE);
    snapshot_slot_t* slots = (snapshot_slot_t*)calloc(MAX_THREADS, sizeof(*slots));
    snapshot_devices_t devices;
    memset(&devices, 0, sizeof(devices));
    int ok = new_rdram && slots && fread(&header, sizeof(header), 1, file) == 1 &&
        header.magic == SNAPSHOT_MAGIC && header.version == SNAPSHOT_VERSION &&
        header.header_size == sizeof(header) &&
        header.rdram_size == SNAPSHOT_RDRAM_SIZE && header.slot_count == MAX_THREADS &&
        fread(new_rdram, SNAPSHOT_RDRAM_SIZE, 1, file) == 1 &&
        fread(slots, MAX_THREADS * sizeof(*slots), 1, file) == 1 &&
        fread(&devices, sizeof(devices), 1, file) == 1;
    fclose(file);
    uint32_t hash = 0;
    if (ok) {
        hash = hash_bytes(2166136261u, new_rdram, SNAPSHOT_RDRAM_SIZE);
        hash = hash_bytes(hash, slots, MAX_THREADS * sizeof(*slots));
        hash = hash_bytes(hash, &devices, sizeof(devices));
        ok = hash == header.payload_hash;
    }
    if (ok && (devices.rsp.task_done_first >= WPJ2_RSP_DONE_CAP ||
               devices.rsp.task_done_count > WPJ2_RSP_DONE_CAP)) ok = 0;
    for (int i = 0; ok && i < MAX_THREADS; i++)
        if (slots[i].used && !wpj2_thread_image_validate(&slots[i].image)) ok = 0;
    if (ok) {
        /* Só altere o mundo vivo depois que o arquivo inteiro foi validado. */
        /* A thread gráfica não pode continuar lendo a RDRAM enquanto ela é
           substituída. O backend será recriado a partir do próximo task. */
        rt64_backend_shutdown();
        memcpy(rdram, new_rdram, SNAPSHOT_RDRAM_SIZE);
        memset(g_slots, 0, sizeof(g_slots));
        for (int i = 0; i < MAX_THREADS; i++) if (slots[i].used) {
            g_slots[i].used = 1;
            g_slots[i].finished = slots[i].finished;
            g_slots[i].yield_site = slots[i].yield_site;
            g_slots[i].parked_on = slots[i].parked_on;
            g_slots[i].yields = slots[i].yields;
            if (!wpj2_stateful_thread_restore(&g_slots[i].thread, &slots[i].image)) {
                ok = 0; break;
            }
        }
        g_switches = header.switches;
        g_dispatch_calls = header.dispatch_calls;
        g_empty_dispatch = header.empty_dispatch;
        g_yield_requeued = header.yield_requeued;
        g_current = 0;
        g_current_slot = NULL;
        hle_state_restore(&devices.hle);
        rsp_state_restore(rdram, &devices.rsp);
        pif_state_restore(&devices.pif);
        /* Buffers e objetos graficos do host nao pertencem ao arquivo. Eles
         * sao recriados, enquanto o estado logico dos dispositivos volta da
         * imagem serializada. */
        audio_shutdown();
        audio_init();
        audio_state_restore(&devices.audio);
    }
    free(slots);
    free(new_rdram);
    printf("[savestate] slot %d: %s <- %s\n", state_slot,
           ok ? "restaurado" : "FALHOU", path);
    fflush(stdout);
    return ok;
}

static void snapshot_process_requests(uint8_t* rdram) {
    int save_slot = video_state_save_requested();
    int load_slot = video_state_load_requested();
    if (save_slot) {
        int ok = snapshot_save(rdram, save_slot);
        video_state_request_consumed(1);
        video_state_notify(save_slot, 1, ok);
    }
    if (load_slot) {
        int ok = snapshot_load(rdram, load_slot);
        video_state_request_consumed(0);
        video_state_notify(load_slot, 0, ok);
    }
    if (!g_auto_initialized) {
        g_auto_initialized = 1;
        const char* save_at = getenv("WPJ2_STATEFUL_AUTOSAVE_SWITCH");
        const char* load_after = getenv("WPJ2_STATEFUL_AUTOLOAD_DELTA");
        const char* load_start = getenv("WPJ2_STATEFUL_LOAD_ON_START");
        const char* auto_slot = getenv("WPJ2_STATEFUL_SLOT");
        if (save_at) g_auto_save_switch = strtoull(save_at, NULL, 0);
        if (load_after) g_auto_load_delta = strtoull(load_after, NULL, 0);
        if (auto_slot && atoi(auto_slot) >= 1 && atoi(auto_slot) <= 9)
            g_auto_slot = atoi(auto_slot);
        if (load_start) {
            int requested = atoi(load_start);
            g_auto_load_on_start = requested != 0;
            if (requested >= 1 && requested <= 9) g_auto_slot = requested;
        }
    }
    if (g_auto_load_on_start && !g_auto_start_attempted) {
        g_auto_start_attempted = 1;
        g_auto_load_done = snapshot_load(rdram, g_auto_slot);
        /* Uma falha tambem e terminal: nao tente abrir o mesmo arquivo a cada
         * troca de thread. O F4 continua disponivel para nova tentativa. */
        g_auto_load_on_start = 0;
    }
    if (!g_auto_save_done && g_auto_save_switch &&
        g_switches >= g_auto_save_switch) {
        g_auto_save_done = snapshot_save(rdram, g_auto_slot);
    }
    if (g_auto_save_done && !g_auto_load_done && g_auto_load_delta &&
        g_switches >= g_auto_save_switch + g_auto_load_delta) {
        g_auto_load_done = snapshot_load(rdram, g_auto_slot);
        /* O contador volta ao valor salvo; desarme para nao carregar em laco. */
        if (g_auto_load_done) {
            g_auto_save_switch = 0;
            g_auto_load_delta = 0;
        }
    }
}

static uint32_t rd32(uint8_t* rdram, uint32_t addr) {
    return (uint32_t)MEM_W(0, (gpr)(int32_t)addr);
}
static void wr32(uint8_t* rdram, uint32_t addr, uint32_t value) {
    MEM_W(0, (gpr)(int32_t)addr) = (int32_t)value;
}
static void wr16(uint8_t* rdram, uint32_t addr, uint16_t value) {
    MEM_HU(0, (gpr)(int32_t)addr) = value;
}

static void enqueue_by_priority(uint8_t* rdram, uint32_t queue, uint32_t thread) {
    int32_t pri = (int32_t)rd32(rdram, thread + TH_PRIORITY);
    uint32_t pred = queue;
    uint32_t cur = rd32(rdram, queue);
    while (cur && pri <= (int32_t)rd32(rdram, cur + TH_PRIORITY)) {
        pred = cur;
        cur = rd32(rdram, cur + TH_NEXT);
    }
    wr32(rdram, thread + TH_NEXT, cur);
    wr32(rdram, pred + TH_NEXT, thread);
}

static int unlink_bounded(uint8_t* rdram, uint32_t head, uint32_t thread,
                          uint32_t next_offset) {
    uint32_t prev = head;
    uint32_t cur = rd32(rdram, head);
    for (unsigned i = 0; cur && i < MAX_THREADS * 2u; i++) {
        if (cur == thread) {
            wr32(rdram, prev + (prev == head ? 0u : next_offset),
                 rd32(rdram, cur + next_offset));
            return 1;
        }
        prev = cur;
        cur = rd32(rdram, cur + next_offset);
    }
    return 0;
}

static sched_slot_t* slot_for(uint32_t guest_thread, int create) {
    sched_slot_t* free_slot = NULL;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_slots[i].used && g_slots[i].thread.guest_thread == guest_thread)
            return &g_slots[i];
        if (!free_slot && !g_slots[i].used) free_slot = &g_slots[i];
    }
    return create ? free_slot : NULL;
}

static void initialize_slot(uint8_t* rdram, sched_slot_t* slot, uint32_t th) {
    uint32_t root = rd32(rdram, th + TH_CTX_PC);
    uint32_t id = rd32(rdram, th + TH_ID);
    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    wpj2_stateful_thread_init(&slot->thread, th, id, root);
    recomp_context* ctx = &slot->thread.context;
    ctx->r4 = (gpr)(int32_t)rd32(rdram, th + TH_CTX_A0 + LO_WORD);
    ctx->r28 = (gpr)(int32_t)rd32(rdram, th + TH_CTX_GP + LO_WORD);
    ctx->r29 = (gpr)(int32_t)rd32(rdram, th + TH_CTX_SP + LO_WORD);
    ctx->r30 = (gpr)(int32_t)rd32(rdram, th + TH_CTX_S8 + LO_WORD);
    ctx->r31 = (gpr)(int32_t)rd32(rdram, th + TH_CTX_RA + LO_WORD);
    ctx->status_reg = rd32(rdram, th + TH_CTX_SR);
    printf("  [stateful] nova thread id=%u pc=0x%08X sp=0x%08X\n",
           id, root, (uint32_t)ctx->r29);
}

uint32_t sched_current(void) { return g_current; }
uint64_t sched_switches(void) { return g_switches; }
uint64_t sched_dispatch_calls(void) { return g_dispatch_calls; }
uint64_t sched_empty_dispatch(void) { return g_empty_dispatch; }
uint64_t sched_yield_requeued(void) { return g_yield_requeued; }
uint64_t sched_count_calls(void) { return g_count_calls; }

void sched_init(void) {
    memset(g_slots, 0, sizeof(g_slots));
    g_current_slot = NULL;
    g_current = 0;
}

void func_800CCA8C(uint8_t* rdram, recomp_context* ctx) {
    uint32_t queue = (uint32_t)ctx->r4;
    uint32_t thread = (uint32_t)ctx->r5;
    if (!queue || !thread) return;
    enqueue_by_priority(rdram, queue, thread);
    wr32(rdram, thread + TH_QUEUE, queue);
    sched_slot_t* slot = slot_for(thread, 0);
    if (slot) slot->parked_on = queue;
}

static void yield_current(void) {
    wpj2_continuation* cont = wpj2_cont_current();
    if (cont) {
        if (g_debug_yields++ < 16)
            printf("  [stateful] yield pedido: th=%08X depth=%u\n",
                   g_current, cont->depth);
        hle_prepare_scheduler_yield();
        wpj2_stateful_thread_yield_current();
    }
}

void func_800CCAE4(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    g_dispatch_calls++;
    if (g_current_slot) {
        g_current_slot->yield_site = trace_last_func();
        g_current_slot->yields++;
        uint16_t state = MEM_HU(0, (gpr)(int32_t)(g_current + TH_STATE));
        if (state == OS_STATE_RUNNING) {
            wr16(rdram, g_current + TH_STATE, OS_STATE_RUNNABLE);
            enqueue_by_priority(rdram, ADDR_RUN_QUEUE, g_current);
            g_yield_requeued++;
        }
    }
    /* Tambem encerra a fase de boot, que ainda nao pertence a uma OSThread. */
    yield_current();
}

void sched_pause_current(uint8_t* rdram) {
    if (!g_current_slot) { yield_current(); return; }
    if (g_pause_self_calls++ < 32)
        printf("  [pause-self] thread=0x%08X func=0x%08X\n",
               g_current, trace_last_func());
    wr16(rdram, g_current + TH_STATE, OS_STATE_WAITING);
    g_current_slot->yield_site = trace_last_func();
    g_current_slot->yields++;
    g_current_slot->parked_on = 0;
    yield_current();
}

void sched_terminate_current(uint8_t* rdram) {
    (void)rdram;
    if (g_current_slot) {
        g_current_slot->finished = 1;
        g_current_slot->parked_on = 0;
        g_current_slot->thread.run_state = WPJ2_THREAD_FINISHED;
    }
    yield_current();
}

void sched_destroy_thread(uint8_t* rdram, uint32_t thread) {
    if (!thread) thread = g_current;
    if (!thread) return;
    uint16_t state = MEM_HU(0, (gpr)(int32_t)(thread + TH_STATE));
    uint32_t queue = rd32(rdram, thread + TH_QUEUE);
    if (state != OS_STATE_RUNNING && queue)
        unlink_bounded(rdram, queue, thread, TH_NEXT);
    unlink_bounded(rdram, ADDR_ACTIVE_QUEUE, thread, TH_TLNEXT);
    wr16(rdram, thread + TH_STATE, OS_STATE_STOPPED);
    wr32(rdram, thread + TH_NEXT, 0);
    wr32(rdram, thread + TH_TLNEXT, 0);
    sched_slot_t* slot = slot_for(thread, 0);
    if (slot) {
        slot->finished = 1;
        slot->parked_on = 0;
    }
    if (thread == g_current) sched_terminate_current(rdram);
}

void sched_preempt(uint8_t* rdram) {
    if (!g_current_slot) return;
    uint16_t state = MEM_HU(0, (gpr)(int32_t)(g_current + TH_STATE));
    if (state == OS_STATE_RUNNING &&
        (int32_t)rd32(rdram, g_current + TH_PRIORITY) >= 0) {
        wr16(rdram, g_current + TH_STATE, OS_STATE_RUNNABLE);
        enqueue_by_priority(rdram, ADDR_RUN_QUEUE, g_current);
    }
    yield_current();
}

#define N64_COUNT_HZ 46875000.0
uint32_t sched_count_now(void) {
    if (g_count_vi_clock < 0) {
        const char* value = getenv("WPJ2_COUNT_VI_CLOCK");
        g_count_vi_clock = value && *value && *value != '0';
    }
    if (g_count_vi_clock)
        return (uint32_t)(hle_retraces() * 781250ull);
    if (!g_qpc_freq.QuadPart) {
        QueryPerformanceFrequency(&g_qpc_freq);
        QueryPerformanceCounter(&g_qpc_start);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint32_t)(((double)(now.QuadPart - g_qpc_start.QuadPart) /
                       (double)g_qpc_freq.QuadPart) * N64_COUNT_HZ);
}

void func_800CBBB0(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    g_count_calls++;
    ctx->r2 = (gpr)(int32_t)sched_count_now();
}

static LONG stateful_fault_filter(EXCEPTION_POINTERS* ep, sched_slot_t* slot) {
    ULONG_PTR instruction = (ULONG_PTR)ep->ExceptionRecord->ExceptionAddress;
    ULONG_PTR module = (ULONG_PTR)GetModuleHandleW(NULL);
    recomp_context* c = &slot->thread.context;
    fprintf(stderr,
            "[stateful-seh] code=%08X access=%llu addr=%llX "
            "id=%u root=%08X RVA=%llX a0=%08X a1=%08X "
            "sp=%08X ra=%08X v0=%08X v1=%08X a2=%08X a3=%08X depth=%u\n",
            (uint32_t)ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->NumberParameters > 0
                ? (unsigned long long)ep->ExceptionRecord->ExceptionInformation[0] : 0ull,
            ep->ExceptionRecord->NumberParameters > 1
                ? (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1] : 0ull,
            slot->thread.thread_id, slot->thread.root_pc,
            (unsigned long long)(instruction - module),
            (uint32_t)c->r4, (uint32_t)c->r5,
            (uint32_t)c->r29, (uint32_t)c->r31,
            (uint32_t)c->r2, (uint32_t)c->r3,
            (uint32_t)c->r6, (uint32_t)c->r7,
            slot->thread.continuation.depth);
    fprintf(stderr,
            "  regs t0=%08X t1=%08X t2=%08X t3=%08X t4=%08X t5=%08X "
            "t6=%08X t7=%08X t8=%08X t9=%08X\n",
            (uint32_t)c->r8, (uint32_t)c->r9, (uint32_t)c->r10,
            (uint32_t)c->r11, (uint32_t)c->r12, (uint32_t)c->r13,
            (uint32_t)c->r14, (uint32_t)c->r15, (uint32_t)c->r24,
            (uint32_t)c->r25);
    for (uint32_t i = 0; i < slot->thread.continuation.depth; i++)
        fprintf(stderr, "  seh-frame[%u]=%08X/%08X\n", i,
                slot->thread.continuation.frames[i].function_vram,
                slot->thread.continuation.frames[i].callsite_vram);
    fprintf(stderr,
            "  arena 801ACF88: base=%08X atual=%08X fim/soma=%08X; "
            "pilha+20=%08X pilha+24=%08X\n",
            rd32(g_rdram, 0x801ACF88u), rd32(g_rdram, 0x801ACF8Cu),
            rd32(g_rdram, 0x801ACF90u),
            rd32(g_rdram, (uint32_t)c->r29 + 0x20u),
            rd32(g_rdram, (uint32_t)c->r29 + 0x24u));
    return EXCEPTION_EXECUTE_HANDLER;
}

static void dispatch_slot(uint8_t* rdram, sched_slot_t* slot) {
    recomp_func_t* root = find_function(slot->thread.root_pc);
    if (!root) {
        printf("  [stateful] funcao raiz ausente: 0x%08X\n", slot->thread.root_pc);
        slot->finished = 1;
        return;
    }
    g_current_slot = slot;
    g_current = slot->thread.guest_thread;
    g_switches++;
    wpj2_thread_run_state result = WPJ2_THREAD_FAULTED;
    __try {
        result = wpj2_stateful_thread_dispatch(&slot->thread, rdram, root);
    }
    __except (stateful_fault_filter(GetExceptionInformation(), slot)) {
        wpj2_cont_bind(NULL);
    }
    if (g_switches <= 16)
        printf("  [stateful] retorno: id=%u estado=%u depth=%u\n",
               slot->thread.thread_id, (unsigned)result,
               slot->thread.continuation.depth);
    if (result == WPJ2_THREAD_FINISHED || result == WPJ2_THREAD_FAULTED)
        slot->finished = 1;
    g_current_slot = NULL;
    g_current = 0;
}

void sched_run_stateful(uint8_t* rdram, recomp_func_t* boot,
                        const recomp_context* boot_context) {
    wpj2_stateful_thread boot_thread;
    wpj2_stateful_thread_init(&boot_thread, 0, 0, 0x80000400u);
    boot_thread.context = *boot_context;
    boot_thread.context.f_odd = &boot_thread.context.f0.u32h;
    (void)wpj2_stateful_thread_dispatch(&boot_thread, rdram, boot);

    for (;;) {
        snapshot_process_requests(rdram);
        uint32_t head = rd32(rdram, ADDR_RUN_QUEUE);
        sched_slot_t* existing = head ? slot_for(head, 0) : NULL;
        int runnable = head && (int32_t)rd32(rdram, head + TH_PRIORITY) >= 0 &&
            (rd32(rdram, head + TH_CTX_PC) != 0 ||
             (existing && !existing->finished));
        if (!runnable) {
            g_empty_dispatch++;
            g_idle_rounds += hle_deliver_events(rdram) != 0;
            if (g_idle_rounds == MAX_IDLE_ROUNDS)
                printf("  [stateful] fila vazia; aguardando eventos\n");
            continue;
        }
        g_idle_rounds = 0;
        wr32(rdram, ADDR_RUN_QUEUE, rd32(rdram, head + TH_NEXT));
        wr32(rdram, ADDR_RUNNING_THREAD, head);
        wr16(rdram, head + TH_STATE, OS_STATE_RUNNING);

        sched_slot_t* slot = slot_for(head, 1);
        if (!slot) {
            printf("  [stateful] limite de %d OSThreads excedido\n", MAX_THREADS);
            return;
        }
        if (!slot->used || slot->finished) initialize_slot(rdram, slot, head);
        dispatch_slot(rdram, slot);
    }
}

static const char* state_name(uint16_t state) {
    switch (state) {
        case OS_STATE_STOPPED: return "PARADA";
        case OS_STATE_RUNNABLE: return "PRONTA";
        case OS_STATE_RUNNING: return "RODANDO";
        case OS_STATE_WAITING: return "ESPERANDO";
        default: return "?";
    }
}

void sched_report(uint8_t* rdram) {
    printf("threads stateful (fila=0x%08X):\n", rd32(rdram, ADDR_RUN_QUEUE));
    for (int i = 0; i < MAX_THREADS; i++) {
        sched_slot_t* slot = &g_slots[i];
        if (!slot->used) continue;
        uint32_t th = slot->thread.guest_thread;
        uint16_t state = MEM_HU(0, (gpr)(int32_t)(th + TH_STATE));
        printf("   id=%-3u th=%08X root=%08X %-9s frames=%u yields=%llu\n",
               slot->thread.thread_id, th, slot->thread.root_pc,
               slot->finished ? "TERMINOU" : state_name(state),
               slot->thread.continuation.depth,
               (unsigned long long)slot->yields);
        if (slot->thread.continuation.depth) {
            printf("      cadeia:");
            for (uint32_t f = 0;
                 f < slot->thread.continuation.depth && f < 16u; f++) {
                const wpj2_cont_frame* frame =
                    &slot->thread.continuation.frames[f];
                printf(" %08X/%08X@%08X", frame->function_vram,
                       frame->callsite_vram, frame->stack_pointer);
            }
            if (slot->thread.continuation.depth > 16u) printf(" ...");
            printf("\n");
        }
    }
}
