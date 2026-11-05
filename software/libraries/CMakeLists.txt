# RadioBerry uses the W5500 Ethernet controller and IPv4 DHCP only.
if(NOT WIZNET_CHIP STREQUAL "W5500")
    message(FATAL_ERROR "The bundled WIZnet subset supports W5500 only")
endif()

add_library(ETHERNET_FILES STATIC)
target_sources(ETHERNET_FILES PUBLIC
    ${WIZNET_DIR}/Ethernet/socket.c
    ${WIZNET_DIR}/Ethernet/wizchip_conf.c)
target_include_directories(ETHERNET_FILES INTERFACE
    ${WIZNET_DIR}/Ethernet
    ${WIZNET_DIR}/Ethernet/W5500)
target_link_libraries(ETHERNET_FILES PUBLIC W5500_FILES)

add_library(W5500_FILES STATIC)
target_sources(W5500_FILES PUBLIC
    ${WIZNET_DIR}/Ethernet/W5500/w5500.c)
target_include_directories(W5500_FILES INTERFACE
    ${WIZNET_DIR}/Ethernet
    ${WIZNET_DIR}/Ethernet/W5500)
target_link_libraries(W5500_FILES PRIVATE ETHERNET_FILES)

add_library(DHCP_FILES STATIC)
target_sources(DHCP_FILES PUBLIC
    ${WIZNET_DIR}/Internet/DHCP/dhcp.c)
target_include_directories(DHCP_FILES PUBLIC
    ${WIZNET_DIR}/Ethernet
    ${WIZNET_DIR}/Internet/DHCP)
