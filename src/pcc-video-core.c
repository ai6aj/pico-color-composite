#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include <string.h>
#include <math.h>

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/dma.h"
#include "pcc-video-core.h"
#include "pcc-video-core.pio.h"


int irq_count = 0;

// Enables colorburst if true, set to 0 to get
// 640+ horizontal B&W pixels on some TVs!
int do_color = 1;






/* --------------------------------
 SIGNAL GENERATOR
   -------------------------------- */
volatile int pcc_in_vblank = 0;

uint dma_channel;
dma_channel_config dma_channel_cfg;

volatile void *dma_write_addr;



uint8_t vblank_line[LINE_WIDTH+1];

#ifdef USE_PAL
uint8_t pal_broad_and_short_sync[LINE_WIDTH+1];
uint8_t pal_short_and_broad_sync[LINE_WIDTH+1];

uint8_t pal_short_sync_and_black[LINE_WIDTH+1];
uint8_t pal_black_and_short_sync[LINE_WIDTH+1];

uint8_t pal_broad_sync[LINE_WIDTH+1];
uint8_t pal_short_sync[LINE_WIDTH+1];

#else
uint8_t equalization_line[LINE_WIDTH+1];

uint8_t half_black_half_equalization_line[LINE_WIDTH+1];
uint8_t half_equalization_half_black_line[LINE_WIDTH+1];
uint8_t half_equalization_half_vblank_line[LINE_WIDTH+1];					
uint8_t half_vblank_half_equalization_line[LINE_WIDTH+1];					

#endif


uint8_t black_line[LINE_WIDTH+1];

uint8_t __scratch_x("ntsc") pingpong_line_0[LINE_WIDTH];
uint8_t __scratch_y("ntsc") pingpong_line_1[LINE_WIDTH];
 
uint8_t* pingpong_lines[] = { pingpong_line_0, pingpong_line_1 };


void make_vsync_lines() {
	int ofs = 0;
	memset(vblank_line,BLANKING_VAL,LINE_WIDTH);
	
	// PAL and NTSC share a common "broad pulse" line that 
	// signifies vertical sync.
	memset(vblank_line,SYNC_VAL,VBLANK_CLOCKS); 		
	memset(&vblank_line[LINE_WIDTH/2],SYNC_VAL,VBLANK_CLOCKS);

	#ifdef USE_NTSC
	memset(equalization_line,BLANKING_VAL,LINE_WIDTH);
	memset(equalization_line, SYNC_VAL,SYNC_TIP_CLOCKS/2);
	memset(&equalization_line[LINE_WIDTH/2], SYNC_VAL,SYNC_TIP_CLOCKS/2);
	
	// Do the HSYNC pulse.  The front porch has been drawn
	// by the previous DMA transfer, now provide 4.7uS
	// of SYNC pulse
	memset(half_black_half_equalization_line, BLANKING_VAL, LINE_WIDTH);
	memset(half_black_half_equalization_line, SYNC_VAL,SYNC_TIP_CLOCKS);
	memset(&half_black_half_equalization_line[LINE_WIDTH/2], SYNC_VAL,SYNC_TIP_CLOCKS/2);
	
	memset(half_equalization_half_black_line, BLANKING_VAL, LINE_WIDTH);
	memset(half_equalization_half_black_line, SYNC_VAL,SYNC_TIP_CLOCKS/2);

	memset(half_equalization_half_vblank_line, BLANKING_VAL, LINE_WIDTH);
	memset(half_equalization_half_vblank_line, SYNC_VAL,SYNC_TIP_CLOCKS/2);
	memset(&half_equalization_half_vblank_line[LINE_WIDTH/2], SYNC_VAL,VBLANK_CLOCKS); 		

	memset(half_vblank_half_equalization_line, BLANKING_VAL, LINE_WIDTH);
	memset(half_vblank_half_equalization_line, SYNC_VAL,VBLANK_CLOCKS); 		
	memset(&half_vblank_half_equalization_line[LINE_WIDTH/2], SYNC_VAL,SYNC_TIP_CLOCKS/2); 		
	#endif 
	
	#ifdef USE_PAL
	memset(pal_broad_and_short_sync,BLANKING_VAL,LINE_WIDTH);
	memset(pal_broad_and_short_sync,SYNC_VAL,VBLANK_CLOCKS);
	memset(&pal_broad_and_short_sync[LINE_WIDTH/2],SYNC_VAL,SHORT_SYNC_CLOCKS); 		

	memset(pal_short_and_broad_sync,BLANKING_VAL,LINE_WIDTH);
	memset(pal_short_and_broad_sync,SYNC_VAL,SHORT_SYNC_CLOCKS);
	memset(&pal_short_and_broad_sync[LINE_WIDTH/2],SYNC_VAL,VBLANK_CLOCKS); 		

	memset(pal_short_sync,BLANKING_VAL,LINE_WIDTH); 		
	memset(pal_short_sync,SYNC_VAL,SHORT_SYNC_CLOCKS); 		
	memset(&pal_short_sync[LINE_WIDTH/2],SYNC_VAL,SHORT_SYNC_CLOCKS); 		

	memset(pal_broad_sync,BLANKING_VAL,LINE_WIDTH); 		
	memset(pal_broad_sync,SYNC_VAL,VBLANK_CLOCKS); 		
	memset(&pal_broad_sync[LINE_WIDTH/2],SYNC_VAL,VBLANK_CLOCKS); 		

	memset(pal_short_sync_and_black,BLANKING_VAL,LINE_WIDTH);
	memset(pal_short_sync_and_black,SYNC_VAL,SHORT_SYNC_CLOCKS);

	memset(pal_black_and_short_sync,BLANKING_VAL,LINE_WIDTH);
	memset(pal_black_and_short_sync,SYNC_VAL,SYNC_TIP_CLOCKS);
	memset(&pal_black_and_short_sync[LINE_WIDTH/2],SYNC_VAL,SHORT_SYNC_CLOCKS);
	#endif

}

void make_color_burst(uint8_t* line, float cb_phase) {
	uint8_t c1 = COLOR_BURST_HI_VAL;
	uint8_t c2 = COLOR_BURST_LO_VAL;
	
	float cb_scale = (c1-c2);
	
	// PALFIX
	// Is this correct for PAL?
	float phase = (COLOR_BURST_PHASE_DEGREES+cb_phase)/180 * 3.14159;	// Starting phase.
	#ifdef USE_PAL
	for (int i=0; i<10; i++) {
		for (int n=0; n<SAMPLES_PER_CLOCK; n++) {
			line[COLOR_BURST_START+i*SAMPLES_PER_CLOCK+n] = BLANKING_VAL + (int)((cb_scale * sin(phase))+0.5);
			phase += 3.14159*2/SAMPLES_PER_CLOCK;
		}
	}	
	#else
	for (int i=0; i<10; i++) {
		for (int n=0; n<SAMPLES_PER_CLOCK; n++) {
			line[COLOR_BURST_START+i*SAMPLES_PER_CLOCK+n] = BLANKING_VAL + (int)((cb_scale * sin(phase))+0.5);
			phase += 3.14159*2/SAMPLES_PER_CLOCK;
		}
	}
	#endif
}

/**
	Fill a linebuffer with skeleton HSYNC, colorburst, and black signals.
	
	do_colorburst			If true, insert the colorburst
	use_alternate_phase		If true, invert the colorburst phase 180 degrees
	
**/
void make_normal_line(uint8_t* dest, int do_colorburst, int use_alternate_phase) {
	// Set everything to the blanking level
	memset(dest,BLACK_LEVEL,LINE_WIDTH);
	
	// Do the HSYNC pulse.  The front porch has been drawn
	// by the previous DMA transfer, now provide 4.7uS
	// of SYNC pulse
	memset(dest,SYNC_VAL,SYNC_TIP_CLOCKS);
	
	// Fill in colorburst signal if desired
	if (do_colorburst) {
		if (use_alternate_phase)
			make_color_burst(dest,180);
		else
			make_color_burst(dest,0);			
	}
}

int line = 0;
int frame = 0;
uint8_t*	next_dma_line = vblank_line;

int frcount = 0;

pcc_user_render_raw_func_t	*user_render_raw_func = NULL;
pcc_user_vblank_func_t *user_vblank_func = NULL;
pcc_user_new_line_func_t *user_new_line_func = NULL;

void pcc_set_user_render_raw(pcc_user_render_raw_func_t *f) {
	user_render_raw_func = f;
}

void pcc_set_user_vblank(pcc_user_vblank_func_t *f) {
	user_vblank_func = f;
}

void pcc_set_user_new_line_callback(pcc_user_new_line_func_t *f) {
	user_new_line_func = f;
}

volatile int next_dma_width = LINE_WIDTH;

/*---------------------------------------
	PAL signal generation.
  ---------------------------------------*/

#ifdef USE_PAL
static void __not_in_flash_func(pal_video_dma_handler)(void) {
	
    dma_channel_configure(dma_channel, &dma_channel_cfg,
        dma_write_addr,              // Destination pointer
        next_dma_line,                       // Source pointer
        next_dma_width,          // Number of transfers
        true                        // Start flag (true = start immediately)
    );
	
//    dma_channel_set_read_addr(dma_channel, next_dma_line, true);
    dma_hw->ints0 = 1 << dma_channel;		

	// User line callback goes here so we don't get a bunch
	// of jitter in audio.
	// if (user_new_line_func != NULL) { user_new_line_func(line); }


	// We should ALWAYS set the read address to whatever we decided it should be
	// last time, unless we're in vblank.  Then we determine what the next DMA line is.
	// This way we don't get an extra line(s) when switching between black lines and 
	// framebuffer lines.

/*	if (line == 1 || line == 264) {
		in_vblank = 1;
		if (user_vblank_func != NULL) {
			user_vblank_func();
		}
	} */

	// Lines 1-2: Broad Sync
	if (line < 3) {
		pcc_in_vblank = 1;
		next_dma_line = pal_broad_sync;		
	}
	
	// Line 3 : Broad, then Short Sync
	else if (line == 3) {
		next_dma_line = pal_broad_and_short_sync;
	}
	
	// Lines 4-5 : Short Sync
	else if (line < 6) {
		next_dma_line = pal_short_sync;
	}
	
	// Lines 6-310: Video
	else if (line < 311) {
		pcc_in_vblank = 0;
		if (user_render_raw_func) {
			next_dma_line = pingpong_lines[line & 1];
			user_render_raw_func((line-6)*2,VIDEO_START,pingpong_lines[line & 1]);				
		}
		else
			next_dma_line = black_line;
	}

	// Lines 311-312: Short Sync
	else if (line < 313) {		
		pcc_in_vblank = 1;
		next_dma_line = pal_short_sync;		
	}

	// Line 313: Short, then Broad Sync
	else if (line == 313) {
		next_dma_line = pal_short_and_broad_sync;
	}

	// Line 314-315: Broad Sync
	else if (line < 316) {
		next_dma_line = pal_broad_sync;					
	}

	// Line 316-317: Short Sync
	else if (line < 318) {
		next_dma_line = pal_short_sync;				
	}
	
	// Line 318: Short Sync, then black
	else if (line == 318) {
		next_dma_line = pal_short_sync_and_black;
	}
	
	// Line 319-622: Video
	else if (line < 623) {
		pcc_in_vblank = 0;
		if (user_render_raw_func) {
			next_dma_line = pingpong_lines[line & 1];			
			user_render_raw_func((line-319)*2+1,VIDEO_START,pingpong_lines[line & 1]);				
		}
		else
			next_dma_line = black_line;
		
		//user_render_raw_func(line >> 1,VIDEO_START,pingpong_lines[line & 1]);		
	}
	
	// Line 623: Black, then Short Sync
	else if (line == 623) {
		next_dma_line = pal_black_and_short_sync;
	}
	
	// Line 624-625: Short Sync
	else {
		next_dma_line = pal_short_sync;		
	}

	line++;
	if (line > 625) { line = 1; }	
}
#endif


/*---------------------------------------
	NTSC signal generation.
  ---------------------------------------*/
#ifdef USE_NTSC
static void __not_in_flash_func(ntsc_video_dma_handler)(void) {
	
    dma_channel_configure(dma_channel, &dma_channel_cfg,
        dma_write_addr,              // Destination pointer
        next_dma_line,                       // Source pointer
        next_dma_width,          // Number of transfers
        true                        // Start flag (true = start immediately)
    );
	
//    dma_channel_set_read_addr(dma_channel, next_dma_line, true);
    dma_hw->ints0 = 1 << dma_channel;		

	// User line callback goes here so we don't get a bunch
	// of jitter in audio.
	// if (user_new_line_func != NULL) { user_new_line_func(line); }


	// We should ALWAYS set the read address to whatever we decided it should be
	// last time, unless we're in vblank.  Then we determine what the next DMA line is.
	// This way we don't get an extra line(s) when switching between black lines and 
	// framebuffer lines.

	if (line == 1 || line == 264) {
		pcc_in_vblank = 1;
		if (user_vblank_func != NULL) {
			user_vblank_func();
		}
	}

	// Lines 1-3: Pre-Equalizing
	if (line < 4) {
		next_dma_line = equalization_line;		
	}
	
	// Lines 4,5,6 : VBLANK
	else if (line < 7) {
		next_dma_line = vblank_line;		
	}
	
	// Lines 7,8,9 : Post-Equalizing
	else if (line < 10) {
		next_dma_line = equalization_line;
	}
	
	// Lines 10-21: Black
	else if (line < 21 ) {
		next_dma_line = black_line;		
	}

	// Lines 21-262: Video
	else if (line < 263) {
		pcc_in_vblank = 0;
		if (user_render_raw_func) {
			next_dma_line = pingpong_lines[line & 1];
			user_render_raw_func((line-21)*2+1,VIDEO_START,pingpong_lines[line & 1]);				
		}
		else
			next_dma_line = black_line;
	}

	// Line 263: Half-line, half-equalizing
	else if (line == 263) {
		next_dma_line = half_black_half_equalization_line;		
	}

	// Line 264,265: Pre-Equalizing
	else if (line < 266) {
		next_dma_line = equalization_line;		
	}

	// Line 266: Half-equalizing, half-VBLANK 
	else if (line == 266) {
		next_dma_line = half_equalization_half_vblank_line;					
	}

	// Line 267,268: VBLANK
	else if (line < 269) {
		next_dma_line = vblank_line;				
	}
	
	// Line 269: half VBLANK, half-equalizing
	else if (line == 269) {
		next_dma_line = half_vblank_half_equalization_line;					
	}
	
	// Line 270,271: Post-Equalizing
	else if (line < 272) {
		next_dma_line = equalization_line;			
	}
	
	// Line 272: half-equalizing, half-black
	else if (line == 272) {
		next_dma_line = half_equalization_half_black_line;				
	}
	
	// Lines 274-284: Black
	// Technically line 283 is a half-video line
	// but we ignore that.
	else if (line < 284) {
		next_dma_line = black_line;		
	}
	
	// Lines 284+: Video
	else {
		// next_dma_line = black_line;
		pcc_in_vblank = 0;
		if (user_render_raw_func) {
			next_dma_line = pingpong_lines[line & 1];			
			user_render_raw_func((line-283)*2,VIDEO_START,pingpong_lines[line & 1]);
		}
		else 
			next_dma_line = black_line;
	}

	line++;
	if (line > 525) { line = 1; }	
}
#endif


void pcc_enable_colorburst(int value) {
	do_color = value ? 1 : 0;
	make_normal_line(black_line,do_color,0);
	make_normal_line(pingpong_lines[0],do_color,0);
	#ifdef USE_PAL
		make_normal_line(pingpong_lines[1],do_color,1);
	#else
		make_normal_line(pingpong_lines[1],do_color,0);
	#endif	
}

void init_video_lines() {
	// Initialize video_line to alternating 1s and 2s
	make_vsync_lines();
	
	// Color is on by default.
	pcc_enable_colorburst(1);
}

void start_video(PIO pio, uint sm, uint offset, uint pin, uint num_pins) {

	init_video_lines();
			
	// Initialize the PIO program
    ntsc_composite_program_init(pio, sm, offset, FIRST_GPIO_PIN,DAC_BITS );
	
	// Configure DMA
	
	// Grab an unused DMA channel
	dma_channel = dma_claim_unused_channel(true);
	
    pio_sm_clear_fifos(pio, sm);
    
	dma_channel_cfg = dma_channel_get_default_config(dma_channel);
    
	// DMA transfers exec 8 bits at a time
	channel_config_set_transfer_data_size(&dma_channel_cfg, DMA_SIZE_8);
	
	// DMA transfers increment the address
    channel_config_set_read_increment(&dma_channel_cfg, true);
	
	// DMA transfers use DREQ
    channel_config_set_dreq(&dma_channel_cfg, pio_get_dreq(pio, sm, true));
	

	dma_write_addr = &pio->txf[sm];
	
    dma_channel_configure(dma_channel, &dma_channel_cfg,
        dma_write_addr,              // Destination pointer
        NULL,                       // Source pointer
        LINE_WIDTH,          // Number of transfers
        true                        // Start flag (true = start immediately)
    );
    	
	dma_channel_set_irq0_enabled(dma_channel, true);
	#ifdef USE_PAL
		irq_set_exclusive_handler(DMA_IRQ_0, pal_video_dma_handler);
	#else
		irq_set_exclusive_handler(DMA_IRQ_0, ntsc_video_dma_handler);
	#endif
    irq_set_enabled(DMA_IRQ_0, true);

	// Set the clock divisor
	// This may be subtly wrong, as we should really be matching the # of color clocks
	// we want to spit out to ensure we're perfectly matched to our luma and chroma 
	// signal
	float PIO_clkdiv = (SYS_CLOCK_KHZ*1000) / (COLORBURST_FREQ*SAMPLES_PER_CLOCK);
	
	uint16_t div_int = (uint16_t)PIO_clkdiv;
	uint8_t div_frac = 0;
     if (div_int != 0) {
         div_frac = (uint8_t)((PIO_clkdiv - (float)div_int) * (1u << 8u));
     }

	pio->sm[sm].clkdiv = (((uint)div_frac) << PIO_SM0_CLKDIV_FRAC_LSB) |
             (((uint)div_int) << PIO_SM0_CLKDIV_INT_LSB);
	// Start the state machine
    pio_sm_set_enabled(pio, sm, true);
}
	

/* ------------------------------------
	Main thread
   -------------------------------------*/

pcc_user_video_core_loop_func_t *user_video_core_loop_func = NULL; 

volatile uint32_t current_idle_count = -1;
volatile uint32_t unloaded_idle_count = 0;
float pcc_get_video_core_load() {
	if (current_idle_count == -1) return -1;
	return 1-(float)(current_idle_count)/(float)(unloaded_idle_count);
}

void pcc_set_user_video_core_loop(pcc_user_video_core_loop_func_t f) {
	user_video_core_loop_func = f;
}


void pcc_video_core() {
	
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &ntsc_composite_program);

	
    start_video(pio, 0, offset, FIRST_GPIO_PIN, DAC_BITS);
	
	uint32_t counter = 0;

	while (!pcc_in_vblank);
	while (pcc_in_vblank);
	while (!pcc_in_vblank) counter++;
	unloaded_idle_count = counter;

	if (user_video_core_loop_func != NULL) {
		// Set user_video_core_loop to supply your
		// own code to run on Core 1.
		user_video_core_loop_func();
	}
		
	while (1) {
			while (!pcc_in_vblank) counter++;
			current_idle_count = counter;
			counter = 0;		
			while (pcc_in_vblank);
	}
}
