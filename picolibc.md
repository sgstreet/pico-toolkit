# Picolibc

## Detailed Description
[Picolibc](https://github.com/picolibc/picolibc) implements a standard C library ideally suited for the RP2040 MCU. Picolibc is derived [Newlib](https://sourceware.org/newlib/) blended with the I/O subsystem from [AVR Libc](https://www.nongnu.org/avr-libc).

This interface library builds Picolibc as an external CMake project tuned for the Cortex-M0+. Exception unwind tables and function name generation are enabled to support the full backtrace capabilities in the [pico-fault](../pico-fault/pico-fault.md) library.
<!--stackedit_data:
eyJoaXN0b3J5IjpbMTEzMDgwOTI5MF19
-->