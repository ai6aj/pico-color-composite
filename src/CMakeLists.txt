add_executable($ENV{PICO_PROJECT_NAME}
        $ENV{PICO_PROJECT_NAME}.c
		ntsc-video-core.c
		framebuffer.c
        )

pico_generate_pio_header($ENV{PICO_PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/$ENV{PICO_PROJECT_NAME}.pio)
pico_enable_stdio_usb($ENV{PICO_PROJECT_NAME} 1)
pico_enable_stdio_uart($ENV{PICO_PROJECT_NAME} 0)


# Pull in our pico_stdlib which pulls in commonly used features
# as well as pico_multicore as you really should be using it
# in any modern projects
target_link_libraries($ENV{PICO_PROJECT_NAME} pico_stdlib pico_multicore hardware_pio hardware_dma)

# create map/bin/hex file etc.
pico_add_extra_outputs($ENV{PICO_PROJECT_NAME})

