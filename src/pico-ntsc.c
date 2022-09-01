#define USE_DISPLAY_LIST 1

#include <stdlib.h>
#include <stdio.h>

#include "ntsc-video-core.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "framebuffer.h"


int __not_in_flash_func(main)() {
	set_sys_clock_khz(SYS_CLOCK_KHZ,true);
    stdio_init_all();
	
	init_framebuffer();	
	multicore_launch_core1(ntsc_video_core);
	
	
//	memset(&framebuffer[100][0],2,160);


/*	for (int i=0; i<160; i+=2) {
		drawline(0,199,i,0,0x1C);
	} */

	for (int i=0; i<255; i++) {
		setPaletteRGB(i,(i & 0xE0), (i & 0x1C) << 3, (i & 3) << 6);
	}

	uint8_t tmp;
	
	uint8_t foo = 0;
	
	uint8_t hpos = 63;

/*	
	for (int i=0; i<239; i++) {
		drawline (0, i, 191, i, i);
	}
	
	for (int i=100; i<200; i++) {
		drawline (100,i,200,i,0xC0);
	}
	
	drawline(0,0,383,239,3);
*/

	while(1) {
		// Dump some output to USB serial to make sure it a. works at selected clock frequency
		// and b. doesn't interfere with the display		
		float f = get_video_core_load();
		printf("Video core load %.1f%\n",f*100);
		// sleep_ms(500);
		// Don't update the palette until VBLANK
		while (!in_vblank);
//		setPaletteRGB(0,(foo & 0xE0), (foo & 0x1C) << 3, (foo & 2) << 6);
		foo++;
//		vscrol++;
		//hscrol+=2;
		// Wait until we're out of VBLANK before proceeding
		while (in_vblank);
//		while (in_vblank);
//		set_player_hpos(0,hpos++);
	}
}
	
