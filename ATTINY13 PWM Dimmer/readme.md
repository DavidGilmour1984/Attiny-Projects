# ATtiny13A 5-Step PWM Dimmer (Latched, Auto-Sleep)

## Overview
This firmware turns an **ATtiny13A** into a low-power, button-controlled PWM dimmer with fixed brightness steps, deliberate long-press control, and automatic sleep.

The device **starts asleep**, wakes only on a deliberate button press, and returns to sleep automatically after a fixed time.

---

## Pin Assignment (ATtiny13A DIP-8)

| Physical Pin | AVR Pin | Function |
|-------------|--------|----------|
| 2 | PB3 / ADC3 | **Optional analogue dimmer input**. PCB is designed for a **10 kΩ potentiometer**. *Not used in current firmware.* |
| 3 | PB4 / ADC2 | **Digital brightness control button** (active LOW, internal pull-up). Used for dimming in the current firmware. |
| 5 | PB0 | ON / OFF (sleep / wake) button (active LOW, internal pull-up) |
| 6 | PB1 | PWM output |
| 4 | GND | Ground |
| 8 | VCC | Supply |

---

## Operating Behaviour

### Power-Up
- On power-up the microcontroller immediately enters **deep sleep**.
- No output is active until the unit is deliberately woken.

---

### Wake / ON Control (PB0)
- Hold the ON/OFF button **LOW for ≥ 0.5 seconds** to wake the device.
- The ON state is **latched** — the button does not need to be held.
- On wake:
  - Output starts at **20% brightness**.
  - The auto-sleep timer is reset.

---

### Brightness Control (PB4 / ADC2)
- Hold the brightness button **LOW for ≥ 0.5 seconds** to change brightness.
- Each valid press steps through fixed levels:

20% → 40% → 60% → 80% → 100% → wrap to 20%

yaml
Copy code

- Only one step occurs per press (no bounce or multi-step).
- **Note:** PB4 is ADC-capable but is currently used as a digital input.

---

### Optional Analogue Dimmer (PB3 / ADC3)
- Physical pin 2 is wired on the PCB for a **10 kΩ potentiometer**.
- This input is **not used by the current firmware**.
- Can be enabled in future to replace the step-based brightness control with continuous analogue dimming.

---

### ON / OFF (Sleep) While Running
- While ON, holding the ON/OFF button **LOW for ≥ 0.5 seconds**:
  - Immediately turns the output OFF
  - Enters **deep sleep**
  - State remains OFF until explicitly woken again

---

### Automatic Sleep
- After a fixed time since waking (default **2 hours**, adjustable):
  - PWM output is turned fully OFF
  - Device enters deep sleep automatically
- This prevents accidental long-term power drain.

---

## PWM Output & Power Stage

- Output pin: **PB1 (physical pin 6)**
- PWM type: **software-generated square wave**
- Frequency: ~500 Hz
- Duty cycle is strictly one of the defined steps
- Output is **fully disabled** during sleep

### MOSFET Requirement
- The PWM output is intended to drive the **gate of an IRL540N logic-level N-channel MOSFET**.
- The ATtiny13A **must not** drive the load directly.
- Typical configuration:
  - PB1 → MOSFET gate (series resistor optional)
  - Source → ground
  - Drain → load → supply
  - Flyback diode required for inductive loads

---

## Electrical Notes

- Internal pull-ups are used for all buttons.
- Buttons connect directly between pin and ground.
- No external resistors are required for digital inputs.
- The analogue potentiometer input (PB3) requires standard ADC wiring.

---

## Design Characteristics

- Digital dimming via button (current firmware)
- Optional analogue dimming hardware supported
- No hardware timers required
- Long-press logic prevents accidental activation
- Deterministic behaviour
- Very low sleep current

---

## Adjustable Parameters

- `HOLD_TIME_MS`  
  Long-press duration (default 500 ms)

- `AUTO_SLEEP_MS`  
  Time before automatic sleep  
  Example values:
  - 2 minutes: `2UL * 60UL * 1000UL`
  - 30 minutes: `30UL * 60UL * 1000UL`
  - 2 hours: `2UL * 60UL * 60UL * 1000UL`

---

## Summary
This firmware provides a robust, low-power PWM dimmer with latched control, deliberate user interaction, and hardware support for both **digital step dimming** and **future analogue dimming via a 10 kΩ potentiometer**.





