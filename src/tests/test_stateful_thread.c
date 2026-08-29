#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stateful_thread.h"

static int g_pause_once;

static void child(uint8_t* rdram, recomp_context* ctx) {
    uint32_t site = 0, frame = 0;
    int resumed = wpj2_cont_enter_current(0x80002000u, &site, &frame);
    if (resumed < 0) return;
    if (resumed) {
        if (site != 0x80002010u) return;
        goto resume_pause;
    }
    ctx->r2 += 10;
    if (!wpj2_cont_before_call_current(0x80002000u, 0x80002010u, &frame)) return;
    if (!g_pause_once) {
        g_pause_once = 1;
        wpj2_cont_yield(wpj2_cont_current());
        return;
    }
resume_pause:
    if (!wpj2_cont_after_call_current(frame)) return;
    rdram[3] = 0x5Au;
    ctx->r2 += 2;
}

static void root(uint8_t* rdram, recomp_context* ctx) {
    uint32_t site = 0, frame = 0;
    int resumed = wpj2_cont_enter_current(0x80001000u, &site, &frame);
    if (resumed < 0) return;
    if (resumed) {
        if (site != 0x80001020u) return;
        goto resume_child;
    }
    ctx->r2 += 1;
    if (!wpj2_cont_before_call_current(0x80001000u, 0x80001020u, &frame)) return;
resume_child:
    child(rdram, ctx);
    if (wpj2_cont_yielding_current()) return;
    if (!wpj2_cont_after_call_current(frame)) return;
    ctx->r2 += 3;
}

int main(int argc, char** argv) {
    if (argc != 2) return 10;
    uint8_t rdram[16] = {0};
    wpj2_stateful_thread thread;
    wpj2_thread_image image;
    wpj2_stateful_thread_init(&thread, 0x80100000u, 7u, 0x80001000u);

    if (wpj2_stateful_thread_dispatch(&thread, rdram, root) != WPJ2_THREAD_WAITING)
        return 11;
    if (thread.context.r2 != 11 || thread.continuation.depth != 2u) return 12;
    if (!wpj2_stateful_thread_capture(&thread, &image)) return 13;

    FILE* file = fopen(argv[1], "wb");
    if (!file) return 14;
    if (fwrite(&image, sizeof(image), 1, file) != 1) return 15;
    fclose(file);

    memset(&thread, 0xCD, sizeof(thread));
    memset(&image, 0, sizeof(image));
    memset(rdram, 0, sizeof(rdram));
    file = fopen(argv[1], "rb");
    if (!file) return 16;
    if (fread(&image, sizeof(image), 1, file) != 1) return 17;
    fclose(file);
    if (!wpj2_stateful_thread_restore(&thread, &image)) return 18;

    if (wpj2_stateful_thread_dispatch(&thread, rdram, root) != WPJ2_THREAD_FINISHED)
        return 19;
    if (thread.context.r2 != 16 || rdram[3] != 0x5A ||
        thread.continuation.depth != 0u ||
        thread.context.f_odd != &thread.context.f0.u32h)
        return 20;
    puts("stateful thread file restart: OK");
    return 0;
}
