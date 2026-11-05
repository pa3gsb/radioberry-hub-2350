# RadioBerry SDR Meets the RP2350

*Introducing the RadioBerry Hub 2350 — an Ethernet hub for SDR and radio control*

RadioBerry began as a radio card for the Raspberry Pi, bringing together an
AD9866 mixed-signal front end, an FPGA, and open-source software to build an
SDR transceiver. It gives radio amateurs an opportunity to experiment with
the complete radio: hardware, gateware, signal processing, and the software
used to operate it. [More about RadioBerry](https://pa3gsb.github.io/projects/radioberry/).

As the project develops, new ideas continue to emerge. One question led to
the RadioBerry Hub 2350: how much of the work normally handled by the
Raspberry Pi could be performed by a small microcontroller?

The idea originated during development of the Raspberry Pi 5 RadioBerry
driver. Its use of Programmable Input/Output, or PIO, suggested another
possibility. The RP2350 also provides PIO state machines, which can handle
carefully timed communication with external hardware. Could this provide
a practical interface between a microcontroller and the RadioBerry FPGA?

Juan, WP3DN, pointed me towards the Waveshare RP2350 Pi Zero board.
Experiments with that board provided a starting point for exploring the
idea. From there, the next step was to design a dedicated board for
RadioBerry, with the connections needed for Ethernet operation and future
radio-control applications.

The result is the **RadioBerry Hub 2350**, or **RB Hub 2350** for short.

The board follows the familiar Raspberry Pi form factor and provides the
standard 40-pin connection for the RadioBerry card. The FPGA continues to
perform the radio's digital downconversion and upconversion, while the
RP2350 handles communication, control, and application firmware. At
startup, the microcontroller also loads the RadioBerry FPGA gateware.

Ethernet is an integral part of the design. A WIZnet W5500 controller
provides a 10/100 Mb Ethernet connection and handles TCP/IP functions in
hardware. It communicates with the RP2350 over SPI, giving the
microcontroller a practical way to exchange radio data and control
messages over the local network.

This continues an earlier direction explored with the
[RadioBerry JUICE board](https://www.pa3gsb.nl/2021/05/31/radioberry-juice/):
making RadioBerry accessible through different interfaces and computing
platforms. The Hub 2350 brings that exploration into a dedicated
microcontroller-based design.

Two firmware applications demonstrate what the new hardware can do.

The first implements **OpenHPSDR Protocol 1**. In this arrangement, the Hub
2350 acts as the Ethernet-connected host for the RadioBerry card.
Compatible SDR software on a computer receives the IQ samples and
provides demodulation, audio processing, and the operating interface.

This allows the existing RadioBerry card to become a network-connected
receiver, with the embedded firmware taking care of the radio-side startup
and communication. The current implementation supports receive operation.

The second application explores a different way of using the board:
**accessing RadioBerry directly from a web browser**.

A computer, laptop, or tablet on the same local network can open the
board's address and load its radio interface. The board serves the page
itself, including a spectrum display, waterfall, frequency controls, and
receiver gain adjustment. There is no separate web server to install or
maintain.

For a demonstration, this makes the setup particularly approachable.
Connect the radio to Ethernet, open a browser on a local device, and
interact with the board. It shows how a small amount of hardware can
provide both a radio interface and a network service.

The experimental WebSDR application also brings DSP onto the RP2350. Its
receiver software includes AM, USB, and LSB demodulation, filtering, and
adjustable automatic gain control. The controller processes the IQ
samples locally and sends demodulated audio to the browser for playback.
Transmitting audio requires considerably less network traffic than
continuously transferring the complete IQ stream.

These DSP functions are being developed as reusable software components.
That makes them useful beyond the browser application: the same receiver
processing can support future experiments with local audio hardware,
physical controls, or a display.

The expansion connections provide room to explore those possibilities.
An additional board with microphone input, audio output, buttons, and a
small display could form the basis of a compact standalone radio. Other
possibilities include filter switching, RX/TX control, and communication
with a power-amplifier controller for information such as temperature or
forward and reflected power.

Those are directions for further development. The immediate work centres
on the prototype hardware, reliable receive operation, and the two
firmware applications. The browser receiver remains an experimental
application, with testing and refinement continuing alongside development.

The RadioBerry Hub 2350 opens another route for building and experimenting
with RadioBerry. Existing SDR software can operate the radio through
OpenHPSDR Protocol 1, while the WebSDR application explores direct browser
access and local signal processing. Both approaches use the same hardware
platform and leave room for further ideas from builders and developers.

Have fun experimenting!

73,  
Johan — PA3GSB

