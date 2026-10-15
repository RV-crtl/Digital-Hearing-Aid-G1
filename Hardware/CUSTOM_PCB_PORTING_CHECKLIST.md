# Custom PCB hardware-port gate — NOT flash-ready

The project at the repository root is the **Nucleo-L476RG development target**, with
working R34 clock/SAI/DMA/UART runtime configuration. The original KiCad files are
included for reference, not as proof of assembled-PCB functionality. Do **not**
flash root firmware to the custom PCB and assume correct audio pin assignments.

The U13 pad map is extracted directly from the routed PCB footprint in
`Hardware/STM32_U13_PINMAP.csv`. Examples:

| PCB function | U13 pin/function | Integration impact |
|---|---|---|
| Encoder A/B | PA0/PA1 (pads 14/15) | GPIO quadrature decoder provided; disabled until board target is ported |
| Nucleo B1 | PC13 (pad 2 is unconnected) | B1/LD2 UI in this firmware is NUCLEO-ONLY |
| PDM mic | DFSDM1_CKOUT / DFSDM1_DATIN2 | Requires pin+DFSDM DMA configuration and microphone electrical validation |
| Bluetooth control | USART1 TX/RX (pads 42/43) | BT UART already exists, validate board wiring and oscillator |
| UART debug | USART2 TX/RX (pads 16/17) | Board physical connector/access unknown; Nucleo ST-LINK USB VCP is not on bare PCB |
| Audio output | SAI2B FS/SCK/SD (pads 50/51/53) | Must port GPIO alternative function and DMA wiring |
| USB-C data | USB_OTG_FS DM/DP (pads 44/45) | Data routing exists, USB device firmware not integrated |
| Capacitive touch | I2C1 SCL/SDA (pads 58/59) | MPR121 IRQ/address unconnected; needs hardware verification |
| HSE | OSC_IN/OUT pads 5/6 | Verify oscillator frequency before assuming R34 PLL parameters |

## Mandatory hardware checks

1. KiCad ERC/DRC, physical assembly, power-rail continuity, regulator output, and current limits.
2. SWD/debug connection, NRST and BOOT0, correct board oscillator and full clock tree.
3. Recreate CubeMX pinout from PCB pad map; preserve TIM2 (runtime) and TIM6 (HAL tick).
4. SAI1/SAI2 internal clocking, DMA channel requests, PDM microphone DFSDM, I2C touch, encoder GPIO.
5. Verify USB-C CC wiring and VBUS protection; do not assume USB-PD or battery charging.
6. Confirm PCM1808 microphone/telecoil signal paths and actual channel mapping.
7. Validate SAI2B output at a safe level with unloaded signals before connecting a transducer.
8. Build, flash and compare real DMA/overrun counters, then test each source individually.

**Not demonstrated here:** physical two-mic beamforming, USB Audio Class,
USB-C PD, two-cell LiFePO4 charging/balancing, BLE control over the BT401,
user battery sensing, trained TinyML inference or a calibrated acoustic SPL ceiling.
These features require more hardware validation and/or training data.
