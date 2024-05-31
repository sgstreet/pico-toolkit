# Pico NMI

## Detailed Description

The RP2040 provides the capability to map any combination of the first 26 NVIC interrupts to the Cortex-M0+ NMI (Non-Maskable-Interrupt) on either core.  From page 61 of the [RP2040 Datasheet:](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)

>  The 26 system IRQ signals are masked (NMI mask) and then ORed together creating the NMI signal for the core. The NMI mask for each core can be configured using **PROC0_NMI_MASK** and **PROC1_NMI_MASK** in the Syscfg register block. Each of these registers has one bit for each system interrupt, and the each core’s NMI is asserted if a system interrupt is asserted and the corresponding NMI mask bit is set for that core. 

Unfortunately, the [Raspberry Pi Pico SDK](https://datasheets.raspberrypi.com/pico/raspberry-pi-pico-c-sdk.pdf) does not provide an interface library for the NMI functionality.  **Pico NMI** library provides a simple API to the RP2040 NMI mapping functionality.

To reduce NMI execution time the implementation maintains a zero-terminated array of the NMI-mapped interrupt handlers on a per-core basis.  When an interrupt is mapped to the NMI via `nmi_set_enable` the current core's interrupt vector table is interrogated to locate the handler to register in the NMI vector table. It is necessary to register the interrupt handler via Raspberry Pi Pico SDK [irq_set_exclusive_handler](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#gafffd448ba2d2eef5b355b88180aefe7f) or [irq_set_share_handler](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#gaf02f8599896c66f4579c845a96b2126e) functions before enabling the NMI

> Please be aware that the NMI mapping occurs before the interrupt signal is routed to the Cortex-M0+ NVIC. This hardware structure prevents software from triggering interrupts via the NVIC Interrupt Set Pending Register. 

<sub>Example</sub>
```
Need Example
```

## Functions

#### `void nmi_set_enable(uint num, bool enabled)`

Map or unmap a specific interrupt to the current cores NMI handler.  Interrupts will be disabled during the execution of this function.

#### `bool nmi_is_enabled(uint num)`

Determine if the specified interrupt is mapped to the current core's NMI handler.

#### `uint64_t nmi_mask(void)`

Globally and atomically mask all NMI interrupts returning the current state at a 64-bit integer.

#### `void nmi_unmask(uint64_t state)`

Globally and atomically unmask all NMI interrupts using the state returned by a previous call to `nmi_mask`.