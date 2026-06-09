// ---------------------------------------------------------------------------
// SleepKeyboard.ino — HijelHID_BLEKeyboard Example
// ---------------------------------------------------------------------------
// Copyright (c) 2026 Hijel. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, this software
// is provided "AS IS", WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied. The author(s) accept no liability for any damages,
// loss, or consequences arising from the use or misuse of this software.
// See the License for the full terms governing permissions and limitations.
// ---------------------------------------------------------------------------
// Demonstrates deep and light sleep with TX power configuration and
// getIdleTime() for idle detection.
//
// Press the BOOT button to type a short string to the host.
// If the button is not pressed for IDLE_SLEEP_TIMEOUT milliseconds, the
// device enters sleep. Which type of sleep is selected below.
//
// Hardware:
//   No wiring required — uses the onboard BOOT button (GPIO 0).
//   GPIO 0 is an RTC GPIO on the original ESP32 and most common variants,
//   required for ext0 deep and light sleep wakeup. Check your board's pinout
//   if wake does not work — not all variants expose GPIO 0 as RTC-capable.
// ---------------------------------------------------------------------------

#include <HijelHID_BLEKeyboard.h>

// ---------------------------------------------------------------------------
// Sleep Mode Selection
// ---------------------------------------------------------------------------
//
// Uncomment ONE of the two lines below to choose the sleep type.
// Only one should be active at a time.
//
//  SLEEP_MODE_DEEP  (default)
//    Maximum power saving. The CPU, RAM, and BLE stack are fully powered off.
//    On wake, the chip resets and setup() runs from the beginning. The BLE
//    bond survives in flash — the host reconnects automatically after
//    keyboard.begin(). afterWake() is not used in this mode.
//
//  SLEEP_MODE_LIGHT
//    Moderate power saving. RAM and the BLE stack are preserved across sleep.
//    On wake, execution resumes after esp_light_sleep_start() — setup() does
//    not run again. afterWake() blocks until the host reconnects and
//    authenticates, then typing resumes without re-pairing. Reconnect is
//    faster than deep sleep but current draw during sleep is higher.

#define  SLEEP_MODE_DEEP
// #define SLEEP_MODE_LIGHT

// Guard against invalid or conflicting selection at compile time.
#if !defined(SLEEP_MODE_DEEP) && !defined(SLEEP_MODE_LIGHT)
  #error "No sleep mode selected. Uncomment SLEEP_MODE_DEEP or SLEEP_MODE_LIGHT above."
#endif
#if defined(SLEEP_MODE_DEEP) && defined(SLEEP_MODE_LIGHT)
  #error "Both sleep modes are defined. Comment out one of SLEEP_MODE_DEEP or SLEEP_MODE_LIGHT."
#endif

// ---------------------------------------------------------------------------
// Pin definitions
// ---------------------------------------------------------------------------

// GPIO 0 is the BOOT button on most ESP32 dev boards.
// It is also an RTC GPIO, required for ext0 sleep wakeup in both modes.
// Change this to another RTC-capable GPIO if your board uses a different pin.
#define BOOT_BUTTON_PIN    GPIO_NUM_0

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

// How long to wait with no button press before entering sleep (milliseconds).
#define IDLE_SLEEP_TIMEOUT 60000

// ---------------------------------------------------------------------------
// Keyboard object
// ---------------------------------------------------------------------------

HijelHID_BLEKeyboard keyboard("SleepKeyboard", "Hijel");

// ---------------------------------------------------------------------------
// setup()
//
// Runs once at power-on and on every deep sleep wake (deep sleep is a full
// chip reset). With light sleep, setup() only runs at power-on — wakes
// resume inside goToSleep() after esp_light_sleep_start().
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);

    // With deep sleep, detect whether this is a cold boot or a wake event
    // so we can print the appropriate startup message.
#if defined(SLEEP_MODE_DEEP)
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("Woke from deep sleep — reconnecting...");
    } else {
        Serial.println("Cold boot — starting.");
    }
#endif

    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    // Set TX power before begin().
    // Lower power saves energy at close range.
    // Scale: 1 = -12 dBm (lowest), 8 = +9 dBm (highest, default).
    keyboard.setTxPower(4);  // -3 dBm — a good middle ground for desktop use

    // Start BLE. If a bond is already stored the host will reconnect
    // automatically. On a deep sleep wake, this is equivalent to a fresh
    // start — the bond in flash is reloaded and advertising begins.
    keyboard.begin();

    Serial.println("Ready — press the BOOT button to type.");
}

// ---------------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------------

void loop() {

    // Do nothing until the host has fully paired and authenticated.
    if (!keyboard.isPaired()) {
        delay(500);
        return;
    }

    // Check if the button has been pressed (pin reads LOW when pressed).
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {

        // Small debounce delay — ignore very brief contact.
        delay(50);

        // Confirm the button is still held before acting.
        if (digitalRead(BOOT_BUTTON_PIN) == LOW) {

            keyboard.println("Hello from SleepKeyboard!");
            Serial.println("Button pressed — typed a line.");

            // Wait for the button to be released before looping again.
            while (digitalRead(BOOT_BUTTON_PIN) == LOW) {
                delay(10);
            }
        }
    }

    // Enter sleep if the button has not been pressed for IDLE_SLEEP_TIMEOUT.
    if (keyboard.getIdleTime() >= IDLE_SLEEP_TIMEOUT) {
        goToSleep();
    }

    delay(10);
}

// ---------------------------------------------------------------------------
// goToSleep()
//
// Behaviour differs between the two modes:
//
// SLEEP_MODE_DEEP
//   Releases any held keys, configures GPIO 0 as the ext0 wake source, then
//   calls esp_deep_sleep_start(). This function never returns — on wake the
//   chip resets and setup() runs from the beginning.
//
// SLEEP_MODE_LIGHT
//   Calls beforeSleep() to release held keys and prepare priming state,
//   configures GPIO 0 as the ext0 wake source, then calls
//   esp_light_sleep_start(). Execution resumes at the line after that call.
//   afterWake() then blocks until the host has reconnected and authenticated,
//   after which loop() continues normally.
// ---------------------------------------------------------------------------

void goToSleep() {

#if defined(SLEEP_MODE_DEEP)

    Serial.println("No activity — entering deep sleep.");
    Serial.println("Press the BOOT button to wake.");
    Serial.flush();

    // Release any held keys cleanly before powering off.
    // Optional for deep sleep (setup() reinitialises everything on wake),
    // but good practice so the host does not see keys stuck down.
    keyboard.beforeSleep();

    // Configure the BOOT button (GPIO 0) as the ext0 wake source.
    // ext0 monitors a single RTC GPIO and wakes when it reaches the
    // specified level. Level 0 wakes when the pin is driven LOW (button pressed).
    esp_sleep_enable_ext0_wakeup(BOOT_BUTTON_PIN, 0);

    // Enter deep sleep. This call never returns.
    // On wake, the chip resets and setup() runs from the beginning.
    esp_deep_sleep_start();

#elif defined(SLEEP_MODE_LIGHT)

    Serial.println("No activity — entering light sleep.");
    Serial.println("Press the BOOT button to wake.");
    Serial.flush();

    // Release any held keys and set the priming flag so the first report
    // after wake absorbs the Windows HID resume handshake correctly.
    keyboard.beforeSleep();

    // Configure the BOOT button (GPIO 0) as the ext0 wake source.
    // ext0 monitors a single RTC GPIO and wakes when it reaches the
    // specified level. Level 0 wakes when the pin is driven LOW (button pressed).
    esp_sleep_enable_ext0_wakeup(BOOT_BUTTON_PIN, 0);

    // Enter light sleep. RAM and the BLE stack are preserved.
    // The CPU and radio power down until the wake source fires.
    esp_light_sleep_start();

    // Execution resumes here after the BOOT button wakes the chip.
    Serial.println("Woke from light sleep — waiting for host to reconnect...");

    // Block until the host reconnects and completes LTK re-encryption.
    // Times out after the value set by setAfterWakeTimeout() (default 15s).
    keyboard.afterWake();

    if (keyboard.isPaired()) {
        keyboard.println("I am AWAKE!");
        Serial.println("Reconnected — resuming.");
    } else {
        Serial.println("afterWake() timed out — host did not reconnect.");
    }

#endif
}
