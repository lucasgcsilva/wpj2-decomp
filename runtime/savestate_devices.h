#ifndef WPJ2_SAVESTATE_DEVICES_H
#define WPJ2_SAVESTATE_DEVICES_H

#include <stdint.h>

/* Parte serializavel dos dispositivos hospedados. Ponteiros, handles e
 * relogios do Windows ficam deliberadamente de fora e sao recriados no load. */
#define WPJ2_RSP_SPMEM_SIZE 0x2000u
#define WPJ2_RSP_DONE_CAP 256u

typedef struct {
    uint64_t retraces;
    uint64_t polls;
    uint32_t event_mask;
    uint32_t counter_compare;
    int32_t rsp_dp_pending;
    int32_t counter_compare_armed;
} wpj2_hle_state_image;

typedef struct {
    uint8_t spmem[WPJ2_RSP_SPMEM_SIZE];
    uint8_t task_done[WPJ2_RSP_DONE_CAP];
    uint32_t status;
    uint32_t task_done_first;
    uint32_t task_done_count;
} wpj2_rsp_state_image;

typedef struct {
    uint8_t pif[64];
    uint32_t si_done;
    uint64_t si_reads;
    uint64_t si_writes;
    uint64_t pif_commands;
    uint64_t controller_polls;
    int8_t stick_x;
    int8_t stick_y;
    uint8_t reserved[6];
} wpj2_pif_state_image;

typedef struct {
    uint32_t rate;
    uint32_t ai_primary_bytes;
    uint32_t ai_secondary_bytes;
    uint32_t ai_virtual_remaining;
    uint32_t ai_virtual_phase;
    uint32_t ai_vi_phase;
    int32_t ai_compat_pending;
    int32_t ai_compat_active;
} wpj2_audio_state_image;

#endif
