// ioexpander.h — Single shared M5IOE1 instance for vibration-motor PWM.
//
// M5Unified does not integrate M5IOE1 (it predates the unified HAL), so
// each app that needs the vibration motor goes through this single global
// instance declared in ioexpander.cpp. Initialise once from setup().

#pragma once

#include <M5IOE1.h>

namespace ioe {

// One global M5IOE1 instance. Apps use this directly for PWM channels
// (channel 9 is the vibration motor on the StopWatch carrier).
extern M5IOE1 expander;

// True after init() succeeded. M5IOE1 has no public isEnabled() method,
// so we track initialisation here.
extern bool ready;

// Initialise the I²C expander. Idempotent. Returns true on success.
bool init();

// Set PWM duty on the vibration channel. No-op if not initialised.
void setVibrationDuty(uint8_t duty);

}  // namespace ioe