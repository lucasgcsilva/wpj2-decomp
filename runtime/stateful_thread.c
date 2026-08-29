#include "stateful_thread.h"

#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#if defined(_MSC_VER)
#define WPJ2_THREAD_LOCAL __declspec(thread)
#else
#define WPJ2_THREAD_LOCAL _Thread_local
#endif

static WPJ2_THREAD_LOCAL jmp_buf* g_dispatch_escape;

/* Os registradores são campos consecutivos no ABI do N64Recomp. As asserções
 * impedem que uma mudança futura no cabeçalho seja silenciosamente gravada no
 * formato antigo. */
_Static_assert(offsetof(recomp_context, r31) - offsetof(recomp_context, r0) ==
               31u * sizeof(gpr), "GPRs deixaram de ser contiguos");
_Static_assert(offsetof(recomp_context, f31) - offsetof(recomp_context, f0) ==
               31u * sizeof(fpr), "FPRs deixaram de ser contiguos");

static void context_to_image(const recomp_context* source,
                             wpj2_context_image* target) {
    memcpy(target->gpr_values, &source->r0, sizeof(target->gpr_values));
    memcpy(target->fpr_values, &source->f0, sizeof(target->fpr_values));
    target->hi = source->hi;
    target->lo = source->lo;
    target->status_reg = source->status_reg;
    target->mips3_float_mode = source->mips3_float_mode;
    memset(target->reserved, 0, sizeof(target->reserved));
}

static void image_to_context(const wpj2_context_image* source,
                             recomp_context* target) {
    memset(target, 0, sizeof(*target));
    memcpy(&target->r0, source->gpr_values, sizeof(source->gpr_values));
    memcpy(&target->f0, source->fpr_values, sizeof(source->fpr_values));
    target->hi = source->hi;
    target->lo = source->lo;
    target->status_reg = source->status_reg;
    target->mips3_float_mode = source->mips3_float_mode;
    target->f_odd = &target->f0.u32h;
}

void wpj2_stateful_thread_init(wpj2_stateful_thread* thread,
                               uint32_t guest_thread, uint32_t thread_id,
                               uint32_t root_pc) {
    if (!thread) return;
    memset(thread, 0, sizeof(*thread));
    thread->guest_thread = guest_thread;
    thread->thread_id = thread_id;
    thread->root_pc = root_pc;
    thread->run_state = WPJ2_THREAD_READY;
    thread->context.f_odd = &thread->context.f0.u32h;
    wpj2_cont_init(&thread->continuation);
}

wpj2_thread_run_state wpj2_stateful_thread_dispatch(
    wpj2_stateful_thread* thread, uint8_t* rdram, recomp_func_t* root) {
    if (!thread || !root || !wpj2_cont_validate(&thread->continuation))
        return WPJ2_THREAD_FAULTED;
    thread->run_state = WPJ2_THREAD_RUNNING;
    wpj2_cont_begin_dispatch(&thread->continuation);
    wpj2_cont_bind(&thread->continuation);
    jmp_buf escape;
    g_dispatch_escape = &escape;
    if (setjmp(escape) == 0)
        root(rdram, &thread->context);
    g_dispatch_escape = NULL;
    wpj2_cont_bind(NULL);
    if (wpj2_cont_yielding(&thread->continuation))
        thread->run_state = WPJ2_THREAD_WAITING;
    else if (thread->continuation.depth == 0)
        thread->run_state = WPJ2_THREAD_FINISHED;
    else {
        thread->run_state = WPJ2_THREAD_FAULTED;
        fprintf(stderr, "[stateful-fault] root=%08X depth=%u cursor=%u resume=%u\n",
                thread->root_pc, thread->continuation.depth,
                thread->continuation.cursor, thread->continuation.resuming);
        for (uint32_t i = 0; i < thread->continuation.depth; i++)
            fprintf(stderr, "  frame[%u]=%08X/%08X\n", i,
                    thread->continuation.frames[i].function_vram,
                    thread->continuation.frames[i].callsite_vram);
    }
    return thread->run_state;
}

void wpj2_stateful_thread_yield_current(void) {
    wpj2_continuation* cont = wpj2_cont_current();
    if (cont) wpj2_cont_yield(cont);
    if (g_dispatch_escape) longjmp(*g_dispatch_escape, 1);
}

int wpj2_stateful_thread_capture(const wpj2_stateful_thread* thread,
                                 wpj2_thread_image* image) {
    if (!thread || !image || !wpj2_cont_validate(&thread->continuation)) return 0;
    memset(image, 0, sizeof(*image));
    image->magic = WPJ2_THREAD_IMAGE_MAGIC;
    image->version = WPJ2_THREAD_IMAGE_VERSION;
    image->guest_thread = thread->guest_thread;
    image->thread_id = thread->thread_id;
    image->root_pc = thread->root_pc;
    image->run_state = (uint32_t)thread->run_state;
    context_to_image(&thread->context, &image->context);
    image->continuation_depth = thread->continuation.depth;
    memcpy(image->continuation_frames, thread->continuation.frames,
           thread->continuation.depth * sizeof(wpj2_cont_frame));
    return 1;
}

int wpj2_thread_image_validate(const wpj2_thread_image* image) {
    if (!image || image->magic != WPJ2_THREAD_IMAGE_MAGIC ||
        image->version != WPJ2_THREAD_IMAGE_VERSION ||
        image->continuation_depth > WPJ2_CONT_MAX_FRAMES ||
        (image->root_pc & 3u) != 0u || image->root_pc < 0x80000000u ||
        image->run_state < WPJ2_THREAD_READY ||
        image->run_state > WPJ2_THREAD_FAULTED)
        return 0;
    wpj2_continuation temp;
    wpj2_cont_init(&temp);
    temp.depth = image->continuation_depth;
    memcpy(temp.frames, image->continuation_frames,
           temp.depth * sizeof(wpj2_cont_frame));
    return wpj2_cont_validate(&temp);
}

int wpj2_stateful_thread_restore(wpj2_stateful_thread* thread,
                                 const wpj2_thread_image* image) {
    if (!thread || !wpj2_thread_image_validate(image)) return 0;
    wpj2_stateful_thread_init(thread, image->guest_thread, image->thread_id,
                              image->root_pc);
    thread->run_state = (wpj2_thread_run_state)image->run_state;
    image_to_context(&image->context, &thread->context);
    thread->continuation.depth = image->continuation_depth;
    memcpy(thread->continuation.frames, image->continuation_frames,
           image->continuation_depth * sizeof(wpj2_cont_frame));
    thread->continuation.cursor = 0;
    thread->continuation.yielding = 0;
    thread->continuation.resuming = image->continuation_depth != 0;
    return 1;
}
