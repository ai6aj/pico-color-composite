#ifndef __NTSC_VIDEO_CORE_H__
#define __NTSC_VIDEO_CORE_H__
#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>
#include "pico/stdlib.h"
void pcc_enable_colorburst(int value);

typedef void (pcc_user_render_raw_func_t)(uint, uint, uint8_t*);
typedef void (pcc_user_vblank_func_t)();

// Called back on every new line, with line #
typedef void (pcc_user_new_line_func_t)(uint);
typedef void (pcc_user_video_core_loop_func_t)(void);

void pcc_set_user_new_line_callback(pcc_user_new_line_func_t);
void pcc_set_user_render_raw(pcc_user_render_raw_func_t);
void pcc_set_user_vblank(pcc_user_vblank_func_t);
void pcc_set_user_video_core_loop(pcc_user_video_core_loop_func_t);

void pcc_video_core();

float pcc_get_video_core_load();

// This is almost an ideal multiple of the NTSC
// colorburst signal, which gives us very low jitter.
// Some devices (i.e. my cheap projector) will refuse
// to display in color if you go below this value.
#define SYS_CLOCK_KHZ	157500

// 195 or 204MHz are (almost) good multiples for PAL.
// #define SYS_CLOCK_KHZ	204000

// The phase of the generated colorburst signal.  
#define COLOR_BURST_PHASE_DEGREES 180.0

extern volatile int pcc_in_vblank;

/*
	
	Raw DAC values to generate proper levels.
	
	Not NTSC/PAL specific.
	
*/
#define SAMPLES_PER_CLOCK	4
#define	DAC_BITS	8
#define FIRST_GPIO_PIN 0

#define MAX_DAC_OUT	((1 << DAC_BITS)-1)
#define MIN_DAC_OUT	0

#ifndef USE_PAL
#define USE_NTSC
#endif

#ifdef USE_NTSC
	/* NTSC values. */

	#define BLANKING_VAL		(uint)(((40.0/100.0)*(float)MAX_DAC_OUT)+0.5)
	#define COLOR_BURST_HI_VAL	(uint)(((60.0/140.0)*(float)MAX_DAC_OUT)+0.5)
	#define COLOR_BURST_LO_VAL	(uint)(((20.0/140.0)*(float)MAX_DAC_OUT)+0.5)
	#define SYNC_VAL			MIN_DAC_OUT
	#define BLACK_LEVEL			(uint)(((47.5/140.0)*(float)MAX_DAC_OUT)+0.5)
	#define WHITE_LEVEL			MAX_DAC_OUT
	#define LUMA_SCALE			(WHITE_LEVEL-BLACK_LEVEL)

	/* 
		Values needed to make timing calculations
	*/

	#define NTSC_COLORBURST_MHZ		(3.5795454)
	#define NTSC_COLORBURST_FREQ	(NTSC_COLORBURST_MHZ*1000000)
	#define CLOCK_FREQ (float)(SAMPLES_PER_CLOCK*NTSC_COLORBURST_MHZ)
	#define SAMPLE_LENGTH_US	(1.0/CLOCK_FREQ)

	#define LINE_WIDTH (226*SAMPLES_PER_CLOCK)

	/*
		Various timings needed to generate a proper NTSC signal.
	*/
	#define SYNC_TIP_CLOCKS 	(int)(4.7/(SAMPLE_LENGTH_US)+0.5)
	#define COLOR_BURST_START	(int)(5.3/(SAMPLE_LENGTH_US)+0.4)

	#define VBLANK_CLOCKS		(int)(27.1/(SAMPLE_LENGTH_US)+0.5)
	
	// VIDEO_START *MUST* be 32-bit aligned.
	#define VIDEO_START			(SAMPLES_PER_CLOCK*39)
	#define VIDEO_LENGTH		192*SAMPLES_PER_CLOCK
	
	#define LINES_PER_FRAME			262
	#define COLORBURST_FREQ	NTSC_COLORBURST_FREQ
	#define VIDEO_START_PHASE_SHIFT	0


#else // PAL
	
	/*  PAL values. 
	
		These colorburst frequency is not to spec, but it's the only ones 
		that my cheap component-to-USB converter and LCD projector will accept
		produce a color display.  The colors are incorrect, however.
		
		My TV interprets PAL timings as component (YPbPr) input, yielding 
		a black and white display.
		
	*/

	#define BLANKING_VAL		(uint)(((45.0/143.0)*(float)MAX_DAC_OUT)+0.5)
	#define COLOR_BURST_HI_VAL	(uint)(((55.0/143.0)*(float)MAX_DAC_OUT)+0.5)
	#define COLOR_BURST_LO_VAL	(uint)(((35.0/143.0)*(float)MAX_DAC_OUT)+0.5)

	#define SYNC_VAL			MIN_DAC_OUT
	#define BLACK_LEVEL			(uint)(((45.0/143.0)*(float)MAX_DAC_OUT)+0.5)
	#define WHITE_LEVEL			MAX_DAC_OUT
	#define LUMA_SCALE			(WHITE_LEVEL-BLACK_LEVEL)


	/* 
		Values needed to make timing calculations
	*/

	// PAL colorburst timing is much more finicky than NTSC for some reason.
	// The fraction below adjusts the colorburst frequency so it works on
	// my (admittedly bad) equipment.
	#define PAL_COLORBURST_MHZ		(4.43361875 * 180.25/180.0)
	#define PAL_COLORBURST_FREQ	(PAL_COLORBURST_MHZ*1000000)
	#define CLOCK_FREQ (float)(SAMPLES_PER_CLOCK*PAL_COLORBURST_MHZ)
	#define SAMPLE_LENGTH_US	(1.0/CLOCK_FREQ)

	/*
		Set the total line width, in color clocks.
		ALTERNATE_COLORBURST_PHASE will generate proper 262.5
		color clock lines but is only partially supported at
		the moment (and doesn't seem to be necessary.)	
	*/

	#ifdef ALTERNATE_COLORBURST_PHASE
		#define LINE_WIDTH (283*SAMPLES_PER_CLOCK-(SAMPLES_PER_CLOCK/2))
	#else 
		#define LINE_WIDTH (282*SAMPLES_PER_CLOCK)
	#endif 

	/*
		Various timings needed to generate a proper PAL signal.
	*/
	#define SYNC_TIP_CLOCKS 	(int)(4.7/(SAMPLE_LENGTH_US)+0.5)
	#define COLOR_BURST_START	(int)(5.35/(SAMPLE_LENGTH_US)+0.5)

	#define VBLANK_CLOCKS		(int)(27.3/(SAMPLE_LENGTH_US)+0.5)
	#define SHORT_SYNC_CLOCKS	(int)(2.35/(SAMPLE_LENGTH_US)+0.5)

	// VIDEO_START *MUST* be 32-bit aligned.
	#define VIDEO_START			(SAMPLES_PER_CLOCK*50)
	#define VIDEO_LENGTH		192*SAMPLES_PER_CLOCK

	#define LINES_PER_FRAME			312
	#define COLORBURST_FREQ	PAL_COLORBURST_FREQ
	#define VIDEO_START_PHASE_SHIFT	0

#endif


#ifdef __cplusplus
}
#endif


#endif