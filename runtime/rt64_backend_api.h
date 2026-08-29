#ifndef WPJ2_RT64_BACKEND_API_H
#define WPJ2_RT64_BACKEND_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wpj2_rt64_config {
    void* window;
    uint32_t window_thread_id;
    uint8_t* rdram;
    uint8_t* dmem;
    uint8_t* imem;
    uint32_t* vi_status;
    uint32_t* vi_origin;
    uint32_t* vi_width;
    uint32_t* vi_intr;
    uint32_t* vi_current;
    uint32_t* vi_timing;
    uint32_t* vi_v_sync;
    uint32_t* vi_h_sync;
    uint32_t* vi_leap;
    uint32_t* vi_h_start;
    uint32_t* vi_v_start;
    uint32_t* vi_v_burst;
    uint32_t* vi_x_scale;
    uint32_t* vi_y_scale;
} wpj2_rt64_config;

typedef int  (__cdecl *wpj2_rt64_init_fn)(const wpj2_rt64_config* config);
typedef int  (__cdecl *wpj2_rt64_submit_fn)(uint32_t dl_start,
                                             uint32_t dl_size,
                                             uint32_t ucode,
                                             uint32_t ucode_data);
typedef void (__cdecl *wpj2_rt64_present_fn)(void);
typedef int  (__cdecl *wpj2_rt64_take_completed_fn)(void);
typedef void (__cdecl *wpj2_rt64_sync_fn)(void);
typedef void (__cdecl *wpj2_rt64_shutdown_fn)(void);
typedef const char* (__cdecl *wpj2_rt64_error_fn)(void);

#ifdef __cplusplus
}
#endif

#endif
