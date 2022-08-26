#include "ntsc-video-core.h"
#include "ntsc-video-core.h"
#include "pico-ntsc.pio.h"

	// PALFIX

const uint LED_PIN = PICO_DEFAULT_LED_PIN;


int irq_count = 0;

// Enables colorburst if true, set to 0 to get
// 640+ horizontal B&W pixels on some TVs!
int do_color = 1;




/**********************************
 FRAMEBUFFER STUFF
 **********************************/
uint8_t palette[256][4];
volatile int in_vblank = 0;


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


/* --------------------------------
	DISPLAY LIST LOGIC
   --------------------------------
  
Here we borrow heavily from Atari's ANTIC display processor.  

JMP - not useful for norm
JMP AND WAIT FOR VBLANK

/* --------------------------------
 SIGNAL GENERATOR
   -------------------------------- */
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

uint8_t __scratch_x("ntsc") pingpong_line_0[LINE_WIDTH];
uint8_t __scratch_y("ntsc") pingpong_line_1[LINE_WIDTH];
 
uint8_t* pingpong_lines[] = { pingpong_line_0, pingpong_line_1 };



void make_vsync_line() {
	int ofs = 0;
	memset(vblank_line,BLANKING_VAL,LINE_WIDTH);
	
	// PALFIX
	// This is almost certainly wrong for PAL
	memset(vblank_line,SYNC_VAL,VBLANK_CLOCKS); 		

	
	memset(vblank_odd_line,BLANKING_VAL,LINE_WIDTH);
	memset(vblank_odd_line,SYNC_VAL,SYNC_TIP_CLOCKS);

	// PALFIX
	// This is almost certainly wrong for PAL
//	memset(&vblank_odd_line[LINE_WIDTH/2],SYNC_VAL,VBLANK_CLOCKS);
	memset(vblank_odd_line,SYNC_VAL,VBLANK_CLOCKS);
}

void make_color_burst(uint8_t* line, int use_alternate_phase) {
	uint8_t c1 = COLOR_BURST_HI_VAL;
	uint8_t c2 = COLOR_BURST_LO_VAL;
	
	float cb_scale = (c1-c2);
	float phase = COLOR_BURST_PHASE_DEGREES/180.0 * 3.14159;	// Starting phase.
	
	// PALFIX
	// Is this correct for PAL?
	#ifdef USE_PAL
	for (int i=0; i<11; i++) {
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
	
	if (do_color) {
		make_color_burst(black_line,0);
	}
	
	// Need to alternate phase for line 2

	#ifdef ALTERNATE_COLORBURST_PHASE
	
	if (do_color) {
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
	memset(dest,BLACK_LEVEL,LINE_WIDTH);
	
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
int do_interlace = 1;




int line = 0;
int frame = 0;
uint8_t*	next_dma_line = vblank_line;

/* --------------------------------------------------------------------------------------------------------------
 	Generate video using a display list.  
   --------------------------------------------------------------------------------------------------------------
*/



// Basic display list for a 200 line display.
display_list_t sample_display_list[] = { DISPLAY_LIST_USER_RENDER_RAW, 200,
							  DISPLAY_LIST_WVB,0 };

// Our initial display list is just a black display.  This allows the TV time to 
// lock VBLANK.
volatile int startup_frame_counter = STARTUP_FRAME_DELAY;
display_list_t startup_display_list[] = { DISPLAY_LIST_WVB, 0 };

display_list_t* display_list_ptr = startup_display_list;
display_list_t* next_display_list = sample_display_list;

void set_display_list(display_list_t *display_list) {
	next_display_list = display_list;
}


display_list_t  display_list_current_cmd = 0;
display_list_t display_list_lines_remaining = 0;
display_list_t display_list_ofs = 0;

/*
	User routine to generate a line
	
	This is how Pico-XL will simulate ANTIC.
*/
int framebuffer_line_offset = 35;

/*
uint8_t user_line[320];


static uint8_t* __not_in_flash_func(user_render_ex)(uint line) {
	for (int i=0; i<160; i++) { user_line[i] = framebuffer[line-framebuffer_line_offset][i]; };
	return user_line;
}
*/

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

user_render_raw_func_t	*user_render_raw_func = NULL;
user_vblank_func_t *user_vblank_func = NULL;
user_new_line_func_t *user_new_line_func = NULL;

void set_user_render_raw(user_render_raw_func_t *f) {
	user_render_raw_func = f;
}

void set_user_vblank(user_vblank_func_t *f) {
	user_vblank_func = f;
}

// Typically used for audio output
void set_user_new_line_callback(user_new_line_func_t *f) {
	user_new_line_func = f;
}


static void __not_in_flash_func(user_render_160)(uint line, uint8_t* dest,user_render_func_t user_func) {
		uint8_t* sourceline = user_func(line);
		uint8_t* colorptr;
		frcount++;
		int ofs = VIDEO_START;

		for (int i=0; i<320; i++) {
			colorptr = palette[sourceline[i]];
		
			// The compiler will optimize this		
			dest[ofs] = colorptr[0];
			dest[ofs+1] = colorptr[1];
//			dest[ofs+2] = colorptr[2];
//			dest[ofs+3] = colorptr[3];
			ofs += 4;
		}
}

user_render_func_t user_render_func = NULL; 
// Called back on every new line, with line #

/*

	Generate a video line from the framebuffer
	
*/

static void __not_in_flash_func(cvideo_dma_handler)(void) {
	
	// Set the next DMA access point and reset the interrupt
    dma_channel_set_read_addr(dma_channel, next_dma_line, true);
    dma_hw->ints0 = 1 << dma_channel;		

	// User line callback goes here so we don't get a bunch
	// of jitter in audio.
	if (user_new_line_func != NULL) { user_new_line_func(line); }


	// We should ALWAYS set the read address to whatever we decided it should be
	// last time, unless we're in vblank.  Then we determine what the next DMA line is.
	// This way we don't get an extra line(s) when switching between black lines and 
	// framebuffer lines.

	


	// VBLANK should start on line 258.
	if (line >= LINES_PER_FRAME) {		
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

		if (startup_frame_counter) {
			startup_frame_counter--;
			if (startup_frame_counter == 0) {
				display_list_ptr = next_display_list;			
			}
		} else {			
			display_list_ptr = next_display_list;
		}

		display_list_ofs = 0;
		display_list_current_cmd = 0;
		display_list_lines_remaining = 0;
		
		if (user_vblank_func != NULL) {
			user_vblank_func();
		}
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
					// reset by VBLANK, since every time we pass through
					// this switch() display_list_lines_remaining will be reset to 2
					display_list_lines_remaining = 2;
					// Otherwise it's a black line

				default:
				case DISPLAY_LIST_BLACK_LINE:
					next_dma_line = black_lines[line & 1];
					break;
										
				case DISPLAY_LIST_USER_RENDER:
					next_dma_line = pingpong_lines[line & 1];
					user_render(line,pingpong_lines[line & 1],user_render_func);
					break;
					
				case DISPLAY_LIST_USER_RENDER_RAW:
				    //next_dma_line = black_lines[line & 1];
					next_dma_line = pingpong_lines[line & 1];			
					user_render_raw_func(line,VIDEO_START,pingpong_lines[line & 1]);
					break;
			}
			
			if (display_list_lines_remaining) { display_list_lines_remaining--; }
			line++;
	}
	
}




void init_video_lines() {
	// Initialize video_line to alternating 1s and 2s
	make_vsync_line();
	
	// PALFIX
	// Is this needed?
	for (int i=0; i<768; i++) {
		test_line[i] = i/3;
	}
	
	make_normal_line(black_lines[0],do_color,0);
	make_normal_line(black_lines[1],do_color,0);
	make_normal_line(pingpong_lines[0],do_color,0);
	make_normal_line(pingpong_lines[1],do_color,0);

	// Is this needed?
	pingpong_lines[0][800] = 31;
	pingpong_lines[0][801] = 31;
	pingpong_lines[1][800] = 31;
	pingpong_lines[1][801] = 31;

	// Is this needed?
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

user_video_core_loop_func_t *user_video_core_loop_func = NULL; 

volatile uint32_t current_idle_count = -1;
volatile uint32_t unloaded_idle_count = 0;
float get_video_core_load() {
	if (current_idle_count == -1) return -1;
	return 1-(float)(current_idle_count)/(float)(unloaded_idle_count);
}

void set_user_video_core_loop(user_video_core_loop_func_t f) {
	user_video_core_loop_func = f;
}


void ntsc_video_core() {
	
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &ntsc_composite_program);

	gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
	
    start_video(pio, 0, offset, FIRST_GPIO_PIN, DAC_BITS);
	
	uint32_t counter = 0;

	if (user_video_core_loop_func != NULL) {
		user_video_core_loop_func();
	}
		
	while (1) {
			while (!in_vblank) counter++;
			if (startup_frame_counter == 0) {
				current_idle_count = counter;
			} else {
				unloaded_idle_count = counter;				
			}
			counter = 0;		
			while (in_vblank);
	}
}
