#ifndef WPJ2_CONTINUATION_H
#define WPJ2_CONTINUATION_H

#include <stddef.h>
#include <stdint.h>
#include "recomp.h"

/* Pilha serializavel de chamadas recompiladas.
 *
 * Uma frame identifica a funcao hospedeira e o ponto imediatamente anterior
 * a uma chamada que pode, transitivamente, ceder ao scheduler. Ao abandonar a
 * pilha C, as frames permanecem no estado da OSThread. No proximo despacho,
 * cada funcao consome sua frame e salta para o callsite correspondente,
 * reconstruindo a cadeia sem copiar uma fiber do sistema operacional. */
#define WPJ2_CONT_MAX_FRAMES 128u
#define WPJ2_CONT_FORMAT_VERSION 4u

typedef struct {
    uint32_t function_vram;
    uint32_t callsite_vram;
    uint64_t local_hi;
    uint64_t local_lo;
    uint64_t local_result;
    int32_t local_c1cs;
    uint32_t call_target_vram;
    uint32_t stack_pointer;
    /* A pilha guest conserva variaveis locais, mas nao os registradores
     * volateis usados para montar os argumentos de uma chamada. Uma retomada
     * precisa reproduzir o contexto do pai no instante do JAL; caso contrario
     * uma chamada bloqueante recebe os registradores deixados pelo filho mais
     * profundo. f_odd fica de fora por ser ponteiro de host. */
    gpr gpr_values[32];
    fpr fpr_values[32];
    uint64_t context_hi;
    uint64_t context_lo;
    uint32_t status_reg;
    uint8_t mips3_float_mode;
    uint8_t reserved[3];
} wpj2_cont_frame;

typedef struct {
    uint32_t version;
    uint32_t depth;
    wpj2_cont_frame frames[WPJ2_CONT_MAX_FRAMES];

    /* Campos transitorios: zerados/reconstruidos depois de carregar. */
    uint32_t cursor;
    uint32_t yielding;
    uint32_t resuming;
} wpj2_continuation;

void wpj2_cont_init(wpj2_continuation* cont);
void wpj2_cont_begin_dispatch(wpj2_continuation* cont);

/* Retorna 1 quando a funcao esta sendo reconstruida. Nesse caso callsite
 * recebe o endereco salvo e frame_index identifica a frame a remover quando a
 * chamada terminar normalmente. */
int wpj2_cont_enter(wpj2_continuation* cont, uint32_t function_vram,
                    uint32_t* callsite, uint32_t* frame_index);
int wpj2_cont_enter_ex(wpj2_continuation* cont, uint32_t function_vram,
                       uint32_t* callsite, uint32_t* frame_index,
                       uint64_t* hi, uint64_t* lo, uint64_t* result,
                       int* c1cs, uint32_t* call_target_vram,
                       recomp_context* context);

/* Registra uma chamada nova. A geracao futura insere esta operacao logo antes
 * da chamada C emitida pelo N64Recomp. */
int wpj2_cont_before_call(wpj2_continuation* cont, uint32_t function_vram,
                          uint32_t callsite_vram, uint32_t* frame_index);
int wpj2_cont_before_call_ex(wpj2_continuation* cont, uint32_t function_vram,
                             uint32_t callsite_vram, uint64_t hi, uint64_t lo,
                             uint64_t result, int c1cs, uint32_t call_target_vram,
                             const recomp_context* context,
                             uint32_t* frame_index);

/* Remove a frame apenas quando o filho retornou sem cessao. */
int wpj2_cont_after_call(wpj2_continuation* cont, uint32_t frame_index);

void wpj2_cont_yield(wpj2_continuation* cont);
int wpj2_cont_yielding(const wpj2_continuation* cont);

/* Valida somente a parte persistente. Pode ser usada antes de aceitar um
 * snapshot vindo de arquivo. */
int wpj2_cont_validate(const wpj2_continuation* cont);

/* Adaptador usado pelo C gerado. O dispatcher associa a continuation da
 * OSThread antes de entrar no seu PC raiz; chamadas aninhadas acessam a mesma
 * instância sem mudar a assinatura padrão do N64Recomp. */
void wpj2_cont_bind(wpj2_continuation* cont);
wpj2_continuation* wpj2_cont_current(void);
int wpj2_cont_enter_current(uint32_t function_vram, uint32_t* callsite,
                            uint32_t* frame_index);
int wpj2_cont_enter_current_ex(uint32_t function_vram, uint32_t* callsite,
                               uint32_t* frame_index, uint64_t* hi,
                               uint64_t* lo, uint64_t* result, int* c1cs,
                               uint32_t* call_target_vram,
                               recomp_context* context);
int wpj2_cont_before_call_current(uint32_t function_vram,
                                  uint32_t callsite_vram,
                                  uint32_t* frame_index);
int wpj2_cont_before_call_current_ex(uint32_t function_vram,
                                     uint32_t callsite_vram, uint64_t hi,
                                     uint64_t lo, uint64_t result, int c1cs,
                                     uint32_t call_target_vram,
                                     const recomp_context* context,
                                     uint32_t* frame_index);
int wpj2_cont_after_call_current(uint32_t frame_index);
int wpj2_cont_yielding_current(void);

#endif
