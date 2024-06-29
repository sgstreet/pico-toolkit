# Pico Thread and Core Local Storage
## Detailed Description
[Picolibc](https://github.com/picolibc/picolibc) uses [C11 thread local](https://en.wikipedia.org/wiki/Thread-local_storage) compiler support to hold all persistent state used by the ISO C library functions.  The global variable `errno` is a common example. 
<!--stackedit_data:
eyJoaXN0b3J5IjpbLTkwOTAxNTQ5MSwtODM2NDIwMjc1LC05Mz
I2NjE4MDIsLTE3NjA1MTM1OTgsNzg3MzY4NTE4XX0=
-->