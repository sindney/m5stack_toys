// bmi270_direct.cpp — Bypass M5Unified's M5.Imu and talk to the BMI270
// directly on the C152's internal I²C bus.
//
// Why this exists:
//   M5Unified's BMI270_Class::begin() only writes PWR_CTRL=0x0F (a
//   configuration that includes AUX power) AFTER it detects a BMM150
//   magnetometer on the auxiliary interface. The C152 StopWatch has no
//   magnetometer, so:
//     * the BMI270 stays unpowered (default PWR_CTRL after reset is
//       typically 0x00)
//     * isEnabled() returns true because the chip IS on the bus and
//       responds to WHO_AM_I
//     * M5.Imu.getAccel() reads zeros, getImuData().accel reads zeros
//   This driver explicitly enables ACC+GYR without AUX, so the
//   accelerometer and gyroscope come up regardless.
//
// Address: M5Unified's BMI270_Class.hpp declares DEFAULT_ADDRESS=0x69.
// We probe 0x69 first (matches M5Unified's first attempt) and fall back
// to 0x68 for boards where SDO is wired low.
//
// Bus: Wire (default). M5.begin() configures Wire on SDA=47, SCL=48
// when cfg.internal_imu=true; we use that same bus so this driver and
// the rest of the firmware share a single I²C instance.

#include "include/bmi270_direct.h"

#include <Arduino.h>
#include <Wire.h>

namespace bmi {

namespace {
constexpr uint8_t ADDR_PRIMARY  = 0x69;
constexpr uint8_t ADDR_FALLBACK = 0x68;

constexpr uint8_t REG_CHIP_ID         = 0x00;
constexpr uint8_t REG_PWR_CONF       = 0x7C;
constexpr uint8_t REG_PWR_CTRL       = 0x7D;
constexpr uint8_t REG_ACC_CONF       = 0x40;
constexpr uint8_t REG_ACC_X_LSB      = 0x0C;
constexpr uint8_t WHO_AM_I_EXPECTED = 0x24;

bool writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

uint8_t readReg(uint8_t addr, uint8_t reg) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0;
    Wire.requestFrom((int)addr, 1);
    if (!Wire.available()) return 0;
    return Wire.read();
}

}  // namespace

bool ready = false;
uint8_t whoAmI = 0;
uint8_t i2cAddr = 0;

// Bring up the BMI270 in the way the C152 actually wants:
//   * `cfg.internal_imu = true` lets M5.begin() run the
//     BMI270_Class::begin path (soft-reset, PWR_CONF=0, config file
//     upload, etc.) — this is what makes `M5.Imu.isEnabled()` return
//     true afterwards, matching the sample's "keep imu enable from
//     the beginning" requirement.
//   * That path then writes PWR_CTRL = 0x0F only IF a BMM150
//     magnetometer is found on the auxiliary interface. The C152
//     has no magnetometer, so the BMI270 stays in default-low-power
//     mode and every subsequent `getAccel` returns 0.
//   * We fix the gap by writing PWR_CTRL = 0x06 (ACC+GYR on, AUX
//     off) and ACC_CONF = 0xA8 (±2 g, 100 Hz ODR) ourselves.
// We deliberately do NOT soft-reset, do NOT re-upload the config
// file, and do NOT touch PWR_CONF — those are already handled by
// M5.begin(), and redoing them would discard the calibration.
bool begin() {
    whoAmI = readReg(ADDR_PRIMARY, REG_CHIP_ID);
    if (whoAmI == WHO_AM_I_EXPECTED) {
        i2cAddr = ADDR_PRIMARY;
    } else {
        whoAmI = readReg(ADDR_FALLBACK, REG_CHIP_ID);
        if (whoAmI == WHO_AM_I_EXPECTED) {
            i2cAddr = ADDR_FALLBACK;
        } else {
            whoAmI = 0;
            i2cAddr = 0;
            return false;
        }
    }

    // Accelerometer: ±2 g, 100 Hz ODR, normal mode (no filter).
    //   ACC_CONF bits: filter_perf=1, bwp=01, odr=100Hz, mode=normal
    //   = 0b1010_1000 = 0xA8
    if (!writeReg(i2cAddr, REG_ACC_CONF, 0xA8)) return false;
    delay(1);

    // Power up ACC + GYR but leave AUX off — the C152 has no BMM150.
    //   PWR_CTRL bits: temp_en=0, acc_en=1, gyr_en=1, aux_en=0
    //   = 0b0000_0110 = 0x06
    if (!writeReg(i2cAddr, REG_PWR_CTRL, 0x06)) return false;
    delay(1);

    ready = true;
    return true;
}

bool readAccel(float* ax, float* ay, float* az) {
    if (!ready) return false;

    Wire.beginTransmission(i2cAddr);
    Wire.write(REG_ACC_X_LSB);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom((int)i2cAddr, 6);
    if (Wire.available() < 6) return false;

    int16_t raw[3];
    uint8_t* p = reinterpret_cast<uint8_t*>(raw);
    for (int i = 0; i < 6; ++i) p[i] = Wire.read();

    // ±2 g range: 1 g = 16384 LSB.
    constexpr float aRes = 2.0f / 32768.0f;
    *ax = raw[0] * aRes;
    *ay = raw[1] * aRes;
    *az = raw[2] * aRes;
    return true;
}

}  // namespace bmi
