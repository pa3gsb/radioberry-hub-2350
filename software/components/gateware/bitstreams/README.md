# RadioBerry gateware images

Each assembled FPGA has its own image:

- `CL016/radioberry.rbf`
- `CL025/radioberry.rbf`

Select it with the CMake cache variable `RADIOBERRY_FPGA_VARIANT`. These images
are hardware-specific and must not be interchanged.
