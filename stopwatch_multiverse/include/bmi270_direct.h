// bmi270_direct.h — Direct BMI270 access bypassing M5.Imu.

#pragma once

#include <stdint.h>

namespace bmi {

extern bool ready;
extern uint8_t whoAmI;
extern uint8_t i2cAddr;

// Bring up the BMI270 on the internal I²C bus (SDA=47, SCL=48) at
// 400 kHz. Tries address 0x69 first (the address M5Unified probes for
// the BMI270), then falls back to 0x68. Configures the accelerometer
// for ±2 g at 100 Hz ODR and explicitly enables ACC+GYR (without AUX),
// so the chip is powered even on boards without a BMM150 magnetometer
// — which is the C152 StopWatch.
//
// Returns false if WHO_AM_I doesn't read 0x24 on either address.
bool begin();

// Read accelerometer in g (assuming ±2 g range).
bool readAccel(float* ax, float* ay, float* az);

}  // namespace bmi
