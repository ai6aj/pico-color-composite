#ifndef __ATARI_NTSC_CORE_H__
#include "ntsc-video-core.h"

#define ATARI_NTSC_VERTICAL_LINE_COUNT	286

#ifdef __cplusplus
extern "C" {
#endif

extern display_list_t atari_8bit_display_list[];
void init_atari_8bit_video_core();

uint8_t antic_read_memory(uint16_t addr);

void set_antic_dma_width(int width);

void set_player_hpos(uint8_t player,uint8_t hpos);
void set_missile_hpos(uint8_t player,uint8_t hpos);
void set_player_width(uint8_t player,uint8_t hpos);
void set_missile_widths(uint8_t width);

void set_antic_hscrol(uint8_t value);
void set_antic_vscrol(uint8_t value);


void set_player_data(uint8_t player,uint8_t data);

void set_missile_data(uint8_t data);

void setAtariColorRegister(int num,uint8_t palette[][4],int palette_num);
void set_color_register(uint8_t num,uint8_t val,int cycle);

// void antic_set_block_ram(int block,uint8_t* where);

void set_next_antic_mode(uint8_t mode);
void set_antic_graphics_ptr(uint8_t* ptr);
void set_next_antic_character_ptr(const uint8_t* ptr);

// This renders P/M graphics, sets the collision registers,
// and writes the whole thing into the frame buffer
void process_antic_video_line(uint8_t* user_line, uint8_t hscrol);

// This will be reset to 0 by the video core every HSYNC
extern volatile uint8_t atari_hsync_flag;
extern volatile uint8_t atari_vblank_flag;

/*
	TODO
For video:
	void set_ANTIC_register(num, value, when)
	void set_GTIA_register(num, value, when)
	
	Where 'when' is the color clock that the 
	value should be applied
	

For sound:
	void set_POKEY_register(num, value, when) */

#ifdef __cplusplus
}
#endif

#endif
