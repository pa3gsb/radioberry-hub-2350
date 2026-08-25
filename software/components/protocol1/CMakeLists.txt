add_library(hpsdr_protocol1 STATIC src/protocol1.c)
add_library(radioberry::protocol1 ALIAS hpsdr_protocol1)
target_include_directories(hpsdr_protocol1 PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(hpsdr_protocol1 PUBLIC radioberry::net_transport)
