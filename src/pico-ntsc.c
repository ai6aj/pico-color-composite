/**

	TODO:
		Switch palette model from direct DAC values to
		H (as phase) / S (as color signal intensity) / L (as avg value)
		triplets.
		
		Output signal is a combination of H/S + L.  H/S signal should
		probably lead by 1/2 color clock i.e.
		
		Pixel -1:
		Subsamples a,b = black
		Subsamples c,d = black + HS signal of pixel 0
		
		Pixel 0:
		Subsamples a,b = luma[0] + HS signal of pixel 0
		Subsamples c,d = luma[0] + HS signal of pixel 1

		Pixel 1:
		Subsamples a,b = luma[1] + HS signal of pixel 1
		Subsamples c,d = luma[1] + HS signal of pixel 2
		
		..etc
		
		
	
	For B&W mode -
		Just disable colorburst.
		
	For color mode -
		We need to sum the color signal and luma signal.
		
		i.e. generate a 16-value sine wave 
		Use chroma as an index into this
		
		Look up chroma, add luma
		
		Due to our limited R-ladder we only have 4 bits total
		which will limit luma to 3 bits.  By subsampling chroma
		signal and generating 4 bits of output we can hopefully
		make real color happen.
		
		

 */
#define USE_DISPLAY_LIST 1

#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include <stdio.h>
#include <string.h>

// #include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/dma.h"
#include "pico-ntsc.pio.h"


#ifndef PICO_DEFAULT_LED_PIN
#warning blink example requires a board with a regular LED
#endif

// No longer works due to change in PIO routine
// Need to calculate new #s	
// #define USE_FIXED_OVERCLOCK	0

const uint LED_PIN = PICO_DEFAULT_LED_PIN;

void start_video(PIO pio, uint sm, uint offset, uint pin, uint num_pins);

int irq_count = 0;

void video_core();

// #define SYS_CLOCK_KHZ	150000
#define SYS_CLOCK_KHZ	157500

/**********************************
 FRAMEBUFFER STUFF
 **********************************/
uint8_t palette[256][4];
uint8_t framebuffer[200][160];


void drawline (int x0, int y0, int x1, int y1, uint8_t color)
{
  int dx =  abs (x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs (y1 - y0), sy = y0 < y1 ? 1 : -1; 
  int err = dx + dy, e2; /* error value e_xy */
 
  for (;;){  /* loop */
	framebuffer[y0][x0] = color; // putpixel(x,y,7);
    if (x0 == x1 && y0 == y1) break;
    e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; } /* e_xy+e_x > 0 */
    if (e2 <= dx) { err += dx; y0 += sy; } /* e_xy+e_y < 0 */
  }
}

volatile int in_vblank = 0;

int main() {
	set_sys_clock_khz(SYS_CLOCK_KHZ,true);

    stdio_init_all();

	multicore_launch_core1(video_core);


	palette[0][0] = 15;
	palette[0][1] = 15;
	palette[0][2] = 15;
	palette[0][3] = 15;
	
	palette[1][0] = 31;
	palette[1][1] = 31;
	palette[1][2] = 31;
	palette[1][3] = 31;

	palette[2][0] = 25;
	palette[2][1] = 25;
	palette[2][2] = 15;
	palette[2][3] = 5;

	
	for (int i=0; i<200; i++) {
//		memset(&framebuffer[i][0],0,160);
	}
	
	memset(&framebuffer[100][0],2,160);


	for (int i=0; i<160; i+=2) {
		drawline(0,199,i,0,2);
	}

	uint8_t tmp;
	while(1) {
//		printf("Hello, world.\n");
		sleep_ms(500);
		while (!in_vblank);
		tmp = palette[2][0];
		palette[2][0] = palette[2][1];
		palette[2][1] = palette[2][2];
		palette[2][2] = palette[2][3];
		palette[2][3] = tmp;
	}
}

/* --------------------------------
	DISPLAY LIST LOGIC
   --------------------------------
  
Here we borrow heavily from Atari's ANTIC display processor.  

JMP - not useful for norm
JMP AND WAIT FOR VBLANK

/* --------------------------------
 SIGNAL GENERATOR
   -------------------------------- */

void video_core() {
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &ntsc_composite_program);
    printf("Loaded program at %d\n", offset);


	gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
	
    start_video(pio, 0, offset, 16, 5);
	
	while (1);
}

uint dma_channel;

#define SAMPLES_PER_CLOCK	4
#define	DAC_BITS	5


/* 
	Values needed to make timing calculations
*/

#define NTSC_COLORBURST_FREQ	3579545.0
#define CLOCK_FREQ (float)(SAMPLES_PER_CLOCK*3.5795454)
#define SAMPLE_LENGTH_US	(1.0/CLOCK_FREQ)


/*
	Raw DAC values to generate proper levels.
*/
#define MAX_DAC_OUT	((1 << DAC_BITS)-1)
#define MIN_DAC_OUT	0
#define BLANKING_VAL		(uint)(((40.0/140.0)*(float)MAX_DAC_OUT)+0.5)
#define COLOR_BURST_HI_VAL	(uint)(((60.0/140.0)*(float)MAX_DAC_OUT)+0.5)
#define COLOR_BURST_LO_VAL	(uint)(((20.0/140.0)*(float)MAX_DAC_OUT)+0.5)
#define SYNC_VAL			MIN_DAC_OUT


/*
	Various timings needed to generate a proper NTSC signal.
*/
#define SYNC_TIP_CLOCKS 	(int)(4.7/(SAMPLE_LENGTH_US)+0.5)
#define COLOR_BURST_START	(int)(5.3/(SAMPLE_LENGTH_US)+0.5)
#define VIDEO_START			(COLOR_BURST_START+SAMPLES_PER_CLOCK*30-2)
#define VIDEO_LENGTH		192*SAMPLES_PER_CLOCK


/*
	Set the total line width, in color clocks.
	ALTERNATE_COLORBURST_PHASE will generate proper 262.5
	color clock lines but is only partially supported at
	the moment (and doesn't seem to be necessary.)
	
	Note that a lot of old equipment uses 228 color clock
	lines; a lot of new equipment doesn't sync well to this
	but is just fine with 226 color clocks.  
*/

#ifdef ALTERNATE_COLORBURST_PHASE
#define LINE_WIDTH (227*SAMPLES_PER_CLOCK-(SAMPLES_PER_CLOCK/2))
#else 
#define LINE_WIDTH (226*SAMPLES_PER_CLOCK)
#endif 


/*
	Buffers for our vblank line etc.
	
	Note that virtually every TV made since the late 70s is 
	perfectly happy with one long VSYNC pulse and does not use
	the pre/post equalization pulses.  So there's no point in
	implementing the full VBLANK spec unless you have an absolutely
	ancient analog TV that you want to use.
	
*/
uint8_t vblank_line[LINE_WIDTH+1];
uint8_t vblank_odd_line[LINE_WIDTH+1];
uint8_t black_line[LINE_WIDTH+1];
uint8_t black_line_2[LINE_WIDTH];

uint8_t* black_lines[2] = {black_line,black_line_2 };

uint8_t __scratch_y("pingpong_group") pingpong_line_0[LINE_WIDTH];
uint8_t __scratch_y("pingpong_group") pingpong_line_1[LINE_WIDTH];
 
uint8_t* __scratch_y("pingpong_group") pingpong_lines[] = { pingpong_line_0, pingpong_line_1 };

//uint8_t video_line[LINE_WIDTH+1];
//uint8_t video_line2[LINE_WIDTH+1];


int want_color=1;

void make_vsync_line() {
	int ofs = 0;
	memset(vblank_line,BLANKING_VAL,LINE_WIDTH);
	memset(vblank_line,SYNC_VAL,105*SAMPLES_PER_CLOCK);
	
	memset(vblank_odd_line,BLANKING_VAL,LINE_WIDTH);
	memset(vblank_odd_line,SYNC_VAL,SYNC_TIP_CLOCKS);
	memset(&vblank_odd_line[LINE_WIDTH/2],SYNC_VAL,105*SAMPLES_PER_CLOCK);
}



void make_black_line() {
	// Set everything to the blanking level
	memset(black_line,BLANKING_VAL,LINE_WIDTH);
	
	// Do the HSYNC pulse.  The front porch has been drawn
	// by the previous DMA transfer, now provide 4.7uS
	// of SYNC pulse
	memset(black_line,SYNC_VAL,SYNC_TIP_CLOCKS);
	
	// Do our colorburst
	
	// Starts at 5.3uS = 37.94318 clocks

	if (want_color) {
		#if SAMPLES_PER_CLOCK==2
		for (int i=0; i<10; i++) {
			black_line[COLOR_BURST_START+i*2] = COLOR_BURST_HI_VAL;
			black_line[COLOR_BURST_START+1+i*2] = COLOR_BURST_LO_VAL;
		}
		#else
		for (int i=0; i<10; i++) {
			black_line[COLOR_BURST_START+i*4] = BLANKING_VAL;
			black_line[COLOR_BURST_START+1+i*4] = COLOR_BURST_HI_VAL;
			black_line[COLOR_BURST_START+2+i*4] = BLANKING_VAL;
			black_line[COLOR_BURST_START+3+i*4] = COLOR_BURST_LO_VAL;
		}
	}
	#endif
	

	memcpy(black_line_2,black_line,LINE_WIDTH);

	// Start HSYNC here
	black_line[LINE_WIDTH-1] = 0;

	// Need to alternate phase for line 2

	#ifdef ALTERNATE_COLORBURST_PHASE
	if (want_color) {
		#if SAMPLES_PER_CLOCK==2
		for (int i=0; i<10; i++) {
			black_line[COLOR_BURST_START+i*2] = COLOR_BURST_LO_VAL;
			black_line[COLOR_BURST_START+1+i*2] = COLOR_BURST_HI_VAL;
		}
		#else
		for (int i=0; i<10; i++) {
			black_line[COLOR_BURST_START+i*4] = BLANKING_VAL;
			black_line[COLOR_BURST_START+1+i*4] = COLOR_BURST_LO_VAL;
			black_line[COLOR_BURST_START+2+i*4] = BLANKING_VAL;
			black_line[COLOR_BURST_START+3+i*4] = COLOR_BURST_HI_VAL;
		}
	}
	#endif
	#endif
	
	// Video officially begins at 9.4uS / 67.2954 clocks
	// Start at 68 to be in phase with clock signal	
}

/**
	Fill a linebuffer with skeleton HSYNC, colorburst, and black signals.
	
	do_colorburst			If true, insert the colorburst
	use_alternate_phase		If true, invert the colorburst phase 180 degrees
	
**/
void make_normal_line(uint8_t* dest, int do_colorburst, int use_alternate_phase) {
	// Set everything to the blanking level
	memset(dest,BLANKING_VAL,LINE_WIDTH);
	
	// Do the HSYNC pulse.  The front porch has been drawn
	// by the previous DMA transfer, now provide 4.7uS
	// of SYNC pulse
	memset(dest,SYNC_VAL,SYNC_TIP_CLOCKS);
	
	// Fill in colorburst signal if desired
	if (do_colorburst) {
		uint8_t c1 = COLOR_BURST_HI_VAL;
		uint8_t c2 = COLOR_BURST_LO_VAL;
		
		if (use_alternate_phase) {
			uint8_t tmp;
			tmp = c1; c1 = c2; c2 = tmp;
		}
			
		
		#if SAMPLES_PER_CLOCK==2
		for (int i=0; i<10; i++) {
			dest[COLOR_BURST_START+i*2] = c1;
			dest[COLOR_BURST_START+1+i*2] = c2;
		}
		#else
		for (int i=0; i<10; i++) {
			dest[COLOR_BURST_START+i*4] = BLANKING_VAL;
			dest[COLOR_BURST_START+1+i*4] = c1;
			dest[COLOR_BURST_START+2+i*4] = BLANKING_VAL;
			dest[COLOR_BURST_START+3+i*4] = c2;
		}
	}
	#endif
}


uint8_t* video_lines[240];

int even_frame = 1; 	// 0 = odd, 1 = even





// If true, will generate an interlaced TV signal
// Currently jitters a lot possibly due to a bug in
// odd/even field ordering
int do_interlace = 0;

// Enables colorburst if true, set to 0 to get
// 640+ horizontal B&W pixels on some TVs!
int do_color = 1;

int start_video_line = 35;
int end_video_line = 234;


int line = 0;
int frame = 0;
uint8_t*	next_dma_line = vblank_line;

/* --------------------------------------------------------------------------------------------------------------
 	Generate video using a display list.  
   --------------------------------------------------------------------------------------------------------------
*/

#define DISPLAY_LIST_BLACK_LINE		0
#define DISPLAY_LIST_FRAMEBUFFER	1
#define DISPLAY_LIST_WVB			2
#define DISPLAY_LIST_USER_RENDER	3	// Will stub out to USER_RENDER with the next display line

// Basic display list for a 200 line display.
typedef unsigned int display_list_t;
display_list_t sample_display_list[] = { DISPLAY_LIST_BLACK_LINE, 35,
						      DISPLAY_LIST_FRAMEBUFFER, 200,
							  DISPLAY_LIST_WVB,0 };

display_list_t* display_list_ptr = sample_display_list;

display_list_t  display_list_current_cmd = 0;
display_list_t display_list_lines_remaining = 0;
display_list_t display_list_ofs = 0;

void user_render(uint line, uint8_t* dest) {
	// Line = 0 means VBLANK, don't do anything but reset your internals
	if (!line) return;
	
	int ofs = VIDEO_START;
	for (int i=0; i<160; i++) {
		dest[ofs] = 15;
		dest[ofs+1] = 31;
		dest[ofs] = 15;
		dest[ofs+1] = 15;
		ofs += 4;
	}
}

/*

	Generate a video line from the framebuffer
	
*/
int framebuffer_line_offset = 35;

static void __not_in_flash_func(make_video_line)(uint line, uint8_t* dest) {
	
	//	uint8_t* nextline = (line & 1) ? pingpong_lines[0] : pingpong_lines[1];
		uint8_t* sourceline = framebuffer[line-framebuffer_line_offset];
		uint8_t* colorptr;
		int ofs = VIDEO_START;
		for (int i=0; i<160; i++) {
			colorptr = palette[sourceline[i]];
		
			// The compiler will optimize this		
			dest[ofs] = colorptr[0];
			dest[ofs+1] = colorptr[1];
			dest[ofs+2] = colorptr[2];
			dest[ofs+3] = colorptr[3];
			ofs += 4;
		}
}

static void __not_in_flash_func(cvideo_dma_handler)(void) {
	
	// Set the next DMA access point and reset the interrupt
    dma_channel_set_read_addr(dma_channel, next_dma_line, true);
    dma_hw->ints0 = 1 << dma_channel;		

	// We should ALWAYS set the read address to whatever we decided it should be
	// last time, unless we're in vblank.  Then we determine what the next DMA line is.
	// This way we don't get an extra line(s) when switching between black lines and 
	// framebuffer lines.


	if (line >= 262) {
		in_vblank = 1;
		if (even_frame) {
		    next_dma_line = vblank_line;
			line = 0;
			frame++;
			if (do_interlace) even_frame = 0;
		}
		else {
		    next_dma_line = vblank_odd_line;
			line = 0;
			frame++;
			even_frame = 1;
		} 

		// Reset display list
		display_list_ptr = sample_display_list;
		display_list_ofs = 0;
		display_list_current_cmd = 0;
		display_list_lines_remaining = 0;
	} else {
			in_vblank = 0;
			// If no display list command, read it
			if (display_list_lines_remaining == 0) {
				display_list_current_cmd = display_list_ptr[display_list_ofs++];
				display_list_lines_remaining = display_list_ptr[display_list_ofs++];				
			}
			
			// Do the display list command
			switch (display_list_current_cmd) {
				// Forge a VBLANK
				case DISPLAY_LIST_WVB:
					// This will force reading the DL to stall until 
					// reset by VBLANK
					display_list_lines_remaining = 2;
					// Otherwise it's a black line
					
				case DISPLAY_LIST_BLACK_LINE:
					next_dma_line = black_lines[line & 1];
					break;
					
				case DISPLAY_LIST_FRAMEBUFFER:
				    //next_dma_line = black_lines[line & 1];
					next_dma_line = pingpong_lines[line & 1];
			
					// Now for the fun, we need to rasterize the next DMA line before 
					// the current line completes
					make_video_line(line,pingpong_lines[line & 1]);
//					next_dma_line = vblank_line;
					break;
					
				case DISPLAY_LIST_USER_RENDER:
					next_dma_line = pingpong_lines[line & 1];
					user_render(line,pingpong_lines[line & 1]);
					break;
			}
			
			if (display_list_lines_remaining) { display_list_lines_remaining--; }
			line++;
	}
	
}




void init_video_lines() {
	// Initialize video_line to alternating 1s and 2s
	make_vsync_line();
	
	make_normal_line(black_lines[0],do_color,0);
	make_normal_line(black_lines[1],do_color,0);
	make_normal_line(pingpong_lines[0],do_color,0);
	make_normal_line(pingpong_lines[1],do_color,0);

	pingpong_lines[0][800] = 31;
	pingpong_lines[0][801] = 31;
	pingpong_lines[1][800] = 31;
	pingpong_lines[1][801] = 31;

	for (int i=VIDEO_START; i<VIDEO_LENGTH; i+=SAMPLES_PER_CLOCK) {
				pingpong_lines[0][i+0] = 15;
				pingpong_lines[0][i+1] = 15;
				pingpong_lines[0][i+2] = 15;
				pingpong_lines[0][i+3] = 15;
	}

}

void start_video(PIO pio, uint sm, uint offset, uint pin, uint num_pins) {

	init_video_lines();
			
	// Initialize the PIO program
    ntsc_composite_program_init(pio, sm, offset, pin, num_pins);
	
	// Configure DMA
	
	// Grab an unused DMA channel
	dma_channel = dma_claim_unused_channel(true);
	
    pio_sm_clear_fifos(pio, sm);
    
	dma_channel_config c = dma_channel_get_default_config(dma_channel);
    
	// DMA transfers exec 8 bits at a time
	channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
	
	// DMA transfers increment the address
    channel_config_set_read_increment(&c, true);
	
	// DMA transfers use DREQ
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
	
	
    dma_channel_configure(dma_channel, &c,
        &pio->txf[sm],              // Destination pointer
        NULL,                       // Source pointer
        LINE_WIDTH,          // Number of transfers
        true                        // Start flag (true = start immediately)
    );
    	
	dma_channel_set_irq0_enabled(dma_channel, true);
    irq_set_exclusive_handler(DMA_IRQ_0, cvideo_dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);

	// Set the clock divisor
		
	float PIO_clkdiv = (SYS_CLOCK_KHZ*1000) / (NTSC_COLORBURST_FREQ*SAMPLES_PER_CLOCK);
	
	uint16_t div_int = (uint16_t)PIO_clkdiv;
	uint8_t div_frac = 0;
     if (div_int != 0) {
         div_frac = (uint8_t)((PIO_clkdiv - (float)div_int) * (1u << 8u));
     }
//	pio->sm[sm].clkdiv = (uint32_t) (PIO_clkdiv * (1 << 16)); // INT portion: 0xffff0000, FRAC portion: 0x0000ff00	

	pio->sm[sm].clkdiv = (((uint)div_frac) << PIO_SM0_CLKDIV_FRAC_LSB) |
             (((uint)div_int) << PIO_SM0_CLKDIV_INT_LSB);
	// Start the state machine
    pio_sm_set_enabled(pio, sm, true);
}
	
