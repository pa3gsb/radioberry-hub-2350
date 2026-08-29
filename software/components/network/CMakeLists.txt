add_library(net_transport INTERFACE)
add_library(radioberry::net_transport ALIAS net_transport)
target_include_directories(net_transport INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

add_library(net_w5500 STATIC w5500/src/w5500.c)
add_library(radioberry::w5500 ALIAS net_w5500)
target_include_directories(net_w5500 PUBLIC ${CMAKE_CURRENT_LIST_DIR}/w5500/include)
target_link_libraries(net_w5500 PUBLIC net_transport pico_stdlib
    ETHERNET_FILES DHCP_FILES IOLIBRARY_FILES)
