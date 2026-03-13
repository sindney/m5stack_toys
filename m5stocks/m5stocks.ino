/*
 * M5Stocks - 港股实时行情监控
 * 
 * 功能：
 * - WiFi + 股票代码通过串口配置（PC 端 Python 脚本）
 * - 支持多只股票，按 A/B 按钮切换
 * - 每只股票独立记录一天的价格历史
 * - 赛博朋克绿色 UI 风格
 * - 配置持久化到 NVS Flash
 * 
 * 配置方式：
 *   python config_stocks.py         # 交互式配置
 *   python config_stocks.py --read  # 读取当前配置
 * 
 * 硬件：M5Stack Core (320x240, 3 按钮)
 *   按钮 A: 上一只股票
 *   按钮 B: 下一只股票
 *   按钮 C: 强制刷新数据
 */

#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>

// ============================================================
// 配置相关
// ============================================================
#define MAX_STOCKS       10      // 最多支持 10 只股票
#define MAX_POINTS       480     // 每只股票最多记录 480 分钟数据 (8小时)
#define CODE_LEN         12      // 股票代码最大长度
#define NAME_LEN         20      // 股票名称最大长度
#define SSID_LEN         33      // WiFi SSID 最大长度
#define PASS_LEN         65      // WiFi 密码最大长度

// 串口配置协议命令
#define CMD_PREFIX       "CFG:"
#define CMD_SET_WIFI     "WIFI"
#define CMD_ADD_STOCK    "ADD"
#define CMD_DEL_STOCK    "DEL"
#define CMD_CLEAR_STOCKS "CLEAR"
#define CMD_LIST         "LIST"
#define CMD_SAVE         "SAVE"
#define CMD_REBOOT       "REBOOT"

// 屏幕尺寸
#define SCREEN_W 320
#define SCREEN_H 240

// 赛博朋克绿色主题
#define COLOR_BG        0x0000  // 纯黑
#define COLOR_GREEN     0x07E0  // 亮绿 #00FF00
#define COLOR_DIM       0x0320  // 暗绿
#define COLOR_DARK      0x0120  // 深暗绿
#define COLOR_CYAN      0x07FF  // 青色
#define COLOR_ORANGE    0xFD20  // 橙色警告
#define COLOR_RED       0xF800  // 红色
#define COLOR_YELLOW    0xFFE0  // 黄色
#define COLOR_WHITE     0xFFFF

// UI 布局
#define HEADER_H        22
#define INFO_Y          (HEADER_H + 2)
#define INFO_H          52
#define CHART_Y         (INFO_Y + INFO_H + 2)
#define CHART_H         (SCREEN_H - CHART_Y - 26)
#define FOOTER_Y        (SCREEN_H - 24)

// ============================================================
// 数据结构
// ============================================================
struct StockConfig {
    char code[CODE_LEN];
    char name[NAME_LEN];
};

struct StockData {
    float priceHistory[MAX_POINTS];
    uint16_t timeHistory[MAX_POINTS];
    int nPoints;
    float lastPrice;
    float lastChange;
    float lastPercent;
    float openPrice;       // 今日开盘价
    float highPrice;       // 今日最高
    float lowPrice;        // 今日最低
};

// ============================================================
// 全局变量
// ============================================================
// WiFi 配置
char wifiSSID[SSID_LEN] = "";
char wifiPass[PASS_LEN] = "";

// 股票配置
StockConfig stocks[MAX_STOCKS];
int stockCount = 0;
int currentStock = 0;       // 当前显示的股票索引

// 每只股票的运行数据
StockData stockData[MAX_STOCKS];

// Preferences 持久化
Preferences prefs;

// 状态
bool wifiConnected = false;
bool configMode = false;     // 是否处于配置模式
uint32_t lastFetchTime = 0;
uint32_t lastUITime = 0;
int lastDay = -1;
int frameCount = 0;

// 串口缓冲
char serialBuf[256];
int serialBufPos = 0;

// ============================================================
// 配置存储与读取
// ============================================================
void loadConfig() {
    prefs.begin("m5stocks", true);  // 只读模式
    
    // WiFi
    String s = prefs.getString("ssid", "");
    strncpy(wifiSSID, s.c_str(), SSID_LEN - 1);
    s = prefs.getString("pass", "");
    strncpy(wifiPass, s.c_str(), PASS_LEN - 1);
    
    // 股票列表
    stockCount = prefs.getInt("count", 0);
    if (stockCount > MAX_STOCKS) stockCount = MAX_STOCKS;
    
    for (int i = 0; i < stockCount; i++) {
        String keyCode = "code" + String(i);
        String keyName = "name" + String(i);
        s = prefs.getString(keyCode.c_str(), "");
        strncpy(stocks[i].code, s.c_str(), CODE_LEN - 1);
        s = prefs.getString(keyName.c_str(), "");
        strncpy(stocks[i].name, s.c_str(), NAME_LEN - 1);
    }
    
    prefs.end();
    
    // 如果没有配置任何股票，添加默认的腾讯
    if (stockCount == 0) {
        strncpy(stocks[0].code, "00700", CODE_LEN - 1);
        strncpy(stocks[0].name, "Tencent", NAME_LEN - 1);
        stockCount = 1;
        saveConfig();  // 保存默认配置
    }
    
    Serial.printf("[Config] WiFi SSID: %s\n", wifiSSID);
    Serial.printf("[Config] Stocks: %d\n", stockCount);
    for (int i = 0; i < stockCount; i++) {
        Serial.printf("  [%d] %s (%s)\n", i, stocks[i].code, stocks[i].name);
    }
}

void saveConfig() {
    prefs.begin("m5stocks", false);  // 读写模式
    
    prefs.putString("ssid", wifiSSID);
    prefs.putString("pass", wifiPass);
    prefs.putInt("count", stockCount);
    
    for (int i = 0; i < stockCount; i++) {
        String keyCode = "code" + String(i);
        String keyName = "name" + String(i);
        prefs.putString(keyCode.c_str(), stocks[i].code);
        prefs.putString(keyName.c_str(), stocks[i].name);
    }
    
    // 清理多余的旧数据
    for (int i = stockCount; i < MAX_STOCKS; i++) {
        String keyCode = "code" + String(i);
        String keyName = "name" + String(i);
        prefs.remove(keyCode.c_str());
        prefs.remove(keyName.c_str());
    }
    
    prefs.end();
    Serial.println("[Config] Saved to NVS");
}

// ============================================================
// 串口配置协议
// ============================================================
void processSerialCommand(const char* line) {
    // 所有命令以 "CFG:" 开头
    if (strncmp(line, CMD_PREFIX, 4) != 0) return;
    const char* cmd = line + 4;
    
    if (strncmp(cmd, CMD_SET_WIFI, 4) == 0) {
        // CFG:WIFI ssid password
        const char* p = cmd + 5;
        char newSSID[SSID_LEN] = "";
        char newPass[PASS_LEN] = "";
        
        // 解析 SSID (可能含空格，用引号括起)
        if (*p == '"') {
            p++;
            const char* end = strchr(p, '"');
            if (end) {
                int len = min((int)(end - p), SSID_LEN - 1);
                strncpy(newSSID, p, len);
                p = end + 1;
                while (*p == ' ') p++;
            }
        } else {
            const char* sp = strchr(p, ' ');
            if (sp) {
                int len = min((int)(sp - p), SSID_LEN - 1);
                strncpy(newSSID, p, len);
                p = sp + 1;
            }
        }
        
        // 解析密码
        if (*p == '"') {
            p++;
            const char* end = strchr(p, '"');
            if (end) {
                int len = min((int)(end - p), PASS_LEN - 1);
                strncpy(newPass, p, len);
            }
        } else {
            strncpy(newPass, p, PASS_LEN - 1);
            // 去掉尾部换行
            int l = strlen(newPass);
            while (l > 0 && (newPass[l-1] == '\n' || newPass[l-1] == '\r')) newPass[--l] = 0;
        }
        
        strncpy(wifiSSID, newSSID, SSID_LEN - 1);
        strncpy(wifiPass, newPass, PASS_LEN - 1);
        Serial.printf("OK:WIFI %s\n", wifiSSID);
        
    } else if (strncmp(cmd, CMD_ADD_STOCK, 3) == 0) {
        // CFG:ADD code name
        if (stockCount >= MAX_STOCKS) {
            Serial.println("ERR:MAX_STOCKS");
            return;
        }
        const char* p = cmd + 4;
        char code[CODE_LEN] = "";
        char name[NAME_LEN] = "";
        
        const char* sp = strchr(p, ' ');
        if (sp) {
            int len = min((int)(sp - p), CODE_LEN - 1);
            strncpy(code, p, len);
            p = sp + 1;
            strncpy(name, p, NAME_LEN - 1);
            int l = strlen(name);
            while (l > 0 && (name[l-1] == '\n' || name[l-1] == '\r')) name[--l] = 0;
        } else {
            strncpy(code, p, CODE_LEN - 1);
            int l = strlen(code);
            while (l > 0 && (code[l-1] == '\n' || code[l-1] == '\r')) code[--l] = 0;
            strncpy(name, code, NAME_LEN - 1);
        }
        
        // 检查是否已存在
        for (int i = 0; i < stockCount; i++) {
            if (strcmp(stocks[i].code, code) == 0) {
                Serial.printf("ERR:DUPLICATE %s\n", code);
                return;
            }
        }
        
        strncpy(stocks[stockCount].code, code, CODE_LEN - 1);
        strncpy(stocks[stockCount].name, name, NAME_LEN - 1);
        memset(&stockData[stockCount], 0, sizeof(StockData));
        stockCount++;
        Serial.printf("OK:ADD %s %s (total:%d)\n", code, name, stockCount);
        
    } else if (strncmp(cmd, CMD_DEL_STOCK, 3) == 0) {
        // CFG:DEL code
        const char* p = cmd + 4;
        char code[CODE_LEN] = "";
        strncpy(code, p, CODE_LEN - 1);
        int l = strlen(code);
        while (l > 0 && (code[l-1] == '\n' || code[l-1] == '\r')) code[--l] = 0;
        
        int found = -1;
        for (int i = 0; i < stockCount; i++) {
            if (strcmp(stocks[i].code, code) == 0) {
                found = i;
                break;
            }
        }
        
        if (found >= 0) {
            for (int i = found; i < stockCount - 1; i++) {
                stocks[i] = stocks[i + 1];
                stockData[i] = stockData[i + 1];
            }
            stockCount--;
            if (currentStock >= stockCount) currentStock = max(0, stockCount - 1);
            Serial.printf("OK:DEL %s (total:%d)\n", code, stockCount);
        } else {
            Serial.printf("ERR:NOT_FOUND %s\n", code);
        }
        
    } else if (strncmp(cmd, CMD_CLEAR_STOCKS, 5) == 0) {
        // CFG:CLEAR
        stockCount = 0;
        currentStock = 0;
        Serial.println("OK:CLEAR");
        
    } else if (strncmp(cmd, CMD_LIST, 4) == 0) {
        // CFG:LIST
        Serial.printf("WIFI:%s\n", wifiSSID);
        Serial.printf("STOCKS:%d\n", stockCount);
        for (int i = 0; i < stockCount; i++) {
            Serial.printf("STOCK:%d:%s:%s\n", i, stocks[i].code, stocks[i].name);
        }
        Serial.println("END");
        
    } else if (strncmp(cmd, CMD_SAVE, 4) == 0) {
        // CFG:SAVE
        saveConfig();
        Serial.println("OK:SAVE");
        
    } else if (strncmp(cmd, CMD_REBOOT, 6) == 0) {
        // CFG:REBOOT
        Serial.println("OK:REBOOT");
        delay(500);
        ESP.restart();
        
    } else {
        Serial.printf("ERR:UNKNOWN %s\n", cmd);
    }
}

void checkSerial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (serialBufPos > 0) {
                serialBuf[serialBufPos] = 0;
                processSerialCommand(serialBuf);
                serialBufPos = 0;
            }
        } else if (serialBufPos < (int)sizeof(serialBuf) - 1) {
            serialBuf[serialBufPos++] = c;
        }
    }
}

// ============================================================
// WiFi 连接
// ============================================================
bool connectWiFi(int timeoutSec = 15) {
    if (strlen(wifiSSID) == 0) return false;
    
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(wifiSSID, wifiPass);
    
    int elapsed = 0;
    while (WiFi.status() != WL_CONNECTED && elapsed < timeoutSec * 10) {
        delay(100);
        elapsed++;
    }
    
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    return wifiConnected;
}

// ============================================================
// 时间相关
// ============================================================
bool isTradingTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return false;
    if (timeinfo.tm_wday == 0 || timeinfo.tm_wday == 6) return false;
    int hour = timeinfo.tm_hour;
    if (hour < 9 || hour >= 16) return false;
    return true;
}

uint16_t getMinutesToday() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return 0;
    return timeinfo.tm_hour * 60 + timeinfo.tm_min;
}

String getTimeString() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return "--:--";
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    return String(buf);
}

// ============================================================
// 股票数据获取
// ============================================================
void addPrice(int idx, float price) {
    if (idx < 0 || idx >= stockCount) return;
    StockData& sd = stockData[idx];
    uint16_t nowMin = getMinutesToday();
    
    // 更新最高最低
    if (sd.nPoints == 0) {
        sd.openPrice = price;
        sd.highPrice = price;
        sd.lowPrice = price;
    } else {
        if (price > sd.highPrice) sd.highPrice = price;
        if (price < sd.lowPrice) sd.lowPrice = price;
    }
    
    if (sd.nPoints == 0 || sd.timeHistory[sd.nPoints - 1] != nowMin) {
        if (sd.nPoints < MAX_POINTS) {
            sd.priceHistory[sd.nPoints] = price;
            sd.timeHistory[sd.nPoints] = nowMin;
            sd.nPoints++;
        } else {
            for (int i = 1; i < MAX_POINTS; i++) {
                sd.priceHistory[i - 1] = sd.priceHistory[i];
                sd.timeHistory[i - 1] = sd.timeHistory[i];
            }
            sd.priceHistory[MAX_POINTS - 1] = price;
            sd.timeHistory[MAX_POINTS - 1] = nowMin;
        }
    }
}

bool fetchStock(int idx) {
    if (idx < 0 || idx >= stockCount) return false;
    
    String url = "https://hq.sinajs.cn/list=hk" + String(stocks[idx].code);
    
    HTTPClient http;
    http.begin(url);
    http.setUserAgent("Mozilla/5.0");
    http.addHeader("Referer", "https://finance.sina.com.cn/");
    int httpCode = http.GET();
    
    if (httpCode == 200) {
        String payload = http.getString();
        int eq = payload.indexOf("=\"");
        int end = payload.lastIndexOf("\";");
        if (eq == -1 || end == -1) { http.end(); return false; }
        
        String data_str = payload.substring(eq + 2, end);
        if (data_str.length() < 10) { http.end(); return false; }
        
        char data[512];
        data_str.toCharArray(data, sizeof(data));
        char* fields[20] = {0};
        int field_idx = 0;
        char* tok = strtok(data, ",");
        while (tok && field_idx < 20) {
            fields[field_idx++] = tok;
            tok = strtok(NULL, ",");
        }
        if (field_idx < 11) { http.end(); return false; }
        
        StockData& sd = stockData[idx];
        sd.lastPrice = atof(fields[6]);
        sd.lastChange = atof(fields[7]);
        sd.lastPercent = atof(fields[8]);
        
        addPrice(idx, sd.lastPrice);
        
        http.end();
        return true;
    }
    http.end();
    return false;
}

void fetchAllStocks() {
    for (int i = 0; i < stockCount; i++) {
        fetchStock(i);
        if (i < stockCount - 1) delay(200);  // 避免请求过快
    }
}

// ============================================================
// UI 绘制 - 赛博朋克绿色风格
// ============================================================

// 绘制扫描线效果（纯装饰）
void drawScanlines(int y, int h, int spacing = 4) {
    for (int sy = y; sy < y + h; sy += spacing) {
        M5.Display.drawFastHLine(0, sy, SCREEN_W, COLOR_DARK);
    }
}

// 绘制角落装饰
void drawCorners(int x, int y, int w, int h, int len = 8) {
    // 左上
    M5.Display.drawFastHLine(x, y, len, COLOR_GREEN);
    M5.Display.drawFastVLine(x, y, len, COLOR_GREEN);
    // 右上
    M5.Display.drawFastHLine(x + w - len, y, len, COLOR_GREEN);
    M5.Display.drawFastVLine(x + w - 1, y, len, COLOR_GREEN);
    // 左下
    M5.Display.drawFastHLine(x, y + h - 1, len, COLOR_GREEN);
    M5.Display.drawFastVLine(x, y + h - len, len, COLOR_GREEN);
    // 右下
    M5.Display.drawFastHLine(x + w - len, y + h - 1, len, COLOR_GREEN);
    M5.Display.drawFastVLine(x + w - 1, y + h - len, len, COLOR_GREEN);
}

// 顶部状态栏
void drawHeader() {
    M5.Display.fillRect(0, 0, SCREEN_W, HEADER_H, COLOR_BG);
    
    // 左侧装饰线
    M5.Display.drawFastHLine(0, 0, 40, COLOR_GREEN);
    M5.Display.drawFastHLine(0, HEADER_H - 1, SCREEN_W, COLOR_DIM);
    
    // 时间
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(COLOR_GREEN);
    M5.Display.setCursor(4, 4);
    M5.Display.print(getTimeString());
    
    // 标题
    M5.Display.setTextColor(COLOR_CYAN);
    M5.Display.setCursor(100, 4);
    M5.Display.print("M5STOCKS");
    
    // 股票索引指示器
    M5.Display.setTextColor(COLOR_DIM);
    M5.Display.setCursor(200, 4);
    M5.Display.printf("[%d/%d]", currentStock + 1, stockCount);
    
    // WiFi 状态
    uint16_t wifiColor = wifiConnected ? COLOR_GREEN : COLOR_RED;
    M5.Display.fillCircle(SCREEN_W - 20, HEADER_H / 2, 4, wifiColor);
    
    // 电池
    int bat = M5.Power.getBatteryLevel();
    int barX = SCREEN_W - 55;
    M5.Display.drawRect(barX, 5, 25, 10, COLOR_DIM);
    M5.Display.fillRect(barX + 25, 7, 2, 6, COLOR_DIM);
    int fillW = bat * 23 / 100;
    uint16_t batColor = bat > 20 ? COLOR_GREEN : COLOR_RED;
    M5.Display.fillRect(barX + 1, 6, fillW, 8, batColor);
    
    // 右侧装饰线
    M5.Display.drawFastHLine(SCREEN_W - 40, 0, 40, COLOR_GREEN);
}

// 股票信息区域
void drawStockInfo(int idx) {
    if (idx < 0 || idx >= stockCount) return;
    StockData& sd = stockData[idx];
    
    M5.Display.fillRect(0, INFO_Y, SCREEN_W, INFO_H, COLOR_BG);
    
    // 股票名称和代码
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(COLOR_CYAN);
    M5.Display.setCursor(6, INFO_Y + 2);
    M5.Display.printf("%s", stocks[idx].name);
    
    M5.Display.setTextColor(COLOR_DIM);
    M5.Display.setCursor(6, INFO_Y + 14);
    M5.Display.printf("HK.%s", stocks[idx].code);
    
    // 当前价格 - 大字号
    M5.Display.setTextSize(3);
    uint16_t priceColor = (sd.lastChange >= 0) ? COLOR_GREEN : COLOR_RED;
    M5.Display.setTextColor(priceColor);
    M5.Display.setCursor(100, INFO_Y + 4);
    if (sd.lastPrice > 0) {
        M5.Display.printf("%.2f", sd.lastPrice);
    } else {
        M5.Display.print("---");
    }
    
    // 涨跌信息
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(priceColor);
    M5.Display.setCursor(100, INFO_Y + 32);
    if (sd.lastPrice > 0) {
        char sign = sd.lastChange >= 0 ? '+' : ' ';
        M5.Display.printf("%c%.3f (%c%.2f%%)", sign, sd.lastChange, sign, sd.lastPercent);
    }
    
    // 右侧 开/高/低
    M5.Display.setTextSize(1);
    int rx = 240;
    M5.Display.setTextColor(COLOR_DIM);
    M5.Display.setCursor(rx, INFO_Y + 2);
    M5.Display.print("O:");
    M5.Display.setTextColor(COLOR_GREEN);
    if (sd.openPrice > 0) M5.Display.printf("%.2f", sd.openPrice);
    else M5.Display.print("--");
    
    M5.Display.setTextColor(COLOR_DIM);
    M5.Display.setCursor(rx, INFO_Y + 14);
    M5.Display.print("H:");
    M5.Display.setTextColor(COLOR_GREEN);
    if (sd.highPrice > 0) M5.Display.printf("%.2f", sd.highPrice);
    else M5.Display.print("--");
    
    M5.Display.setTextColor(COLOR_DIM);
    M5.Display.setCursor(rx, INFO_Y + 26);
    M5.Display.print("L:");
    M5.Display.setTextColor(COLOR_GREEN);
    if (sd.lowPrice > 0) M5.Display.printf("%.2f", sd.lowPrice);
    else M5.Display.print("--");
    
    // 分割线
    M5.Display.drawFastHLine(4, INFO_Y + INFO_H - 1, SCREEN_W - 8, COLOR_DIM);
}

// 价格走势图
void drawChart(int idx) {
    if (idx < 0 || idx >= stockCount) return;
    StockData& sd = stockData[idx];
    
    int cx = 4, cy = CHART_Y, cw = SCREEN_W - 8, ch = CHART_H;
    
    // 清空图表区域
    M5.Display.fillRect(0, cy - 2, SCREEN_W, ch + 4, COLOR_BG);
    
    // 边框
    M5.Display.drawRect(cx, cy, cw, ch, COLOR_DIM);
    drawCorners(cx, cy, cw, ch, 6);
    
    // 网格线（虚线效果）
    for (int gy = 1; gy < 4; gy++) {
        int yy = cy + ch * gy / 4;
        for (int gx = cx + 2; gx < cx + cw - 2; gx += 6) {
            M5.Display.drawPixel(gx, yy, COLOR_DARK);
            M5.Display.drawPixel(gx + 1, yy, COLOR_DARK);
        }
    }
    
    if (sd.nPoints < 2) {
        // 没有足够数据时显示提示
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(COLOR_DIM);
        M5.Display.setCursor(cx + cw / 2 - 40, cy + ch / 2 - 4);
        M5.Display.print("Waiting data...");
        return;
    }
    
    // 计算 min/max
    float pmin = sd.priceHistory[0], pmax = sd.priceHistory[0];
    for (int i = 1; i < sd.nPoints; i++) {
        if (sd.priceHistory[i] < pmin) pmin = sd.priceHistory[i];
        if (sd.priceHistory[i] > pmax) pmax = sd.priceHistory[i];
    }
    float prange = pmax - pmin;
    if (prange < 0.01) prange = 1.0;
    
    // 在图表区域绘制走势线
    int graphX = cx + 2;
    int graphY = cy + 2;
    int graphW = cw - 4;
    int graphH = ch - 4;
    
    uint16_t tmin = sd.timeHistory[0];
    uint16_t tmax = sd.timeHistory[sd.nPoints - 1];
    uint16_t trange = tmax - tmin;
    if (trange == 0) trange = 1;
    
    int prevX = -1, prevY = -1;
    for (int i = 0; i < sd.nPoints; i++) {
        int x = graphX + (int)((sd.timeHistory[i] - tmin) * graphW / (float)trange);
        int y = graphY + graphH - (int)((sd.priceHistory[i] - pmin) * graphH / prange);
        y = constrain(y, graphY, graphY + graphH);
        
        if (prevX >= 0) {
            // 绿色走势线，当前价格高于开盘价用绿色，低于用红色
            uint16_t lineColor = (sd.priceHistory[i] >= sd.openPrice) ? COLOR_GREEN : COLOR_RED;
            M5.Display.drawLine(prevX, prevY, x, y, lineColor);
        }
        prevX = x;
        prevY = y;
    }
    
    // Min/Max 标注
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(COLOR_DIM);
    M5.Display.setCursor(cx + 4, cy + 2);
    M5.Display.printf("H:%.2f", pmax);
    M5.Display.setCursor(cx + 4, cy + ch - 12);
    M5.Display.printf("L:%.2f", pmin);
    
    // 右下角显示数据点数
    M5.Display.setCursor(cx + cw - 50, cy + ch - 12);
    M5.Display.printf("%dpts", sd.nPoints);
}

// 底部状态栏
void drawFooter(const char* msg = nullptr) {
    M5.Display.fillRect(0, FOOTER_Y, SCREEN_W, 24, COLOR_BG);
    M5.Display.drawFastHLine(0, FOOTER_Y, SCREEN_W, COLOR_DIM);
    
    M5.Display.setTextSize(1);
    
    // 按钮提示
    M5.Display.setTextColor(COLOR_DIM);
    M5.Display.setCursor(4, FOOTER_Y + 5);
    M5.Display.print("[A]Prev");
    M5.Display.setCursor(130, FOOTER_Y + 5);
    M5.Display.print("[B]Next");
    
    // 右侧状态消息
    if (msg) {
        M5.Display.setTextColor(COLOR_ORANGE);
        M5.Display.setCursor(230, FOOTER_Y + 5);
        M5.Display.print(msg);
    } else {
        M5.Display.setTextColor(COLOR_DIM);
        M5.Display.setCursor(240, FOOTER_Y + 5);
        M5.Display.print("[C]Refresh");
    }
    
    // 底部装饰线
    M5.Display.drawFastHLine(0, SCREEN_H - 1, 30, COLOR_GREEN);
    M5.Display.drawFastHLine(SCREEN_W - 30, SCREEN_H - 1, 30, COLOR_GREEN);
}

// 完整 UI 绘制
void drawMainUI(const char* footerMsg = nullptr) {
    drawHeader();
    drawStockInfo(currentStock);
    drawChart(currentStock);
    drawFooter(footerMsg);
}

// 启动画面
void drawBootScreen(const char* status) {
    M5.Display.fillScreen(COLOR_BG);
    drawCorners(2, 2, SCREEN_W - 4, SCREEN_H - 4, 12);
    
    // 标题
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(COLOR_GREEN);
    M5.Display.setCursor(80, 40);
    M5.Display.print("M5STOCKS");
    
    // 版本
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(COLOR_DIM);
    M5.Display.setCursor(120, 70);
    M5.Display.print("v2.0");
    
    // 装饰线
    M5.Display.drawFastHLine(40, 90, SCREEN_W - 80, COLOR_DIM);
    
    // 状态信息
    M5.Display.setTextColor(COLOR_CYAN);
    M5.Display.setCursor(40, 110);
    M5.Display.print(status);
}

// WiFi 未配置画面
void drawConfigScreen() {
    M5.Display.fillScreen(COLOR_BG);
    drawCorners(2, 2, SCREEN_W - 4, SCREEN_H - 4, 12);
    
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(COLOR_ORANGE);
    M5.Display.setCursor(30, 30);
    M5.Display.print("SETUP REQUIRED");
    
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(COLOR_GREEN);
    M5.Display.setCursor(20, 70);
    M5.Display.print("WiFi not configured.");
    M5.Display.setCursor(20, 90);
    M5.Display.print("Connect USB and run:");
    
    M5.Display.setTextColor(COLOR_CYAN);
    M5.Display.setCursor(20, 115);
    M5.Display.print("python config_stocks.py");
    
    M5.Display.setTextColor(COLOR_DIM);
    M5.Display.setCursor(20, 145);
    M5.Display.print("Listening on serial...");
    
    // 动态指示器
    int dotCount = (frameCount / 5) % 4;
    M5.Display.setTextColor(COLOR_GREEN);
    M5.Display.setCursor(170, 145);
    for (int i = 0; i < dotCount; i++) M5.Display.print(".");
    
    // 底部装饰
    M5.Display.drawFastHLine(0, SCREEN_H - 1, 30, COLOR_GREEN);
    M5.Display.drawFastHLine(SCREEN_W - 30, SCREEN_H - 1, 30, COLOR_GREEN);
}

// WiFi 连接中画面
void drawConnectingScreen(int attempt) {
    M5.Display.fillScreen(COLOR_BG);
    drawCorners(2, 2, SCREEN_W - 4, SCREEN_H - 4, 12);
    
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(COLOR_GREEN);
    M5.Display.setCursor(50, 50);
    M5.Display.print("CONNECTING");
    
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(COLOR_CYAN);
    M5.Display.setCursor(50, 90);
    M5.Display.printf("SSID: %s", wifiSSID);
    
    // 进度条
    int barW = attempt * (SCREEN_W - 80) / 30;
    M5.Display.drawRect(40, 120, SCREEN_W - 80, 10, COLOR_DIM);
    M5.Display.fillRect(41, 121, barW, 8, COLOR_GREEN);
    
    M5.Display.setTextColor(COLOR_DIM);
    M5.Display.setCursor(50, 150);
    M5.Display.printf("Attempt %d / 30", attempt);
}

// ============================================================
// setup & loop
// ============================================================
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    
    Serial.println("\n=== M5Stocks v2.0 ===");
    Serial.println("Serial config ready. Send CFG:LIST to see config.");
    
    // 显示启动画面
    drawBootScreen("Loading config...");
    
    // 加载配置
    loadConfig();
    delay(500);
    
    // 初始化股票数据
    for (int i = 0; i < MAX_STOCKS; i++) {
        memset(&stockData[i], 0, sizeof(StockData));
    }
    
    // 尝试连接 WiFi
    if (strlen(wifiSSID) == 0) {
        // WiFi 未配置，进入配置等待模式
        Serial.println("[WiFi] No SSID configured. Entering config mode.");
        configMode = true;
    } else {
        drawBootScreen("Connecting WiFi...");
        
        WiFi.begin(wifiSSID, wifiPass);
        int attempt = 0;
        while (WiFi.status() != WL_CONNECTED && attempt < 30) {
            delay(500);
            attempt++;
            drawConnectingScreen(attempt);
            checkSerial();  // 连接期间也监听串口配置
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            Serial.println("[WiFi] Connected!");
            Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
            
            drawBootScreen("Syncing time...");
            configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
            delay(2000);
            
            drawBootScreen("Ready!");
            delay(500);
            
            // 首次获取所有股票数据
            M5.Display.fillScreen(COLOR_BG);
            drawMainUI("Loading...");
            fetchAllStocks();
        } else {
            // WiFi 连接失败，进入配置模式
            Serial.println("[WiFi] Connection failed. Entering config mode.");
            configMode = true;
            WiFi.disconnect(true);
        }
    }
    
    if (!configMode) {
        M5.Display.fillScreen(COLOR_BG);
        drawMainUI();
    }
}

void loop() {
    M5.update();
    
    uint32_t now = millis();
    
    // 始终检查串口命令
    checkSerial();
    
    // ---- 配置模式 ----
    if (configMode) {
        if (now - lastUITime >= 200) {
            lastUITime = now;
            drawConfigScreen();
            frameCount++;
        }
        
        // 检查是否已通过串口配置好 WiFi
        if (strlen(wifiSSID) > 0) {
            // 尝试连接
            drawBootScreen("Connecting WiFi...");
            if (connectWiFi(15)) {
                configMode = false;
                Serial.println("[WiFi] Connected! Exiting config mode.");
                
                configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
                delay(2000);
                
                M5.Display.fillScreen(COLOR_BG);
                fetchAllStocks();
                drawMainUI();
            } else {
                // 连接失败，继续等待
                Serial.println("[WiFi] Still cannot connect. Waiting for new config...");
                memset(wifiSSID, 0, SSID_LEN);  // 清空以便重新配置
            }
        }
        return;
    }
    
    // ---- 正常运行模式 ----
    
    // 检查日期变化，清空历史数据
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        int today = timeinfo.tm_yday;
        if (lastDay != -1 && lastDay != today) {
            for (int i = 0; i < stockCount; i++) {
                stockData[i].nPoints = 0;
                stockData[i].openPrice = 0;
                stockData[i].highPrice = 0;
                stockData[i].lowPrice = 0;
            }
            Serial.println("[Data] New day, cleared history.");
        }
        lastDay = today;
    }
    
    // 按钮处理
    bool needRedraw = false;
    
    if (M5.BtnA.wasPressed() && stockCount > 1) {
        // A 按钮：上一只
        currentStock = (currentStock - 1 + stockCount) % stockCount;
        Serial.printf("[UI] Switch to stock %d: %s\n", currentStock, stocks[currentStock].code);
        needRedraw = true;
    }
    
    if (M5.BtnB.wasPressed() && stockCount > 1) {
        // B 按钮：下一只
        currentStock = (currentStock + 1) % stockCount;
        Serial.printf("[UI] Switch to stock %d: %s\n", currentStock, stocks[currentStock].code);
        needRedraw = true;
    }
    
    if (M5.BtnC.wasPressed()) {
        // C 按钮：强制刷新
        Serial.println("[Fetch] Manual refresh");
        drawMainUI("Fetching...");
        fetchAllStocks();
        lastFetchTime = now;
        needRedraw = true;
    }
    
    // 定时获取数据（交易时间内每 5 秒）
    if (isTradingTime() && (now - lastFetchTime >= 5000)) {
        lastFetchTime = now;
        fetchStock(currentStock);  // 优先刷新当前查看的
        
        // 每 60 秒刷新其他股票
        static uint32_t lastFullFetch = 0;
        if (now - lastFullFetch >= 60000) {
            lastFullFetch = now;
            fetchAllStocks();
        }
        needRedraw = true;
    }
    
    // WiFi 断线重连
    if (WiFi.status() != WL_CONNECTED) {
        wifiConnected = false;
        static uint32_t lastReconnect = 0;
        if (now - lastReconnect >= 30000) {
            lastReconnect = now;
            Serial.println("[WiFi] Reconnecting...");
            connectWiFi(10);
        }
    } else {
        wifiConnected = true;
    }
    
    // UI 刷新（最快 500ms 一次）
    if (needRedraw || (now - lastUITime >= 1000)) {
        lastUITime = now;
        frameCount++;
        
        const char* footer = nullptr;
        if (!isTradingTime()) {
            footer = "Closed";
        }
        
        drawMainUI(footer);
    }
    
    delay(50);  // 降低 CPU 使用率
}
