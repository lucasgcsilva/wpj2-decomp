#include <stdio.h>
#include <string.h>

#include "continuation.h"

static wpj2_continuation g_cont;
static int g_value;
static int g_should_yield;

static void leaf(void) {
    uint32_t site = 0, frame = 0;
    int resumed = wpj2_cont_enter(&g_cont, 0x80003000u, &site, &frame);
    if (resumed < 0) return;
    if (resumed) {
        if (site != 0x80003018u) return;
        goto after_pause;
    }
    g_value += 100;
    if (!wpj2_cont_before_call(&g_cont, 0x80003000u, 0x80003018u, &frame)) return;
    if (g_should_yield) {
        g_should_yield = 0;
        wpj2_cont_yield(&g_cont);
        return;
    }
after_pause:
    if (!wpj2_cont_after_call(&g_cont, frame)) return;
    g_value += 1;
}

static void middle(void) {
    uint32_t site = 0, frame = 0;
    int resumed = wpj2_cont_enter(&g_cont, 0x80002000u, &site, &frame);
    if (resumed < 0) return;
    if (resumed) {
        if (site != 0x80002020u) return;
        goto call_leaf;
    }
    g_value += 10;
    if (!wpj2_cont_before_call(&g_cont, 0x80002000u, 0x80002020u, &frame)) return;
call_leaf:
    leaf();
    if (wpj2_cont_yielding(&g_cont)) return;
    if (!wpj2_cont_after_call(&g_cont, frame)) return;
    g_value += 2;
}

static void root(void) {
    uint32_t site = 0, frame = 0;
    int resumed = wpj2_cont_enter(&g_cont, 0x80001000u, &site, &frame);
    if (resumed < 0) return;
    if (resumed) {
        if (site != 0x80001030u) return;
        goto call_middle;
    }
    g_value += 1;
    if (!wpj2_cont_before_call(&g_cont, 0x80001000u, 0x80001030u, &frame)) return;
call_middle:
    middle();
    if (wpj2_cont_yielding(&g_cont)) return;
    if (!wpj2_cont_after_call(&g_cont, frame)) return;
    g_value += 3;
}

int main(void) {
    wpj2_continuation saved;
    wpj2_cont_init(&g_cont);
    g_value = 0;
    g_should_yield = 1;

    wpj2_cont_begin_dispatch(&g_cont);
    root();
    if (!wpj2_cont_yielding(&g_cont) || g_cont.depth != 3u || g_value != 111)
        return 1;
    if (!wpj2_cont_validate(&g_cont)) return 2;

    /* Simula fechar o processo: apenas a parte serializavel sobrevive. */
    saved = g_cont;
    memset(&g_cont, 0, sizeof(g_cont));
    g_cont = saved;
    g_value = 111;

    wpj2_cont_begin_dispatch(&g_cont);
    root();
    if (wpj2_cont_yielding(&g_cont) || g_cont.depth != 0u || g_value != 117)
        return 3;

    /* Temporarios do N64Recomp tambem pertencem a frame. Sem eles, um MULT ou
     * comparador de FPU anterior a uma preempcao volta como zero. */
    wpj2_cont_init(&g_cont);
    uint32_t frame = 0, site = 0;
    recomp_context context;
    memset(&context, 0, sizeof(context));
    context.r4 = (gpr)(int64_t)(int32_t)0x80123456u;
    context.r29 = (gpr)(int64_t)(int32_t)0x80001000u;
    context.f_odd = &context.f0.u32h;
    if (!wpj2_cont_before_call_ex(&g_cont, 0x80004000u, 0x80004020u,
                                  11u, 22u, 33u, 1, 0x80009900u,
                                  &context, &frame)) return 4;
    wpj2_cont_yield(&g_cont);
    saved = g_cont;
    g_cont = saved;
    wpj2_cont_begin_dispatch(&g_cont);
    uint64_t hi = 0, lo = 0, result = 0;
    int c1cs = 0;
    uint32_t target = 0;
    recomp_context restored;
    memset(&restored, 0, sizeof(restored));
    if (wpj2_cont_enter_ex(&g_cont, 0x80004000u, &site, &frame,
                           &hi, &lo, &result, &c1cs, &target, &restored) != 1 ||
        site != 0x80004020u || hi != 11u || lo != 22u || result != 33u ||
        c1cs != 1 || target != 0x80009900u ||
        (uint32_t)restored.r4 != 0x80123456u ||
        (uint32_t)restored.r29 != 0x80001000u ||
        restored.f_odd != &restored.f0.u32h) return 5;

    puts("continuation snapshot roundtrip: OK");
    return 0;
}
