// stopwatch_multiverse.ino
// Four-app launcher firmware for the M5Stack StopWatch (C152).
//
// Apps shipped: Stopwatch, Balance, Badge, USB Drive.
// Inputs: click KEYA / KEYB for nav + actions (release within 500 ms);
//         tap to enter; press KEYA+KEYB together to exit any app back
//         to the launcher (or to enter the focused app from it).
//
// Badge images land on the watch via USB mass storage: the USB Drive
// app presents the badgeimg FAT partition as a removable drive, the
// host writes slot_XX.bin files (see tools/badge_uploader.html), and
// the Badge app reads them back through the read-only FAT mount.

#include <Arduino.h>
#include <M5Unified.h>

#include "include/app.h"
#include "include/shell.h"
#include "include/theme.h"
#include "include/ioexpander.h"
#include "include/usb_drive.h"

#include "include/app_stopwatch.h"
#include "include/app_balance.h"
#include "include/app_badge.h"
#include "include/app_usb_drive.h"
#include "include/badge_fs.h"

static StopwatchApp   g_stopwatch;
static BalanceApp     g_balance;
static BadgeApp       g_badge;
static UsbDriveApp    g_usb;

static App* g_apps[] = {
    &g_stopwatch,
    &g_balance,
    &g_badge,
    &g_usb,
};

void setup() {
    auto cfg = M5.config();
    cfg.internal_imu = true;     // matches the official sample
    M5.begin(cfg);

    Serial.begin(115200);
    Serial.println("\n=== stopwatch_multiverse ===");

    ioe::init();

    Serial.printf("M5.Imu.isEnabled=%s type=%d\n",
                  M5.Imu.isEnabled() ? "true" : "false",
                  (int)M5.Imu.getType());

    theme::init();

    // Device-side view of the badge partition: read-only FAT mount,
    // formatted on first boot. Runs before the USB stack starts so the
    // "mounted <=> media absent" invariant holds from the start.
    badgefs::begin();

    // USB composite: CDC console (this Serial) + MSC card reader for the
    // badgeimg partition. Media starts absent; the USB Drive app presents
    // it. Must run before shell::begin so the boot log is visible.
    usbdrive::begin();

    shell::begin(g_apps, sizeof(g_apps) / sizeof(g_apps[0]));
}

void loop() {
    M5.update();
    shell::pumpM5();
    shell::tick();
}
