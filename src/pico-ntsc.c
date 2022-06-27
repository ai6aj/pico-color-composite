#define USE_DISPLAY_LIST 1

#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

// #include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/dma.h"
#include "pico-ntsc.pio.h"


#ifndef PICO_DEFAULT_LED_PIN
#warning blink example requires a board with a regular LED
#endif

/* The Atari 8-bit palette, used to test RGB to NTSC palette conversion */

uint32_t atari_8bit_fullColors[] = {
0x00000000, 0x00202020,
0x00404040, 0x00565656,
0x006c6c6c, 0x007e7e7e,
0x00909090, 0x00a0a0a0,
0x00b0b0b0, 0x00bcbcbc,
0x00c8c8c8, 0x00d2d2d2,
0x00dcdcdc, 0x00e4e4e4,
0x00ececec, 0x00ececec,
0x00444400, 0x00545408,
0x00646410, 0x0074741a,
0x00848424, 0x0092922c,
0x00a0a034, 0x00acac3a,
0x00b8b840, 0x00c4c448,
0x00d0d050, 0x00dcdc56,
0x00e8e85c, 0x00f2f262,
0x00fcfc68, 0x00fcfc68,
0x00702800, 0x007a360a,
0x00844414, 0x008e501e,
0x00985c28, 0x00a26a32,
0x00ac783c, 0x00b48244,
0x00bc8c4c, 0x00c49654,
0x00cca05c, 0x00d4aa62,
0x00dcb468, 0x00e4be70,
0x00ecc878, 0x00ecc878,
0x00841800, 0x008e260c,
0x00983418, 0x00a24224,
0x00ac5030, 0x00b65c3c,
0x00c06848, 0x00c87452,
0x00d0805c, 0x00d88a66,
0x00e09470, 0x00e69e78,
0x00eca880, 0x00f4b28a,
0x00fcbc94, 0x00fcbc94,
0x00880000, 0x00921010,
0x009c2020, 0x00a62e2e,
0x00b03c3c, 0x00b84a4a,
0x00c05858, 0x00c86464,
0x00d07070, 0x00d87c7c,
0x00e08888, 0x00e69494,
0x00eca0a0, 0x00f4aaaa,
0x00fcb4b4, 0x00fcb4b4,
0x0078005c, 0x00821068,
0x008c2074, 0x00962e7e,
0x00a03c88, 0x00a84a92,
0x00b0589c, 0x00b864a6,
0x00c070b0, 0x00c87ab8,
0x00d084c0, 0x00d690c8,
0x00dc9cd0, 0x00e4a6d8,
0x00ecb0e0, 0x00ecb0e0,
0x00480078, 0x00541084,
0x00602090, 0x006c2e9a,
0x00783ca4, 0x00824aae,
0x008c58b8, 0x009664c2,
0x00a070cc, 0x00aa7ad4,
0x00b484dc, 0x00bc90e4,
0x00c49cec, 0x00cca6f4,
0x00d4b0fc, 0x00d4b0fc,
0x00140084, 0x0022108e,
0x00302098, 0x003e2ea2,
0x004c3cac, 0x005a4ab6,
0x006858c0, 0x007264c8,
0x007c70d0, 0x00887cd8,
0x009488e0, 0x009e94e6,
0x00a8a0ec, 0x00b2aaf4,
0x00bcb4fc, 0x00bcb4fc,
0x00000088, 0x000e1092,
0x001c209c, 0x002a30a6,
0x003840b0, 0x00444eb8,
0x00505cc0, 0x005c68c8,
0x006874d0, 0x007280d8,
0x007c8ce0, 0x008698e6,
0x0090a4ec, 0x009aaef4,
0x00a4b8fc, 0x00a4b8fc,
0x0000187c, 0x000e2886,
0x001c3890, 0x002a469c,
0x003854a8, 0x004462b2,
0x005070bc, 0x005c7cc4,
0x006888cc, 0x007292d4,
0x007c9cdc, 0x0086a8e4,
0x0090b4ec, 0x009abef4,
0x00a4c8fc, 0x00a4c8fc,
0x00002c5c, 0x000e3c6a,
0x001c4c78, 0x002a5a84,
0x00386890, 0x0044769e,
0x005084ac, 0x005c90b6,
0x00689cc0, 0x0072a8ca,
0x007cb4d4, 0x0086c0de,
0x0090cce8, 0x009ad6f2,
0x00a4e0fc, 0x00a4e0fc,
0x00003c2c, 0x000e4c3a,
0x001c5c48, 0x002a6c56,
0x00387c64, 0x00448c72,
0x00509c80, 0x005ca88a,
0x0068b494, 0x0072c2a0,
0x007cd0ac, 0x0086dab6,
0x0090e4c0, 0x009af0ca,
0x00a4fcd4, 0x00a4fcd4,
0x00003c00, 0x00104c10,
0x00205c20, 0x00306c30,
0x00407c40, 0x004e8c4e,
0x005c9c5c, 0x0068a868,
0x0074b474, 0x0080c280,
0x008cd08c, 0x0098da98,
0x00a4e4a4, 0x00aef0ae,
0x00b8fcb8, 0x00b8fcb8,
0x00143800, 0x00244a0e,
0x00345c1c, 0x00426c2a,
0x00507c38, 0x005e8a44,
0x006c9850, 0x0078a65c,
0x0084b468, 0x0090c072,
0x009ccc7c, 0x00a8d886,
0x00b4e490, 0x00bef09a,
0x00c8fca4, 0x00c8fca4,
0x002c3000, 0x003c400e,
0x004c501c, 0x005a6028,
0x00687034, 0x00767e40,
0x00848c4c, 0x00909a58,
0x009ca864, 0x00a8b46e,
0x00b4c078, 0x00c0ca80,
0x00ccd488, 0x00d6e092,
0x00e0ec9c, 0x00e0ec9c,
0x00442800, 0x0054380c,
0x00644818, 0x00745824,
0x00846830, 0x0092763a,
0x00a08444, 0x00ac904e,
0x00b89c58, 0x00c4a862,
0x00d0b46c, 0x00dcc074,
0x00e8cc7c, 0x00f2d684,
0x00fce08c, 0x00fce08c
};



const uint LED_PIN = PICO_DEFAULT_LED_PIN;

void start_video(PIO pio, uint sm, uint offset, uint pin, uint num_pins);

int irq_count = 0;

void video_core();

// This overclocks the Pico a bit but gives us a nice
// multiple of the NTSC clock frequency with low jitter
// for a better signal.  Set this to 120000 for the default
// Pico frequency.
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

/*
	Raw DAC values to generate proper levels.
*/
#define SAMPLES_PER_CLOCK	4
#define	DAC_BITS	8
#define FIRST_GPIO_PIN 0

#define MAX_DAC_OUT	((1 << DAC_BITS)-1)
#define MIN_DAC_OUT	0
#define BLANKING_VAL		(uint)(((40.0/140.0)*(float)MAX_DAC_OUT)+0.5)
#define COLOR_BURST_HI_VAL	(uint)(((60.0/140.0)*(float)MAX_DAC_OUT)+0.5)
#define COLOR_BURST_LO_VAL	(uint)(((20.0/140.0)*(float)MAX_DAC_OUT)+0.5)
#define SYNC_VAL			MIN_DAC_OUT
#define BLACK_LEVEL			(uint)(((47.5/140.0)*(float)MAX_DAC_OUT)+0.5)
#define WHITE_LEVEL			MAX_DAC_OUT
#define LUMA_SCALE			(WHITE_LEVEL-BLACK_LEVEL)

/* 
	Values needed to make timing calculations
*/

#define NTSC_COLORBURST_FREQ	3579545.0
#define CLOCK_FREQ (float)(SAMPLES_PER_CLOCK*3.5795454)
#define SAMPLE_LENGTH_US	(1.0/CLOCK_FREQ)

/*
	Various timings needed to generate a proper NTSC signal.
*/
#define SYNC_TIP_CLOCKS 	(int)(4.7/(SAMPLE_LENGTH_US)+0.5)
#define COLOR_BURST_START	(int)(5.3/(SAMPLE_LENGTH_US)+0.5)
#define VIDEO_START			(COLOR_BURST_START+SAMPLES_PER_CLOCK*30-2)
#define VIDEO_LENGTH		192*SAMPLES_PER_CLOCK

// The amount of time to display a black screen
// before turning on the video display.  This 
// is needed to give the TV time to lock the 
// VBLANK signal.
#define STARTUP_FRAME_DELAY 150

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

void setPaletteRaw(int num,float a,float b,float c,float d) {
	palette[num][0] = BLACK_LEVEL+(uint8_t)(a*LUMA_SCALE);
	palette[num][1] = BLACK_LEVEL+(uint8_t)(b*LUMA_SCALE);
	palette[num][2] = BLACK_LEVEL+(uint8_t)(c*LUMA_SCALE);
	palette[num][3] = BLACK_LEVEL+(uint8_t)(d*LUMA_SCALE);
}

#define XXBLACK_LEVEL 0

/**
	Generate a palette entry from an NTSC phase/intensity/luma
	triplet.  Given the complexity of NTSC encoding it's highly recommended
	to use setPaletteRGB instead.
	
	chroma_phase		Phase of the chroma signal with respect to colorburst
	chroma_amplitude	Amplitude of the chroma signal
	luminance			The black and white portion of the signal
	
*/
void setPaletteNTSC(int num,float chroma_phase,float chroma_amplitude,float luminance) {
		
	// Hue = phase 
	float sat_scaled = LUMA_SCALE * chroma_amplitude;
	
	// Saturation = amplitude of chroma signal.
	
	int tmp = BLACK_LEVEL + sin(chroma_phase)*sat_scaled + luminance*LUMA_SCALE;
	tmp = tmp < BLACK_LEVEL ? BLACK_LEVEL : tmp;
	tmp = tmp > WHITE_LEVEL ? WHITE_LEVEL : tmp;
	
	
	palette[num][0] = tmp;
	tmp = BLACK_LEVEL + sin(chroma_phase+3.14159/2)*sat_scaled + luminance*LUMA_SCALE;
	tmp = tmp < BLACK_LEVEL ? BLACK_LEVEL : tmp;
	tmp = tmp > WHITE_LEVEL ? WHITE_LEVEL : tmp;
	
	palette[num][1] = tmp;
	
	tmp = BLACK_LEVEL + sin(chroma_phase+3.14159)*sat_scaled + luminance*LUMA_SCALE;
	tmp = tmp < BLACK_LEVEL ? BLACK_LEVEL : tmp;
	tmp = tmp > WHITE_LEVEL ? WHITE_LEVEL : tmp;
	palette[num][2] = tmp;
	
	tmp = BLACK_LEVEL + sin(chroma_phase+3.14159*3/2)*sat_scaled + luminance*LUMA_SCALE;
	tmp = tmp < BLACK_LEVEL ? BLACK_LEVEL : tmp;
	tmp = tmp > WHITE_LEVEL ? WHITE_LEVEL : tmp;palette[num][3] = tmp;
	palette[num][3] = tmp;
}

void setPaletteRGB_float(int num,float r, float g, float b) {
	// Calculate Y 
	float y = 0.299*r + 0.587*g + 0.114*b;

	// Determine (U,V)
	float u = 0.492111 * (b-y);
	float v = 0.877283 * (r-y);

	// Find S and H
	float s = sqrt(u*u+v*v);
	float h = atan2(v,u) + (55/180 * 3.14159 * 2);
	if (h < 0) h += 2*3.14159;
	
//	h += (55/180 * 3.14159);
	// Use setPalletteHSL to set the palette
	setPaletteNTSC(num,h,s,y);
}


void setPaletteRGB(int num,uint8_t r, uint8_t g, uint8_t b) {
	float rf = (float)r/255.0;
	float gf = (float)g/255.0;
	float bf = (float)b/255.0;
	setPaletteRGB_float(num,rf,gf,bf);
}

int main() {
	set_sys_clock_khz(SYS_CLOCK_KHZ,true);

    stdio_init_all();

	multicore_launch_core1(video_core);
	
	for (int i=0; i<200; i++) {
		uint32_t rgb = atari_8bit_fullColors[i];
		setPaletteRGB(i,(rgb & 0xFF0000) >> 16, (rgb & 0xFF00) >> 8, (rgb & 0xFF));
		memset(&framebuffer[i][0],i,160);
	}
	
	memset(&framebuffer[100][0],2,160);


	for (int i=0; i<160; i+=2) {
		drawline(0,199,i,0,2);
	}

	uint8_t tmp;
	
	uint8_t foo = 0;
	
	while(1) {
		// Dump some output to USB serial to make sure it a. works at selected clock frequency
		// and b. doesn't interfere with the display
		printf("Hello, world.\n");
		// Don't update the palette until VBLANK
		while (!in_vblank);
		setPaletteRGB(2,0,0,foo);
		foo++;

		// Wait until we're out of VBLANK before proceeding
		while (in_vblank);

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
	
    start_video(pio, 0, offset, FIRST_GPIO_PIN, DAC_BITS);
	
	while (1);
}

uint dma_channel;





/*
	Buffers for our vblank line etc.
	
	Note that virtually every TV made since the late 70s is 
	perfectly happy with one long VSYNC pulse and does not use
	the pre/post equalization pulses.  So there's no point in
	implementing the full VBLANK spec unless you have an absolutely
	ancient analog TV that you want to use.
	
*/
uint8_t test_line[LINE_WIDTH+1];

uint8_t vblank_line[LINE_WIDTH+1];
uint8_t vblank_odd_line[LINE_WIDTH+1];
uint8_t black_line[LINE_WIDTH+1];
uint8_t black_line_2[LINE_WIDTH];

uint8_t* black_lines[2] = {black_line,black_line_2 };

uint8_t pingpong_line_0[LINE_WIDTH];
uint8_t pingpong_line_1[LINE_WIDTH];
 
uint8_t* pingpong_lines[] = { pingpong_line_0, pingpong_line_1 };


int want_color=1;

void make_vsync_line() {
	int ofs = 0;
	memset(vblank_line,BLANKING_VAL,LINE_WIDTH);
	memset(vblank_line,SYNC_VAL,105*SAMPLES_PER_CLOCK);
	
	memset(vblank_odd_line,BLANKING_VAL,LINE_WIDTH);
	memset(vblank_odd_line,SYNC_VAL,SYNC_TIP_CLOCKS);
	memset(&vblank_odd_line[LINE_WIDTH/2],SYNC_VAL,105*SAMPLES_PER_CLOCK);
}

void make_color_burst(uint8_t* line, int use_alternate_phase) {
	uint8_t c1 = COLOR_BURST_HI_VAL;
	uint8_t c2 = COLOR_BURST_LO_VAL;
		
	if (use_alternate_phase) {
		uint8_t tmp;
		tmp = c1; c1 = c2; c2 = tmp;
	}
				
	if (SAMPLES_PER_CLOCK==2) {
		for (int i=0; i<10; i++) {
			line[COLOR_BURST_START+i*2] = c1;
			line[COLOR_BURST_START+1+i*2] = c2;
		}
	}
	else if (SAMPLES_PER_CLOCK==4) {
		for (int i=0; i<10; i++) {
			line[COLOR_BURST_START+i*4] = BLANKING_VAL;
			line[COLOR_BURST_START+1+i*4] = c1;
			line[COLOR_BURST_START+2+i*4] = BLANKING_VAL;
			line[COLOR_BURST_START+3+i*4] = c2;
		}
	}
	else {
		for (int i=0; i<10; i++) {
			line[COLOR_BURST_START+i*8] = BLANKING_VAL;
			line[COLOR_BURST_START+1+i*8] = c1;
			line[COLOR_BURST_START+2+i*8] = c1;
			line[COLOR_BURST_START+3+i*8] = BLANKING_VAL;
			line[COLOR_BURST_START+4+i*8] = BLANKING_VAL;
			line[COLOR_BURST_START+5+i*8] = c2;
			line[COLOR_BURST_START+6+i*8] = c2;
			line[COLOR_BURST_START+7+i*8] = BLANKING_VAL;
		}
	}	
	
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

	
	memcpy(black_line_2,black_line,LINE_WIDTH);

	// Start HSYNC here
	black_line[LINE_WIDTH-1] = 0;
	
	if (want_color) {
		make_color_burst(black_line,0);
	}
	
	// Need to alternate phase for line 2

	#ifdef ALTERNATE_COLORBURST_PHASE
	
	if (want_color) {
		make_color_burst(black_line_2,1);
	}
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
		make_color_burst(dest,use_alternate_phase);
	}
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
						      DISPLAY_LIST_USER_RENDER, 200,
							  DISPLAY_LIST_WVB,0 };

// Our initial display list is just a black display.  This allows the TV time to 
// lock VBLANK.
int startup_frame_counter = STARTUP_FRAME_DELAY;
display_list_t startup_display_list[] = { DISPLAY_LIST_WVB, 0 };

display_list_t* display_list_ptr = startup_display_list;

display_list_t  display_list_current_cmd = 0;
display_list_t display_list_lines_remaining = 0;
display_list_t display_list_ofs = 0;

/*
	User routine to generate a line
	
	This is how Pico-XL will simulate ANTIC.
*/
int framebuffer_line_offset = 35;

typedef uint8_t* (*user_render_func_t)(uint);

uint8_t user_line[160];

static uint8_t* __not_in_flash_func(user_render_ex)(uint line) {
	for (int i=0; i<160; i++) { user_line[i] = framebuffer[line-framebuffer_line_offset][i]; };
	return user_line;
}

int frcount = 0;

static void __not_in_flash_func(user_render)(uint line, uint8_t* dest,user_render_func_t user_func) {
		uint8_t* sourceline = user_func(line);
		uint8_t* colorptr;
		frcount++;
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

/*

	Generate a video line from the framebuffer
	
*/

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

		if (display_list_ptr != sample_display_list) {
			startup_frame_counter--;
			if (startup_frame_counter == 0) {
				display_list_ptr = sample_display_list;			
			}
		} else {			
			display_list_ptr = sample_display_list;
		}

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
					user_render(line,pingpong_lines[line & 1],user_render_ex);
					break;
			}
			
			if (display_list_lines_remaining) { display_list_lines_remaining--; }
			line++;
	}
	
}




void init_video_lines() {
	// Initialize video_line to alternating 1s and 2s
	make_vsync_line();
	
	for (int i=0; i<768; i++) {
		test_line[i] = i/3;
	}
	
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
    ntsc_composite_program_init(pio, sm, offset, FIRST_GPIO_PIN,DAC_BITS );
	
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

	pio->sm[sm].clkdiv = (((uint)div_frac) << PIO_SM0_CLKDIV_FRAC_LSB) |
             (((uint)div_int) << PIO_SM0_CLKDIV_INT_LSB);
	// Start the state machine
    pio_sm_set_enabled(pio, sm, true);
}
	
