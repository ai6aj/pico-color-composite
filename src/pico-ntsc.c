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

#define USE_OVERCLOCK	0

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

void video_core();

int main() {
#ifdef USE_OVERCLOCK
	// Rock solid signal
	set_sys_clock_pll(1260000000,4,2);
#else
	// A more typical frequency, but 
	// shows a fair amount of jitter
	// on the scope
	set_sys_clock_khz(1250000,true);
#endif

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

// Note that you can probably improve color rendition
// By making each line 227.5 clocks.  Since we output
// at 2x resolution this is feasible.
#define ALTERNATE_COLORBURST_PHASE
#define LINE_WIDTH (227*2-1)
#define MAX_DAC_OUT	31
#define MIN_DAC_OUT 0
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
#define SYNC_TIP_CLOCKS 	33
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

uint8_t vblank_line[LINE_WIDTH+1];
uint8_t black_line[LINE_WIDTH+1];
uint8_t black_line_2[LINE_WIDTH];

uint8_t* black_lines[2] = {black_line,black_line_2 };
uint8_t pingpong_lines[2][LINE_WIDTH];

//uint8_t video_line[LINE_WIDTH+1];
//uint8_t video_line2[LINE_WIDTH+1];

// #define IN_LIVING_COLOR 1

void make_vsync_line() {
	int ofs = 0;
	memset(vblank_line,BLANKING_VAL,LINE_WIDTH);
	memset(vblank_line,SYNC_VAL,210*2);
}

void make_black_line() {
	// Set everything to the blanking level
	memset(black_line,BLANKING_VAL,LINE_WIDTH);
	
	// Do the HSYNC pulse.  The front porch has been drawn
	// by the previous DMA transfer.
	// 4.7uS = 33.6477 clocks,  
	memset(black_line,SYNC_VAL,SYNC_TIP_CLOCKS);
	
	// Do our colorburst
	
	// Starts at 5.3uS = 37.94318 clocks
	for (int i=0; i<10; i++) {
		black_line[38+i*2] = COLOR_BURST_HI_VAL;
		black_line[39+i*2] = COLOR_BURST_LO_VAL;
	}

	memcpy(black_line_2,black_line,LINE_WIDTH);

	// Start HSYNC here
	black_line[LINE_WIDTH-1] = 0;

	// Need to alternate phase for line 2

	#ifdef ALTERNATE_COLORBURST_PHASE
	for (int i=0; i<10; i++) {
		black_line[38+i*2] = COLOR_BURST_LO_VAL;
		black_line[39+i*2] = COLOR_BURST_HI_VAL;
	}
	#endif
	
	// Video officially begins at 9.4uS / 67.2954 clocks
	// Start at 68 to be in phase with clock signal	
}

void make_video_line() {
	memcpy(pingpong_lines[0],black_line,LINE_WIDTH);
	memcpy(pingpong_lines[1],black_line_2,LINE_WIDTH);

	for (int i=0; i<188; i++) {
		if (i & 1) {
			pingpong_lines[0][67+i] = 11;
			pingpong_lines[1][67+i] = 31;
		}
		else {
			pingpong_lines[0][67+i] = 31;
			pingpong_lines[1][67+i] = 11;
		}
	}	
	
	for (int i=188; i<188+191; i++) {
		if (i & 1) {
			pingpong_lines[0][67+i] = 31;
			pingpong_lines[1][67+i] = 11;
		}
		else {
			pingpong_lines[0][67+i] = 11;	
			pingpong_lines[1][67+i] = 31;
		}
	}
	
}


int line = 0;

static void __not_in_flash_func(cvideo_dma_handler)(void) {
	if (line == 262) {
	    dma_channel_set_read_addr(dma_channel, vblank_line, true);
		line = 0;
	} else if (line<20) {
	    dma_channel_set_read_addr(dma_channel, black_lines[line & 1], true);
		line++;	
	}
	else {
	    dma_channel_set_read_addr(dma_channel, pingpong_lines[line & 1], true);
		line++;
	}

/*	
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
*/
	// Need to reset the interrupt	
    dma_hw->ints0 = 1u << dma_channel;		
}

void start_video(PIO pio, uint sm, uint offset, uint pin, uint num_pins) {

	// Initialize video_line to alternating 1s and 2s
	make_black_line();
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
	pio->sm[sm].clkdiv = ((17 << 16) + (118 << 8));
#endif


	// Start the state machine
    pio_sm_set_enabled(pio, sm, true);
}
