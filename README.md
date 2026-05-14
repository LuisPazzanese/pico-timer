# pico-timer

Non-blocking HC-SR04 ultrasonic ranging on the Raspberry Pi Pico 2, built around the SDK's hardware alarm/timer API and GPIO interrupts. Configurable via a simple serial CLI.

## Overview

A distance-measurement system that drives an HC-SR04 sensor without ever blocking on the echo. Periodic readings are scheduled by `add_alarm_in_ms`, the echo pulse is timed using rising/falling-edge GPIO interrupts, and a watchdog alarm cancels the read if no echo arrives within 50 ms. The user can start, stop, and reconfigure the reading period at runtime over USB serial.

## Features

- **Non-blocking ultrasonic ranging:** trigger pulse + edge-IRQ echo capture using `time_us_64()`, no `pulse_in`-style busy loops
- **Watchdog alarm** (50 ms) on each trigger to detect missing/lost echoes and flag a read failure
- **Periodic reading scheduler** via the Pico SDK alarm API (default 3 s, runtime-configurable)
- **1 Hz uptime counter** maintained by a self-rescheduling alarm callback, used to timestamp readings
- **Serial command interface** with line buffering:
  - `start` — begin periodic readings
  - `stop` — pause periodic readings
  - `period <seconds>` — change the polling interval
- **Glitch rejection:** echo pulses longer than 30 ms are discarded as invalid
- Distance computed with the standard `pulse_us * 0.0343 / 2` formula

## Tech Stack

- **Language:** C (C11)
- **SDK:** Raspberry Pi Pico SDK 2.2.0 (`hardware/timer`, `hardware/gpio`)
- **Hardware:** Raspberry Pi Pico 2 (RP2350), HC-SR04 ultrasonic sensor
- **Build system:** CMake 3.12+, GNU Arm Embedded Toolchain 14.2

## Architecture

All shared state lives in a single `volatile`-qualified struct that is written from ISRs (GPIO edges, alarm callbacks) and read from the main loop. The main loop is a small dispatcher: it polls the serial command buffer, fires triggers when the periodic alarm flags one, and prints results once per measurement.

```
main/
├── main.c           # ISRs, alarm callbacks, serial CLI, main loop
└── CMakeLists.txt
CMakeLists.txt       # Top-level project, SDK import
```

## Pinout

| Signal       | GPIO |
| ------------ | ---- |
| HC-SR04 TRIG | 14   |
| HC-SR04 ECHO | 15   |

## How to Build

Requires the [Pico SDK](https://github.com/raspberrypi/pico-sdk) and the ARM toolchain.

```bash
mkdir build && cd build
cmake ..
make
```

Flash the resulting `.uf2` to the Pico 2 in BOOTSEL mode, then connect via USB serial at 115200 baud.

---
*Developed as part of the Embedded Systems course at [Insper](https://www.insper.edu.br/).*
