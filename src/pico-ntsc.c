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
    uint offset = pio_add_program(pio, &blink_program);
    printf("Loaded program at %d\n", offset);

    blink_pin_forever(pio, 0, offset, 1, 1);
	
	while (1) {
		printf("Hello world\n");
		sleep_ms(1000);
	}
}

void blink_pin_forever(PIO pio, uint sm, uint offset, uint pin, uint freq) {
    blink_program_init(pio, sm, offset, pin);
    printf("Blinking pin %d at %d Hz\n", pin, freq);
    pio_sm_set_enabled(pio, sm, true);
	
	// Set the clock divisor.
#ifdef USE_OVERCLOCK
	pio->sm[sm].clkdiv = (22 << 16 + 0);
#else
	pio->sm[sm].clkdiv = ((17 << 16) + (117 << 8));
#endif

}
