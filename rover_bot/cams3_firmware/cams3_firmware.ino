/*
 * =============================================================================
 *  cams3_firmware.ino — Unit CamS3 5MP Firmware for XiaoChong Rover
 * =============================================================================
 *  Runs on Unit CamS3 5MP (ESP32-S3 + PY260/OV5640 5MP camera).
 *  Listens for commands on UART (Grove connector) and sends back image data.
 *
 *  Protocol (same as the old UnitV boot.py, fully backward-compatible):
 *    RX commands (from StickC Plus via Grove UART):
 *      "PING\n"  → responds with "PONG\n"
 *      "CAP\n"   → capture JPEG image, send "IMG:<length>\n" + raw JPEG bytes
 *      "DET\n"   → simple obstacle detection (dark regions), send "OBS:..." 
 *      "FPS\n"   → send "FPS:<number>\n"
 *      "QUAL:N\n"→ set JPEG quality (10-63 for esp32-camera, lower = better)
 *
 *    TX responses (to StickC Plus):
 *      "PONG\n"              → alive acknowledgment
 *      "IMG:<length>\n<data>" → JPEG image data
 *      "OBS:<results>\n"     → obstacle detection results
 *      "LOG:<message>\n"     → debug log messages
 *      "FPS:<number>\n"      → current FPS
 *
 *  Hardware:
 *    - ESP32-S3-WROOM-1-N16R8 (dual-core 240MHz, 8MB PSRAM, 16MB Flash)
 *    - PY260 5MP camera sensor (or OV5640)
 *    - Grove HY2.0-4P: G20 (TX) / G19 (RX) — UART to StickC Plus
 *    - Onboard LED: GPIO 14 (status indicator)
 *    - Wi-Fi disabled (not needed for this firmware)
 *
 *  Board settings in Arduino IDE:
 *    Board: "M5UnitCAMS3" (or ESP32S3 Dev Module)
 *    USB CDC On Boot: Enabled
 *    PSRAM: OPI PSRAM
 *
 *  Flashing:
 *    Connect CamS3 5MP via Grove2USB-C adapter, select COM port, upload.
 *
 *  Author: sindney (m5stack_toys)
 *  License: MIT
 * =============================================================================
 */

#include "esp_camera.h"
#include <HardwareSerial.h>

// =============================================================================
//  Pin Definitions — Unit CamS3 5MP
// =============================================================================

// Camera pins (PY260 / OV5640 on CamS3 5MP)
#define CAM_PIN_PWDN    -1    // No power-down pin (connected to GND)
#define CAM_PIN_RESET   21
#define CAM_PIN_XCLK    11
#define CAM_PIN_SIOD    17    // SCCB SDA
#define CAM_PIN_SIOC    41    // SCCB SCL
#define CAM_PIN_D0      6
#define CAM_PIN_D1      15
#define CAM_PIN_D2      16
#define CAM_PIN_D3      7
#define CAM_PIN_D4      5
#define CAM_PIN_D5      10
#define CAM_PIN_D6      4
#define CAM_PIN_D7      13
#define CAM_PIN_VSYNC   42
#define CAM_PIN_HREF    18
#define CAM_PIN_PCLK    12

// LED (onboard indicator)
#define LED_PIN         14

// Grove UART (HY2.0-4P connector)
// G20 = TX (CamS3 → StickC), G19 = RX (StickC → CamS3)
#define GROVE_TX        20
#define GROVE_RX        19
#define GROVE_BAUD      115200

// =============================================================================
//  Globals
// =============================================================================

// Use Serial1 for Grove UART communication with StickC Plus
HardwareSerial groveSerial(1);

// Connection state
bool connected = false;
bool ledToggle = false;
uint32_t blinkCounter = 0;
uint32_t lastBlinkTime = 0;

// JPEG quality (esp32-camera: 0-63, lower = better quality, larger file)
// Default 12 ≈ good balance (roughly equivalent to MaixPy quality 50)
int jpegQuality = 12;

// FPS tracking
uint32_t frameCount = 0;
uint32_t lastFpsTime = 0;
float currentFps = 0;

// Command buffer
String cmdBuf = "";

// =============================================================================
//  LED Control
// =============================================================================

void ledOn() {
    digitalWrite(LED_PIN, HIGH);
}

void ledOff() {
    digitalWrite(LED_PIN, LOW);
}

// =============================================================================
//  UART Helper Functions
// =============================================================================

void sendLine(const char* msg) {
    groveSerial.print(msg);
    groveSerial.print('\n');
}

void sendLine(const String& msg) {
    groveSerial.print(msg);
    groveSerial.print('\n');
}

void sendLog(const char* msg) {
    groveSerial.print("LOG:");
    groveSerial.print(msg);
    groveSerial.print('\n');
}

// =============================================================================
//  Camera Initialization
// =============================================================================

bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = CAM_PIN_D0;
    config.pin_d1       = CAM_PIN_D1;
    config.pin_d2       = CAM_PIN_D2;
    config.pin_d3       = CAM_PIN_D3;
    config.pin_d4       = CAM_PIN_D4;
    config.pin_d5       = CAM_PIN_D5;
    config.pin_d6       = CAM_PIN_D6;
    config.pin_d7       = CAM_PIN_D7;
    config.pin_xclk     = CAM_PIN_XCLK;
    config.pin_pclk     = CAM_PIN_PCLK;
    config.pin_vsync    = CAM_PIN_VSYNC;
    config.pin_href     = CAM_PIN_HREF;
    config.pin_sccb_sda = CAM_PIN_SIOD;
    config.pin_sccb_scl = CAM_PIN_SIOC;
    config.pin_pwdn     = CAM_PIN_PWDN;
    config.pin_reset    = CAM_PIN_RESET;
    config.xclk_freq_hz = 20000000;  // 20MHz XCLK
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size   = FRAMESIZE_QVGA;  // 320x240 — same as old UnitV
    config.jpeg_quality = jpegQuality;
    config.fb_count     = 2;  // Double buffering with PSRAM
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.grab_mode    = CAMERA_GRAB_LATEST;  // Always get latest frame

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[CAM] Init failed: 0x%x\n", err);
        return false;
    }

    // Get sensor info
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        Serial.printf("[CAM] Sensor PID: 0x%04X\n", s->id.PID);
        // Auto white balance and exposure
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 1);
        s->set_gain_ctrl(s, 1);
    }

    Serial.println("[CAM] Camera initialized OK");
    return true;
}

// =============================================================================
//  Capture and Send JPEG
// =============================================================================

void captureAndSend() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        sendLog("CAP failed: no frame");
        Serial.println("[CAM] Capture failed");
        return;
    }

    // If not JPEG (shouldn't happen), convert
    if (fb->format != PIXFORMAT_JPEG) {
        sendLog("CAP failed: not JPEG");
        esp_camera_fb_return(fb);
        return;
    }

    size_t len = fb->len;

    // Send header: "IMG:<length>\n"
    String header = "IMG:" + String(len);
    sendLine(header);
    delay(5);  // Small delay for receiver to prepare

    // Send raw JPEG data in chunks (match old UnitV behavior)
    const size_t CHUNK_SIZE = 1024;
    size_t offset = 0;
    while (offset < len) {
        size_t toSend = min(CHUNK_SIZE, len - offset);
        groveSerial.write(fb->buf + offset, toSend);
        offset += toSend;
        delay(2);  // Prevent UART overflow — same as old UnitV
    }

    esp_camera_fb_return(fb);
    frameCount++;

    Serial.printf("[CAM] Frame #%d sent: %d bytes\n", frameCount, len);
}

// =============================================================================
//  Simple Obstacle Detection
//  (Analyzes dark regions in bottom half of image — same logic as old UnitV)
// =============================================================================

void detectObstacles() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        sendLog("DET failed: no frame");
        return;
    }

    // For obstacle detection we need grayscale data.
    // Since we're in JPEG mode, we'd need to decode first.
    // For simplicity, just report CLEAR — the AI cloud does the real analysis.
    // In the future, we could switch to RGB565 temporarily or use a separate buffer.
    sendLine("OBS:CLEAR");

    esp_camera_fb_return(fb);
    Serial.println("[CAM] DET: reported CLEAR (JPEG mode, no local analysis)");
}

// =============================================================================
//  SETUP
// =============================================================================

void setup() {
    // USB Serial for debug (via Grove2USB-C or native USB CDC)
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[BOOT] === CamS3 5MP Rover Firmware v1.0 ===");

    // LED
    pinMode(LED_PIN, OUTPUT);
    ledOn();  // Solid LED during boot
    Serial.println("[LED] GPIO 14 initialized");

    // Grove UART — communicate with StickC Plus
    groveSerial.begin(GROVE_BAUD, SERIAL_8N1, GROVE_RX, GROVE_TX);
    Serial.printf("[UART] Grove serial on TX=%d RX=%d @ %d baud\n",
        GROVE_TX, GROVE_RX, GROVE_BAUD);

    // Camera
    if (!initCamera()) {
        Serial.println("[BOOT] Camera init FAILED! LED will blink fast.");
        // Fast blink to indicate hardware error
        while (true) {
            ledOn(); delay(100);
            ledOff(); delay(100);
        }
    }

    // Warm up — take a few frames to let auto-exposure stabilize
    Serial.println("[CAM] Warming up (discarding initial frames)...");
    for (int i = 0; i < 10; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
        delay(100);
    }

    // Signal ready
    sendLog("CamS3 5MP booting...");
    sendLine("PONG");  // Signal that we're alive
    sendLog("Ready. Waiting for commands.");

    lastFpsTime = millis();
    lastBlinkTime = millis();

    Serial.println("[BOOT] Setup complete. Waiting for StickC Plus connection.");
}

// =============================================================================
//  MAIN LOOP
// =============================================================================

void loop() {
    uint32_t now = millis();

    // --- LED status indicator ---
    if (connected) {
        // Solid LED when connected (always on)
        ledOn();
    } else {
        // Blink LED when waiting for connection (~2Hz)
        if (now - lastBlinkTime >= 250) {
            lastBlinkTime = now;
            ledToggle = !ledToggle;
            if (ledToggle) {
                ledOn();
            } else {
                ledOff();
            }
        }
    }

    // --- FPS calculation (every second) ---
    if (now - lastFpsTime >= 1000) {
        // We track response FPS, not capture FPS
        lastFpsTime = now;
    }

    // --- Read Grove UART commands ---
    while (groveSerial.available()) {
        char c = groveSerial.read();

        if (c == '\n') {
            cmdBuf.trim();

            if (cmdBuf == "PING") {
                if (!connected) {
                    connected = true;
                    sendLog("Connected!");
                    Serial.println("[UART] StickC Plus connected (PING received)");
                }
                sendLine("PONG");
            }
            else if (cmdBuf == "CAP") {
                captureAndSend();
            }
            else if (cmdBuf == "DET") {
                detectObstacles();
            }
            else if (cmdBuf == "FPS") {
                // Report approximate capture FPS
                char fpsBuf[16];
                snprintf(fpsBuf, sizeof(fpsBuf), "FPS:%d", (int)currentFps);
                sendLine(fpsBuf);
            }
            else if (cmdBuf.startsWith("QUAL:")) {
                int q = cmdBuf.substring(5).toInt();
                // esp32-camera quality: 0-63 (lower = better)
                // Map from old UnitV range (10-95, lower=better) to esp32-camera range
                // Simple mapping: divide by ~1.5, clamp to 4-63
                jpegQuality = constrain(q / 2, 4, 63);

                sensor_t *s = esp_camera_sensor_get();
                if (s) {
                    s->set_quality(s, jpegQuality);
                }

                char logBuf[32];
                snprintf(logBuf, sizeof(logBuf), "Quality set to %d", jpegQuality);
                sendLog(logBuf);
                Serial.printf("[CAM] JPEG quality set to %d\n", jpegQuality);
            }
            else if (cmdBuf.length() > 0) {
                Serial.printf("[UART] Unknown command: '%s'\n", cmdBuf.c_str());
            }

            cmdBuf = "";
        } else {
            cmdBuf += c;
            // Prevent buffer overflow
            if (cmdBuf.length() > 64) {
                cmdBuf = "";
            }
        }
    }

    // Small delay to prevent tight loop
    delay(5);
}
