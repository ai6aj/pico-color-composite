# pico-color-composite

NTSC color composite video output for Rasperry Pi Pico

pico-color-composite outputs a composite video signal using an external 8-bit R/2R ladder using
100 and 220 ohm 1% tolerance resistors.  The resistor values are not an exact 2:1 ratio due to
the internal resistance of the RP2040's GPIO pins.

Why composite video?
--------------------
Unlike VGA, most consumer equipment made in the past 20+ years is capable of displaying it,
and will likely continue to do so foreseeable future for backwards compatibility with old VCRs,
video game consoles, etc.

Why NTSC?
---------
Compatibility and simplicity.  Virtually all consumer equipment that supports composite input
supports NTSC, even equipment sold in countries where PAL or SECAM were the broadcast standards.

Is there any PAL support?
-------------------------
There is very limited PAL support.  A basic PAL signal can be generated, however there is almost
no support for PAL's more convoluted chroma encoding scheme, so you are generally restricted
to a black and white signal.
