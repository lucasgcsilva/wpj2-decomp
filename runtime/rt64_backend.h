#ifndef WPJ2_RT64_BACKEND_H
#define WPJ2_RT64_BACKEND_H

#include <stdint.h>

int  rt64_backend_requested(void);
int  rt64_backend_active(void);
int  rt64_backend_submit(uint8_t* rdram, uint8_t* spmem,
                         uint32_t dl_start, uint32_t dl_size,
                         uint32_t ucode, uint32_t ucode_data);
int  rt64_backend_take_completed(void);
void rt64_backend_sync(void);
void rt64_backend_present(void);
void rt64_backend_perf_report(void);
void rt64_backend_shutdown(void);

#endif
