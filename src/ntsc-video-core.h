#ifndef __NTSC_VIDEO_CORE_H__
#define __NTSC_VIDEO_CORE_H__
#ifdef __cplusplus
extern "C" {
#endif


#include <stdlib.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include <string.h>
#include <math.h>

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/dma.h"

void ntsc_video_core();

float get_video_core_load();

// This overclocks the Pico a bit but gives us a nice
// multiple of the NTSC clock frequency with low jitter
// for a better signal.  Set this to 133000 for the maximum
// RP2040 frequency that's still in spec; most emulation 
// should still work but display quality will suffer.
//
// The *default* Pico frequency is 120000, at this frequency
// you can generate display but some emulators (i.e. Atari 8-bit)
// won't have enough CPU time to generate their worst-case
// display.
//
// #define SYS_CLOCK_KHZ	120000
// #define SYS_CLOCK_KHZ	133000
#define SYS_CLOCK_KHZ	157500

// The phase of the generated colorburst signal.  
// Old video hardware had a pot you could adjust to tune the
// color.  This emulates that pot.  Adjust it to get the 
// correct(ish) color output for your emulator; NTSC displays
// were notoriously inaccurate and so were old consoles so
// don't expect to ever get perfect color rendition... there
// was no such thing.
//
// TODO this should be a default; the value should be 
// adjustable in software.  To properly implement it the
// video core will need to rebuild the colorburst signal
// during VBLANK.
#define COLOR_BURST_PHASE_DEGREES 180.0

/**********************************
 FRAMEBUFFER STUFF
 **********************************/
extern uint8_t palette[256][4];
extern uint8_t **framebuffer; // [200][160];


void drawline (int x0, int y0, int x1, int y1, uint8_t color);
extern volatile int in_vblank;

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

// VIDEO_START *MUST* be 32-bit aligned.
#define VIDEO_START			(COLOR_BURST_START+SAMPLES_PER_CLOCK*15)
#define VIDEO_LENGTH		192*SAMPLES_PER_CLOCK

// The chroma phase shift needed to adjust for VIDEO_START.  This
// will typically be some fraction of pi.
#define VIDEO_START_PHASE_SHIFT	0 // 3.14159


// The amount of time to display a black screen
// before turning on the video display.  This 
// is needed to give the TV time to lock the 
// VBLANK signal.
#define STARTUP_FRAME_DELAY 180

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

void setPaletteRaw(int num,float a,float b,float c,float d);

#define XXBLACK_LEVEL 0

/**
	Generate a palette entry from an NTSC phase/intensity/luma
	triplet.  Given the complexity of NTSC encoding it's highly recommended
	to use setPaletteRGB instead.
	
	chroma_phase		Phase of the chroma signal with respect to colorburst
	chroma_amplitude	Amplitude of the chroma signal
	luminance			The black and white portion of the signal
	
*/
void setPaletteNTSC(int num,float chroma_phase,float chroma_amplitude,float luminance);
void setPaletteRGB_float(int num,float r, float g, float b);
void setPaletteRGB(int num,uint8_t r, uint8_t g, uint8_t b);



#define DISPLAY_LIST_BLACK_LINE		0
#define DISPLAY_LIST_FRAMEBUFFER	1
#define DISPLAY_LIST_WVB			2

/* USER_RENDER expects a pointer to 256 color pixel data.
   USER_RENDER_RAW allows you to render directly to the output stream. */
#define DISPLAY_LIST_USER_RENDER		3	// Will stub out to USER_RENDER with the next display line
#define DISPLAY_LIST_USER_RENDER_RAW	4	// Will stub out to USER_RENDER with the next display line

// Basic display list for a 200 line display.
typedef unsigned int display_list_t;
extern display_list_t sample_display_list[];

// Our initial display list is just a black display.  This allows the TV time to 
// lock VBLANK.
extern volatile int startup_frame_counter;

typedef uint8_t* (*user_render_func_t)(uint);
typedef unsigned int display_list_t;
typedef void (user_render_raw_func_t)(uint, uint, uint8_t*);
typedef void (user_vblank_func_t)();

void set_user_render_raw(user_render_raw_func_t);
void set_user_vblank(user_vblank_func_t);

#ifdef __cplusplus
}
#endif


#endif