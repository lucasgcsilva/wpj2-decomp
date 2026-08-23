#ifndef WPJ2_RUNTIME_H
#define WPJ2_RUNTIME_H

/* Runtime de sondagem do Wonder Project J2.
 *
 * Escopo deliberado: executar o entrypoint recompilado e observar ate onde o
 * boot chega. Nao ha HLE de libultra, scheduler ou renderizador aqui; esses
 * vem depois, guiados pelo traco que este runtime produz.
 *
 * Nenhum endereco deste arquivo veio do projeto Superman: os unicos valores
 * fixos sao os do cabecalho da ROM e as convencoes do IPL3, que sao do
 * hardware e nao do jogo.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "recomp.h"
#include "video.h"


typedef struct {
    uint32_t vram;
    recomp_func_t* func;
} func_entry_t;

extern const func_entry_t g_func_table[];
extern const size_t g_func_table_size;

/* Base do espaco emulado. Indexe como g_rdram[vaddr - 0x80000000]. */
extern uint8_t* g_rdram;

/* Janela do cartucho: 0xB0000000. */
#define CART_WINDOW_OFFSET 0x30000000ull

/* Busca nao fatal: NULL quando nenhuma funcao recompilada comeca em vram.
   get_function() e a variante fatal usada por LOOKUP_FUNC. */
recomp_func_t* find_function(uint32_t vram);

/* Prepara um contexto para receber codigo recompilado.
 *
 * `f_odd` nao e opcional. Sem o modo de ponto flutuante do MIPS3, um registrador
 * impar como $f13 e a metade alta do par $f12/$f13, e o codigo gerado o alcanca
 * como `ctx->f_odd[(13-1)*2]`. O campo e um ponteiro que *o runtime* tem de
 * apontar para a metade alta de $f0; zerado, cada acesso a um registrador impar
 * vira escrita em `NULL + (n-1)*8`. Foi assim que a thread de audio morria, em
 * 0x60 - que e exatamente `(13-1)*2*4`. */
static inline void ctx_init(recomp_context* c) {
    memset(c, 0, sizeof(*c));
    c->f_odd = &c->f0.u32h;
    c->mips3_float_mode = 0;
}

/* Ultima funcao recompilada em que se entrou, ou 0 em build sem tracing. */
uint32_t trace_last_func(void);

/* Imprime as ultimas funcoes alcancadas, da mais antiga para a mais recente. */
void trace_trail(const char* label);

/* --- scheduler cooperativo (runtime/sched.c) --- */
void     sched_init(void);
void     sched_pause_current(uint8_t* rdram);
/* __osCleanupThread nunca volta ao chamador: apos remover a OSThread, o fiber
   correspondente tambem precisa deixar de poder ser retomado. */
void     sched_terminate_current(uint8_t* rdram);
uint64_t sched_switches(void);
uint64_t sched_dispatch_calls(void);
uint64_t sched_empty_dispatch(void);
uint64_t sched_count_calls(void);
uint32_t sched_count_now(void);
uint32_t sched_current(void);
void     sched_report(uint8_t* rdram);
uint64_t sched_yield_requeued(void);

/* --- substituicoes de dispositivo (runtime/hle.c) --- */
uint64_t hle_pi_transfers(void);
uint64_t hle_pi_bytes(void);
uint64_t hle_pi_rejected(void);
void     hle_dma_report(void);
void     hle_faixas_report(uint32_t consulta);
void     hle_heap_report(void);
void     hle_raster_report(void);
void     hle_texto_report(const char* prefixo);
void     hle_mesg_report(void);
uint32_t rsp_ultima_textura(void);
uint64_t hle_retraces(void);
uint64_t hle_polls(void);
void     sched_preempt(uint8_t* rdram);
int      hle_deliver_events(uint8_t* rdram);
void     hle_set_event_mask(uint32_t mask);
uint32_t hle_event_mask(void);
void     hle_set_retrace(double hz);
/* Reservado para aceleracao futura: a cadencia desta ROM permanece fixa. */
int      hle_toggle_fast_forward(void);
int      hle_fast_forward_active(void);
void     hle_clock_init(void);
void     hle_clock_shutdown(void);
void     hle_force_state2_after(uint64_t polls);
void     hle_force_active_after(uint64_t polls, int32_t index);
void     hle_force_next_scene(void);
void     hle_hold_state_8_1(void);
void     hle_hold_state_12_50(void);
void     hle_set_preempt_every_poll(int enabled);

/* --- RSP (runtime/rsp.c) --- */
uint64_t rsp_tasks(void);
uint64_t rsp_sp_dma(void);
uint64_t rsp_tasks_tipo(int tipo);
/* F5 nao captura audio em um ponto arbitrario do meio da tarefa RSP: arma a
 * proxima AList de audio, cuja entrada completa podera ser confrontada com o
 * mesmo instante capturado no Project64. */
void     rsp_request_audio_state_capture(void);
void     rsp_audio_probe_ai_buffer(uint8_t* rdram, uint32_t address, uint32_t bytes);
void     rsp_audio_probe_flush(void);
/* Telemetria de custo do rasterizador. So e observada quando um chamador pede
   um arquivo de status; nao muda a ordem nem o conteudo das listas RDP. */
uint64_t rsp_gfx_raster_last_us(void);
uint64_t rsp_gfx_raster_peak_us(void);
uint64_t rsp_gfx_raster_total_us(void);
uint64_t rsp_gfx_raster_samples(void);
uint64_t rsp_acmd_last_us(void);
uint64_t rsp_acmd_peak_us(void);
uint64_t rsp_acmd_total_us(void);
uint64_t rsp_acmd_samples(void);
uint64_t rsp_camera_discarded_vertices(void);
uint64_t rsp_culled_triangles(void);
uint32_t rsp_prim_color(void);
uint32_t rsp_env_color(void);
uint32_t rsp_othermode_l(void);
int      rsp_last_gfx_had_triangles(void);
/* Resumo da ultima lista grafica: usado pela captura F5 para localizar
 * desaparecimentos temporarios de geometria sem registrar cada triangulo. */
uint64_t rsp_last_gfx_index(void);
uint32_t rsp_last_gfx_tri_received(void);
uint32_t rsp_last_gfx_tri_drawn(void);
uint32_t rsp_last_gfx_tri_cull_front(void);
uint32_t rsp_last_gfx_tri_cull_back(void);
uint32_t rsp_last_gfx_tri_camera_rejected(void);
uint64_t rsp_last_gfx_z_accepted(void);
uint64_t rsp_last_gfx_z_rejected(void);
/* Estado visual de uma troca de framebuffer detectada pela própria lista RDP:
 * 0 normal, 1 conservar quadro frontal, 2 preto temporário. */
int      rsp_transition_presentation_mode(void);
uint64_t rsp_alpha_texrects(void);
uint32_t rsp_alpha_rect_x0(void);
uint32_t rsp_alpha_rect_y0(void);
uint32_t rsp_alpha_rect_x1(void);
uint32_t rsp_alpha_rect_y1(void);
void     rsp_gfx_report(const char* prefixo);
void     rsp_tlut_report(void);
int      rsp_num_alvos(void);
uint32_t rsp_alvo(int i);
void     rsp_set_prefix(const char* p);
void     rsp_set_f3d_matrix_mode(int enabled);
void     rsp_set_f3d_z_mode(int enabled);
void     rsp_set_f3d_cull_mode(int enabled);
void     rsp_set_f3d_matrix_conventional(int enabled);
void     rsp_set_tmem_interleave(int enabled);
void     rsp_set_rdp_combine_mode(int enabled);
void     rsp_set_debug(int enabled);
void     rsp_cycle_audio_voice(void);
uint64_t rsp_acmd_run(void);
uint64_t rsp_bytes_saved(void);
uint32_t rsp_acmd_load_peak(void);
uint32_t rsp_acmd_mix_peak(void);
uint32_t rsp_acmd_save_peak(void);
uint64_t rsp_acmd_opcode(uint32_t opcode);
int      rsp_take_task_done(void);
int      rsp_peek_task_done(void);

/* --- Saida de audio hospedada (runtime/audio.c) --- */
void     audio_init(void);
void     audio_shutdown(void);
void     audio_set_frequency(uint32_t hz);
void     audio_queue_ai_buffer(uint8_t* rdram, uint32_t address, uint32_t bytes);
uint32_t audio_ai_length(void);
uint32_t audio_ai_status(void);
uint64_t audio_buffers_queued(void);
uint64_t audio_bytes_queued(void);
uint32_t audio_peak_sample(void);
int      audio_ai_done_pending(void);
void     audio_take_ai_done(void);

/* Microcodigo de audio gerado pelo RSPRecomp. Continua opt-in enquanto a
 * comparacao de PCM com o Project64 nao estiver concluida. */
int      wpj2_native_audio_rsp(uint8_t* rdram, uint8_t* spmem);
/* Entrada de teste: executa apenas o HLE C da Audio ABI, sem tentar o
 * microcódigo recompilado. Não é chamada pelo executável normal. */
int      wpj2_hle_audio_rsp(uint8_t* rdram, uint8_t* spmem);

/* --- PIF (runtime/pif.c) --- */
uint64_t pif_si_reads(void);
uint64_t pif_si_writes(void);
uint64_t pif_commands(void);
uint64_t pif_controller_polls(void);
int      pif_si_done_pending(void);
void     pif_take_si_done(void);
/* Sai a cada segundo pelo relogio, nao por leitura: continua reportando mesmo
   depois que o jogo para de perguntar pelo controle. */
void     pif_relatorio_periodico(void);
void     pif_set_buttons(uint16_t b);
void     pif_set_script(const char* s);
void     pif_set_poll_script(const char* s);
void     pif_set_stick(const char* value);

#endif
