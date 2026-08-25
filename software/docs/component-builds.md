# Component builds

The default configuration builds the complete Protocol 1 + W5500 firmware as
`radioberry.uf2`.

## CMake options

| Option | Default | Purpose |
|---|---:|---|
| `RADIOBERRY_APPLICATION` | `protocol1_w5500` | `protocol1_w5500` or `gateware_loader` |
| `RADIOBERRY_FPGA_VARIANT` | `CL025` | Gateware for assembled FPGA: `CL016` or `CL025` |

The selected application automatically enables exactly the components it
needs. CMake reports an error for an unknown application or FPGA variant.

## FPGA gateware selection

Gateware belongs to the gateware component and is stored per FPGA under
`components/gateware/bitstreams/<variant>/radioberry.rbf`. Select the FPGA
assembled on the RadioBerry while configuring the build:

```powershell
cmake -S . -B build-cl025 -G Ninja -DRADIOBERRY_FPGA_VARIANT=CL025
cmake --build build-cl025
```

For a CL016 board, use `-DRADIOBERRY_FPGA_VARIANT=CL016`. Configuration fails
when the selection is invalid or its bitstream is absent, preventing a build
with gateware for the wrong FPGA. A separate build directory per variant is
recommended because CMake caches the selection.

## Minimal gateware loader

```powershell
cmake -S . -B build-gateware -G Ninja `
  -DRADIOBERRY_APPLICATION=gateware_loader `
  -DRADIOBERRY_FPGA_VARIANT=CL025
cmake --build build-gateware
```

Output: `build-gateware/gateware_loader_example.uf2`.

Applications use reusable component targets such as `radioberry::gateware`,
`radioberry::radio`, `radioberry::net_transport`, `radioberry::protocol1` and
`radioberry::w5500`.
