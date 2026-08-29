#include "runtime.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Linha do tempo opt-in: registra em RAM para que a propria medicao nao crie
 * I/O ou flushes no caminho de audio/video. O arquivo nasce somente no exit. */
#define PERF_TIMELINE_MAX 65536u
typedef struct {
    LONGLONG ticks;
    uint32_t arg0;
    uint32_t arg1;
    char tag[16];
} perf_timeline_event;

static perf_timeline_event g_events[PERF_TIMELINE_MAX];
static uint32_t g_event_count;
static LARGE_INTEGER g_frequency, g_origin;
static char g_path[MAX_PATH];
static int g_initialized;
static int g_written;

void perf_timeline_flush(void) {
    if (g_written || !g_path[0] || !g_frequency.QuadPart) return;
    FILE* f = fopen(g_path, "w");
    if (!f) return;
    fputs("seq,ms,evento,arg0,arg1\n", f);
    for (uint32_t i = 0; i < g_event_count; i++) {
        const perf_timeline_event* e = &g_events[i];
        double ms = (double)(e->ticks - g_origin.QuadPart) * 1000.0 /
                    (double)g_frequency.QuadPart;
        fprintf(f, "%u,%.6f,%s,%u,%u\n", i, ms, e->tag, e->arg0, e->arg1);
    }
    fclose(f);
    g_written = 1;
}

static void perf_timeline_init(void) {
    if (g_initialized) return;
    g_initialized = 1;
    const char* path = getenv("WPJ2_PERF_TIMELINE");
    if (!path || !*path) return;
    strncpy(g_path, path, sizeof(g_path) - 1u);
    QueryPerformanceFrequency(&g_frequency);
    QueryPerformanceCounter(&g_origin);
    atexit(perf_timeline_flush);
}

void perf_timeline_mark(const char* tag, uint32_t arg0, uint32_t arg1) {
    perf_timeline_init();
    if (!g_path[0] || g_event_count >= PERF_TIMELINE_MAX) return;
    perf_timeline_event* e = &g_events[g_event_count++];
    QueryPerformanceCounter((LARGE_INTEGER*)&e->ticks);
    e->arg0 = arg0;
    e->arg1 = arg1;
    strncpy(e->tag, tag ? tag : "?", sizeof(e->tag) - 1u);
    e->tag[sizeof(e->tag) - 1u] = 0;
}
