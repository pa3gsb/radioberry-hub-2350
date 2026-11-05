# RadioBerry Hub 2350 firmware

This directory contains the firmware for the RP2350-based RadioBerry Hub
with a W5500 Ethernet controller. It is **one CMake project**: select the
application to build using `RADIOBERRY_APPLICATION`.

## Build in Visual Studio Code

Open the **`software/`** folder in Visual Studio Code on Windows, with the
**Raspberry Pi Pico** and **CMake Tools** extensions installed. For a fresh
checkout, use **Raspberry Pi Pico: Import Pico Project**, select SDK
**2.2.0** and board **pico2**, and enable CMake Tools integration. Select
the **Pico** kit if prompted.

Use the Command Palette (`Ctrl+Shift+P`):

1. Run **Raspberry Pi Pico: Configure CMake**.
2. Open **CMake: Edit CMake Cache (UI)** and choose:
   - `RADIOBERRY_APPLICATION`: `protocol1_w5500`, `radioberry_websdr` or
     `gateware_loader`.
   - `RADIOBERRY_FPGA_VARIANT`: `CL016` or `CL025`, matching your RadioBerry
     card.
3. Save the changes and choose **CMake: Select Variant > Release**.
4. Run **Raspberry Pi Pico: Configure CMake** again.
5. Run **Raspberry Pi Pico: Compile Pico Project**.

With the project's VS Code configuration, the output for the selected
application appears under **`software/build-cl025/`**:

| Application setting | Purpose | Output in `build-cl025/` |
|---|---|---|
| `protocol1_w5500` | OpenHPSDR Protocol 1 receiver | `radioberry.uf2` |
| `radioberry_websdr` | Browser receiver with AM, USB and LSB audio | `radioberry_websdr.uf2` |
| `gateware_loader` | Load the FPGA gateware | `gateware_loader_example.uf2` |

For **WebSDR on CL025**, select `radioberry_websdr` and `CL025`. The result
is **`software/build-cl025/radioberry_websdr.uf2`**.

The FPGA setting selects the embedded gateware; the build directory name
does not select the FPGA. To switch applications, change the application
setting and configure and compile again in the same VS Code project.

Connect the Hub in BOOTSEL mode and copy the selected UF2 to its USB drive.

## Components and dependencies

See the [browser README](applications/radioberry_websdr/browser/README.md)
for browser development and checks, and the [DSP README](components/dsp/README.md)
for the reusable signal-processing API.

The Pico SDK is managed outside this repository by the Pico extension.
Keep the FPGA images in `components/gateware/bitstreams/`; they are embedded
in the firmware.

`libraries/ioLibrary_Driver/` contains the W5500/DHCP subset used here, with a
local socket fix. See its [RadioBerry notes](libraries/ioLibrary_Driver/README.radioberry.md)
before updating it from upstream. Keep its license and the browser's
`README.md`, `LICENSE-ubersdr.txt` and referenced source files when sharing
this software. The root license does not replace third-party licenses.
