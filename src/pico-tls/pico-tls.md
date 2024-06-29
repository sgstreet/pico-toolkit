# Pico Thread and Core Local Storage
## Detailed Description
[Picolibc](https://github.com/picolibc/picolibc) uses [C11 Thread Local](https://en.wikipedia.org/wiki/Thread-local_storage) compiler support to hold all persistent state used by the ISO C library functions.  The global variable `errno` is a common example. As deeply embedded systems targeted by Picolibc do not use dynamic loading nor shared libraries, Picolic uses the `local-exec`TLS model. For further information, please see section 4.4 of [ELF handling for thread-local storage](https://www.akkadia.org/drepper/tls.pdf) and [Thread Local Storage in Picolibc](https://github.com/picolibc/picolibc/blob/main/doc/tls.md#thread-local-storage-in-picolibc).



<!--stackedit_data:
eyJoaXN0b3J5IjpbMTcwNzAyNzE2NCwxNTY5NTk4MTcyLDE4Mz
g1MzY1MiwtMTA1MzQ2MTE2MywxMzAwOTEyNDM5LC04MzY0MjAy
NzUsLTkzMjY2MTgwMiwtMTc2MDUxMzU5OCw3ODczNjg1MThdfQ
==
-->