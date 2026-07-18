// ioexpander.cpp — Single shared M5IOE1 instance for vibration motor.

#include "include/ioexpander.h"

#include <Arduino.h>
#include <M5Unified.h>

namespace ioe {

M5IOE1 expander;
bool    ready = false;

bool init() {
    if (ready) return true;
    // The C152's M5IOE1 chip is on the *internal* I2C bus (pins 47/48),
    // not the default Wire bus (external, 11/10). Pass M5.In_I2C so
    // expander.begin() talks to the right pins; otherwise we get
    // SDA=255, SCL=255 and the init silently fails.
    auto err = expander.begin(&M5.In_I2C);
    if (err == M5IOE1_OK) {
        ready = true;
    }
    return ready;
}

void setVibrationDuty(uint8_t duty) {
    if (!ready) return;
    expander.setPwmDuty(9, duty);
}

}  // namespace ioe