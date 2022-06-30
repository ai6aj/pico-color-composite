#define USE_DISPLAY_LIST 1

#include <stdlib.h>
#include <stdio.h>

#include "ntsc-video-core.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"



int main() {
	set_sys_clock_khz(SYS_CLOCK_KHZ,true);
    stdio_init_all();
	
	multicore_launch_core1(ntsc_video_core);
	
	
	memset(&framebuffer[100][0],2,160);


	for (int i=0; i<160; i+=2) {
		drawline(0,199,i,0,2);
	}

	uint8_t tmp;
	
	uint8_t foo = 0;
	
	while(1) {
		// Dump some output to USB serial to make sure it a. works at selected clock frequency
		// and b. doesn't interfere with the display		
		float f = get_video_core_load();
		printf("Video core load %.1f%\n",f*100);
		sleep_ms(500);
		// Don't update the palette until VBLANK
		while (!in_vblank);
		setPaletteRGB(2,0,0,foo);
		foo++;
		
		// Wait until we're out of VBLANK before proceeding
		while (in_vblank);

	}
}
	
