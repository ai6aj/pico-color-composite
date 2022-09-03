#ifndef __FRAMEBUFFER_H__
#define __FRAMEBUFFER_H__
#include "pcc-video-core.h"

/**********************************
 FRAMEBUFFER STUFF
 **********************************/
extern uint8_t palette[256][4];

void drawline (int x0, int y0, int x1, int y1, uint8_t color);

/**	Set a palette entry to a (R,G,B) tuple specified as floats.

	num		The palette entry to use.
	r		The Red value, from 0.0-1.0
	g		The Green value, from 0.0-1.0
	b		The Blue value, from 0.0-1.0
*/
void setPaletteRGB_float(int num,float r, float g, float b);

/**	Set a palette entry to a (R,G,B) tuple specified as bytes.

	num		The palette entry to use.
	r		The Red value, from 0-255
	g		The Green value, from 0-255
	b		The Blue value, from 0-255
*/
void setPaletteRGB(int num,uint8_t r, uint8_t g, uint8_t b);

/**
	Generate a palette entry from an NTSC phase/intensity/luma
	triplet.  Given the complexity of NTSC encoding it's highly recommended
	to use setPaletteRGB instead.
	
	num					The number of the palette entry.
	chroma_phase		Phase of the chroma signal with respect to colorburst
	chroma_amplitude	Amplitude of the chroma signal
	luminance			The black and white portion of the signal

*/
void setPaletteNTSC(int num,float chroma_phase,float chroma_amplitude,float luminance);

extern int vscrol;
extern int hscrol;
void init_framebuffer();
void drawline (int x0, int y0, int x1, int y1, uint8_t color);

#endif
