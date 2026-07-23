# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(rb2350-hub-cwkeyer C CXX ASM)

option(RB_USB_MIDI_ONLY "Enumerate as a pure USB MIDI device for host compatibility testing" OFF)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Use project-local TinyUSB class configuration (CDC + MIDI)
add_compile_definitions(PICO_TUSB_CONFIG_HEADER=\"${CMAKE_CURRENT_LIST_DIR}/tusb_config.h\")

if(RB_USB_MIDI_ONLY)
    add_compile_definitions(RB_USB_MIDI_ONLY=1)
endif()

# Add executable. Default name is the project name, version 0.1

add_executable(rb2350-hub-cwkeyer
    rb2350-hub-cwkeyer.c
    usb_descriptors.c
    usb_audio_callbacks.c)

pico_set_program_name(rb2350-hub-cwkeyer "rb2350-hub-cwkeyer")
pico_set_program_version(rb2350-hub-cwkeyer "0.1")

# Generate PIO header
pico_generate_pio_header(rb2350-hub-cwkeyer ${CMAKE_CURRENT_LIST_DIR}/blink.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(rb2350-hub-cwkeyer 0)
pico_enable_stdio_usb(rb2350-hub-cwkeyer 0)

# Add the standard library to the build
target_link_libraries(rb2350-hub-cwkeyer
    pico_stdlib
    tinyusb_device
    tinyusb_board)

# Add the standard include files to the build
target_include_directories(rb2350-hub-cwkeyer PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(rb2350-hub-cwkeyer 
        hardware_spi
        hardware_i2c
        hardware_dma
        hardware_pio
        hardware_interp
        hardware_timer
        hardware_watchdog
        hardware_clocks
        )

pico_add_extra_outputs(rb2350-hub-cwkeyer)

