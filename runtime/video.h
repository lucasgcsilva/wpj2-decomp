#ifndef WPJ2_VIDEO_H
#define WPJ2_VIDEO_H

#include <stdint.h>

/* Apresentacao Win32 minima para o prototipo visual. Ela nao substitui RDP/VI:
 * apenas mostra, em uma janela, o framebuffer que o renderer recompilado ja
 * escreveu na RDRAM. */
int  video_init(void);
void video_present(uint8_t* rdram, uint32_t origin, uint32_t width,
                   uint32_t height, uint32_t format, uint32_t vi_status);
void video_shutdown(void);
int  video_quit_requested(void);

#endif
