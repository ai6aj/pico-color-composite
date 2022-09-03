#include "framebuffer.h"
#include <math.h>


/**********************************
 FRAMEBUFFER STUFF
 **********************************/
uint8_t palette[256][4];


/*
	Set the total line width, in color clocks.
	ALTERNATE_COLORBURST_PHASE will generate proper 227.5
	color clock lines but is only partially supported at
	the moment (and doesn't seem to be necessary.)
	
	Note that a lot of old equipment uses 228 color clock
	lines; a lot of new equipment doesn't sync well to this
	but is just fine with 226 color clocks.  
*/


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
	
	int tmp = BLACK_LEVEL + sin(chroma_phase + VIDEO_START_PHASE_SHIFT)*sat_scaled + luminance*LUMA_SCALE;
	tmp = tmp < BLACK_LEVEL ? BLACK_LEVEL : tmp;
	tmp = tmp > WHITE_LEVEL ? WHITE_LEVEL : tmp;
	
	
	palette[num][0] = tmp;
	tmp = BLACK_LEVEL + sin(chroma_phase+3.14159/2 + VIDEO_START_PHASE_SHIFT)*sat_scaled + luminance*LUMA_SCALE;
	tmp = tmp < BLACK_LEVEL ? BLACK_LEVEL : tmp;
	tmp = tmp > WHITE_LEVEL ? WHITE_LEVEL : tmp;
	
	palette[num][1] = tmp;
	
	tmp = BLACK_LEVEL + sin(chroma_phase+3.14159 + VIDEO_START_PHASE_SHIFT)*sat_scaled + luminance*LUMA_SCALE;
	tmp = tmp < BLACK_LEVEL ? BLACK_LEVEL : tmp;
	tmp = tmp > WHITE_LEVEL ? WHITE_LEVEL : tmp;
	palette[num][2] = tmp;
	
	tmp = BLACK_LEVEL + sin(chroma_phase+3.14159*3/2 + VIDEO_START_PHASE_SHIFT)*sat_scaled + luminance*LUMA_SCALE;
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
	float h = atan2(v,u); // + (55/180 * 3.14159 * 2);
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



uint16_t framebuffer_line = 0;

#define FRAMEBUFFER_HIRES

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

#define CHARBUFFER
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
	line >>= 1;
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

	// Generate the initial RGB palette
	for (int i=0; i<256; i++) {
		setPaletteRGB(i,(i & 0xE0), (i & 0x1C) << 3, (i & 0x3) << 6);
	}
		
	// Fill the charBuffer

	
	for (int i=0; i<256; i++) {
		setPaletteRGB(i,(i & 0xE0), (i & 0x1C) << 3, (i & 0x3) << 6);
		for (int n=0; n<64; n++) {
			charSet[i*64+n] = i;
		}
		charSet[i*64+(i & 0x7)*8 + (i & 0x7)] = 255;
	}


	for (int y=0; y<30; y++) {
		for (int x=0; x<48; x++) {
			for (int n=0; n<64; n++) {
				charBuffer[y*48+x] = ((y*48+x) & 0xFF);
			}
		}
	}

	pcc_set_user_render_raw(charbuffer_render);

/*
	pcc_set_user_vblank(framebuffer_vblank);



	framebuffer[128][128] = 0xFF;
	pcc_set_user_render_raw(framebuffer_render);
	pcc_set_user_vblank(framebuffer_vblank);
	for (int i=50; i<150; i++)
		drawline(10,i,100,i,0x1C);
	
	drawline(0,5,383,5,0xFF);
	drawline(191,10,191,229,0xFF);
	drawline(383,232,0,232,0xFF);
	drawline(0,229,0,10,0xFF);
*/

}
