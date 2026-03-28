/*
 * ============================================================================
 *  rover_bot.ino — Voice-Controlled Rover with TOF Collision Avoidance
 * ============================================================================
 *  Voice-controlled rover running on M5StickC Plus.
 *  Uses Unit ASR for offline voice commands and Unit TOF for collision avoidance.
 *
 *  Hardware:
 *    - Main Controller: M5StickC Plus (ESP32-PICO-D4)
 *    - Chassis: RoverC Pro (I2C 0x38, Mecanum wheels)
 *    - Front Sensor: Unit TOF (VL53L0X, I2C 0x29) — RoverC Grove
 *    - Voice Control: Unit ASR (CI-03T, UART 115200) — StickC Plus bottom Grove
 *
 *  Unit ASR Preloaded Commands (UART protocol: 0xAA 0x55 [ID] 0x55 0xAA):
 *    0xFF "Hi M Five"  — Wake up ASR
 *    0x07 "forward"    — Move forward (with TOF collision check)
 *    0x09 "backward"   — Move backward
 *    0x04 "turn left"  — Rotate left (in-place spin)
 *    0x06 "turn right"  — Rotate right (in-place spin)
 *    0x03 "left"       — Lateral move left (mecanum strafe)
 *    0x05 "right"      — Lateral move right (mecanum strafe)
 *    0x13 "stop"       — Stop all movement
 *    0x10 "open"       — Open front gripper
 *    0x11 "close"      — Close front gripper
 *    0x15 "turn off"   — Deep sleep / shutdown
 *
 *  Collision Avoidance:
 *    When moving forward, TOF distance is checked every 50ms.
 *    If obstacle < 100mm (10cm): stop + beep warning.
 *
 *  Dependencies:
 *    - M5Unified, M5GFX, Wire
 *    - VL53L0X (Pololu)
 *    - RoverC Pro controlled via direct I2C (motors: 0x00-0x03, servos: 0x10+)
 *
 *  Author: sindney (m5stack_toys)
 *  License: MIT
 * ============================================================================
 */

#include <M5Unified.h>
#include <Wire.h>
#include <VL53L0X.h>

// ============================================================================
//  Pin & Address Definitions
// ============================================================================

// RoverC Pro I2C (HAT connector — G0=SDA, G26=SCL)
#define ROVER_SDA   0
#define ROVER_SCL   26
#define ROVER_ADDR  0x38

// TOF sensor (shared I2C bus via RoverC Grove)
#define TOF_ADDR    0x29

// Unit ASR UART — StickC Plus bottom Grove
// Grove Yellow wire = G32 = StickC TX -> ASR RX
// Grove White wire  = G33 = StickC RX <- ASR TX
#define ASR_UART_RX    33
#define ASR_UART_TX    32
#define ASR_UART_BAUD  115200

// ASR Protocol constants
// Frame format: [0xAA] [0x55] [CMD_ID] [0x55] [0xAA]  (5 bytes)
// Or 6-byte:    [0xAA] [0x55] [CMD_ID] [MSG]  [0x55] [0xAA]
#define ASR_HEAD1       0xAA
#define ASR_HEAD2       0x55
#define ASR_TAIL1       0x55
#define ASR_TAIL2       0xAA
#define ASR_FRAME_LEN   5

// ASR Command IDs (preloaded firmware — from official M5Unit-ASR library)
#define ASR_CMD_UNKNOWN    0x00  // Unknown
#define ASR_CMD_UP         0x01  // "up"
#define ASR_CMD_DOWN       0x02  // "down"
#define ASR_CMD_LEFT       0x03  // "left"
#define ASR_CMD_TURNLEFT   0x04  // "turn left"
#define ASR_CMD_RIGHT      0x05  // "right"
#define ASR_CMD_TURNRIGHT  0x06  // "turn right"
#define ASR_CMD_FORWARD    0x07  // "forward"
#define ASR_CMD_FRONT      0x08  // "front"
#define ASR_CMD_BACKWARD   0x09  // "backward"
#define ASR_CMD_BACK       0x0A  // "back"
#define ASR_CMD_OPEN       0x10  // "open"
#define ASR_CMD_CLOSE      0x11  // "close"
#define ASR_CMD_START      0x12  // "start"
#define ASR_CMD_STOP       0x13  // "stop"
#define ASR_CMD_TURNON     0x14  // "turn on"
#define ASR_CMD_TURNOFF    0x15  // "turn off"
#define ASR_CMD_PLAY       0x16  // "play"
#define ASR_CMD_PAUSE      0x17  // "pause"
#define ASR_CMD_WAKEUP     0xFF  // "Hi, M Five" (wake word)

// Collision avoidance thresholds (mm)
#define TOF_WARN_DIST    150   // Slow down warning
#define TOF_STOP_DIST    100   // Emergency stop (10cm)
#define TOF_BEEP_DIST    100   // Beep when too close

// ============================================================================
//  Rover States
// ============================================================================
enum RoverState : uint8_t {
    STATE_BOOT       = 0,
    STATE_SCAN       = 1,   // Hardware self-check
    STATE_READY      = 2,   // Ready for voice commands
    STATE_SHUTDOWN   = 3,
};

// Movement directions
enum MoveDir : uint8_t {
    DIR_STOP = 0,
    DIR_FORWARD,
    DIR_BACKWARD,
    DIR_LEFT,
    DIR_RIGHT,
    DIR_TURN_LEFT,
    DIR_TURN_RIGHT,
};

// ============================================================================
//  Global State
// ============================================================================

static RoverState g_state          = STATE_BOOT;
static MoveDir    g_moveDir        = DIR_STOP;

// Hardware detection
static bool       g_hasRover       = false;
static bool       g_hasTOF         = false;
static bool       g_hasASR         = false;

// RoverC Pro direct I2C control
// Register map (matching official M5_RoverC library):
//   0x00-0x03: Motor speed (int8_t, -100~100)  [setSpeed writes here]
//   0x10+pos:  Servo angle (uint8_t, 0~180)    [setServoAngle writes here]
// Motor layout (top view, front = TOF sensor side):
//   M1 (front-left)    M2 (front-right)
//   M3 (rear-left)     M4 (rear-right)
#define ROVERC_MOTOR_REG  0x00  // Starting register for motor speeds
#define ROVERC_SERVO_REG  0x10  // Starting register for servo angles (0x10+pos)

// Gripper servo (front claw on RoverC Pro)
#define GRIPPER_SERVO_CH     0     // Servo channel 0 = front gripper
#define GRIPPER_OPEN_ANGLE   10    // Angle for open position (small angle = open)
#define GRIPPER_CLOSE_ANGLE  90    // Angle for closed position (large angle = closed)

// TOF
VL53L0X           g_tof;
static uint16_t   g_tofDistance     = 9999;
static bool       g_tofReady       = false;
static uint32_t   g_lastTOFRead    = 0;

// Motor
static int8_t     g_motorSpeed     = 35;

// Battery
static float      g_batVoltage     = 0;
static float      g_batPercent     = 0;
static uint32_t   g_lastBatCheck   = 0;
static bool       g_lowBatWarned   = false;

// Display
static M5Canvas   g_canvas(&M5.Display);
static uint32_t   g_lastUIUpdate   = 0;

// ASR state
static String     g_lastASRCmd     = "";       // Last recognized command name
static uint32_t   g_lastASRTime    = 0;        // When last ASR command arrived
static bool       g_asrAwake       = false;    // Whether ASR is in listening mode
static uint32_t   g_asrWakeTime    = 0;        // When ASR was woken up

// ASR debug — raw UART hex display
static String     g_asrRawHex      = "";       // Last N raw bytes as hex string
static uint32_t   g_asrRxCount     = 0;        // Total bytes received from ASR UART
static uint32_t   g_asrFrameCount  = 0;        // Total valid frames parsed
static String     g_motorLastCmd   = "";       // Last motor I2C command sent

// Collision avoidance
static bool       g_tofBlocked     = false;    // TOF detected obstacle while moving forward
static uint32_t   g_lastBeepTime   = 0;

// Movement timing (commands run for a fixed duration then stop)
static uint32_t   g_moveStartTime  = 0;
static uint32_t   g_moveDuration   = 0;       // 0 = continuous until stop

// Motor soft-start ramp
static int        g_targetVtx      = 0;        // Target velocity X (strafe)
static int        g_targetVty      = 0;        // Target velocity Y (forward/back)
static int        g_targetWt       = 0;        // Target angular velocity (rotation)
static int        g_currentVtx     = 0;        // Current ramped velocity X
static int        g_currentVty     = 0;        // Current ramped velocity Y
static int        g_currentWt      = 0;        // Current ramped angular velocity
static uint32_t   g_lastRampTime   = 0;
static bool       g_ramping        = false;
static uint32_t   g_lastMotorRefresh = 0;      // Periodic motor command refresh

// Gripper state
static bool       g_gripperOpen    = false;     // Current gripper state (starts closed)
static uint8_t    g_gripperAngle   = GRIPPER_CLOSE_ANGLE;

// Non-blocking beep
static uint32_t   g_beepEndTime    = 0;        // When to stop the current beep
static bool       g_beepActive     = false;

// Color palette
#define COL_BG      0x0000
#define COL_PRIMARY 0x07FF   // Cyan
#define COL_ACCENT  0xF81F   // Magenta
#define COL_WARNING 0xFD20   // Orange
#define COL_DANGER  0xF800   // Red
#define COL_SUCCESS 0x07E0   // Green
#define COL_DIM     0x4208   // Dark gray
#define COL_TEXT    0xFFFF   // White
#define COL_GRID    0x2104   // Very dark gray

// ============================================================================
//  Forward Declarations
// ============================================================================
void roverMove(int vtx, int vty, int wt);
void roverWriteMotors(int8_t m1, int8_t m2, int8_t m3, int8_t m4);
void roverStop();
void gripperSetAngle(uint8_t angle);
void gripperOpen();
void gripperClose();
void roverForward(int speed);
void roverBackward(int speed);
void roverTurnLeft(int speed);
void roverTurnRight(int speed);
void roverStrafeLeft(int speed);
void roverStrafeRight(int speed);
bool scanI2CDevice(uint8_t addr);
void scanHardware();
void updateTOF();
void updateBattery();
void enterState(RoverState state);
void asrInit();
void asrRead();
void handleASRCommand(uint8_t cmdId);
void handleSerialConfig();
void drawUI();
void drawBootUI(int w, int h);
void drawScanUI(int w, int h);
void drawReadyUI(int w, int h);
void drawStatusBar(int w);
void beepWarning();
void beepConfirm();
void checkCollision();
void updateMotorRamp();
void updateBeep();
void roverMoveRamped(int vtx, int vty, int wt, int targetSpeed);

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    // Disable internal mic — G0 is shared with I2C SDA on HAT connector
    cfg.internal_mic = false;
    cfg.internal_spk = true;   // Enable speaker for beep warnings
    cfg.internal_imu = true;
    cfg.internal_rtc = true;
    M5.begin(cfg);

    Serial.println("\n[BOOT] === Voice Rover v1.0 ===");

    // Display
    M5.Display.setRotation(1);
    M5.Display.setBrightness(200);
    g_canvas.createSprite(M5.Display.width(), M5.Display.height());
    g_canvas.setTextDatum(MC_DATUM);

    // Speaker setup
    auto spk_cfg = M5.Speaker.config();
    M5.Speaker.config(spk_cfg);
    M5.Speaker.begin();
    M5.Speaker.setVolume(64);

    // RoverC Pro I2C initialization — direct register control (not using M5_RoverC library)
    Wire.begin(ROVER_SDA, ROVER_SCL);
    Wire.setClock(100000);  // 100kHz I2C
    delay(50);
    // Check if RoverC Pro responds
    Wire.beginTransmission(ROVER_ADDR);
    uint8_t i2cErr = Wire.endTransmission();
    g_hasRover = (i2cErr == 0);
    Serial.printf("[BOOT] RoverC Pro I2C (0x%02X): %s (err=%d)\n",
                  ROVER_ADDR, g_hasRover ? "OK" : "FAIL", i2cErr);
    if (g_hasRover) {
        // Stop all motors on boot
        roverWriteMotors(0, 0, 0, 0);
        // Initialize gripper to closed position (default)
        gripperClose();
    }

    // Start
    enterState(STATE_BOOT);
    Serial.println("[BOOT] Setup complete");
}

// ============================================================================
//  MAIN LOOP
// ============================================================================
void loop() {
    M5.update();
    uint32_t now = millis();

    // Battery (every 5s)
    if (now - g_lastBatCheck > 5000) {
        updateBattery();
        g_lastBatCheck = now;
    }

    // TOF polling (every 50ms)
    if (g_hasTOF && g_tofReady && now - g_lastTOFRead > 50) {
        updateTOF();
        g_lastTOFRead = now;
    }

    // ASR serial read (always)
    asrRead();

    // Non-blocking beep management
    updateBeep();

    // Motor soft-start ramp
    updateMotorRamp();

    // Serial config (commands)
    handleSerialConfig();

    // Collision check while moving forward
    if (g_state == STATE_READY && g_moveDir == DIR_FORWARD) {
        checkCollision();
    }

    // Movement duration check
    if (g_moveDuration > 0 && g_moveDir != DIR_STOP) {
        if (now - g_moveStartTime >= g_moveDuration) {
            roverStop();
            Serial.println("[MOV] Timed movement ended");
        }
    }

    // State machine
    switch (g_state) {
        case STATE_BOOT:
            if (now > 1500) {
                enterState(STATE_SCAN);
            }
            break;

        case STATE_SCAN: {
            static bool scanned = false;
            if (!scanned) {
                scanHardware();
                scanned = true;
                delay(800);
                // Play ready beep
                beepConfirm();
                enterState(STATE_READY);
            }
            break;
        }

        case STATE_READY:
            // BtnA: emergency stop
            if (M5.BtnA.wasPressed()) {
                roverStop();
                beepConfirm();
                Serial.println("[CMD] Button stop");
            }
            break;

        case STATE_SHUTDOWN:
            roverStop();
            M5.Display.fillScreen(0);
            M5.Display.setTextDatum(MC_DATUM);
            M5.Display.setTextSize(2);
            M5.Display.setTextColor(COL_DANGER);
            M5.Display.drawString("SHUTDOWN", M5.Display.width()/2, M5.Display.height()/2);
            delay(1000);
            M5.Power.deepSleep(0, true);
            break;
    }

    // UI update ~20fps
    if (now - g_lastUIUpdate >= 50) {
        drawUI();
        g_lastUIUpdate = now;
    }

    delay(5);
}

// ============================================================================
//  SERIAL CONFIG
// ============================================================================
void handleSerialConfig() {
    if (!Serial.available()) return;

    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line == "SCAN") {
        scanHardware();
    } else if (line == "REBOOT") {
        ESP.restart();
    } else if (line.startsWith("SPEED:")) {
        int spd = line.substring(6).toInt();
        g_motorSpeed = constrain(spd, 10, 100);
        Serial.printf("[CMD] Speed set to %d\n", g_motorSpeed);
    } else if (line == "STATUS") {
        Serial.printf("[STATUS] State=%d Dir=%d Speed=%d TOF=%dmm ASR=%d Bat=%.0f%%\n",
            g_state, g_moveDir, g_motorSpeed, g_tofDistance, g_hasASR, g_batPercent);
        Serial.printf("[STATUS] Heap: free=%d min=%d\n",
            ESP.getFreeHeap(), ESP.getMinFreeHeap());
    } else if (line == "STOP") {
        handleASRCommand(ASR_CMD_STOP);
    } else if (line == "FWD") {
        handleASRCommand(ASR_CMD_FORWARD);
    } else if (line == "BWD") {
        handleASRCommand(ASR_CMD_BACKWARD);
    } else if (line == "TL") {
        handleASRCommand(ASR_CMD_TURNLEFT);
    } else if (line == "TR") {
        handleASRCommand(ASR_CMD_TURNRIGHT);
    } else if (line == "SL") {
        handleASRCommand(ASR_CMD_LEFT);
    } else if (line == "SR") {
        handleASRCommand(ASR_CMD_RIGHT);
    } else if (line == "OPEN") {
        handleASRCommand(ASR_CMD_OPEN);
    } else if (line == "CLOSE") {
        handleASRCommand(ASR_CMD_CLOSE);
    }
}

// ============================================================================
//  UNIT ASR (CI-03T, UART)
//  Protocol: [0xAA] [0x55] [CMD_ID] [0x55] [0xAA] (5 bytes)
// ============================================================================

void asrInit() {
    Serial1.begin(ASR_UART_BAUD, SERIAL_8N1, ASR_UART_RX, ASR_UART_TX);
    delay(100);

    // Check if ASR module responds by draining any initial data
    Serial.println("[ASR] UART initialized on G33(RX from ASR)/G32(TX to ASR)");
    delay(200);

    // Drain any boot messages from ASR
    int avail = Serial1.available();
    if (avail > 0) {
        Serial.printf("[ASR] Received %d bytes during init\n", avail);
        while (Serial1.available()) Serial1.read();
        g_hasASR = true;
    } else {
        // ASR doesn't send anything on boot unless spoken to.
        // Mark as available — it's passive until voice command received.
        g_hasASR = true;
        Serial.println("[ASR] Module assumed present (passive until voice input)");
    }
}

void asrRead() {
    // Read UART data from ASR module
    // Protocol: [0xAA] [0x55] [CMD_ID] [0x55] [0xAA]  (5 bytes)
    // Or 6-byte: [0xAA] [0x55] [CMD_ID] [MSG] [0x55] [0xAA]
    static uint8_t asrBuf[6];
    static uint8_t asrBufIdx = 0;

    while (Serial1.available()) {
        uint8_t b = Serial1.read();
        g_asrRxCount++;

        // Append to raw hex display (keep last ~30 chars)
        char hexByte[4];
        snprintf(hexByte, sizeof(hexByte), "%02X ", b);
        g_asrRawHex += hexByte;
        if (g_asrRawHex.length() > 30) {
            g_asrRawHex = g_asrRawHex.substring(g_asrRawHex.length() - 30);
        }

        // State machine for 5-byte frame: AA 55 CMD 55 AA
        if (asrBufIdx == 0) {
            if (b == ASR_HEAD1) { asrBuf[0] = b; asrBufIdx = 1; }
        } else if (asrBufIdx == 1) {
            if (b == ASR_HEAD2) { asrBuf[1] = b; asrBufIdx = 2; }
            else if (b == ASR_HEAD1) { asrBuf[0] = b; asrBufIdx = 1; } // restart
            else { asrBufIdx = 0; }
        } else if (asrBufIdx == 2) {
            // Command ID byte
            asrBuf[2] = b;
            asrBufIdx = 3;
        } else if (asrBufIdx == 3) {
            asrBuf[3] = b;
            if (b == ASR_TAIL1) {
                // Expecting: next byte is 0xAA (standard 5-byte frame)
                asrBufIdx = 4;
            } else {
                // Could be 6-byte frame with MSG — keep going
                asrBufIdx = 4;
            }
        } else if (asrBufIdx == 4) {
            asrBuf[4] = b;
            // Check for valid 5-byte frame: AA 55 CMD 55 AA
            if (asrBuf[3] == ASR_TAIL1 && b == ASR_TAIL2) {
                uint8_t cmdId = asrBuf[2];
                g_asrFrameCount++;
                Serial.printf("[ASR] cmd 0x%02X\n", cmdId);
                handleASRCommand(cmdId);
                asrBufIdx = 0;
            }
            // Check for 6-byte frame: AA 55 CMD MSG 55 AA
            else if (b == ASR_TAIL1) {
                asrBufIdx = 5;
            } else {
                asrBufIdx = 0;
            }
        } else if (asrBufIdx == 5) {
            asrBuf[5] = b;
            if (b == ASR_TAIL2) {
                uint8_t cmdId = asrBuf[2];
                g_asrFrameCount++;
                Serial.printf("[ASR] cmd 0x%02X\n", cmdId);
                handleASRCommand(cmdId);
            }
            asrBufIdx = 0;
        }
    }
}

void handleASRCommand(uint8_t cmdId) {
    g_lastASRTime = millis();

    // Only process movement commands in READY state
    if (g_state != STATE_READY && cmdId != ASR_CMD_WAKEUP) {
        Serial.printf("[ASR] Ignoring cmd 0x%02X (not in READY state)\n", cmdId);
        return;
    }

    switch (cmdId) {
        case ASR_CMD_WAKEUP:
            // "Hi M Five" (0xFF)
            g_asrAwake = true;
            g_asrWakeTime = millis();
            g_lastASRCmd = "WAKE UP";
            beepConfirm();
            Serial.println("[ASR] Woke up! Listening for commands...");
            break;

        case ASR_CMD_FORWARD:
        case ASR_CMD_FRONT:
            // "forward" (0x07) or "front" (0x08)
            g_lastASRCmd = "FORWARD";
            if (g_hasTOF && g_tofDistance < TOF_STOP_DIST) {
                Serial.printf("[ASR] BLOCKED! TOF=%dmm, too close to move forward\n", g_tofDistance);
                g_lastASRCmd = "BLOCKED!";
                roverStop();
                beepWarning();
            } else {
                beepConfirm();
                g_moveDuration = 0;
                g_moveStartTime = millis();
                g_moveDir = DIR_FORWARD;
                roverMoveRamped(0, g_motorSpeed, 0, g_motorSpeed);
                Serial.printf("[CMD] Forward @%d\n", g_motorSpeed);
            }
            break;

        case ASR_CMD_BACKWARD:
        case ASR_CMD_BACK:
            // "backward" (0x09) or "back" (0x0A)
            g_lastASRCmd = "BACKWARD";
            beepConfirm();
            g_moveDuration = 0;
            g_moveStartTime = millis();
            g_moveDir = DIR_BACKWARD;
            roverMoveRamped(0, -g_motorSpeed, 0, g_motorSpeed);
            Serial.printf("[CMD] Backward @%d\n", g_motorSpeed);
            break;

        case ASR_CMD_TURNLEFT:
            // "turn left" (0x04) — continuous until stop, 2/3 speed
            g_lastASRCmd = "TURN LEFT";
            beepConfirm();
            g_moveDuration = 0;
            g_moveStartTime = millis();
            g_moveDir = DIR_TURN_LEFT;
            {
                int turnSpd = g_motorSpeed * 2 / 3;
                roverMoveRamped(0, 0, turnSpd, turnSpd);
                Serial.printf("[CMD] Turn left @%d\n", turnSpd);
            }
            break;

        case ASR_CMD_TURNRIGHT:
            // "turn right" (0x06) — continuous until stop, 2/3 speed
            g_lastASRCmd = "TURN RIGHT";
            beepConfirm();
            g_moveDuration = 0;
            g_moveStartTime = millis();
            g_moveDir = DIR_TURN_RIGHT;
            {
                int turnSpd = g_motorSpeed * 2 / 3;
                roverMoveRamped(0, 0, -turnSpd, turnSpd);
                Serial.printf("[CMD] Turn right @%d\n", turnSpd);
            }
            break;

        case ASR_CMD_LEFT:
            // "left" (0x03) — mecanum strafe, continuous until stop
            g_lastASRCmd = "LEFT";
            beepConfirm();
            g_moveDuration = 0;
            g_moveStartTime = millis();
            g_moveDir = DIR_LEFT;
            roverMoveRamped(-g_motorSpeed, 0, 0, g_motorSpeed);
            Serial.printf("[CMD] Strafe left @%d\n", g_motorSpeed);
            break;

        case ASR_CMD_RIGHT:
            // "right" (0x05) — mecanum strafe, continuous until stop
            g_lastASRCmd = "RIGHT";
            beepConfirm();
            g_moveDuration = 0;
            g_moveStartTime = millis();
            g_moveDir = DIR_RIGHT;
            roverMoveRamped(g_motorSpeed, 0, 0, g_motorSpeed);
            Serial.printf("[CMD] Strafe right @%d\n", g_motorSpeed);
            break;

        case ASR_CMD_STOP:
        case ASR_CMD_PAUSE:
            // "stop" (0x13) or "pause" (0x17)
            g_lastASRCmd = "STOP";
            beepConfirm();
            roverStop();
            Serial.println("[ASR] Stop");
            break;

        case ASR_CMD_UP:
            // "up" (0x01) — speed up
            g_motorSpeed = constrain(g_motorSpeed + 15, 10, 100);
            g_lastASRCmd = "SPD+" + String(g_motorSpeed);
            beepConfirm();
            Serial.printf("[ASR] Speed up: %d\n", g_motorSpeed);
            break;

        case ASR_CMD_DOWN:
            // "down" (0x02) — speed down
            g_motorSpeed = constrain(g_motorSpeed - 15, 10, 100);
            g_lastASRCmd = "SPD-" + String(g_motorSpeed);
            beepConfirm();
            Serial.printf("[ASR] Speed down: %d\n", g_motorSpeed);
            break;

        case ASR_CMD_TURNOFF:
            // "turn off" (0x15)
            g_lastASRCmd = "TURN OFF";
            Serial.println("[ASR] Shutdown requested");
            g_ramping = false;  // Cancel any ramp
            g_targetVtx = g_targetVty = g_targetWt = 0;
            roverStop();
            M5.Speaker.tone(440, 300);
            g_beepActive = true;
            g_beepEndTime = millis() + 350;
            enterState(STATE_SHUTDOWN);
            break;

        case ASR_CMD_OPEN:
            // "open" (0x10) — open gripper
            g_lastASRCmd = "GRIP OPEN";
            beepConfirm();
            gripperOpen();
            Serial.println("[ASR] Gripper open");
            break;

        case ASR_CMD_CLOSE:
            // "close" (0x11) — close gripper
            g_lastASRCmd = "GRIP CLOSE";
            beepConfirm();
            gripperClose();
            Serial.println("[ASR] Gripper close");
            break;

        default:
            {
                char cmdStr[10];
                snprintf(cmdStr, sizeof(cmdStr), "CMD:%02X", cmdId);
                g_lastASRCmd = cmdStr;
            }
            Serial.printf("[ASR] Unknown command: 0x%02X\n", cmdId);
            break;
    }
}

// ============================================================================
//  COLLISION AVOIDANCE
// ============================================================================
void checkCollision() {
    if (!g_hasTOF || !g_tofReady) return;
    if (g_moveDir != DIR_FORWARD) return;

    if (g_tofDistance < TOF_STOP_DIST) {
        // Emergency stop!
        roverStop();
        g_tofBlocked = true;
        g_lastASRCmd = "TOO CLOSE!";
        Serial.printf("[COL] Emergency stop! TOF=%dmm\n", g_tofDistance);
        beepWarning();
    } else if (g_tofDistance < TOF_WARN_DIST) {
        // Slow down proportionally
        g_ramping = false;  // Cancel ramp, take direct control
        int slowSpeed = map(g_tofDistance, TOF_STOP_DIST, TOF_WARN_DIST,
                           g_motorSpeed / 4, g_motorSpeed);
        slowSpeed = constrain(slowSpeed, 10, g_motorSpeed);
        g_targetVty = slowSpeed;
        g_targetVtx = 0;
        g_targetWt  = 0;
        roverMove(0, slowSpeed, 0);
        g_moveDir = DIR_FORWARD;  // Keep direction state
        g_tofBlocked = false;
    } else {
        // Clear ahead — resume full speed
        if (g_tofBlocked || g_targetVty < g_motorSpeed) {
            g_tofBlocked = false;
            g_targetVtx = 0;
            g_targetVty = g_motorSpeed;
            g_targetWt  = 0;
            g_currentVtx = 0;
            g_currentVty = g_targetVty;  // Resume immediately (no re-ramp)
            g_currentWt  = 0;
            roverMove(0, g_motorSpeed, 0);
            g_lastASRCmd = "FORWARD";
        }
    }
}

// ============================================================================
//  BEEP / SOUND (non-blocking)
// ============================================================================
void beepConfirm() {
    // Short high beep — command acknowledged (non-blocking)
    M5.Speaker.tone(1200, 80);
    g_beepActive = true;
    g_beepEndTime = millis() + 80;
}

void beepWarning() {
    // Warning beep (non-blocking, single short tone)
    uint32_t now = millis();
    if (now - g_lastBeepTime < 500) return;  // Don't spam beeps
    g_lastBeepTime = now;

    M5.Speaker.tone(2000, 120);
    g_beepActive = true;
    g_beepEndTime = now + 120;
}

void updateBeep() {
    // Stop speaker when beep duration is over
    if (g_beepActive && millis() >= g_beepEndTime) {
        M5.Speaker.stop();
        g_beepActive = false;
    }
}

// ============================================================================
//  MOTOR SOFT-START RAMP + PERIODIC REFRESH
// ============================================================================

// Ramp a single value toward its target by `step`
static int rampToward(int current, int target, int step) {
    if (current < target) return min(current + step, target);
    if (current > target) return max(current - step, target);
    return current;
}

void roverMoveRamped(int vtx, int vty, int wt, int /* unused */) {
    // Set target velocities — ramp will gradually reach them
    g_targetVtx = vtx;
    g_targetVty = vty;
    g_targetWt  = wt;

    // Start ramp from ~40% of target to avoid zero-crossing stall
    g_currentVtx = vtx * 4 / 10;
    g_currentVty = vty * 4 / 10;
    g_currentWt  = wt  * 4 / 10;

    g_lastRampTime = millis();
    g_ramping = true;

    // Immediately send initial low-speed command
    roverMove(g_currentVtx, g_currentVty, g_currentWt);
}

void updateMotorRamp() {
    uint32_t now = millis();

    if (g_ramping) {
        // Ramp up every 40ms, step = ~30% of typical speed
        if (now - g_lastRampTime >= 40) {
            g_lastRampTime = now;
            int step = 10;
            g_currentVtx = rampToward(g_currentVtx, g_targetVtx, step);
            g_currentVty = rampToward(g_currentVty, g_targetVty, step);
            g_currentWt  = rampToward(g_currentWt,  g_targetWt,  step);

            roverMove(g_currentVtx, g_currentVty, g_currentWt);

            // Check if ramp is complete
            if (g_currentVtx == g_targetVtx &&
                g_currentVty == g_targetVty &&
                g_currentWt  == g_targetWt) {
                g_ramping = false;
            }
        }
    }

    // Periodic motor refresh: re-send current speed every 200ms
    // This prevents drift from I2C glitches or motor controller resets
    if (g_moveDir != DIR_STOP && now - g_lastMotorRefresh >= 200) {
        g_lastMotorRefresh = now;
        if (!g_ramping) {
            roverMove(g_targetVtx, g_targetVty, g_targetWt);
        }
    }
}

// ============================================================================
//  HARDWARE SCANNING
// ============================================================================
void scanHardware() {
    Serial.println("\n[SCAN] === Hardware Scan ===");

    // Full I2C bus scan on Wire (G0/G26 HAT pins, initialized by roverc.begin())
    Serial.println("[SCAN] I2C bus scan on Wire (SDA=0, SCL=26):");
    int devCount = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            Serial.printf("[SCAN]   Found device at 0x%02X\n", addr);
            devCount++;
        }
    }
    Serial.printf("[SCAN]   Total on Wire: %d devices\n", devCount);

    // RoverC Pro (0x38)
    if (!g_hasRover) {
        g_hasRover = scanI2CDevice(ROVER_ADDR);
    }
    Serial.printf("[SCAN] RoverC Pro (0x%02X): %s\n",
        ROVER_ADDR, g_hasRover ? "FOUND" : "NOT FOUND");

    // TOF (0x29)
    g_hasTOF = scanI2CDevice(TOF_ADDR);
    Serial.printf("[SCAN] Unit TOF (0x%02X): %s\n",
        TOF_ADDR, g_hasTOF ? "FOUND" : "NOT FOUND");

    if (g_hasTOF) {
        g_tof.setBus(&Wire);
        g_tof.setAddress(TOF_ADDR);
        if (g_tof.init()) {
            g_tof.setTimeout(500);
            g_tof.setMeasurementTimingBudget(33000);
            g_tof.startContinuous(50);
            g_tofReady = true;
            Serial.println("[TOF] Initialized OK");
        } else {
            g_hasTOF = false;
            Serial.println("[TOF] Init FAILED");
        }
    }

    // ASR module (UART)
    asrInit();

    Serial.printf("[SCAN] Summary: Rover=%d TOF=%d ASR=%d\n",
        g_hasRover, g_hasTOF, g_hasASR);
}

bool scanI2CDevice(uint8_t addr) {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
}

// ============================================================================
//  ROVERC MOTOR CONTROL (registers 0x00-0x03)
// ============================================================================

// Direct I2C write to RoverC motor registers 0x00-0x03
// Each register takes int8_t (-100 to 100)
void roverWriteMotors(int8_t m1, int8_t m2, int8_t m3, int8_t m4) {
    Wire.beginTransmission(ROVER_ADDR);
    Wire.write(ROVERC_MOTOR_REG);  // Start at 0x00
    Wire.write((uint8_t)m1);
    Wire.write((uint8_t)m2);
    Wire.write((uint8_t)m3);
    Wire.write((uint8_t)m4);
    Wire.endTransmission();
}

// Mecanum wheel kinematics for RoverC Pro
// vtx = strafe (positive = right, negative = left)
// vty = forward/backward (positive = forward, negative = backward)
// wt  = rotation (positive = turn right, negative = turn left)
//
// Motor layout (top view, front = TOF sensor):
//   M1(FL)  M2(FR)
//   M3(RL)  M4(RR)
//
// Mecanum mixing:
//   FL = vty + vtx - wt
//   FR = vty - vtx + wt
//   RL = vty - vtx - wt
//   RR = vty + vtx + wt
void roverMove(int vtx, int vty, int wt) {
    if (!g_hasRover) {
        Serial.println("[MOT] SKIP — g_hasRover is false!");
        g_motorLastCmd = "NO ROVER!";
        return;
    }
    vtx = constrain(vtx, -100, 100);
    vty = constrain(vty, -100, 100);
    wt  = constrain(wt, -100, 100);

    // Reduce x/y when rotating (same as official library)
    if (wt != 0) {
        vtx = vtx * (100 - abs(wt)) / 100;
        vty = vty * (100 - abs(wt)) / 100;
    }

    int m1 = constrain(vty + vtx - wt, -100, 100);  // Front-left
    int m2 = constrain(vty - vtx + wt, -100, 100);  // Front-right
    int m3 = constrain(vty - vtx - wt, -100, 100);  // Rear-left
    int m4 = constrain(vty + vtx + wt, -100, 100);  // Rear-right

    char buf[40];
    snprintf(buf, sizeof(buf), "M:%d,%d,%d,%d", m1, m2, m3, m4);
    g_motorLastCmd = buf;

    roverWriteMotors((int8_t)m1, (int8_t)m2, (int8_t)m3, (int8_t)m4);
}

void roverStop() {
    g_moveDir = DIR_STOP;
    g_moveDuration = 0;
    g_ramping = false;
    g_targetVtx = g_targetVty = g_targetWt = 0;
    g_currentVtx = g_currentVty = g_currentWt = 0;
    roverMove(0, 0, 0);
}
void roverForward(int s)   { g_moveDir = DIR_FORWARD;    roverMove(0,  s, 0); }
void roverBackward(int s)  { g_moveDir = DIR_BACKWARD;   roverMove(0, -s, 0); }
void roverTurnLeft(int s)  { g_moveDir = DIR_TURN_LEFT;  roverMove(0, 0, -s); }
void roverTurnRight(int s) { g_moveDir = DIR_TURN_RIGHT; roverMove(0, 0,  s); }
void roverStrafeLeft(int s){ g_moveDir = DIR_LEFT;       roverMove(-s, 0, 0); }
void roverStrafeRight(int s){g_moveDir = DIR_RIGHT;      roverMove( s, 0, 0); }

// ============================================================================
//  GRIPPER SERVO CONTROL (register 0x10 + channel)
// ============================================================================

// Write angle to a servo channel
// Channels: 0-3, angle: 0-180 degrees
void gripperSetAngle(uint8_t angle) {
    if (!g_hasRover) return;
    uint8_t reg = ROVERC_SERVO_REG + GRIPPER_SERVO_CH;  // 0x10 + 0 = 0x10
    Wire.beginTransmission(ROVER_ADDR);
    Wire.write(reg);
    Wire.write(angle);
    Wire.endTransmission();
    g_gripperAngle = angle;
    Serial.printf("[GRIP] Servo ch%d -> angle %d (reg 0x%02X)\n",
                  GRIPPER_SERVO_CH, angle, reg);
}

void gripperOpen() {
    gripperSetAngle(GRIPPER_OPEN_ANGLE);
    g_gripperOpen = true;
    Serial.println("[GRIP] OPEN");
}

void gripperClose() {
    gripperSetAngle(GRIPPER_CLOSE_ANGLE);
    g_gripperOpen = false;
    Serial.println("[GRIP] CLOSE");
}

// ============================================================================
//  TOF
// ============================================================================
void updateTOF() {
    uint16_t dist = g_tof.readRangeContinuousMillimeters();
    if (!g_tof.timeoutOccurred()) {
        g_tofDistance = dist;
    }
}

// ============================================================================
//  BATTERY
// ============================================================================
void updateBattery() {
    g_batVoltage = M5.Power.getBatteryVoltage() / 1000.0f;
    g_batPercent = constrain((g_batVoltage - 3.0f) / 1.2f * 100.0f, 0, 100);

    if (g_batPercent < 15 && !g_lowBatWarned) {
        g_lowBatWarned = true;
        Serial.println("[BAT] LOW BATTERY!");
    }
    if (g_batPercent > 25) g_lowBatWarned = false;
}

// ============================================================================
//  STATE MANAGEMENT
// ============================================================================
void enterState(RoverState newState) {
    RoverState prev = g_state;
    g_state = newState;

    const char* names[] = { "BOOT", "SCAN", "READY", "SHUTDOWN" };
    Serial.printf("[STATE] %s -> %s\n", names[prev], names[newState]);

    switch (newState) {
        case STATE_READY:
            roverStop();
            break;
        default:
            break;
    }
}

// ============================================================================
//  UI DRAWING (StickC Plus: 240x135 landscape)
// ============================================================================
void drawUI() {
    int w = g_canvas.width();
    int h = g_canvas.height();
    g_canvas.fillScreen(COL_BG);

    // Subtle scanlines
    for (int y = 0; y < h; y += 6) g_canvas.drawFastHLine(0, y, w, COL_GRID);

    switch (g_state) {
        case STATE_BOOT:  drawBootUI(w, h);  break;
        case STATE_SCAN:  drawScanUI(w, h);  break;
        case STATE_READY: drawReadyUI(w, h); break;
        default: break;
    }

    g_canvas.pushSprite(0, 0);
}

void drawBootUI(int w, int h) {
    g_canvas.setTextColor(COL_PRIMARY);
    g_canvas.setTextSize(2);
    g_canvas.drawString("VOICE ROVER", w/2, h/3);
    g_canvas.setTextSize(1);
    g_canvas.setTextColor(COL_ACCENT);
    g_canvas.drawString("v1.0", w/2, h/3 + 22);
    g_canvas.setTextColor(COL_DIM);
    g_canvas.drawString("Voice Controlled", w/2, h*2/3);
}

void drawScanUI(int w, int h) {
    g_canvas.setTextColor(COL_PRIMARY);
    g_canvas.setTextSize(1.5);
    g_canvas.drawString("SCANNING...", w/2, 15);

    int y = 40;
    auto item = [&](const char* name, bool found) {
        g_canvas.setTextDatum(ML_DATUM);
        g_canvas.setTextColor(COL_TEXT);
        g_canvas.setTextSize(1);
        g_canvas.drawString(name, 15, y);
        g_canvas.setTextDatum(MR_DATUM);
        g_canvas.setTextColor(found ? COL_SUCCESS : COL_DIM);
        g_canvas.drawString(found ? "[OK]" : "[--]", w-15, y);
        g_canvas.setTextDatum(MC_DATUM);
        y += 22;
    };

    item("RoverC Pro", g_hasRover);
    item("Unit TOF",   g_hasTOF);
    item("Unit ASR",   g_hasASR);
}

void drawReadyUI(int w, int h) {
    // Status bar
    drawStatusBar(w);

    // === ROW 1 (y=18): ASR command — BIG and prominent ===
    g_canvas.setTextDatum(TL_DATUM);
    g_canvas.setTextSize(1);
    g_canvas.setTextColor(COL_PRIMARY);
    g_canvas.drawString("ASR:", 4, 18);

    if (g_lastASRCmd.length() > 0) {
        uint32_t cmdAge = (millis() - g_lastASRTime) / 1000;
        g_canvas.setTextSize(2);
        if (cmdAge < 3) {
            if (g_lastASRCmd == "TOO CLOSE!" || g_lastASRCmd == "BLOCKED!") {
                g_canvas.setTextColor(COL_DANGER);
            } else {
                g_canvas.setTextColor(COL_ACCENT);
            }
        } else {
            g_canvas.setTextColor(COL_DIM);
        }
        g_canvas.drawString(g_lastASRCmd.c_str(), 36, 14);
    } else {
        g_canvas.setTextSize(1.2);
        g_canvas.setTextColor(COL_DIM);
        g_canvas.drawString("(none)", 36, 18);
    }

    // === ROW 2 (y=36): Raw UART hex bytes ===
    g_canvas.setTextSize(1);
    g_canvas.setTextColor(COL_DIM);
    g_canvas.drawString("RX:", 4, 36);
    if (g_asrRawHex.length() > 0) {
        g_canvas.setTextColor(COL_WARNING);
        g_canvas.drawString(g_asrRawHex.c_str(), 26, 36);
    } else {
        g_canvas.setTextColor(COL_DIM);
        g_canvas.drawString("(waiting)", 26, 36);
    }

    // === ROW 3 (y=48): Counters — bytes received / frames parsed ===
    char cntBuf[40];
    snprintf(cntBuf, sizeof(cntBuf), "B:%d F:%d", g_asrRxCount, g_asrFrameCount);
    g_canvas.setTextColor(COL_DIM);
    g_canvas.drawString(cntBuf, 4, 48);

    // Direction + Speed
    const char* dirNames[] = {"STOP", "FWD", "BWD", "LEFT", "RIGHT", "T-L", "T-R"};
    uint16_t dirCol = g_moveDir == DIR_STOP ? COL_DIM :
                      g_moveDir == DIR_FORWARD ? COL_SUCCESS :
                      g_moveDir == DIR_BACKWARD ? COL_WARNING : COL_ACCENT;
    g_canvas.setTextColor(COL_PRIMARY);
    g_canvas.drawString("DIR:", w/2, 48);
    g_canvas.setTextColor(dirCol);
    char dirSpd[20];
    snprintf(dirSpd, sizeof(dirSpd), "%s @%d", dirNames[g_moveDir], g_motorSpeed);
    g_canvas.drawString(dirSpd, w/2 + 28, 48);

    // === ROW 4 (y=62): Motor I2C command sent ===
    g_canvas.setTextColor(COL_DIM);
    g_canvas.drawString("MOT:", 4, 62);
    if (g_motorLastCmd.length() > 0) {
        g_canvas.setTextColor(COL_SUCCESS);
        g_canvas.drawString(g_motorLastCmd.c_str(), 32, 62);
    }

    // === ROW 5 (y=76): TOF distance ===
    g_canvas.setTextColor(COL_DIM);
    g_canvas.drawString("TOF:", 4, 76);
    if (g_hasTOF) {
        uint16_t col = g_tofDistance < TOF_STOP_DIST ? COL_DANGER :
                       g_tofDistance < TOF_WARN_DIST ? COL_WARNING : COL_SUCCESS;
        g_canvas.setTextColor(col);
        char tofBuf[16];
        snprintf(tofBuf, sizeof(tofBuf), "%dmm", min((uint16_t)9999, g_tofDistance));
        g_canvas.drawString(tofBuf, 32, 76);

        // TOF bar
        int barX = 80, barW = w - barX - 4;
        uint16_t dist = constrain(g_tofDistance, 0, 2000);
        float ratio = dist / 2000.0f;
        g_canvas.drawRect(barX, 76, barW, 8, COL_DIM);
        g_canvas.fillRect(barX + 1, 77, (int)(ratio * (barW - 2)), 6, col);
    } else {
        g_canvas.setTextColor(COL_DANGER);
        g_canvas.drawString("N/A", 32, 76);
    }

    // === ROW 6 (y=92): Hardware status compact + Gripper ===
    auto drawHw = [&](int x, const char* label, bool ok) {
        g_canvas.setTextColor(ok ? COL_SUCCESS : COL_DANGER);
        g_canvas.drawString(ok ? "+" : "-", x, 92);
        g_canvas.setTextColor(COL_TEXT);
        g_canvas.drawString(label, x + 8, 92);
    };
    drawHw(4, "Rover", g_hasRover);
    drawHw(60, "TOF", g_hasTOF);
    drawHw(104, "ASR", g_hasASR);

    // Gripper state
    g_canvas.setTextColor(g_gripperOpen ? COL_SUCCESS : COL_ACCENT);
    g_canvas.drawString(g_gripperOpen ? "[OPEN]" : "[GRIP]", w - 40, 92);

    // === Bottom (y=h-16): Prompt ===
    g_canvas.setTextDatum(MC_DATUM);
    if (g_tofDistance < TOF_STOP_DIST && g_hasTOF) {
        g_canvas.setTextColor(COL_DANGER);
        if ((millis() / 300) % 2 == 0)
            g_canvas.drawString("!! TOO CLOSE !!", w/2, h - 10);
    } else if (g_asrRxCount == 0) {
        g_canvas.setTextSize(1);
        g_canvas.setTextColor(COL_DIM);
        g_canvas.drawString("Say \"Hi M Five\" to wake ASR", w/2, h - 10);
    }
}

void drawStatusBar(int w) {
    g_canvas.setTextSize(1);

    // Battery (top right)
    g_canvas.setTextDatum(MR_DATUM);
    uint16_t batCol = g_batPercent > 25 ? COL_SUCCESS : g_batPercent > 10 ? COL_WARNING : COL_DANGER;
    char batStr[8]; snprintf(batStr, sizeof(batStr), "%.0f%%", g_batPercent);
    g_canvas.setTextColor(batCol);
    g_canvas.drawString(batStr, w-3, 7);

    // Title (top left)
    g_canvas.setTextDatum(ML_DATUM);
    g_canvas.setTextColor(COL_PRIMARY);
    g_canvas.drawString("VOICE", 3, 7);

    g_canvas.setTextDatum(MC_DATUM);
}
