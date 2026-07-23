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

# Add libraries in subdirectories
add_subdirectory(${CMAKE_SOURCE_DIR}/libraries)
add_subdirectory(${PORT_DIR})

# Add executable. Default name is the project name, version 0.1

set(RADIOBERRY_RBF_INPUT ${CMAKE_CURRENT_LIST_DIR}/radioberry.rbf)
set(RADIOBERRY_RBF_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(RADIOBERRY_RBF_SOURCE ${RADIOBERRY_RBF_GENERATED_DIR}/radioberry_rbf.c)
set(RADIOBERRY_RBF_HEADER ${RADIOBERRY_RBF_GENERATED_DIR}/radioberry_rbf.h)

add_custom_command(
    OUTPUT ${RADIOBERRY_RBF_SOURCE} ${RADIOBERRY_RBF_HEADER}
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_LIST_DIR}/cmake/embed_binary_as_c.ps1
        -InputFile ${RADIOBERRY_RBF_INPUT}
        -OutputC ${RADIOBERRY_RBF_SOURCE}
        -OutputH ${RADIOBERRY_RBF_HEADER}
        -SymbolName radioberry_rbf
    DEPENDS
        ${RADIOBERRY_RBF_INPUT}
        ${CMAKE_CURRENT_LIST_DIR}/cmake/embed_binary_as_c.ps1
    COMMENT "Embedding radioberry.rbf as C source"
    VERBATIM
)

add_executable(radioberry
        radioberry.c
        fpga_loader.c
        radioberry_spi.c
        hpsdr/hpsdr_protocol1.c
        w5500/w5500_udp.c   
        stream-rx/stream_rx_dma.c
    stream-tx/stream_tx_dma.c
        ${RADIOBERRY_RBF_SOURCE}
)

pico_generate_pio_header(radioberry
    ${CMAKE_CURRENT_LIST_DIR}/rb-rx.pio
    ${CMAKE_CURRENT_LIST_DIR}/rb-tx.pio
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
        ETHERNET_FILES
        IOLIBRARY_FILES
        LOOPBACK_FILES 
        DHCP_FILES)

# Add the standard include files to the build
target_include_directories(radioberry PRIVATE
         ${CMAKE_CURRENT_LIST_DIR}
         ${RADIOBERRY_RBF_GENERATED_DIR}
         ${CMAKE_CURRENT_LIST_DIR}/port
         ${CMAKE_CURRENT_LIST_DIR}/hpsdr
         ${CMAKE_CURRENT_LIST_DIR}/w5500
         ${CMAKE_CURRENT_LIST_DIR}/stream-rx
     ${CMAKE_CURRENT_LIST_DIR}/stream-tx
 )

pico_add_extra_outputs(radioberry)

