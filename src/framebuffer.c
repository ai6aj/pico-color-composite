#include "framebuffer.h"


#ifdef USE_PAL
	#define	BLACK_LINE_COUNT	30
#else
	#define	BLACK_LINE_COUNT	18
#endif
display_list_t __scratch_y("ntsc_framebuffer") framebuffer_display_list[] = { 
	DISPLAY_LIST_BLACK_LINE, BLACK_LINE_COUNT,
	DISPLAY_LIST_USER_RENDER_RAW, 240,
	DISPLAY_LIST_WVB,0 };

uint16_t framebuffer_line = 0;

#define CHARBUFFER 1

#ifdef FRAMEBUFFER_LORES
uint8_t framebuffer[240][192];
#endif

#ifdef FRAMEBUFFER_HIRES
uint8_t framebuffer[240][384];
	
void drawline (int x0, int y0, int x1, int y1, uint8_t color)
{
  int dx =  abs (x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs (y1 - y0), sy = y0 < y1 ? 1 : -1; 
  int err = dx + dy, e2; // error value e_xy 
  for (;;){  // loop 
	framebuffer[y0][x0] = color; // putpixel(x,y,7);
    if (x0 == x1 && y0 == y1) break;
    e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; } // e_xy+e_x > 0 
    if (e2 <= dx) { err += dx; y0 += sy; } // e_xy+e_y < 0 
  }
}
#endif

static void __not_in_flash_func(framebuffer_vblank)() {
	framebuffer_line = 0;
}

#ifdef FRAMEBUFFER_LORES
static void __not_in_flash_func(framebuffer_render)(uint line, uint video_start, uint8_t* output_buffer) {

	uint32_t* dest = (uint32_t*)(&output_buffer[video_start]);
	uint32_t* palette32 = (uint32_t*)palette;		
	uint32_t framebuffer_line_ofs = framebuffer_line * 192;
	
	for (int i=0; i<192; i++) {
		*(dest++) = palette32[framebuffer[framebuffer_line][i]];
	}
	framebuffer_line++;

}
#endif

	
#ifdef FRAMEBUFFER_HIRES
static void __not_in_flash_func(framebuffer_render)(uint line, uint video_start, uint8_t* output_buffer) {

	uint16_t* dest = (uint16_t*)(&output_buffer[video_start]);	
	uint16_t* palette16 = (uint16_t*)palette;
		
	for (int i=0; i<384; i+=2) {
		*(dest++) = palette16[framebuffer[framebuffer_line][i] * 2];
		*(dest++) = palette16[framebuffer[framebuffer_line][i+1] * 2 + 1];
	}
	
	framebuffer_line++;
}
#endif

#ifdef CHARBUFFER
// Valid tile sizes:
// 22x16		 (17x15 char array)
// 11x8			 (34x30 char array)
// 8x8			 (48x30 char array)
// (8/11/22)x10	 (48/34/17)x24
// (8/11/22)x12	 (48/34/17)x20

uint8_t	charBuffer[30*48];
uint8_t	charSet[10*8*256];

int chHeight = 8;

int chStride = 48;	// Can be increased for scrolling purposes.
int vscrol = 0;
int hscrol = 0;


static void __not_in_flash_func(charbuffer_render)(uint line, uint video_start, uint8_t* output_buffer) {	

	uint16_t* dest = (uint16_t*)(&output_buffer[video_start]);	
	uint16_t* palette16 = (uint16_t*)palette;
	// line >>= 1;
/*
	if (xeven_frame) {
		for (int i=0; i<384; i+=2) {
			*(dest++) = palette16[0];
			*(dest++) = palette16[1];
		}
		return;
	}
*/
	
	// Start of the line of characters in our character framebuffer.
	int chBufOfs = chStride * ((line+vscrol) / chHeight) + (hscrol >> 3);
	uint8_t* charLine = charBuffer + chBufOfs;
	int chGridSize = 8*chHeight;
	
	// Y offset into the character data.
	int chLineOfs = (line+vscrol) % chHeight;

	// Starting X offset into the character data.
	int chCol = (hscrol & 0x7);

	// Start at character 0.  We will reload ch every time
	// chCol goes to 0.
	int chOfs = 0;
	int ch = charLine[chOfs++];

	int chLineOfsTimeschHeight = chLineOfs*chHeight;
	int chTimesChGridSize = ch * chGridSize;
	int charSetEntryOfs = chTimesChGridSize+chLineOfsTimeschHeight;
	
	if (hscrol & 1) {
		*(dest++) = palette16[charSet[charSetEntryOfs+chCol] * 2];
		chCol++;
		chCol &= 0x7;
		if (chCol == 0) {
			ch = charLine[chOfs++];
			chTimesChGridSize = ch * chGridSize;
			charSetEntryOfs = chTimesChGridSize+chLineOfsTimeschHeight;
		}

		for (int i = 1; i < 384; i+=2) {
			*(dest++) = palette16[charSet[charSetEntryOfs+chCol] * 2 + 1];
			*(dest++) = palette16[charSet[charSetEntryOfs+chCol+1] * 2];
			chCol+=2;
			chCol &= 0x7;
			if (chCol == 0) {
				ch = charLine[chOfs++];
				chTimesChGridSize = ch * chGridSize;
				charSetEntryOfs = chTimesChGridSize+chLineOfsTimeschHeight;
			}
		}		
	}
	else 
	{
		for (int i = 0; i < 384; i+=2) {
			*(dest++) = palette16[charSet[charSetEntryOfs+chCol] * 2];
			*(dest++) = palette16[charSet[charSetEntryOfs+chCol+1] * 2 + 1];
			chCol+=2;
			chCol &= 0x7;
			if (chCol == 0) {
				ch = charLine[chOfs++];
				chTimesChGridSize = ch * chGridSize;
				charSetEntryOfs = chTimesChGridSize+chLineOfsTimeschHeight;
			}
		}
	}
	
	framebuffer_line++;
}
#endif

void init_framebuffer() {

	for (int i=0; i<256; i++) {
		setPaletteRGB(i,(i & 0xE0), (i & 0x1C) << 3, (i & 0x3) << 6);
		for (int n=0; n<64; n++) {
			charSet[i*64+n] = i;
		}
		charSet[i*64+(i & 0x7)*8 + (i & 0x7)] = 255;

	}

	// Fill the charBuffer
	for (int y=0; y<30; y++) {
		for (int x=0; x<48; x++) {
			for (int n=0; n<64; n++) {
				charBuffer[y*48+x] = ((y*48+x) & 0xFF);
			}
		}
	}

	set_user_render_raw(charbuffer_render);
	set_user_vblank(framebuffer_vblank);
	set_display_list(framebuffer_display_list);	


/*
	framebuffer[128][128] = 0xFF;
	set_user_render_raw(framebuffer_render);
	set_user_vblank(framebuffer_vblank);
	set_display_list(framebuffer_display_list);	
	for (int i=50; i<150; i++)
		drawline(10,i,100,i,0x1C);
	
	drawline(0,5,383,5,0xFF);
	drawline(191,10,191,229,0xFF);
	drawline(383,232,0,232,0xFF);
	drawline(0,229,0,10,0xFF);
*/

}
