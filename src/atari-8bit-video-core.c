#include "ntsc-video-core.h"
#include "atari-8bit-video-core.h"
#include "atari-8bit-palette-colors.h"
#include "atari-8bit-charset.h"

uint8_t* antic_read_banks[16];
uint8_t antic_ram[8192];

__attribute__((always_inline)) inline uint8_t antic_memory_readByte(uint16_t addr) {
	if ((addr & 0xD000) == 0xD000) {
		// 0xD000 - 0xDFFF are mmio and need to either be emulated
		// or just set to 0
		return 0;
	}
	else {	
		return antic_read_banks[(addr >> 12)][addr & 0xFFF];
	}
}

uint8_t antic_read_memory(uint16_t addr) {
	return antic_memory_readByte(addr);
}


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
uint8_t atari_pm_graphics_enabled = 1;
// uint8_t atari_source_data[2048];

int atari_source_data_base = 4096;
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
#define ATARI_PM_COLOR_1	1
#define ATARI_PM_COLOR_2	2
#define ATARI_PM_COLOR_3	3
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

// NOTE - USER_LINE needs to be 256 to allow some dummy space
// for rendering P/M graphics.  Only the first 192 bytes will
// actually be used in generating the display.
uint8_t user_line[256];
uint8_t chset_line;

uint16_t atari_source_line_base = 4096;
uint16_t atari_source_line_ofs = 0;

volatile uint8_t atari_vblank_flag = 0;

static void __not_in_flash_func(atari_vblank)() {
		atari_source_line_ofs = 0;
		chset_line = 0;
		atari_vblank_flag = 0;
		atari_hsync_flag = 0;
}

#define ATARI_PLAYER_MINIMUM_OFFSET		63 
uint8_t atari_player_bitmaps[4];
uint8_t atari_player_widths[4];
uint8_t atari_player_offsets[4];

uint8_t atari_missile_bitmap;
uint8_t atari_missile_widths[4];
uint8_t atari_missile_offsets[4];
uint8_t atari_missile_is_one_color = 0;

void set_player_hpos(uint8_t player,uint8_t hpos) {
	atari_player_offsets[player & 0x7] = hpos;
}

void set_player_data(uint8_t player,uint8_t data) {
	atari_player_bitmaps[player] = data;
}

void set_missile_data(uint8_t data) {
	atari_missile_bitmap = data;
}


// Playfied co
const uint8_t pm_priority_table[][4] = {
// 0
{ 0,0,0,0 },
// 1
{ 0,1,1,1 },
// 2
{ 0,1,2,2 },
// 3
{ 0,1,2,3 },
// 4
{ 0,1,2,3 },
// 5
{ 0,1,2,3 },
// 6
{ 0,1,2,3 },
// 7
{ 0,1,2,3 },
// 8
{ 0,1,2,3 },
// 9
{ 0,1,2,3 },
// A
{ 0,1,2,3 },
// B
{ 0,1,2,3 },
// C
{ 0,1,2,3 },
// D
{ 0,1,2,3 },
// E
{ 0,1,2,3 },
// F
{ 0,1,2,3 },
};

uint8_t player_collisions[4][16];
uint8_t missile_collisions[4][16];

void clear_all_collisions() {
	for (int x=0; x<4; x++) {
		for (int y=0; y<16; y++) {
			player_collisions[x][y] = 0;
			missile_collisions[x][y] = 0;
		}
	}
}


uint8_t get_player_player_collisions(uint8_t player) {
	// Subtle bug here that should be easy to fix - 
	// player/player collisions should report for 
	// BOTH players but currently only the higher-numbered
	// player will report it.  Simple enough to fix, just
	// check the other player's collision data to see if it
	// collided with us.
	uint8_t rv = 0;
	rv |= player_collisions[player][0];
	rv |= player_collisions[player][1] << 1;
	rv |= player_collisions[player][2] << 2;
	rv |= player_collisions[player][3] << 3;
	return rv;	
}

uint8_t get_player_playfield_collisions(uint8_t player) {
	uint8_t rv = 0;
	rv |= player_collisions[player][4];
	rv |= player_collisions[player][5] << 1;
	rv |= player_collisions[player][6] << 2;
	rv |= player_collisions[player][7] << 3;
	rv |= player_collisions[player][0xC] ? 0x02 : 0;
	rv |= player_collisions[player][0xD] ? 0x06 : 0;
	rv |= player_collisions[player][0xE] ? 0x06 : 0;
	rv |= player_collisions[player][0xF] ? 0x04 : 0;
	return rv;	
}

static inline void render_pm_graphics(uint8_t hscrol) {
	for (int i=0; i<4; i++) {
		int player_width = atari_player_widths[i];
		if (player_width > 0) {
			uint8_t player_bitmap = atari_player_bitmaps[i];
			uint8_t player_offset = atari_player_offsets[i]+hscrol;
			uint8_t pixel_pos = player_offset-ATARI_PLAYER_MINIMUM_OFFSET;
			for (int n=0; n<8; n++) {
				if (player_bitmap & 0x80) {
					for (int x=0; x<player_width; x++) {						
						uint8_t user_line_byte = user_line[pixel_pos];
						player_collisions[i][user_line_byte] = 1;
						user_line[pixel_pos] = pm_priority_table[user_line_byte][i];
						pixel_pos++;
					}						
				} else {
					pixel_pos += player_width;
				}
				player_bitmap <<= 1;
			}
		}
	}
	
	
	// Now do missiles.  NOTE there is a subtle bug in the missile-player
	// collision detection - we will detect a missile to missile collision
	// (which should not be reported) as missile to player.  If this is
	// a problem it can be fixed by adding some pseudocolors to our 
	// intermediate palette.
	if (atari_missile_bitmap) {
		uint8_t missile_data = atari_missile_bitmap;
		for (int i=0; i<4; i++) {
			uint8_t missile_color = atari_missile_is_one_color ? 7 : i;
			uint8_t missile_width = atari_missile_widths[i];
			uint8_t missile_offset = atari_missile_offsets[i]+hscrol;
			uint8_t pixel_pos = missile_offset-ATARI_PLAYER_MINIMUM_OFFSET;
			if (missile_data & 0x80) {
				for (int x=0; x<missile_width; x++) {						
					uint8_t user_line_byte = user_line[pixel_pos];
					missile_collisions[i][user_line_byte] = 1;
					user_line[pixel_pos] = pm_priority_table[user_line_byte][i];
					pixel_pos++;
				}
			}
			else { pixel_pos += missile_width; }

			if (missile_data & 0x40) {
				for (int x=0; x<missile_width; x++) {						
					uint8_t user_line_byte = user_line[pixel_pos];
					missile_collisions[i][user_line_byte] = 1;
					user_line[pixel_pos] = pm_priority_table[user_line_byte][i];
					pixel_pos++;
				}
			}

			missile_data <<= 2;
			
		}
	}
	
}



/*
	NOTES ON SCROLLING:
	
	
	...ANTIC accomplishes vertical scrolling not by moving the display list up and down by a number of scan lines, 
	but by using the VSCROL value to skip that number of scan lines in the first line of the display list, 
	essentially shortening the number of displayed lines...
	
	(https://www.playermissile.com/scrolling_tutorial/index.html)
	
	The HSCROL hardware register at $d404 controls the horizontal shift for fine scrolling, measured in color clocks from 0 - 15.

	On display list instructions with the horizontal scrolling bit set, ANTIC automatically expands its screen memory use to the 
	next larger playfield size, unless it is already using a wide playfield.
*/

int antic_use_gtia_mode = 1;
int atari_antic_hscrol = 0;

// Should always be 0, but using this variable
// instead of a constant '0' enables the compiler
// to do a little extra optimization for some reason
// *shrug*
int output_hscrol = 0;

int antic_dma_width=40;
int antic_dma_start=4;

void set_antic_dma_width(int width) {
	switch (width) {
		case 32:
			antic_dma_width = 32;
			antic_dma_start = 8;
			break;
			
		case 40:
			antic_dma_width = 40;
			antic_dma_start = 4;
			break;
			
		case 48:
			antic_dma_width = 48;
			antic_dma_start = 0;
			break;
	}
}

volatile uint8_t atari_hsync_flag = 0;

static void __not_in_flash_func(atari_render)(uint line, uint video_start, uint8_t* output_buffer) {
//	const uint16_t* mode_patterns = atari_hires_mode_patterns; //atari_fourcolor_mode_patterns;

	// Let the other core know we have hsync
	atari_hsync_flag = 0;

	/* Step one is to translate the DMA'd line into its bit pattern. */
	int atari_tmp_line_ofs = atari_source_line_base+atari_source_line_ofs;
	int user_line_ofs = 0;
	
	int shift_start = 7;
	int shift_mask = 0x01;
	int shift_by = 1;
	int reduce_shift_by = 1;
	int shift_times = 8;

	uint16_t* user_line_16 = (uint16_t*)user_line;

	/* Render left border. */
	for (int i=0; i<antic_dma_start; i++) {
		user_line_16[user_line_ofs++] = 0x0808;
		user_line_16[user_line_ofs++] = 0x0808;
	}

	/* Playfield graphics render first. */
	uint8_t chset_line_ofs = chset_line;
	const uint16_t* mode_f_mode_pattern = NULL;
	
	switch (atari_mode_line) {
		
		case 2:
		if (antic_use_gtia_mode) {			
			mode_f_mode_pattern = atari_fourcolor_mode_patterns;
		} else {
			mode_f_mode_pattern = atari_hires_mode_patterns;
		}
		for (int i=0; i<antic_dma_width; i++) {
			uint8_t chdata = antic_memory_readByte(atari_tmp_line_ofs++);
			
//			uint8_t data = atari_8bit_charset[((chdata & 0x7F) << 3) + chset_line];

			uint8_t data = antic_memory_readByte(((chdata & 0x7F) << 3) + chset_line);
			if (chdata & 0x80) data ^= 0xFF;
			user_line_16[user_line_ofs++] = (mode_f_mode_pattern[data >> 4]);
			user_line_16[user_line_ofs++] = (mode_f_mode_pattern[data & 0x0F]);
		}
		chset_line++; 
		if (chset_line == 8) {
			atari_source_line_ofs += antic_dma_width;
			chset_line = 0;
		}
		break;

		case 3:
		if (antic_use_gtia_mode) {			
			mode_f_mode_pattern = atari_fourcolor_mode_patterns;
		} else {
			mode_f_mode_pattern = atari_hires_mode_patterns;
		}
		for (int i=0; i<antic_dma_width; i++) {
			uint8_t chdata = antic_memory_readByte(atari_tmp_line_ofs++);
			
//			uint8_t data = (chset_line < 2) ? 
//				0:atari_8bit_charset[((chdata & 0x7F) << 3) + mode_3_chline_xlat[chset_line]];

			uint8_t data = (chset_line < 2) ? 
				0:antic_memory_readByte(((chdata & 0x7F) << 3) + mode_3_chline_xlat[chset_line]);

				
			if (chdata & 0x80) data ^= 0xFF;
			user_line_16[user_line_ofs++] = (mode_f_mode_pattern[data >> 4]);
			user_line_16[user_line_ofs++] = (mode_f_mode_pattern[data & 0x0F]);
		}
		chset_line++; 
		if (chset_line == 10) {
			atari_source_line_ofs += antic_dma_width;
			chset_line = 0;
		}
		break;
		

		case 5:
			chset_line_ofs = chset_line_ofs >> 1;

		case 4:
		
		for (int i=0; i<antic_dma_width; i++) {
			uint8_t chdata = antic_memory_readByte(atari_tmp_line_ofs++);
			uint8_t inv_select = (chdata & 0x80) >> 3;
			
			chdata &= 0x7F;
			
			uint8_t data = atari_8bit_charset[(chdata << 3) + chset_line_ofs];

			user_line_16[user_line_ofs++] = atari_fourcolor_mode_patterns[inv_select + (data >> 4)];
			user_line_16[user_line_ofs++] = atari_fourcolor_mode_patterns[inv_select + (data & 0xF)];				
		}
		
		chset_line++; 
		if ((atari_mode_line == 4 && chset_line == 8) || (atari_mode_line == 5 && chset_line == 16)) {
			atari_source_line_ofs += antic_dma_width;
			chset_line = 0;
		}

		break;

		case 7:
			chset_line_ofs = chset_line_ofs >> 1;

		case 6:
		
		for (int i=0; i<antic_dma_width/2; i++) {
			uint8_t chdata = antic_memory_readByte(atari_tmp_line_ofs++);
			uint8_t fgcolor = (chdata & 0xC0) >> 4;
			
			chdata &= 0x3F;
			uint8_t data = atari_8bit_charset[(chdata << 3) + chset_line_ofs];

			uint8_t fgdata = atari_lores_char_patterns[fgcolor];
			for (int x=0; x<4; x++) {
				user_line_16[user_line_ofs++] = atari_lores_char_patterns[fgcolor + ((data & 0xC0) >> 6)];
				data <<= 2;
			}
		}

		chset_line++; 
		if ((atari_mode_line == 6 && chset_line == 8) || (atari_mode_line == 6 && chset_line == 16)) {
			atari_source_line_ofs += antic_dma_width/2;
			chset_line = 0;
		}
		break;

		case 0x08:
		for (int i=0; i<antic_dma_width/4; i++) {
			uint8_t data = antic_memory_readByte(atari_tmp_line_ofs++);
						
			user_line_16[user_line_ofs++] = atari_modes_8_A_patterns[data >> 6];
			user_line_16[user_line_ofs++] = atari_modes_8_A_patterns[data >> 6];
			user_line_16[user_line_ofs++] = atari_modes_8_A_patterns[(data & 0x30) >> 4];
			user_line_16[user_line_ofs++] = atari_modes_8_A_patterns[(data & 0x30) >> 4];
			user_line_16[user_line_ofs++] = atari_modes_8_A_patterns[(data & 0xC) >> 2];			
			user_line_16[user_line_ofs++] = atari_modes_8_A_patterns[(data & 0xC) >> 2];			
			user_line_16[user_line_ofs++] = atari_modes_8_A_patterns[data & 3];
			user_line_16[user_line_ofs++] = atari_modes_8_A_patterns[data & 3];
		}
		break;
		
		case 0x09:
		for (int i=0; i<antic_dma_width/4; i++) {
			uint8_t data = antic_memory_readByte(atari_tmp_line_ofs++);
						
			user_line_16[user_line_ofs++] = atari_mode_9_patterns[data >> 7];
			user_line_16[user_line_ofs++] = atari_mode_9_patterns[(data & 0x40) >> 6];
			user_line_16[user_line_ofs++] = atari_mode_9_patterns[(data & 0x20) >> 5];
			user_line_16[user_line_ofs++] = atari_mode_9_patterns[(data & 0x10) >> 4];
			user_line_16[user_line_ofs++] = atari_mode_9_patterns[(data & 0x8) >> 3];
			user_line_16[user_line_ofs++] = atari_mode_9_patterns[(data & 0x4) >> 2];
			user_line_16[user_line_ofs++] = atari_mode_9_patterns[(data & 0x2) >> 1];
			user_line_16[user_line_ofs++] = atari_mode_9_patterns[data & 1];
		}
		break;

		case 0x0A:
		for (int i=0; i<antic_dma_width/2; i++) {
			uint8_t data = antic_memory_readByte(atari_tmp_line_ofs++);
						
			user_line_16[user_line_ofs++] = atari_modes_8_A_patterns[data >> 6];
			user_line_16[user_line_ofs++] = atari_modes_8_A_patterns[(data & 0x30) >> 4];
			user_line_16[user_line_ofs++] = atari_modes_8_A_patterns[(data & 0xC) >> 2];			
			user_line_16[user_line_ofs++] = atari_modes_8_A_patterns[data & 3];
		}
		break;

		// Only difference between Mode B and Mode C is how often the DMA buffer is 
		// refreshed (every other line in D, every line in E)
		case 0x0B:
		case 0x0C:
		for (int i=0; i<antic_dma_width/2; i++) {
			uint8_t data = antic_memory_readByte(atari_tmp_line_ofs++);
			user_line_16[user_line_ofs++] = atari_twocolor_mode_patterns[data >> 6];
			user_line_16[user_line_ofs++] = atari_twocolor_mode_patterns[(data & 0x30) >> 4];
			user_line_16[user_line_ofs++] = atari_twocolor_mode_patterns[(data & 0xC) >> 2];
			user_line_16[user_line_ofs++] = atari_twocolor_mode_patterns[data & 3];
		}
		break;

		
		// Only difference between Mode D and Mode E is how often the DMA buffer is 
		// refreshed (every other line in D, every line in E)
		case 0x0D:
		case 0x0E:
		for (int i=0; i<antic_dma_width; i++) {
			uint8_t data = antic_memory_readByte(atari_tmp_line_ofs++);
			user_line_16[user_line_ofs++] = atari_fourcolor_mode_patterns[data >> 4];
			user_line_16[user_line_ofs++] = atari_fourcolor_mode_patterns[data & 0xF];				
		}
		break;
		
		case 0x0F:
		if (antic_use_gtia_mode) {			
			mode_f_mode_pattern = atari_fourcolor_mode_patterns;
		} else {
			mode_f_mode_pattern = atari_hires_mode_patterns;
		}
			
		for (int i=0; i<antic_dma_width; i++) {
			uint8_t data = antic_memory_readByte(atari_tmp_line_ofs++);
			user_line_16[user_line_ofs++] = (mode_f_mode_pattern[data >> 4]);
			user_line_16[user_line_ofs++] = (mode_f_mode_pattern[data & 0x0F]);
		}
		break;

	}

	/* Render right border */
	for (int i=antic_dma_start+antic_dma_width; i<48; i++) {
		user_line_16[user_line_ofs++] = 0x0808;
		user_line_16[user_line_ofs++] = 0x0808;
	}

	/* Player Graphics render next. */
	output_hscrol = atari_antic_hscrol;
	if (atari_pm_graphics_enabled) {		
	
		// Horizontal scroll shifts the players RIGHT to compensate for
		// the fact that the displayed line will be shifted LEFT when
		// the video signal is generated.  
		render_pm_graphics(output_hscrol);		
	}

	/* Final housekeeping */
	atari_source_line_ofs += antic_dma_width;

	/* There are two ways to render the display - the fast way using 32-bit copies,
	   and the slow way using 8-bit copies.  The fast way is much preferable since
	   it provides ample CPU time for other stuff like P/M graphics and sound 
	   generation.  The slow way is temporarily included for debugging purposes. */
	uint32_t* colorptr;
	uint32_t* dest = (uint32_t*)(&output_buffer[video_start]);

	
	/* This is where horizontal fine scroll is implemented.
	   Instead of starting at i=0, start at i=HSCROL and continue to i=HSCROL+192 */
	int colorptr_ofs = 0;
	for (int i=output_hscrol; i<output_hscrol+192; i++) {
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



void worst_case_test() {
	
	// Copy the character set into RAM bank 0
	for (int i=0; i<1024; i++) {
		antic_ram[i] = atari_8bit_charset[i];
	}
		
	// Initialize our RAM banks to point to antic_ram
	for (int i=0; i<16; i+=2) {
		antic_read_banks[i] = &antic_ram[0];
		antic_read_banks[i+1] = &antic_ram[4096];
	}

	
	
	atari_mode_line = 0xF;
	atari_antic_hscrol = 0;

	// Worst c
	int width = 4;
	atari_pm_graphics_enabled = 0;
	atari_player_bitmaps[0] = 0xFF;	
	atari_player_widths[0] = width;
	atari_player_offsets[0] = 64;

	atari_player_bitmaps[1] = 0xFF;
	atari_player_widths[1] = width;
	atari_player_offsets[1] = 98;

	atari_player_bitmaps[2] = 0xFF;
	atari_player_widths[2] = width;
	atari_player_offsets[2] = 132;

	atari_player_bitmaps[3] = 0xFF;
	atari_player_widths[3] = width;
	atari_player_offsets[3] = 166;

	atari_missile_bitmap = 0b11111111;
	atari_missile_widths[0] = width;
	atari_missile_widths[1] = width;
	atari_missile_widths[2] = width;
	atari_missile_widths[3] = width;
	atari_missile_offsets[0] = 200;
	atari_missile_offsets[1] = 210;
	atari_missile_offsets[2] = 220;
	atari_missile_offsets[3] = 230;
	
	
	for (int i=0; i<256; i++) {
		uint32_t rgb = atari_8bit_fullColors[i];
		setPaletteRGB(i,(rgb & 0xFF0000) >> 16, (rgb & 0xFF00) >> 8, (rgb & 0xFF));
	}

	for (int i=0; i<16; i++) {
		setAtariColorRegister(i,palette,0);
	}

	setAtariColorRegister(ATARI_PM_COLOR_0,palette,255);
	setAtariColorRegister(ATARI_PM_COLOR_1,palette,128+15);
	setAtariColorRegister(ATARI_PM_COLOR_2,palette,64+15);
	setAtariColorRegister(ATARI_PM_COLOR_3,palette,32+15);
	
	setAtariColorRegister(ATARI_PF_COLOR_0,palette,193);
	setAtariColorRegister(ATARI_PF_COLOR_1,palette,128);
	setAtariColorRegister(ATARI_PF_COLOR_2,palette,0x61);
	setAtariColorRegister(ATARI_PF_COLOR_3,palette,0x20);

	
	for (int i=0; i<2048; i++) {
		// atari_source_data[i] = i & 0xFF;
		antic_ram[4096 + i] = i & 0xFF;
	}
}

void init_atari_8bit_video_core() {

	// Initialize everything we need
	// to start the renderer
	atari_missile_bitmap = 0;
	clear_all_collisions();
	atari_pm_graphics_enabled = 0;
	set_antic_dma_width(48);
	
	for (int i=0; i<4; i++) {
		atari_player_bitmaps[i] = 0;
		atari_player_widths[i] = 0;
		atari_missile_widths[i] = 0;
		atari_player_offsets[i] = 0;
		atari_missile_offsets[i] = 0;
	}

	for (int i=0; i<16; i++) {
		setAtariColorRegister(i,palette,0);
	}
	
	atari_mode_line=8;
	set_user_render_raw(atari_render);
	set_user_vblank(atari_vblank);
	
	worst_case_test();
}
