/*
 * M5Stack UnitV Camera Viewer
 * 
 * 功能：接收 UnitV 摄像头图像并显示
 * 协议：4字节长度头（大端）+ JPEG 数据
 * 握手：M5_READY -> UNITV_OK -> START -> 数据流
 * 
 * 使用 M5Unified 库（兼容 ESP32 SDK 3.x）
 * 
 * GUI 风格：简约赛博朋克 (Minimal Cyberpunk HUD)
 */

#include <M5Unified.h>

// ─── UART 配置 ───
#define UNITV_SERIAL Serial2
#define UNITV_BAUD   115200

// Port C 引脚 (UART2 默认)
#define PORT_C_RX 16
#define PORT_C_TX 17

// Port B 引脚 (PIR Unit)
#define PIR_PIN 36  // Port B 第一引脚 (INPUT)

// 缓冲区
#define MAX_JPEG_SIZE 20000
uint8_t jpegBuf[MAX_JPEG_SIZE];

// 用于图像翻转的 Sprite
M5Canvas imgSprite(&M5.Display);

// ─── 显示模式 ───
enum DisplayMode {
    MODE_ALWAYS_ON,    // 模式一：常驻显示，忽略 PIR
    MODE_PIR_ACTIVE    // 模式二：PIR 有信号才显示，否则黑屏
};
DisplayMode displayMode = MODE_ALWAYS_ON;
bool screenVisible = true;        // 当前画面是否显示
bool lastPirState = false;        // 上次 PIR 状态
uint32_t lastPirChangeTime = 0;   // PIR 状态变化时间（防抖）
#define PIR_DEBOUNCE_MS 200       // PIR 防抖时间

// ─── 赛博朋克调色板 (RGB565) ───
// 主色调：青色 / 品红 / 深底
#define CP_BG         0x0000  // 纯黑背景
#define CP_BG_DARK    0x0841  // 极深灰 (微亮于纯黑)
#define CP_CYAN       0x07FF  // 霓虹青 - 主色
#define CP_CYAN_DIM   0x0410  // 暗青 - 次要信息
#define CP_MAGENTA    0xF81F  // 霓虹品红 - 强调/警告
#define CP_MAG_DIM    0x780F  // 暗品红
#define CP_GREEN      0x07E0  // 霓虹绿 - 成功/在线
#define CP_GREEN_DIM  0x0320  // 暗绿
#define CP_YELLOW     0xFFE0  // 霓虹黄 - 注意
#define CP_WHITE      0xFFFF  // 纯白 - 标题高亮
#define CP_GRID       0x18E3  // 网格线色 (深灰偏青)

// 屏幕尺寸
#define SCR_W 320
#define SCR_H 240

// HUD 布局常量
#define HUD_TOP_H     16  // 顶部 HUD 条高度
#define HUD_BOT_H     18  // 底部 HUD 条高度
#define HUD_MARGIN     4  // HUD 内边距
#define CORNER_LEN     8  // 角标线长度

// UnitV 图像尺寸 (QQVGA)
#define IMG_W        160
#define IMG_H        120

// 图像居中坐标 (在 HUD 区域之间居中)
#define VIEW_Y0      (HUD_TOP_H + 1)                        // 可视区域顶部
#define VIEW_H       (SCR_H - HUD_TOP_H - HUD_BOT_H - 2)   // 可视区域高度 = 204
#define IMG_X        ((SCR_W - IMG_W) / 2)                   // 水平居中 = 80
#define IMG_Y        (VIEW_Y0 + (VIEW_H - IMG_H) / 2)       // 垂直居中 = 59

// ─── 状态机 ───
enum State {
    STATE_HANDSHAKE,
    STATE_SYNC,        // 新增：搜索 JPEG 帧头来同步
    STATE_WAIT_HEADER,
    STATE_READ_DATA
};

State currentState = STATE_HANDSHAKE;

// 统计
uint32_t frameCount = 0;
uint32_t errorCount = 0;
uint32_t totalBytes = 0;
uint32_t lastFpsTime = 0;
float fps = 0;

// 握手相关
uint32_t lastHandshakeTime = 0;
bool unitvReady = false;

// ─── GUI 辅助函数 ───

// 绘制赛博朋克角标 (四角 L 形装饰线)
void drawCornerMarks(int x, int y, int w, int h, uint16_t color) {
    // 左上
    M5.Display.drawFastHLine(x, y, CORNER_LEN, color);
    M5.Display.drawFastVLine(x, y, CORNER_LEN, color);
    // 右上
    M5.Display.drawFastHLine(x + w - CORNER_LEN, y, CORNER_LEN, color);
    M5.Display.drawFastVLine(x + w - 1, y, CORNER_LEN, color);
    // 左下
    M5.Display.drawFastHLine(x, y + h - 1, CORNER_LEN, color);
    M5.Display.drawFastVLine(x, y + h - CORNER_LEN, CORNER_LEN, color);
    // 右下
    M5.Display.drawFastHLine(x + w - CORNER_LEN, y + h - 1, CORNER_LEN, color);
    M5.Display.drawFastVLine(x + w - 1, y + h - CORNER_LEN, CORNER_LEN, color);
}

// 绘制水平分隔线 (带端点装饰)
void drawHLine(int y, uint16_t color) {
    M5.Display.drawFastHLine(0, y, SCR_W, color);
    // 两端小方块装饰
    M5.Display.fillRect(0, y - 1, 3, 3, color);
    M5.Display.fillRect(SCR_W - 3, y - 1, 3, 3, color);
}

// 绘制扫描线背景效果 (每隔 N 行画一条暗线)
void drawScanlines(int y0, int y1, uint16_t color, int step = 4) {
    for (int y = y0; y < y1; y += step) {
        M5.Display.drawFastHLine(0, y, SCR_W, color);
    }
}

// 绘制闪烁的小光标方块
void drawCursorBlink(int x, int y, uint16_t color, bool on) {
    M5.Display.fillRect(x, y, 6, 8, on ? color : CP_BG);
}

// 顶部 HUD 条
void drawTopHUD(const char* leftText, const char* rightText, uint16_t accentColor) {
    // 半透明条背景
    M5.Display.fillRect(0, 0, SCR_W, HUD_TOP_H, CP_BG_DARK);
    drawHLine(HUD_TOP_H, accentColor);

    M5.Display.setTextSize(1);

    // 左侧标签
    M5.Display.setTextColor(accentColor);
    M5.Display.setCursor(HUD_MARGIN, 4);
    M5.Display.print(leftText);

    // 右侧信息
    if (rightText) {
        int len = strlen(rightText);
        M5.Display.setCursor(SCR_W - len * 6 - HUD_MARGIN, 4);
        M5.Display.setTextColor(CP_CYAN_DIM);
        M5.Display.print(rightText);
    }
}

// 底部 HUD 条
void drawBottomHUD(const char* text, uint16_t accentColor) {
    int y0 = SCR_H - HUD_BOT_H;
    M5.Display.fillRect(0, y0, SCR_W, HUD_BOT_H, CP_BG_DARK);
    drawHLine(y0, accentColor);

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(accentColor);
    M5.Display.setCursor(HUD_MARGIN, y0 + 5);
    M5.Display.print(text);
}

// ─── 画面绘制函数 ───

// 启动画面
void drawBootScreen() {
    M5.Display.fillScreen(CP_BG);
    drawScanlines(0, SCR_H, CP_GRID, 4);
    drawCornerMarks(10, 30, SCR_W - 20, SCR_H - 60, CP_CYAN);

    // 标题
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(CP_CYAN);
    M5.Display.setCursor(56, 80);
    M5.Display.print("UNITV  VIEWER");

    // 细分隔线
    M5.Display.drawFastHLine(56, 100, 208, CP_MAGENTA);

    // 副标题
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(CP_MAG_DIM);
    M5.Display.setCursor(88, 110);
    M5.Display.print("CAMERA FEED SYSTEM");

    // 底部版本
    M5.Display.setTextColor(CP_CYAN_DIM);
    M5.Display.setCursor(112, 200);
    M5.Display.print("INITIALIZING...");
}

// 等待握手画面
void drawWaitingScreen() {
    M5.Display.fillScreen(CP_BG);
    drawScanlines(0, SCR_H, CP_GRID, 4);
    drawCornerMarks(10, 30, SCR_W - 20, SCR_H - 60, CP_MAGENTA);

    drawTopHUD("> HANDSHAKE", "SCAN", CP_MAGENTA);

    // 主提示
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(CP_YELLOW);
    M5.Display.setCursor(28, 90);
    M5.Display.print("AWAITING LINK...");

    // 指令
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(CP_CYAN_DIM);
    M5.Display.setCursor(52, 125);
    M5.Display.print("CONNECT UNITV -> PORT C");

    // 底部 HUD
    drawBottomHUD("[B] RESET", CP_MAG_DIM);
}

// 连接成功画面
void drawConnectedScreen() {
    M5.Display.fillScreen(CP_BG);
    drawScanlines(0, SCR_H, CP_GRID, 4);
    drawCornerMarks(10, 30, SCR_W - 20, SCR_H - 60, CP_GREEN);

    drawTopHUD("> LINKED", "OK", CP_GREEN);

    M5.Display.setTextSize(2);
    M5.Display.setTextColor(CP_GREEN);
    M5.Display.setCursor(44, 95);
    M5.Display.print("UNIT-V ONLINE");

    // 小装饰方块
    M5.Display.fillRect(155, 120, 10, 3, CP_GREEN);

    drawBottomHUD("STREAM START...", CP_GREEN_DIM);
}

// 获取当前模式标签
const char* getModeTag() {
    switch (displayMode) {
        case MODE_ALWAYS_ON:  return "M1:ON";
        case MODE_PIR_ACTIVE: return "M2:PIR";
    }
    return "??";
}

// 黑屏待机画面 (PIR 未触发时)
void drawBlankScreen() {
    M5.Display.fillScreen(CP_BG);
    drawScanlines(0, SCR_H, CP_GRID, 6);
    
    drawTopHUD("> STANDBY", getModeTag(), CP_CYAN_DIM);
    
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(CP_CYAN_DIM);
    M5.Display.setCursor(100, 112);
    M5.Display.print("PIR WAITING...");
    
    // 小动画点
    static uint8_t dotAnim = 0;
    dotAnim = (dotAnim + 1) % 4;
    for (int i = 0; i < dotAnim; i++) {
        M5.Display.fillRect(184 + i * 8, 115, 4, 2, CP_MAGENTA);
    }
    
    drawBottomHUD("[A] ALWAYS ON  [B] PIR", CP_MAG_DIM);
}

// 断线画面
void drawDisconnectedScreen() {
    M5.Display.fillScreen(CP_BG);
    drawScanlines(0, SCR_H, CP_GRID, 4);
    drawCornerMarks(10, 30, SCR_W - 20, SCR_H - 60, CP_MAGENTA);

    drawTopHUD("! SIGNAL LOST", "ERR", CP_MAGENTA);

    M5.Display.setTextSize(2);
    M5.Display.setTextColor(CP_MAGENTA);
    M5.Display.setCursor(24, 90);
    M5.Display.print("LINK SEVERED...");

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(CP_YELLOW);
    M5.Display.setCursor(80, 125);
    M5.Display.print("RE-ESTABLISHING");

    drawBottomHUD("AUTO RECONNECT", CP_MAG_DIM);
}

// 实时视频流的 HUD 叠加层
void drawStreamHUD(float curFps, uint32_t frameSize, uint32_t errors) {
    // ── 顶部 HUD ──
    M5.Display.fillRect(0, 0, SCR_W, HUD_TOP_H, CP_BG_DARK);
    drawHLine(HUD_TOP_H, CP_CYAN_DIM);

    M5.Display.setTextSize(1);

    // 左上：模式标签
    M5.Display.setTextColor(CP_CYAN);
    M5.Display.setCursor(HUD_MARGIN, 4);
    M5.Display.printf("LIVE %s", getModeTag());

    // PIR 指示灯
    uint16_t pirColor = lastPirState ? CP_GREEN : CP_CYAN_DIM;
    M5.Display.fillCircle(110, 7, 3, pirColor);

    // 右上：FPS
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f FPS", curFps);
    int len = strlen(buf);
    M5.Display.setCursor(SCR_W - len * 6 - HUD_MARGIN, 4);
    M5.Display.setTextColor(curFps >= 3.0 ? CP_GREEN : CP_YELLOW);
    M5.Display.print(buf);

    // 小 REC 指示灯 (品红圆点)
    static bool recBlink = false;
    recBlink = !recBlink;
    M5.Display.fillCircle(98, 7, 2, recBlink ? CP_MAGENTA : CP_MAG_DIM);

    // ── 底部 HUD ──
    int y0 = SCR_H - HUD_BOT_H;
    M5.Display.fillRect(0, y0, SCR_W, HUD_BOT_H, CP_BG_DARK);
    drawHLine(y0, CP_CYAN_DIM);

    // 左下：帧大小
    M5.Display.setTextColor(CP_CYAN_DIM);
    M5.Display.setCursor(HUD_MARGIN, y0 + 5);
    snprintf(buf, sizeof(buf), "%uB", frameSize);
    M5.Display.print(buf);

    // 中间：模式按钮提示
    M5.Display.setTextColor(CP_MAG_DIM);
    M5.Display.setCursor(SCR_W / 2 - 54, y0 + 5);
    M5.Display.print("[A]ON  [B]PIR");

    // 右下：错误计数
    if (errors > 0) {
        M5.Display.setTextColor(CP_MAGENTA);
    } else {
        M5.Display.setTextColor(CP_GREEN_DIM);
    }
    snprintf(buf, sizeof(buf), "E:%u", errors);
    len = strlen(buf);
    M5.Display.setCursor(SCR_W - len * 6 - HUD_MARGIN, y0 + 5);
    M5.Display.print(buf);

    // ── 四角角标 (围绕实际图像区域) ──
    drawCornerMarks(IMG_X - 3, IMG_Y - 3, IMG_W + 6, IMG_H + 6, CP_CYAN_DIM);
}

// ─── setup ───

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    
    // 串口调试
    Serial.begin(115200);
    Serial.println("\n=== M5Stack UnitV Viewer ===");
    
    // 初始化 UART2 (Port C)
    UNITV_SERIAL.begin(UNITV_BAUD, SERIAL_8N1, PORT_C_RX, PORT_C_TX);
    UNITV_SERIAL.setRxBufferSize(4096);
    
    Serial.printf("UART2: %d baud on G%d(RX)/G%d(TX)\n", UNITV_BAUD, PORT_C_RX, PORT_C_TX);

    // 初始化 PIR (Port B)
    pinMode(PIR_PIN, INPUT);
    Serial.printf("PIR on G%d (Port B)\n", PIR_PIN);

    // 初始化图像翻转 Sprite (160x120, RGB565)
    imgSprite.createSprite(IMG_W, IMG_H);
    imgSprite.setColorDepth(16);

    // ── 绘制启动画面 ──
    drawBootScreen();
    delay(500);

    // 清空接收缓冲
    while (UNITV_SERIAL.available()) UNITV_SERIAL.read();

    // ── 切换到等待握手画面 ──
    drawWaitingScreen();

    currentState = STATE_HANDSHAKE;
    lastHandshakeTime = millis();
    
    Serial.println("Starting handshake...");
}

// ─── 握手处理 ───

bool doHandshake() {
    static char rxBuf[64];
    static int rxPos = 0;
    static bool sentStop = false;
    
    uint32_t now = millis();
    
    // 每 500ms 发送一次握手信号
    if (now - lastHandshakeTime > 500) {
        // 先发 STOP 让 UnitV 停止可能正在进行的数据流
        if (!sentStop) {
            UNITV_SERIAL.print("STOP\n");
            delay(100);
            // 清空所有缓冲数据（丢弃 JPEG 流）
            while (UNITV_SERIAL.available()) UNITV_SERIAL.read();
            delay(100);
            while (UNITV_SERIAL.available()) UNITV_SERIAL.read();
            sentStop = true;
            Serial.println("Sent: STOP (clear stream)");
            lastHandshakeTime = now;
            return false;
        }
        
        // 同时发 M5_READY 和 START，覆盖 UnitV 的两种等待状态
        // UnitV 可能在：1) 握手等待（等 M5_READY）2) STOP 后等 START
        UNITV_SERIAL.print("M5_READY\n");
        delay(20);
        UNITV_SERIAL.print("START\n");
        Serial.println("Sent: M5_READY + START");
        lastHandshakeTime = now;
        
        // 闪烁 "Searching" 光标
        static bool blink = false;
        blink = !blink;
        M5.Display.fillRect(60, 160, 200, 12, CP_BG);
        M5.Display.setCursor(84, 160);
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(blink ? CP_MAGENTA : CP_MAG_DIM);
        M5.Display.print("> SCANNING...");
        drawCursorBlink(162, 160, CP_MAGENTA, blink);
    }
    
    // 检查是否收到 UNITV_OK 或 JPEG 数据流
    while (UNITV_SERIAL.available()) {
        char c = UNITV_SERIAL.read();
        
        // 检测到可能的 JPEG 帧头（4字节长度头，高位通常为 0x00）
        // 如果收到的是二进制数据，说明 UnitV 已经在发帧了
        if ((uint8_t)c == 0x00 && UNITV_SERIAL.available() >= 3) {
            // 可能是 4 字节长度头的开头，读取后续字节
            uint8_t h1 = UNITV_SERIAL.read();
            uint8_t h2 = UNITV_SERIAL.read();
            uint8_t h3 = UNITV_SERIAL.read();
            uint32_t potentialLen = ((uint32_t)(uint8_t)c << 24) | 
                                    ((uint32_t)h1 << 16) | 
                                    ((uint32_t)h2 << 8) | h3;
            if (potentialLen > 100 && potentialLen < MAX_JPEG_SIZE) {
                // 这看起来是有效的 JPEG 帧长度，UnitV 已经在发帧了！
                Serial.printf("Detected frame stream (len=%u), connecting directly!\n", potentialLen);
                drawConnectedScreen();
                delay(200);
                // 把这4字节放回去（不能 unread，所以我们在 loop 里处理）
                // 清空当前帧数据然后等下一帧
                uint32_t toSkip = potentialLen;
                while (toSkip > 0 && UNITV_SERIAL.available()) {
                    size_t chunk = min((size_t)UNITV_SERIAL.available(), (size_t)min(toSkip, (uint32_t)256));
                    for (size_t i = 0; i < chunk; i++) UNITV_SERIAL.read();
                    toSkip -= chunk;
                    if (toSkip > 0) delay(10);
                }
                rxPos = 0;
                sentStop = false;
                return true;
            }
        }
        
        // 过滤非 ASCII 可打印字符（忽略其他二进制数据）
        if (c < 0x20 && c != '\n' && c != '\r') {
            rxPos = 0;
            continue;
        }
        
        if (c == '\n' || c == '\r') {
            if (rxPos > 0) {
                rxBuf[rxPos] = '\0';
                Serial.printf("Received: %s\n", rxBuf);
                
                if (strstr(rxBuf, "UNITV_OK") != NULL) {
                    Serial.println("UnitV responded! Sending START...");
                    
                    // 绘制连接成功画面
                    drawConnectedScreen();
                    delay(300);
                    
                    // 清空残留数据
                    while (UNITV_SERIAL.available()) UNITV_SERIAL.read();
                    delay(100);
                    
                    // 发送 START 命令
                    UNITV_SERIAL.print("START\n");
                    delay(100);
                    UNITV_SERIAL.print("START\n");  // 发两次确保收到
                    
                    Serial.println("Sent: START");
                    
                    // 清空缓冲，准备接收图像
                    delay(200);
                    while (UNITV_SERIAL.available()) UNITV_SERIAL.read();
                    
                    rxPos = 0;
                    sentStop = false;
                    return true;
                }
                rxPos = 0;
            }
        } else if (rxPos < 63) {
            rxBuf[rxPos++] = c;
        } else {
            // Buffer overflow - likely garbage data, reset
            rxPos = 0;
        }
    }
    
    return false;
}

// ─── 主循环 ───

void loop() {
    M5.update();
    
    // ── 读取 PIR ──
    bool pirActive = digitalRead(PIR_PIN) == HIGH;
    
    // PIR 防抖
    if (pirActive != lastPirState && millis() - lastPirChangeTime > PIR_DEBOUNCE_MS) {
        lastPirChangeTime = millis();
        lastPirState = pirActive;
    }
    
    // ── 按钮切换模式 ──
    if (M5.BtnA.wasPressed()) {
        displayMode = MODE_ALWAYS_ON;
        screenVisible = true;
        Serial.println("Mode: ALWAYS ON");
        // 如果在黑屏状态需要重绘
        if (unitvReady) {
            M5.Display.fillScreen(CP_BG);
            drawStreamHUD(fps, 0, errorCount);
        }
    }
    if (M5.BtnB.wasPressed()) {
        displayMode = MODE_PIR_ACTIVE;
        screenVisible = pirActive;  // 立即根据当前 PIR 状态决定
        Serial.println("Mode: PIR ACTIVE");
        if (!screenVisible && unitvReady) {
            drawBlankScreen();
        } else if (screenVisible && unitvReady) {
            M5.Display.fillScreen(CP_BG);
            drawStreamHUD(fps, 0, errorCount);
        }
    }
    
    // ── PIR 逻辑控制显隐 ──
    if (unitvReady) {
        bool newVisible = screenVisible;
        
        switch (displayMode) {
            case MODE_ALWAYS_ON:
                newVisible = true;
                break;
            case MODE_PIR_ACTIVE:
                newVisible = pirActive;
                break;
        }
        
        // 显隐状态变化时重绘
        if (newVisible != screenVisible) {
            screenVisible = newVisible;
            if (screenVisible) {
                M5.Display.fillScreen(CP_BG);
                drawStreamHUD(fps, 0, errorCount);
            } else {
                drawBlankScreen();
            }
        }
    }
    
    // 握手阶段
    if (currentState == STATE_HANDSHAKE) {
        if (doHandshake()) {
            unitvReady = true;
            currentState = STATE_SYNC;  // 先进入同步模式
            lastFpsTime = millis();
            
            M5.Display.fillScreen(CP_BG);
            drawStreamHUD(0, 0, 0);
            Serial.println("Handshake complete, syncing...");
        }
        return;
    }
    
    // ── 图像接收阶段 ──
    static uint32_t expectedLen = 0;
    static uint32_t receivedLen = 0;
    static uint32_t headerBytes = 0;
    static uint8_t header[4];
    static uint32_t lastDataTime = millis();
    
    // 超时检测 - 8秒无数据重新握手
    if (millis() - lastDataTime > 8000) {
        Serial.println("Timeout! Restarting handshake...");
        currentState = STATE_HANDSHAKE;
        unitvReady = false;
        lastHandshakeTime = millis();
        headerBytes = 0;
        drawDisconnectedScreen();
        return;
    }
    
    while (UNITV_SERIAL.available()) {
        lastDataTime = millis();
        
        // ── SYNC 模式：丢弃数据直到找到 4 字节长度头 + JPEG 头 ──
        if (currentState == STATE_SYNC) {
            // 逐字节读取，寻找合理的 4 字节长度头
            // 策略：读 4 字节，检查是否为合理长度，然后检查后续 2 字节是否为 FF D8
            uint8_t b = UNITV_SERIAL.read();
            header[headerBytes++] = b;
            
            if (headerBytes >= 4) {
                uint32_t len = ((uint32_t)header[0] << 24) | 
                               ((uint32_t)header[1] << 16) | 
                               ((uint32_t)header[2] << 8) | 
                               header[3];
                
                // 合理的 JPEG 大小: 200 ~ 20000 字节
                if (len >= 200 && len < MAX_JPEG_SIZE) {
                    // 等一下看看后续2字节是不是 FF D8 (JPEG SOI)
                    uint32_t waitStart = millis();
                    while (UNITV_SERIAL.available() < 2 && millis() - waitStart < 200) {
                        delay(1);
                    }
                    if (UNITV_SERIAL.available() >= 2) {
                        uint8_t j0 = UNITV_SERIAL.read();
                        uint8_t j1 = UNITV_SERIAL.read();
                        if (j0 == 0xFF && j1 == 0xD8) {
                            // 找到了！这是有效的帧头
                            Serial.printf("[SYNC] Found frame! len=%u\n", len);
                            expectedLen = len;
                            receivedLen = 2;
                            jpegBuf[0] = 0xFF;
                            jpegBuf[1] = 0xD8;
                            currentState = STATE_READ_DATA;
                            headerBytes = 0;
                            continue;
                        }
                    }
                }
                // 没匹配，移位继续找
                header[0] = header[1];
                header[1] = header[2];
                header[2] = header[3];
                headerBytes = 3;
            }
            continue;
        }
        
        // ── 等待帧头 ──
        if (currentState == STATE_WAIT_HEADER) {
            header[headerBytes++] = UNITV_SERIAL.read();
            
            if (headerBytes >= 4) {
                expectedLen = ((uint32_t)header[0] << 24) | 
                              ((uint32_t)header[1] << 16) | 
                              ((uint32_t)header[2] << 8) | 
                              header[3];
                
                if (expectedLen > 0 && expectedLen < MAX_JPEG_SIZE) {
                    currentState = STATE_READ_DATA;
                    receivedLen = 0;
                    headerBytes = 0;
                } else {
                    Serial.printf("Invalid length: %u, resync...\n", expectedLen);
                    errorCount++;
                    // 回到 SYNC 模式重新对齐
                    currentState = STATE_SYNC;
                    header[0] = header[1];
                    header[1] = header[2];
                    header[2] = header[3];
                    headerBytes = 3;
                }
            }
        }
        else if (currentState == STATE_READ_DATA) {
            size_t toRead = min((size_t)UNITV_SERIAL.available(), 
                               (size_t)(expectedLen - receivedLen));
            toRead = min(toRead, (size_t)256);
            
            size_t got = UNITV_SERIAL.readBytes(jpegBuf + receivedLen, toRead);
            receivedLen += got;
            totalBytes += got;
            
            if (receivedLen >= expectedLen) {
                // 验证 JPEG
                if (jpegBuf[0] == 0xFF && jpegBuf[1] == 0xD8) {
                    frameCount++;
                    
                    // 只在画面可见时显示图像
                    if (screenVisible) {
                        // JPEG 解码到 Sprite，再翻转 Y 轴绘制
                        imgSprite.drawJpg(jpegBuf, expectedLen, 0, 0);
                        // pushRotateZoom: 目标中心点, 旋转角度, scaleX, scaleY
                        // scaleX=-1, scaleY=-1 实现 180° 翻转（上下+左右）
                        imgSprite.pushRotateZoom(&M5.Display,
                            IMG_X + IMG_W / 2, IMG_Y + IMG_H / 2,
                            0, -1.0, -1.0);
                    
                        // 每秒更新 FPS 计算
                        uint32_t now = millis();
                        if (now - lastFpsTime >= 1000) {
                            fps = frameCount * 1000.0 / (now - lastFpsTime);
                            lastFpsTime = now;
                            frameCount = 0;
                        }
                    
                        // 每帧都重绘 HUD (防止被图像遮挡)
                        drawStreamHUD(fps, expectedLen, errorCount);
                    }
                } else {
                    Serial.printf("Invalid JPEG: %02X %02X\n", jpegBuf[0], jpegBuf[1]);
                    errorCount++;
                }
                
                // 重置状态
                currentState = STATE_WAIT_HEADER;
                headerBytes = 0;
            }
        }
    }
}
