#include "framebuffer.h"


display_list_t __scratch_y("ntsc_framebuffer") framebuffer_display_list[] = { 
	DISPLAY_LIST_BLACK_LINE, 18,
	DISPLAY_LIST_USER_RENDER_RAW, 240,
	DISPLAY_LIST_WVB,0 };

uint16_t framebuffer_line = 0;
uint8_t framebuffer[240][192];


void drawline (int x0, int y0, int x1, int y1, uint8_t color)
{
  int dx =  abs (x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs (y1 - y0), sy = y0 < y1 ? 1 : -1; 
  int err = dx + dy, e2; // error value e_xy 
  for (;;){  // loop 
	framebuffer[y0][x0] = color; // putpixel(x,y,7);
    if (x0 == x1 && y0 == y1) break;
    e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; } // e_xy+e_x > 0 
    if (e2 <= dx) { err += dx; y0 += sy; } // e_xy+e_y < 0 
  }
}


static void __not_in_flash_func(framebuffer_vblank)() {
	framebuffer_line = 0;
}

static void __not_in_flash_func(framebuffer_render)(uint line, uint video_start, uint8_t* output_buffer) {

	uint32_t* dest = (uint32_t*)(&output_buffer[video_start]);
	uint32_t* palette32 = (uint32_t*)palette;		
	uint32_t framebuffer_line_ofs = framebuffer_line * 192;
	
	for (int i=0; i<192; i++) {
		*(dest++) = palette32[framebuffer[framebuffer_line][i]];
	}
	framebuffer_line++;

}



void init_framebuffer() {

	for (int i=0; i<256; i++) {
		setPaletteRGB(i,(i & 0xE0), (i & 0x1C) << 3, (i & 0x3) << 6);
	}

	framebuffer[128][128] = 0xFF;
	set_user_render_raw(framebuffer_render);
	set_user_vblank(framebuffer_vblank);
	set_display_list(framebuffer_display_list);	
	for (int i=50; i<150; i++)
		drawline(10,i,100,i,0x1C);
	
	drawline(0,10,191,10,0xFF);
	drawline(191,10,191,229,0xFF);
	drawline(191,229,0,229,0xFF);
	drawline(0,229,0,10,0xFF);
	
}
