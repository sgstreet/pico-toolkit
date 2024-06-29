# Pico Thread and Core Local Storage
## Detailed Description
[Picolibc](https://github.com/picolibc/picolibc) uses [C11 thread local](https://en.wikipedia.org/wiki/Thread-local_storage) compiler support to hold all persistent state used by the ISO C library functions.  The global variable `errno` is a common example. As deeply embedded systems targeted by Picolibc do not use dynamic loading nor shared libraries,  Picolic uses the `local-exec`TLS model. See section 4.4 of [ELF Handling For Thread-Local Storage](https://www.akkadia.org/drepper/tls.pdf) and [Thread Local Storage in Picolibc](https://github.com/picolibc/picolibc/blob/main/doc/tls.md#thread-local-storage-in-picolibc) for detailed information.
<!--stackedit_data:
eyJoaXN0b3J5IjpbMTgzODUzNjUyLC0xMDUzNDYxMTYzLDEzMD
A5MTI0MzksLTgzNjQyMDI3NSwtOTMyNjYxODAyLC0xNzYwNTEz
NTk4LDc4NzM2ODUxOF19
-->