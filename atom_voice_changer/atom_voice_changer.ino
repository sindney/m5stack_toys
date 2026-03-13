/*
 * AtomS3R Voice Changer - Cyberpunk Edition v5.0
 * 
 * v5.0: New interaction model + louder speaker
 *   - TAP screen to cycle: OFF -> FX1 -> FX2 -> ... -> FX6 -> OFF
 *   - In FX mode: auto-listen, detect voice, record, transform, play, loop
 *   - DAC volume raised to 100% (was 65%)
 *   - No more long-press; single tap cycles through modes
 *
 * v4.2: Fix mic over-sensitivity and playback distortion
 * v4.1: Uses 32-bit I2S slot width for correct BCLK frequency
 *
 * Hardware: AtomS3R (ESP32-S3) + Atomic Echo Base
 * Audio:    ES8311 codec, MEMS mic, NS4150B amp
 * Controls: Tap screen = cycle mode (OFF/FX1..FX6)
 * 
 * MIT License - 2026
 */

#include <M5Unified.h>
#include <math.h>
#include <driver/i2s_std.h>

// ============================================================
// Audio Configuration
// ============================================================
#define SAMPLE_RATE     16000
#define PROC_BUF_SIZE   160
#define RING_BUF_SIZE   4096
#define ECHO_BUF_SIZE   8000

// Recording config
#define MAX_RECORD_SECONDS  10
#define MAX_RECORD_SAMPLES  (SAMPLE_RATE * MAX_RECORD_SECONDS)
#define SILENCE_THRESHOLD   300     // Amplitude threshold for silence detection
#define SILENCE_DURATION_MS 1200    // How long silence before auto-stop recording
#define VAD_THRESHOLD       300     // Voice Activity Detection threshold (lowered for easier trigger)
#define VAD_CONFIRM_MS      80      // How long voice must persist to trigger recording

// ============================================================
// Echo Base Hardware Pins
// ============================================================
#define AUDIO_I2S_GPIO_BCLK   GPIO_NUM_8
#define AUDIO_I2S_GPIO_WS     GPIO_NUM_6
#define AUDIO_I2S_GPIO_DOUT   GPIO_NUM_5   // ESP32 -> ES8311 DAC
#define AUDIO_I2S_GPIO_DIN    GPIO_NUM_7   // ES8311 ADC -> ESP32
#define AUDIO_I2C_SDA         GPIO_NUM_38
#define AUDIO_I2C_SCL         GPIO_NUM_39

// ES8311 & PI4IOE I2C addresses
#define ES8311_ADDR           0x18
#define PI4IOE_ADDR           0x43

// PI4IOE register definitions (from M5EchoBase library)
#define PI4IOE_REG_CTRL      0x00
#define PI4IOE_REG_IO_DIR    0x03
#define PI4IOE_REG_IO_OUT    0x05
#define PI4IOE_REG_IO_PP     0x07
#define PI4IOE_REG_IO_PULLUP 0x0D

// I2S DMA configuration
#define I2S_DMA_DESC_NUM      8
#define I2S_DMA_FRAME_NUM     240

// I2S chunk for read/write: stereo 32-bit, PROC_BUF_SIZE frames
// Each frame = 8 bytes (4 bytes L + 4 bytes R, 32-bit per slot)
#define I2S_CHUNK_FRAMES  128
#define I2S_CHUNK_BYTES   (I2S_CHUNK_FRAMES * 8)

// ============================================================
// Display Configuration
// ============================================================
#define SCREEN_W  128
#define SCREEN_H  128

// Cyberpunk Color Palette (RGB565)
#define CP_BLACK      0x0000
#define CP_DARK_BG    0x0841
#define CP_CYAN       0x07FF
#define CP_CYAN_DIM   0x0410
#define CP_MAGENTA    0xF81F
#define CP_MAG_DIM    0x780F
#define CP_YELLOW     0xFFE0
#define CP_YEL_DIM    0x8400
#define CP_GREEN      0x07E0
#define CP_GRN_DIM    0x0320
#define CP_RED        0xF800
#define CP_ORANGE     0xFD20
#define CP_WHITE      0xFFFF
#define CP_GRAY       0x4208
#define CP_DARK_GRAY  0x2104
#define CP_PURPLE     0x801F

// ============================================================
// Voice Effect Definitions
// ============================================================
enum VoiceEffect {
    FX_DEEP_BASS = 0,
    FX_CHIPMUNK,
    FX_ROBOT,
    FX_ECHO,
    FX_TREMOLO,
    FX_RADIO,
    FX_COUNT
};

struct EffectInfo {
    const char* name;
    const char* icon;
    uint16_t    color;
};

static const EffectInfo effects[FX_COUNT] = {
    { "DEEP BASS",  "BASS", CP_MAGENTA },
    { "CHIPMUNK",   "CHIP", CP_YELLOW  },
    { "ROBOT",      "ROBT", CP_CYAN    },
    { "ECHO",       "ECHO", CP_GREEN   },
    { "TREMOLO",    "TREM", CP_ORANGE  },
    { "RADIO",      "RDIO", CP_PURPLE  },
};

// ============================================================
// State Machine
// ============================================================
enum AppState {
    STATE_IDLE = 0,       // OFF mode - not listening
    STATE_LISTENING,      // FX active - listening for voice (VAD)
    STATE_RECORDING,      // Voice detected - recording
    STATE_PROCESSING,     // Applying voice effect
    STATE_PLAYING,        // Playing back transformed audio
};

// ============================================================
// Global State
// ============================================================
AppState appState      = STATE_IDLE;
int   currentEffect    = -1;      // -1 = OFF, 0..FX_COUNT-1 = active effect
bool  needRedraw       = true;

// I2S handle (single channel for both TX+RX using new std API)
static i2s_chan_handle_t i2s_tx_handle = nullptr;
static i2s_chan_handle_t i2s_rx_handle = nullptr;
static bool audioInitialized = false;

// Recording buffer (PSRAM)
static int16_t* recordBuffer    = nullptr;
static int16_t* processedBuffer = nullptr;
static int recordedSamples      = 0;
static int playbackPos          = 0;

// I2S read/write buffer (32-bit stereo)
static uint8_t i2sBuf[I2S_CHUNK_BYTES];

// Silence detection
static unsigned long silenceStartMs = 0;
static bool inSilence = false;
static unsigned long recordStartMs  = 0;

// VAD (Voice Activity Detection) for auto-listen mode
static unsigned long vadVoiceStartMs = 0;
static bool vadVoiceDetected = false;

// Ring buffer for pitch shifting
static int16_t ringBuf[RING_BUF_SIZE];
static int     ringWritePos = 0;
static float   ringReadPos  = 0.0f;

// Echo buffer
static int16_t* echoBuf = nullptr;
static int     echoWritePos = 0;

// Tremolo state
static float tremoloPhase = 0.0f;

// Waveform display buffer
#define WAVE_POINTS    100
static int8_t waveDisplay[WAVE_POINTS];
static int    waveWriteIdx = 0;

// Animation state
unsigned long lastFrameTime   = 0;
int           animFrame       = 0;
float         scanLineY       = 0;

// VU meter
float vuLevel = 0.0f;
float vuPeak  = 0.0f;

// Progress
float recordProgress = 0.0f;
float playProgress   = 0.0f;

// Debug
int recDebugCount = 0;

// Sprite for double-buffered rendering
static M5Canvas canvas(&M5.Display);

// ============================================================
// I2C Helper (using M5.In_I2C with pin switcher)
// ============================================================
#define ECHO_I2C_BEGIN  m5gfx::i2c::i2c_temporary_switcher_t _i2c_sw(1, GPIO_NUM_38, GPIO_NUM_39)

static bool i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    return M5.In_I2C.writeRegister8(addr, reg, val, 400000);
}

static uint8_t i2c_read_reg(uint8_t addr, uint8_t reg) {
    return M5.In_I2C.readRegister8(addr, reg, 400000);
}

// ============================================================
// ES8311 Register Addresses (from M5EchoBase es8311_reg.h)
// ============================================================
#define ES_REG00_RESET        0x00
#define ES_REG01_CLK_MGR1     0x01
#define ES_REG02_CLK_MGR2     0x02
#define ES_REG03_CLK_MGR3     0x03
#define ES_REG04_CLK_MGR4     0x04
#define ES_REG05_CLK_MGR5     0x05
#define ES_REG06_CLK_MGR6     0x06
#define ES_REG07_CLK_MGR7     0x07
#define ES_REG08_CLK_MGR8     0x08
#define ES_REG09_SDP_IN       0x09
#define ES_REG0A_SDP_OUT      0x0A
#define ES_REG0D_SYSTEM       0x0D
#define ES_REG0E_ADC_POWER    0x0E
#define ES_REG12_DAC_POWER    0x12
#define ES_REG13_DAC_OUTPUT   0x13
#define ES_REG14_ADC_INPUT    0x14
#define ES_REG15_ADC_FADE     0x15
#define ES_REG16_ADC_GAIN     0x16
#define ES_REG17_ADC_VOLUME   0x17
#define ES_REG1C_ADC_EQ       0x1C
#define ES_REG31_DAC_MUTE     0x31
#define ES_REG32_DAC_VOLUME   0x32
#define ES_REG37_DAC_EQ       0x37

// ============================================================
// Hardware Init Functions
// ============================================================

bool initPI4IOE() {
    ECHO_I2C_BEGIN;
    i2c_read_reg(PI4IOE_ADDR, PI4IOE_REG_CTRL);
    i2c_write_reg(PI4IOE_ADDR, PI4IOE_REG_IO_PP,     0x00);
    i2c_read_reg(PI4IOE_ADDR, PI4IOE_REG_IO_PP);
    i2c_write_reg(PI4IOE_ADDR, PI4IOE_REG_IO_PULLUP,  0xFF);
    i2c_write_reg(PI4IOE_ADDR, PI4IOE_REG_IO_DIR,     0x6F);
    i2c_read_reg(PI4IOE_ADDR, PI4IOE_REG_IO_DIR);
    i2c_write_reg(PI4IOE_ADDR, PI4IOE_REG_IO_OUT,     0xFF);
    uint8_t out_val = i2c_read_reg(PI4IOE_ADDR, PI4IOE_REG_IO_OUT);
    Serial.printf("[PI4IOE] OUT=0x%02X\n", out_val);
    return true;
}

void setSpeakerMute(bool mute) {
    ECHO_I2C_BEGIN;
    i2c_write_reg(PI4IOE_ADDR, PI4IOE_REG_IO_OUT, mute ? 0x00 : 0xFF);
}

bool initES8311() {
    ECHO_I2C_BEGIN;
    
    // === RESET ===
    i2c_write_reg(ES8311_ADDR, ES_REG00_RESET, 0x1F);
    delay(20);
    i2c_write_reg(ES8311_ADDR, ES_REG00_RESET, 0x00);
    i2c_write_reg(ES8311_ADDR, ES_REG00_RESET, 0x80);

    // === CLOCK CONFIGURATION ===
    // BCLK = 16000 * 32 * 2 = 1,024,000 Hz
    // coeff_div entry: pre_div=0x01, pre_multi=0x02, adc/dac_div=0x01
    i2c_write_reg(ES8311_ADDR, ES_REG01_CLK_MGR1, 0xBF);
    i2c_write_reg(ES8311_ADDR, ES_REG02_CLK_MGR2, 0x10);
    i2c_write_reg(ES8311_ADDR, ES_REG03_CLK_MGR3, 0x10);
    i2c_write_reg(ES8311_ADDR, ES_REG04_CLK_MGR4, 0x10);
    i2c_write_reg(ES8311_ADDR, ES_REG05_CLK_MGR5, 0x00);
    i2c_write_reg(ES8311_ADDR, ES_REG06_CLK_MGR6, 0x04);
    i2c_write_reg(ES8311_ADDR, ES_REG07_CLK_MGR7, 0x00);
    i2c_write_reg(ES8311_ADDR, ES_REG08_CLK_MGR8, 0xFF);

    // === I2S FORMAT: 32-bit ===
    i2c_write_reg(ES8311_ADDR, ES_REG09_SDP_IN,  0x00);
    i2c_write_reg(ES8311_ADDR, ES_REG0A_SDP_OUT, 0x00);

    // === POWER & ANALOG ===
    i2c_write_reg(ES8311_ADDR, ES_REG0D_SYSTEM,     0x01);
    i2c_write_reg(ES8311_ADDR, ES_REG0E_ADC_POWER,  0x02);
    i2c_write_reg(ES8311_ADDR, ES_REG12_DAC_POWER,  0x00);
    i2c_write_reg(ES8311_ADDR, ES_REG13_DAC_OUTPUT,  0x10);
    i2c_write_reg(ES8311_ADDR, ES_REG1C_ADC_EQ,     0x6A);
    i2c_write_reg(ES8311_ADDR, ES_REG37_DAC_EQ,     0x08);

    // === MICROPHONE CONFIG ===
    i2c_write_reg(ES8311_ADDR, ES_REG14_ADC_INPUT,   0x10); // Moderate PGA
    i2c_write_reg(ES8311_ADDR, ES_REG17_ADC_VOLUME,  0xBF); // 0dB ADC volume

    // === MIC GAIN === 30dB (more sensitive, thresholds raised to compensate)
    i2c_write_reg(ES8311_ADDR, ES_REG16_ADC_GAIN, 0x05);

    // === SPEAKER VOLUME === 80%
    // 0xCC ≈ 80% of max DAC output volume
    i2c_write_reg(ES8311_ADDR, ES_REG32_DAC_VOLUME, 0xCC);

    delay(50);
    
    // Read back key registers
    uint8_t reg00 = i2c_read_reg(ES8311_ADDR, ES_REG00_RESET);
    uint8_t reg01 = i2c_read_reg(ES8311_ADDR, ES_REG01_CLK_MGR1);
    uint8_t reg02 = i2c_read_reg(ES8311_ADDR, ES_REG02_CLK_MGR2);
    uint8_t reg0D = i2c_read_reg(ES8311_ADDR, ES_REG0D_SYSTEM);
    uint8_t reg0E = i2c_read_reg(ES8311_ADDR, ES_REG0E_ADC_POWER);
    uint8_t reg14 = i2c_read_reg(ES8311_ADDR, ES_REG14_ADC_INPUT);
    uint8_t reg16 = i2c_read_reg(ES8311_ADDR, ES_REG16_ADC_GAIN);
    uint8_t reg17 = i2c_read_reg(ES8311_ADDR, ES_REG17_ADC_VOLUME);
    uint8_t reg32 = i2c_read_reg(ES8311_ADDR, ES_REG32_DAC_VOLUME);
    
    Serial.printf("[ES8311] REG00=0x%02X REG01=0x%02X REG02=0x%02X\n", reg00, reg01, reg02);
    Serial.printf("[ES8311] REG0D=0x%02X REG0E=0x%02X REG14=0x%02X\n", reg0D, reg0E, reg14);
    Serial.printf("[ES8311] REG16(gain)=0x%02X REG17(vol)=0x%02X REG32(dac)=0x%02X\n", reg16, reg17, reg32);
    
    return (reg00 != 0x00 && reg00 != 0xFF);
}

bool initI2S() {
    i2s_chan_config_t chan_cfg = {};
    chan_cfg.id = I2S_NUM_1;
    chan_cfg.role = I2S_ROLE_MASTER;
    chan_cfg.dma_desc_num = I2S_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = I2S_DMA_FRAME_NUM;
    chan_cfg.auto_clear_after_cb = true;
    chan_cfg.auto_clear_before_cb = false;
    chan_cfg.intr_priority = 0;
    
    esp_err_t err = i2s_new_channel(&chan_cfg, &i2s_tx_handle, &i2s_rx_handle);
    if (err != ESP_OK) {
        Serial.printf("[I2S] Channel create failed: %s\n", esp_err_to_name(err));
        return false;
    }
    
    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg.sample_rate_hz = SAMPLE_RATE;
    std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    
    std_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_32BIT;
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
    std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO;
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    std_cfg.slot_cfg.ws_width = 32;
    std_cfg.slot_cfg.ws_pol = false;
    std_cfg.slot_cfg.bit_shift = true;
    std_cfg.slot_cfg.left_align = false;
    std_cfg.slot_cfg.big_endian = false;
    std_cfg.slot_cfg.bit_order_lsb = false;
    
    std_cfg.gpio_cfg.mclk = GPIO_NUM_NC;
    std_cfg.gpio_cfg.bclk = AUDIO_I2S_GPIO_BCLK;
    std_cfg.gpio_cfg.ws = AUDIO_I2S_GPIO_WS;
    std_cfg.gpio_cfg.dout = AUDIO_I2S_GPIO_DOUT;
    std_cfg.gpio_cfg.din = AUDIO_I2S_GPIO_DIN;
    std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.ws_inv = false;
    
    err = i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg);
    if (err != ESP_OK) { Serial.printf("[I2S] TX init failed: %s\n", esp_err_to_name(err)); return false; }
    
    err = i2s_channel_init_std_mode(i2s_rx_handle, &std_cfg);
    if (err != ESP_OK) { Serial.printf("[I2S] RX init failed: %s\n", esp_err_to_name(err)); return false; }
    
    err = i2s_channel_enable(i2s_tx_handle);
    if (err != ESP_OK) { Serial.printf("[I2S] TX enable failed: %s\n", esp_err_to_name(err)); return false; }
    
    err = i2s_channel_enable(i2s_rx_handle);
    if (err != ESP_OK) { Serial.printf("[I2S] RX enable failed: %s\n", esp_err_to_name(err)); return false; }
    
    Serial.println("[I2S] TX+RX channels ready (32-bit stereo)");
    return true;
}

// ============================================================
// Audio DSP Functions
// ============================================================

void processPitchShift(int16_t* in, int16_t* out, int len, float pitchFactor) {
    for (int i = 0; i < len; i++) {
        ringBuf[ringWritePos] = in[i];
        ringWritePos = (ringWritePos + 1) % RING_BUF_SIZE;
        int idx0 = (int)ringReadPos;
        int idx1 = (idx0 + 1) % RING_BUF_SIZE;
        float frac = ringReadPos - (float)idx0;
        out[i] = (int16_t)((1.0f - frac) * ringBuf[idx0 % RING_BUF_SIZE] + frac * ringBuf[idx1]);
        ringReadPos += pitchFactor;
        if (ringReadPos >= RING_BUF_SIZE) ringReadPos -= RING_BUF_SIZE;
        if (ringReadPos < 0) ringReadPos += RING_BUF_SIZE;
        float dist = ringWritePos - ringReadPos;
        if (dist < 0) dist += RING_BUF_SIZE;
        if (dist > RING_BUF_SIZE / 2) {
            ringReadPos = (ringWritePos - RING_BUF_SIZE / 4);
            if (ringReadPos < 0) ringReadPos += RING_BUF_SIZE;
        }
        if (dist < RING_BUF_SIZE / 8) {
            ringReadPos = (ringWritePos - RING_BUF_SIZE / 4);
            if (ringReadPos < 0) ringReadPos += RING_BUF_SIZE;
        }
    }
}

void processRobot(int16_t* in, int16_t* out, int len) {
    static float modPhase = 0.0f;
    float modFreq = 150.0f;
    for (int i = 0; i < len; i++) {
        float mod = sinf(modPhase * 2.0f * M_PI);
        modPhase += modFreq / SAMPLE_RATE;
        if (modPhase >= 1.0f) modPhase -= 1.0f;
        float sample = in[i] * mod;
        int16_t crushed = ((int16_t)(sample / 256)) * 256;
        out[i] = crushed;
    }
}

void processEcho(int16_t* in, int16_t* out, int len) {
    if (!echoBuf) { memcpy(out, in, len * sizeof(int16_t)); return; }
    float decay = 0.5f;
    int delaySamples = (SAMPLE_RATE * 200) / 1000;
    for (int i = 0; i < len; i++) {
        int readPos = echoWritePos - delaySamples;
        if (readPos < 0) readPos += ECHO_BUF_SIZE;
        float mixed = in[i] + echoBuf[readPos] * decay;
        if (mixed > 32000) mixed = 32000;
        if (mixed < -32000) mixed = -32000;
        out[i] = (int16_t)mixed;
        echoBuf[echoWritePos] = (int16_t)mixed;
        echoWritePos = (echoWritePos + 1) % ECHO_BUF_SIZE;
    }
}

void processTremolo(int16_t* in, int16_t* out, int len) {
    for (int i = 0; i < len; i++) {
        float mod = 1.0f - 0.7f * (0.5f + 0.5f * sinf(tremoloPhase * 2.0f * M_PI));
        out[i] = (int16_t)(in[i] * mod);
        tremoloPhase += 6.0f / SAMPLE_RATE;
        if (tremoloPhase >= 1.0f) tremoloPhase -= 1.0f;
    }
}

void processRadio(int16_t* in, int16_t* out, int len) {
    static float prevIn = 0, prevOut = 0, prevOut2 = 0;
    for (int i = 0; i < len; i++) {
        float sample = in[i];
        float hp = 0.94f * (prevOut + sample - prevIn);
        prevIn = sample; prevOut = hp;
        float lp = prevOut2 + 0.35f * (hp - prevOut2);
        prevOut2 = lp;
        float norm = lp / 16000.0f;
        norm = constrain(norm, -1.0f, 1.0f);
        float distorted = norm * (1.5f - 0.5f * norm * norm);
        out[i] = (int16_t)(distorted * 14000.0f + random(-200, 200));
    }
}

void processAudio(int16_t* in, int16_t* out, int len) {
    if (currentEffect < 0 || currentEffect >= FX_COUNT) {
        memcpy(out, in, len * sizeof(int16_t));
        return;
    }
    switch (currentEffect) {
        case FX_DEEP_BASS:  processPitchShift(in, out, len, 0.65f); break;
        case FX_CHIPMUNK:   processPitchShift(in, out, len, 1.8f);  break;
        case FX_ROBOT:      processRobot(in, out, len);              break;
        case FX_ECHO:       processEcho(in, out, len);               break;
        case FX_TREMOLO:    processTremolo(in, out, len);            break;
        case FX_RADIO:      processRadio(in, out, len);              break;
        default:            memcpy(out, in, len * sizeof(int16_t));  break;
    }
}

// ============================================================
// Cyberpunk UI Drawing Functions
// ============================================================

void drawNeonBorder(uint16_t color) {
    canvas.drawRect(0, 0, SCREEN_W, SCREEN_H, color);
    canvas.drawRect(1, 1, SCREEN_W - 2, SCREEN_H - 2, CP_DARK_GRAY);
    int cl = 10;
    canvas.drawFastHLine(0, 0, cl, color);
    canvas.drawFastVLine(0, 0, cl, color);
    canvas.drawFastHLine(SCREEN_W - cl, 0, cl, color);
    canvas.drawFastVLine(SCREEN_W - 1, 0, cl, color);
    canvas.drawFastHLine(0, SCREEN_H - 1, cl, color);
    canvas.drawFastVLine(0, SCREEN_H - cl, cl, color);
    canvas.drawFastHLine(SCREEN_W - cl, SCREEN_H - 1, cl, color);
    canvas.drawFastVLine(SCREEN_W - 1, SCREEN_H - cl, cl, color);
}

void drawScanLine(uint16_t color) {
    int y = (int)scanLineY;
    if (y >= 2 && y < SCREEN_H - 2) {
        canvas.drawFastHLine(2, y, SCREEN_W - 4, ((color >> 1) & 0x7BEF));
    }
}

void drawHeader(uint16_t accentColor) {
    canvas.fillRect(2, 2, SCREEN_W - 4, 16, CP_DARK_BG);
    canvas.drawFastHLine(2, 18, SCREEN_W - 4, accentColor);
    canvas.setTextSize(1);
    canvas.setTextColor(accentColor);
    canvas.setCursor(5, 5);
    canvas.print("VCHNGR");
    canvas.setCursor(65, 5);
    switch (appState) {
        case STATE_LISTENING:  canvas.setTextColor(CP_CYAN);   canvas.print("[HEAR]"); break;
        case STATE_RECORDING:  canvas.setTextColor(CP_RED);    canvas.print("[*REC]"); break;
        case STATE_PROCESSING: canvas.setTextColor(CP_YELLOW); canvas.print("[PROC]"); break;
        case STATE_PLAYING:    canvas.setTextColor(CP_GREEN);  canvas.print("[PLAY]"); break;
        default:               canvas.setTextColor(CP_GRAY);   canvas.print("[ OFF]"); break;
    }
}

void drawWaveform(int yCenter, int height, uint16_t color) {
    int halfH = height / 2;
    canvas.drawFastHLine(4, yCenter, WAVE_POINTS, CP_DARK_GRAY);
    for (int i = 1; i < WAVE_POINTS; i++) {
        int y0 = yCenter + (waveDisplay[(waveWriteIdx + i - 1) % WAVE_POINTS] * halfH / 127);
        int y1 = yCenter + (waveDisplay[(waveWriteIdx + i) % WAVE_POINTS] * halfH / 127);
        canvas.drawLine(3 + i, constrain(y0, yCenter - halfH, yCenter + halfH),
                        4 + i, constrain(y1, yCenter - halfH, yCenter + halfH), color);
    }
}

void drawVUMeter(int y, uint16_t color) {
    int barW = SCREEN_W - 16, barH = 6, x = 8;
    canvas.fillRect(x, y, barW, barH, CP_DARK_BG);
    canvas.drawRect(x, y, barW, barH, CP_DARK_GRAY);
    int levelW = constrain((int)(vuLevel * barW), 0, barW);
    for (int i = 0; i < levelW; i++) {
        float r = (float)i / barW;
        canvas.drawFastVLine(x + i, y + 1, barH - 2,
                             r < 0.6f ? CP_GREEN : (r < 0.8f ? CP_YELLOW : CP_RED));
    }
    int peakX = x + constrain((int)(vuPeak * barW), 0, barW - 1);
    canvas.drawFastVLine(peakX, y, barH, CP_WHITE);
}

// Draw mode indicator dots: OFF + 6 effects = 7 positions
void drawModeDots(int y) {
    // Total modes: OFF(-1), FX0..FX5 = 7 dots
    int totalModes = FX_COUNT + 1;
    int dotSpacing = 14;
    int startX = (SCREEN_W - totalModes * dotSpacing) / 2 + dotSpacing / 2;
    for (int i = 0; i < totalModes; i++) {
        int dx = startX + i * dotSpacing;
        int modeIdx = i - 1;  // -1=OFF, 0=FX0, ...
        bool isActive = (modeIdx == currentEffect);
        if (isActive) {
            uint16_t color = (modeIdx < 0) ? CP_GRAY : effects[modeIdx].color;
            canvas.fillCircle(dx, y, 4, color);
            canvas.drawCircle(dx, y, 5, color);
        } else {
            uint16_t color = (modeIdx < 0) ? CP_DARK_GRAY : ((effects[modeIdx].color >> 2) & 0x39E7);
            canvas.drawCircle(dx, y, 3, color);
        }
    }
}

void drawGridLines() {
    for (int x = 12; x < SCREEN_W; x += 16)
        for (int y = 20; y < SCREEN_H - 5; y += 3)
            canvas.drawPixel(x, y, CP_DARK_GRAY);
}

void drawStatusBar(uint16_t accentColor) {
    int y = SCREEN_H - 14;
    canvas.fillRect(2, y, SCREEN_W - 4, 12, CP_DARK_BG);
    canvas.drawFastHLine(2, y, SCREEN_W - 4, CP_DARK_GRAY);
    canvas.setTextSize(1);
    canvas.setCursor(5, y + 3);
    canvas.setTextColor(CP_CYAN_DIM);
    canvas.print("TAP: SWITCH MODE");
}

void drawProgressBar(int y, float progress, uint16_t color, const char* label) {
    int barW = SCREEN_W - 16, barH = 10, x = 8;
    canvas.setTextSize(1);
    canvas.setTextColor(CP_GRAY);
    canvas.setCursor(x, y - 10);
    canvas.print(label);
    if (appState == STATE_RECORDING) {
        canvas.setCursor(80, y - 10);
        canvas.setTextColor(color);
        canvas.printf("%.1fs", (float)(millis() - recordStartMs) / 1000.0f);
    }
    canvas.fillRect(x, y, barW, barH, CP_DARK_BG);
    canvas.drawRect(x, y, barW, barH, CP_DARK_GRAY);
    int fillW = (int)(progress * (barW - 2));
    if (fillW > 0) canvas.fillRect(x + 1, y + 1, fillW, barH - 2, color);
}

// ---- UI Screens ----

void drawIdleUI() {
    canvas.fillSprite(CP_BLACK);
    drawGridLines();
    drawNeonBorder(CP_DARK_GRAY);
    drawHeader(CP_GRAY);
    
    canvas.setTextSize(2);
    canvas.setTextColor(CP_GRAY);
    canvas.setCursor((SCREEN_W - 36) / 2, 28);
    canvas.print("OFF");
    
    canvas.setTextSize(1);
    canvas.setTextColor(CP_DARK_GRAY);
    canvas.setCursor(15, 52);
    canvas.print("Voice Changer");
    canvas.setCursor(25, 64);
    canvas.print("is standby");
    
    drawModeDots(85);
    drawWaveform(100, 12, CP_DARK_GRAY);
    drawStatusBar(CP_DARK_GRAY);
    canvas.pushSprite(0, 0);
}

void drawListeningUI() {
    uint16_t ac = effects[currentEffect].color;
    canvas.fillSprite(CP_BLACK);
    drawGridLines();
    drawNeonBorder(ac);
    drawHeader(ac);
    
    // Pulsing "LISTENING" text
    uint16_t textColor = (animFrame % 3 == 0) ? ac : ((ac >> 1) & 0x7BEF);
    canvas.setTextSize(1);
    canvas.setTextColor(textColor);
    canvas.setCursor(25, 24);
    canvas.print("~ LISTENING ~");
    
    // Current effect name
    canvas.setTextSize(2);
    canvas.setTextColor(ac);
    const char* nm = effects[currentEffect].name;
    int tw = strlen(nm) * 12;
    canvas.setCursor(max(4, (SCREEN_W - tw) / 2), 38);
    canvas.print(nm);
    
    // Live waveform from mic
    drawWaveform(62, 20, ac);
    drawVUMeter(78, ac);
    
    drawModeDots(95);
    drawStatusBar(ac);
    drawScanLine(ac);
    canvas.pushSprite(0, 0);
}

void drawRecordingUI() {
    uint16_t ac = effects[currentEffect].color;
    canvas.fillSprite(CP_BLACK);
    drawGridLines();
    drawNeonBorder(CP_RED);
    drawHeader(CP_RED);
    canvas.setTextSize(2);
    canvas.setTextColor((animFrame & 1) ? CP_RED : CP_DARK_GRAY);
    canvas.setCursor((SCREEN_W - 36) / 2, 24);
    canvas.print("REC");
    drawWaveform(50, 24, CP_RED);
    drawProgressBar(72, recordProgress, CP_RED, "BUFFER:");
    drawVUMeter(88, CP_RED);
    canvas.setTextSize(1);
    canvas.setTextColor(ac);
    canvas.setCursor(5, 100);
    canvas.printf("FX: %s", effects[currentEffect].name);
    canvas.setTextColor(CP_GRAY);
    canvas.setCursor(5, 108);
    canvas.printf("N:%d VU:%.2f", recordedSamples, vuLevel);
    drawStatusBar(CP_RED);
    canvas.pushSprite(0, 0);
}

void drawProcessingUI() {
    uint16_t ac = effects[currentEffect].color;
    canvas.fillSprite(CP_BLACK);
    drawGridLines();
    drawNeonBorder(CP_YELLOW);
    drawHeader(CP_YELLOW);
    canvas.setTextSize(2);
    canvas.setTextColor(CP_YELLOW);
    canvas.setCursor(8, 40);
    canvas.print("PROCESS");
    canvas.setTextSize(1);
    canvas.setTextColor(CP_GRAY);
    canvas.setCursor(20, 65);
    const char* sp[] = { "|", "/", "-", "\\" };
    canvas.printf("Applying %s %s", effects[currentEffect].name, sp[animFrame % 4]);
    canvas.setCursor(20, 80);
    canvas.setTextColor(CP_CYAN);
    canvas.printf("%.1f sec recorded", (float)recordedSamples / SAMPLE_RATE);
    drawStatusBar(CP_YELLOW);
    canvas.pushSprite(0, 0);
}

void drawPlayingUI() {
    uint16_t ac = effects[currentEffect].color;
    canvas.fillSprite(CP_BLACK);
    drawGridLines();
    drawNeonBorder(ac);
    drawHeader(ac);
    canvas.setTextSize(2);
    canvas.setTextColor(ac);
    const char* nm = effects[currentEffect].name;
    int tw = strlen(nm) * 12;
    canvas.setCursor(max(4, (SCREEN_W - tw) / 2), 24);
    canvas.print(nm);
    drawWaveform(50, 24, ac);
    drawProgressBar(72, playProgress, CP_GREEN, "PLAY:");
    drawVUMeter(88, ac);
    drawModeDots(104);
    drawStatusBar(ac);
    drawScanLine(ac);
    canvas.pushSprite(0, 0);
}

void drawFullUI() {
    switch (appState) {
        case STATE_LISTENING:  drawListeningUI();  break;
        case STATE_RECORDING:  drawRecordingUI();  break;
        case STATE_PROCESSING: drawProcessingUI(); break;
        case STATE_PLAYING:    drawPlayingUI();    break;
        default:               drawIdleUI();       break;
    }
    needRedraw = false;
}

// ============================================================
// Mode Switching
// ============================================================

void enterMode(int effectIdx) {
    currentEffect = effectIdx;
    
    if (currentEffect < 0) {
        // OFF mode
        appState = STATE_IDLE;
        setSpeakerMute(true);
        vuLevel = 0; vuPeak = 0;
        memset(waveDisplay, 0, sizeof(waveDisplay));
        Serial.println("[MODE] OFF");
    } else {
        // Enter listening mode for this effect
        appState = STATE_LISTENING;
        vadVoiceDetected = false;
        vadVoiceStartMs = 0;
        setSpeakerMute(true);  // Mute speaker while listening to avoid feedback
        vuLevel = 0; vuPeak = 0;
        Serial.printf("[MODE] %s - listening...\n", effects[currentEffect].name);
    }
    needRedraw = true;
}

void cycleMode() {
    // Cycle: OFF(-1) -> FX0 -> FX1 -> ... -> FX5 -> OFF(-1)
    int next = currentEffect + 1;
    if (next >= FX_COUNT) next = -1;
    enterMode(next);
}

// ============================================================
// Recording & Playback
// ============================================================

void startRecording() {
    recordedSamples = 0;
    silenceStartMs = 0;
    inSilence = false;
    recordStartMs = millis();
    recDebugCount = 0;
    
    memset(ringBuf, 0, sizeof(ringBuf));
    ringWritePos = 0;
    ringReadPos = 0;
    if (echoBuf) memset(echoBuf, 0, ECHO_BUF_SIZE * sizeof(int16_t));
    echoWritePos = 0;
    tremoloPhase = 0;
    
    setSpeakerMute(true);
    
    appState = STATE_RECORDING;
    Serial.println("[REC] Voice detected! Recording...");
}

// Read a chunk from I2S mic and return max amplitude (used by both LISTENING and RECORDING)
int16_t readMicChunk(bool storeToBuffer) {
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(i2s_rx_handle, i2sBuf, I2S_CHUNK_BYTES, &bytes_read, 100);
    
    if (err != ESP_OK || bytes_read == 0) return 0;
    
    int32_t* samples32 = (int32_t*)i2sBuf;
    int stereoFrames = bytes_read / 8;
    
    int16_t maxAmp = 0;
    for (int i = 0; i < stereoFrames; i++) {
        int16_t sampleL = (int16_t)(samples32[i * 2] >> 16);
        int16_t sampleR = (int16_t)(samples32[i * 2 + 1] >> 16);
        int16_t sample = (abs(sampleR) > abs(sampleL)) ? sampleR : sampleL;
        
        if (storeToBuffer && recordedSamples < MAX_RECORD_SAMPLES) {
            recordBuffer[recordedSamples++] = sample;
        }
        
        int16_t absVal = abs(sample);
        if (absVal > maxAmp) maxAmp = absVal;
    }
    
    // Debug first few chunks in recording mode
    if (storeToBuffer && recDebugCount < 5) {
        Serial.printf("[REC] chunk %d: %d frames, max=%d, total=%d\n",
                      recDebugCount, stereoFrames, maxAmp, recordedSamples);
        recDebugCount++;
    }
    
    // Update VU meter
    vuLevel = (float)maxAmp / 32768.0f;
    if (vuLevel > vuPeak) vuPeak = vuLevel;
    vuPeak *= 0.995f;
    
    // Update waveform display
    int step = max(1, stereoFrames / 10);
    for (int i = 0; i < stereoFrames; i += step) {
        waveDisplay[waveWriteIdx] = (int8_t)((samples32[i * 2] >> 16) / 256);
        waveWriteIdx = (waveWriteIdx + 1) % WAVE_POINTS;
    }
    
    if (storeToBuffer) {
        recordProgress = (float)recordedSamples / MAX_RECORD_SAMPLES;
    }
    
    return maxAmp;
}

bool listenForVoice() {
    int16_t maxAmp = readMicChunk(false);  // Don't store, just listen
    unsigned long now = millis();
    
    if (maxAmp >= VAD_THRESHOLD) {
        if (!vadVoiceDetected) {
            vadVoiceDetected = true;
            vadVoiceStartMs = now;
        } else if (now - vadVoiceStartMs >= VAD_CONFIRM_MS) {
            // Voice confirmed! Transition to recording
            return true;
        }
    } else {
        vadVoiceDetected = false;
    }
    
    return false;
}

bool recordChunk() {
    if (recordedSamples >= MAX_RECORD_SAMPLES) {
        Serial.println("[REC] Max duration reached");
        return false;
    }
    
    int16_t maxAmp = readMicChunk(true);  // Store to buffer
    
    // Silence detection
    unsigned long now = millis();
    if (maxAmp < SILENCE_THRESHOLD) {
        if (!inSilence) { inSilence = true; silenceStartMs = now; }
        else if (now - silenceStartMs >= SILENCE_DURATION_MS && recordedSamples > SAMPLE_RATE / 2) {
            Serial.printf("[REC] Silence after %d samples\n", recordedSamples);
            return false;
        }
    } else {
        inSilence = false;
    }
    
    if (now - recordStartMs >= MAX_RECORD_SECONDS * 1000UL) {
        Serial.println("[REC] Timeout");
        return false;
    }
    
    return true;
}

void stopRecording() {
    Serial.printf("[REC] Stopped. %d samples (%.1fs)\n", recordedSamples, (float)recordedSamples / SAMPLE_RATE);
    if (recordedSamples < 800) {
        Serial.println("[REC] Too short, discarding");
        // Go back to listening
        if (currentEffect >= 0) {
            appState = STATE_LISTENING;
            vadVoiceDetected = false;
        } else {
            appState = STATE_IDLE;
        }
        return;
    }
    appState = STATE_PROCESSING;
}

// Soft-clip a float sample to [-1.0, 1.0] using tanh-like curve
static inline float softClip(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    if (x > 0.66f) return 0.66f + (1.0f - 0.66f) * tanhf((x - 0.66f) / (1.0f - 0.66f));
    if (x < -0.66f) return -0.66f + (-1.0f + 0.66f) * tanhf((x + 0.66f) / (1.0f - 0.66f));
    return x;
}

void processRecordedAudio() {
    Serial.printf("[PROC] Processing %d samples with %s\n", recordedSamples, effects[currentEffect].name);
    unsigned long t0 = millis();
    
    // Step 1: Find peak
    int16_t rawPeak = 0;
    for (int i = 0; i < recordedSamples; i++) {
        int16_t a = abs(recordBuffer[i]);
        if (a > rawPeak) rawPeak = a;
    }
    Serial.printf("[PROC] Raw peak: %d (%.1f%%)\n", rawPeak, rawPeak * 100.0f / 32768.0f);
    
    // Step 2: Normalize to ~70%
    float inputGain = 1.0f;
    if (rawPeak > 100) {
        float targetPeak = 22937.0f;
        inputGain = targetPeak / (float)rawPeak;
        inputGain = constrain(inputGain, 0.1f, 10.0f);
        for (int i = 0; i < recordedSamples; i++) {
            float s = recordBuffer[i] * inputGain;
            recordBuffer[i] = (int16_t)constrain((int)s, -32000, 32000);
        }
        Serial.printf("[PROC] Input gain: %.2fx\n", inputGain);
    }
    
    // Step 3: Apply voice effect
    int processed = 0;
    while (processed < recordedSamples) {
        int chunk = min(PROC_BUF_SIZE, recordedSamples - processed);
        processAudio(&recordBuffer[processed], &processedBuffer[processed], chunk);
        processed += chunk;
    }
    
    // Step 4: Soft-clip
    int16_t fxPeak = 0;
    for (int i = 0; i < recordedSamples; i++) {
        int16_t a = abs(processedBuffer[i]);
        if (a > fxPeak) fxPeak = a;
    }
    Serial.printf("[PROC] Post-FX peak: %d (%.1f%%)\n", fxPeak, fxPeak * 100.0f / 32768.0f);
    
    float outGain = 1.0f;
    if (fxPeak > 26000) {
        outGain = 26000.0f / (float)fxPeak;
    }
    
    for (int i = 0; i < recordedSamples; i++) {
        float s = processedBuffer[i] * outGain / 32768.0f;
        s = softClip(s);
        processedBuffer[i] = (int16_t)(s * 28000.0f);
    }
    
    Serial.printf("[PROC] Output gain: %.2fx, done in %lu ms\n", outGain, millis() - t0);
}

void startPlayback() {
    playbackPos = 0;
    setSpeakerMute(false);
    delay(10);
    appState = STATE_PLAYING;
    Serial.println("[PLAY] Started");
}

bool playChunk() {
    if (playbackPos >= recordedSamples) return false;
    
    int maxFrames = I2S_CHUNK_BYTES / 8;
    int chunkSize = min(maxFrames, recordedSamples - playbackPos);
    
    int32_t* out32 = (int32_t*)i2sBuf;
    for (int i = 0; i < chunkSize; i++) {
        int32_t s32 = ((int32_t)processedBuffer[playbackPos + i]) << 16;
        out32[i * 2]     = s32;
        out32[i * 2 + 1] = s32;
    }
    
    size_t bytes_written = 0;
    i2s_channel_write(i2s_tx_handle, i2sBuf, chunkSize * 8, &bytes_written, 100);
    
    playbackPos += chunkSize;
    playProgress = (float)playbackPos / recordedSamples;
    
    int step = max(1, chunkSize / 10);
    for (int i = 0; i < chunkSize; i += step) {
        waveDisplay[waveWriteIdx] = (int8_t)(processedBuffer[playbackPos - chunkSize + i] / 256);
        waveWriteIdx = (waveWriteIdx + 1) % WAVE_POINTS;
    }
    
    int16_t maxAmp = 0;
    for (int i = 0; i < chunkSize; i++) {
        int16_t a = abs(processedBuffer[playbackPos - chunkSize + i]);
        if (a > maxAmp) maxAmp = a;
    }
    vuLevel = (float)maxAmp / 32768.0f;
    
    return (playbackPos < recordedSamples);
}

void stopPlayback() {
    setSpeakerMute(true);
    vuLevel = 0; vuPeak = 0;
    memset(waveDisplay, 0, sizeof(waveDisplay));
    Serial.println("[PLAY] Finished");
    
    // After playback, go back to listening (if still in an FX mode)
    if (currentEffect >= 0) {
        appState = STATE_LISTENING;
        vadVoiceDetected = false;
        vadVoiceStartMs = 0;
        Serial.printf("[MODE] Back to listening for %s\n", effects[currentEffect].name);
    } else {
        appState = STATE_IDLE;
    }
    needRedraw = true;
}

// ============================================================
// Setup
// ============================================================
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    delay(100);
    Serial.println("\n[VoiceChanger] v5.0 Starting...");
    Serial.println("[VoiceChanger] Tap=CycleMode, Auto-listen+record+play");

    M5.Display.setRotation(0);
    M5.Display.fillScreen(CP_BLACK);
    canvas.createSprite(SCREEN_W, SCREEN_H);

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(CP_CYAN);
    M5.Display.setCursor(10, 20);
    M5.Display.println("VOICE CHANGER");
    M5.Display.setTextColor(CP_MAGENTA);
    M5.Display.setCursor(10, 35);
    M5.Display.println("CYBERPUNK v5.0");
    M5.Display.setTextColor(CP_YELLOW);
    M5.Display.setCursor(10, 50);
    M5.Display.println("Auto-Listen Mode");
    M5.Display.setTextColor(CP_GRAY);
    M5.Display.setCursor(10, 68);
    M5.Display.println("Initializing...");
    
    initPI4IOE();
    M5.Display.setTextColor(CP_GREEN);
    M5.Display.setCursor(10, 78);
    M5.Display.println("PI4IOE [OK]");
    
    bool esOk = initES8311();
    M5.Display.setTextColor(esOk ? CP_GREEN : CP_RED);
    M5.Display.setCursor(10, 88);
    M5.Display.printf("ES8311 [%s]", esOk ? "OK" : "FAIL");
    
    bool i2sOk = false;
    if (esOk) {
        i2sOk = initI2S();
        M5.Display.setTextColor(i2sOk ? CP_GREEN : CP_RED);
        M5.Display.setCursor(10, 98);
        M5.Display.printf("I2S    [%s]", i2sOk ? "OK" : "FAIL");
    }
    
    audioInitialized = esOk && i2sOk;

    recordBuffer    = (int16_t*)ps_malloc(MAX_RECORD_SAMPLES * sizeof(int16_t));
    processedBuffer = (int16_t*)ps_malloc(MAX_RECORD_SAMPLES * sizeof(int16_t));
    bool bufOk = (recordBuffer && processedBuffer);
    if (!bufOk) {
        if (!recordBuffer)    recordBuffer    = (int16_t*)malloc(SAMPLE_RATE * 3 * sizeof(int16_t));
        if (!processedBuffer) processedBuffer = (int16_t*)malloc(SAMPLE_RATE * 3 * sizeof(int16_t));
    }
    
    echoBuf = (int16_t*)ps_malloc(ECHO_BUF_SIZE * sizeof(int16_t));
    if (!echoBuf) echoBuf = (int16_t*)malloc(ECHO_BUF_SIZE * sizeof(int16_t));
    if (echoBuf) memset(echoBuf, 0, ECHO_BUF_SIZE * sizeof(int16_t));
    memset(ringBuf, 0, sizeof(ringBuf));
    memset(waveDisplay, 0, sizeof(waveDisplay));

    setSpeakerMute(true);

    M5.Display.setTextColor(CP_GREEN);
    M5.Display.setCursor(10, 108);
    M5.Display.println("Ready [OK]");
    delay(800);

    needRedraw = true;
    currentEffect = -1;  // Start in OFF mode
    appState = STATE_IDLE;
    Serial.printf("[VoiceChanger] Ready! audio=%d psram=%d\n", audioInitialized, bufOk);
}

// ============================================================
// Main Loop
// ============================================================
void loop() {
    M5.update();
    unsigned long now = millis();

    // ---- Button Handling: Tap = cycle mode ----
    if (M5.BtnA.wasClicked()) {
        // Tap cycles mode regardless of current state
        // If we're recording or playing, stop first, then cycle
        if (appState == STATE_RECORDING || appState == STATE_PLAYING) {
            if (appState == STATE_PLAYING) {
                setSpeakerMute(true);
            }
            vuLevel = 0; vuPeak = 0;
            memset(waveDisplay, 0, sizeof(waveDisplay));
        }
        cycleMode();
    }

    // ---- State Machine ----
    switch (appState) {
        case STATE_IDLE:
            // OFF mode - just update display slowly
            if (now - lastFrameTime >= 500) {
                lastFrameTime = now;
                animFrame++;
                drawFullUI();
            }
            break;
            
        case STATE_LISTENING:
            // Actively reading mic, looking for voice
            if (listenForVoice()) {
                // Voice detected! Start recording
                startRecording();
            }
            if (now - lastFrameTime >= 100) {
                lastFrameTime = now;
                animFrame++;
                scanLineY += 1.0f;
                if (scanLineY >= SCREEN_H) scanLineY = 0;
                drawFullUI();
            }
            break;
            
        case STATE_RECORDING:
            if (!recordChunk()) stopRecording();
            if (now - lastFrameTime >= 80) {
                lastFrameTime = now;
                animFrame++;
                drawFullUI();
            }
            break;
        
        case STATE_PROCESSING:
            drawFullUI();
            processRecordedAudio();
            startPlayback();
            drawFullUI();
            break;
            
        case STATE_PLAYING:
            if (!playChunk()) stopPlayback();
            if (now - lastFrameTime >= 80) {
                lastFrameTime = now;
                animFrame++;
                scanLineY += 1.5f;
                if (scanLineY >= SCREEN_H) scanLineY = 0;
                drawFullUI();
            }
            break;
    }
    
    if (needRedraw) drawFullUI();
}
