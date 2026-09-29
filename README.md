# STM32 Adaptive Sonar Waveform Demo

Firmware for an STM32F446RE that generates adaptive sonar-style pulses on the
12-bit DAC and displays the current modulation mode and analog sensor readings
on an SSD1306 OLED.

This is a waveform-generation and sensor-response demo, not a complete sonar
ranging system: it does not acquire echoes or calculate distance from a
transducer. The displayed depth is derived from an analog input.

## Features

- Timer-triggered DAC output with circular DMA at a 1 MHz sample rate.
- Three selectable waveforms: linear-frequency-modulated (LFM) chirp, geometric-frequency sweep, and Barker-7 phase-coded pulse.
- Hamming-windowed pulses, with adaptive frequency, duration, and amplitude parameters.
- Periodic two-channel ADC sampling for normalized turbidity and estimated depth values.
- 128 x 64 SSD1306 OLED status display over I2C.
- Press the board's PC13 user button to cycle through the waveform modes.

## Hardware

| Function | STM32F446RE connection | Notes |
| --- | --- | --- |
| Turbidity analog input | PA0 / ADC1_IN0 | 0 to 3.3 V maps to 0 to 100% |
| Depth analog input | PA1 / ADC1_IN1 | 0 to 3.3 V maps to 0 to 200 m |
| Waveform output | PA4 / DAC_OUT1 | 12-bit DAC output; connect suitable signal conditioning and a transducer driver |
| OLED SCL | PB6 / I2C1_SCL | SSD1306, address 0x3C |
| OLED SDA | PB7 / I2C1_SDA | SSD1306, address 0x3C |
| Mode button | PC13 / EXTI13 | Falling-edge press cycles modes |

Use analog sensor outputs within the MCU's 0 to 3.3 V input range. Do not
connect a transducer directly to PA4; use an appropriate driver and protection
circuit. I2C1 is configured on PB6/PB7 by the CubeMX project and HAL MSP code;
`main.c` also initializes PB8/PB9, so verify the GPIO setup if changing or
regenerating the project configuration.

## Waveform Behavior

The ADC samples are normalized as follows:

- PA0: ADC count / 4095, interpreted as turbidity from 0.0 (clear) to 1.0.
- PA1: ADC count / 4095 x 200, interpreted as depth from 0 to 200 m.

These inputs feed an adaptive difficulty score that adjusts the waveform
frequency band (100 to 500 kHz), pulse duration (0.5 to 5 ms), and amplitude.
The implementation includes a Thorp absorption term, but uses depth as a proxy
for one-way range and a placeholder 20 dB loss bound. These are modeling
assumptions, not calibrated measurements. The ADC inputs therefore need
external sensors or signal conditioning that produce the expected voltage
ranges.

## Build and Flash

1. Install STM32CubeIDE with STM32Cube FW_F4 V1.28.3, the firmware package used by this project.
2. Import the repository as an existing STM32CubeIDE project, or open `sonar_test.ioc` in STM32CubeMX / STM32CubeIDE.
3. Select the `Debug` or `Release` build configuration and build the `sonar_test` target.
4. Connect an STM32F446RE board over ST-LINK, then use **Run** or **Debug** in STM32CubeIDE to program it.

The project uses the STM32 HAL and GCC toolchain. `Debug/` and `Release/`
contain generated build outputs; rebuild them in STM32CubeIDE for your local
toolchain and configuration.

## Project Layout

- `Core/Src/main.c`: peripheral setup, ADC processing, DMA callbacks, OLED UI, and button handling.
- `Core/Src/waveform_gen.c` and `Core/Inc/waveform_gen.h`: adaptive parameter mapping and waveform lookup-table generation.
- `Core/Src/oled_ssd1306.c` and `Core/Inc/oled_ssd1306.h`: minimal SSD1306 display driver.
- `Core/Inc/` and `Core/Src/`: application headers and source files.
- `Drivers/`: STM32F4 HAL and CMSIS dependencies.
- `sonar_test.ioc`: STM32CubeMX peripheral and pin configuration.
- `STM32F446RETX_FLASH.ld` and `STM32F446RETX_RAM.ld`: linker scripts.
