#ifndef __ATARI_NTSC_CORE_H__
#include "ntsc-video-core.h"

#define ATARI_NTSC_VERTICAL_LINE_COUNT	286

extern display_list_t atari_8bit_display_list[];
void init_atari_8bit_video_core();
uint8_t antic_read_memory(uint16_t addr);

void set_player_hpos(uint8_t player,uint8_t hpos);

void set_player_data(uint8_t player,uint8_t data);

void set_missile_data(uint8_t data);


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

#endif
