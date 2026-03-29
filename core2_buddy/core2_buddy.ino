/*
 * ============================================================================
 *  core2_buddy.ino — WorkBuddy 物理任务看板
 * ============================================================================
 *  Runs on M5Stack Core2 (+M5GO Bottom2).
 *  Displays WorkBuddy workspaces & tasks on 320x240 touch screen.
 *  Communicates with PC via USB serial (buddy_bridge.py).
 *
 *  Navigation:  Workspace List → Task List (tap task = TTS readout)
 *
 *  UI Style: Cyberpunk (black bg, cyan/teal accents, large touch targets)
 *
 *  Dependencies: M5Unified, M5GFX, FastLED, ArduinoJson
 *  Board: m5stack:esp32:m5stack_core2
 *  Serial: 460800 baud
 *
 *  Author: sindney (m5stack_toys)
 *  License: MIT
 * ============================================================================
 */

#include <M5Unified.h>
#include <FastLED.h>
#include <ArduinoJson.h>
#include <lgfx/Fonts/efont/lgfx_efont_cn.h>

// ============================================================================
//  Serial Protocol
// ============================================================================
#define SERIAL_BAUD     460800
#define SYNC_BYTE       0xBD
#define MAX_PAYLOAD     4096

// PC → Core2
#define MSG_WORKSPACE_LIST  0x01
#define MSG_TASK_LIST       0x02
// 0x03 reserved (was MSG_TASK_IMAGE, removed)
#define MSG_STATUS_CHANGE   0x04
#define MSG_AUDIO_DATA      0x05
#define MSG_HEARTBEAT_PC    0x06

// Core2 → PC
#define MSG_REQ_WORKSPACES  0x10
#define MSG_REQ_TASKS       0x11
#define MSG_REQ_TTS         0x12  // Request TTS readout for a task
#define MSG_ACK             0x13
#define MSG_HEARTBEAT_DEV   0x14

// ============================================================================
//  LED Configuration (M5GO Bottom2)
// ============================================================================
#define LED_PIN     25
#define NUM_LEDS    10
CRGB leds[NUM_LEDS];

// ============================================================================
//  UI Colors (RGB565)
// ============================================================================
#define COL_BG          0x0000  // Pure black
#define COL_CYAN        0x07FF  // #00FFFF — primary accent
#define COL_DARK_CYAN   0x0333  // Subtle dark teal for lines/borders
#define COL_PANEL       0x10A2  // #181818 — card/panel bg (very dark gray)
#define COL_TEXT        0xB7FF  // #B0FFFF — main text (bright cyan-white)
#define COL_TEXT_DIM    0x4228  // #444444 — secondary/disabled text
#define COL_GREEN       0x2EE9  // #2EDD48 — completed/online
#define COL_YELLOW      0xFE00  // #FFD000 — in_progress/warning
#define COL_RED         0xF800  // #FF0000 — error/pending
#define COL_HIGHLIGHT   0x0B4D  // #005A6A — selected item bg
#define COL_BADGE_BG    0x18C3  // #1A1A1A — badge background

// ============================================================================
//  UI Layout
// ============================================================================
#define SCREEN_W    320
#define SCREEN_H    240

// Header
#define HEADER_H    28

// Content area
#define CONTENT_Y   HEADER_H
#define FOOTER_H    40
#define CONTENT_H   (SCREEN_H - HEADER_H - FOOTER_H)

// List items — large for touch
#define ITEM_H      44
#define ITEM_PAD_X  12
#define ITEM_PAD_Y  4
#define ITEM_GAP    2
#define MAX_VISIBLE ((CONTENT_H) / (ITEM_H + ITEM_GAP))

// Footer button zones (three equal columns)
#define BTN_L_X     0
#define BTN_L_W     107
#define BTN_M_X     107
#define BTN_M_W     106
#define BTN_R_X     213
#define BTN_R_W     107

// ============================================================================
//  Data Structures
// ============================================================================
struct WorkspaceInfo {
    char name[32];
    uint8_t id;
    uint16_t taskCount;
    uint16_t doneCount;
    uint16_t activeCount;
};

struct TaskInfo {
    char id[8];
    char convId[12];
    char status;        // 'c'=completed, 'p'=in_progress, 'd'=pending
    char content[192];  // UTF-8: ~60 Chinese chars (3 bytes each)
};

// ============================================================================
//  State Machine
// ============================================================================
enum AppState {
    STATE_CONNECTING,   // Waiting for PC Bridge
    STATE_WS_LIST,      // Workspace list
    STATE_TASK_LIST,    // Task list (for selected workspace)
    STATE_TTS_PLAYING,  // TTS waveform animation + replay on tap
};

// ============================================================================
//  Globals
// ============================================================================
static M5Canvas canvas(&M5.Display);

static AppState state = STATE_CONNECTING;
static WorkspaceInfo workspaces[8];
static uint8_t wsCount = 0;
static TaskInfo tasks[32];
static uint8_t taskCount = 0;
static int8_t selectedWs = -1;

// Scroll
static int scrollOffset = 0;
static int totalItems = 0;

// Audio
static uint8_t* audioBuffer = nullptr;
static uint32_t audioSize = 0;
static uint16_t audioTotalChunks = 0;
static uint16_t audioRecvChunks = 0;
static bool audioReady = false;
static bool audioPlaying = false;      // speaker is currently playing
static bool audioFinished = false;     // playback completed, ready for replay
static uint32_t audioPlayStartTime = 0; // when playback started
static uint32_t audioReceiveStartTime = 0; // when first chunk received
static bool audioReceiving = false;    // receiving audio chunks

// Connection
static bool pcConnected = false;
static uint32_t lastHbSent = 0;
static uint32_t lastHbRecv = 0;
static uint32_t connectAnimT = 0;

// LED notification
static uint32_t ledStartTime = 0;
static uint32_t ledDuration = 0;
static CRGB ledColor = CRGB::Black;
static bool ledActive = false;

// Serial receive
static uint8_t rxBuf[MAX_PAYLOAD + 16];
static uint16_t rxLen = 0;
static enum { RX_SYNC1, RX_SYNC2, RX_TYPE, RX_SEQ, RX_LEN_L, RX_LEN_H, RX_PAYLOAD, RX_CRC } rxState = RX_SYNC1;
static uint8_t rxType, rxSeq;
static uint16_t rxPayloadLen;
static uint16_t rxPayloadIdx;

// Touch
static bool touchWasPressed = false;

// Chinese font (U8g2 efont CN 14px — supports CJK characters)
static const lgfx::U8g2font efontCN14((const uint8_t*)lgfx_efont_cn_14);
static const lgfx::U8g2font efontCN10((const uint8_t*)lgfx_efont_cn_10);

// ============================================================================
//  Forward Declarations
// ============================================================================
void drawHeader();
void drawFooter(const char* btnL, const char* btnM, const char* btnR);
void drawConnecting();
void drawWsList();
void drawTaskList();
void drawTtsPlaying();
void drawListItem(int index, int y, const char* label, uint16_t labelColor,
                  const char* badge, uint16_t badgeColor, bool showArrow);
void handleTouch();
void sendFrame(uint8_t type, const uint8_t* payload, uint16_t len);
void sendHeartbeat();
void requestWorkspaces();
void requestTasks(uint8_t wsId);
void requestTts(uint8_t wsId, const char* taskId);
void processFrame(uint8_t type, const uint8_t* payload, uint16_t len);
void serialReceive();
void updateLed();
void startLedNotify(CRGB color, uint32_t duration_ms);

// ============================================================================
//  Setup
// ============================================================================
void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = SERIAL_BAUD;
    // Increase serial RX buffer BEFORE M5.begin() (which calls Serial.begin)
    Serial.setRxBufferSize(8192);
    M5.begin(cfg);

    // Display
    M5.Display.setRotation(1);
    canvas.createSprite(SCREEN_W, SCREEN_H);
    canvas.setTextWrap(false);

    // Speaker
    auto spk_cfg = M5.Speaker.config();
    spk_cfg.sample_rate = 16000;
    spk_cfg.task_pinned_core = 1;
    M5.Speaker.config(spk_cfg);
    M5.Speaker.begin();
    M5.Speaker.setVolume(128);

    // FastLED
    FastLED.addLeds<SK6812, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(40);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();

    // Allocate PSRAM buffers
    audioBuffer = (uint8_t*)ps_malloc(64000);           // ~2s of 16kHz 16bit audio

    // Initial state
    state = STATE_CONNECTING;
    connectAnimT = millis();
}

// ============================================================================
//  Main Loop
// ============================================================================
void loop() {
    M5.update();
    uint32_t now = millis();

    // Serial receive
    serialReceive();

    // Heartbeat
    if (now - lastHbSent > 3000) {
        sendHeartbeat();
        lastHbSent = now;
    }

    // Connection timeout
    if (pcConnected && now - lastHbRecv > 10000) {
        pcConnected = false;
        state = STATE_CONNECTING;
    }

    // Touch
    handleTouch();

    // LED
    updateLed();

    // Audio
    if (audioReady) {
        M5.Speaker.playRaw((const int16_t*)audioBuffer, audioSize / 2, 16000, false, 1);
        audioReady = false;
        audioPlaying = true;
        audioFinished = false;
        audioPlayStartTime = millis();
        state = STATE_TTS_PLAYING;
    }

    // Check if audio playback finished
    if (audioPlaying && !M5.Speaker.isPlaying()) {
        audioPlaying = false;
        audioFinished = true;
    }

    // ── Render ──
    canvas.fillSprite(COL_BG);
    drawHeader();

    switch (state) {
        case STATE_CONNECTING:
            drawConnecting();
            break;
        case STATE_WS_LIST:
            drawWsList();
            drawFooter(NULL, "REFRESH", NULL);
            break;
        case STATE_TASK_LIST:
            drawTaskList();
            drawFooter("BACK", "REFRESH", NULL);
            break;
        case STATE_TTS_PLAYING:
            drawTtsPlaying();
            drawFooter("BACK", NULL, NULL);
            break;
    }

    canvas.pushSprite(0, 0);

    // Yield to serial: process incoming data between frames
    // This is critical during image transfer — Core2 must drain the RX buffer
    // frequently to avoid overflow at 460800 baud
    serialReceive();
    delay(16);  // ~60fps (shorter delay = more serial processing time)
    serialReceive();
}

// ============================================================================
//  UI: Header
// ============================================================================
void drawHeader() {
    // Background
    canvas.fillRect(0, 0, SCREEN_W, HEADER_H, COL_BG);
    // Bottom accent line
    canvas.drawLine(0, HEADER_H - 1, SCREEN_W - 1, HEADER_H - 1, COL_DARK_CYAN);

    // Title — depends on current state
    canvas.setFont(nullptr);  // default font for English text
    canvas.setTextSize(1);
    canvas.setTextColor(COL_CYAN);
    canvas.setCursor(8, 8);

    switch (state) {
        case STATE_CONNECTING:
            canvas.print("CORE2 BUDDY");
            break;
        case STATE_WS_LIST:
            canvas.print("WORKSPACES");
            break;
        case STATE_TASK_LIST:
            if (selectedWs >= 0 && selectedWs < wsCount) {
                // Workspace name may contain Chinese — use efont
                canvas.setFont(&efontCN10);
                canvas.setCursor(8, 7);
                canvas.printf("< %s", workspaces[selectedWs].name);
            } else {
                canvas.print("< TASKS");
            }
            break;
    }

    // Connection indicator (right side) — always use default font
    canvas.setFont(nullptr);
    canvas.setTextSize(1);
    canvas.setCursor(248, 8);
    if (pcConnected) {
        canvas.setTextColor(COL_GREEN);
        canvas.print("ONLINE");
        // Green dot
        canvas.fillCircle(240, 13, 3, COL_GREEN);
    } else {
        canvas.setTextColor(COL_RED);
        canvas.print("OFFLINE");
        canvas.fillCircle(240, 13, 3, COL_RED);
    }
}

// ============================================================================
//  UI: Footer (3-button bar)
// ============================================================================
void drawFooter(const char* btnL, const char* btnM, const char* btnR) {
    int y = SCREEN_H - FOOTER_H;

    // Background
    canvas.fillRect(0, y, SCREEN_W, FOOTER_H, COL_BG);
    // Top accent line
    canvas.drawLine(0, y, SCREEN_W - 1, y, COL_DARK_CYAN);

    canvas.setTextSize(1);
    int textY = y + 15;

    if (btnL) {
        // Draw button outline
        canvas.drawRoundRect(4, y + 6, BTN_L_W - 8, FOOTER_H - 12, 4, COL_DARK_CYAN);
        canvas.setTextColor(COL_CYAN);
        // Center text in button
        int tw = strlen(btnL) * 6;  // approx char width at size 1
        canvas.setCursor(BTN_L_X + (BTN_L_W - tw) / 2, textY);
        canvas.print(btnL);
    }

    if (btnM) {
        canvas.drawRoundRect(BTN_M_X + 4, y + 6, BTN_M_W - 8, FOOTER_H - 12, 4, COL_DARK_CYAN);
        canvas.setTextColor(COL_CYAN);
        int tw = strlen(btnM) * 6;
        canvas.setCursor(BTN_M_X + (BTN_M_W - tw) / 2, textY);
        canvas.print(btnM);
    }

    if (btnR) {
        canvas.drawRoundRect(BTN_R_X + 4, y + 6, BTN_R_W - 8, FOOTER_H - 12, 4, COL_DARK_CYAN);
        canvas.setTextColor(COL_CYAN);
        int tw = strlen(btnR) * 6;
        canvas.setCursor(BTN_R_X + (BTN_R_W - tw) / 2, textY);
        canvas.print(btnR);
    }

    // Scroll hint (if list has more items)
    if ((state == STATE_WS_LIST || state == STATE_TASK_LIST) && totalItems > MAX_VISIBLE) {
        // Page indicator between buttons
        canvas.setTextColor(COL_TEXT_DIM);
        char pageInfo[16];
        snprintf(pageInfo, sizeof(pageInfo), "%d/%d",
                 scrollOffset / MAX_VISIBLE + 1,
                 (totalItems + MAX_VISIBLE - 1) / MAX_VISIBLE);
        int tw = strlen(pageInfo) * 6;
        canvas.setCursor(SCREEN_W - tw - 8, textY);
        canvas.print(pageInfo);
    }
}

// ============================================================================
//  UI: Connecting
// ============================================================================
void drawConnecting() {
    uint32_t t = (millis() - connectAnimT) / 500;
    int dots = (t % 4);

    // Animated connecting text
    canvas.setTextColor(COL_CYAN);
    canvas.setTextSize(2);
    int cx = 60;
    int cy = 80;
    canvas.setCursor(cx, cy);
    canvas.print("CONNECTING");
    for (int i = 0; i < dots; i++) canvas.print(".");

    // Subtle pulse circle
    float pulse = sin(millis() * 0.003f) * 0.5f + 0.5f;
    uint8_t alpha = (uint8_t)(pulse * 80);
    uint16_t pulseCol = ((alpha / 8) << 11) | ((alpha / 4) << 5) | (alpha / 8);
    canvas.drawCircle(160, 140, 30 + (int)(pulse * 10), COL_DARK_CYAN);

    canvas.setTextSize(1);
    canvas.setTextColor(COL_TEXT_DIM);
    canvas.setCursor(60, 120);
    canvas.print("Waiting for buddy_bridge.py ...");
    canvas.setCursor(60, 140);
    canvas.printf("Serial: %d baud", SERIAL_BAUD);

    // Hint
    canvas.setTextColor(COL_DARK_CYAN);
    canvas.setCursor(40, 180);
    canvas.print("Run: python buddy_bridge.py");
}

// ============================================================================
//  UI: Workspace List
// ============================================================================
void drawWsList() {
    totalItems = wsCount;

    if (wsCount == 0) {
        canvas.setTextColor(COL_TEXT_DIM);
        canvas.setTextSize(1);
        canvas.setCursor(80, 110);
        canvas.print("No workspaces found");
        canvas.setCursor(70, 130);
        canvas.print("Tap REFRESH to reload");
        return;
    }

    for (int i = 0; i < wsCount; i++) {
        int vi = i - scrollOffset;  // visible index
        if (vi < 0) continue;
        if (vi >= MAX_VISIBLE) break;

        int y = CONTENT_Y + ITEM_PAD_Y + vi * (ITEM_H + ITEM_GAP);

        // Badge: done/total
        char badge[16] = "";
        uint16_t badgeCol = COL_TEXT_DIM;
        if (workspaces[i].taskCount > 0) {
            snprintf(badge, sizeof(badge), "%d/%d",
                     workspaces[i].doneCount, workspaces[i].taskCount);
            if (workspaces[i].activeCount > 0) badgeCol = COL_YELLOW;
            else if (workspaces[i].doneCount == workspaces[i].taskCount) badgeCol = COL_GREEN;
        }

        drawListItem(i, y, workspaces[i].name, COL_TEXT, badge, badgeCol, true);
    }

    // Scrollbar
    if (totalItems > MAX_VISIBLE) {
        int barH = max(10, CONTENT_H * MAX_VISIBLE / totalItems);
        int maxScroll = max(1, totalItems - MAX_VISIBLE);
        int barY = CONTENT_Y + (CONTENT_H - barH) * scrollOffset / maxScroll;
        canvas.fillRoundRect(SCREEN_W - 4, barY, 3, barH, 1, COL_DARK_CYAN);
    }
}

// ============================================================================
//  UI: Task List
// ============================================================================
void drawTaskList() {
    totalItems = taskCount;

    if (taskCount == 0) {
        canvas.setTextColor(COL_TEXT_DIM);
        canvas.setTextSize(1);
        canvas.setCursor(90, 110);
        canvas.print("No tasks found");
        canvas.setCursor(70, 130);
        canvas.print("Tap REFRESH to reload");
        return;
    }

    for (int i = 0; i < taskCount; i++) {
        int vi = i - scrollOffset;
        if (vi < 0) continue;
        if (vi >= MAX_VISIBLE) break;

        int y = CONTENT_Y + ITEM_PAD_Y + vi * (ITEM_H + ITEM_GAP);

        // Status badge
        const char* statusText = "?";
        uint16_t statusCol = COL_TEXT_DIM;
        switch (tasks[i].status) {
            case 'c':
                statusText = "DONE";
                statusCol = COL_GREEN;
                break;
            case 'p':
                statusText = "WORK";
                statusCol = COL_YELLOW;
                break;
            case 'd':
                statusText = "TODO";
                statusCol = COL_RED;
                break;
        }

        drawListItem(i, y, tasks[i].content, COL_TEXT, statusText, statusCol, false);

        // Status dot on left edge
        canvas.fillCircle(ITEM_PAD_X + 4, y + ITEM_H / 2, 4, statusCol);
    }

    // Scrollbar
    if (totalItems > MAX_VISIBLE) {
        int barH = max(10, CONTENT_H * MAX_VISIBLE / totalItems);
        int maxScroll = max(1, totalItems - MAX_VISIBLE);
        int barY = CONTENT_Y + (CONTENT_H - barH) * scrollOffset / maxScroll;
        canvas.fillRoundRect(SCREEN_W - 4, barY, 3, barH, 1, COL_DARK_CYAN);
    }
}

// ============================================================================
//  UI: TTS Playing (Waveform Animation + Replay)
// ============================================================================
void drawTtsPlaying() {
    int centerY = CONTENT_Y + CONTENT_H / 2;
    int barCount = 24;        // number of waveform bars
    int barW = 6;             // bar width
    int barGap = 4;           // gap between bars
    int totalW = barCount * (barW + barGap) - barGap;
    int startX = (SCREEN_W - totalW) / 2;
    int maxBarH = 60;

    if (audioReceiving) {
        // ── Receiving chunks: show progress ──
        uint32_t t = millis() / 300;
        int dots = (t % 4);
        
        canvas.setFont(&efontCN14);
        canvas.setTextColor(COL_CYAN);
        int textX = 90;
        canvas.setCursor(textX, centerY - 30);
        canvas.print("\xe6\xad\xa3\xe5\x9c\xa8\xe5\x8a\xa0\xe8\xbd\xbd\xe8\xaf\xad\xe9\x9f\xb3");  // 正在加载语音
        for (int i = 0; i < dots; i++) canvas.print(".");
        
        // Progress bar
        if (audioTotalChunks > 0) {
            int progW = 200;
            int progH = 6;
            int progX = (SCREEN_W - progW) / 2;
            int progY = centerY + 10;
            canvas.drawRoundRect(progX, progY, progW, progH, 3, COL_DARK_CYAN);
            int fillW = (int)((float)audioRecvChunks / audioTotalChunks * progW);
            if (fillW > 0) {
                canvas.fillRoundRect(progX, progY, fillW, progH, 3, COL_CYAN);
            }
        }
        
        canvas.setFont(nullptr);
    }
    else if (audioPlaying) {
        // ── Playing: animated waveform bars ──
        uint32_t elapsed = millis() - audioPlayStartTime;
        
        for (int i = 0; i < barCount; i++) {
            // Create organic-looking wave using multiple sine frequencies
            float phase1 = sin((elapsed * 0.008f) + i * 0.6f);
            float phase2 = sin((elapsed * 0.012f) + i * 0.4f + 1.5f);
            float phase3 = sin((elapsed * 0.005f) + i * 0.9f + 3.0f);
            float combined = (phase1 * 0.5f + phase2 * 0.3f + phase3 * 0.2f);
            
            // Map to bar height (always at least a small bar)
            int barH = (int)(10 + (combined * 0.5f + 0.5f) * (maxBarH - 10));
            
            int x = startX + i * (barW + barGap);
            int y = centerY - barH / 2;
            
            // Cyan-tinted color gradient: center bars brighter
            float dist = fabsf((float)i - barCount / 2.0f) / (barCount / 2.0f);
            uint8_t g = (uint8_t)(255 * (1.0f - dist * 0.5f));
            uint16_t col = ((0) << 11) | ((g >> 2) << 5) | ((g >> 3));
            
            canvas.fillRoundRect(x, y, barW, barH, 2, col);
        }
        
        // "Playing" label
        canvas.setFont(&efontCN10);
        canvas.setTextColor(COL_TEXT_DIM);
        canvas.setCursor(110, CONTENT_Y + CONTENT_H - 24);
        canvas.print("\xe6\xad\xa3\xe5\x9c\xa8\xe6\x92\xad\xe6\x94\xbe...");  // 正在播放...
        canvas.setFont(nullptr);
    }
    else if (audioFinished) {
        // ── Finished: static bars + replay prompt ──
        for (int i = 0; i < barCount; i++) {
            int barH = 8;
            int x = startX + i * (barW + barGap);
            int y = centerY - barH / 2;
            canvas.fillRoundRect(x, y, barW, barH, 2, COL_DARK_CYAN);
        }
        
        // "Playback complete" text
        canvas.setFont(&efontCN14);
        canvas.setTextColor(COL_CYAN);
        canvas.setCursor(100, centerY - 40);
        canvas.print("\xe6\x92\xad\xe6\x94\xbe\xe5\xae\x8c\xe6\x88\x90");  // 播放完成
        
        // Tap to replay hint — pulsing
        float pulse = sin(millis() * 0.004f) * 0.5f + 0.5f;
        uint8_t alpha = (uint8_t)(100 + pulse * 155);
        uint16_t hintCol = ((0) << 11) | ((alpha >> 2) << 5) | (alpha >> 3);
        canvas.setTextColor(hintCol);
        canvas.setCursor(60, centerY + 20);
        canvas.print("\xe7\x82\xb9\xe5\x87\xbb\xe5\xb1\x8f\xe5\xb9\x95\xe9\x87\x8d\xe6\x96\xb0\xe6\x92\xad\xe6\x94\xbe");  // 点击屏幕重新播放
        canvas.setFont(nullptr);
    }
}
// ============================================================================
// Returns byte count of one UTF-8 character (1-4), or 1 if invalid
static int utf8CharLen(uint8_t b) {
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;  // invalid → skip 1 byte
}

// Truncate UTF-8 string to fit within maxWidth pixels using the current font
// Appends ".." if truncated. Output written to `out` buffer (must be large enough).
// Returns true if truncated.
static bool utf8Truncate(M5Canvas& c, const char* src, int maxWidth, char* out, int outSize) {
    int srcLen = strlen(src);
    // First check if it fits as-is
    if (c.textWidth(src) <= maxWidth) {
        strlcpy(out, src, outSize);
        return false;
    }
    // Truncate: find max prefix that fits with ".."
    int dotW = c.textWidth("..");
    int targetW = maxWidth - dotW;
    if (targetW < 0) targetW = 0;
    
    int pos = 0;
    int lastGoodPos = 0;
    while (pos < srcLen) {
        int cl = utf8CharLen((uint8_t)src[pos]);
        int nextPos = pos + cl;
        if (nextPos > srcLen) break;
        
        // Measure prefix [0..nextPos)
        char tmp = ((char*)src)[nextPos];
        ((char*)src)[nextPos] = '\0';
        int w = c.textWidth(src);
        ((char*)src)[nextPos] = tmp;
        
        if (w > targetW) break;
        lastGoodPos = nextPos;
        pos = nextPos;
    }
    
    if (lastGoodPos == 0) lastGoodPos = utf8CharLen((uint8_t)src[0]);
    if (lastGoodPos + 3 < outSize) {
        memcpy(out, src, lastGoodPos);
        out[lastGoodPos] = '.';
        out[lastGoodPos+1] = '.';
        out[lastGoodPos+2] = '\0';
    } else {
        strlcpy(out, src, outSize);
    }
    return true;
}

// ============================================================================
//  UI: Generic List Item
// ============================================================================
void drawListItem(int index, int y, const char* label, uint16_t labelColor,
                  const char* badge, uint16_t badgeColor, bool showArrow) {
    // Card background
    canvas.fillRoundRect(ITEM_PAD_X, y, SCREEN_W - ITEM_PAD_X * 2, ITEM_H, 6, COL_PANEL);
    // Subtle top highlight (1px lighter)
    canvas.drawLine(ITEM_PAD_X + 6, y, SCREEN_W - ITEM_PAD_X - 6, y, COL_DARK_CYAN);

    // Use Chinese-capable font for label
    canvas.setTextSize(1);  // MUST reset textSize before setFont (efont respects textSize)
    canvas.setFont(&efontCN14);
    canvas.setTextColor(labelColor);
    int textX = ITEM_PAD_X + 16;
    int textY = y + (ITEM_H - 14) / 2;  // center vertically (efont 14px)

    // If it's task list, shift text right to make room for status dot
    if (state == STATE_TASK_LIST) {
        textX = ITEM_PAD_X + 24;
    }

    // Calculate available width for label text
    int availW = SCREEN_W - textX - ITEM_PAD_X - 16;
    if (badge && strlen(badge) > 0) availW -= strlen(badge) * 7 + 22;
    if (showArrow) availW -= 16;

    // Truncate and print label (UTF-8 aware)
    char truncated[128];
    canvas.setCursor(textX, textY);
    utf8Truncate(canvas, label, availW, truncated, sizeof(truncated));
    canvas.print(truncated);

    // Reset font state for non-CJK elements
    canvas.setFont(nullptr);
    canvas.setTextSize(1);

    // Badge (right side)
    if (badge && strlen(badge) > 0) {
        int bw = strlen(badge) * 7 + 10;
        int bx = SCREEN_W - ITEM_PAD_X - bw - (showArrow ? 16 : 8);
        int by = y + (ITEM_H - 16) / 2;

        canvas.fillRoundRect(bx, by - 2, bw, 18, 4, COL_BADGE_BG);
        canvas.setTextColor(badgeColor);
        canvas.setCursor(bx + 5, by + 2);
        canvas.print(badge);
    }

    // Arrow indicator ">"
    if (showArrow) {
        canvas.setTextSize(2);
        canvas.setTextColor(COL_DARK_CYAN);
        canvas.setCursor(SCREEN_W - ITEM_PAD_X - 16, y + (ITEM_H - 16) / 2);
        canvas.print(">");
        canvas.setTextSize(1);  // Reset after arrow
    }
}

// ============================================================================
//  Touch Handling
// ============================================================================
void handleTouch() {
    auto t = M5.Touch.getDetail();
    bool pressed = t.isPressed();

    // Detect tap (on release)
    if (touchWasPressed && !pressed) {
        int tx = t.x, ty = t.y;
        int footerY = SCREEN_H - FOOTER_H;

        // ── Footer buttons ──
        if (ty >= footerY) {
            if (tx < BTN_L_X + BTN_L_W) {
                // LEFT button
                onFooterLeft();
            } else if (tx < BTN_M_X + BTN_M_W) {
                // MIDDLE button
                onFooterMiddle();
            } else {
                // RIGHT button
                onFooterRight();
            }
        }
        // ── Header tap (back navigation for task list) ──
        else if (ty < HEADER_H) {
            if (state == STATE_TASK_LIST) {
                // Tap header = go back to workspace list
                state = STATE_WS_LIST;
                scrollOffset = 0;
            } else if (state == STATE_TTS_PLAYING) {
                // Tap header = go back to task list
                M5.Speaker.stop();
                audioPlaying = false;
                audioFinished = false;
                audioReceiving = false;
                state = STATE_TASK_LIST;
            }
        }
        // ── Content area tap ──
        else if (ty >= CONTENT_Y && ty < footerY) {
            if (state == STATE_WS_LIST) {
                int tappedVisIndex = (ty - CONTENT_Y - ITEM_PAD_Y) / (ITEM_H + ITEM_GAP);
                int tappedIndex = scrollOffset + tappedVisIndex;
                onWsTap(tappedIndex);
            } else if (state == STATE_TASK_LIST) {
                int tappedVisIndex = (ty - CONTENT_Y - ITEM_PAD_Y) / (ITEM_H + ITEM_GAP);
                int tappedIndex = scrollOffset + tappedVisIndex;
                onTaskTap(tappedIndex);
            } else if (state == STATE_TTS_PLAYING && audioFinished) {
                // Tap to replay
                if (audioSize > 0) {
                    M5.Speaker.playRaw((const int16_t*)audioBuffer, audioSize / 2, 16000, false, 1);
                    audioPlaying = true;
                    audioFinished = false;
                    audioPlayStartTime = millis();
                    startLedNotify(CRGB::Blue, 500);
                }
            }
        }
    }

    touchWasPressed = pressed;
}

void onFooterLeft() {
    switch (state) {
        case STATE_WS_LIST:
            // No back from workspace list; could scroll up
            if (scrollOffset > 0) scrollOffset--;
            break;
        case STATE_TASK_LIST:
            // BACK → workspace list
            state = STATE_WS_LIST;
            scrollOffset = 0;
            break;
        case STATE_TTS_PLAYING:
            // BACK → task list, stop audio
            M5.Speaker.stop();
            audioPlaying = false;
            audioFinished = false;
            audioReceiving = false;
            state = STATE_TASK_LIST;
            break;
        default:
            break;
    }
}

void onFooterMiddle() {
    switch (state) {
        case STATE_WS_LIST:
            // REFRESH
            requestWorkspaces();
            break;
        case STATE_TASK_LIST:
            // REFRESH
            if (selectedWs >= 0) requestTasks(workspaces[selectedWs].id);
            break;
        default:
            break;
    }
}

void onFooterRight() {
    switch (state) {
        case STATE_WS_LIST:
            // Scroll down
            if (scrollOffset < totalItems - MAX_VISIBLE) scrollOffset++;
            break;
        case STATE_TASK_LIST:
            // Scroll down
            if (scrollOffset < totalItems - MAX_VISIBLE) scrollOffset++;
            break;
        default:
            break;
    }
}

void onWsTap(int index) {
    if (index < 0 || index >= wsCount) return;

    selectedWs = index;
    taskCount = 0;
    scrollOffset = 0;
    state = STATE_TASK_LIST;

    // Request tasks for this workspace
    requestTasks(workspaces[index].id);
}

void onTaskTap(int index) {
    if (index < 0 || index >= taskCount) return;

    // Request TTS readout for this task
    requestTts(workspaces[selectedWs].id, tasks[index].id);

    // Enter TTS receiving state immediately
    audioReceiving = true;
    audioPlaying = false;
    audioFinished = false;
    audioSize = 0;
    audioRecvChunks = 0;
    audioTotalChunks = 0;
    audioReceiveStartTime = millis();
    state = STATE_TTS_PLAYING;

    // Brief LED flash to acknowledge tap
    startLedNotify(CRGB::Blue, 500);
}

// ============================================================================
//  Serial Protocol
// ============================================================================
void sendFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    uint8_t header[6] = { SYNC_BYTE, SYNC_BYTE, type, 0, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8) };

    // CRC: XOR of type+seq+len_l+len_h+payload
    uint8_t crc = type ^ 0 ^ header[4] ^ header[5];
    for (uint16_t i = 0; i < len; i++) crc ^= payload[i];

    Serial.write(header, 6);
    if (len > 0 && payload) Serial.write(payload, len);
    Serial.write(crc);
}

void sendHeartbeat() {
    sendFrame(MSG_HEARTBEAT_DEV, nullptr, 0);
}

void requestWorkspaces() {
    sendFrame(MSG_REQ_WORKSPACES, nullptr, 0);
}

void requestTasks(uint8_t wsId) {
    sendFrame(MSG_REQ_TASKS, &wsId, 1);
}

void requestTts(uint8_t wsId, const char* taskId) {
    char json[64];
    snprintf(json, sizeof(json), "{\"ws\":%d,\"task_id\":\"%s\"}", wsId, taskId);
    sendFrame(MSG_REQ_TTS, (const uint8_t*)json, strlen(json));
}

void serialReceive() {
    while (Serial.available()) {
        uint8_t b = Serial.read();

        switch (rxState) {
            case RX_SYNC1:
                if (b == SYNC_BYTE) rxState = RX_SYNC2;
                break;
            case RX_SYNC2:
                rxState = (b == SYNC_BYTE) ? RX_TYPE : RX_SYNC1;
                break;
            case RX_TYPE:
                rxType = b;
                rxState = RX_SEQ;
                break;
            case RX_SEQ:
                rxSeq = b;
                rxState = RX_LEN_L;
                break;
            case RX_LEN_L:
                rxPayloadLen = b;
                rxState = RX_LEN_H;
                break;
            case RX_LEN_H:
                rxPayloadLen |= (b << 8);
                rxPayloadIdx = 0;
                if (rxPayloadLen == 0) {
                    rxState = RX_CRC;
                } else if (rxPayloadLen > MAX_PAYLOAD) {
                    rxState = RX_SYNC1;
                } else {
                    rxState = RX_PAYLOAD;
                }
                break;
            case RX_PAYLOAD:
                rxBuf[rxPayloadIdx++] = b;
                if (rxPayloadIdx >= rxPayloadLen) {
                    rxState = RX_CRC;
                }
                break;
            case RX_CRC: {
                uint8_t crc = rxType ^ rxSeq ^ (rxPayloadLen & 0xFF) ^ (rxPayloadLen >> 8);
                for (uint16_t i = 0; i < rxPayloadLen; i++) crc ^= rxBuf[i];

                if (crc == b) {
                    processFrame(rxType, rxBuf, rxPayloadLen);
                }
                rxState = RX_SYNC1;
                break;
            }
        }
    }
}

// ============================================================================
//  Frame Processing
// ============================================================================
void processFrame(uint8_t type, const uint8_t* payload, uint16_t len) {
    switch (type) {
        case MSG_HEARTBEAT_PC:
            lastHbRecv = millis();
            if (!pcConnected) {
                pcConnected = true;
                state = STATE_WS_LIST;
                requestWorkspaces();
                startLedNotify(CRGB::Blue, 2000);
            }
            break;

        case MSG_WORKSPACE_LIST: {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload, len);
            if (err) break;

            JsonArray arr = doc.as<JsonArray>();
            wsCount = 0;
            for (JsonObject obj : arr) {
                if (wsCount >= 8) break;
                strlcpy(workspaces[wsCount].name, obj["name"] | "?", 32);
                workspaces[wsCount].id = obj["id"] | 0;
                workspaces[wsCount].taskCount = obj["tasks"] | 0;
                workspaces[wsCount].doneCount = obj["done"] | 0;
                workspaces[wsCount].activeCount = obj["active"] | 0;
                wsCount++;
            }

            // If we're on workspace list, reset scroll
            if (state == STATE_WS_LIST) {
                scrollOffset = 0;
            }
            break;
        }

        case MSG_TASK_LIST: {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload, len);
            if (err) break;

            taskCount = 0;
            JsonArray arr = doc["tasks"].as<JsonArray>();
            for (JsonObject obj : arr) {
                if (taskCount >= 32) break;
                strlcpy(tasks[taskCount].id, obj["id"] | "?", 8);
                strlcpy(tasks[taskCount].convId, obj["cid"] | "", 12);
                const char* s = obj["s"] | "d";
                tasks[taskCount].status = s[0];
                strlcpy(tasks[taskCount].content, obj["c"] | "?", 192);
                taskCount++;
            }

            // Reset scroll when new data arrives
            if (state == STATE_TASK_LIST) {
                scrollOffset = 0;
            }
            break;
        }

        case MSG_STATUS_CHANGE: {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload, len);
            if (err) break;

            const char* newStatus = doc["new"] | "";
            if (strcmp(newStatus, "c") == 0) {
                startLedNotify(CRGB::Green, 10000);
            } else if (strcmp(newStatus, "d") == 0) {
                startLedNotify(CRGB::Red, 10000);
            }

            // Refresh data
            requestWorkspaces();
            break;
        }

        case MSG_AUDIO_DATA: {
            if (len < 4) break;
            uint16_t chunkIdx = payload[0] | (payload[1] << 8);
            uint16_t totalChunks = payload[2] | (payload[3] << 8);
            const uint8_t* data = payload + 4;
            uint16_t dataLen = len - 4;

            if (chunkIdx == 0) {
                audioSize = 0;
                audioTotalChunks = totalChunks;
                audioRecvChunks = 0;
                audioReceiving = true;
            }

            if (audioSize + dataLen <= 64000) {
                memcpy(audioBuffer + audioSize, data, dataLen);
                audioSize += dataLen;
            }
            audioRecvChunks++;

            if (audioRecvChunks >= audioTotalChunks) {
                audioReceiving = false;
                audioReady = true;
            }
            break;
        }
    }
}

// ============================================================================
//  LED Notification
// ============================================================================
void startLedNotify(CRGB color, uint32_t duration_ms) {
    ledColor = color;
    ledStartTime = millis();
    ledDuration = duration_ms;
    ledActive = true;
}

void updateLed() {
    if (!ledActive) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        return;
    }

    uint32_t elapsed = millis() - ledStartTime;
    if (elapsed >= ledDuration) {
        ledActive = false;
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        return;
    }

    // Breathing effect
    float phase = sin(elapsed * 0.006f);
    uint8_t brightness = (uint8_t)(128.0f + 127.0f * phase);

    CRGB c = ledColor;
    c.nscale8(brightness);
    fill_solid(leds, NUM_LEDS, c);
    FastLED.show();
}
