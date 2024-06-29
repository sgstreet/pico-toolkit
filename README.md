# Pico Toolkit
The Pico Toolkit aims to provide a collection of production quality add-ons to the Raspberry RP2040 ecosystem.  The toolkit is focused around a hackable Symmetric Multi Processing (SMP) scheduler paired with a RP2040 tuned integration of the [PicoLibc](https://github.com/picolibc/picolibc) standard C library which targets small embedded systems. Two RTOS personalities are provided: [ISO C11 Threads](https://en.cppreference.com/w/c/thread) and a [CMSIS RTOS v2](https://arm-software.github.io/CMSIS_5/RTOS2/html/index.html). The toolkit provides standalone features such as a C11 standard atomic and thread local storage support as well as a backtrace generator using a lightweight stack unwinder compatible with the [Exception Handling ABI for the Arm Architecture](https://github.com/ARM-software/abi-aa/releases/download/2023Q3/ehabi32.pdf).

# Licensing
To encourage community support, the Pico Toolkit is licensed under the [Mozilla Public License](https://www.mozilla.org/en-US/MPL/2.0) (MPL). The MLP allows inclusion toolkit in closed source embedded systems while requiring the open sourcing of any changes to the the Pico Toolkit source code. This seems like a reasonable compromise allowing construction of typical closed source deeply embedded systems while preserving the copyleft, but non-viral, nature of successful open source development.

# Contributing
The Pico Toolkit project strives to be an [inclusive community](CODE_OF_CONDUCT.md) following [Bazzar Model](https://en.wikipedia.org/wiki/The_Cathedral_and_the_Bazaar) of software development.  We are looking for maintainers, contributors and testers! 

> Ask me what I think of Cathedral projects pretending to be a Bazzar.

[**Pull Requests**](https://github.com/sgstreet/pico-toolkit/pulls) are the primary way of contributing to the toolkit so go ahead, fork the repo, hack it and open a PR. A matching [**Pico Tookkit Issue**](https://github.com/sgstreet/pico-toolkit/issues) for enhancements and bug fixes would be greatly appreciated. A response and review is guaranteed!  

Have a question or comment? [**Pico Toolkit Discussions**](https://github.com/sgstreet/pico-toolkit/discussions) is the place to be!  Considering contributing a new library?  Have great new idea? Open a discussion to get community feedback!

# The Libraries

| Library | Description |
| ------- | ----------- |
| [multicore-support](src/multicore-support/multicore-support.md) | Adds multicore irqs and SMP support to when the pico-scheduler is included. Uses the flexible RP2040 NMI handling to support Realtime IRQ priorities |
| [pico-atomic](src/pico-atomic/pico-atomic.md) | Provides multicore safe atomic options. |
| [pico-cmsis-rtos2](src/pico-cmsis-rtos2/pico-cmsis-rtos2.md) | Includes the CMSIS RTOS v2 personality on top of the pico-scheduler |
| [pico-fault](src/pico-fault/pico-fault.md) | Cortex-M0+ fault handling and exception backtrace support. |
| [pico-nmi](src/pico-nmi/pico-nmi.md) | Create a simple interface to the RP2040 NMI block. |
| [pico-rtt](src/pico-rtt/pico-rtt.md) | Include Segger Real Time Transfer (RTT) support. This also works with OpenOCD. |
| [pico-scheduler](src/pico-scheduler/pico-scheduler.md) | A SMP capable real time scheduler with flexible Futex support including priority inheritance and contention tracking. |
| [pico-threads](src/pico-threads/pico-threads.md) | Includes the ISO C11 threads personality. |
| [pico-tls](src/pico-tls/pico-tls.md) | Add support for C11 _Thread_local storage-class specifier and matching support for a core_local concept riding on top of the RP2040 multicore hardware. |
| [picolibc-glue](src/picolibc-glue/picolibc-glue.md) | Implements the extensible retargetable locking mechanism utilized by Picolibc. Locking works transparently with and without an RTOS. |
| [picolibc](src/picolibc/picolibc.md) | Builds and integrates the Picolibc version of Newlib tuned for the Cortex-M0+ processor. Unwind table and function names are included in the build. |
| [toolkit-support](src/toolkit-support/toolkit-support.md) | Common toolkit header files including a header only ticket based spin locks and intrusive linked lists. |
| 

# Using the Pico Toolkit
The [Pico Toolkit](https://github.com/sgstreet/pico-toolkit.git) integrates with the [Pico SDK](https://github.com/raspberrypi/pico-sdk) and your project following [Getting Started with the Raspberry Pi Pico Guide](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf). In addition to the Pico SDK build dependencies of `cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential` the toolkit requires the `meson` build system when using the [**picolibc**](src/picolibc) interface.

```
sudo apt install meson
```

In your project environment you should export the **PICO_TOOLKIT_PATH** variable.  Assuming you checked out the [pico-sdk](https://github.com/raspberrypi/pico-sdk) and the [pico-toolkit](https://github.com/sgstreet/pico-toolkit.git) at the same level as your project.
```
$ cd your_project_path
$ export PICO_SDK_PATH=../pico-sdk
$ export PICO_TOOLKIT_PATH=../pico-toolkit
```

Your project CMakeLists.txt should look something like this:

```
cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include($ENV{PICO_TOOLKIT_PATH}/external/pico_toolkit_import.cmake)

project(pico-project C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(hello-world)
```

Notice the include of `pico_toolkit_import.cmake`.  You are good to go, configure and build as you normally would.  You can add `-DPICO_TOOLKIT_TESTS_ENABLED=true` to your cmake configure to build the include test projects.

<!--stackedit_data:
eyJoaXN0b3J5IjpbLTE2MzQ4ODEwOTddfQ==
-->