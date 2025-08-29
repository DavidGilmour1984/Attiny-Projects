# Attiny-Projects
Attiny Projects
https://www.youtube.com/watch?v=-mV6ETGLIg8

What you need

Arduino Uno + USB cable

ATtiny45 or ATtiny85 (DIP-8)

Breadboard + jumper wires

10 µF electrolytic capacitor (to keep the Uno from auto-resetting while it’s acting as a programmer)

Optional: 1× LED + 220 Ω resistor for a blink test

1) Install ATtiny support in Arduino IDE

Open File → Preferences → Additional Boards Manager URLs and paste:
http://drazzy.com/package_drazzy.com_index.json 
arduino-developer.com

Open Tools → Board → Boards Manager…, search for “ATTinyCore by Spence Konde”, click Install. 
arduino-developer.com

Why this matters: ATTinyCore adds proper pin maps, clocks, and tools menus for the ATtiny25/45/85 family, and you’ll use its “Burn Bootloader” action to set fuses (clock, BOD, etc.). 
GitHub

2) Turn your Uno into a programmer (Arduino as ISP)

Connect the Uno to your computer.

In the IDE, open File → Examples → ArduinoISP → ArduinoISP and Upload it to the Uno. 
arduino.cc

After it uploads, place a 10 µF capacitor between RESET and GND on the Uno (positive lead to RESET, negative to GND). This prevents the Uno from resetting when the IDE opens the serial port, which would otherwise break programming. 
Arduino Stack Exchange

Set Tools → Programmer → Arduino as ISP. 
arduino.cc

3) Wire the Uno to the ATtiny45/85 (ISP)

On the ATtiny85/45 DIP, pins are numbered with the notch up (pin 1 at top-left). Make these connections:

Arduino Uno	ATtiny85/45	ATtiny pin	Function
5 V	VCC	8	Power
GND	GND	4	Ground
D10	RESET	1	Reset
D11 (MOSI)	MOSI (PB0)	5	Data to tiny
D12 (MISO)	MISO (PB1)	6	Data from tiny
D13 (SCK)	SCK (PB2)	7	SPI clock

This is the standard ISP wiring for an Uno↔ATtiny85/45. 
Instructables

4) Select the right board & set fuses (clock)

Tools → Board → ATTinyCore → “ATtiny25/45/85 (No bootloader)”.

Tools → Chip → ATtiny85 (or ATtiny45 if that’s your chip).

Tools → Clock → “8 MHz (internal)” (recommended starter setting).

(Optional) Tools → B.O.D. set to a value like 2.7 V (or leave default).

Click Tools → Burn Bootloader.

This does not install a bootloader; it writes the fuses to match the options you just chose (clock/BOD/etc.). Do this once per new chip or whenever you change those options. 
GitHub
homemadehardware.com
Electrical Engineering Stack Exchange

5) Upload a sketch to the ATtiny

Keep the wiring in place.

Open (or write) your sketch. For a quick test, use Blink and put the LED (via 220 Ω to GND) on PB0 (ATtiny pin 5) or PB1 (pin 6).

Use Sketch → Upload Using Programmer (not the normal Upload button). This pushes code over SPI via the Uno. 
homemadehardware.com

Quick test sketch (Blink on PB0 / physical pin 5)
// ATtiny45/85 @ 8 MHz (Tools→Clock→8 MHz (internal))
// LED anode -> PB0 (pin 5) through 220Ω; LED cathode -> GND

#include <Arduino.h>
const uint8_t LED = 0; // PB0

void setup() {
  pinMode(LED, OUTPUT);
}

void loop() {
  digitalWrite(LED, HIGH);
  delay(500);
  digitalWrite(LED, LOW);
  delay(500);
}


Upload it with Sketch → Upload Using Programmer. If the LED blinks, you’re good.

Common gotchas & fixes

“Invalid device signature” / timeouts → Re-check every wire, power the ATtiny from the Uno’s 5 V & GND, and confirm Programmer = Arduino as ISP. Ensure the 10 µF cap is across RESET–GND on the Uno so it doesn’t auto-reset. 
Arduino Stack Exchange

Nothing happens after upload → Make sure you Burn Bootloader after changing Tools → Clock; otherwise delays and timing will be wrong. Also confirm your LED is on the same pin you used in code (e.g., PB0). 
GitHub

Clocks → Start with 8 MHz internal. Don’t pick an external clock unless you actually wired a crystal; doing so can “brick” the chip until you recover with a high-voltage or proper ISP/clock source. 
martingunnarsson.com

Using a Mega/Nano instead of Uno → MOSI/MISO/SCK pins are different on those boards; always use the board’s real SPI pins (or its 6-pin ICSP header) when acting as ISP. 
Arduino Forum

Why this matches the video

The video you shared is “Program an ATtiny45/85 with an Arduino Uno without Any Errors | Complete In-Depth Tutorial (2024)”; it walks through installing ATTinyCore, using Arduino as ISP, wiring UNO↔ATtiny over SPI, burning fuses via “Burn Bootloader”, and then uploading via Upload Using Programmer — exactly the flow above. If you prefer following along visually, that’s the one. 
YouTube

If you hit any specific error message, paste it here and I’ll pinpoint the exact fix.
