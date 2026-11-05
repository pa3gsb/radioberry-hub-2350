# RadioBerry WIZnet subset

RadioBerry builds the W5500 controller driver, the common Ethernet socket and
configuration code, and IPv4 DHCP from WIZnet's ioLibrary. The other controller
drivers, application examples, Internet protocols, and compiled help files are
not part of this project's dependency set.

The upstream [README](README.md) and [license](license.txt) are retained. The
upstream README describes the full ioLibrary; RadioBerry supports only the
subset selected in `../CMakeLists.txt`.

## Local fix

`Ethernet/socket.c`, in `recv()`, checks for buffered data before returning
`SOCK_BUSY` for a nonblocking socket. Without this ordering, IPv4 TCP sockets
could remain busy even when received data was available. Preserve this change
when updating the vendor files. The regression check is
`../../applications/radioberry_websdr/tests/wiznet_recv_check.c`.

The hardware SPI adapter is maintained separately under
`../../port/ioLibrary_Driver/`. Its QSPI header is still included for the SPI
handle type; this does not enable the unused PIO transport.
