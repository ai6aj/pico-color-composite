#ifndef __FRAMEBUFFER_H__
#define __FRAMEBUFFER_H__
#include "ntsc-video-core.h"

// extern uint8_t **framebuffer;
void init_framebuffer();
void drawline (int x0, int y0, int x1, int y1, uint8_t color);

#endif
