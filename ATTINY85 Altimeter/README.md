# ATtiny85 + BMP280 Rocket Altitude Logger (LED Flash Output)

This project is a **minimal, low-power rocket flight computer** built on an **ATtiny85** with a **BMP280 barometric sensor**.  
It detects launch, logs altitude for a short flight window, stores the **maximum altitude** in EEPROM, and flashes it on the onboard LEDs whenever a readout is requested.

---

## ✨ Features

- **ATtiny85** + **BMP280** over **software I²C** (address `0x76` or `0x77`, detected automatically)
- **No external libraries**: the I²C and BMP280 drivers are built into the sketch
- **Launch detection**
  - Baseline pressure averaged from 20 samples at power-on
  - Baseline slowly follows weather changes (about 1 minute time constant), preventing false launches on the bench
  - Launch is detected on a rapid pressure drop of **60 Pa (about 5 m)** below the baseline
- **Logging**
  - Reads altitude at **20 Hz (every 50 ms)** for 6 s after launch
  - Tracks **maximum altitude** during flight
- **Persistent storage**
  - Maximum altitude is saved to **EEPROM** after landing
  - The stored value survives power-off and battery removal
- **On-demand readout**
  - Touch physical **pin 2 to VCC** and release to flash the stored altitude
  - Works before launch and after landing (disabled only during the 6 s flight window)
- **LED output**
  - **Boot**: single sweep (ones → tens → hundreds) at power-on
  - **Pre-launch heartbeat**: one short blink on the ones LED about every 10 s
  - **Thousands digit**: hundreds **and** ones LEDs flash together (e.g. 2 flashes = 2000 m)
  - **Hundreds / tens / ones digits**: flashed individually on their dedicated LEDs
  - **No data**: three quick flashes on all LEDs if no flight has been recorded
- **Low power**
  - Watchdog sleep between pre-launch samples (about 20 µA average)
  - Deep sleep after landing, woken only by the readout trigger (under 1 µA for the ATtiny85)

---

## 🔧 Hardware

- **MCU**: ATtiny85 (internal oscillator, 1 MHz, no external crystal)
- **Sensor**: BMP280 barometric pressure/temperature sensor (I²C, address `0x76` or `0x77`)
- **LEDs**: 3 × red LEDs, each with a **150–220 Ω** series resistor to GND
- **Power**: CR1632 coin cell with an SS-12D00 slide switch
- **Decoupling**: 100 nF capacitor between pin 8 (VCC) and pin 4 (GND), close to the chip

| Function        | Pin name | Arduino pin | ATtiny85 physical pin |
|:----------------|:--------:|:-----------:|:---------------------:|
| Ones LED        | `PB1`    | `1`         | 6                     |
| Tens LED        | `PB3`    | `3`         | 2                     |
| Hundreds LED    | `PB4`    | `4`         | 3                     |
| Readout trigger | `PB3`    | `3`         | 2 (shared with tens)  |
| I²C SDA         | `PB2`    | `2`         | 7                     |
| I²C SCL         | `PB0`    | `0`         | 5                     |
| VCC             | —        | —           | 8                     |
| GND             | —        | —           | 4                     |

SDA and SCL are wired opposite to the ATtiny85 hardware I²C (USI) pins, so the sketch uses bit-banged I²C.  
The BMP280 breakout must provide pull-up resistors on SDA and SCL (most modules include them).

Thousands are indicated by **hundreds + ones LEDs flashing at the same time**.  
If there are no thousands flashes, the altitude is under 1000 m.

---

## 📊 Example Output

If the stored maximum altitude is **2345 m**:

1. **Thousands (2)** → hundreds + ones LEDs flash twice together
2. **Hundreds (3)** → hundreds LED flashes 3 times
3. **Tens (4)** → tens LED flashes 4 times
4. **Ones (5)** → ones LED flashes 5 times

Each flash lasts 15 ms with 0.5 s between flashes and 0.8 s between digits.  
A digit of zero produces no flashes for that LED.  
The sequence plays once after landing, then again each time the trigger is used.

---

## 🚀 Usage

1. Upload the sketch with the Arduino IDE using an Arduino as ISP (see settings below).
2. Switch on. The LEDs sweep once after about 2 s, once the baseline has been measured.
3. Before launch, the ones LED blinks briefly about every 10 s.
4. On launch detection, logging begins and the LEDs stay off.
5. After 6 s, the maximum altitude is saved to EEPROM and flashed once.
6. The altimeter then deep-sleeps. Touch pin 2 to VCC and release to repeat the readout.
7. Switch off and on to re-arm for a new flight. The previous altitude remains readable until the next landing overwrites it.

### Arduino IDE settings

| Setting                   | Value                          |
|:--------------------------|:-------------------------------|
| Board                     | ATtiny85                       |
| Processor Speed           | 1 MHz Internal Oscillator      |
| Use Bootloader            | No (ISP Programmer Upload)     |
| Brown-out Detection Level | Disabled                       |
| Programmer                | Arduino as ISP                 |

Run **Burn Bootloader** once to set the 1 MHz fuses, then upload with **Sketch → Upload Using Programmer** (Ctrl+Shift+U).  
Switch the coin cell off while programming. If the BMP280 board has no onboard regulator, unplug it during programming.

---

## 📝 Code Overview

- **`setup()`**
  - Sets all LED pins to high-impedance, disables the ADC, analog comparator and USI.
  - Starts the BMP280 and averages 20 samples to establish the baseline pressure.
  - Plays the boot sweep and enables the readout trigger.

- **`loop()`**
  - **Readout**: if the trigger has fired (outside flight), waits for release and flashes the stored altitude.
  - **Pre-launch**: one low-power reading per second, baseline tracking, heartbeat about every 10 s, watchdog sleep between samples.
  - **In-flight**: samples at 20 Hz for 6 s with 8× pressure oversampling and updates the maximum altitude.
  - **Post-flight**: saves to EEPROM, flashes once, then deep-sleeps until triggered.

---

## ⚠️ Notes

- Altitude is relative to the pad (baseline captured just before launch).
- Launch threshold (`LAUNCH_DROP_PA`, default 60 Pa) and logging window (`FLIGHT_WINDOW_MS`, default 6000 ms) are adjustable in code. Apogee must occur within the logging window.
- LED pins are only ever driven high or left high-impedance, so connecting VCC to any LED pin cannot short an output.
- The trigger pin is held low only weakly by its LED. If unprompted readouts occur, fit a 100 kΩ resistor from pin 2 to GND.
- Uploading new code through the ISP erases the EEPROM, so the stored altitude will read as "no data" afterwards.
- Remove any power LED on the BMP280 breakout, as it can drain a coin cell in days.
- Display range is 0–9999 m.

---

## 📂 Repository Layout

- `RocketLogger.ino` → main sketch (includes software I²C and BMP280 driver)
- `README.md` → this document
