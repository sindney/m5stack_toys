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

// 缓冲区
#define MAX_JPEG_SIZE 20000
uint8_t jpegBuf[MAX_JPEG_SIZE];

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

    // 左上：模式
    M5.Display.setTextColor(CP_CYAN);
    M5.Display.setCursor(HUD_MARGIN, 4);
    M5.Display.print("LIVE");

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
    M5.Display.fillCircle(38, 7, 3, recBlink ? CP_MAGENTA : CP_MAG_DIM);

    // ── 底部 HUD ──
    int y0 = SCR_H - HUD_BOT_H;
    M5.Display.fillRect(0, y0, SCR_W, HUD_BOT_H, CP_BG_DARK);
    drawHLine(y0, CP_CYAN_DIM);

    // 左下：帧大小
    M5.Display.setTextColor(CP_CYAN_DIM);
    M5.Display.setCursor(HUD_MARGIN, y0 + 5);
    snprintf(buf, sizeof(buf), "%uB", frameSize);
    M5.Display.print(buf);

    // 中间：分隔点
    M5.Display.fillCircle(SCR_W / 2, y0 + 9, 1, CP_CYAN_DIM);

    // 右下：错误计数
    if (errors > 0) {
        M5.Display.setTextColor(CP_MAGENTA);
    } else {
        M5.Display.setTextColor(CP_GREEN_DIM);
    }
    snprintf(buf, sizeof(buf), "ERR:%u", errors);
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
    
    uint32_t now = millis();
    
    // 每 500ms 发送一次 M5_READY
    if (now - lastHandshakeTime > 500) {
        UNITV_SERIAL.print("M5_READY\n");
        Serial.println("Sent: M5_READY");
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
    
    // 检查是否收到 UNITV_OK
    while (UNITV_SERIAL.available()) {
        char c = UNITV_SERIAL.read();
        if (c == '\n' || c == '\r') {
            if (rxPos > 0) {
                rxBuf[rxPos] = '\0';
                Serial.printf("Received: %s\n", rxBuf);
                
                if (strstr(rxBuf, "UNITV_OK") != NULL) {
                    Serial.println("UnitV responded! Sending START...");
                    
                    // 绘制连接成功画面
                    drawConnectedScreen();
                    delay(300);
                    
                    // 发送 START 命令
                    UNITV_SERIAL.print("START\n");
                    delay(100);
                    UNITV_SERIAL.print("START\n");  // 发两次确保收到
                    
                    Serial.println("Sent: START");
                    
                    // 清空缓冲，准备接收图像
                    while (UNITV_SERIAL.available()) UNITV_SERIAL.read();
                    
                    rxPos = 0;
                    return true;
                }
                rxPos = 0;
            }
        } else if (rxPos < 63) {
            rxBuf[rxPos++] = c;
        }
    }
    
    return false;
}

// ─── 主循环 ───

void loop() {
    M5.update();
    
    // 按钮 B: 重置/重新握手
    if (M5.BtnB.wasPressed()) {
        Serial.println("Reset requested");
        currentState = STATE_HANDSHAKE;
        unitvReady = false;
        frameCount = 0;
        errorCount = 0;
        
        // 发送停止命令
        UNITV_SERIAL.print("STOP\n");
        delay(100);
        
        // 清空缓冲
        while (UNITV_SERIAL.available()) UNITV_SERIAL.read();
        
        drawWaitingScreen();
        lastHandshakeTime = millis();
    }
    
    // 握手阶段
    if (currentState == STATE_HANDSHAKE) {
        if (doHandshake()) {
            unitvReady = true;
            currentState = STATE_WAIT_HEADER;
            lastFpsTime = millis();
            
            M5.Display.fillScreen(CP_BG);
            // 绘制初始 HUD 框架
            drawStreamHUD(0, 0, 0);
            Serial.println("Handshake complete, waiting for frames...");
        }
        return;
    }
    
    // ── 图像接收阶段 ──
    static uint32_t expectedLen = 0;
    static uint32_t receivedLen = 0;
    static uint32_t headerBytes = 0;
    static uint8_t header[4];
    static uint32_t lastDataTime = millis();
    
    // 超时检测 - 5秒无数据重新握手
    if (millis() - lastDataTime > 5000) {
        Serial.println("Timeout! Restarting handshake...");
        currentState = STATE_HANDSHAKE;
        unitvReady = false;
        lastHandshakeTime = millis();
        
        drawDisconnectedScreen();
        return;
    }
    
    while (UNITV_SERIAL.available()) {
        lastDataTime = millis();
        
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
                } else {
                    Serial.printf("Invalid length: %u, resync...\n", expectedLen);
                    errorCount++;
                    
                    // 尝试重新同步：移位查找
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
                    // 显示图像 (居中到 HUD 区域之间)
                    M5.Display.drawJpg(jpegBuf, expectedLen, IMG_X, IMG_Y);
                    frameCount++;
                    
                    // 每秒更新 FPS 计算
                    uint32_t now = millis();
                    if (now - lastFpsTime >= 1000) {
                        fps = frameCount * 1000.0 / (now - lastFpsTime);
                        lastFpsTime = now;
                        frameCount = 0;
                    }
                    
                    // 每帧都重绘 HUD (防止被图像遮挡)
                    drawStreamHUD(fps, expectedLen, errorCount);
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
