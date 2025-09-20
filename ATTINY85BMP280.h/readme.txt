📘 ATTINY85BMP280 Library

A minimal, lightweight Arduino library for using the BMP280 barometric pressure sensor with an ATtiny85 microcontroller.
Optimized for altimeters (rocketry, cansats, water rockets) where relative altitude is more important than absolute sea-level altitude.

✨ Features

Works with ATtiny85 using TinyWireM
 (I²C).

Very small footprint — runs on 8 KB flash.

Relative altitude built-in: zero at startup, then track altitude changes.

Forced mode sampling → fresh reading every time you call measure().

Outputs pressure (Pa), temperature (°C, optional), absolute altitude, and relative altitude.

Perfect for rocket altimeters with LED or telemetry outputs.

📌 Wiring (ATtiny85 DIP-8)
BMP280 Pin	ATtiny85 Pin	Notes
SDA	PB0 (pin 5)	I²C data
SCL	PB2 (pin 7)	I²C clock
VCC	VCC (3.3 V)	BMP280 is 3.3 V
GND	GND	Ground

Optional:

Serial output: PB3 (pin 2) → to ESP32 / USB-TTL RX for logging.