/*
 * AtomS3R PC Monitor - BLE 版本
 * 通过 BLE 接收 PC 状态数据并显示
 * 
 * 功能：
 * - BLE Server 等待 PC 连接
 * - 显示 CPU、GPU、MEM 使用率
 * - 科技感绿色 UI
 */

#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_mac.h>

// BLE 服务和特征 UUID
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// 设备名前缀
#define DEVICE_NAME_PREFIX "AtomS3R-Mon"

// 屏幕尺寸
#define SCREEN_W 128
#define SCREEN_H 128

// 颜色定义 - 科技感绿色主题
#define COLOR_BG      0x0000  // 纯黑背景
#define COLOR_GREEN   0x07E0  // 亮绿色
#define COLOR_DIM     0x0320  // 暗绿色
#define COLOR_CYAN    0x07FF  // 青色
#define COLOR_ORANGE  0xFD20  // 橙色（高负载警告）
#define COLOR_RED     0xF800  // 红色（过载警告）

// 历史数据点数（用于折线图）
#define HISTORY_SIZE 30

// 系统状态数据
struct SystemStats {
    uint8_t cpuPercent;
    uint8_t gpuPercent;
    uint8_t memPercent;
    uint8_t gpuTemp;
    uint8_t cpuTemp;
} stats = {0, 0, 0, 0, 0};

// 历史记录
uint8_t cpuHistory[HISTORY_SIZE] = {0};
uint8_t gpuHistory[HISTORY_SIZE] = {0};
int historyIndex = 0;

// BLE 状态
BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;
uint32_t lastUpdateTime = 0;
uint32_t lastDataTime = 0;

// 动画帧计数
int frameCount = 0;

// 设备名（含唯一 ID）
char deviceName[20];

// 屏幕旋转 (0, 1, 2, 3 对应 0°, 90°, 180°, 270°)
uint8_t screenRotation = 0;

// BLE 回调
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("BLE Client Connected!");
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("BLE Client Disconnected!");
    }
};

class CharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        String valueStr = pCharacteristic->getValue();
        if (valueStr.length() >= 5) {
            // 数据格式: CPU%, GPU%, MEM%, GPU_TEMP, CPU_TEMP
            const uint8_t* value = (const uint8_t*)valueStr.c_str();
            stats.cpuPercent = value[0];
            stats.gpuPercent = value[1];
            stats.memPercent = value[2];
            stats.gpuTemp = value[3];
            stats.cpuTemp = value[4];
            
            // 更新历史
            cpuHistory[historyIndex] = stats.cpuPercent;
            gpuHistory[historyIndex] = stats.gpuPercent;
            historyIndex = (historyIndex + 1) % HISTORY_SIZE;
            
            lastDataTime = millis();
            
            Serial.printf("Data: CPU=%d%% GPU=%d%% MEM=%d%% GPU_T=%d° CPU_T=%d°\n",
                         stats.cpuPercent, stats.gpuPercent, stats.memPercent,
                         stats.gpuTemp, stats.cpuTemp);
        }
    }
};

void setup() {
    // 初始化 M5
    auto cfg = M5.config();
    M5.begin(cfg);
    
    Serial.begin(115200);
    Serial.println("\n=== AtomS3R PC Monitor ===");
    
    // 显示设置
    M5.Display.setRotation(0);
    M5.Display.fillScreen(COLOR_BG);
    M5.Display.setTextColor(COLOR_GREEN);
    M5.Display.setTextSize(1);
    
    // 生成唯一设备名（使用 MAC 地址后 4 位）
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(deviceName, sizeof(deviceName), "%s-%02X%02X", 
             DEVICE_NAME_PREFIX, mac[4], mac[5]);
    
    Serial.printf("Device name: %s\n", deviceName);
    
    // 启动画面
    drawBootScreen();
    
    // 初始化 BLE
    Serial.println("Initializing BLE...");
    BLEDevice::init(deviceName);
    
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    
    BLEService* pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    
    pCharacteristic->setCallbacks(new CharacteristicCallbacks());
    pCharacteristic->addDescriptor(new BLE2902());
    
    pService->start();
    
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    
    Serial.println("BLE Advertising started. Waiting for connection...");
    Serial.printf("Service UUID: %s\n", SERVICE_UUID);
    
    delay(1000);
    M5.Display.fillScreen(COLOR_BG);
}

void loop() {
    M5.update();
    
    uint32_t now = millis();
    
    // 检测按钮按下 - 旋转屏幕 90°
    if (M5.BtnA.wasPressed()) {
        screenRotation = (screenRotation + 1) % 4;
        M5.Display.setRotation(screenRotation);
        M5.Display.fillScreen(COLOR_BG);
        Serial.printf("Screen rotation: %d° (rotation=%d)\n", screenRotation * 90, screenRotation);
    }
    
    // 检查连接状态变化
    if (!deviceConnected && oldDeviceConnected) {
        delay(500);
        pServer->startAdvertising();
        Serial.println("Restarting advertising...");
        oldDeviceConnected = deviceConnected;
    }
    
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
    }
    
    // 每 100ms 更新显示
    if (now - lastUpdateTime >= 100) {
        lastUpdateTime = now;
        
        if (deviceConnected && (now - lastDataTime < 5000)) {
            // 已连接且有数据
            drawMainUI();
        } else if (deviceConnected) {
            // 已连接但无数据
            drawWaitingData();
        } else {
            // 未连接
            drawWaitingConnection();
        }
        
        frameCount++;
    }
}

// 绘制启动画面
void drawBootScreen() {
    M5.Display.fillScreen(COLOR_BG);
    
    // 外框
    M5.Display.drawRect(2, 2, 124, 124, COLOR_DIM);
    M5.Display.drawRect(4, 4, 120, 120, COLOR_GREEN);
    
    // 标题
    M5.Display.setTextColor(COLOR_GREEN);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(20, 30);
    M5.Display.print("AtomS3R");
    
    M5.Display.setTextSize(1);
    M5.Display.setCursor(15, 50);
    M5.Display.print("PC MONITOR");
    
    // 版本
    M5.Display.setTextColor(COLOR_DIM);
    M5.Display.setCursor(40, 75);
    M5.Display.print("v1.0");
    
    // 加载动画
    for (int i = 0; i < 100; i += 5) {
        int barWidth = i * 80 / 100;
        M5.Display.fillRect(24, 95, barWidth, 4, COLOR_GREEN);
        M5.Display.drawRect(24, 95, 80, 4, COLOR_DIM);
        delay(30);
    }
    
    M5.Display.setCursor(25, 110);
    M5.Display.setTextColor(COLOR_CYAN);
    M5.Display.print("BLE Ready");
}

// 绘制等待连接画面
void drawWaitingConnection() {
    static int dotCount = 0;
    
    M5.Display.fillScreen(COLOR_BG);
    
    // 外框装饰
    drawCornerDecorations();
    
    // 标题
    M5.Display.setTextColor(COLOR_GREEN);
    M5.Display.setCursor(8, 8);
    M5.Display.print("PC MONITOR");
    
    // BLE 图标区域
    int cx = 64, cy = 55;
    
    // 动态圆环
    int radius = 20 + (frameCount % 10);
    M5.Display.drawCircle(cx, cy, radius, COLOR_DIM);
    M5.Display.drawCircle(cx, cy, 15, COLOR_GREEN);
    
    // BLE 符号
    M5.Display.setTextColor(COLOR_CYAN);
    M5.Display.setCursor(cx - 6, cy - 4);
    M5.Display.print("BT");
    
    // 状态文字
    M5.Display.setTextColor(COLOR_GREEN);
    M5.Display.setCursor(20, 90);
    M5.Display.print("Waiting");
    
    // 动态点
    dotCount = (dotCount + 1) % 4;
    M5.Display.setCursor(68, 90);
    for (int i = 0; i < dotCount; i++) {
        M5.Display.print(".");
    }
    
    // 设备名（显示实际生成的名字）
    M5.Display.setTextColor(COLOR_DIM);
    // 居中显示设备名
    int nameLen = strlen(deviceName);
    int nameX = (128 - nameLen * 6) / 2;
    M5.Display.setCursor(nameX, 110);
    M5.Display.print(deviceName);
}

// 绘制等待数据画面
void drawWaitingData() {
    M5.Display.fillScreen(COLOR_BG);
    drawCornerDecorations();
    
    M5.Display.setTextColor(COLOR_CYAN);
    M5.Display.setCursor(15, 8);
    M5.Display.print("CONNECTED");
    
    M5.Display.setTextColor(COLOR_GREEN);
    M5.Display.setCursor(10, 55);
    M5.Display.print("Waiting for");
    M5.Display.setCursor(25, 70);
    M5.Display.print("data...");
}

// 绘制主 UI
void drawMainUI() {
    M5.Display.fillScreen(COLOR_BG);
    
    // 顶部标题栏
    drawHeader();
    
    // CPU 区域 (y: 18-48)
    drawCPUSection(0, 18, 128, 35);
    
    // 分割线
    M5.Display.drawFastHLine(5, 53, 118, COLOR_DIM);
    
    // MEM 区域 (y: 55-80)
    drawMEMSection(0, 55, 128, 28);
    
    // 分割线
    M5.Display.drawFastHLine(5, 83, 118, COLOR_DIM);
    
    // GPU 区域 (y: 85-125)
    drawGPUSection(0, 85, 128, 40);
}

// 绘制顶部标题栏
void drawHeader() {
    // 左上角装饰
    M5.Display.drawLine(0, 0, 10, 0, COLOR_GREEN);
    M5.Display.drawLine(0, 0, 0, 10, COLOR_GREEN);
    
    // 右上角装饰
    M5.Display.drawLine(117, 0, 127, 0, COLOR_GREEN);
    M5.Display.drawLine(127, 0, 127, 10, COLOR_GREEN);
    
    // 标题
    M5.Display.setTextColor(COLOR_DIM);
    M5.Display.setCursor(3, 4);
    M5.Display.print("SYS_STAT");
    
    // 连接状态指示灯
    uint16_t statusColor = (frameCount % 20 < 10) ? COLOR_GREEN : COLOR_DIM;
    M5.Display.fillCircle(120, 7, 3, statusColor);
}

// 绘制 CPU 区域
void drawCPUSection(int x, int y, int w, int h) {
    // 标签
    M5.Display.setTextColor(COLOR_CYAN);
    M5.Display.setCursor(x + 3, y + 2);
    M5.Display.print("CPU");
    
    // 百分比 - 大字
    uint16_t valueColor = getLoadColor(stats.cpuPercent);
    M5.Display.setTextColor(valueColor);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(x + 3, y + 12);
    M5.Display.printf("%2d", stats.cpuPercent);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(x + 28, y + 20);
    M5.Display.print("%");
    
    // 温度
    if (stats.cpuTemp > 0) {
        M5.Display.setTextColor(getTempColor(stats.cpuTemp));
        M5.Display.setCursor(x + 3, y + 28);
        M5.Display.printf("%d", stats.cpuTemp);
        M5.Display.setTextColor(COLOR_DIM);
        M5.Display.print("C");
    }
    
    // 迷你折线图 (右侧)
    drawMiniGraph(x + 45, y + 5, 78, 28, cpuHistory, HISTORY_SIZE, historyIndex, COLOR_GREEN);
}

// 绘制 MEM 区域
void drawMEMSection(int x, int y, int w, int h) {
    // 标签
    M5.Display.setTextColor(COLOR_CYAN);
    M5.Display.setCursor(x + 3, y + 2);
    M5.Display.print("MEM");
    
    // 百分比
    uint16_t valueColor = getLoadColor(stats.memPercent);
    M5.Display.setTextColor(valueColor);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(x + 3, y + 12);
    M5.Display.printf("%2d", stats.memPercent);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(x + 28, y + 20);
    M5.Display.print("%");
    
    // 进度条
    drawProgressBar(x + 45, y + 8, 78, 12, stats.memPercent);
}

// 绘制 GPU 区域
void drawGPUSection(int x, int y, int w, int h) {
    // 标签
    M5.Display.setTextColor(COLOR_CYAN);
    M5.Display.setCursor(x + 3, y + 2);
    M5.Display.print("GPU");
    
    // 利用率百分比
    uint16_t valueColor = getLoadColor(stats.gpuPercent);
    M5.Display.setTextColor(valueColor);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(x + 3, y + 12);
    M5.Display.printf("%2d", stats.gpuPercent);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(x + 28, y + 20);
    M5.Display.print("%");
    
    // 温度
    if (stats.gpuTemp > 0) {
        M5.Display.setTextColor(getTempColor(stats.gpuTemp));
        M5.Display.setCursor(x + 3, y + 30);
        M5.Display.printf("%d", stats.gpuTemp);
        M5.Display.setTextColor(COLOR_DIM);
        M5.Display.print("C");
    }
    
    // 迷你折线图 (右侧)
    drawMiniGraph(x + 45, y + 5, 78, 32, gpuHistory, HISTORY_SIZE, historyIndex, COLOR_GREEN);
}

// 绘制迷你折线图
void drawMiniGraph(int x, int y, int w, int h, uint8_t* data, int dataSize, int currentIndex, uint16_t color) {
    // 边框
    M5.Display.drawRect(x, y, w, h, COLOR_DIM);
    
    // 网格线
    for (int i = 1; i < 4; i++) {
        int gy = y + (h * i / 4);
        for (int gx = x + 2; gx < x + w - 2; gx += 4) {
            M5.Display.drawPixel(gx, gy, COLOR_DIM);
        }
    }
    
    // 折线
    int graphW = w - 4;
    int graphH = h - 4;
    int graphX = x + 2;
    int graphY = y + 2;
    
    for (int i = 0; i < dataSize - 1; i++) {
        int idx1 = (currentIndex + i) % dataSize;
        int idx2 = (currentIndex + i + 1) % dataSize;
        
        int x1 = graphX + (i * graphW / (dataSize - 1));
        int x2 = graphX + ((i + 1) * graphW / (dataSize - 1));
        int y1 = graphY + graphH - (data[idx1] * graphH / 100);
        int y2 = graphY + graphH - (data[idx2] * graphH / 100);
        
        // 限制在范围内
        y1 = constrain(y1, graphY, graphY + graphH);
        y2 = constrain(y2, graphY, graphY + graphH);
        
        M5.Display.drawLine(x1, y1, x2, y2, color);
    }
}

// 绘制进度条
void drawProgressBar(int x, int y, int w, int h, int percent) {
    // 外框
    M5.Display.drawRect(x, y, w, h, COLOR_DIM);
    
    // 内部填充
    int fillW = (w - 4) * percent / 100;
    uint16_t fillColor = getLoadColor(percent);
    
    // 分段显示
    int segmentW = 5;
    int segments = fillW / (segmentW + 1);
    
    for (int i = 0; i < segments; i++) {
        int sx = x + 2 + i * (segmentW + 1);
        M5.Display.fillRect(sx, y + 2, segmentW, h - 4, fillColor);
    }
    
    // 显示数值在条内
    M5.Display.setTextColor(COLOR_BG);
    M5.Display.setCursor(x + w/2 - 8, y + 2);
    // M5.Display.printf("%d%%", percent);
}

// 根据负载获取颜色
uint16_t getLoadColor(int percent) {
    if (percent >= 90) return COLOR_RED;
    if (percent >= 70) return COLOR_ORANGE;
    return COLOR_GREEN;
}

// 根据温度获取颜色
uint16_t getTempColor(int temp) {
    if (temp >= 85) return COLOR_RED;
    if (temp >= 70) return COLOR_ORANGE;
    return COLOR_GREEN;
}

// 绘制角落装饰
void drawCornerDecorations() {
    // 四角装饰
    int len = 8;
    // 左上
    M5.Display.drawFastHLine(0, 0, len, COLOR_GREEN);
    M5.Display.drawFastVLine(0, 0, len, COLOR_GREEN);
    // 右上
    M5.Display.drawFastHLine(SCREEN_W - len, 0, len, COLOR_GREEN);
    M5.Display.drawFastVLine(SCREEN_W - 1, 0, len, COLOR_GREEN);
    // 左下
    M5.Display.drawFastHLine(0, SCREEN_H - 1, len, COLOR_GREEN);
    M5.Display.drawFastVLine(0, SCREEN_H - len, len, COLOR_GREEN);
    // 右下
    M5.Display.drawFastHLine(SCREEN_W - len, SCREEN_H - 1, len, COLOR_GREEN);
    M5.Display.drawFastVLine(SCREEN_W - 1, SCREEN_H - len, len, COLOR_GREEN);
}
