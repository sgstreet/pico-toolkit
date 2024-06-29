# Pico Thread and Core Local Storage
## Detailed Description
[Picolibc](https://github.com/picolibc/picolibc) uses [C11 thread local](https://en.wikipedia.org/wiki/Thread-local_storage) compiler support to hold all persistent state used by the ISO C library functions.  The global variable `errno` is a common example. As deeply embedded systems targeted by Picolibc do not use dynamic loading nor shared libraries,  uses the `local-exec`model to imp
<!--stackedit_data:
eyJoaXN0b3J5IjpbMjExOTU5OTg1OSwtMTA1MzQ2MTE2MywxMz
AwOTEyNDM5LC04MzY0MjAyNzUsLTkzMjY2MTgwMiwtMTc2MDUx
MzU5OCw3ODczNjg1MThdfQ==
-->