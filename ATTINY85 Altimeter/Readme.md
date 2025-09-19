# ATtiny85 + BMP280 Rocket Altitude Logger (LED Flash Output)

This project is a **minimal rocket flight computer** built on an **ATtiny85** with a **BMP280 barometric sensor**.  
It detects launch, logs altitude for 60 seconds, and then flashes the **maximum altitude** using onboard LEDs.  

---

## ✨ Features

- **ATtiny85** + **BMP280** (I²C @ 0x76)
- **Launch detection**:
  - Ignores false triggers for first 5 seconds after power-on
  - Detects launch when altitude rises >10 m above baseline
- **Logging**:
  - Reads altitude at **5 Hz (every 200 ms)**
  - Tracks maximum altitude for 60 seconds after launch
- **LED output**:
  - **Heartbeat**: all LEDs flash every 2 seconds before launch detect  
  - **Thousands digit**: all LEDs flash together (e.g. 2 flashes = 2000 m)  
  - **Hundreds / tens / ones digits**: flashed individually on separate pins  
- **Frozen altitude**: after 60 seconds, max altitude is locked and continuously displayed

---

## 🔧 Hardware

- **MCU**: ATtiny85 (running on internal oscillator, no external crystal)
- **Sensor**: BMP280 barometric pressure/temperature sensor (I²C, addr `0x76`)
- **LEDs**: 3 × LEDs on output pins

| LED  | Pin name | ATtiny85 pin | Physical pin |
|------|----------|--------------|--------------|
| Ones | `PB1`    | `1`          | 6            |
| Tens | `PB3`    | `3`          | 2            |
| Hundreds | `PB4`| `4`          | 3            |

Thousands are indicated by **all LEDs flashing at once**.

---

## 📊 Example Output

If the frozen max altitude is **2345 m**:

1. **Thousands (2)** → all LEDs flash twice  
2. **Hundreds (3)** → hundreds LED flashes 3 times  
3. **Tens (4)** → tens LED flashes 4 times  
4. **Ones (5)** → ones LED flashes 5 times  
5. Sequence repeats forever  

---

## 🚀 Usage

1. Flash the code to an **ATtiny85** using Arduino IDE (select *ATtiny85, 8 MHz internal*).
2. Power the board + BMP280.
3. Before launch → heartbeat flashes every 2 seconds.
4. On launch detection → logging begins (LEDs stay off).
5. After 60 seconds → maximum altitude is frozen and flashed repeatedly.

---

## 📝 Code Overview

- **`setup()`**  
  - Initializes I²C, BMP280, and LEDs.  
  - Reads baseline altitude at power-on.  

- **`loop()`**  
  - Reads altitude every 200 ms.  
  - Detects launch only after 5 s uptime and >10 m altitude change.  
  - Tracks maximum altitude for 60 s, then freezes value.  
  - Handles heartbeat and altitude flashing via non-blocking state machine.  

---

## ⚠️ Notes

- Code assumes **sea-level reference pressure = 101325 Pa**.  
- Power draw is suitable for coin cell, but consider capacity for long logging.  
- Launch detect threshold (10 m) and logging duration (60 s) can be adjusted in code.  

---

## 📂 Repository Layout

