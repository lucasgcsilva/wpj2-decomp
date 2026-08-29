#include "continuation.h"

#include <string.h>
#include <stddef.h>

#if defined(_MSC_VER)
#define WPJ2_THREAD_LOCAL __declspec(thread)
#else
#define WPJ2_THREAD_LOCAL _Thread_local
#endif

static WPJ2_THREAD_LOCAL wpj2_continuation* g_current_cont;

_Static_assert(offsetof(recomp_context, r31) - offsetof(recomp_context, r0) ==
               31u * sizeof(gpr), "GPRs precisam ser contiguos");
_Static_assert(offsetof(recomp_context, f31) - offsetof(recomp_context, f0) ==
               31u * sizeof(fpr), "FPRs precisam ser contiguos");

static void context_to_frame(const recomp_context* source,
                             wpj2_cont_frame* frame) {
    if (!source || !frame) return;
    memcpy(frame->gpr_values, &source->r0, sizeof(frame->gpr_values));
    memcpy(frame->fpr_values, &source->f0, sizeof(frame->fpr_values));
    frame->context_hi = source->hi;
    frame->context_lo = source->lo;
    frame->status_reg = source->status_reg;
    frame->mips3_float_mode = source->mips3_float_mode;
    memset(frame->reserved, 0, sizeof(frame->reserved));
}

static void frame_to_context(const wpj2_cont_frame* frame,
                             recomp_context* target) {
    if (!frame || !target) return;
    memcpy(&target->r0, frame->gpr_values, sizeof(frame->gpr_values));
    memcpy(&target->f0, frame->fpr_values, sizeof(frame->fpr_values));
    target->hi = frame->context_hi;
    target->lo = frame->context_lo;
    target->status_reg = frame->status_reg;
    target->mips3_float_mode = frame->mips3_float_mode;
    target->f_odd = &target->f0.u32h;
}

void wpj2_cont_init(wpj2_continuation* cont) {
    if (!cont) return;
    memset(cont, 0, sizeof(*cont));
    cont->version = WPJ2_CONT_FORMAT_VERSION;
}

int wpj2_cont_validate(const wpj2_continuation* cont) {
    if (!cont || cont->version != WPJ2_CONT_FORMAT_VERSION) return 0;
    if (cont->depth > WPJ2_CONT_MAX_FRAMES) return 0;
    for (uint32_t i = 0; i < cont->depth; i++) {
        uint32_t tag = cont->frames[i].callsite_vram & 3u;
        if ((cont->frames[i].function_vram & 3u) != 0u || tag == 3u ||
            cont->frames[i].function_vram < 0x80000000u ||
            (cont->frames[i].callsite_vram & ~3u) < 0x80000000u)
            return 0;
        if (cont->frames[i].call_target_vram &&
            ((cont->frames[i].call_target_vram & 3u) != 0u ||
             cont->frames[i].call_target_vram < 0x80000000u)) return 0;
        if (cont->frames[i].stack_pointer < 0x80000000u ||
            cont->frames[i].stack_pointer >= 0x80800000u ||
            (cont->frames[i].stack_pointer & 7u) != 0u) return 0;
    }
    return 1;
}

void wpj2_cont_begin_dispatch(wpj2_continuation* cont) {
    if (!cont) return;
    cont->cursor = 0;
    cont->yielding = 0;
    cont->resuming = cont->depth != 0;
}

int wpj2_cont_enter_ex(wpj2_continuation* cont, uint32_t function_vram,
                       uint32_t* callsite, uint32_t* frame_index,
                       uint64_t* hi, uint64_t* lo, uint64_t* result,
                       int* c1cs, uint32_t* call_target_vram,
                       recomp_context* context) {
    if (!cont || !cont->resuming || cont->cursor >= cont->depth) return 0;
    const uint32_t index = cont->cursor;
    const wpj2_cont_frame* frame = &cont->frames[index];
    if (frame->function_vram != function_vram) return -1;
    cont->cursor++;
    if (callsite) *callsite = frame->callsite_vram;
    if (frame_index) *frame_index = index;
    if (hi) *hi = frame->local_hi;
    if (lo) *lo = frame->local_lo;
    if (result) *result = frame->local_result;
    if (c1cs) *c1cs = frame->local_c1cs;
    if (call_target_vram) *call_target_vram = frame->call_target_vram;
    frame_to_context(frame, context);
    /* A ultima frame persistida descreve o callsite do pai que alcancou a
     * fronteira congelada; ela nao descreve a proxima funcao chamada depois
     * dessa fronteira. Manter `resuming` ativo com cursor == depth fazia a
     * primeira chamada nova anexar uma frame e o filho interpreta-la como se
     * fosse sua propria frame antiga, retornando sem executar. */
    if (cont->cursor == cont->depth) cont->resuming = 0;
    return 1;
}

int wpj2_cont_enter(wpj2_continuation* cont, uint32_t function_vram,
                    uint32_t* callsite, uint32_t* frame_index) {
    return wpj2_cont_enter_ex(cont, function_vram, callsite, frame_index,
                              NULL, NULL, NULL, NULL, NULL, NULL);
}

int wpj2_cont_before_call_ex(wpj2_continuation* cont, uint32_t function_vram,
                             uint32_t callsite_vram, uint64_t hi, uint64_t lo,
                             uint64_t result, int c1cs, uint32_t call_target_vram,
                             const recomp_context* context,
                             uint32_t* frame_index) {
    /* Um override nativo pode chamar codigo recompilado e, portanto, formar
     * uma borda que o gerador nao consegue envolver. Se um filho ja pediu
     * cessao, qualquer chamada recompilada seguinte deve propagar a saida em
     * vez de acrescentar frames depois do ponto congelado. */
    if (!cont || cont->yielding || cont->depth >= WPJ2_CONT_MAX_FRAMES) return 0;
    const uint32_t index = cont->depth++;
    cont->frames[index].function_vram = function_vram;
    cont->frames[index].callsite_vram = callsite_vram;
    cont->frames[index].local_hi = hi;
    cont->frames[index].local_lo = lo;
    cont->frames[index].local_result = result;
    cont->frames[index].local_c1cs = c1cs;
    cont->frames[index].call_target_vram = call_target_vram;
    /* A API curta existe para o teste estrutural e nao carrega um contexto
     * real. Use um SP guest neutro para manter a frame validavel. O gerador e
     * o runtime sempre passam context != NULL. */
    cont->frames[index].stack_pointer =
        context ? (uint32_t)context->r29 : 0x80000000u;
    context_to_frame(context, &cont->frames[index]);
    if (frame_index) *frame_index = index;
    return 1;
}

int wpj2_cont_before_call(wpj2_continuation* cont, uint32_t function_vram,
                          uint32_t callsite_vram, uint32_t* frame_index) {
    return wpj2_cont_before_call_ex(cont, function_vram, callsite_vram,
                                    0, 0, 0, 0, 0, NULL, frame_index);
}

int wpj2_cont_after_call(wpj2_continuation* cont, uint32_t frame_index) {
    if (!cont || cont->yielding || frame_index >= cont->depth) return 0;
    /* Uma chamada normal sempre encerra a frame mais interna. Na reconstrucao,
     * frames mais profundas ja foram removidas pelos respectivos filhos. */
    if (frame_index + 1u != cont->depth) return 0;
    cont->depth--;
    if (cont->cursor > cont->depth) cont->cursor = cont->depth;
    if (cont->depth == 0) cont->resuming = 0;
    return 1;
}

void wpj2_cont_yield(wpj2_continuation* cont) {
    if (cont) cont->yielding = 1;
}

int wpj2_cont_yielding(const wpj2_continuation* cont) {
    return cont && cont->yielding;
}

void wpj2_cont_bind(wpj2_continuation* cont) {
    g_current_cont = cont;
}

wpj2_continuation* wpj2_cont_current(void) {
    return g_current_cont;
}

int wpj2_cont_enter_current(uint32_t function_vram, uint32_t* callsite,
                            uint32_t* frame_index) {
    if (!g_current_cont) return 0;
    return wpj2_cont_enter(g_current_cont, function_vram, callsite,
                           frame_index);
}

int wpj2_cont_enter_current_ex(uint32_t function_vram, uint32_t* callsite,
                               uint32_t* frame_index, uint64_t* hi,
                               uint64_t* lo, uint64_t* result, int* c1cs,
                               uint32_t* call_target_vram,
                               recomp_context* context) {
    if (!g_current_cont) return 0;
    return wpj2_cont_enter_ex(g_current_cont, function_vram, callsite,
                              frame_index, hi, lo, result, c1cs,
                              call_target_vram, context);
}

int wpj2_cont_before_call_current(uint32_t function_vram,
                                  uint32_t callsite_vram,
                                  uint32_t* frame_index) {
    if (!g_current_cont) return 1;
    return wpj2_cont_before_call(g_current_cont, function_vram, callsite_vram,
                                 frame_index);
}

int wpj2_cont_before_call_current_ex(uint32_t function_vram,
                                     uint32_t callsite_vram, uint64_t hi,
                                     uint64_t lo, uint64_t result, int c1cs,
                                     uint32_t call_target_vram,
                                     const recomp_context* context,
                                     uint32_t* frame_index) {
    if (!g_current_cont) return 1;
    return wpj2_cont_before_call_ex(g_current_cont, function_vram,
                                    callsite_vram, hi, lo, result, c1cs,
                                    call_target_vram, context,
                                    frame_index);
}

int wpj2_cont_after_call_current(uint32_t frame_index) {
    if (!g_current_cont) return 1;
    return wpj2_cont_after_call(g_current_cont, frame_index);
}

int wpj2_cont_yielding_current(void) {
    return wpj2_cont_yielding(g_current_cont);
}
