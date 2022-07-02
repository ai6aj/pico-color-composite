#ifndef __ATARI_NTSC_CORE_H__
#include "ntsc-video-core.h"

#define ATARI_NTSC_VERTICAL_LINE_COUNT	286

extern display_list_t atari_8bit_display_list[];
void init_atari_8bit_video_core();

void set_player_hpos(uint8_t player,uint8_t hpos);

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
