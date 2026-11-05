# Hardware SPI transport used by the RadioBerry W5500 network component.
add_library(IOLIBRARY_FILES STATIC)
target_sources(IOLIBRARY_FILES PUBLIC
    ${PORT_DIR}/ioLibrary_Driver/src/wizchip_spi.c)
target_include_directories(IOLIBRARY_FILES PUBLIC
    ${WIZNET_DIR}/Ethernet
    ${PORT_DIR}/ioLibrary_Driver/inc
    ${PORT_DIR})
target_link_libraries(IOLIBRARY_FILES PUBLIC
    pico_stdlib
    hardware_pio
    hardware_spi
    hardware_dma
    hardware_clocks)
