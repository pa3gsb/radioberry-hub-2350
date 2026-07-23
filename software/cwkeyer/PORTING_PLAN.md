# PORTING_PLAN

## Implemented phase

This document now covers three completed transport phases:

1. TinyUSB CDC serial
2. TinyUSB USB MIDI
3. Audio + MIDI + CDC composite descriptors

### Changes in scope (CDC serial)

- `stdio` output is routed to USB CDC instead of UART.
- The explicit UART1 setup path has been removed from the application entry point.
- No audio, GPIO remapping, WM8960 control port, EEPROM backend, or keyer logic porting is included in this phase.

### Build-time switch (CDC serial)

- `pico_enable_stdio_usb(... 0)` is now disabled because USB transport is handled as MIDI-only in the current phase.
- `pico_enable_stdio_uart(... 0)` is disabled.

### Changes in scope (USB MIDI)

- TinyUSB device stack is enabled with a project-local TinyUSB config header.
- USB MIDI class is enabled and custom TinyUSB descriptors are provided.
- Main loop now services `tud_task()` and a MIDI task.
- Incoming USB MIDI bytes are read and echoed back.
- A simple MIDI heartbeat (note on/off) is emitted periodically for host-side detection.

### Build-time switch (USB MIDI)

- TinyUSB libraries are linked: `tinyusb_device`, `tinyusb_board`.
- `PICO_TUSB_CONFIG_HEADER` points to `tusb_config.h`.
- TinyUSB class selection in this phase: MIDI enabled, CDC disabled.

### Changes in scope (Audio + MIDI + CDC composite descriptors)

- USB device descriptor is now composite (IAD mode) to support multi-function USB classes.
- Configuration descriptor now includes:
	- UAC2 microphone descriptor block (TinyUSB `TUD_AUDIO_MIC_FOUR_CH_DESCRIPTOR`)
	- CDC descriptor block
	- MIDI descriptor block
- TinyUSB class config now enables Audio, CDC, and MIDI simultaneously.
- This phase is descriptor/class-enablement only. It does not implement a complete USB audio streaming engine or application-level CDC command protocol.

### Build-time switch (Audio + MIDI + CDC composite descriptors)

- TinyUSB class selection in this phase:
	- `CFG_TUD_AUDIO = 1`
	- `CFG_TUD_CDC = 1`
	- `CFG_TUD_MIDI = 1`
- UAC2 helper sizing and FIFO config are enabled through `tusb_config.h`.

## Dependency map for this phase

### Replaced now

| Existing use | RP2350 implementation in this phase |
|---|---|
| Explicit `uart_init()` / `gpio_set_function(... GPIO_FUNC_UART)` / `uart_puts()` | Removed; no UART console path is used |
| No USB MIDI transport in app | TinyUSB MIDI device transport with periodic `tud_task()` servicing |
| No USB descriptors for MIDI | Project descriptor callbacks in `usb_descriptors.c` |
| Single-function USB MIDI descriptor set | Composite Audio + CDC + MIDI descriptor set |

### Deferred to later phases

| Area | Current status | Planned replacement |
|---|---|---|
| Teensy AudioStream graph | Not implemented | RP2350 audio runtime with DMA/I2S backend |
| WM8960 register control | Not implemented | Pico SDK I2C driver plus WM8960 register port |
| GPIO/PTT/CW outputs | Not ported from reference design | Pico SDK GPIO HAL |
| ADC pot inputs | Not ported from reference design | Pico SDK ADC HAL with software averaging |
| EEPROM persistence | Not implemented | Flash-backed store or external I2C EEPROM |

## Remaining hardware assumptions

These assumptions still exist after the CDC-only phase and must be validated before later phases.

### USB and console assumptions

- The board is connected through the RP2350 USB data port, not only power.
- Host-side MIDI class enumeration is available and allowed by the target operating system.
- The host MIDI stack accepts full-speed USB MIDI endpoints with 64-byte endpoint sizing.

### Board and clock assumptions

- `PICO_BOARD` remains `pico2`.
- The board provides the normal USB clocking required by Pico SDK USB stdio.

### Pins still assumed by current sample code

These are not part of the CDC phase, but they remain compiled into the sample application:

- SPI0 on GPIO16/17/18/19
- I2C0 on GPIO8/9
- PIO blink output on `PICO_DEFAULT_LED_PIN` or GPIO6 fallback

### Reference-design assumptions still unresolved

These come from the reference Teensy-based design and are not addressed in this phase:

- Four analog controls are expected for master volume, sidetone volume, sidetone frequency, and speed.
- Separate PTT input, PTT output, and CW output lines are expected.
- WM8960 audio codec availability, wiring, reset behavior, and I2S clocking are still assumed but unverified on RP2350 hardware.
- Non-volatile parameter storage is still assumed by the reference protocol but has no RP2350 backend yet.
- WinKey-related behavior is still only an integration target, not an implemented module in this tree.

### USB MIDI specific assumptions still unresolved

- Product/vendor ID pair is currently development-oriented and may need project-specific allocation.
- MIDI virtual cable count is currently one cable and may need expansion for control/data separation.
- The current MIDI task behavior includes echo and heartbeat test traffic and is not yet the final keyer protocol transport layer.
- Timing, buffering, and backpressure behavior under sustained host MIDI traffic are not characterized yet.

### Composite USB (Audio + CDC + MIDI) assumptions still unresolved

- Host OS driver behavior for this exact composite interface ordering has not yet been validated on target systems.
- The UAC2 descriptor currently exposes a 4-channel mic-style function for enumeration compatibility, not finalized CWKeyer audio routing semantics.
- The project currently does not push real-time PCM payloads for the audio function; descriptor-level enumeration is in place, but runtime audio transport policy remains a later phase.
- CDC class is present in descriptors, but stdio-over-CDC and a dedicated CDC command parser are not finalized in this phase.

## Stop point

This document intentionally stops after Audio + MIDI + CDC composite descriptor enablement.
The next phases must not start until composite enumeration is validated on hardware and the remaining pin, codec, and persistence assumptions are confirmed.