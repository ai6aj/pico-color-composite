/**
	
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

#define SYS_CLOCK_KHZ	266000

int main() {
	set_sys_clock_khz(SYS_CLOCK_KHZ,true);

    stdio_init_all();

	multicore_launch_core1(video_core);

	while(1) {
		printf("Hello, world.\n");
		sleep_ms(500);
	}
}

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
#define VIDEO_START			COLOR_BURST_START+SAMPLES_PER_CLOCK*30
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
uint8_t pingpong_lines[2][LINE_WIDTH];

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

uint8_t* video_lines[240];

int even_frame = 1; 	// 0 = odd, 1 = even

static void __not_in_flash_func(make_video_line)(uint line) {
	memcpy(pingpong_lines[0],black_line,LINE_WIDTH);
	memcpy(pingpong_lines[1],black_line_2,LINE_WIDTH);

//	int phase[16] = { 0,4,7,9,10,9,7,4,0,-4,-7,-9,-10,-9,-7,-4 };

	int phase[16] = { 0,1,2,3,
		3,3,2,1,
		0,-1,-2,-3,
		-3,-3,-2,-1};

		
	int ph = 14;

	uint8_t* nextline = (line & 1) ? pingpong_lines[0] : pingpong_lines[1];

//	memset(nextline[VIDEO_STAR

	if (even_frame) {
		for (int i=VIDEO_START; i<VIDEO_LENGTH; i+=SAMPLES_PER_CLOCK) {
				nextline[i+0] = 15-(line & 0xF);
				nextline[i+1] = 15+(line & 0xF);
				nextline[i+2] = 15+(line & 0xF);
				nextline[i+3] = 15-(line & 0xF);
		}	
	} else {
	for (int i=VIDEO_START; i<VIDEO_LENGTH; i+=SAMPLES_PER_CLOCK) {
				nextline[i+0] = 15;
				nextline[i+1] = 15;
				nextline[i+2] = 15;
				nextline[i+3] = 15;
		}
	}
	
}


int line = 0;
int frame = 0;

// Interlacing barely functions right now.
// Need to check the spec.  May be sending the
// wrong length HSYNC, or sending it on the wrong line,
// or the line count may be wrong.
//
// While not strictly necessary, it's a nice touch.
//
// #define DO_INTERLACE

int do_interlace = 1;

static void __not_in_flash_func(cvideo_dma_handler)(void) {

	if (line >= 262) {
		if (even_frame) {
		    dma_channel_set_read_addr(dma_channel, vblank_line, true);
			line = 0;
			frame++;
			if (do_interlace) even_frame = 0;
		}
		else {
		    dma_channel_set_read_addr(dma_channel, vblank_odd_line, true);
			line = 0;
			frame++;
			even_frame = 1;
		} 
	} else if (line < 20) {
			dma_channel_set_trans_count(dma_channel, LINE_WIDTH, false);
		    dma_channel_set_read_addr(dma_channel, black_lines[line & 1], true);
			line++;	
	} else {
		    dma_channel_set_read_addr(dma_channel, pingpong_lines[line & 1], true);
			make_video_line(++line);
	}
			
	// Need to reset the interrupt	
    dma_hw->ints0 = 1u << dma_channel;		
}



void init_video_lines() {
	// Initialize video_line to alternating 1s and 2s
	make_vsync_line();
	make_black_line();
	make_video_line(1);
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
	pio->sm[sm].clkdiv = (uint32_t) (PIO_clkdiv * (1 << 16)); // INT portion: 0xffff0000, FRAC portion: 0x0000ff00	

	// Start the state machine
    pio_sm_set_enabled(pio, sm, true);
}
