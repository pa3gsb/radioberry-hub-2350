add_library(rb_dsp STATIC
    src/fft.c src/tone.c src/am.c src/ssb.c src/agc.c
    src/receiver.c src/spectrum.c)
add_library(radioberry::dsp ALIAS rb_dsp)
target_include_directories(rb_dsp PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(rb_dsp PUBLIC m)

if(TARGET pico_platform)
    target_compile_definitions(rb_dsp PRIVATE RB_DSP_PICO=1)
endif()
