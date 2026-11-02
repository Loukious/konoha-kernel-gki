# UART FIFO and fingerprint diagnostic boot

Bluetooth passed the corrected power-driver probe and power-on handshake, then
failed opening ttyHS0 with ENXIO. The UART port read a valid RX FIFO depth larger
than the port's legacy 16-word default and rejected it. The driver now accepts
all depths represented by the hardware's eight-bit FIFO depth field, with a
matching maximum-sized receive allocation. It validates received word counts
and trailing-byte lengths before reading into that allocation. The default
console FIFO sizing remains available for its original use.

verify.py compiles extracted production FIFO sizing and receive functions with
ASan/UBSan and checks all supported depths, all receive counts, trailing-byte
lengths, invalid widths/depths, zero/drop paths, and output accounting. The prior
sizing code, a too-small allocation, and removed status bounds each fail.

The same boot adds operation-result logging for fingerprint power/reset ioctls.
This records whether Linux successfully powers and resets the sensor before
its vendor secure command fails; it does not bypass sensor initialization.
