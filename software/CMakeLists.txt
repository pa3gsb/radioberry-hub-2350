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
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

set(BOARD_NAME RBHUB2350)

if(${BOARD_NAME} STREQUAL RBHUB2350)
    set(PICO_PLATFORM rp2350)
    set(WIZNET_CHIP W5500)
    add_definitions(-D_WIZCHIP_=W5500)
    add_definitions(-DDEVICE_BOARD_NAME=RBHUB2350)
endif()

# Pull in Raspberry Pi Pico SDK (must be before project)
# Keep optional downloads reproducible; an installed SDK can be selected with
# PICO_SDK_PATH. The SDK is maintained outside this source tree.
set(PICO_SDK_FETCH_FROM_GIT_TAG "${sdkVersion}" CACHE STRING "Pico SDK release to fetch")
include(pico_sdk_import.cmake)

project(radioberry C CXX ASM)

set(RADIOBERRY_APPLICATION "protocol1_w5500" CACHE STRING
    "Application to build (protocol1_w5500, radioberry_websdr or gateware_loader)")
set_property(CACHE RADIOBERRY_APPLICATION PROPERTY STRINGS
    protocol1_w5500 radioberry_websdr gateware_loader)
set(RADIOBERRY_FPGA_VARIANT "CL025" CACHE STRING
    "Assembled Cyclone 10 LP FPGA variant (CL016 or CL025)")
set_property(CACHE RADIOBERRY_FPGA_VARIANT PROPERTY STRINGS CL016 CL025)

if(NOT RADIOBERRY_APPLICATION MATCHES "^(protocol1_w5500|radioberry_websdr|gateware_loader)$")
    message(FATAL_ERROR
        "Invalid RADIOBERRY_APPLICATION='${RADIOBERRY_APPLICATION}'. "
        "Choose protocol1_w5500, radioberry_websdr or gateware_loader.")
endif()
# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

if(NOT DEFINED WIZNET_DIR)
    set(WIZNET_DIR ${CMAKE_SOURCE_DIR}/libraries/ioLibrary_Driver)
    message(STATUS "WIZNET_DIR = ${WIZNET_DIR}")
endif()


if(NOT DEFINED PORT_DIR)
    set(PORT_DIR ${CMAKE_SOURCE_DIR}/port)
    message(STATUS "PORT_DIR = ${PORT_DIR}")
endif()

# Set compile options
add_compile_options(
        -Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        -Wno-maybe-uninitialized
        )

# Ethernet application dependencies
if(NOT RADIOBERRY_APPLICATION STREQUAL "gateware_loader")
    add_subdirectory(${CMAKE_SOURCE_DIR}/libraries)
    add_subdirectory(${PORT_DIR})
endif()

# Add executable. Default name is the project name, version 0.1

add_subdirectory(components/gateware)
if(RADIOBERRY_APPLICATION STREQUAL "protocol1_w5500")
    add_subdirectory(components/network)
    add_subdirectory(components/radio)
    add_subdirectory(components/protocol1)
elseif(RADIOBERRY_APPLICATION STREQUAL "radioberry_websdr")
    target_compile_definitions(IOLIBRARY_FILES PUBLIC RADIOBERRY_WEBSDR_NETWORK=1)
    add_subdirectory(components/network)
    add_subdirectory(components/dsp)
    add_subdirectory(components/radio)
    # A complete FFT window per buffer gives RX 21.3 ms of DMA capacity.
    # Protocol 1 retains its original 126-pair packet buffers.
    target_compile_definitions(rb_radio PUBLIC STREAM_RX_DMA_IQ_PAIRS=512u)
endif()

if(RADIOBERRY_APPLICATION STREQUAL "protocol1_w5500")
    add_executable(radioberry
        applications/protocol1_w5500/main.c
    )

pico_set_program_name(radioberry "radioberry")
pico_set_program_version(radioberry "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(radioberry 0)
pico_enable_stdio_usb(radioberry 1)

# Add the standard library to the build
target_link_libraries(radioberry
        pico_stdlib
        pico_multicore
        hardware_spi
        hardware_pio
        hardware_dma
        hardware_irq
        hardware_sync
        radioberry::gateware
        radioberry::gateware_image
        radioberry::radio
        radioberry::protocol1
        radioberry::w5500)

# Add the standard include files to the build
target_include_directories(radioberry PRIVATE
         ${CMAKE_CURRENT_LIST_DIR}/applications/protocol1_w5500
         ${CMAKE_CURRENT_LIST_DIR}/port
 )

    pico_add_extra_outputs(radioberry)
elseif(RADIOBERRY_APPLICATION STREQUAL "radioberry_websdr")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(web_assets ${CMAKE_CURRENT_BINARY_DIR}/web_assets.c)
    file(GLOB_RECURSE web_sources CONFIGURE_DEPENDS
        applications/radioberry_websdr/browser/src/*)
    add_custom_command(OUTPUT ${web_assets}
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/applications/radioberry_websdr/embed_assets.py ${web_assets}
        DEPENDS applications/radioberry_websdr/embed_assets.py
            applications/radioberry_websdr/browser/build.ps1
            applications/radioberry_websdr/browser/README.md
            applications/radioberry_websdr/browser/LICENSE-ubersdr.txt ${web_sources}
            applications/radioberry_websdr/browser/iq-probe.html
            applications/radioberry_websdr/browser/iq-stream.js
            applications/radioberry_websdr/browser/iq-probe.js
            applications/radioberry_websdr/browser/pcm-probe.html
            applications/radioberry_websdr/browser/pcm-stream.js
            applications/radioberry_websdr/browser/pcm-probe.js
        VERBATIM)
    add_executable(radioberry_websdr
        applications/radioberry_websdr/main.c
        applications/radioberry_websdr/websdr_service.c
        applications/radioberry_websdr/websdr_http.c
        applications/radioberry_websdr/websdr_spectrum.c
        applications/radioberry_websdr/websdr_iq.c
        applications/radioberry_websdr/websdr_audio.c
        applications/radioberry_websdr/websdr_receiver.c
        applications/radioberry_websdr/websdr_radio.c
        applications/radioberry_websdr/websdr_radio_config.c
        ${web_assets}
    )
    target_include_directories(radioberry_websdr PRIVATE applications/radioberry_websdr)
    pico_set_program_name(radioberry_websdr "radioberry_websdr")
    pico_set_program_version(radioberry_websdr "0.1")
    pico_enable_stdio_uart(radioberry_websdr 0)
    pico_enable_stdio_usb(radioberry_websdr 1)
    target_link_libraries(radioberry_websdr PRIVATE
        pico_stdlib
        pico_rand
        pico_unique_id
        pico_multicore
        radioberry::gateware
        radioberry::gateware_image
        radioberry::radio
        radioberry::dsp
        radioberry::w5500
    )
    pico_add_extra_outputs(radioberry_websdr)
elseif(RADIOBERRY_APPLICATION STREQUAL "gateware_loader")
    add_executable(gateware_loader_example
        applications/gateware_loader/main.c
    )
    target_link_libraries(gateware_loader_example PRIVATE
        pico_stdlib
        radioberry::gateware
        radioberry::gateware_image
    )
    pico_enable_stdio_uart(gateware_loader_example 0)
    pico_enable_stdio_usb(gateware_loader_example 1)
    pico_add_extra_outputs(gateware_loader_example)
endif()

