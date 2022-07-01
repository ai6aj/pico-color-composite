#include "ntsc-video-core.h"
#include "atari-8bit-video-core.h"
#include "atari-8bit-palette-colors.h"
#include "atari-8bit-charset.h"

// Basic display list for a 200 line display.
display_list_t atari_8bit_display_list[] = { 
	DISPLAY_LIST_USER_RENDER_RAW, ATARI_NTSC_VERTICAL_LINE_COUNT,
	DISPLAY_LIST_WVB,0 };

/* Test of (almost) worst-case performance for Atari:
	Graphics Mode 8
	
	NOTES:
		- The entire atari_render func needs to be rewritten from scratch; see below
	
		- We can't go above ~75% on video core usage without the display going haywire
		
		- We probably need to implement a USER_DIRECT_RENDER call so we can directly
		  generate the output signal rather than rendering the line to a framebuffer line,
		  then having another function translate that framebuffer line to the video signal
		  
		- Using a LUT to translate graphics data to intermediate output data is a lot faster
		  than trying to calculate it programatically
		  
		- An elegant solution to the color change problem is to use partial render: implement
		  the video rendering routine to render from (x1,x2) rather than trying to render
		  everything, then patch it.  The rendering routine will be slower, but it will run
		  in (relatively) constant time and will be straightforward to optimize.
		  
		  Now we can simply step through captured writes to the palette registers, i.e.
		  
		  NO WRITES TO PALETTE REGS:
			render(0,159)
			
		  SAMPLE WRITES TO PALETTE REGS - AT CLOCKS 50 AND 100:
			render(0,50)
			update palette
			render(50,100)
			update palette
			render(100,159)
			
		- The above also solves a problem with Mode 0 and Mode 8, which both use 2x the color clock.
		
		  Instead of trying to render everything at 320 pixel horizontal resolution, Mode 0 and Mode 8
		  will act as a Mode 7 line with special palette indexes for the bit patterns 00,01,10,and 11
		  
		  i.e. a normal mode 7 line might use indexes 0-7, a mode 8 line will also use indexes 8 (00),
		  9 (01), 10 (10), and 11 (11)
		  
	    - The above ALSO solves the problem of implementing P/M graphics, as we can use the same technique:
		
		  SAMPLE WRITE TO HPOS REG - AT CLOCK 50:
			render(0,50)
			update hpos
			render(50,159)
			
		- The above ALSO is the path we need to take for the 2600 which lacks DMA
		
		
*/
uint8_t atari_source_data[2048];
int atari_source_data_ofs = 0;

uint8_t atari_dma_data[48];
volatile int atari_playfield_width = 48;

uint8_t atari_color_registers[8];
uint8_t atari_register_update_times[8];
uint8_t atari_register_update_values[8];
uint8_t atari_tmp_line[320];

uint8_t atari_palette[] = { 0x04, 0x08, 0x0C, 0x0F, 0x00, 0x00, 0x00, 0x00 };
uint8_t atari_palette_8bit_value[] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };

uint32_t atari_signal_palette[16];

#define ATARI_PM_COLOR_0	0
#define	ATARI_PF_COLOR_0	4
#define	ATARI_PF_COLOR_1	5
#define	ATARI_PF_COLOR_2	6
#define	ATARI_PF_COLOR_3	7

/* Player / Playfield colors:
	0		Player/Missile 0
	1		Player/Missile 1
	2		Player/Missile 2
	3   	Player/Missile 3
	4		Playfield 0
	5		Playfield 1
	6		Playfield 2
	7		Playfield 3
	8		Background
	9..F	Only used in GTIA mode
	C		Bit pattern 00 in hi-res mode
	D		Bit pattern 01 in hi-res mode
	E		Bit pattern 10 in hi-res mode
	F		Bit pattern 11 in hi-res mode
*/


void setAtariColorRegister(int num,uint8_t palette[][4],int palette_num) {
	uint32_t* colorptr;
	colorptr = (uint32_t*)(palette[palette_num]);		
	atari_signal_palette[num] = colorptr[0];
	
	// Save the 8-bit value it was set to, in case we need to look it up
	// in the future e.g. for hi-res or GTIA modes
	atari_palette_8bit_value[num] = palette_num;


	// If we set playfield 1 or 2 in hi-res mode,
	// we need to implicitly recalculate colors C-F
	if (num == ATARI_PF_COLOR_1 || num == ATARI_PF_COLOR_2) {
		colorptr = (uint32_t*)(palette[atari_palette_8bit_value[ATARI_PF_COLOR_2]]);		
		uint32_t base_signal = colorptr[0];

		colorptr = (uint32_t*)(palette[atari_palette_8bit_value[(ATARI_PF_COLOR_2 & 0xF0) | (ATARI_PF_COLOR_1 & 0x0F)]]);		
		uint32_t luma_signal = colorptr[0];
		
		// Bit pattern 00
		atari_signal_palette[0x0C] = base_signal;
		
		// Bit pattern 01.  Note that little-endianness means the bitmasks are
		// the opposite of what you'd initially expect.
		atari_signal_palette[0x0D] = (base_signal & 0xFFFF) | (luma_signal & 0xFFFF0000);
		

		// Bit pattern 10.  
		atari_signal_palette[0x0E] = (base_signal & 0xFFFF0000) | (luma_signal & 0xFFFF);

		atari_signal_palette[0x0F] = luma_signal;
	}
	
	   
}
	
volatile int atari_mode_line = 0;

/*
	NOTES -

		Instead of rendering to 320 pixels and trying to calculate our signal from there,
		we render to 160 pixels and reserve colors 0xC - 0xF for the hi-res modes.  This
		greatly speeds up rendering and should give us ample time to implement P/M graphics
		etc.
		
		We need to modify how we access the palette - instead of using the built-in 256 color
		palette we need to copy what we want to our own 16 color palette and use that to
		generate the video signal.
		
		We can speed up video signal generation by getting the "fast path" working where we 
		do a 32-bit copy from the palette to the video signal rather than the existing 
		byte-at-a-time.
		
		It should be mentioned in the notes that none of this optimization happened easily!!!
*/

uint8_t user_line[192];
uint8_t chset_line;

int atari_source_line_ofs = 0;

static void __not_in_flash_func(atari_vblank)() {
		atari_source_line_ofs = 0;
		chset_line = 0;
}


static void __not_in_flash_func(atari_render)(uint line, uint video_start, uint8_t* output_buffer) {
	const uint16_t* mode_patterns = atari_hires_mode_patterns; //atari_fourcolor_mode_patterns;

	/* Step one is to translate the DMA'd line into its bit pattern. */
	int atari_tmp_line_ofs = atari_source_line_ofs;
	int user_line_ofs = 0;
	
	int shift_start = 7;
	int shift_mask = 0x01;
	int shift_by = 1;
	int reduce_shift_by = 1;
	int shift_times = 8;

	uint16_t* user_line_16 = (uint16_t*)user_line;

	

	for (int i=0; i<48; i++) {
		uint8_t chdata = atari_source_data[atari_tmp_line_ofs++];
		uint8_t data = atari_8bit_charset[(chdata << 3) + chset_line];
		user_line_16[user_line_ofs++] = (mode_patterns[data >> 4]);
		user_line_16[user_line_ofs++] = (mode_patterns[data & 0x0F]);
	}

	chset_line++; 
	if (chset_line == 8) {
		atari_source_line_ofs += 48;
		chset_line = 0;
	}

/*	for (int i=0; i<48; i++) {
		uint8_t data = atari_source_data[atari_source_line_ofs++];
		user_line_16[user_line_ofs++] = (mode_patterns[data >> 4]);
		user_line_16[user_line_ofs++] = (mode_patterns[data & 0x0F]);
	} */


	/* There are two ways to render the display - the fast way using 32-bit copies,
	   and the slow way using 8-bit copies.  The fast way is much preferable since
	   it provides ample CPU time for other stuff like P/M graphics and sound 
	   generation.  The slow way is temporarily included for debugging purposes. */
	uint32_t* colorptr;
	uint32_t* dest = (uint16_t*)(&output_buffer[video_start]);
	
	int colorptr_ofs = 0;
	for (int i=0; i<192; i++) {
		*(dest++) = atari_signal_palette[user_line[i]];
	}

	/* Uncomment below to use the much slower 8-bit copy (for debugging purposes only) */
/*	int ofs = video_start;
	uint8_t* dest = &output_buffer[ofs];
	uint8_t* colorptr;
	
	int colorptr_ofs = 0;
	for (int i=0; i<192; i++) {
		colorptr = (uint8_t*)&atari_signal_palette[user_line[i]];
		*(dest++) = colorptr[0];
		*(dest++) = colorptr[1];
		*(dest++) = colorptr[2];
		*(dest++) = colorptr[3];
	} */


}

void init_atari_8bit_video_core() {
	for (int i=0; i<256; i++) {
		uint32_t rgb = atari_8bit_fullColors[i];
		setPaletteRGB(i,(rgb & 0xFF0000) >> 16, (rgb & 0xFF00) >> 8, (rgb & 0xFF));
	}

	for (int i=0; i<16; i++) {
		setAtariColorRegister(i,palette,0);
	}

	setAtariColorRegister(ATARI_PF_COLOR_0,palette,128+2);
	setAtariColorRegister(ATARI_PF_COLOR_1,palette,128+15);
	setAtariColorRegister(ATARI_PF_COLOR_2,palette,145+0);
	setAtariColorRegister(ATARI_PF_COLOR_3,palette,128+12);

	
	for (int i=0; i<2048; i++) {
		atari_source_data[i] = i & 0x7F;
	}
		
	atari_mode_line = 8;
	set_user_render_raw(atari_render);
	set_user_vblank(atari_vblank);
}
