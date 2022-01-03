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
void blink_pin_forever(PIO pio, uint sm, uint offset, uint pin, uint freq);

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

    blink_pin_forever(pio, 0, offset, 1, 1);
	
	while (1) {
		printf("Hello world\n");
		sleep_ms(1000);
	}
}

void blink_pin_forever(PIO pio, uint sm, uint offset, uint pin, uint freq) {
    ntsc_composite_program_init(pio, sm, offset, pin, 5);
    printf("Blinking pin %d at %d Hz\n", pin, freq);
    pio_sm_set_enabled(pio, sm, true);
	
	// Set the clock divisor.
#ifdef USE_OVERCLOCK
	pio->sm[sm].clkdiv = (22 << 16 + 0);
#else
	pio->sm[sm].clkdiv = ((17 << 16) + (117 << 8));
#endif

}



/* ----

Example code for a PAL b&w composite output from
https://github.com/breakintoprogram/pico-mposite/blob/main/cvideo.c


#define state_machine 0     // The PIO state machine to use
#define width 256           // Bitmap width in pixels
#define height 192          // Bitmap height in pixels
#define hsync_bp1 24        // Length of pulse at 0.0v
#define hsync_bp2 48        // Length of pulse at 0.3v
#define hdots 382           // Data for hsync including back porch
#define piofreq 7.0f        // Clock frequence of state machine
#define border_colour 11    // The border colour

#define pixel_start hsync_bp1 + hsync_bp2 + 18  // Where the pixel data starts in pixel_buffer

uint dma_channel;           // DMA channel for transferring hsync data to PIO
uint vline;                 // Current video line being processed
uint bline;                 // Line in the bitmap to fetch

#include "bitmap.h"         // The demo bitmap

unsigned char vsync_ll[hdots+1];					// buffer for a vsync line with a long/long pulse
unsigned char vsync_ls[hdots+1];					// buffer for a vsync line with a long/short pulse
unsigned char vsync_ss[hdots+1];					// Buffer for a vsync line with a short/short pulse
unsigned char border[hdots+1];						// Buffer for a vsync line for the top and bottom borders
unsigned char pixel_buffer[2][hdots+1];	        	// Double-buffer for the pixel data scanlines

int main() {
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &cvideo_program);	// Load up the PIO program

    dma_channel = dma_claim_unused_channel(true);			// Claim a DMA channel for the hsync transfer
    vline = 1;												// Initialise the video scan line counter to 1
    bline = 0;												// And the index into the bitmap pixel buffer to 0

    write_vsync_l(&vsync_ll[0],        hdots>>1);			// Pre-build a long/long vscan line...
    write_vsync_l(&vsync_ll[hdots>>1], hdots>>1);
    write_vsync_l(&vsync_ls[0],        hdots>>1);			// A long/short vscan line...
    write_vsync_s(&vsync_ls[hdots>>1], hdots>>1);
    write_vsync_s(&vsync_ss[0],        hdots>>1);			// A short/short vscan line
    write_vsync_s(&vsync_ss[hdots>>1], hdots>>1);

    // This bit pre-builds the border scanline
    //
    memset(&border[0], border_colour, hdots);				// Fill the border with the border colour
    memset(&border[0], 1, hsync_bp1);				        // Add the hsync pulse
    memset(&border[hsync_bp1], 9, hsync_bp2);

	// This bit pre-builds the pixel buffer scanlines by adding the hsync pulse and left and right horizontal borders 
	//
    for(int i = 0; i < 2; i++) {							// Loop through the pixel buffers
        memset(&pixel_buffer[i][0], border_colour, hdots);	// First fill the buffer with the border colour
        memset(&pixel_buffer[i][0], 1, hsync_bp1);			// Add the hsync pulse
        memset(&pixel_buffer[i][hsync_bp1], 9, hsync_bp2);
        memset(&pixel_buffer[i][pixel_start], 31, width);
    }

	// Initialise the PIO
	//
    pio_sm_set_enabled(pio, state_machine, false);                      // Disable the PIO state machine
    pio_sm_clear_fifos(pio, state_machine);	                            // Clear the PIO FIFO buffers
    cvideo_initialise_pio(pio, state_machine, offset, 0, 5, piofreq);   // Initialise the PIO (function in cvideo.pio)
    cvideo_configure_pio_dma(pio, state_machine, dma_channel, hdots+1); // Hook up the DMA channel to the state machine
    pio_sm_set_enabled(pio, state_machine, true);                       // Enable the PIO state machine

	// And kick everything off
	//
    cvideo_dma_handler();       // Call the DMA handler as a one-off to initialise it
    while (true) {              // And then just loop doing nothing
        tight_loop_contents();
    }
}

// Write out a short vsync pulse
// Parameters:
// - p: Pointer to the buffer to store this sync data
// - length: The buffer size
//
void write_vsync_s(unsigned char *p, int length) {
    int pulse_width = length / 16;
    for(int i = 0; i < length; i++) {
        p[i] = i <= pulse_width ? 1 : 13;
    }
}

// Write out a long vsync half-pulse
// Parameters:
// - p: Pointer to the buffer to store this sync data
// - length: The buffer size
//
void write_vsync_l(unsigned char *p, int length) {
    int pulse_width = length - (length / 16) - 1;
    for(int i = 0; i < length; i++) {
        p[i] = i >= pulse_width ? 13 : 1;
    }
}

void cvideo_dma_handler(void) {

    // Switch condition on the vertical scanline number (vline)
    // Each statement does a dma_channel_set_read_addr to point the PIO to the next data to output
    //
    switch(vline) {

        // First deal with the vertical sync scanlines
        // Also on scanline 3, preload the first pixel buffer scanline
        //
        case 1 ... 2: 
            dma_channel_set_read_addr(dma_channel, vsync_ll, true);
            break;
        case 3:
            dma_channel_set_read_addr(dma_channel, vsync_ls, true);
            memcpy(&pixel_buffer[bline & 1][pixel_start], &bitmap[bline], width);
            break;
        case 4 ... 5:
        case 310 ... 312:
            dma_channel_set_read_addr(dma_channel, vsync_ss, true);
            break;

        // Then the border scanlines
        //
        case 6 ... 68:
        case 260 ... 309:
            dma_channel_set_read_addr(dma_channel, border, true);
            break;

        // Now point the dma at the first buffer for the pixel data,
        // and preload the data for the next scanline
        // 
        default:
            dma_channel_set_read_addr(dma_channel, pixel_buffer[bline++ & 1], true);    // Set the DMA to read from one of the pixel_buffers
            memcpy(&pixel_buffer[bline & 1][pixel_start], &bitmap[bline], width);       // And memcpy the next scanline into the other pixel buffer
            break;
    }

    // Increment and wrap the counters
    //
    if(vline++ >= 312) {    // If we've gone past the bottom scanline then
        vline = 1;		    // Reset the scanline counter
        bline = 0;		    // And the pixel buffer row index counter
    }

    // Finally, clear the interrupt request ready for the next horizontal sync interrupt
    //
    dma_hw->ints0 = 1u << dma_channel;		
}

// Configure the PIO DMA
// Parameters:
// - pio: The PIO to attach this to
// - sm: The state machine number
// - dma_channel: The DMA channel
// - buffer_size_words: Number of bytes to transfer
//
void cvideo_configure_pio_dma(PIO pio, uint sm, uint dma_channel, size_t buffer_size_words) {
    pio_sm_clear_fifos(pio, sm);
    dma_channel_config c = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
    dma_channel_configure(dma_channel, &c,
        &pio->txf[sm],              // Destination pointer
        NULL,                       // Source pointer
        buffer_size_words,          // Number of transfers
        true                        // Start flag (true = start immediately)
    );
    
	
	
	dma_channel_set_irq0_enabled(dma_channel, true);
    irq_set_exclusive_handler(DMA_IRQ_0, cvideo_dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

*/
