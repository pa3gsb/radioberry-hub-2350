#ifndef RADIOBERRY_DSP_INTERNAL_H
#define RADIOBERRY_DSP_INTERNAL_H

/* Keep the live sample loops out of XIP flash on the Pico. The host build
 * uses the same algorithms without a platform or linker dependency. */
#ifdef RB_DSP_PICO
#define RB_DSP_HOT __attribute__((section(".time_critical.rb_dsp")))
#else
#define RB_DSP_HOT
#endif

#endif
