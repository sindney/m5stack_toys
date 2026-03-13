/*
 * M5Stack UnitV Camera Viewer
 * 
 * 功能：接收 UnitV 摄像头图像并显示
 * 协议：4字节长度头（大端）+ JPEG 数据
 * 握手：M5_READY -> UNITV_OK -> START -> 数据流
 * 
 * 使用 M5Unified 库（兼容 ESP32 SDK 3.x）
 */

#include <M5Unified.h>

// UART 配置
#define UNITV_SERIAL Serial2
#define UNITV_BAUD 115200

// Port C 引脚 (UART2 默认)
#define PORT_C_RX 16
#define PORT_C_TX 17

// 缓冲区
#define MAX_JPEG_SIZE 20000
uint8_t jpegBuf[MAX_JPEG_SIZE];

// 状态机
enum State {
    STATE_HANDSHAKE,    // 握手阶段
    STATE_WAIT_HEADER,  // 等待帧头
    STATE_READ_DATA     // 读取数据
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

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    
    // 屏幕初始化
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(2);
    
    // 显示启动信息
    M5.Display.setCursor(40, 100);
    M5.Display.println("UnitV Camera Viewer");
    M5.Display.setCursor(60, 130);
    M5.Display.println("Initializing...");
    
    // 串口调试
    Serial.begin(115200);
    Serial.println("\n=== M5Stack UnitV Viewer ===");
    
    // 初始化 UART2 (Port C)
    UNITV_SERIAL.begin(UNITV_BAUD, SERIAL_8N1, PORT_C_RX, PORT_C_TX);
    UNITV_SERIAL.setRxBufferSize(4096);
    
    Serial.printf("UART2: %d baud on G%d(RX)/G%d(TX)\n", UNITV_BAUD, PORT_C_RX, PORT_C_TX);
    
    delay(500);
    
    // 清空接收缓冲
    while (UNITV_SERIAL.available()) UNITV_SERIAL.read();
    
    // 显示等待握手
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(30, 110);
    M5.Display.setTextColor(TFT_YELLOW);
    M5.Display.println("Waiting for UnitV...");
    M5.Display.setCursor(50, 140);
    M5.Display.setTextSize(1);
    M5.Display.println("Connect UnitV to Port C");
    
    currentState = STATE_HANDSHAKE;
    lastHandshakeTime = millis();
    
    Serial.println("Starting handshake...");
}

// 握手处理
bool doHandshake() {
    static char rxBuf[64];
    static int rxPos = 0;
    
    uint32_t now = millis();
    
    // 每 500ms 发送一次 M5_READY
    if (now - lastHandshakeTime > 500) {
        UNITV_SERIAL.print("M5_READY\n");
        Serial.println("Sent: M5_READY");
        lastHandshakeTime = now;
        
        // 更新屏幕闪烁
        static bool blink = false;
        blink = !blink;
        M5.Display.fillRect(0, 180, 320, 20, TFT_BLACK);
        M5.Display.setCursor(100, 180);
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(blink ? TFT_YELLOW : TFT_DARKGREY);
        M5.Display.print("Searching...");
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
                    
                    // 显示连接成功
                    M5.Display.fillScreen(TFT_BLACK);
                    M5.Display.setCursor(60, 110);
                    M5.Display.setTextSize(2);
                    M5.Display.setTextColor(TFT_GREEN);
                    M5.Display.println("UnitV Connected!");
                    
                    delay(300);
                    
                    // 发送 START 命令
                    UNITV_SERIAL.print("START\n");
                    delay(100);
                    UNITV_SERIAL.print("START\n");  // 发两次确保收到
                    
                    Serial.println("Sent: START");
                    
                    // 清空缓冲，准备接收图像
                    while (UNITV_SERIAL.available()) UNITV_SERIAL.read();
                    
                    rxPos = 0;
                    return true;  // 握手成功
                }
                rxPos = 0;
            }
        } else if (rxPos < 63) {
            rxBuf[rxPos++] = c;
        }
    }
    
    return false;  // 握手中
}

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
        
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setCursor(30, 110);
        M5.Display.setTextColor(TFT_YELLOW);
        M5.Display.setTextSize(2);
        M5.Display.println("Waiting for UnitV...");
        
        lastHandshakeTime = millis();
    }
    
    // 握手阶段
    if (currentState == STATE_HANDSHAKE) {
        if (doHandshake()) {
            unitvReady = true;
            currentState = STATE_WAIT_HEADER;
            lastFpsTime = millis();
            
            M5.Display.fillScreen(TFT_BLACK);
            Serial.println("Handshake complete, waiting for frames...");
        }
        return;
    }
    
    // 图像接收阶段
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
        
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setCursor(30, 110);
        M5.Display.setTextColor(TFT_YELLOW);
        M5.Display.setTextSize(2);
        M5.Display.println("Connection lost...");
        M5.Display.setCursor(30, 140);
        M5.Display.println("Reconnecting...");
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
                    // 无效长度，可能是同步丢失
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
            toRead = min(toRead, (size_t)256);  // 每次最多读 256 字节
            
            size_t got = UNITV_SERIAL.readBytes(jpegBuf + receivedLen, toRead);
            receivedLen += got;
            totalBytes += got;
            
            if (receivedLen >= expectedLen) {
                // 验证 JPEG
                if (jpegBuf[0] == 0xFF && jpegBuf[1] == 0xD8) {
                    // 显示图像
                    M5.Display.drawJpg(jpegBuf, expectedLen, 0, 0);
                    frameCount++;
                    
                    // 计算 FPS
                    uint32_t now = millis();
                    if (now - lastFpsTime >= 1000) {
                        fps = frameCount * 1000.0 / (now - lastFpsTime);
                        lastFpsTime = now;
                        frameCount = 0;
                        
                        // 显示状态栏
                        M5.Display.fillRect(0, 220, 320, 20, TFT_BLACK);
                        M5.Display.setCursor(5, 222);
                        M5.Display.setTextSize(1);
                        M5.Display.setTextColor(TFT_GREEN);
                        M5.Display.printf("FPS: %.1f  Size: %uB  Err: %u", 
                                          fps, expectedLen, errorCount);
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
