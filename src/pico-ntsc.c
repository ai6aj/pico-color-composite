/**
 * Multicore Blink - core 0 turns the LED on, core 1 turns it off
 *
 * Modified from Blink example for Raspberry Pi Pico
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include <stdio.h>

// #include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/dma.h"
#include "pico-ntsc.pio.h"


#ifndef PICO_DEFAULT_LED_PIN
#warning blink example requires a board with a regular LED
#endif

#define USE_OVERCLOCK	1

const uint LED_PIN = PICO_DEFAULT_LED_PIN;

/*
	Some NTSC timing information in terms of color clocks:
	
	1 full line = ~63.5uS = 227.50 clocks
	Front porch = ~1.5uS = 5.37 clocks, round to 5
	Sync tip = ~4.7uS = 16.82 clocks, round to 17
	Breezeway = 0.6uS = 2.14 clocks, round to 2
	Color burst = exactly 9 clocks per spec
	Back porch = ~1.6uS = 5.7 clocks, round to 6
	Total HSYNC = 39 clocks = (5+17+2+9+6)
	
	Video information: 52.6uS, 224 color clocks
	
	Total line size: 263 clocks
	
	VBlank = 21 lines at 0.3V (front porch level) ?
	
	See:
    http://www.ifp.illinois.edu/~yuhuang/ntscdecoding.htm
	https://www.technicalaudio.com/pdf/Grass_Valley/Grass_Valley_NTSC_Studio_Timing.pdf
*/
void start_video(PIO pio, uint sm, uint offset, uint pin, uint num_pins);

int irq_count = 0;

int main() {
#ifdef USE_OVERCLOCK
	// Rock solid signal
	set_sys_clock_pll(1260000000,4,2);
#else
	// A more typical frequency, but 
	// shows a fair amount of jitter
	// on the scope
	set_sys_clock_khz(125000,true);
#endif

    stdio_init_all();

    // todo get free sm
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &ntsc_composite_program);
    printf("Loaded program at %d\n", offset);


	gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
	
    start_video(pio, 0, offset, 16, 5);
	
	while (1) {
		printf("Hello world %d\n",irq_count);
		sleep_ms(1000);
	}
}


uint dma_channel;

// Note that you can probably improve color rendition
// By making each line 227.5 clocks.  Since we output
// at 2x resolution this is feasible.
#define LINE_WIDTH (228*2)
#define MAX_DAC_OUT	31
#define MIN_DAC_OUT 1
#define DAC_RANGE (MAX_DAC_OUT-MIN_DAC_OUT)


#define BLANKING_VAL		(uint)(((40.0/140.0)*(float)DAC_RANGE)+MIN_DAC_OUT+0.5)
#define SYNC_VAL			MIN_DAC_OUT
#define BLACK_VAL			(uint)(((47.5/140.0)*(float)DAC_RANGE)+MIN_DAC_OUT+0.5)
#define COLOR_BURST_HI_VAL	(uint)(((60.0/140.0)*(float)DAC_RANGE)+MIN_DAC_OUT+0.5)
#define COLOR_BURST_LO_VAL	(uint)(((20.0/140.0)*(float)DAC_RANGE)+MIN_DAC_OUT+0.5)
#define VIDEO
#define FRONT_PORCH_CLOCKS	5*2
#define BLANKING_IRE		0
#define FRONT_PORCH_VALUE	BLANKING_IRE
#define SYNC_TIP_CLOCKS 	(17*2)
#define SYNC_TIP_IRE		-40
#define BREEZEWAY_CLOCKS	2*2
#define COLOR_BURST_CLOCKS	9
#define BACK_PORCH_CLOCKS	6*2
#define VIDEO_CLOCKS		188*2

/*
	Color burst = exactly 9 clocks per spec
	Back porch = ~1.6uS = 5.7 clocks, round to 6
	Total HSYNC = 39 clocks = (5+17+2+9+6)
	
	Video information: 52.6uS, 224 color clocks
	Total line width: 227.5
	
	Total lines: 262.5 lines, usually 262 are written
*/

uint8_t vblank_line[LINE_WIDTH];
uint8_t black_line[LINE_WIDTH];
uint8_t video_line[LINE_WIDTH];

// #define IN_LIVING_COLOR 1

void make_vsync_line() {
	int ofs = 0;
	for (int i=0; i<LINE_WIDTH; i++) {
		vblank_line[i] = BLANKING_VAL;
	}
	
	for (int i=0; i<(210*2+1); i++) {
		// Long VSYNC pulse
		vblank_line[i] = SYNC_VAL;
	}
	
	for (int i=0; i<FRONT_PORCH_CLOCKS; i++) {
		vblank_line[LINE_WIDTH-FRONT_PORCH_CLOCKS-1+i] = BLANKING_VAL;
	}
	
	vblank_line[LINE_WIDTH-1] = 0;
}

void make_video_line() {
	int ofs=0;
	
	for (int i=0; i<SYNC_TIP_CLOCKS; i++) {
		black_line[ofs] = SYNC_VAL;
		video_line[ofs++] = SYNC_VAL;		
	}
	
	for (int i=0; i<BREEZEWAY_CLOCKS; i++) {
		black_line[ofs] = BLANKING_VAL;
		video_line[ofs++] = BLANKING_VAL;		
	}

	for (int i=0; i<COLOR_BURST_CLOCKS; i++) {
		#ifdef IN_LIVING_COLOR
			black_line[ofs] = COLOR_BURST_HI_VAL;
			video_line[ofs++] = COLOR_BURST_HI_VAL;		
			black_line[ofs] = COLOR_BURST_LO_VAL;
			video_line[ofs++] = COLOR_BURST_LO_VAL;		
		#else
			black_line[ofs] = BLANKING_VAL;
			video_line[ofs++] = BLANKING_VAL;		
			black_line[ofs] = BLANKING_VAL;
			video_line[ofs++] = BLANKING_VAL;		
		#endif
	}

	for (int i=0; i<BACK_PORCH_CLOCKS; i++) {
		black_line[ofs] = BLANKING_VAL;
		video_line[ofs++] = BLANKING_VAL;		
	}


	for (int i=0; i<188; i++) {
		black_line[ofs] = BLACK_VAL;
		if (i & 1) 
			video_line[ofs++] = 21 + (i % 10);
		else
			video_line[ofs++] = 21;
	}

	for (int i=0; i<188; i++) {
		black_line[ofs] = BLACK_VAL;
			video_line[ofs++] = 30;
	}


	black_line[ofs] = BLACK_VAL;
	video_line[ofs++] = BLACK_VAL;
	
	for (int i=0; i<FRONT_PORCH_CLOCKS; i++) {
		black_line[ofs] = BLACK_VAL;
		video_line[ofs++] = BLANKING_VAL;
	} 
	
	// Last pixel is only 1/2 clock
//	video_line[LINE_WIDTH-2] = BLACK_VAL;
	black_line[ofs] = 0;
	video_line[ofs++] = 0;
}

int led_on = 0;
// int irq_count = 0;

int line = 0;

void cvideo_dma_handler(void) {
	if (line == 262) {
	    dma_channel_set_read_addr(dma_channel, vblank_line, true);
		line = 0;
	} else if (line<100) {
	    dma_channel_set_read_addr(dma_channel, black_line, true);
		line++;	
	}
	else {
	    dma_channel_set_read_addr(dma_channel, video_line, true);
		line++;
	}
	
	irq_count++;
	if (irq_count > 1000) {
		irq_count = 0;
		if (led_on) {
			gpio_put(LED_PIN, 0);	
			led_on = 0;
		} else {
			gpio_put(LED_PIN, 1);	
			led_on = 1;	
		}
	}

	// Need to reset the interrupt	
    dma_hw->ints0 = 1u << dma_channel;		
}

void start_video(PIO pio, uint sm, uint offset, uint pin, uint num_pins) {

	// Initialize video_line to alternating 1s and 2s
	make_video_line();
	make_vsync_line();
			
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

	
	// Set the clock divisor.
#ifdef USE_OVERCLOCK
	pio->sm[sm].clkdiv = (2 << 16 + 0);
#else
	pio->sm[sm].clkdiv = ((17 << 16) + (117 << 8));
#endif


	// Start the state machine
    pio_sm_set_enabled(pio, sm, true);
}
