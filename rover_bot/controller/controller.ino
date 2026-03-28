/*
 * ============================================================================
 *  controller.ino — "小虫" (XiaoChong) Remote Controller
 * ============================================================================
 *  Runs on M5Stack Core2 (320x240 touch, speaker, mic).
 *  Communicates with rover (M5StickC Plus) via ESP-NOW.
 *
 *  UI Screens:
 *    - PAIR:    Scan & pair with rover
 *    - HOME:    Mode selection (Manual / Bat / Free)
 *    - MANUAL:  D-pad controls (car-like: forward, backward, turn L/R)
 *    - BAT:     Bat mode monitor (TOF distance display)
 *    - FREE:    Free explore (camera feed + TOF)
 *
 *  Core2 specs:
 *    - 320x240 IPS touch (FT6336U capacitive)
 *    - ESP32-D0WDQ6, 16MB Flash, 8MB PSRAM
 *    - Speaker NS4168 (I2S), Mic SPM1423
 *    - 500mAh LiPo, AXP192 PMU
 *
 *  Dependencies: M5Unified, M5GFX, esp_now, esp_wifi
 *
 *  Author: sindney (m5stack_toys)
 *  License: MIT
 * ============================================================================
 */

#include <M5Unified.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>

// ============================================================================
//  ESP-NOW Protocol (same definitions as rover — keep in sync!)
// ============================================================================

#define ESPNOW_CHANNEL  1
#define ROVER_PREFIX    "XC-R_"
#define CTRL_PREFIX     "XC-C_"

enum MsgType : uint8_t {
    MSG_BEACON       = 0x01,
    MSG_PAIR_REQ     = 0x02,
    MSG_PAIR_ACK     = 0x03,
    MSG_TELEMETRY    = 0x10,
    MSG_CMD_MODE     = 0x20,
    MSG_CMD_MOVE     = 0x21,
    MSG_CMD_SPEED    = 0x22,
    MSG_CAM_CHUNK    = 0x30,
    MSG_CAM_START    = 0x31,
    MSG_CAM_END      = 0x32,
};

enum RoverMode : uint8_t {
    MODE_BOOT       = 0,
    MODE_WAIT_PAIR  = 1,
    MODE_IDLE       = 2,
    MODE_MANUAL     = 3,
    MODE_BAT        = 4,
    MODE_FREE       = 5,
    MODE_SCAN       = 6,
    MODE_SHUTDOWN   = 7,
};

enum MoveDir : uint8_t {
    DIR_STOP = 0, DIR_FORWARD, DIR_BACKWARD, DIR_LEFT, DIR_RIGHT,
    DIR_TURN_LEFT, DIR_TURN_RIGHT,
};

// Message structs — must match rover exactly
struct __attribute__((packed)) MsgBeacon {
    uint8_t type; char name[16];
    uint8_t hasRover, hasTOF, hasCamS3, hasASR, mode, batPercent;
};

struct __attribute__((packed)) MsgPairReq {
    uint8_t type; char name[16]; uint8_t mac[6];
};

struct __attribute__((packed)) MsgPairAck {
    uint8_t type; char name[16]; uint8_t mac[6];
    uint8_t hasRover, hasTOF, hasCamS3, hasASR;
};

struct __attribute__((packed)) MsgTelemetry {
    uint8_t type, mode, moveDir;
    uint16_t tofDist;
    uint8_t batPercent;
    float batVoltage;
    int8_t motorSpeed;
    uint8_t camConnected;
};

struct __attribute__((packed)) MsgCmdMode {
    uint8_t type, mode;
};

struct __attribute__((packed)) MsgCmdMove {
    uint8_t type, dir; int8_t speed;
};

struct __attribute__((packed)) MsgCamChunk {
    uint8_t type;
    uint16_t frameId, chunkIdx, totalChunks;
    uint8_t data[200];
    uint16_t dataLen;
};

// ============================================================================
//  Controller UI Screens
// ============================================================================
enum CtrlScreen : uint8_t {
    SCR_PAIR,       // Scanning / pairing
    SCR_HOME,       // Mode selection
    SCR_MANUAL,     // Manual drive (d-pad)
    SCR_BAT,        // Bat mode monitor
    SCR_FREE,       // Free explore + camera
};

// ============================================================================
//  Color Palette (dark cyberpunk)
// ============================================================================
#define COL_BG      0x0000
#define COL_PRIMARY 0x07FF  // Cyan
#define COL_ACCENT  0xF81F  // Magenta
#define COL_WARNING 0xFD20  // Orange
#define COL_DANGER  0xF800  // Red
#define COL_SUCCESS 0x07E0  // Green
#define COL_DIM     0x4208
#define COL_TEXT    0xFFFF
#define COL_GRID    0x2104
#define COL_BTN_BG  0x18E3  // Dark blue-gray
#define COL_BTN_ACT 0x03EF  // Active button (teal)

// ============================================================================
//  Globals
// ============================================================================

// Controller identity
static char       g_ctrlName[16] = {0};

// Screen state
static CtrlScreen g_screen       = SCR_PAIR;
static M5Canvas   g_canvas(&M5.Display);
static uint32_t   g_lastUIUpdate = 0;

// Pairing
static bool       g_paired       = false;
static uint8_t    g_roverMAC[6]  = {0};
static char       g_roverName[16] = {0};

// Rover capabilities (from beacon/pair ack)
static bool       g_roverHasRover = false;
static bool       g_roverHasTOF   = false;
static bool       g_roverHasCamS3 = false;
static bool       g_roverHasASR   = false;

// Telemetry (updated from rover)
static uint8_t    g_roverMode     = MODE_IDLE;
static uint8_t    g_roverMoveDir  = DIR_STOP;
static uint16_t   g_roverTOF      = 9999;
static uint8_t    g_roverBat      = 0;
static float      g_roverBatV     = 0;
static int8_t     g_roverSpeed    = 60;
static bool       g_roverCamConn  = false;
static uint32_t   g_lastTelemetry = 0;

// Beacon tracking
struct RoverInfo {
    char     name[16];
    uint8_t  mac[6];
    uint8_t  hasRover, hasTOF, hasCamS3, hasASR;
    uint8_t  batPercent;
    uint32_t lastSeen;
};
static RoverInfo  g_discoveredRovers[8];
static int        g_numDiscovered = 0;

// Camera frame buffer (PSRAM)
static uint8_t*   g_camBuf       = nullptr;
static size_t     g_camBufSize   = 0;
static size_t     g_camBufLen    = 0;
static uint16_t   g_camFrameId   = 0;
static bool       g_camFrameReady = false;
static uint16_t   g_camRecvChunks = 0;
static uint16_t   g_camTotalChunks = 0;

// Touch state
static bool       g_touchActive   = false;
static int        g_touchX        = 0;
static int        g_touchY        = 0;

// Sending state (for continuous hold)
static uint32_t   g_lastMoveCmd   = 0;
static MoveDir    g_activeMoveDir = DIR_STOP;

// ============================================================================
//  Forward Declarations
// ============================================================================
void espnowInit();
void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len);
void onEspNowSent(const uint8_t *mac, esp_now_send_status_t status);
void sendPairRequest(uint8_t* roverMac);
void sendModeCmd(uint8_t mode);
void sendMoveCmd(MoveDir dir, int8_t speed = 0);
void drawPairScreen();
void drawHomeScreen();
void drawManualScreen();
void drawBatScreen();
void drawFreeScreen();
void drawSonarViz(int cx, int cy);
void handleTouch();
void handlePairTouch(int tx, int ty);
void handleHomeTouch(int tx, int ty);
void handleManualTouch(int tx, int ty);
void handleMonitorTouch(int tx, int ty);
void checkTouchRelease();
bool touchInRect(int x, int y, int w, int h);

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);

    Serial.println("\n[CTRL] === XiaoChong Controller v2.0 ===");

    M5.Display.setRotation(1);  // Landscape 320x240
    M5.Display.setBrightness(100);

    g_canvas.createSprite(320, 240);
    g_canvas.setTextDatum(MC_DATUM);

    // Allocate camera buffer in PSRAM
    g_camBufSize = 40000;
    g_camBuf = (uint8_t*)ps_malloc(g_camBufSize);
    if (g_camBuf) {
        Serial.println("[CTRL] Camera buffer allocated (PSRAM)");
    } else {
        Serial.println("[CTRL] WARNING: No PSRAM for camera buffer!");
        g_camBufSize = 0;
    }

    // Generate random name: XC-C_XXX
    randomSeed(esp_random());
    uint16_t rnd = random(0x100, 0xFFF);
    snprintf(g_ctrlName, sizeof(g_ctrlName), "%s%03X", CTRL_PREFIX, rnd);
    Serial.printf("[CTRL] Name: %s\n", g_ctrlName);

    espnowInit();

    g_screen = SCR_PAIR;
    Serial.println("[CTRL] Setup complete, scanning for rovers...");
}

// ============================================================================
//  MAIN LOOP
// ============================================================================
void loop() {
    M5.update();
    uint32_t now = millis();

    // Touch handling
    handleTouch();

    // Continuous move command sending when button held (every 150ms)
    if (g_screen == SCR_MANUAL && g_touchActive && g_activeMoveDir != DIR_STOP) {
        if (now - g_lastMoveCmd > 150) {
            sendMoveCmd(g_activeMoveDir);
            g_lastMoveCmd = now;
        }
    }

    // Connection timeout check
    if (g_paired && now - g_lastTelemetry > 5000) {
        Serial.println("[CTRL] Rover connection lost!");
        g_paired = false;
        g_screen = SCR_PAIR;
        g_numDiscovered = 0;
    }

    // UI update ~30fps
    if (now - g_lastUIUpdate >= 33) {
        switch (g_screen) {
            case SCR_PAIR:   drawPairScreen();   break;
            case SCR_HOME:   drawHomeScreen();   break;
            case SCR_MANUAL: drawManualScreen(); break;
            case SCR_BAT:    drawBatScreen();    break;
            case SCR_FREE:   drawFreeScreen();   break;
        }
        g_canvas.pushSprite(0, 0);
        g_lastUIUpdate = now;
    }

    delay(5);
}

// ============================================================================
//  TOUCH HANDLING
// ============================================================================
void handleTouch() {
    auto touch = M5.Touch.getDetail();
    g_touchActive = touch.isPressed();

    if (g_touchActive) {
        g_touchX = touch.x;
        g_touchY = touch.y;
    }

    if (!touch.wasPressed()) return;  // Only act on new press

    int tx = touch.x;
    int ty = touch.y;

    switch (g_screen) {
        case SCR_PAIR:
            handlePairTouch(tx, ty);
            break;
        case SCR_HOME:
            handleHomeTouch(tx, ty);
            break;
        case SCR_MANUAL:
            handleManualTouch(tx, ty);
            break;
        case SCR_BAT:
        case SCR_FREE:
            handleMonitorTouch(tx, ty);
            break;
    }
}

bool touchInRect(int x, int y, int w, int h) {
    return (g_touchX >= x && g_touchX < x+w && g_touchY >= y && g_touchY < y+h);
}

void handlePairTouch(int tx, int ty) {
    // Touch on a discovered rover to pair
    for (int i = 0; i < g_numDiscovered; i++) {
        int itemY = 70 + i * 45;
        if (ty >= itemY && ty < itemY + 40 && tx >= 20 && tx < 300) {
            Serial.printf("[CTRL] Selected rover: %s\n", g_discoveredRovers[i].name);
            sendPairRequest(g_discoveredRovers[i].mac);
            break;
        }
    }
}

void handleHomeTouch(int tx, int ty) {
    // Mode buttons: Manual(40,90,240,40), Bat(40,140,240,40), Free(40,190,240,40)
    if (ty >= 80 && ty < 120) {
        sendModeCmd(MODE_MANUAL);
        g_screen = SCR_MANUAL;
    } else if (ty >= 130 && ty < 170) {
        if (g_roverHasTOF) {
            sendModeCmd(MODE_BAT);
            g_screen = SCR_BAT;
        }
    } else if (ty >= 180 && ty < 220) {
        sendModeCmd(MODE_FREE);
        g_screen = SCR_FREE;
    }
}

void handleManualTouch(int tx, int ty) {
    // Back button (top-left corner)
    if (tx < 60 && ty < 40) {
        sendMoveCmd(DIR_STOP);
        sendModeCmd(MODE_IDLE);
        g_screen = SCR_HOME;
        g_activeMoveDir = DIR_STOP;
        return;
    }

    // D-pad layout (car-like):
    //        [FORWARD]
    //  [LEFT]  [STOP]  [RIGHT]
    //       [BACKWARD]
    int cx = 160, cy = 155;
    int btnW = 80, btnH = 50;
    int gap = 5;

    // Forward: center top
    if (tx >= cx-btnW/2 && tx < cx+btnW/2 && ty >= cy-btnH-gap-btnH/2 && ty < cy-gap-btnH/2) {
        g_activeMoveDir = DIR_FORWARD;
        sendMoveCmd(DIR_FORWARD);
    }
    // Backward: center bottom
    else if (tx >= cx-btnW/2 && tx < cx+btnW/2 && ty >= cy+gap+btnH/2 && ty < cy+gap+btnH/2+btnH) {
        g_activeMoveDir = DIR_BACKWARD;
        sendMoveCmd(DIR_BACKWARD);
    }
    // Left: center left
    else if (tx >= cx-btnW-gap-btnW/2 && tx < cx-gap-btnW/2 && ty >= cy-btnH/2 && ty < cy+btnH/2) {
        g_activeMoveDir = DIR_LEFT;
        sendMoveCmd(DIR_LEFT);
    }
    // Right: center right
    else if (tx >= cx+gap+btnW/2 && tx < cx+gap+btnW/2+btnW && ty >= cy-btnH/2 && ty < cy+btnH/2) {
        g_activeMoveDir = DIR_RIGHT;
        sendMoveCmd(DIR_RIGHT);
    }
    // Center: STOP
    else if (tx >= cx-btnW/2 && tx < cx+btnW/2 && ty >= cy-btnH/2 && ty < cy+btnH/2) {
        g_activeMoveDir = DIR_STOP;
        sendMoveCmd(DIR_STOP);
    }
}

void handleMonitorTouch(int tx, int ty) {
    // Back button (top-left)
    if (tx < 60 && ty < 40) {
        sendModeCmd(MODE_IDLE);
        g_screen = SCR_HOME;
    }
}

// Touch release handler — stop movement
void checkTouchRelease() {
    if (!g_touchActive && g_activeMoveDir != DIR_STOP && g_screen == SCR_MANUAL) {
        g_activeMoveDir = DIR_STOP;
        sendMoveCmd(DIR_STOP);
    }
}

// ============================================================================
//  ESP-NOW
// ============================================================================
void espnowInit() {
    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    Serial.printf("[NOW] MAC: %s\n", WiFi.macAddress().c_str());

    if (esp_now_init() != ESP_OK) {
        Serial.println("[NOW] Init FAILED!");
        return;
    }

    esp_now_register_recv_cb(onEspNowRecv);
    esp_now_register_send_cb(onEspNowSent);

    // Add broadcast peer
    esp_now_peer_info_t bcast = {};
    memset(bcast.peer_addr, 0xFF, 6);
    bcast.channel = ESPNOW_CHANNEL;
    bcast.ifidx   = WIFI_IF_STA;
    bcast.encrypt = false;
    esp_now_add_peer(&bcast);

    Serial.println("[NOW] ESP-NOW initialized");
}

void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len < 1) return;
    const uint8_t *mac = info->src_addr;
    uint8_t msgType = data[0];

    switch (msgType) {
        case MSG_BEACON: {
            if (len < (int)sizeof(MsgBeacon)) return;
            const MsgBeacon* b = (const MsgBeacon*)data;

            // Only accept rovers
            if (strncmp(b->name, ROVER_PREFIX, strlen(ROVER_PREFIX)) != 0) return;

            // Update or add to discovered list
            bool found = false;
            for (int i = 0; i < g_numDiscovered; i++) {
                if (memcmp(g_discoveredRovers[i].mac, mac, 6) == 0) {
                    g_discoveredRovers[i].lastSeen = millis();
                    g_discoveredRovers[i].batPercent = b->batPercent;
                    found = true;
                    break;
                }
            }
            if (!found && g_numDiscovered < 8) {
                RoverInfo& r = g_discoveredRovers[g_numDiscovered];
                strncpy(r.name, b->name, sizeof(r.name));
                memcpy(r.mac, mac, 6);
                r.hasRover   = b->hasRover;
                r.hasTOF     = b->hasTOF;
                r.hasCamS3   = b->hasCamS3;
                r.hasASR     = b->hasASR;
                r.batPercent = b->batPercent;
                r.lastSeen   = millis();
                g_numDiscovered++;
                Serial.printf("[NOW] Discovered rover: %s\n", b->name);
            }
            break;
        }

        case MSG_PAIR_ACK: {
            if (len < (int)sizeof(MsgPairAck)) return;
            const MsgPairAck* ack = (const MsgPairAck*)data;

            memcpy(g_roverMAC, ack->mac, 6);
            strncpy(g_roverName, ack->name, sizeof(g_roverName));
            g_roverHasRover = ack->hasRover;
            g_roverHasTOF   = ack->hasTOF;
            g_roverHasCamS3 = ack->hasCamS3;
            g_roverHasASR   = ack->hasASR;
            g_paired = true;
            g_lastTelemetry = millis();

            Serial.printf("[NOW] Paired with %s! HW: R=%d T=%d C=%d A=%d\n",
                g_roverName, g_roverHasRover, g_roverHasTOF, g_roverHasCamS3, g_roverHasASR);

            // Switch to home screen
            g_screen = SCR_HOME;
            break;
        }

        case MSG_TELEMETRY: {
            if (len < (int)sizeof(MsgTelemetry)) return;
            const MsgTelemetry* tel = (const MsgTelemetry*)data;

            g_roverMode    = tel->mode;
            g_roverMoveDir = tel->moveDir;
            g_roverTOF     = tel->tofDist;
            g_roverBat     = tel->batPercent;
            g_roverBatV    = tel->batVoltage;
            g_roverSpeed   = tel->motorSpeed;
            g_roverCamConn = tel->camConnected;
            g_lastTelemetry = millis();
            break;
        }

        case MSG_CAM_START: {
            if (len < (int)sizeof(MsgCamChunk)) return;
            const MsgCamChunk* msg = (const MsgCamChunk*)data;
            g_camFrameId = msg->frameId;
            g_camBufLen = 0;
            g_camRecvChunks = 0;
            g_camTotalChunks = msg->totalChunks;
            g_camFrameReady = false;
            // dataLen in START msg = total JPEG size
            break;
        }

        case MSG_CAM_CHUNK: {
            if (len < (int)sizeof(MsgCamChunk)) return;
            const MsgCamChunk* msg = (const MsgCamChunk*)data;
            if (msg->frameId != g_camFrameId) return;
            if (!g_camBuf) return;

            // Copy chunk data to buffer (in order)
            size_t offset = (size_t)msg->chunkIdx * 200;
            if (offset + msg->dataLen <= g_camBufSize) {
                memcpy(g_camBuf + offset, msg->data, msg->dataLen);
                g_camBufLen = offset + msg->dataLen;
                g_camRecvChunks++;
            }
            break;
        }

        case MSG_CAM_END: {
            if (len < (int)sizeof(MsgCamChunk)) return;
            const MsgCamChunk* msg = (const MsgCamChunk*)data;
            if (msg->frameId == g_camFrameId && g_camRecvChunks >= g_camTotalChunks * 0.8) {
                g_camFrameReady = true;
                // Allow display even if some chunks lost
            }
            break;
        }
    }
}

void onEspNowSent(const uint8_t *mac, esp_now_send_status_t status) {
    // Track delivery status if needed
}

void sendPairRequest(uint8_t* roverMac) {
    // Add rover as peer first
    if (!esp_now_is_peer_exist(roverMac)) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, roverMac, 6);
        peer.channel = ESPNOW_CHANNEL;
        peer.ifidx   = WIFI_IF_STA;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    MsgPairReq req = {};
    req.type = MSG_PAIR_REQ;
    strncpy(req.name, g_ctrlName, sizeof(req.name));
    uint8_t myMAC[6];
    esp_wifi_get_mac(WIFI_IF_STA, myMAC);
    memcpy(req.mac, myMAC, 6);

    esp_now_send(roverMac, (uint8_t*)&req, sizeof(req));
    Serial.println("[NOW] Pair request sent");
}

void sendModeCmd(uint8_t mode) {
    if (!g_paired) return;
    MsgCmdMode cmd = {MSG_CMD_MODE, mode};
    esp_now_send(g_roverMAC, (uint8_t*)&cmd, sizeof(cmd));
    Serial.printf("[CMD] Mode -> %d\n", mode);
}

void sendMoveCmd(MoveDir dir, int8_t speed) {
    if (!g_paired) return;
    MsgCmdMove cmd = {MSG_CMD_MOVE, (uint8_t)dir, speed};
    esp_now_send(g_roverMAC, (uint8_t*)&cmd, sizeof(cmd));
}

// ============================================================================
//  UI DRAWING — Core2 (320x240 touch)
// ============================================================================

// Helper: draw rounded button
void drawBtn(int x, int y, int w, int h, const char* label,
             uint16_t bgCol, uint16_t textCol, bool pressed = false) {
    uint16_t bg = pressed ? COL_BTN_ACT : bgCol;
    g_canvas.fillRoundRect(x, y, w, h, 8, bg);
    g_canvas.drawRoundRect(x, y, w, h, 8, textCol);
    g_canvas.setTextColor(textCol);
    g_canvas.setTextDatum(MC_DATUM);
    g_canvas.drawString(label, x + w/2, y + h/2);
}

// Helper: draw back button
void drawBackBtn() {
    g_canvas.fillRoundRect(5, 5, 50, 30, 6, COL_BTN_BG);
    g_canvas.drawRoundRect(5, 5, 50, 30, 6, COL_DIM);
    g_canvas.setTextColor(COL_TEXT);
    g_canvas.setTextSize(1);
    g_canvas.setTextDatum(MC_DATUM);
    g_canvas.drawString("<Back", 30, 20);
}

// Helper: draw status bar at top
void drawStatusInfo(int y) {
    g_canvas.setTextSize(1);
    g_canvas.setTextDatum(MR_DATUM);

    // Rover battery
    uint16_t batCol = g_roverBat > 25 ? COL_SUCCESS : g_roverBat > 10 ? COL_WARNING : COL_DANGER;
    char batStr[16];
    snprintf(batStr, sizeof(batStr), "Rover:%d%%", g_roverBat);
    g_canvas.setTextColor(batCol);
    g_canvas.drawString(batStr, 315, y);

    // Connection indicator
    g_canvas.setTextDatum(ML_DATUM);
    g_canvas.setTextColor(g_paired ? COL_SUCCESS : COL_DANGER);
    g_canvas.drawString(g_paired ? "LINKED" : "DISCONNECTED", 65, y);
}

// Helper: draw TOF distance display
void drawTOFDisplay(int x, int y, int w, int h, bool large = false) {
    // Color based on distance
    uint16_t col = g_roverTOF < 150 ? COL_DANGER :
                   g_roverTOF < 300 ? COL_WARNING : COL_SUCCESS;

    // Bar background
    g_canvas.drawRoundRect(x, y, w, h, 4, COL_DIM);
    float ratio = constrain(g_roverTOF, 0, 2000) / 2000.0f;
    int fillW = (int)(ratio * (w - 4));
    g_canvas.fillRoundRect(x+2, y+2, fillW, h-4, 3, col);

    // Distance text
    char buf[16];
    snprintf(buf, sizeof(buf), "%dmm", g_roverTOF);
    g_canvas.setTextColor(COL_TEXT);
    g_canvas.setTextDatum(MC_DATUM);
    if (large) {
        g_canvas.setTextSize(1.5);
    } else {
        g_canvas.setTextSize(1);
    }
    g_canvas.drawString(buf, x + w/2, y + h/2);
    g_canvas.setTextSize(1);
}

// ---- PAIR SCREEN ----
void drawPairScreen() {
    g_canvas.fillScreen(COL_BG);

    // Title
    g_canvas.setTextColor(COL_PRIMARY);
    g_canvas.setTextSize(2);
    g_canvas.setTextDatum(MC_DATUM);
    g_canvas.drawString("XIAOCHONG", 160, 20);

    g_canvas.setTextSize(1);
    g_canvas.setTextColor(COL_ACCENT);
    g_canvas.drawString("Controller v2.0", 160, 40);

    // Scanning indicator
    g_canvas.setTextColor(COL_TEXT);
    g_canvas.drawString(g_ctrlName, 160, 55);

    if (g_numDiscovered == 0) {
        // No rovers found yet
        g_canvas.setTextColor(COL_DIM);
        g_canvas.setTextSize(1.2);
        int dots = (millis() / 300) % 4;
        String msg = "Scanning for rovers";
        for (int i = 0; i < dots; i++) msg += ".";
        g_canvas.drawString(msg, 160, 130);
    } else {
        // List discovered rovers
        g_canvas.setTextColor(COL_TEXT);
        g_canvas.setTextDatum(ML_DATUM);
        g_canvas.setTextSize(1);
        g_canvas.drawString("Tap to pair:", 20, 68);

        for (int i = 0; i < g_numDiscovered; i++) {
            int iy = 80 + i * 45;
            RoverInfo& r = g_discoveredRovers[i];

            // Button-like row
            g_canvas.fillRoundRect(15, iy, 290, 40, 6, COL_BTN_BG);
            g_canvas.drawRoundRect(15, iy, 290, 40, 6, COL_PRIMARY);

            // Name
            g_canvas.setTextDatum(ML_DATUM);
            g_canvas.setTextColor(COL_PRIMARY);
            g_canvas.setTextSize(1.3);
            g_canvas.drawString(r.name, 25, iy + 13);

            // Hardware icons
            g_canvas.setTextSize(1);
            g_canvas.setTextDatum(ML_DATUM);
            int ix = 25;
            int iconY = iy + 28;
            if (r.hasRover) { g_canvas.setTextColor(COL_SUCCESS); g_canvas.drawString("MOT", ix, iconY); ix += 30; }
            if (r.hasTOF)   { g_canvas.setTextColor(COL_SUCCESS); g_canvas.drawString("TOF", ix, iconY); ix += 30; }
            if (r.hasCamS3) { g_canvas.setTextColor(COL_SUCCESS); g_canvas.drawString("CAM", ix, iconY); ix += 30; }
            if (r.hasASR)   { g_canvas.setTextColor(COL_SUCCESS); g_canvas.drawString("ASR", ix, iconY); ix += 30; }

            // Battery
            char batStr[8];
            snprintf(batStr, sizeof(batStr), "%d%%", r.batPercent);
            g_canvas.setTextDatum(MR_DATUM);
            g_canvas.setTextColor(r.batPercent > 25 ? COL_SUCCESS : COL_WARNING);
            g_canvas.drawString(batStr, 295, iy + 20);
        }
    }

    // Animated scan bar at bottom
    int barY = 225;
    float progress = (millis() % 2000) / 2000.0f;
    int barX = (int)(progress * 280);
    g_canvas.fillRect(20 + barX, barY, 20, 3, COL_PRIMARY);
}

// ---- HOME SCREEN ----
void drawHomeScreen() {
    g_canvas.fillScreen(COL_BG);

    // Scanlines
    for (int y = 0; y < 240; y += 8) g_canvas.drawFastHLine(0, y, 320, COL_GRID);

    // Title bar
    g_canvas.setTextColor(COL_PRIMARY);
    g_canvas.setTextSize(1.5);
    g_canvas.setTextDatum(ML_DATUM);
    g_canvas.drawString(g_roverName, 10, 15);

    drawStatusInfo(15);

    // Divider
    g_canvas.drawFastHLine(10, 35, 300, COL_DIM);

    // Hardware status row
    g_canvas.setTextSize(1);
    g_canvas.setTextDatum(ML_DATUM);
    int hx = 15;
    g_canvas.setTextColor(g_roverHasRover ? COL_SUCCESS : COL_DIM);
    g_canvas.drawString("MOT", hx, 47); hx += 40;
    g_canvas.setTextColor(g_roverHasTOF ? COL_SUCCESS : COL_DIM);
    g_canvas.drawString("TOF", hx, 47); hx += 40;
    g_canvas.setTextColor(g_roverHasCamS3 ? COL_SUCCESS : COL_DIM);
    g_canvas.drawString("CAM", hx, 47); hx += 40;
    g_canvas.setTextColor(g_roverHasASR ? COL_SUCCESS : COL_DIM);
    g_canvas.drawString("ASR", hx, 47);

    // Mode label
    g_canvas.setTextDatum(MR_DATUM);
    const char* modeLabel = "IDLE";
    uint16_t modeCol = COL_DIM;
    switch (g_roverMode) {
        case MODE_MANUAL: modeLabel = "MANUAL"; modeCol = COL_PRIMARY; break;
        case MODE_BAT:    modeLabel = "BAT";    modeCol = COL_WARNING; break;
        case MODE_FREE:   modeLabel = "FREE";   modeCol = COL_ACCENT;  break;
    }
    g_canvas.setTextColor(modeCol);
    g_canvas.drawString(modeLabel, 305, 47);

    g_canvas.setTextDatum(MC_DATUM);

    // Mode selection buttons
    g_canvas.setTextSize(1.5);

    // Manual mode button
    drawBtn(40, 80, 240, 40, "MANUAL DRIVE", COL_BTN_BG, COL_PRIMARY);

    // Bat mode button (grayed if no TOF)
    if (g_roverHasTOF) {
        drawBtn(40, 130, 240, 40, "BAT MODE", COL_BTN_BG, COL_WARNING);
    } else {
        drawBtn(40, 130, 240, 40, "BAT MODE (no TOF)", COL_BG, COL_DIM);
    }

    // Free explore button
    drawBtn(40, 180, 240, 40, "FREE EXPLORE", COL_BTN_BG, COL_ACCENT);
}

// ---- MANUAL DRIVE SCREEN ----
void drawManualScreen() {
    g_canvas.fillScreen(COL_BG);

    // Back button
    drawBackBtn();

    // Status
    g_canvas.setTextSize(1.3);
    g_canvas.setTextColor(COL_PRIMARY);
    g_canvas.setTextDatum(MC_DATUM);
    g_canvas.drawString("MANUAL", 160, 15);

    drawStatusInfo(30);

    // TOF bar (if available)
    if (g_roverHasTOF) {
        drawTOFDisplay(20, 42, 280, 18);
    }

    // D-pad (car-like layout)
    // Center at (160, 155), button size 80x50
    int cx = 160, cy = 155;
    int bw = 80, bh = 50;
    int gap = 5;

    bool fwdActive  = (g_activeMoveDir == DIR_FORWARD);
    bool bwdActive  = (g_activeMoveDir == DIR_BACKWARD);
    bool leftActive = (g_activeMoveDir == DIR_LEFT);
    bool rightActive= (g_activeMoveDir == DIR_RIGHT);
    bool stopActive = (g_activeMoveDir == DIR_STOP);

    // Forward (top center)
    drawBtn(cx-bw/2, cy-bh-gap-bh/2, bw, bh, "FWD",
            fwdActive ? COL_BTN_ACT : COL_BTN_BG, COL_SUCCESS, fwdActive);

    // Backward (bottom center)
    drawBtn(cx-bw/2, cy+gap+bh/2, bw, bh, "BWD",
            bwdActive ? COL_BTN_ACT : COL_BTN_BG, COL_WARNING, bwdActive);

    // Left (center left)
    drawBtn(cx-bw-gap-bw/2, cy-bh/2, bw, bh, "LEFT",
            leftActive ? COL_BTN_ACT : COL_BTN_BG, COL_PRIMARY, leftActive);

    // Right (center right)
    drawBtn(cx+gap+bw/2, cy-bh/2, bw, bh, "RIGHT",
            rightActive ? COL_BTN_ACT : COL_BTN_BG, COL_PRIMARY, rightActive);

    // Stop (center)
    drawBtn(cx-bw/2, cy-bh/2, bw, bh, "STOP",
            stopActive ? COL_DANGER : COL_BTN_BG, COL_DANGER, stopActive);

    // Speed indicator bottom
    g_canvas.setTextSize(1);
    g_canvas.setTextColor(COL_DIM);
    g_canvas.setTextDatum(MC_DATUM);
    char speedStr[16];
    snprintf(speedStr, sizeof(speedStr), "Speed: %d%%", g_roverSpeed);
    g_canvas.drawString(speedStr, 160, 228);

    // Check touch release
    checkTouchRelease();
}

// ---- BAT MODE SCREEN ----
void drawBatScreen() {
    g_canvas.fillScreen(COL_BG);

    drawBackBtn();

    g_canvas.setTextSize(1.5);
    g_canvas.setTextColor(COL_WARNING);
    g_canvas.setTextDatum(MC_DATUM);
    g_canvas.drawString("BAT MODE", 160, 15);

    drawStatusInfo(30);

    // Large TOF distance display
    g_canvas.setTextDatum(MC_DATUM);

    // Big distance number
    uint16_t distCol = g_roverTOF < 150 ? COL_DANGER :
                       g_roverTOF < 300 ? COL_WARNING : COL_SUCCESS;
    g_canvas.setTextColor(distCol);
    g_canvas.setTextSize(4);
    char distStr[16];
    snprintf(distStr, sizeof(distStr), "%d", g_roverTOF);
    g_canvas.drawString(distStr, 160, 100);

    g_canvas.setTextSize(1.5);
    g_canvas.setTextColor(COL_DIM);
    g_canvas.drawString("mm", 160, 130);

    // Visual distance bar
    drawTOFDisplay(20, 150, 280, 25, true);

    // Direction indicator
    g_canvas.setTextSize(1.2);
    g_canvas.setTextColor(COL_TEXT);
    const char* dirStr = "STOP";
    switch (g_roverMoveDir) {
        case DIR_FORWARD:    dirStr = ">> FORWARD >>";   break;
        case DIR_BACKWARD:   dirStr = "<< BACKWARD <<";  break;
        case DIR_TURN_LEFT:  dirStr = "<< TURNING LEFT";  break;
        case DIR_TURN_RIGHT: dirStr = "TURNING RIGHT >>"; break;
        default: break;
    }
    g_canvas.drawString(dirStr, 160, 195);

    // Sonar-like visualization
    drawSonarViz(160, 220);
}

void drawSonarViz(int cx, int cy) {
    // Simple sonar rings based on distance
    int maxR = 30;
    float ratio = constrain(g_roverTOF, 0, 2000) / 2000.0f;
    int r = (int)(ratio * maxR);

    uint16_t col = g_roverTOF < 150 ? COL_DANGER :
                   g_roverTOF < 300 ? COL_WARNING : COL_SUCCESS;

    // Animated ring
    int phase = (millis() / 200) % maxR;
    for (int ring = phase; ring <= maxR; ring += 10) {
        g_canvas.drawCircle(cx, cy, ring, COL_DIM);
    }
    g_canvas.fillCircle(cx, cy, max(2, r), col);
}

// ---- FREE EXPLORE SCREEN ----
void drawFreeScreen() {
    g_canvas.fillScreen(COL_BG);

    drawBackBtn();

    g_canvas.setTextSize(1.3);
    g_canvas.setTextColor(COL_ACCENT);
    g_canvas.setTextDatum(MC_DATUM);
    g_canvas.drawString("FREE EXPLORE", 160, 15);

    drawStatusInfo(30);

    // Camera area (center of screen)
    int camX = 20, camY = 45, camW = 280, camH = 150;
    g_canvas.drawRect(camX, camY, camW, camH, COL_DIM);

    if (g_camFrameReady && g_camBuf && g_camBufLen > 0) {
        // Draw JPEG from buffer
        g_canvas.drawJpg(g_camBuf, g_camBufLen, camX+1, camY+1, camW-2, camH-2);
        g_camFrameReady = false;  // Mark as consumed
    } else if (g_roverCamConn) {
        g_canvas.setTextColor(COL_DIM);
        g_canvas.setTextSize(1);
        g_canvas.drawString("Waiting for frame...", camX + camW/2, camY + camH/2);
    } else {
        g_canvas.setTextColor(COL_DIM);
        g_canvas.setTextSize(1);
        g_canvas.drawString("Camera not connected", camX + camW/2, camY + camH/2 - 8);
        g_canvas.setTextColor(COL_WARNING);
        g_canvas.drawString("TOF data only", camX + camW/2, camY + camH/2 + 8);
    }

    // TOF bar below camera
    if (g_roverHasTOF) {
        drawTOFDisplay(20, 200, 200, 20, true);
    }

    // Direction indicator
    g_canvas.setTextSize(1);
    g_canvas.setTextDatum(MR_DATUM);
    const char* dirStr = "-";
    uint16_t dirCol = COL_DIM;
    switch (g_roverMoveDir) {
        case DIR_FORWARD:    dirStr = "FWD";  dirCol = COL_SUCCESS; break;
        case DIR_BACKWARD:   dirStr = "BWD";  dirCol = COL_WARNING; break;
        case DIR_TURN_LEFT:  dirStr = "T-L";  dirCol = COL_PRIMARY; break;
        case DIR_TURN_RIGHT: dirStr = "T-R";  dirCol = COL_PRIMARY; break;
        case DIR_STOP:       dirStr = "STOP"; dirCol = COL_DIM;     break;
    }
    g_canvas.setTextColor(dirCol);
    g_canvas.drawString(dirStr, 300, 210);

    // Elapsed time
    g_canvas.setTextDatum(ML_DATUM);
    g_canvas.setTextColor(COL_DIM);
    g_canvas.drawString("Exploring...", 20, 228);
}
