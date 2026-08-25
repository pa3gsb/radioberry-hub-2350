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
include(pico_sdk_import.cmake)

project(radioberry C CXX ASM)

set(RADIOBERRY_APPLICATION "protocol1_w5500" CACHE STRING
    "Application to build (protocol1_w5500 or gateware_loader)")
set_property(CACHE RADIOBERRY_APPLICATION PROPERTY STRINGS
    protocol1_w5500 gateware_loader)
set(RADIOBERRY_FPGA_VARIANT "CL025" CACHE STRING
    "Assembled Cyclone 10 LP FPGA variant (CL016 or CL025)")
set_property(CACHE RADIOBERRY_FPGA_VARIANT PROPERTY STRINGS CL016 CL025)

if(NOT RADIOBERRY_APPLICATION MATCHES "^(protocol1_w5500|gateware_loader)$")
    message(FATAL_ERROR
        "Invalid RADIOBERRY_APPLICATION='${RADIOBERRY_APPLICATION}'. "
        "Choose protocol1_w5500 or gateware_loader.")
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

# Protocol 1 application dependencies
if(RADIOBERRY_APPLICATION STREQUAL "protocol1_w5500")
    add_subdirectory(${CMAKE_SOURCE_DIR}/libraries)
    add_subdirectory(${PORT_DIR})
endif()

# Add executable. Default name is the project name, version 0.1

add_subdirectory(components/gateware)
if(RADIOBERRY_APPLICATION STREQUAL "protocol1_w5500")
    add_subdirectory(components/network)
    add_subdirectory(components/radio)
    add_subdirectory(components/protocol1)
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

