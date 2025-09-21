ATtiny85 + BMP280 Rocket Altitude Logger (LED Flash Output)

This project is a minimal rocket flight computer built on an ATtiny85 with a BMP280 barometric sensor.
It detects launch, logs altitude for a short flight window, and then flashes the maximum altitude using onboard LEDs.

✨ Features

ATtiny85 + BMP280 (I²C @ 0x76)

Launch detection

Ignores false triggers during startup baseline sampling

Detects launch when altitude rises > 5–10 m above baseline

Logging

Reads altitude at 20 Hz (every 50 ms) for 6 s after launch

Tracks maximum altitude during flight

LED output

Heartbeat: LEDs blink in sequence (ones → tens → hundreds) every 2 s before launch

Thousands digit: hundreds and ones LEDs flash together (e.g. 2 flashes = 2000 m)

Hundreds / tens / ones digits: flashed individually on their dedicated pins

Frozen altitude: after flight, the max altitude is locked and flashed every 10 s

🔧 Hardware

MCU: ATtiny85 (running on internal oscillator, 1 MHz or 8 MHz, no external crystal)

Sensor: BMP280 barometric pressure/temperature sensor (I²C, addr 0x76)

LEDs: 3 × LEDs on output pins

Digit	Pin name	Arduino pin	ATtiny85 physical pin
Ones	PB1	1	6
Tens	PB3	3	2
Hundreds	PB4	4	3

Thousands are indicated by hundreds + ones LEDs flashing at the same time.

📊 Example Output

If the frozen max altitude is 2345 m:

Thousands (2) → hundreds + ones LEDs flash twice together

Hundreds (3) → hundreds LED flashes 3 times

Tens (4) → tens LED flashes 4 times

Ones (5) → ones LED flashes 5 times

Sequence repeats after 10 s

🚀 Usage

Flash the code to an ATtiny85 using Arduino IDE (select ATtiny85, internal 1 MHz or 8 MHz, BOD disabled).

Power the board + BMP280.

Before launch → sequential heartbeat (1s → 10s → 100s LEDs blink) every 2 s.

On launch detection → logging begins (LEDs stay off).

After ~6 s → maximum altitude is frozen and flashed every 10 s.

📝 Code Overview

setup()

Initializes I²C, BMP280, and LEDs.

Averages multiple samples to establish a baseline pressure.

loop()

Pre-launch: heartbeat every 2 s, altitude checked once per second.

In-flight: samples at 20 Hz for 6 s, updates max altitude.

Post-flight: flashes altitude digits every 10 s using custom LED code.

⚠️ Notes

Code assumes BMP280 relative altitude mode (baseline = pad height).

Launch detect threshold (default: 5 m) and flight logging duration (6 s) can be adjusted in code.

For longer runtime on coin cells, use sleep modes and disable BOD in fuses.

Thousands digit currently limited to a single place (0–9000 m).

📂 Repository Layout

RocketLogger.ino → main sketch

ATTINY85BMP280.h/.cpp → lightweight BMP280 driver for ATtiny85

README.md → this document
