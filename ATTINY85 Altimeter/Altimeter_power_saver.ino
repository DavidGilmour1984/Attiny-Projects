void loop() {
  // ---- Pre-launch ----
  if (!launched) {
    static unsigned long lastBeat = 0;
    if (millis() - lastBeat > 10000) {     // heartbeat every 10 s
      heartbeat();
      lastBeat = millis();
    }

    static unsigned long lastCheck = 0;
    if (millis() - lastCheck >= 1000) {    // altitude check once per second
      bmp.measure();
      float alt = bmp.getRelativeAltitudeM();
      if (alt > 5) {
        launched = true;
        launchTime = millis();
        maxAltitude = alt;
      }
      lastCheck = millis();
    }
    return;
  }

  // ---- Logging after launch ----
  if (launched && !landed) {
    if (millis() - launchTime <= 6000) {   // log for 6 s @ 20 Hz
      static unsigned long lastSample = 0;
      if (millis() - lastSample >= 50) {
        bmp.measure();
        float alt = bmp.getRelativeAltitudeM();
        if (alt > maxAltitude) maxAltitude = alt;
        lastSample = millis();
      }
      return;
    } else {
      landed = true;
      startFlashAltitude((int)maxAltitude);   // first flash sequence
    }
  }

  // ---- After landing: repeat altitude flash every 10 s ----
  if (landed) {
    updateFlashAltitude();   // drive the non-blocking flash state

    if (!flashing) {
      static unsigned long lastRepeat = 0;
      if (millis() - lastRepeat >= 10000) {   // repeat interval
        startFlashAltitude((int)maxAltitude);
        lastRepeat = millis();
      }

      // watchdog sleep between repeats
      cli();
      MCUSR &= ~(1<<WDRF);
      WDTCR |= (1<<WDCE) | (1<<WDE);
      WDTCR = (1<<WDIE) | (1<<WDP3) | (1<<WDP0); // ~8 s interrupt
      sei();

      set_sleep_mode(SLEEP_MODE_PWR_DOWN);
      sleep_enable();
      WDTCR |= (1 << WDIF);   // clear interrupt flag
      sleep_cpu();
      sleep_disable();
    }
  }
}
