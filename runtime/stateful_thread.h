#ifndef WPJ2_STATEFUL_THREAD_H
#define WPJ2_STATEFUL_THREAD_H

#include <stdint.h>

#include "continuation.h"
#include "recomp.h"

#define WPJ2_THREAD_IMAGE_VERSION 3u
#define WPJ2_THREAD_IMAGE_MAGIC 0x57505448u /* WPTH */

typedef enum {
    WPJ2_THREAD_READY = 1,
    WPJ2_THREAD_RUNNING = 2,
    WPJ2_THREAD_WAITING = 3,
    WPJ2_THREAD_FINISHED = 4,
    WPJ2_THREAD_FAULTED = 5
} wpj2_thread_run_state;

/* recomp_context contém f_odd, um ponteiro para dentro dele mesmo. Um ponteiro
 * de host nunca pode entrar no arquivo; a imagem enumera apenas dados guest. */
typedef struct {
    gpr gpr_values[32];
    fpr fpr_values[32];
    uint64_t hi;
    uint64_t lo;
    uint32_t status_reg;
    uint8_t mips3_float_mode;
    uint8_t reserved[3];
} wpj2_context_image;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t guest_thread;
    uint32_t thread_id;
    uint32_t root_pc;
    uint32_t run_state;
    wpj2_context_image context;
    uint32_t continuation_depth;
    wpj2_cont_frame continuation_frames[WPJ2_CONT_MAX_FRAMES];
} wpj2_thread_image;

typedef struct {
    uint32_t guest_thread;
    uint32_t thread_id;
    uint32_t root_pc;
    wpj2_thread_run_state run_state;
    recomp_context context;
    wpj2_continuation continuation;
} wpj2_stateful_thread;

void wpj2_stateful_thread_init(wpj2_stateful_thread* thread,
                               uint32_t guest_thread, uint32_t thread_id,
                               uint32_t root_pc);

/* Executa o PC raiz. O código transformado reconstrói a cadeia gravada.
 * Retorna o novo wpj2_thread_run_state. */
wpj2_thread_run_state wpj2_stateful_thread_dispatch(
    wpj2_stateful_thread* thread, uint8_t* rdram, recomp_func_t* root);

/* Abandona imediatamente a pilha C da rodada atual. O salto e estritamente
 * transitorio: a parte persistente ja esta em continuation e nenhum endereco
 * do host entra no snapshot. */
void wpj2_stateful_thread_yield_current(void);

int wpj2_stateful_thread_capture(const wpj2_stateful_thread* thread,
                                 wpj2_thread_image* image);
int wpj2_stateful_thread_restore(wpj2_stateful_thread* thread,
                                 const wpj2_thread_image* image);
int wpj2_thread_image_validate(const wpj2_thread_image* image);

#endif
