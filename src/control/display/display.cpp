/**
 * @file display.cpp
 * @brief TFT display UI implementation — ST7789 320×240, encoder-only
 *
 * All drawing goes through a full-screen TFT_eSprite (framebuffer) then
 * pushed to the panel in one DMA transfer — eliminates flicker.
 *
 * Coordinate origin: top-left (0,0).
 */

#include "display.h"
#include "../../effects/dsp_pipeline.h"
#include "../../control/param/preset_manager.h"
#include "../../utils/debug_log.h"
#include "battery_monitor.h"
#include "assets/Century751BT_12pt_GFX.h"
#include "assets/icon.h"
#include <cstdio>
#include <cstring>
#include <cmath>

#define TAG "Display"

static constexpr int16_t SIDEBAR_W              = 28;
static constexpr int16_t SIDEBAR_X              = 2;
static constexpr int16_t MAIN_MENU_CONTENT_X    = SIDEBAR_W + 4;
static constexpr int16_t CONTENT_X              = 0;
static constexpr int16_t MAIN_MENU_CONTENT_W    = DISP_W - MAIN_MENU_CONTENT_X - 2;
static constexpr int16_t CONTENT_W              = DISP_W - CONTENT_X - 2;
static constexpr int16_t ROW_H                  = 28;
static constexpr int16_t NAV_BTN_H              = 26;
static constexpr int16_t HEADER_H               = 20;
static constexpr int16_t FOOTER_Y               = DISP_H - NAV_BTN_H - 2;

struct ModuleEntry {
    DisplayModuleID id;
    const char*     name;
    bool            hasGraph;
    bool            isEq;
    bool            isDrc;
    bool            isIsf;
};

static const ModuleEntry MODULE_LIST[] = {
    { DisplayModuleID::PRE_GAIN,          "Pre Gain",          false, false, false, false },
    { DisplayModuleID::COMPANDER,         "Compander",         false, false, false, false },
    { DisplayModuleID::EXCITER,           "Exciter",           false, false, false, false },
    { DisplayModuleID::DYNAMIC_BASS,      "Dynamic Bass",      false, false, false, false },
    { DisplayModuleID::DYNAMIC_EQ_THRESH, "DynEQ Thresholds",  false, false, false, false },
    { DisplayModuleID::DYNAMIC_EQ_LOW,    "DynEQ - Low",       true,  true,  false, false },
    { DisplayModuleID::DYNAMIC_EQ_HIGH,   "DynEQ - High",      true,  true,  false, false },
    { DisplayModuleID::EQ1,               "EQ 1",              true,  true,  false, false },
    { DisplayModuleID::EQ2,               "EQ 2",              true,  true,  false, false },
    { DisplayModuleID::LEFT_EQ,           "Left EQ",           true,  true,  false, false },
    { DisplayModuleID::RIGHT_EQ,          "Right EQ",          true,  true,  false, false },
    { DisplayModuleID::ISF1,              "ISF EQ 1",          true,  true,  false, true  },
    { DisplayModuleID::ISF2,              "ISF EQ 2",          true,  true,  false, true  },
    { DisplayModuleID::DRC,               "DRC",               true,  false, true,  false },
    { DisplayModuleID::POST_GAIN,         "Post Gain",         false, false, false, false },
};
static constexpr uint8_t MODULE_COUNT = sizeof(MODULE_LIST) / sizeof(MODULE_LIST[0]);
static constexpr uint8_t DSP_LIST_PRESET_SAVE_IDX = MAX_PRESET_SLOTS;
static constexpr uint8_t DSP_LIST_MODULE_START_IDX = MAX_PRESET_SLOTS + 1;

static const char* KB_ROWS[] = { "789", "456", "123", "0.-" };
static const char* KB_ACTIONS[] = { "DEL", "CANCEL", "ENTER" };
static constexpr uint8_t KB_COLS      = 3;
static constexpr uint8_t KB_NUM_ROWS  = 4;
static constexpr uint8_t KB_ACT_COLS  = 3;

static uint8_t countEnabledEqBands(ParametricEQ* eq) {
    if (!eq) return 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_EQ_BANDS; i++)
        if (eq->getBandParams(i).enabled) n++;
    return n;
}

static void toggleEqBandSlot(ParametricEQ* eq, uint8_t bandIdx) {
    if (!eq || bandIdx >= MAX_EQ_BANDS) return;
    EQFilterParams p = eq->getBandParams(bandIdx);
    p.enabled = p.enabled ? 0 : 1;
    eq->setBand(bandIdx, p);
}

DspModule* Display::getModulePtr(DisplayModuleID id) {
    if (!_pipeline) return nullptr;
    switch (id) {
    case DisplayModuleID::PRE_GAIN:          return &_pipeline->getPreGain();
    case DisplayModuleID::COMPANDER:         return &_pipeline->getCompander();
    case DisplayModuleID::EXCITER:           return &_pipeline->getExciter();
    case DisplayModuleID::DYNAMIC_BASS:      return &_pipeline->getDynamicBass();
    case DisplayModuleID::DYNAMIC_EQ_THRESH: return &_pipeline->getDynamicEq();
    case DisplayModuleID::DYNAMIC_EQ_LOW:    return &_pipeline->getDynamicEq();
    case DisplayModuleID::DYNAMIC_EQ_HIGH:   return &_pipeline->getDynamicEq();
    case DisplayModuleID::EQ1:               return &_pipeline->getEqDsp_1();
    case DisplayModuleID::EQ2:               return &_pipeline->getEqDsp_2();
    case DisplayModuleID::LEFT_EQ:           return &_pipeline->getLeftRightEq();
    case DisplayModuleID::RIGHT_EQ:          return &_pipeline->getLeftRightEq();
    case DisplayModuleID::ISF1:              return &_pipeline->getIsf1();
    case DisplayModuleID::ISF2:              return &_pipeline->getIsf2();
    case DisplayModuleID::DRC:               return &_pipeline->getDrc();
    case DisplayModuleID::POST_GAIN:         return &_pipeline->getPostGain();
    default: return nullptr;
    }
}

ParametricEQ* Display::getEqPtr(DisplayModuleID id) {
    if (!_pipeline) return nullptr;
    switch (id) {
    case DisplayModuleID::EQ1:           return &_pipeline->getEqDsp_1();
    case DisplayModuleID::EQ2:           return &_pipeline->getEqDsp_2();
    case DisplayModuleID::DYNAMIC_EQ_LOW:  return &_pipeline->getDynamicEq().getEqLow();
    case DisplayModuleID::DYNAMIC_EQ_HIGH: return &_pipeline->getDynamicEq().getEqHigh();
    case DisplayModuleID::LEFT_EQ:         return &_pipeline->getLeftRightEq().getEqLeft();
    case DisplayModuleID::RIGHT_EQ:        return &_pipeline->getLeftRightEq().getEqRight();
    default: return nullptr;
    }
}

uint8_t Display::getScreenParams(NavEntry nav, UiParam* outParams) {
    switch (nav.module) {

    case DisplayModuleID::PRE_GAIN:
    case DisplayModuleID::POST_GAIN: {
        static const UiParam p[] = {
            { "Gain",  "dB", -96.0f, 24.0f,  1.0f, 1 },
            { "Mute",  "",    0.0f,   1.0f,   1.0f, 0 },
            { "Mono",  "",    0.0f,   1.0f,   1.0f, 0 },
        };
        if (outParams) memcpy(outParams, p, sizeof(p));
        return 3;
    }

    case DisplayModuleID::COMPANDER: {
        static const UiParam p[] = {
            { "Threshold",   "dB",  -60.0f,  0.0f,    1.0f,  1   },
            { "Ratio Below", ":1",   10.00f, 1000.0f, 10.0f, 1   },
            { "Ratio Above", ":1",   10.00f, 1000.0f, 10.0f, 1   },
            { "Attack",      "ms",   1.0f, 2000.0f,   1.0f,  0   },
            { "Release",     "ms",  10.0f, 2000.0f,   1.0f,  0   },
            { "Lookahead",   "ms",   0.0f,  10.0f,    1.0f,  0   },
        };
        if (outParams) memcpy(outParams, p, sizeof(p));
        return 6;
    }

    case DisplayModuleID::EXCITER: {
        static const UiParam p[] = {
            { "Cutoff Freq", "Hz",  300.0f, 10000.0f, 100.0f, 0 },
            { "Dry",         "%",    0.0f,   100.0f,   1.0f,  0 },
            { "Wet",         "%",    0.0f,   100.0f,   1.0f,  0 },
        };
        if (outParams) memcpy(outParams, p, sizeof(p));
        return 3;
    }

    case DisplayModuleID::DYNAMIC_BASS: {
        static const UiParam p[] = {
            { "Cutoff Freq",    "Hz",  30.0f, 300.0f,  5.0f, 0 },
            { "Gain Boost",     "dB",  0.0f,  20.0f,   1.0f, 1 },
            { "Enhanced",       "",    0.0f,   1.0f,   1.0f, 0 },
            { "Boost Full Thr", "dB", -60.0f,  0.0f,  0.1f,  2 },
            { "Neutral Thr",    "dB", -60.0f,  0.0f,  0.1f,  2 },
            { "Clip Full Thr",  "dB", -60.0f,  0.0f,  0.1f,  2 },
            { "Attack",         "ms",  1.0f, 2000.0f,  1.0f, 0 },
            { "Release",        "ms",  10.0f, 2000.0f,  1.0f, 0 },
            { "Lookahead",      "ms",  0.0f,  10.0f,   1.0f, 0 },
        };
        if (outParams) memcpy(outParams, p, sizeof(p));
        return 9;
    }

    case DisplayModuleID::DYNAMIC_EQ_THRESH: {
        static const UiParam p[] = {
            { "Low Thresh",    "dB", -96.0f, 0.0f, 1.0f, 1 },
            { "Normal Thresh", "dB", -96.0f, 0.0f, 1.0f, 1 },
            { "High Thresh",   "dB", -96.0f, 0.0f, 1.0f, 1 },
            { "Attack",        "ms",  1.0f, 2000.0f, 1.0f, 0 },
            { "Release",       "ms",  10.0f, 2000.0f, 1.0f, 0 },
            { "Lookahead",     "ms",  0.0f,  10.0f,  1.0f, 0 },
        };
        if (outParams) memcpy(outParams, p, sizeof(p));
        return 6;
    }

    case DisplayModuleID::ISF1:
    case DisplayModuleID::ISF2: {
        static const UiParam p[] = {
            { "RMS Window",  "ms",      10.0f, 2000.0f, 10.0f, 0 },
            { "Slew Time",   "ms/stp", 10.0f, 2000.0f, 10.0f, 0 },
            { "Lookahead",   "ms",       0.0f,   10.0f,  1.0f, 0 },
        };
        if (outParams) memcpy(outParams, p, sizeof(p));
        return 3;
    }

    default:
        return 0;
    }
}

float Display::getParamValue(NavEntry nav, uint8_t idx) {
    if (!_pipeline) return 0.0f;
    switch (nav.module) {
    case DisplayModuleID::PRE_GAIN:
    case DisplayModuleID::POST_GAIN: {
        VolumeControl* v = (nav.module == DisplayModuleID::PRE_GAIN) ? &_pipeline->getPreGain() : &_pipeline->getPostGain();
        switch (idx) {
        case 0: return DB_Q8_TO_FLOAT(v->getGainDb());
        case 1: return v->isMuted() ? 1.0f : 0.0f;
        case 2: return v->isMono()  ? 1.0f : 0.0f;
        } break;
    }
    case DisplayModuleID::COMPANDER: {
        Compander& c = _pipeline->getCompander();
        switch (idx) {
        case 0: return c._thresholdDb;
        case 1: return (float)c._ratioBelowQ88;
        case 2: return (float)c._ratioAboveQ88;
        case 3: return (float)c._attackMs;
        case 4: return (float)c._releaseMs;
        case 5: return c._lookaheadMs;
        } break;
    }
    case DisplayModuleID::EXCITER: {
        Exciter& e = _pipeline->getExciter();
        switch (idx) {
        case 0: return (float)e._fCut;
        case 1: return (float)e._dry;
        case 2: return (float)e._wet;
        } break;
    }
    case DisplayModuleID::DYNAMIC_BASS: {
        DynamicBass& b = _pipeline->getDynamicBass();
        switch (idx) {
        case 0: return (float)b._fCut;
        case 1: return (float)b._gainBoostDb / 100.0f;
        case 2: return b._enhanced ? 1.0f : 0.0f;
        case 3: return (float)b._boostFullDb / 100.0f;
        case 4: return (float)b._neutralDb   / 100.0f;
        case 5: return (float)b._clipFullDb  / 100.0f;
        case 6: return (float)b._clipattack;
        case 7: return (float)b._cliprelease;
        case 8: return b._lookaheadMs;
        } break;
    }
    case DisplayModuleID::DYNAMIC_EQ_THRESH: {
        DynamicEQ& d = _pipeline->getDynamicEq();
        switch (idx) {
        case 0: return (float)d._lowThreshDb    / 100.0f;
        case 1: return (float)d._normalThreshDb / 100.0f;
        case 2: return (float)d._highThreshDb   / 100.0f;
        case 3: return (float)d._attackMs;
        case 4: return (float)d._releaseMs;
        case 5: return d._lookaheadMs;
        } break;
    }
    case DisplayModuleID::ISF1:
    case DisplayModuleID::ISF2: {
        IndexSelectableFilter& isf = (nav.module == DisplayModuleID::ISF1) ? _pipeline->getIsf1() : _pipeline->getIsf2();
        switch (idx) {
        case 0: return (float)isf.getRmsWindowMs();
        case 1: return (float)isf.getSlewMs();
        case 2: return isf.getLookaheadMs();
        } break;
    }
    default: break;
    }
    return 0.0f;
}

void Display::setParamValue(NavEntry nav, uint8_t idx, float val, UiParam* param) {
    if (!_pipeline) return;
    switch (nav.module) {
    case DisplayModuleID::PRE_GAIN:
    case DisplayModuleID::POST_GAIN: {
        VolumeControl* v = (nav.module == DisplayModuleID::PRE_GAIN) ? &_pipeline->getPreGain() : &_pipeline->getPostGain();
        switch (idx) {
        case 0: { v->setGainDb(FLOAT_TO_DB_Q8(val)); break; }
        case 1: v->setMute(val >= 0.5f);  break;
        case 2: v->setMono(val >= 0.5f);  break;
        } break;
    }
    case DisplayModuleID::COMPANDER: {
        Compander& c = _pipeline->getCompander();
        switch (idx) {
        case 0: c.setThreshold(val);                        break;
        case 1: c.setRatioBelow((val));                     break;
        case 2: c.setRatioAbove((val));                     break;
        case 3: c.setAttackTime((int32_t)val);              break;
        case 4: c.setReleaseTime((int32_t)val);             break;
        case 5: {
            c.setLookahead(val);
            if (val > 0 && param) { c.setAttackTime((int32_t)param[3].minVal); c.setReleaseTime((int32_t)param[4].minVal); }
            break;
        }
        } break;
    }
    case DisplayModuleID::EXCITER: {
        Exciter& e = _pipeline->getExciter();
        switch (idx) {
        case 0: e.setCutoffFreq((int32_t)val); break;
        case 1: e.setDry((int32_t)val);        break;
        case 2: e.setWet((int32_t)val);        break;
        } break;
    }
    case DisplayModuleID::DYNAMIC_BASS: {
        DynamicBass& b = _pipeline->getDynamicBass();
        switch (idx) {
        case 0: b.setCutoffFreq((int32_t)val);              break;
        case 1: b.setGainBoost((int32_t)(val * 100.0f));    break;
        case 2: b.setEnhanced(val >= 0.5f);                  break;
        case 3: b.setBoostFullThreshold((int32_t)(val * 100.0f)); break;
        case 4: b.setNeutralThreshold((int32_t)(val * 100.0f));   break;
        case 5: b.setClipFullThreshold((int32_t)(val * 100.0f));  break;
        case 6: b.setClipAttack((int32_t)val);              break;
        case 7: b.setClipRelease((int32_t)val);             break;
        case 8: {
            b.setLookahead(val);
            if (val > 0 && param) { b.setClipAttack((int32_t)param[6].minVal); b.setClipRelease((int32_t)param[7].minVal); }
            break;
        }
        } break;
    }
    case DisplayModuleID::DYNAMIC_EQ_THRESH: {
        DynamicEQ& d = _pipeline->getDynamicEq();
        switch (idx) {
        case 0: d.setLowEnergyThreshold((int32_t)(val * 100.0f));    break;
        case 1: d.setNormalEnergyThreshold((int32_t)(val * 100.0f)); break;
        case 2: d.setHighEnergyThreshold((int32_t)(val * 100.0f));   break;
        case 3: d.setAttackTime((int32_t)val);                       break;
        case 4: d.setReleaseTime((int32_t)val);                      break;
        case 5: {
            d.setLookahead(val);
            if (val > 0 && param) { d.setAttackTime((int32_t)param[3].minVal); d.setReleaseTime((int32_t)param[4].minVal); }
            break;
        }
        }
        break; // ← fix: was missing, caused fall-through into ISF1/ISF2 case
    }
    case DisplayModuleID::ISF1:
    case DisplayModuleID::ISF2: {
        IndexSelectableFilter& isf = (nav.module == DisplayModuleID::ISF1) ? _pipeline->getIsf1() : _pipeline->getIsf2();
        switch (idx) {
        case 0: isf.setRmsWindowMs((int32_t)val); break;
        case 1: isf.setSlewMs((int32_t)val);      break;
        case 2: isf.setLookahead(val);            break;
        } break;
    }
    default: break;
    }
}

void Display::formatFloat(char* buf, uint8_t bufLen, float val, uint8_t decimals, const char* unit) {
    static const char* const fmts[] = { "%.0f", "%.1f", "%.2f", "%.3f" };
    snprintf(buf, bufLen, decimals < 4 ? fmts[decimals] : "%.2f", val);
    if (unit) {
        snprintf(buf + strlen(buf), bufLen - strlen(buf), "%s", unit);
    }
}

uint16_t Display::blendColor(uint16_t a, uint16_t b, uint8_t t) {
    uint8_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    uint8_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    uint8_t rr = ar + ((int)(br - ar) * t / 255);
    uint8_t rg = ag + ((int)(bg - ag) * t / 255);
    uint8_t rb = ab + ((int)(bb - ab) * t / 255);
    return (uint16_t)((rr << 11) | (rg << 5) | rb);
}

/**
 * @brief Compute animated bg/border/text colors for a focusable row widget.
 *
 * Replaces the identical 3-color focus-blend block that was duplicated in
 * drawSliderRow, drawSwitchRow, drawInputRow, and drawNavButton.
 *
 * @param focused   Whether this widget currently has focus.
 * @param bg        [out] Background color.
 * @param border    [out] Border color.
 * @param textColor [out] Text color.
 */
void Display::getFocusColors(bool focused,
                             uint16_t& bg, uint16_t& border, uint16_t& textColor) {
    if (!focused) {
        bg        = Color::BG;
        border    = Color::BORDER;
        textColor = Color::TEXT;
        return;
    }
    if (_anim.focusTransition < 1.0f) {
        uint8_t bl = (uint8_t)(_anim.focusTransition * 255);
        bg        = blendColor(Color::BG,     Color::PANEL,       bl);
        border    = blendColor(Color::BORDER, Color::ACCENT,      bl);
        textColor = blendColor(Color::TEXT,   Color::TEXT_FOCUS,  bl);
    } else {
        bg        = Color::PANEL;
        border    = Color::ACCENT;
        textColor = Color::TEXT_FOCUS;
    }
}

void Display::init() {
    _tft.init();
    _tft.setRotation(1);
    _tft.fillScreen(Color::BG);

    // Setup PWM for TFT backlight control
    #if defined(TFT_BACKLIGHT_PIN) && TFT_BACKLIGHT_PIN >= 0
        ledcAttach(TFT_BACKLIGHT_PIN, 5000, 8);  // 5kHz, 8-bit resolution
        ledcWrite(TFT_BACKLIGHT_PIN, 255);        // Full brightness on init
    #endif

    #ifdef CONFIG_SPIRAM
        _spr.setColorDepth(16);
    #else
        _spr.setColorDepth(8);
    #endif
    if (_spr.createSprite(DISP_W, DISP_H)) {
        _sprReady = true;
        LOG_INFO(TAG, "Sprite allocated (%d x %d)", DISP_W, DISP_H);
    } else {
        LOG_WARN(TAG, "Sprite alloc failed — direct draw mode");
    }
    _splashStartMs = millis();
    pushScreen(ScreenID::SPLASH);
    _dirty = true;
    _anim.reset();
    LOG_INFO(TAG, "Display initialized (ST7789 %dx%d)", _tft.width(), _tft.height());
}

void Display::setPipeline(DspPipeline* pipeline, PresetManager* presetMgr) {
    _pipeline  = pipeline;
    _presetMgr = presetMgr;
    if (_presetMgr && _presetMgr->hasMainMenuParam()) {
        _presetMgr->loadMainMenuParam(&_mmparam);
    } else {
        _mmparam.vol = 32; _mmparam.bass = 50; _mmparam.mid = 50; _mmparam.treble = 50;
    }
    _mmparam.vol = constrain(_mmparam.vol, 0, 32);
    _mmparam.bass = constrain(_mmparam.bass, 0, 100);
    _mmparam.mid = constrain(_mmparam.mid, 0, 100);
    _mmparam.treble = constrain(_mmparam.treble, 0, 100);
    if (_pipeline) {
        VolumeControl& pre = _pipeline->getPreGain();
        float db = -32.0f + _mmparam.vol * 32.0f / 32.0f;
        pre.setGainDb(FLOAT_TO_DB_Q8(db));
        pre.setMute(_mmparam.vol <= 0);
        ParametricEQ& preEq = _pipeline->getPreEq();
        const int8_t toneParams[3] = { _mmparam.bass, _mmparam.mid, _mmparam.treble };
        for (uint8_t i = 0; i < 3; i++) {
            EQFilterParams p = preEq.getBandParams(i);
            float gainDb = ((float)toneParams[i] / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam;
            p.gain = FLOAT_TO_DB_Q8(gainDb);
            preEq.setBand(i, p);
        }
    }
}

void Display::setSettings(SettingsManager* settingsMgr) {
    _settingsMgr = settingsMgr;
    if (_settingsMgr) {
        // Load WiFi setting
        _settingsWifi = _settingsMgr->getWifiEnabled();
        
        // Display brightness - apply PWM duty cycle
        #if defined(TFT_BACKLIGHT_PIN) && TFT_BACKLIGHT_PIN >= 0
            uint8_t brightness = _settingsMgr->getBrightness();
            // Brightness is stored as 0-255, use directly
            ledcWrite(TFT_BACKLIGHT_PIN, brightness);
        #endif
    }
}

void Display::pushKeyboard(float* target, float minVal, float maxVal, bool allowDot, ScreenID returnTo) {
    _kb.target = target; _kb.minVal = minVal; _kb.maxVal = maxVal;
    _kb.allowDot = allowDot; _kb.returnScreen = returnTo;
    _kb.len = 0; _kb.buf[0] = '\0'; _kb.cursorCol = 0; _kb.cursorRow = 0;
    if (target) {
        char tmp[KB_MAX_LEN + 1];
        formatFloat(tmp, sizeof(tmp), *target, allowDot ? 2 : 0);
        strncpy(_kb.buf, tmp, KB_MAX_LEN); _kb.len = strlen(_kb.buf);
    }
    pushScreen(ScreenID::KEYBOARD);
}

void Display::setStats(uint16_t cpuTenths, uint8_t heapPct,
                       uint32_t sampleRate, ConnectStatus connectStatus, bool clockAbsent) {
    bool changed = (cpuTenths != _cpuTenths) || (heapPct != _heapPct) ||
                   (sampleRate != _sampleRate) || (connectStatus != _connectStatus) || (clockAbsent != _clockAbsent);
    _cpuTenths = cpuTenths; _heapPct = 100 - heapPct; _sampleRate = sampleRate;
    _connectStatus = connectStatus; _clockAbsent = clockAbsent;
    if (changed) _dirty = true;
}

void Display::pushScreen(ScreenID s, DisplayModuleID mod, uint8_t subCtx) {
    if (_navTop < NAV_STACK_DEPTH - 1) _navTop++;
    _navStack[_navTop] = { s, mod, subCtx };
    _focusIdx = 0; _editMode = false; _paramScroll = 0; _holdTracking = false;
    if (s == ScreenID::EFFECT_EQ_COMMON || s == ScreenID::EFFECT_EQ_GRAPH) {
        ParametricEQ* eq = getEqPtr(mod);
        if (eq) { _eqBandCount = countEnabledEqBands(eq); if (_eqBandCount == 0) _eqBandCount = 1; }
    }
    if (s == ScreenID::EQ_BAND_EDIT) {
        ParametricEQ* eq = getEqPtr(mod);
        if (eq && subCtx < MAX_EQ_BANDS) _bandEditType = (EQFilterType)eq->getBandParams(subCtx).type;
    }
    if (s == ScreenID::EFFECT_EQ_GRAPH) {
        ParametricEQ* eq = getEqPtr(mod);
        if (eq) for (uint8_t i = 0; i < MAX_EQ_BANDS; i++) { if (eq->getBandParams(i).enabled) { _focusIdx = i; break; } }
    }
    _dirty = true;
}

void Display::popScreen() {
    if (_navTop > 0) _navTop--;
    _focusIdx = 1; _mainMenuTab = 0; _mainMenuActiveParam = 0; _editMode = false; _paramScroll = 0; _holdTracking = false; _dirty = true;
}

NavEntry& Display::currentNav() { return _navStack[_navTop]; }
ScreenID  Display::currentScreen() const { return _navStack[_navTop].screen; }

void Display::update(EncoderEvent enc) {
    if (g_timerShutdownTriggered) {
        _powerOffUserRequest = true;
        _powerOffScreenActive = true;
        _powerOffStartMs = millis();
        _dirty = true;
        return; 
    }

    // Hold power-off screen for 5 s then set shutdown flag
    if (_powerOffScreenActive) {
        if (millis() - _powerOffStartMs >= 5000) {
            g_userShutdownRequest = true;
        }
        _dirty = true;  // keep re-drawing (countdown animation possible later)
    }
    
    // ── Splash auto-advance ──
    if (currentScreen() == ScreenID::SPLASH) {
        if (millis() - _splashStartMs >= SPLASH_DURATION_MS) {
            _navStack[_navTop] = { ScreenID::MAIN_MENU, DisplayModuleID::NONE, 0 };
            _focusIdx = 1; _mainMenuTab = 0; _mainMenuActiveParam = 0; _dirty = true;
            // ── Start volume animation: 0 → vol with deceleration ──
            _anim.volAnimating = true;
            _anim.volFrom = 0.0f;
            _anim.volTo = (float)_mmparam.vol;
            _anim.volStartMs = millis();
        }
    }

    // ── Live meter polling (10 Hz for main menu and DSP list) ──
    if ((currentScreen() == ScreenID::MAIN_MENU || currentScreen() == ScreenID::DSP_LIST) && _pipeline) {
        uint32_t now = millis();
        if (now - _lastMeterUpdate >= METER_UPDATE_INTERVAL_MS) {
            _lastMeterUpdate = now;
            Compander& comp = _pipeline->getCompander();
            _companderEnv = comp.isEnabled() ? comp.getEnvLinear() : 0.0f;
            _companderGainDb = comp.isEnabled() ? comp.getGainDb() : 0.0f;
            DRC& drc = _pipeline->getDrc();
            for (uint8_t i = 0; i < 4; i++) _drcGainDb[i] = drc.isEnabled() ? drc.getBandGainDb(i) : 0.0f;
            DynamicBass& dynBass = _pipeline->getDynamicBass();
            _dynBassAlpha = dynBass.isEnabled() ? dynBass.getAlpha() : 0.0f;
            _dynBassEnergyDb = dynBass.isEnabled() ? dynBass.getEnergyDb() : -96.0f;
            DynamicEQ& dynEq = _pipeline->getDynamicEq();
            _dynEqAlphaLow = dynEq.isEnabled() ? dynEq.getAlphaLow() : 0.0f;
            _dynEqAlphaHigh = dynEq.isEnabled() ? dynEq.getAlphaHigh() : 0.0f;
            _dynEqEnergyDb = dynEq.isEnabled() ? dynEq.getEnergyDb() : -96.0f;
            _dirty = true;
        }
    }

    // ── Battery monitor poll + power-off trigger ──────────────────────────────
    if (_battery && _battery->isPresent()) {
        _battery->update();   // BatteryMonitor::update() is rate-limited internally
        const BatteryStatus& bat = _battery->getStatus();

        if (bat.state == BatteryState::CRITICAL && !_powerOffScreenActive) {
            _powerOffScreenActive = true;
            _powerOffStartMs = millis();
            _dirty = true;
        }

        // Battery badge makes header dirty ~1 Hz
        if (millis() - _lastBatteryDrawMs >= 1000) {
            _lastBatteryDrawMs = millis();
            if (currentScreen() == ScreenID::MAIN_MENU) _dirty = true;
        }
    }

    updateAnimations();

    // ── Main menu tab-return hold poll ────────────────────────────────────────
    // SW_PRESS armed _mmTabReturnArmed. If still held after TAB_RETURN_HOLD_MS
    // we jump focusIdx back to 0 (tab bar). The arm is cleared on SW release.
    if (_mmTabReturnArmed && currentScreen() == ScreenID::MAIN_MENU) {
        uint32_t held = millis() - _mmTabReturnStartMs;
        if (held >= TAB_RETURN_HOLD_MS) {
            _mmTabReturnArmed = false;
            _mmTabReturnViaHold = true;   // next SW release at the tab bar should flip tabs
            _anim.prevFocusIdx = _focusIdx;
            _focusIdx = 0;
            _anim.focusTransition = 0.0f;
            _anim.focusStartMs = millis();
            _dirty = true;
        } else {
            _dirty = true; // keep redrawing so progress indicator animates
        }
    }

    if (enc != EncoderEvent::NONE) handleEncoder(enc);

    if (_mainMenuAutosaveArmed && _pipeline && _presetMgr) {
        if (millis() - _mainMenuLastEditMs >= 3000) {
            _mainMenuAutosaveArmed = false;
            _presetMgr->saveMainMenuParam(&_mmparam);
        }
    }

    // ── Power-off screen overrides everything ─────────────────────────────────
    if (_powerOffScreenActive) {
        if (millis() - _powerOffStartMs >= 2000) {
            #if defined(TFT_BACKLIGHT_PIN) && TFT_BACKLIGHT_PIN >= 0
                ledcWrite(TFT_BACKLIGHT_PIN, _screenFadeOutValue);  // fade out backlight
            #endif
            if (_screenFadeOutValue > 0) {
                _screenFadeOutValue = _screenFadeOutValue - 5;
            }
        }
    }

    if (_dirty) { draw(); _dirty = false; }
}

void Display::handleEncoder(EncoderEvent enc) {
    ScreenID s = currentScreen();
    int8_t dir = 0;
    if      (enc == EncoderEvent::CW)  dir = +1;
    else if (enc == EncoderEvent::CCW) dir = -1;

    if (enc == EncoderEvent::SW_HOLD5) {
        _powerOffUserRequest = true;
        _powerOffScreenActive = true;
        _powerOffStartMs = millis();
        _dirty = true;
        return; 
    }

    // ── SW_PRESS — arm tab-return hold in main menu ───────────────────────────
    // Fires immediately on button-down. If the user releases within 500 ms the
    // normal SW / SW_DOUBLE / SW_HOLD* events handle the action. If held ≥ 500 ms
    // update() fires the tab-return before any hold event.
    if (enc == EncoderEvent::SW_PRESS) {
        _globalHoldArmed    = true;
        _globalHoldStartMs  = millis();
        if (s == ScreenID::MAIN_MENU && _focusIdx > 0) {
            _mmTabReturnArmed   = true;
            _mmTabReturnStartMs = millis();
        }
        return; // SW_PRESS is an arm signal only — no further action here
    } else if (g_softLatchPinIsAvailable) {
        bool btnIsPressed = (digitalRead(POWER_PIN_OFF) == LOW); // LOW = pressed
        if (btnIsPressed) {
            _globalHoldArmed    = true;
            _globalHoldStartMs  = millis();
            if (!g_shutdownButtonIsHolding) {
                g_shutdownButtonIsHolding = true;
                g_shutdownCountdown = millis();
            } else if (millis() - g_shutdownCountdown >= 5000) {
                _powerOffUserRequest = true;
                _powerOffScreenActive = true;
                _powerOffStartMs = millis();
                _dirty = true;
            }
        }
        else {
            _globalHoldArmed = false;
            g_shutdownButtonIsHolding = false;
            g_shutdownCountdown = 0;
        }
        return; // SW_PRESS is an arm signal only — no further action here
    }

    // ── SW release events — always disarm the tab-return hold ─────────────────
    // SW, SW_DOUBLE, SW_HOLD3, SW_HOLD5 all represent button-released or
    // threshold-fired; in any case the arm should be cleared.
    if (enc == EncoderEvent::SW || enc == EncoderEvent::SW_DOUBLE ||
        enc == EncoderEvent::SW_HOLD3 || enc == EncoderEvent::SW_HOLD5) {
        _mmTabReturnArmed = false;
        _globalHoldArmed  = false;
    }

    // A double-click or HOLD3 means something other than a plain tab-bar release
    // happened — cancel any pending hold-to-switch-tab flip so it doesn't fire later.
    if (enc == EncoderEvent::SW_DOUBLE || enc == EncoderEvent::SW_HOLD3) {
        _mmTabReturnViaHold = false;
    }

    if (enc == EncoderEvent::SW_HOLD3) {
        if (s == ScreenID::MAIN_MENU) {
            pushScreen(ScreenID::SETTINGS);
            #ifdef ONLY_SERIAL
            _focusIdx = 1;   // skip WiFi toggle in serial-only builds
            #endif
            _dirty = true;
            return; 
        }
        if (s != ScreenID::SPLASH) { while (_navTop > 0) _navTop--;
            _navStack[_navTop] = { ScreenID::MAIN_MENU, DisplayModuleID::NONE, 0 };
            _focusIdx = 0; _editMode = false; _paramScroll = 0; _holdTracking = false; _dirty = true; }
        return;
    }

    if (dir != 0 && _editMode) { editDelta(dir); _dirty = true; return; }

    if (dir != 0) {
        int16_t next = (int16_t)_focusIdx + dir;
        if (s == ScreenID::KEYBOARD) { editDelta(dir); _dirty = true; return; }
        if (s == ScreenID::MAIN_MENU) {
            if (_focusIdx == 0) {
                // At tab bar: CW/CCW switches the active tab
                _mainMenuTab = (_mainMenuTab + (dir > 0 ? 1 : -1) + 2) % 2;
                _focusIdx = 1;   // jump into first item of the newly selected tab
                _mmTabReturnViaHold = false;   // explicit rotate overrides any pending hold-flip
                if (_mainMenuTab == 0) _mainMenuActiveParam = 0;   // sync with pill 0 (Vol)
                startFocusTransition();
            } else if (_mainMenuTab == 0) {
                // PARAMS tab: CW/CCW adjusts the value of the currently focused pill
                editDelta(dir);
            } else {
                // CTRL tab: CW/CCW moves focus among the 4 buttons (wrap 1..4, never 0 —
                // 0 is reserved for the tab bar)
                int16_t next = (int16_t)_focusIdx + dir;
                if (next < 1) next = 4;
                if (next > 4) next = 1;
                _focusIdx = (uint8_t)next;
                startFocusTransition();
            }
            _dirty = true; return;
        }
        else if (s == ScreenID::SETTINGS) {
            _itemCount = getItemCount();
            if (next < 0) next = (int16_t)(_itemCount - 1);
            if (next >= _itemCount) next = 0;
            #ifdef ONLY_SERIAL
            // Skip WiFi toggle (index 0) in serial-only builds, in either direction,
            // including when wrap-around would land back on it.
            while (next == 0) {
                next += dir;
                if (next < 0) next = (int16_t)(_itemCount - 1);
                if (next >= _itemCount) next = 0;
            }
            #endif
        }
        else {
            _itemCount = getItemCount();
            if (next < 0) next = (int16_t)(_itemCount - 1);
            if (next >= _itemCount) next = 0;
        }

        if (s == ScreenID::EFFECT_EQ_GRAPH && (uint8_t)next < MAX_EQ_BANDS) {
            ParametricEQ* eq = getEqPtr(currentNav().module);
            uint8_t tries = 0;
            while (eq && tries < MAX_EQ_BANDS && !eq->getBandParams((uint8_t)next).enabled) {
                next += dir;
                if (next < 0) next = (int16_t)(_itemCount - 1);
                if (next >= _itemCount) next = 0;
                if ((uint8_t)next >= MAX_EQ_BANDS) break;
                tries++;
            }
        }

        _anim.prevFocusIdx = _focusIdx;
        _focusIdx = (uint8_t)next;
        _holdTracking = false;
        _anim.focusTransition = 0.0f;
        _anim.focusStartMs = millis();
        onFocusChanged();
        _dirty = true;
        return;
    }

    if (enc == EncoderEvent::SW_DOUBLE) {
        switch (s) {
        case ScreenID::MAIN_MENU: { 
            pushScreen(ScreenID::DSP_LIST);
            _anim.reset();
            _presetTargetSlot = _presetMgr->getCurrentSlotIndex();
            _anim.presetSweepFinishedBg[_presetTargetSlot] = Color::ACCENT;
            break;
        }
        case ScreenID::DSP_LIST: {
            uint8_t moduleIdx = _focusIdx - DSP_LIST_MODULE_START_IDX;
            if (moduleIdx < MODULE_COUNT) {
                const ModuleEntry& m = MODULE_LIST[moduleIdx];
                if (m.isIsf) pushScreen(ScreenID::EFFECT_ISF_COMMON, m.id);
                else if (m.isDrc) pushScreen(ScreenID::DRC_CONFIG, m.id);
                else if (m.isEq) pushScreen(ScreenID::EFFECT_EQ_COMMON, m.id);
                else pushScreen(ScreenID::EFFECT_COMMON, m.id);
            }
            break;
        }
        default: break;
        }
        _dirty = true; return;
    }

    if (enc == EncoderEvent::SW) {
        if (s == ScreenID::MAIN_MENU) {
            if (_focusIdx == 0) {
                if (_mmTabReturnViaHold && enc != EncoderEvent::SW_DOUBLE && enc != EncoderEvent::SW_HOLD3) {
                        // This SW release completes a hold-to-switch gesture (held inside
                        // a tab until the hold-poll snapped focus back to the tab bar) →
                        // flip to the other tab as the shortcut.
                        _mmTabReturnViaHold = false;
                        _mainMenuTab = (_mainMenuTab + 1) % 2;
                    }
                    _focusIdx = 1;           // jump into first item of the (possibly new) tab
                    if (_mainMenuTab == 0) _mainMenuActiveParam = 0;   // sync with pill 0 (Vol)
                    startFocusTransition();
            } else if (_mainMenuTab == 0) {
                // PARAMS tab: SW on pill 1-4 → cycle to next pill + update display
                // Wrap is 1→2→3→4→1: must NEVER land on 0 (that's the tab-bar sentinel)
                if (_focusIdx >= 1 && _focusIdx <= 4) {
                    _focusIdx = (_focusIdx % 4) + 1;
                    _mainMenuActiveParam = _focusIdx - 1;   // convert 1..4 → 0..3 index
                    startFocusTransition();
                }
            } else {
                // CTRL tab: SW activates the focused button
                if (_focusIdx == 1) {
                    // BT Kick
                    // TODO: declare in display.h: std::function<void()> onBtKick;
                    if (onBtKick) onBtKick();
                } else if (_focusIdx == 2) {
                    editDelta(+1);       // + button
                } else if (_focusIdx == 3) {
                    // Mute/unmute toggle
                    if (_pipeline) {
                        VolumeControl& pre = _pipeline->getPreGain();
                        if (_mmparam.vol <= 0) {
                            _mmparam.vol = 16;
                            float db = -32.0f + _mmparam.vol * 32.0f / 32.0f;
                            pre.setGainDb(FLOAT_TO_DB_Q8(db));
                            pre.setMute(false);
                        } else {
                            pre.setMute(true);
                            _mmparam.vol = 0;
                        }
                        _mainMenuLastEditMs = millis();
                        _mainMenuAutosaveArmed = true;
                    }
                } else if (_focusIdx == 4) {
                    editDelta(-1);       // − button
                }
            }
            _dirty = true; return;
        }
        if (s == ScreenID::SETTINGS) {
            const uint8_t backIdx = getItemCount() - 1;
            if (_focusIdx == backIdx) {
                popScreen(); _dirty = true; return;
            }
            // WiFi toggle - direct toggle, no edit mode
            if (_focusIdx == 0) {
                _settingsWifi = !_settingsWifi;
                if (_settingsMgr) _settingsMgr->setWifiEnabled(_settingsWifi);
                wifiOnOffTriggered = true;
                _dirty = true; return;
            }
            // Brightness and battery items - toggle edit mode with sweep animation
            toggleEditMode();
            _dirty = true; return;
        }
        if (s == ScreenID::DSP_LIST) {
            const uint8_t backIdx = getItemCount() - 1;
            if (_focusIdx == backIdx) { popScreen(); _dirty = true; return; }
            if (_focusIdx < MAX_PRESET_SLOTS) {
                if (_presetMgr && _pipeline) {
                    int8_t oldSlot = (int8_t) _presetTargetSlot;
                    _presetTargetSlot = _focusIdx;
                    _presetMgr->loadPreset(_presetTargetSlot, *_pipeline);
                    _presetMgr->saveCurrentSlotIndex(_presetTargetSlot);
                    // Preset sweep: old slot exit → new slot entry
                    if (oldSlot != (int8_t)_focusIdx) {
                        _anim.presetPrevSlot = oldSlot;
                        _anim.presetSweepSlot = (int8_t)_focusIdx;
                        _anim.presetSweepPhase2 = false;
                        _anim.presetSweepProgress = 0.0f;
                        _anim.presetSweepStartMs = millis();
                        _anim.presetSweepActive = true;
                    } else {
                        _anim.presetSweepActive = false;
                        _anim.presetPrevSlot = -1;
                    }
                }
                _dirty = true; return;
            }
            if (_focusIdx == DSP_LIST_PRESET_SAVE_IDX) {
                if (_presetMgr && _pipeline) {
                    _presetMgr->savePreset(_presetTargetSlot, *_pipeline);
                    // Save button: entry then exit sweep
                    _anim.saveSweepActive = true;
                    _anim.saveSweepPhase2 = false;
                    _anim.saveSweepProgress = 0.0f;
                    _anim.saveSweepStartMs = millis();
                }
                _dirty = true; return;
            }
            if (_focusIdx >= DSP_LIST_MODULE_START_IDX) {
                uint8_t moduleIdx = _focusIdx - DSP_LIST_MODULE_START_IDX;
                if (moduleIdx < MODULE_COUNT) {
                    DspModule* mod = getModulePtr(MODULE_LIST[moduleIdx].id);
                    if (mod && mod->getModuleId() != MODULE_ID_PRE_GAIN && mod->getModuleId() != MODULE_ID_POST_GAIN) {
                        bool wasOn = mod->isEnabled();
                        mod->setEnabled(!wasOn);
                        // Module toggle sweep
                        _anim.modSweepActive = true;
                        _anim.modSweepExit = wasOn;  // if was on→off = exit sweep
                        _anim.modSweepProgress = 0.0f;
                        _anim.modSweepStartMs = millis();
                        _anim.modSweepIdx = (int8_t)moduleIdx;
                    }
                }
                _dirty = true; return;
            }
        }
        if (s == ScreenID::MAIN_MENU && _focusIdx < 4) {
            _mainMenuLastEditMs = millis();
            _mainMenuAutosaveArmed = true;
        }
        if (s == ScreenID::EFFECT_EQ_COMMON) {
            uint8_t cnt = getItemCount();
            if (_focusIdx == 0) _editMode = !_editMode;
            else if (_focusIdx >= 1 && _focusIdx <= MAX_EQ_BANDS) {
                ParametricEQ* eq = getEqPtr(currentNav().module);
                toggleEqBandSlot(eq, (uint8_t)(_focusIdx - 1));
                _eqBandCount = countEnabledEqBands(eq);
                _editMode = false;
            } else onConfirm();
            _dirty = true; return;
        }
        onConfirm(); _dirty = true; return;
    }
}

void Display::onFocusChanged() { startFocusTransition();
    ScreenID s = currentScreen(); uint8_t cnt = getItemCount();
    if (s == ScreenID::EFFECT_COMMON) {
        uint8_t paramCount = (cnt > 0) ? cnt - 1 : 0;
        if (_focusIdx >= paramCount) return;
        if ((int8_t)_focusIdx < _paramScroll) _paramScroll = (int8_t)_focusIdx;
        if ((int8_t)_focusIdx >= _paramScroll + VISIBLE_ROWS_COMMON) _paramScroll = (int8_t)_focusIdx - VISIBLE_ROWS_COMMON + 1;
    } else if (s == ScreenID::EFFECT_EQ_COMMON) {
        uint8_t navStart = (cnt >= 2) ? cnt - 2 : 0;
        if (_focusIdx == 0) { _paramScroll = 0; return; }
        if (_focusIdx >= navStart) return;
        int8_t bandFocus = (int8_t)(_focusIdx - 1);
        if (bandFocus < _paramScroll) _paramScroll = bandFocus;
        if (bandFocus >= _paramScroll + VISIBLE_ROWS_EQ) _paramScroll = bandFocus - VISIBLE_ROWS_EQ + 1;
    } else if (s == ScreenID::DRC_BAND_PARAMS) {
        int8_t pi = _focusIdx; if (pi >= 6) return;
        if (pi < _paramScroll) _paramScroll = pi;
        if (pi >= _paramScroll + VISIBLE_ROWS_DRC_PARAMS) _paramScroll = pi - VISIBLE_ROWS_DRC_PARAMS + 1;
    } else if (s == ScreenID::DRC_CONFIG) {
        int8_t pi = _focusIdx - 1; if (pi < 0 || pi >= 4) return;
        if (pi < _paramScroll) _paramScroll = pi;
        if (pi >= _paramScroll + VISIBLE_ROWS_DRC_CONFIG) _paramScroll = pi - VISIBLE_ROWS_DRC_CONFIG + 1;
    } else if (s == ScreenID::EFFECT_ISF_COMMON) {
        constexpr uint8_t cfgEnd = 5; uint8_t backIdx = cnt - 1;
        if (_focusIdx < cfgEnd || _focusIdx == backIdx) return;
        uint8_t presetIdx = _focusIdx - cfgEnd;
        if (presetIdx < (uint8_t)_paramScroll) _paramScroll = presetIdx;
        if (presetIdx >= (uint8_t)(_paramScroll + VISIBLE_ISF_PRESET)) _paramScroll = presetIdx - VISIBLE_ISF_PRESET + 1;
    }
}

void Display::onConfirm() {
    ScreenID s = currentScreen(); NavEntry& nav = currentNav();
    switch (s) {
    case ScreenID::SETTINGS: {
        const uint8_t backIdx = getItemCount() - 1;
        if (_focusIdx == backIdx) {
            popScreen();
        } else if (_battery && _battery->isPresent() && _focusIdx == 6) {
            // Reset coulomb counter (triggered by hold via SW_HOLD handler)
            _battery->resetCoulombCounter();
        } else {
            toggleEditMode();
        }
        break;
    }
    case ScreenID::EFFECT_COMMON: {
        uint8_t cnt = getItemCount();
        if (_focusIdx == cnt - 1) popScreen(); else toggleEditMode();
        break;
    }
    case ScreenID::EFFECT_EQ_COMMON: {
        uint8_t cnt = getItemCount();
        if (_focusIdx == cnt - 1) popScreen();
        else if (_focusIdx == cnt - 2) pushScreen(ScreenID::EFFECT_EQ_GRAPH, nav.module, nav.subContext);
        break;
    }
    case ScreenID::EFFECT_EQ_GRAPH:
        if (_focusIdx == getItemCount() - 1) popScreen();
        else if (_focusIdx < MAX_EQ_BANDS) {
            ParametricEQ* eq = getEqPtr(nav.module);
            if (eq && eq->getBandParams(_focusIdx).enabled) pushScreen(ScreenID::EQ_BAND_EDIT, nav.module, _focusIdx);
        }
        break;
    case ScreenID::EQ_BAND_EDIT: {
        uint8_t cnt = getItemCount(), backIdx = cnt - 1;
        if (_focusIdx == backIdx) popScreen(); else toggleEditMode();
        break;
    }
    case ScreenID::EFFECT_ISF_COMMON: {
        uint8_t cnt = getItemCount(), backIdx = cnt - 1, firstPresetIdx = 5;
        if (_focusIdx == backIdx) popScreen();
        else if (_focusIdx == 3) {
            if (_pipeline) {
                auto& isf = (nav.module == DisplayModuleID::ISF1) ? _pipeline->getIsf1() : _pipeline->getIsf2();
                if (isf.getNumPresets() < ISF_MAX_PRESETS) isf.setNumPresets(isf.getNumPresets() + 1);
            }
        } else if (_focusIdx == 4) {
            if (_pipeline) {
                auto& isf = (nav.module == DisplayModuleID::ISF1) ? _pipeline->getIsf1() : _pipeline->getIsf2();
                if (isf.getNumPresets() > 1) isf.setNumPresets(isf.getNumPresets() - 1);
            }
        } else if (_focusIdx >= firstPresetIdx) {
            uint8_t presetIdx = _focusIdx - firstPresetIdx;
            _isfEditPreset = presetIdx;
            pushScreen(ScreenID::EFFECT_EQ_COMMON, nav.module, presetIdx);
        } else if (_focusIdx < 3) {
            _kbContext = nav; _kbParamIdx = _focusIdx;
            UiParam params[3]; getScreenParams(nav, params);
            float curVal = getParamValue(nav, _focusIdx);
            static float tempVal; tempVal = curVal;
            pushKeyboard(&tempVal, params[_focusIdx].minVal, params[_focusIdx].maxVal, false, ScreenID::EFFECT_ISF_COMMON);
        }
        break;
    }
    case ScreenID::DRC_CONFIG: {
        uint8_t cnt = getItemCount();
        if (_focusIdx == cnt - 1) popScreen();
        else if (_focusIdx == cnt - 2) pushScreen(ScreenID::DRC_BAND_SELECT, nav.module);
        else if (_focusIdx == 0 || _focusIdx == 1) toggleEditMode();
        else {
            _kbContext = nav; _kbParamIdx = _focusIdx - 2;
            static float tempVal;
            if (_pipeline) {
                DRC& drc = _pipeline->getDrc();
                if (_kbParamIdx == 0) tempVal = (float)drc._fc[0];
                else if (_kbParamIdx == 1) tempVal = Q_Q610_TO_FLOAT(drc._qLp);
                else if (_kbParamIdx == 2) tempVal = (float)drc._fc[1];
                else if (_kbParamIdx == 3) tempVal = Q_Q610_TO_FLOAT(drc._qHp);
                else tempVal = 0.0f;
            } else tempVal = 0.0f;
            bool isQ = (_kbParamIdx == 1 || _kbParamIdx == 3);
            pushKeyboard(&tempVal, isQ ? 0.1f : 20.0f, isQ ? 4.0f : 20000.0f, isQ, ScreenID::DRC_CONFIG);
        }
        break;
    }
    case ScreenID::DRC_BAND_SELECT:
        if (_focusIdx == getItemCount() - 1) popScreen();
        else { _drcActiveBand = _focusIdx; pushScreen(ScreenID::DRC_BAND_PARAMS, nav.module); }
        break;
    case ScreenID::DRC_BAND_PARAMS: {
        uint8_t cnt = getItemCount();
        if (_focusIdx == cnt - 1) popScreen();
        else if (_focusIdx < 6) toggleEditMode();
        break;
    }
    case ScreenID::KEYBOARD: {
        if (_kb.cursorRow < KB_NUM_ROWS) {
            char key = KB_ROWS[_kb.cursorRow][_kb.cursorCol];
            if (_kb.len < KB_MAX_LEN) {
                if (key == '.' && !_kb.allowDot) break;
                _kb.buf[_kb.len++] = key; _kb.buf[_kb.len] = '\0';
            }
        } else {
            const char* action = KB_ACTIONS[_kb.cursorCol];
            if (strcmp(action, "DEL") == 0) { if (_kb.len > 0) _kb.len--; _kb.buf[_kb.len] = '\0'; }
            else if (strcmp(action, "CANCEL") == 0) popScreen();
            else if (strcmp(action, "ENTER") == 0) {
                float val = atof(_kb.buf); val = constrain(val, _kb.minVal, _kb.maxVal);
                if (_kb.target) *_kb.target = val;
                if (_kb.returnScreen == ScreenID::EFFECT_ISF_COMMON) setParamValue(_kbContext, _kbParamIdx, val, nullptr);
                else if (_kb.returnScreen == ScreenID::DRC_CONFIG && _pipeline) {
                    DRC& drc = _pipeline->getDrc();
                    if (_kbParamIdx == 0) drc.setCrossoverFreq(0, (int32_t)val);
                    else if (_kbParamIdx == 1) drc.setCrossoverQ(0, FLOAT_TO_Q_Q610(val));
                    else if (_kbParamIdx == 2) drc.setCrossoverFreq(1, (int32_t)val);
                    else if (_kbParamIdx == 3) drc.setCrossoverQ(1, FLOAT_TO_Q_Q610(val));
                }
                popScreen();
            }
        }
        break;
    }
    default: break;
    }
}

void Display::editDelta(int8_t dir) {
    ScreenID s = currentScreen(); NavEntry& nav = currentNav();
    if (s == ScreenID::MAIN_MENU && _pipeline) {
        // Active param: in PARAMS tab use pill focus (1-4 → 0-3),
        // in CTRL tab or tab bar → use _mainMenuActiveParam directly.
        uint8_t p = _mainMenuActiveParam;
        if (_mainMenuTab == 0)
            p = _focusIdx - 1;   // focusIdx is 1..4 in PARAMS tab; paramValues[]/_mmparam fields are 0..3

        float oldValue = 0.0f, newValue = 0.0f;
        if (p == 0) {
            VolumeControl& pre = _pipeline->getPreGain();
            oldValue = (float)_mmparam.vol;
            _mmparam.vol = constrain(_mmparam.vol + dir, 0, 32);
            newValue = (float)_mmparam.vol;
            float db = -32.0f + _mmparam.vol * 32.0f / 32.0f;
            pre.setGainDb(FLOAT_TO_DB_Q8(db)); pre.setMute(_mmparam.vol <= 0);
        } else if (p == 1) {
            ParametricEQ& pre = _pipeline->getPreEq();
            float old = ((float)_mmparam.bass / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam;
            _mmparam.bass = constrain(_mmparam.bass + dir, 0, 100);
            float nxt = ((float)_mmparam.bass / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam;
            oldValue = old; newValue = nxt;
            EQFilterParams fp = pre.getBandParams(0); fp.gain = FLOAT_TO_DB_Q8(nxt); pre.setBand(0, fp);
        } else if (p == 2) {
            ParametricEQ& pre = _pipeline->getPreEq();
            float old = ((float)_mmparam.mid / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam;
            _mmparam.mid = constrain(_mmparam.mid + dir, 0, 100);
            float nxt = ((float)_mmparam.mid / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam;
            oldValue = old; newValue = nxt;
            EQFilterParams fp = pre.getBandParams(1); fp.gain = FLOAT_TO_DB_Q8(nxt); pre.setBand(1, fp);
        } else if (p == 3) {
            ParametricEQ& pre = _pipeline->getPreEq();
            float old = ((float)_mmparam.treble / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam;
            _mmparam.treble = constrain(_mmparam.treble + dir, 0, 100);
            float nxt = ((float)_mmparam.treble / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam;
            oldValue = old; newValue = nxt;
            EQFilterParams fp = pre.getBandParams(2); fp.gain = FLOAT_TO_DB_Q8(nxt); pre.setBand(2, fp);
        }
        startValueAnimation(oldValue, newValue);
        _mainMenuLastEditMs = millis(); _mainMenuAutosaveArmed = true;
        _dirty = true; return;
    }
    if (s == ScreenID::EQ_BAND_EDIT && _focusIdx == 0) {
        ParametricEQ* eq = getEqPtr(nav.module);
        uint8_t bandIdx = nav.subContext;
        EQFilterType oldType = _bandEditType;
        int8_t t = (int8_t)_bandEditType + dir;

        if (t < 0) t = (int8_t)EQFilterType::EQ_FILTER_TYPE_COUNT - 3;
        if (t >= (int8_t)EQFilterType::EQ_FILTER_TYPE_COUNT - 2) t = 0;
        _bandEditType = (EQFilterType)t;

        bool oldGain = EQ_FILTER_HAS_GAIN[(uint8_t)oldType], newGain = EQ_FILTER_HAS_GAIN[(uint8_t)_bandEditType];
        if (oldGain && !newGain && _focusIdx > 1) _focusIdx--;
        else if (!oldGain && newGain && _focusIdx > 0) _focusIdx++;
        _itemCount = getItemCount();
        if (_focusIdx >= _itemCount) _focusIdx = _itemCount - 1;
        if (eq && bandIdx < MAX_EQ_BANDS) {
            EQFilterParams p = eq->getBandParams(bandIdx);
            p.type = (int16_t)_bandEditType; eq->setBand(bandIdx, p); 
        }
        _dirty = true; return;
    }
    if (s == ScreenID::KEYBOARD) {
        int8_t col = _kb.cursorCol, row = _kb.cursorRow;
        if (dir > 0) { col++; if (row < KB_NUM_ROWS) { if (col >= KB_COLS) { col = 0; row++; } } else { if (col >= KB_ACT_COLS) { col = 0; row = 0; } } }
        else { col--; if (col < 0) { if (row > 0) { row--; col = (row < KB_NUM_ROWS) ? KB_COLS - 1 : KB_ACT_COLS - 1; } else { row = KB_NUM_ROWS; col = KB_ACT_COLS - 1; } } }
        if (row < 0) row = KB_NUM_ROWS; if (row > KB_NUM_ROWS) row = 0;
        _kb.cursorCol = col; _kb.cursorRow = row; _dirty = true; return;
    }
    if (s == ScreenID::EQ_BAND_EDIT) {
        ParametricEQ* eq = getEqPtr(nav.module); if (!eq) return;
        uint8_t bandIdx = nav.subContext; EQFilterParams p = eq->getBandParams(bandIdx);
        bool hasGain = EQ_FILTER_HAS_GAIN[(uint8_t)_bandEditType];
        uint8_t qItem = hasGain ? 3 : 2;
        if (_focusIdx == 1) {
            uint16_t newF = constrain(p.f0 < 1000 ? (p.f0 + dir * 10) : p.f0 < 10000 ? (p.f0 + dir * 100) : (p.f0 + dir * 1000), 20, 20000);
            p.f0 = newF;
            p.type = (int16_t)_bandEditType;
            eq->setBand(bandIdx, p); _dirty = true;
        } else if (hasGain && _focusIdx == 2) {
            float curGain = DB_Q8_TO_FLOAT(p.gain);
            float newGain = constrain(curGain + dir * 0.5f, -24.0f, 24.0f);
            p.gain = FLOAT_TO_DB_Q8(newGain); p.type = (int16_t)_bandEditType; eq->setBand(bandIdx, p); _dirty = true;
        } else if (_focusIdx == qItem) {
            float curQ = Q_Q610_TO_FLOAT(p.Q);
            float newQ = constrain(curQ + dir * 0.1f, 0.1f, 10.0f);
            p.Q = FLOAT_TO_Q_Q610(newQ); p.type = (int16_t)_bandEditType; eq->setBand(bandIdx, p); _dirty = true;
        }
        return;
    }
    if (s == ScreenID::EFFECT_EQ_COMMON) {
        ParametricEQ* eq = getEqPtr(nav.module);
        if (_focusIdx == 0 && eq) {
            float cur = DB_Q8_TO_FLOAT((int16_t)(eq->getPregain()));
            float nxt = constrain(cur + dir * 1.0f, -24.0f, 24.0f);
            eq->setPregain((int16_t)FLOAT_TO_DB_Q8(nxt)); _dirty = true;
        }
        return;
    }
    if (s == ScreenID::DRC_CONFIG && _pipeline && _editMode) {
        DRC& drc = _pipeline->getDrc();
        if (_focusIdx == 0) {
            int8_t m = (int8_t)drc._mode + dir; if (m < 0) m = 4; if (m > 4) m = 0;
            drc.setMode((DRCMode)m); _dirty = true; return;
        } else if (_focusIdx == 1) {
            int8_t t = (int8_t)drc._cfType + dir; if (t < 2) t = 4; if (t > 4) t = 2;
            drc.setCrossoverType((DRCCrossoverType)t); _dirty = true; return;
        }
    }
    if (s == ScreenID::EFFECT_COMMON || s == ScreenID::DRC_BAND_PARAMS) {
        if (s == ScreenID::EFFECT_COMMON) {
            uint8_t paramCount = getItemCount() - 1; if (_focusIdx >= paramCount) return;
            UiParam params[16]; uint8_t n = getScreenParams(nav, params); if (_focusIdx >= n) return;
            bool skipParam = false;
            if (nav.module == DisplayModuleID::COMPANDER && _pipeline) { float la = getParamValue(nav, 5); if (la > 0 && (_focusIdx == 3 || _focusIdx == 4)) skipParam = true; }
            else if (nav.module == DisplayModuleID::DYNAMIC_BASS && _pipeline) { float la = getParamValue(nav, 8); if (la > 0 && (_focusIdx == 6 || _focusIdx == 7)) skipParam = true; }
            else if (nav.module == DisplayModuleID::DYNAMIC_EQ_THRESH && _pipeline) { float la = getParamValue(nav, 5); if (la > 0 && (_focusIdx == 3 || _focusIdx == 4)) skipParam = true; }
            if (skipParam) return;
            float cur = getParamValue(nav, _focusIdx);
            float nxt = constrain(cur + dir * params[_focusIdx].step, params[_focusIdx].minVal, params[_focusIdx].maxVal);
            setParamValue(nav, _focusIdx, nxt, params); _dirty = true;
        }
        if (s == ScreenID::DRC_BAND_PARAMS && _pipeline) {
            if (_focusIdx >= 6) return;
            uint8_t pi = _focusIdx; DRC& drc = _pipeline->getDrc(); uint8_t band = _drcActiveBand;
            const UiParam drcParams[] = { { "Pregain","dB",-24,24,1,0 },{ "Threshold","dB",-60,0,0.5f,1 },{ "Ratio",":1",1,100,1,0 },{ "Attack","ms",1,2000,10,0 },{ "Release","ms",1,2000,10,0 },{ "Lookahead","ms",0,10,1,0 } };
            float cur = 0;
            if (pi == 0) { float pregainLinear = PREGAIN_Q412_TO_FLOAT(drc._bands[band].pregainQ412); cur = (pregainLinear > 0.0001f) ? 20*log10f(pregainLinear) : -96; }
            else if (pi == 1) cur = DRC_TH_TO_FLOAT_DB(drc._bands[band].thresholdDbInt);
            else if (pi == 2) cur = (float)drc._bands[band].ratioX100/100;
            else if (pi == 3) cur = (float)drc._bands[band].attackMs;
            else if (pi == 4) cur = (float)drc._bands[band].releaseMs;
            else if (pi == 5) cur = drc._bands[band].lookaheadMs;
            float nxt = constrain(cur + dir*drcParams[pi].step, drcParams[pi].minVal, drcParams[pi].maxVal);
            if (pi == 0) { nxt = roundf(nxt); float linearGain = powf(10,nxt/20); drc.setPregain(band,FLOAT_TO_PREGAIN_Q412(linearGain)); }
            else if (pi == 1) drc.setThreshold(band,FLOAT_DB_TO_DRC_TH(nxt));
            else if (pi == 2) drc.setRatio(band,(int32_t)(nxt*100));
            else if (pi == 3) drc.setAttackTime(band,(int32_t)nxt);
            else if (pi == 4) drc.setReleaseTime(band,(int32_t)nxt);
            else if (pi == 5) drc.setLookahead(band,nxt);
            _dirty = true;
        }
        return;
    }
    if (s == ScreenID::SETTINGS) {
        // Item 1: Brightness (0-255)
        if (_focusIdx == 1 && _settingsMgr) {
            uint8_t brightness = _settingsMgr->getBrightness();
            int16_t newBrightness = (int16_t)brightness + dir * 5;
            newBrightness = constrain(newBrightness, 20, 255);

            _settingsMgr->setBrightness((uint8_t)newBrightness);
            #if defined(TFT_BACKLIGHT_PIN) && TFT_BACKLIGHT_PIN >= 0
                ledcWrite(TFT_BACKLIGHT_PIN, (uint8_t)newBrightness);
            #endif
            _dirty = true; return;
        }
        // Battery items start at index 2 (if present)
        else if (_battery && _battery->isPresent()) {
            BatteryConfig& cfg = _battery->editConfig();
            if (_focusIdx == 2) {
                // Cell count 1S–6S
                int8_t cs = (int8_t)cfg.cellS + dir;
                if (cs < 1) cs = 6;
                if (cs > 6) cs = 1;
                cfg.cellS = (uint8_t)cs;
                _battery->applyConfig();
            } else if (_focusIdx == 4) {
                // Cell mAh: step 50 mAh, range 100–20000
                int32_t mah = (int32_t)cfg.cellMah + dir * 50;
                mah = constrain(mah, 100, 20000);
                cfg.cellMah = (uint32_t)mah;
                _battery->applyConfig();
            } else if (_focusIdx == 5) {
                // Shunt mΩ: step 1 mΩ, range 1–1000
                int32_t shunt = (int32_t)cfg.shuntMOhm + dir;
                shunt = constrain(shunt, 1, 1000);
                cfg.shuntMOhm = (uint32_t)shunt;
                _battery->applyConfig();
            }
        }
        _dirty = true; return;
    }
    if (s == ScreenID::SETTINGS && _editMode) { _editMode = false; _dirty = true; return; }
    (void)dir;
}

uint8_t Display::getItemCount() const {
    switch (currentScreen()) {
    case ScreenID::SPLASH:    return 0;
    case ScreenID::MAIN_MENU: return 5;   // 0=tab bar, 1-4=items in active tab
    case ScreenID::SETTINGS: {
        // 0=WiFi, 1=Brightness,
        // then battery items (if present): 2=cellS, 3=cellMah, 4=shuntMOhm, 5=resetCoulomb
        // Last = BACK.
        uint8_t n = 2; // WiFi + Brightness
        if (_battery && _battery->isPresent()) n += 4; // cellS + mAh + shunt + reset
        return n + 1; // +1 for BACK
    }
    case ScreenID::DSP_LIST:  return MAX_PRESET_SLOTS + 1 + MODULE_COUNT + 1;
    case ScreenID::EFFECT_COMMON: { 
        NavEntry nav = _navStack[_navTop];
        uint8_t n = const_cast<Display*>(this)->getScreenParams(nav, nullptr); 
        return (n > 0) ? n + 1 : 8; }
    case ScreenID::EFFECT_EQ_COMMON: return 1 + MAX_EQ_BANDS + 2;
    case ScreenID::EFFECT_EQ_GRAPH: return MAX_EQ_BANDS + 1;
    case ScreenID::EQ_BAND_EDIT: return EQ_FILTER_HAS_GAIN[(uint8_t)_bandEditType] ? 5 : 4;
    case ScreenID::EFFECT_ISF_COMMON: { 
        uint8_t nPresets = 0; 
        if (_pipeline) { 
            auto& isf = (_navStack[_navTop].module == DisplayModuleID::ISF1) ? _pipeline->getIsf1() : _pipeline->getIsf2(); 
            nPresets = isf.getNumPresets(); 
        } 
        return 3 + 2 + nPresets + 1; }
    case ScreenID::DRC_CONFIG: return 2 + 4 + 2;
    case ScreenID::DRC_BAND_SELECT: { 
        if (!_pipeline) return 1; 
        DRC& drc = _pipeline->getDrc(); 
        uint8_t bc = 1; 
        switch (drc._mode) { 
            case DRC_MODE_FULLBAND: bc=1; break; 
            case DRC_MODE_2BAND: bc=2; break; 
            case DRC_MODE_2BAND_FULLBAND: bc=3; break; 
            case DRC_MODE_3BAND: bc=3; break; 
            case DRC_MODE_3BAND_FULLBAND: bc=4; break; 
        } 
        return bc+1; 
    }
    case ScreenID::DRC_BAND_PARAMS: return 6 + 1;
    case ScreenID::KEYBOARD: return KB_NUM_ROWS * KB_COLS + KB_ACT_COLS;
    default: return 0;
    }
}

void Display::draw() {
    TFT_eSprite& d = _spr; if (!_sprReady) return;
    d.fillSprite(Color::BG);
    switch (currentScreen()) {
    case ScreenID::SPLASH: drawSplash(); break;
    case ScreenID::MAIN_MENU: drawMainMenu(); break;
    case ScreenID::SETTINGS: drawSettings(); break;
    case ScreenID::DSP_LIST: drawDspList(); break;
    case ScreenID::EFFECT_COMMON: drawEffectCommon(); break;
    case ScreenID::EFFECT_EQ_COMMON: drawEffectEqCommon(); break;
    case ScreenID::EFFECT_EQ_GRAPH: drawEffectEqGraph(); break;
    case ScreenID::EQ_BAND_EDIT: drawEqBandEdit(); break;
    case ScreenID::EFFECT_ISF_COMMON: drawEffectIsfCommon(); break;
    case ScreenID::DRC_CONFIG: drawDrcConfig(); break;
    case ScreenID::DRC_BAND_SELECT: drawDrcBandSelect(); break;
    case ScreenID::DRC_BAND_PARAMS: drawDrcBandParams(); break;
    case ScreenID::KEYBOARD: drawKeyboard(); break;
    default: break;
    }
    d.pushSprite(0, 0);
}

void Display::drawSplash() {
    TFT_eSprite& d = _spr;

    // ── Gradient chéo: trái-trên → phải-dưới ──────────────────────────────────
    // t = (x + y) chuẩn hóa về 0..255, thay cho chỉ dùng y như cũ
    constexpr int16_t maxSum = (DISP_W - 1) + (DISP_H - 1);
    for (int16_t y = 0; y < DISP_H; y++) {
        for (int16_t x = 0; x < DISP_W; x++) {
            uint8_t t = (uint8_t)(((int32_t)(x + y) * 255) / maxSum);
            d.drawPixel(x, y, blendColor(Color::BG, Color::ACCENT2, t));
        }
    }

    // ── Text vẽ 1 lần (không lặp theo y nữa), nền trong suốt để gradient
    //    hiện ra phía sau chữ thay vì 1 màu nền cố định ──────────────────────
    d.setTextDatum(MC_DATUM);
    d.setFreeFont(&Century751BT12);
    d.setTextSize(1);
    d.setTextColor(Color::ACCENT);                       // 1-arg = transparent bg
    d.drawString("ESP32 DSP CORE", DISP_W / 2, DISP_H / 2 - 30);
    d.setTextSize(2);
    d.setTextFont(1);
    d.setTextColor(Color::TEXT);                          // transparent bg
    d.drawString("v" FIRMWARE_VERSION, DISP_W / 2, DISP_H / 2 + 6);
    d.drawString("by Nagumo", DISP_W / 2, DISP_H / 2 + 24);
}

// ─────────────────────────────────────────────────────────────────────────────
//  drawBatteryWidget — top-right corner battery icon + SoC %
//  Called from any screen that wants to show battery status.
//
//  x, y = top-right corner of the widget bounding box (icon right-aligns here)
// ─────────────────────────────────────────────────────────────────────────────
void Display::drawBatteryWidget(int16_t x, int16_t y) {
    if (!_battery || !_battery->isPresent()) return;

    TFT_eSprite& d = _spr;
    const BatteryStatus& bat = _battery->getStatus();

    // ── Color by state ────────────────────────────────────────────────────────
    uint16_t col;
    switch (bat.state) {
    case BatteryState::CRITICAL: col = Color::RED;    break;
    case BatteryState::WARNING:  col = Color::YELLOW; break;
    case BatteryState::CHARGING: col = Color::ACCENT; break;
    default:                     col = Color::GREEN;  break;
    }

    // ── Battery icon: outer rect + nub + fill ────────────────────────────────
    constexpr int16_t ICON_W  = 22;
    constexpr int16_t ICON_H  = 11;
    constexpr int16_t NUB_W   = 3;
    constexpr int16_t NUB_H   = 5;
    int16_t ix = x - ICON_W - NUB_W;
    int16_t iy = y;

    d.drawRect(ix, iy, ICON_W, ICON_H, col);
    d.fillRect(ix + ICON_W, iy + (ICON_H - NUB_H) / 2, NUB_W, NUB_H, col);

    // Fill proportion
    int16_t fillW = (int16_t)(bat.soc * (ICON_W - 4));
    if (fillW > 0)
        d.fillRect(ix + 2, iy + 2, fillW, ICON_H - 4, col);

    // ── SoC % text right of icon (actually left, after icon) ─────────────────
    char pctBuf[6];
    snprintf(pctBuf, sizeof(pctBuf), "%u%%", (unsigned)(bat.soc * 100));
    d.setTextDatum(MR_DATUM);
    d.setTextColor(col, Color::BG);
    d.setTextSize(1);
    d.drawString(pctBuf, ix - 2, iy + ICON_H / 2);
}

// ─────────────────────────────────────────────────────────────────────────────
//  drawPowerOffScreen — full-screen low-battery warning before shutdown
// ─────────────────────────────────────────────────────────────────────────────
void Display::drawPowerOffScreen() {
    TFT_eSprite& d = _spr;

    // Dark White gradient background
    for (int16_t y = 0; y < DISP_H; y++)
        d.drawFastHLine(0, y, DISP_W, blendColor(Color::BG, Color::ACCENT,
                                                   (uint8_t)(y * 60 / DISP_H)));

    d.setTextDatum(MC_DATUM);
    d.setTextSize(2);
    d.setTextColor(_powerOffUserRequest ? Color::ACCENT : Color::RED, Color::BG);
    d.drawString(_powerOffUserRequest ? "User Interrupt" : "LOW BATTERY", DISP_W / 2, DISP_H / 2 - 28);

    d.setTextSize(1);
    d.setTextColor(Color::TEXT, Color::BG);

    if (_battery) {
        char vBuf[16];
        snprintf(vBuf, sizeof(vBuf), "%.2f V", _battery->getStatus().busVoltage);
        d.drawString(vBuf, DISP_W / 2, DISP_H / 2);
    }

    d.drawString("Shutting down...", DISP_W / 2, DISP_H / 2 + 18);
}

// ─────────────────────────────────────────────────────────────────────────────
//  drawMainMenu — two-tab layout
//
//  Tab 0 "PARAMS" — Vol/Bass/Mid/Treble big value display (encoder adjusts)
//  Tab 1 "CTRL"   — BT kick, +, pause/mute, − buttons (SW activates focused btn)
//
//  SW press when on tab row  → switches tab
//  SW press when inside tab  → activates focused item (CTRL tab), or cycles
//                              param selector (PARAMS tab)
//  CW/CCW always adjusts the active param regardless of which tab
//
//  focusIdx layout:
//    0   = Tab bar (PARAMS / CTRL selector)
//    PARAMS tab (mainMenuTab == 0):
//      1 = Vol selector
//      2 = Bass selector
//      3 = Mid selector
//      4 = Treble selector
//    CTRL tab (mainMenuTab == 1):
//      1 = BT Kick
//      2 = + (increment active param)
//      3 = pause / mute
//      4 = − (decrement active param)

// ─────────────────────────────────────────────────────────────────────────────
// drawMainMenu — new layout (sketch 2025)
//
//  320×240, sidebar (C/H bars + status) on the left, content area on right.
//
//  Content area layout (x = CONTENT_X = 32, w = CONTENT_W ≈ 286):
//
//  [0..19]   Header: "ESP32 DSP CORE"  +  sample-rate + wifi badge (right)
//  [20]      separator line
//  [21..135] Upper panel — split into two columns:
//              LEFT  col  (x=32..144)  : Bluetooth kick btn + +/pause/- buttons
//              RIGHT col  (x=145..317) : Big param value (Vol/Bass/Mid/Treble)
//                                        + param label below
//  [136..143] small gap
//  [144..232] Live meter box
//  [233..239] bottom status text
//
//  SW press  → cycles active param: Vol → Bass → Mid → Treble → Vol
//  CW / CCW  → increments/decrements the active param (same as before)
//  Bluetooth kick button (focusIdx == 4): placeholder, caller assigns callback
// ─────────────────────────────────────────────────────────────────────────────
void Display::drawMainMenu() {
    TFT_eSprite& d = _spr;
    if (_powerOffScreenActive) {
        drawPowerOffScreen();
        return;
    }

    // ── Sidebar (CPU / Heap bars + status) ────────────────────────────────────
    drawSideBars(SIDEBAR_X, HEADER_H + 2, DISP_H - HEADER_H - 48);
 
    // Sample-rate below bars
    d.setTextDatum(ML_DATUM);
    d.setTextColor(Color::TEXT_DIM, Color::BG);
    d.setTextSize(1);
    char srBuf[12];
    snprintf(srBuf, sizeof(srBuf), _sampleRate > 0 ? "%lukHz" : "--",
             (unsigned long)(_sampleRate / 1000));
    d.drawString(srBuf, SIDEBAR_X, DISP_H - 32);
 
    // Connection badge
    drawConnectionBadge(SIDEBAR_X, DISP_H - 26);
 
    // ── Header ───────────────────────────────────────────────────────────
    d.setTextDatum(ML_DATUM);
    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextSize(1);
    d.drawString("ESP32 DSP CORE", MAIN_MENU_CONTENT_X, 10);

    // Voltage + current draw — top-middle of header
    if (_battery && _battery->isPresent()) {
        const BatteryStatus& bat = _battery->getStatus();
        char vaBuf[24];
        snprintf(vaBuf, sizeof(vaBuf), "%.2fV  %.2fA", bat.busVoltage, bat.currentMa / 1000);
        uint16_t vaCol = (bat.state == BatteryState::CRITICAL) ? Color::RED
                        : (bat.state == BatteryState::WARNING)  ? Color::YELLOW
                                                                  : Color::TEXT_DIM;
        d.setTextDatum(MC_DATUM);
        d.setTextColor(vaCol, Color::BG);
        d.setTextSize(1);
        d.drawString(vaBuf, MAIN_MENU_CONTENT_X + MAIN_MENU_CONTENT_W / 2, 10);
    }

    // Battery widget — top right corner (icon + SoC %)
    drawBatteryWidget(DISP_W - 2, 4);

    d.drawFastHLine(MAIN_MENU_CONTENT_X, HEADER_H, MAIN_MENU_CONTENT_W, Color::BORDER);
 
    // ── Tab bar ───────────────────────────────────────────────────────────────
    constexpr int16_t TAB_Y   = HEADER_H + 3;
    constexpr int16_t TAB_H   = 18;
    constexpr int16_t TAB_W   = (MAIN_MENU_CONTENT_W - 4) / 2;
    constexpr int16_t TAB_GAP = 4;
    const char* tabLabels[]   = { "PARAMS", "CTRL" };

    for (uint8_t t = 0; t < 2; t++) {
        int16_t tx      = MAIN_MENU_CONTENT_X + t * (TAB_W + TAB_GAP);
        bool    isActive = (_mainMenuTab == t);
        bool    focused  = (_focusIdx == 0); // focus on tab bar

        uint16_t tabBg, tabBorder, tabText;
        if (isActive) {
            tabBg     = Color::ACCENT;
            tabBorder = Color::ACCENT;
            tabText   = Color::BG;
        } else if (focused) {
            // Animate non-active tab when tab bar is focused
            uint8_t bl = (uint8_t)(_anim.focusTransition * 200);
            tabBg     = blendColor(Color::BG, Color::PANEL, bl);
            tabBorder = blendColor(Color::BORDER, Color::ACCENT, bl);
            tabText   = blendColor(Color::TEXT_DIM, Color::TEXT, bl);
        } else {
            tabBg     = Color::BG;
            tabBorder = Color::BORDER;
            tabText   = Color::TEXT_DIM;
        }

        d.fillRoundRect(tx, TAB_Y, TAB_W, TAB_H, 3, tabBg);
        d.drawRoundRect(tx, TAB_Y, TAB_W, TAB_H, 3, tabBorder);
        d.setTextColor(tabText, tabBg);
        d.setTextDatum(MC_DATUM);
        d.setTextSize(1);
        d.drawString(tabLabels[t], tx + TAB_W / 2, TAB_Y + TAB_H / 2);
    }

    // ── Hold-progress indicator: fill a thin bar at the bottom of the tab bar ──
    // Visible while the user is holding SW inside a tab (tab-return hold).
    if (_globalHoldArmed) {
        uint32_t elapsed = millis() - _globalHoldStartMs;
        float frac = constrain((float)elapsed / 5000.0f, 0.0f, 1.0f);
        uint16_t color = (elapsed < 3000) ? Color::GREEN : Color::RED;
        
        int16_t barY = TAB_Y + TAB_H - 3;
        int16_t barW = (int16_t)(frac * MAIN_MENU_CONTENT_W);
        if (barW > 0) d.fillRect(MAIN_MENU_CONTENT_X, barY, barW, 2, color);
    }

    // ── Content area (below tab bar) ──────────────────────────────────────────
    constexpr int16_t CONTENT_Y = TAB_Y + TAB_H + 4;
    constexpr int16_t CONTENT_H = DISP_H - CONTENT_Y - 4;

    if (_mainMenuTab == 0) {
        // ════════════════════════════════════════════════════════════════════
        //  PARAMS tab — Vol/Bass/Mid/Treble selector + big value
        // ════════════════════════════════════════════════════════════════════
    const char* paramLabels[] = { "Vol", "Bass", "Mid", "Treble" };
    float paramValues[] = {
        _anim.volAnimating ? _anim.volDisplay : (float)_mmparam.vol,
        ((float)_mmparam.bass   / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam,
        ((float)_mmparam.mid    / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam,
        ((float)_mmparam.treble / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam,
    };
    const char* paramUnits[]  = { "",   "dB", "dB", "dB" };
    const uint8_t paramDecs[] = { 0,     1,    1,    1   };
 
    // active param = focusIdx 1-4 mapped to 0-3
        const uint8_t activeParam = _mainMenuActiveParam;

        float displayValue = paramValues[activeParam];
        if (_anim.valueAnimating) displayValue = getAnimatedValue(displayValue);

        // Param selector pills (4 items, horizontal)
        constexpr int16_t PILL_H   = 16;
        constexpr int16_t PILL_GAP = 3;
        int16_t pillW = (MAIN_MENU_CONTENT_W - 3 * PILL_GAP) / 4;

        for (uint8_t i = 0; i < 4; i++) {
            int16_t px      = MAIN_MENU_CONTENT_X + i * (pillW + PILL_GAP);
            bool    isAct   = (i == activeParam);
            bool    focused = (_focusIdx == (uint8_t)(i + 1));

            float focusBlend = 0.0f;
            if (focused) {
                focusBlend = (_anim.focusTransition < 1.0f) ? _anim.focusTransition : 1.0f;
            } else if (_anim.prevFocusIdx == (int8_t)(i + 1) && _anim.focusTransition < 1.0f) {
                focusBlend = 1.0f - _anim.focusTransition;
            }

            uint16_t pillBg, pillText;
            if (isAct) {
                pillBg   = Color::ACCENT;
                pillText = Color::BG;
            } else if (focusBlend > 0.0f) {
                uint8_t bl = (uint8_t)(focusBlend * 255);
                pillBg   = blendColor(Color::PANEL, Color::ACCENT2, bl);
                pillText = blendColor(Color::TEXT_DIM, Color::TEXT_FOCUS, bl);
            } else {
                pillBg   = Color::PANEL;
                pillText = Color::TEXT_DIM;
            }

            d.fillRoundRect(px, CONTENT_Y, pillW, PILL_H, 3, pillBg);
            d.setTextColor(pillText, pillBg);
            d.setTextDatum(MC_DATUM);
            d.setTextSize(1);
            d.drawString(paramLabels[i], px + pillW / 2, CONTENT_Y + PILL_H / 2);
        }

        // Big value
        constexpr int16_t BIG_Y    = CONTENT_Y + PILL_H + 4;
        constexpr int16_t BIG_AREA = 54;
        char bigBuf[10];
        formatFloat(bigBuf, sizeof(bigBuf), displayValue, paramDecs[activeParam]);
        d.setTextDatum(MC_DATUM);
        d.setTextColor(_mainMenuAutosaveArmed ? Color::TEXT_FOCUS : Color::TEXT, Color::BG);
        d.setTextSize(4);
        d.drawString(bigBuf, MAIN_MENU_CONTENT_X + MAIN_MENU_CONTENT_W / 2, BIG_Y + BIG_AREA / 2);

        // Unit + param name below value
        d.setTextSize(1);
        d.setTextColor(Color::TEXT_DIM, Color::BG);
        d.drawString(paramUnits[activeParam],
                     MAIN_MENU_CONTENT_X + MAIN_MENU_CONTENT_W / 2, BIG_Y + BIG_AREA + 2);

        // ── Live meter panel ──────────────────────────────────────────────────
        constexpr int16_t METER_Y = CONTENT_Y + PILL_H + BIG_AREA + 28;
        int16_t meterH = DISP_H - METER_Y - 4;

        if (meterH > 35 && _pipeline) {
            d.fillRoundRect(MAIN_MENU_CONTENT_X, METER_Y, MAIN_MENU_CONTENT_W, meterH, 4, Color::PANEL);
            d.drawRoundRect(MAIN_MENU_CONTENT_X, METER_Y, MAIN_MENU_CONTENT_W, meterH, 4, Color::BORDER);
            d.setTextColor(Color::TEXT_DIM, Color::PANEL);
            d.setTextDatum(TC_DATUM);
            d.setTextSize(1);
            d.drawString("LIVE", MAIN_MENU_CONTENT_X + MAIN_MENU_CONTENT_W / 2, METER_Y + 2);

            constexpr int16_t LBL_W  = 44;
            constexpr int16_t ROW_HM = 12;
            int16_t mW      = MAIN_MENU_CONTENT_W - LBL_W - 12;
            int16_t lblX    = MAIN_MENU_CONTENT_X + 4;
            int16_t mX      = MAIN_MENU_CONTENT_X + LBL_W + 4;
            int16_t rowY    = METER_Y + 12;

            auto hBar = [&](const char* lbl, float frac, uint16_t col) {
                d.setTextColor(Color::TEXT, Color::PANEL);
                d.setTextDatum(ML_DATUM);
                d.setTextSize(1);
                d.drawString(lbl, lblX, rowY + ROW_HM / 2);
                constexpr int16_t bH = 6;
                int16_t bY = rowY + (ROW_HM - bH) / 2;
                d.drawRect(mX, bY, mW, bH, Color::BORDER);
                int16_t fw = (int16_t)(constrain(frac, 0.0f, 1.0f) * (mW - 2));
                if (fw > 0) d.fillRect(mX + 1, bY + 1, fw, bH - 2, col);
                rowY += ROW_HM;
            };

            Compander& comp = _pipeline->getCompander();
            if (comp.isEnabled()) {
                float gr = constrain(-_companderGainDb, 0.0f, 30.0f);
                hBar("Comp", gr / 30.0f,
                     gr > 20 ? Color::RED : gr > 12 ? Color::YELLOW : Color::GREEN);
            }
            DynamicBass& db = _pipeline->getDynamicBass();
            if (db.isEnabled()) {
                float a = constrain(_dynBassAlpha, 0.0f, 1.0f);
                hBar("DBass", a, Color::GREEN);
            }
            DynamicEQ& deq = _pipeline->getDynamicEq();
            if (deq.isEnabled()) {
                float a = constrain((_dynEqAlphaLow + _dynEqAlphaHigh) * 0.5f, 0.0f, 1.0f);
                hBar("DEQ", a, Color::ACCENT);
            }
        }

    } else {
        // ════════════════════════════════════════════════════════════════════
        //  CTRL tab — BT kick, +, pause/mute, −
        //  focusIdx 1-4 map to these buttons
        // ════════════════════════════════════════════════════════════════════
        struct CtrlBtn {
            const char* label;
            const char* sublabel;
        };
        const CtrlBtn btns[] = {
            { "BT",   "Kick"    },
            { "+",    "Vol +"   },
            { "||",   "Play/Pause"    },
            { "-",    "Vol -"   },
        };

        constexpr int16_t BTN_W   = 56;
        constexpr int16_t BTN_H   = 56;
        constexpr int16_t BTN_GAP = 8;
        // 4 buttons in a row, centred
        int16_t totalW = 4 * BTN_W + 3 * BTN_GAP;
        int16_t startX = MAIN_MENU_CONTENT_X + (MAIN_MENU_CONTENT_W - totalW) / 2;
        int16_t btnY   = CONTENT_Y + (CONTENT_H - BTN_H - 14) / 2;

        for (uint8_t i = 0; i < 4; i++) {
            int16_t bx     = startX + i * (BTN_W + BTN_GAP);
            bool    focused = (_focusIdx == (int)i + 1);

            uint16_t bg, border, textCol;
            if (focused) {
                if (_anim.focusTransition < 1.0f) {
                    uint8_t bl = (uint8_t)(_anim.focusTransition * 255);
                    bg      = blendColor(Color::PANEL, Color::ACCENT, bl);
                    border  = blendColor(Color::BORDER, Color::TEXT,  bl);
                    textCol = blendColor(Color::TEXT,   Color::BG,    bl);
                } else {
                    bg = Color::ACCENT; border = Color::TEXT; textCol = Color::BG;
                }
            } else {
                bg = Color::PANEL; border = Color::BORDER; textCol = Color::TEXT;
            }

            d.fillRoundRect(bx, btnY, BTN_W, BTN_H, 6, bg);
            d.drawRoundRect(bx, btnY, BTN_W, BTN_H, 6, border);

            d.setTextColor(textCol, bg);
            d.setTextDatum(MC_DATUM);
            d.setTextSize(i == 0 ? 2 : 3);  // BT smaller, +/||/- bigger
            d.drawString(btns[i].label, bx + BTN_W / 2, btnY + BTN_H / 2 - 2);

            // Sub-label below button
            d.setTextSize(1);
            d.setTextColor(focused ? Color::TEXT : Color::TEXT_DIM, Color::BG);
            d.drawString(btns[i].sublabel, bx + BTN_W / 2, btnY + BTN_H + 7);
        }

        // Hint text at bottom
        d.setTextDatum(MC_DATUM);
        d.setTextColor(Color::TEXT_DIM, Color::BG);
        d.setTextSize(1);
        d.drawString("CW/CCW = navigate  |  SW = activate",
                     MAIN_MENU_CONTENT_X + MAIN_MENU_CONTENT_W / 2, DISP_H - 8);
    }
}

void Display::drawSettings() {
    TFT_eSprite& d = _spr;

    d.setTextDatum(ML_DATUM);
    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextSize(1);
    d.drawString("SETTINGS", CONTENT_X, 10);
    d.drawFastHLine(CONTENT_X, HEADER_H, CONTENT_W, Color::BORDER);

     // ── Settings item list ────────────────────────────────────────────────────
    // focusIdx:
    //   0  = WiFi switch
    //   1  = Trigger switch
    //   2  = Cell count   (1S–6S)    — battery
    //   3  = Cell mAh                — battery
    //   4  = Shunt mΩ               — battery
    //   5  = Reset coulomb counter   — battery nav button
    //   6  = BACK

    int16_t y = HEADER_H + 4;

    // WiFi switch
    #ifndef ONLY_SERIAL
    drawSwitchRow(CONTENT_X, y, CONTENT_W, "WiFi", _settingsWifi, _focusIdx == 0);
    y += ROW_H;
    #else
    d.setTextColor(Color::TEXT_DIM, Color::BG);
    d.setTextDatum(ML_DATUM);
    d.setTextSize(1);
    d.drawString("WiFi Not Available (Force By ONLY_SERIAL)", CONTENT_X , y + 12);
    y += ROW_H;
    #endif

    // Brightness slider
    {
        bool focused = (_focusIdx == 1);
        uint8_t brightness = _settingsMgr ? _settingsMgr->getBrightness() : 255;
        drawSliderRow(CONTENT_X, y, CONTENT_W, "Brightness", (float)brightness, 20, 255, "", focused, focused && _editMode, 0);
    }
    y += ROW_H + 4;

    // ── Battery section header ────────────────────────────────────────────────
    d.drawFastHLine(CONTENT_X, y, CONTENT_W, Color::BORDER);
    d.setTextDatum(ML_DATUM);
    d.setTextColor(Color::TEXT_DIM, Color::BG);
    d.setTextSize(1);
    d.drawString("BATTERY", CONTENT_X + 2, y + 8);
    y += 15;

    if (_battery && _battery->isPresent()) {
        const BatteryConfig& cfg = _battery->getConfig();

        // Cell count selector (1S–6S)
        {
            bool focused = (_focusIdx == 2);
            uint16_t bg, border, textCol;
            getFocusColors(focused, bg, border, textCol);
            d.fillRect(CONTENT_X, y, CONTENT_W, ROW_H - 2, bg);
            d.drawRect(CONTENT_X, y, CONTENT_W, ROW_H - 2, border);
            d.setTextDatum(ML_DATUM);
            d.setTextColor(textCol, bg);
            d.setTextSize(1);
            d.drawString("Cells (S)", CONTENT_X + 6, y + (ROW_H - 2) / 2);

            // Value box with arrows
            char cellBuf[10];
            snprintf(cellBuf, sizeof(cellBuf), "< %dS >", cfg.cellS);
            d.setTextDatum(MR_DATUM);
            d.drawString(cellBuf, CONTENT_X + CONTENT_W - 6, y + (ROW_H - 2) / 2);

            // Voltage hint
            if (!focused) {
                char vBuf[16];
                snprintf(vBuf, sizeof(vBuf), "%.1f-%.1fV",
                         LIION_CELL_MIN_V * cfg.cellS,
                         LIION_CELL_MAX_V * cfg.cellS);
                d.setTextColor(Color::TEXT_DIM, bg);
                d.setTextDatum(MC_DATUM);
                d.drawString(vBuf, CONTENT_X + CONTENT_W / 2, y + (ROW_H - 2) / 2);
            }
        }
        y += ROW_H;

        // Cell mAh
        {
            bool focused = (_focusIdx == 3);
            uint16_t bg, border, textCol;
            getFocusColors(focused, bg, border, textCol);
            d.fillRect(CONTENT_X, y, CONTENT_W, ROW_H - 2, bg);
            d.drawRect(CONTENT_X, y, CONTENT_W, ROW_H - 2, border);
            d.setTextDatum(ML_DATUM);
            d.setTextColor(textCol, bg);
            d.setTextSize(1);
            d.drawString("Cell mAh", CONTENT_X + 6, y + (ROW_H - 2) / 2);
            char mBuf[12];
            snprintf(mBuf, sizeof(mBuf), "%lumAh", (unsigned long)cfg.cellMah);
            d.setTextDatum(MR_DATUM);
            d.drawString(mBuf, CONTENT_X + CONTENT_W - 6, y + (ROW_H - 2) / 2);
        }
        y += ROW_H;

        // Shunt mΩ
        {
            bool focused = (_focusIdx == 4);
            uint16_t bg, border, textCol;
            getFocusColors(focused, bg, border, textCol);
            d.fillRect(CONTENT_X, y, CONTENT_W, ROW_H - 2, bg);
            d.drawRect(CONTENT_X, y, CONTENT_W, ROW_H - 2, border);
            d.setTextDatum(ML_DATUM);
            d.setTextColor(textCol, bg);
            d.setTextSize(1);
            d.drawString("Shunt", CONTENT_X + 6, y + (ROW_H - 2) / 2);
            char sBuf[12];
            snprintf(sBuf, sizeof(sBuf), "%lumΩ", (unsigned long)cfg.shuntMOhm);
            d.setTextDatum(MR_DATUM);
            d.drawString(sBuf, CONTENT_X + CONTENT_W - 6, y + (ROW_H - 2) / 2);
        }
        y += ROW_H;

        // Reset coulomb counter button
        {
            float holdFrac = (_holdTracking && _focusIdx == 5)
                             ? (float)(millis() - _focusHoldStartMs) / 2000.0f
                             : 0.0f;
            drawNavButton(CONTENT_X, y, CONTENT_W, ROW_H - 2,
                          "Reset Coulomb Counter (hold)",
                          _focusIdx == 5,
                          _holdTracking && _focusIdx == 5, holdFrac);
        }
        y += ROW_H;
    } else {
        // INA226 not found
        d.setTextColor(Color::TEXT_DIM, Color::BG);
        d.setTextDatum(ML_DATUM);
        d.setTextSize(1);
        d.drawString("INA226 not found", CONTENT_X + 6, y + 10);
        y += ROW_H * 2;
    }

    // BACK button (always last)
    const uint8_t backIdx = getItemCount() - 1;
    float holdFrac = (_holdTracking && _focusIdx == backIdx)
                     ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS
                     : 0.0f;
    drawNavButton(CONTENT_X, FOOTER_Y, CONTENT_W, NAV_BTN_H, "BACK",
                  _focusIdx == backIdx,
                  _holdTracking && _focusIdx == backIdx, holdFrac);
}

void Display::drawDspList() {
    TFT_eSprite& d = _spr;

    const uint8_t itemCount = getItemCount();
    const uint8_t backIdx = itemCount - 1;

    d.setTextDatum(ML_DATUM);
    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextSize(1);
    d.drawString("DSP / PRESETS", CONTENT_X, 10);
    d.drawFastHLine(CONTENT_X, HEADER_H, CONTENT_W, Color::BORDER);

    int16_t yPos = HEADER_H + 4;

    // Preset slots
    constexpr int16_t presetBoxW = (CONTENT_W - 12) / MAX_PRESET_SLOTS;
    constexpr int16_t presetBoxH = 32;

    for (uint8_t i = 0; i < MAX_PRESET_SLOTS; i++) {
        int16_t x = CONTENT_X + i * (presetBoxW + 4);
        bool focused = (_focusIdx == i);
        bool isTarget = (i == _presetTargetSlot);

        uint16_t bg, border;

        // Check if this slot is being animated
        bool isAnimExit = (_anim.presetSweepActive && !_anim.presetSweepPhase2 && 
                           _anim.presetPrevSlot == (int8_t)i);
        bool isAnimEntry = (isTarget && _anim.presetSweepActive && _anim.presetSweepPhase2 && 
                            _anim.presetSweepSlot == (int8_t)i);

        if (isAnimExit) {
            // Exit animation: BG transitions from ACCENT → BG
            uint8_t bl = (uint8_t)(_anim.presetSweepProgress * 255);
            bg = blendColor(Color::ACCENT, Color::BG, bl);
            border = blendColor(Color::ACCENT, Color::BORDER, bl);
        } else if (isAnimEntry) {
            // Entry animation: BG transitions from BG → ACCENT
            uint8_t bl = (uint8_t)(_anim.presetSweepProgress * 255);
            bg = blendColor(Color::BG, Color::ACCENT, bl);
            border = blendColor(Color::BORDER, Color::ACCENT, bl);
        } else if (isTarget) {
            // Target preset (not animating) - use saved finished BG
            bg = _anim.presetSweepFinishedBg[i];
            border = Color::ACCENT;
        } else if (focused) {
            if (_anim.focusTransition < 1) {
                uint8_t bl = (uint8_t)(_anim.focusTransition * 255);
                bg = blendColor(Color::BG, Color::PANEL, bl);
                border = blendColor(Color::BORDER, Color::ACCENT, bl);
            } else {
                bg = Color::PANEL;
                border = Color::ACCENT;
            }
        } else {
            bg = Color::BG;
            border = Color::BORDER;
        }

        d.fillRect(x, yPos, presetBoxW, presetBoxH, bg);
        if (focused) { // Draw a focus rectangle for the focused preset
            d.drawRoundRect(x, yPos, presetBoxW, presetBoxH, 2, border); // Thicker border for focus
        } else {
            d.drawRect(x, yPos, presetBoxW, presetBoxH, Color::BORDER);
        }

        // Preset sweep overlay (only on entry, for visual flair)
        if (isAnimEntry) {
            drawSweepAnimation(x, yPos, presetBoxW, presetBoxH, false, _anim.presetSweepProgress, Color::ACCENT);
        }

        char label[8];
        snprintf(label, sizeof(label), "P%u", (unsigned)(i + 1));

        uint16_t textColor = isTarget ? Color::BG : (focused ? Color::TEXT_FOCUS : Color::TEXT);
        if (isAnimEntry) textColor = blendColor(Color::TEXT, Color::BG, (uint8_t)(_anim.presetSweepProgress * 255));
        if (isAnimExit) textColor = blendColor(Color::BG, Color::TEXT, (uint8_t)(_anim.presetSweepProgress * 255));

        d.setTextColor(textColor);
        d.setTextDatum(MC_DATUM);
        d.drawString(label, x + presetBoxW / 2, yPos + presetBoxH / 2 - 4);
    }

    yPos += presetBoxH + 6;

    // Save button
    constexpr int16_t saveW = 200, saveH = 28;
    int16_t saveX = CONTENT_X + (CONTENT_W - saveW) / 2;
    bool saveFocused = (_focusIdx == DSP_LIST_PRESET_SAVE_IDX);

    uint16_t saveBg, saveBorder;
    if (saveFocused) {
        if (_anim.focusTransition < 1) {
            uint8_t bl = (uint8_t)(_anim.focusTransition * 255);
            saveBg = blendColor(Color::BG, Color::PANEL, bl);
            saveBorder = blendColor(Color::BORDER, Color::ACCENT, bl);
        } else {
            saveBg = Color::PANEL;
            saveBorder = Color::ACCENT;
        }
    } else {
        saveBg = Color::BG;
        saveBorder = Color::BORDER;
    }

    d.fillRoundRect(saveX, yPos, saveW, saveH, 4, saveBg);
    d.drawRoundRect(saveX, yPos, saveW, saveH, 4, saveBorder);

    // Save button sweep animation
    if (_anim.saveSweepActive) {
        drawSweepAnimation(saveX, yPos, saveW, saveH, _anim.saveSweepPhase2,
                           _anim.saveSweepProgress, Color::ACCENT);
    }

    char saveLabel[24];
    snprintf(saveLabel, sizeof(saveLabel), "SAVE PRESET %u",
            (unsigned)(_presetTargetSlot + 1));

    d.setTextColor(saveFocused ? Color::TEXT_FOCUS : Color::TEXT);
    d.setTextDatum(MC_DATUM);
    d.drawXBitmap(saveX + 45, yPos - 12 + (saveH) / 2, icon_save, 24, 24, saveFocused ? Color::TEXT_FOCUS : Color::TEXT);
    d.drawString(saveLabel, saveX + 18 + saveW / 2, yPos + 2 + saveH / 2);

    yPos += saveH + 8;

    // Module list
    constexpr uint8_t visibleModuleRows = 4;
    uint8_t firstModule = 0;
    if (_focusIdx >= DSP_LIST_MODULE_START_IDX && _focusIdx < backIdx) {
        uint8_t mi = _focusIdx - DSP_LIST_MODULE_START_IDX;
        if (mi >= visibleModuleRows - 1) firstModule = mi - (visibleModuleRows - 2);
    }

    for (uint8_t i = 0; i < visibleModuleRows && (firstModule + i) < MODULE_COUNT; i++) {
        uint8_t mi = firstModule + i;
        uint8_t itemIdx = DSP_LIST_MODULE_START_IDX + mi;
        bool focused = (_focusIdx == itemIdx);

        DspModule* mod = getModulePtr(MODULE_LIST[mi].id);
        bool isOn = mod ? mod->isEnabled() : false;

        uint16_t textColor;

        // Module toggle sweep - only animate when THIS specific module is being toggled
        bool isModAnimating = (_anim.modSweepActive && _anim.modSweepIdx == (int8_t)mi);
        
        uint16_t bg, border;
        if (isModAnimating) {
            // During animation - will be handled below
            bg = Color::BG;
            border = Color::BORDER;
        } else if (focused) {
            // Focus transition animation
            if (_anim.focusTransition < 1) {
                uint8_t bl = (uint8_t)(_anim.focusTransition * 255.0f);
                if (isOn) {
                    // Enabled module - focus transition: BG → ACCENT
                    bg = blendColor(Color::BG, Color::ACCENT, bl);
                    border = blendColor(Color::BORDER, Color::ACCENT, bl);
                } else {
                    // Disabled module - focus transition: BG → PANEL
                    bg = blendColor(Color::BG, Color::PANEL, bl);
                    border = blendColor(Color::BORDER, Color::ACCENT, bl);
                }
            } else {
                bg = isOn ? Color::ACCENT : Color::PANEL;
                border = Color::ACCENT;
            }
        } else {
            bg = isOn ? Color::ACCENT : Color::BG;
            border = isOn ? Color::ACCENT : Color::BORDER;
        }
        
        textColor = (bg == Color::ACCENT) ? Color::BG : (focused ? Color::TEXT_FOCUS : Color::TEXT);

        // During animation - draw sweep and DON'T overwrite with solid fill
        if (isModAnimating) {
            // Exit=true (disable): sweep BG from left, trailing shows ACCENT
            // Exit=false (enable): sweep ACCENT from left, trailing shows BG
            drawSweepAnimation(CONTENT_X, yPos, CONTENT_W, ROW_H - 2,
                               _anim.modSweepExit, _anim.modSweepProgress, Color::ACCENT);
            // Blend text color to match sweep progress so it stays readable.
            // When enabling (exit=false): text transitions TEXT → BG (row goes BG→ACCENT)
            // When disabling (exit=true): text transitions BG → TEXT (row goes ACCENT→BG)
            uint8_t blend = (uint8_t)(_anim.modSweepProgress * 255);
            if (!_anim.modSweepExit)
                textColor = blendColor(Color::TEXT, Color::BG, blend);
            else
                textColor = blendColor(Color::BG, Color::TEXT, blend);
            // For drawCentreString we still need a bg hint; use mid-animation blend
            bg = blendColor(Color::BG, Color::ACCENT,
                            _anim.modSweepExit ? 255 - blend : blend);
        } else {
            d.fillRect(CONTENT_X, yPos, CONTENT_W, ROW_H - 2, bg);
            d.drawRect(CONTENT_X, yPos, CONTENT_W, ROW_H - 2, border);
        }

        d.setTextColor(textColor);
        d.setTextDatum(ML_DATUM);
        d.drawCentreString(MODULE_LIST[mi].name, CONTENT_X + CONTENT_W / 2, yPos + (ROW_H - 6) / 2, 1);

        yPos += ROW_H;
    }

    // Back button
    int16_t backY = DISP_H - NAV_BTN_H - 4;
    bool backFocused = (_focusIdx == backIdx);

    float holdFrac = (_holdTracking && _focusIdx == backIdx)
                         ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS
                         : 0;
    drawNavButton(CONTENT_X, backY, CONTENT_W, NAV_BTN_H, "BACK", backFocused, _holdTracking && _focusIdx == backIdx,
                  holdFrac);

    // Scrollbar
    if (MODULE_COUNT > visibleModuleRows) {
        int16_t maY = HEADER_H + 4 + presetBoxH + 6 + saveH + 8;
        int16_t trackH = backY - maY - 4;
        int16_t thumbH = trackH * visibleModuleRows / MODULE_COUNT;
        int16_t thumbY = maY + trackH * firstModule / MODULE_COUNT;

        d.fillRect(DISP_W - 4, maY, 3, trackH, Color::BORDER);
        d.fillRect(DISP_W - 4, thumbY, 3, thumbH, Color::ACCENT);
    }
}

void Display::drawEffectCommon() {
    TFT_eSprite& d = _spr;
    NavEntry& nav = currentNav();

    const char* modName = "Effect";
    for (uint8_t i = 0; i < MODULE_COUNT; i++) {
        if (MODULE_LIST[i].id == nav.module) {
            modName = MODULE_LIST[i].name;
            break;
        }
    }

    d.setTextDatum(ML_DATUM);
    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextSize(1);
    d.drawString(modName, CONTENT_X, 10);
    d.drawFastHLine(CONTENT_X, HEADER_H, CONTENT_W, Color::BORDER);

    UiParam params[16];
    uint8_t paramCount = getScreenParams(nav, params);
    constexpr uint8_t VISIBLE = 6;

    for (uint8_t i = 0; i < VISIBLE; i++) {
        uint8_t pi = (uint8_t)(_paramScroll + i);
        if (pi >= paramCount) break;

        int16_t y = HEADER_H + 4 + i * ROW_H;
        bool focused = (_focusIdx == pi);
        bool editing = focused && _editMode;
        float val = getParamValue(nav, pi);

        bool isSwitch = (params[pi].minVal == 0 && params[pi].maxVal == 1 &&
                         params[pi].step == 1 && params[pi].unit[0] == '\0');

        if (isSwitch) {
            drawSwitchRow(CONTENT_X, y, CONTENT_W, params[pi].name,
                           val >= 0.5f, focused);
        } else {
            if (nav.module == DisplayModuleID::COMPANDER && (pi == 1 || pi == 2)) { // Ratio
                drawSliderRow(CONTENT_X, y, CONTENT_W, params[pi].name, val / 100.0f, params[pi].minVal / 100.0f, params[pi].maxVal / 100.0f, params[pi].unit,
                              focused, editing, params[pi].decimals);
            } else {
                drawSliderRow(CONTENT_X, y, CONTENT_W, params[pi].name, val, params[pi].minVal, params[pi].maxVal, params[pi].unit,
                              focused, editing, params[pi].decimals);
            }
        }
    }

    uint8_t backIdx = getItemCount() - 1;
    float holdFrac = (_holdTracking && _focusIdx == backIdx)
                         ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS
                         : 0;
    drawNavButton(CONTENT_X, FOOTER_Y, CONTENT_W, NAV_BTN_H, "BACK",
                  _focusIdx == backIdx, _holdTracking && _focusIdx == backIdx,
                  holdFrac);
}

void Display::drawEffectEqCommon() {
    TFT_eSprite& d = _spr;
    NavEntry& nav = currentNav();

    const char* modName = "EQ";
    for (uint8_t i = 0; i < MODULE_COUNT; i++) {
        if (MODULE_LIST[i].id == nav.module) {
            modName = MODULE_LIST[i].name;
            break;
        }
    }

    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextDatum(ML_DATUM);
    d.setTextSize(1);
    d.drawString(modName, CONTENT_X, 10);
    d.drawFastHLine(CONTENT_X, HEADER_H, CONTENT_W, Color::BORDER);

    float pregainDb = 0;
    ParametricEQ* eq = getEqPtr(nav.module);
    if (eq) pregainDb = DB_Q8_TO_FLOAT((int16_t)eq->getPregain());

    bool pregFocused = (_focusIdx == 0);
    drawSliderRow(CONTENT_X, HEADER_H + 4, CONTENT_W, "Pregain", pregainDb, -24,
                  24, "dB", pregFocused, pregFocused && _editMode);

    constexpr uint8_t VISIBLE = 4;

    for (uint8_t i = 0; i < VISIBLE; i++) {
        uint8_t bi = (uint8_t)(_paramScroll + i);
        if (bi >= MAX_EQ_BANDS) break;

        int16_t y = HEADER_H + 4 + (i + 1) * ROW_H;
        bool focused = (_focusIdx == bi + 1);
        uint16_t rowBg = focused ? Color::PANEL : Color::BG;

        d.fillRect(CONTENT_X, y, CONTENT_W, ROW_H - 2, rowBg);
        d.drawRect(CONTENT_X, y, CONTENT_W, ROW_H - 2,
                   focused ? Color::ACCENT : Color::BORDER);
        d.setTextDatum(ML_DATUM);

        bool bandEnabled = false;
        char label[20];
        if (eq) {
            const EQFilterParams& bp = eq->getBandParams(bi);
            bandEnabled = (bp.enabled != 0);
            if (bandEnabled) {
                const char* tname = EQ_FILTER_TYPE_NAMES[(uint8_t)bp.type];
                snprintf(label, sizeof(label), "B%d [%s] %dHz", bi + 1, tname,
                        (int)bp.f0);
            } else {
                snprintf(label, sizeof(label), "B%d  (off)", bi + 1);
            }
        } else {
            snprintf(label, sizeof(label), "Band %d", bi + 1);
        }

        d.setTextColor(bandEnabled ? (focused ? Color::TEXT_FOCUS : Color::TEXT)
                                  : Color::TEXT_DIM,
                       rowBg);
        d.drawString(label, CONTENT_X + 6, y + (ROW_H - 2) / 2);

        d.setTextColor(bandEnabled ? Color::RED : Color::GREEN, rowBg);
        d.setTextDatum(MR_DATUM);
        d.drawString(bandEnabled ? "Press to DEL" : "Press ADD",
                     CONTENT_W + 20, y + (ROW_H - 2) / 2);
    }

    uint8_t cnt = getItemCount();
    uint8_t graphIdx = cnt - 2;
    uint8_t backIdx = cnt - 1;
    int16_t btnY = FOOTER_Y - NAV_BTN_H - 2;

    float hfGraph = (_holdTracking && _focusIdx == graphIdx)
                        ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS
                        : 0;
    float hfBack = (_holdTracking && _focusIdx == backIdx)
                       ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS
                       : 0;

    drawNavButton(CONTENT_X, btnY, CONTENT_W, NAV_BTN_H, "GRAPH",
                  _focusIdx == graphIdx, _holdTracking && _focusIdx == graphIdx,
                  hfGraph);
    drawNavButton(CONTENT_X, FOOTER_Y, CONTENT_W, NAV_BTN_H, "BACK",
                  _focusIdx == backIdx, _holdTracking && _focusIdx == backIdx,
                  hfBack);
}

void Display::drawEffectEqGraph() {
    TFT_eSprite& d = _spr;
    NavEntry& nav = currentNav();

    constexpr int16_t GX = 0, GY = 0, GW = DISP_W, GH = 190;

    d.fillRect(GX, GY, GW, GH, 0x0821);
    d.drawRect(GX, GY, GW, GH, Color::BORDER);

    const int8_t dbLines[] = { -24, -18, -12, -6, 0, 6, 12, 18, 24 };
    for (int8_t db : dbLines) {
        int16_t y = GY + GH / 2 - (int16_t)(db * GH / 48);
        uint16_t c = (db == 0) ? Color::BORDER : 0x18C3;
        d.drawFastHLine(GX, y, GW, c);
        if (db != 0) {
            char lbl[6];
            snprintf(lbl, sizeof(lbl), "%+d", (int)db);
            d.setTextColor(Color::TEXT_DIM, 0x0821);
            d.setTextDatum(ML_DATUM);
            d.setTextSize(1);
            d.drawString(lbl, GX + 2, y - 4);
        }
    }

    EqBandDesc bands[MAX_EQ_BANDS];
    ParametricEQ* eq = getEqPtr(nav.module);
    for (uint8_t i = 0; i < MAX_EQ_BANDS; i++) {
        if (eq) {
            const EQFilterParams& bp = eq->getBandParams(i);
            bands[i] = { bp.enabled != 0, (EQFilterType)bp.type, (float)bp.f0,
                         DB_Q8_TO_FLOAT(bp.gain), Q_Q610_TO_FLOAT(bp.Q) };
        } else {
            bands[i] = { (i == 0), EQFilterType::EQ_FILTER_TYPE_PEAKING, 1000, 0,
                         0.707f };
        }
    }

    drawEqCurve(bands, MAX_EQ_BANDS, GX, GY, GW, GH);

    for (uint8_t i = 0; i < MAX_EQ_BANDS; i++) {
        if (!bands[i].enabled) continue;
        float logF = log10f(bands[i].freq / 20) / log10f(20000 / 20);
        int16_t nx = GX + (int16_t)(logF * GW);
        int16_t ny = GY + GH / 2 - (int16_t)(bands[i].gain * GH / 48);
        bool focused = (_focusIdx == i);
        uint16_t col = focused ? Color::ACCENT : Color::ACCENT2;
        d.fillCircle(nx, ny, focused ? 7 : 5, col);
        d.drawCircle(nx, ny, focused ? 7 : 5, Color::TEXT);
        char num[4];
        snprintf(num, sizeof(num), "%d", i + 1);
        d.setTextColor(Color::BG, col);
        d.setTextDatum(MC_DATUM);
        d.setTextSize(1);
        d.drawString(num, nx, ny);
    }

    if (_focusIdx < MAX_EQ_BANDS && bands[_focusIdx].enabled) {
        const EqBandDesc& fb = bands[_focusIdx];
        const char* typeName = EQ_FILTER_TYPE_NAMES[(uint8_t)fb.type];
        char info[48];
        if (EQ_FILTER_HAS_GAIN[(uint8_t)fb.type]) {
            snprintf(info, sizeof(info), "B%d [%s] %.0fHz %.1fdB Q%.2f",
                    _focusIdx + 1, typeName, fb.freq, fb.gain, fb.q);
        } else {
            snprintf(info, sizeof(info), "B%d [%s] %.0fHz  Q%.2f",
                    _focusIdx + 1, typeName, fb.freq, fb.q);
        }
        d.setTextColor(Color::TEXT, Color::BG);
        d.setTextDatum(ML_DATUM);
        d.setTextSize(1);
        d.drawString(info, 4, GH + 2);
    }

    float hf = (_holdTracking && _focusIdx == getItemCount() - 1)
                   ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS
                   : 0;
    drawNavButton(230, 208, 80, 24, "BACK", _focusIdx == getItemCount() - 1,
                  _holdTracking && _focusIdx == getItemCount() - 1, hf);
}

void Display::drawEqBandEdit() {
    TFT_eSprite& d = _spr;
    NavEntry& nav = currentNav();

    ParametricEQ* eq = getEqPtr(nav.module);
    uint8_t bandIdx = nav.subContext;
    float curFreq = 1000, curGain = 0, curQ = 0.707f;
    if (eq) {
        const EQFilterParams& bp = eq->getBandParams(bandIdx);
        curFreq = (float)bp.f0;
        curGain = DB_Q8_TO_FLOAT(bp.gain);
        curQ = Q_Q610_TO_FLOAT(bp.Q);
    }

    char title[22];
    snprintf(title, sizeof(title), "Band %d  Edit", bandIdx + 1);

    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextDatum(ML_DATUM);
    d.setTextSize(1);
    d.drawString(title, CONTENT_X, 10);
    d.drawFastHLine(CONTENT_X, HEADER_H, CONTENT_W, Color::BORDER);

    bool hasGain = EQ_FILTER_HAS_GAIN[(uint8_t)_bandEditType];

    drawFilterTypeRow(CONTENT_X, HEADER_H + 4, CONTENT_W, _bandEditType,
                      _focusIdx == 0, _focusIdx == 0 && _editMode);

    drawSliderRow(CONTENT_X, HEADER_H + 4 + ROW_H, CONTENT_W, "Freq", curFreq,
                  20, 20000, "Hz", _focusIdx == 1,
                  _focusIdx == 1 && _editMode, 0);

    uint8_t qItem = hasGain ? 3 : 2;
    uint8_t backItem = qItem + 1;

    if (hasGain) {
        drawSliderRow(CONTENT_X, HEADER_H + 4 + 2 * ROW_H, CONTENT_W, "Gain",
                      curGain, -24, 24, "dB", _focusIdx == 2,
                      _focusIdx == 2 && _editMode);
    } else {
        int16_t y = HEADER_H + 4 + 2 * ROW_H;
        d.fillRect(CONTENT_X, y, CONTENT_W, ROW_H - 2, Color::BG);
        d.setTextColor(Color::TEXT_DIM, Color::BG);
        d.setTextDatum(ML_DATUM);
        d.drawString("Gain  —  N/A for this type", CONTENT_X + 4,
                      y + (ROW_H - 2) / 2);
    }

    drawSliderRow(CONTENT_X, HEADER_H + 4 + 3 * ROW_H, CONTENT_W, "Q", curQ,
                  0.1f, 10, "", _focusIdx == qItem,
                  _focusIdx == qItem && _editMode);

    float hf = (_holdTracking && _focusIdx == backItem)
                   ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS
                   : 0;
    drawNavButton(CONTENT_X, FOOTER_Y, CONTENT_W, NAV_BTN_H, "BACK",
                  _focusIdx == backItem, _holdTracking && _focusIdx == backItem,
                  hf);
}

void Display::drawEffectIsfCommon() {
    TFT_eSprite& d = _spr;

    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextDatum(ML_DATUM);
    d.setTextSize(1);
    d.drawString("ISF EQ Config", CONTENT_X, 10);
    d.drawFastHLine(CONTENT_X, HEADER_H, CONTENT_W, Color::BORDER);

    const char* cfgNames[] = { "RMS Window", "Slew Time", "Lookahead" };
    const char* cfgUnits[] = { "ms", "ms/step", "ms" };

    NavEntry& nav = currentNav();
    float cfgVals[3] = { 0 };
    for (uint8_t i = 0; i < 3; i++)
        cfgVals[i] = getParamValue(nav, i);

    for (uint8_t i = 0; i < 3; i++) {
        int16_t y = HEADER_H + 4 + i * ROW_H;
        drawInputRow(CONTENT_X, y, CONTENT_W, cfgNames[i], cfgVals[i],
                     cfgUnits[i], _focusIdx == i);
    }

    int16_t presetLabelY = HEADER_H + 4 + 3 * ROW_H;

    d.setTextColor(Color::TEXT_DIM, Color::BG);
    d.setTextDatum(ML_DATUM);
    d.drawString("Presets:", CONTENT_X, presetLabelY + 8);

    int16_t btnW = (CONTENT_W / 2) - 2;
    drawNavButton(CONTENT_X + 60, presetLabelY, btnW / 2, NAV_BTN_H - 2, "+Add",
                  _focusIdx == 3, false, 0);
    drawNavButton(CONTENT_X + 60 + btnW / 2 + 2, presetLabelY, btnW / 2,
                  NAV_BTN_H - 2, "-Del", _focusIdx == 4, false, 0);

    uint8_t isfPresetCount = 0;
    if (_pipeline) {
        auto& isf = (nav.module == DisplayModuleID::ISF1)
                       ? _pipeline->getIsf1()
                       : _pipeline->getIsf2();
        isfPresetCount = isf.getNumPresets();
    }

    constexpr uint8_t VISIBLE_ISF = 4;
    for (uint8_t i = 0;
         i < VISIBLE_ISF && ((uint8_t)_paramScroll + i) < isfPresetCount; i++) {
        uint8_t presetIdx = (uint8_t)_paramScroll + i;
        int16_t y = presetLabelY + NAV_BTN_H + 2 + i * 22;
        if (y + 22 > FOOTER_Y) break;
        bool focused = (_focusIdx == 5 + presetIdx);
        uint16_t rowBg = focused ? Color::PANEL : Color::BG;
        d.fillRect(CONTENT_X, y, CONTENT_W - 60, 20, rowBg);
        d.drawRect(CONTENT_X, y, CONTENT_W - 60, 20,
                   focused ? Color::ACCENT : Color::BORDER);
        char plabel[12];
        snprintf(plabel, sizeof(plabel), "Preset %d", presetIdx + 1);
        d.setTextColor(focused ? Color::TEXT_FOCUS : Color::TEXT, rowBg);
        d.setTextDatum(ML_DATUM);
        d.drawString(plabel, CONTENT_X + 4, y + 10);
        drawNavButton(CONTENT_X + CONTENT_W - 56, y, 54, 20, "EDIT",
                      focused, false, 0);
    }

    uint8_t cnt = getItemCount();
    float hf = (_holdTracking && _focusIdx == cnt - 1)
                   ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS
                   : 0;
    drawNavButton(CONTENT_X, FOOTER_Y, CONTENT_W, NAV_BTN_H, "BACK",
                  _focusIdx == cnt - 1, _holdTracking && _focusIdx == cnt - 1,
                  hf);
}

static const char* DRC_MODE_NAMES[] = { "Fullband", "2 Band", "2 Band + FB",
                                        "3 Band",  "3 Band + FB" };
static const char* DRC_CF_TYPE_NAMES[] = { "", "", "LR2", "LR4", "Linkwitz" };

void Display::drawDrcConfig() {
    TFT_eSprite& d = _spr;

    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextDatum(ML_DATUM);
    d.setTextSize(1);
    d.drawString("DRC Config", CONTENT_X, 10);
    d.drawFastHLine(CONTENT_X, HEADER_H, CONTENT_W, Color::BORDER);

    DRCMode mode = DRC_MODE_FULLBAND;
    DRCCrossoverType cfType = (DRCCrossoverType)2;
    if (_pipeline) {
        DRC& drc = _pipeline->getDrc();
        mode = drc._mode;
        cfType = drc._cfType;
    }
    _drcMode = (uint8_t)mode;
    _drcCfType = (uint8_t)cfType;

    // Mode row
    {
        int16_t y = HEADER_H + 4;
        bool focused = (_focusIdx == 0);
        bool editing = focused && _editMode;
        uint16_t bg = focused ? Color::PANEL : Color::BG;

        d.fillRect(CONTENT_X, y, CONTENT_W, ROW_H - 2, bg);
        d.drawRect(CONTENT_X, y, CONTENT_W, ROW_H - 2,
                   focused ? Color::ACCENT : Color::BORDER);
        d.setTextDatum(ML_DATUM);
        d.setTextColor(focused ? Color::TEXT_FOCUS : Color::TEXT, bg);
        d.drawString("Mode", CONTENT_X + 4, y + (ROW_H - 2) / 2);

        d.setTextColor(editing ? Color::ACCENT2 : Color::ACCENT, bg);
        d.setTextDatum(MR_DATUM);
        const char* modeStr = (_drcMode <= 4) ? DRC_MODE_NAMES[_drcMode] : "?";
        d.drawString(modeStr, CONTENT_X + CONTENT_W - 6, y + (ROW_H - 2) / 2);

        if (editing) {
            d.setTextColor(Color::ACCENT2, bg);
            d.drawString("< >", CONTENT_X + CONTENT_W - 6 - d.textWidth(modeStr) - 4,
                         y + (ROW_H - 2) / 2);
        }
    }

    // CF Type row
    {
        int16_t y = HEADER_H + 4 + ROW_H;
        bool focused = (_focusIdx == 1);
        bool editing = focused && _editMode;
        uint16_t bg = focused ? Color::PANEL : Color::BG;

        d.fillRect(CONTENT_X, y, CONTENT_W, ROW_H - 2, bg);
        d.drawRect(CONTENT_X, y, CONTENT_W, ROW_H - 2,
                   focused ? Color::ACCENT : Color::BORDER);
        d.setTextDatum(ML_DATUM);
        d.setTextColor(focused ? Color::TEXT_FOCUS : Color::TEXT, bg);
        d.drawString("CF Type", CONTENT_X + 4, y + (ROW_H - 2) / 2);

        d.setTextColor(editing ? Color::ACCENT2 : Color::ACCENT, bg);
        d.setTextDatum(MR_DATUM);
        const char* cfStr = DRC_CF_TYPE_NAMES[_drcCfType];
        d.drawString(cfStr, CONTENT_X + CONTENT_W - 6, y + (ROW_H - 2) / 2);

        if (editing) {
            d.setTextColor(Color::ACCENT2, bg);
            d.drawString("< >", CONTENT_X + CONTENT_W - 6 - d.textWidth(cfStr) - 4,
                         y + (ROW_H - 2) / 2);
        }
    }

    const char* xoverNames[] = { "Freq 1", "Q 1", "Freq 2", "Q 2" };
    const char* xoverUnits[] = { "Hz", "", "Hz", "" };
    float xoverVals[4] = { 200, 0.707f, 3000, 0.707f };

    if (_pipeline) {
        DRC& drc = _pipeline->getDrc();
        xoverVals[0] = (float)drc._fc[0];
        xoverVals[1] = Q_Q610_TO_FLOAT(drc._qLp);
        xoverVals[2] = (float)drc._fc[1];
        xoverVals[3] = Q_Q610_TO_FLOAT(drc._qHp);
    }

    for (uint8_t i = 0; i < 4; i++) {
        int8_t visIdx = (int8_t)i - _paramScroll;
        if (visIdx < 0 || visIdx >= VISIBLE_ROWS_DRC_CONFIG) continue;

        int16_t y = HEADER_H + 4 + (visIdx + 2) * ROW_H;
        if (y + ROW_H > FOOTER_Y) continue;

        bool focused = (_focusIdx == 2 + i);
        drawInputRow(CONTENT_X, y, CONTENT_W, xoverNames[i], xoverVals[i],
                     xoverUnits[i], focused);
    }

    uint8_t cnt = getItemCount();
    float hf = _holdTracking
                  ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS
                  : 0;
    int16_t halfW = (CONTENT_W - 2) / 2;

    drawNavButton(CONTENT_X, FOOTER_Y, halfW, NAV_BTN_H, "NEXT",
                  _focusIdx == cnt - 2, _holdTracking && _focusIdx == cnt - 2, hf);
    drawNavButton(CONTENT_X + halfW + 2, FOOTER_Y, halfW, NAV_BTN_H, "BACK",
                  _focusIdx == cnt - 1, _holdTracking && _focusIdx == cnt - 1, hf);
}

void Display::drawDrcBandSelect() {
    TFT_eSprite& d = _spr;

    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextDatum(ML_DATUM);
    d.setTextSize(1);
    d.drawString("DRC Bands", CONTENT_X, 10);
    d.drawFastHLine(CONTENT_X, HEADER_H, CONTENT_W, Color::BORDER);

    uint8_t bandCount = 1;
    if (_pipeline) {
        DRC& drc = _pipeline->getDrc();
        switch (drc._mode) {
        case DRC_MODE_FULLBAND:       bandCount = 1; break;
        case DRC_MODE_2BAND:          bandCount = 2; break;
        case DRC_MODE_2BAND_FULLBAND:  bandCount = 3; break;
        case DRC_MODE_3BAND:          bandCount = 3; break;
        case DRC_MODE_3BAND_FULLBAND:  bandCount = 4; break;
        }
    }

    const char* bandLabels[] = { "Band 1", "Band 2", "Band 3", "Fullband" };
    int16_t tabW = (CONTENT_W - 8) / 2;
    constexpr int16_t tabH = 40;

    for (uint8_t i = 0; i < bandCount; i++) {
        uint8_t col = i % 2;
        uint8_t row = i / 2;
        int16_t x = CONTENT_X + col * (tabW + 4);
        int16_t y = HEADER_H + 4 + row * (tabH + 4);
        bool focused = (_focusIdx == i);

        uint16_t bg = focused ? Color::ACCENT : Color::PANEL;
        uint16_t fg = focused ? Color::BG : Color::TEXT;

        d.fillRoundRect(x, y, tabW, tabH, 4, bg);
        d.drawRoundRect(x, y, tabW, tabH, 4,
                        focused ? Color::TEXT_FOCUS : Color::BORDER);
        d.setTextColor(fg, bg);
        d.setTextDatum(MC_DATUM);
        d.drawString(bandLabels[i], x + tabW / 2, y + tabH / 2);
    }

    uint8_t cnt = getItemCount();
    float hf = _holdTracking
                   ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS
                   : 0;
    drawNavButton(CONTENT_X, FOOTER_Y, CONTENT_W, NAV_BTN_H, "BACK",
                  _focusIdx == cnt - 1, _holdTracking && _focusIdx == cnt - 1,
                  hf);
}

void Display::drawDrcBandParams() {
    TFT_eSprite& d = _spr;

    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextDatum(ML_DATUM);
    d.setTextSize(1);

    char title[24];
    snprintf(title, sizeof(title), "DRC Band %u",
            (unsigned)(_drcActiveBand + 1));
    d.drawString(title, CONTENT_X, 10);
    d.drawFastHLine(CONTENT_X, HEADER_H, CONTENT_W, Color::BORDER);

    const char* bpNames[] = { "Pregain", "Threshold", "Ratio", "Attack",
                              "Release", "Lookahead" };
    const char* bpUnits[] = { "dB", "dB", ":1", "ms", "ms", "ms" };
    float bpVals[6] = { 0 };

    if (_pipeline) {
        DRC& drc = _pipeline->getDrc();
        uint8_t band = _drcActiveBand;
        float pregainLinear = PREGAIN_Q412_TO_FLOAT(drc._bands[band].pregainQ412);
        bpVals[0] = (pregainLinear > 0.0001f) ? roundf(20 * log10f(pregainLinear))
                                               : -96;
        bpVals[1] = DRC_TH_TO_FLOAT_DB(drc._bands[band].thresholdDbInt);
        bpVals[2] = (float)drc._bands[band].ratioX100 / 100;
        bpVals[3] = (float)drc._bands[band].attackMs;
        bpVals[4] = (float)drc._bands[band].releaseMs;
        bpVals[5] = drc._bands[band].lookaheadMs;
    }

    static const float bpMins[] = { -24, -60, 1, 1, 1, 0 };
    static const float bpMaxs[] = { 24, 0, 100, 2000, 2000, 10 };

    for (uint8_t i = 0; i < VISIBLE_ROWS_DRC_PARAMS; i++) {
        uint8_t pi = (uint8_t)(_paramScroll + i);
        if (pi >= 6) break;
        int16_t y = HEADER_H + 4 + i * ROW_H;
        bool focused = (_focusIdx == pi);
        bool editing = focused && _editMode;
        drawSliderRow(CONTENT_X, y, CONTENT_W, bpNames[pi], bpVals[pi],
                      bpMins[pi], bpMaxs[pi], bpUnits[pi], focused, editing);
    }

    int16_t graphY = HEADER_H + 4 + VISIBLE_ROWS_DRC_PARAMS * ROW_H + 2;
    int16_t graphH = FOOTER_Y - graphY - 4;

    if (graphH > 30) {
        constexpr int16_t GX = 30;
        int16_t GW = CONTENT_W - GX - 4;
        drawDrcCurve(bpVals[1], bpVals[2], bpVals[0], CONTENT_X + GX, graphY,
                     GW, graphH);
    }

    uint8_t cnt = getItemCount();
    float hf = _holdTracking
                   ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS
                   : 0;
    drawNavButton(CONTENT_X, FOOTER_Y, CONTENT_W, NAV_BTN_H, "BACK",
                  _focusIdx == cnt - 1, _holdTracking && _focusIdx == cnt - 1,
                  hf);
}

void Display::drawKeyboard() {
    TFT_eSprite& d = _spr;

    char valBuf[KB_MAX_LEN + 4];
    snprintf(valBuf, sizeof(valBuf), "> %s_", _kb.buf);

    d.setTextColor(Color::TEXT_FOCUS, Color::BG);
    d.setTextDatum(ML_DATUM);
    d.setTextSize(2);
    d.drawString(valBuf, 4, 10);

    constexpr int16_t KEY_W = 60, KEY_H = 36, KEY_GAP = 4;
    constexpr int16_t KBX = (DISP_W - KB_COLS * (KEY_W + KEY_GAP)) / 2;
    constexpr int16_t KBY = 40;

    for (uint8_t r = 0; r < KB_NUM_ROWS; r++) {
        for (uint8_t c = 0; c < KB_COLS; c++) {
            bool focused = (_kb.cursorRow == r && _kb.cursorCol == c);
            char label[2] = { KB_ROWS[r][c], '\0' };
            int16_t kx = KBX + c * (KEY_W + KEY_GAP);
            int16_t ky = KBY + r * (KEY_H + KEY_GAP);

            if (label[0] == '.' && !_kb.allowDot) {
                d.fillRect(kx, ky, KEY_W, KEY_H, Color::BORDER);
                d.drawRect(kx, ky, KEY_W, KEY_H, Color::BORDER);
                d.setTextColor(Color::TEXT_DIM, Color::BORDER);
            } else {
                uint16_t bg = focused ? Color::ACCENT : Color::PANEL;
                d.fillRect(kx, ky, KEY_W, KEY_H, bg);
                d.drawRect(kx, ky, KEY_W, KEY_H, Color::BORDER);
                d.setTextColor(focused ? Color::BG : Color::TEXT, bg);
            }

            d.setTextDatum(MC_DATUM);
            d.setTextSize(2);
            d.drawString(label, kx + KEY_W / 2, ky + KEY_H / 2);
        }
    }

    constexpr int16_t ACT_W = DISP_W / 3 - 4;
    int16_t ACTY = KBY + KB_NUM_ROWS * (KEY_H + KEY_GAP) + 4;
    const uint16_t actColors[] = { Color::YELLOW, Color::TEXT_DIM, Color::GREEN };

    for (uint8_t i = 0; i < KB_ACT_COLS; i++) {
        bool focused = (_kb.cursorRow == KB_NUM_ROWS && _kb.cursorCol == i);
        int16_t ax = 2 + i * (ACT_W + 4);
        uint16_t bg = focused ? actColors[i] : Color::PANEL;

        d.fillRect(ax, ACTY, ACT_W, KEY_H, bg);
        d.drawRect(ax, ACTY, ACT_W, KEY_H, Color::BORDER);
        d.setTextColor(focused ? Color::BG : actColors[i], bg);
        d.setTextDatum(MC_DATUM);
        d.setTextSize(1);
        d.drawString(KB_ACTIONS[i], ax + ACT_W / 2, ACTY + KEY_H / 2);
    }
}

void Display::drawSliderRow(int16_t x, int16_t y, int16_t w, const char* label,
                            float value, float minV, float maxV, const char* unit,
                            bool focused, bool editing) {
    drawSliderRow(x, y, w, label, value, minV, maxV, unit, focused, editing, 2);
}

void Display::drawSliderRow(int16_t x, int16_t y, int16_t w, const char* label,
                            float value, float minV, float maxV, const char* unit,
                            bool focused, bool editing, uint8_t decimals) {
    TFT_eSprite& d = _spr;
    int16_t h = ROW_H - 2;

    uint16_t bg, borderColor, textColor;
    if (focused) {
        if (_anim.focusTransition < 1) {
            uint8_t bl = (uint8_t)(_anim.focusTransition * 255);
            bg = blendColor(Color::BG, Color::PANEL, bl);
            borderColor = blendColor(Color::BORDER, Color::ACCENT, bl);
            textColor = blendColor(Color::TEXT, Color::TEXT_FOCUS, bl);
        } else {
            bg = Color::PANEL;
            borderColor = Color::ACCENT;
            textColor = Color::TEXT_FOCUS;
        }
    } else {
        bg = Color::BG;
        borderColor = Color::BORDER;
        textColor = Color::TEXT;
    }

    d.fillRect(x, y, w, h, bg);
    d.drawRect(x, y, w, h, borderColor);

    if (editing && focused && _anim.aniSweepActive) {
        drawSweepAnimation(x, y, w, h);
        textColor = Color::BG;
        bg = Color::ACCENT;
    } else if (!editing && focused && _anim.aniSweepExit && _anim.aniSweepActive) {
        drawSweepAnimation(x, y, w, h);
        textColor = Color::ACCENT;
        bg = Color::BG;
    }

    d.setTextDatum(ML_DATUM);
    d.setTextColor(textColor);
    d.setTextSize(1);
    d.drawString(label, x + 4, y + h / 2);

    constexpr int16_t LABEL_W = 80, VAL_W = 65, TRACK_PAD = 5;
    int16_t trackX = x + LABEL_W;
    int16_t trackW = w - LABEL_W - VAL_W - TRACK_PAD * 2;
    int16_t trackY = y + h / 2 - 3;
    int16_t trackH = 6;

    d.fillRect(trackX, trackY, trackW, trackH, Color::SLIDER_BG);

    float norm = (maxV > minV) ? (value - minV) / (maxV - minV) : 0;
    norm = norm < 0 ? 0 : (norm > 1 ? 1 : norm);
    int16_t fillW = (int16_t)(norm * trackW);
    if (fillW > 0) {
        d.fillRect(trackX, trackY, fillW, trackH, Color::SLIDER_FILL);
    }

    int16_t thumbX = trackX + fillW;
    d.fillCircle(thumbX, trackY + trackH / 2, editing ? 6 : 4,
                 editing ? Color::ACCENT2 : Color::ACCENT);
    if (editing) {
        d.drawCircle(thumbX, trackY + trackH / 2, 6, Color::BORDER);
    }

    char valBuf[10];
    formatFloat(valBuf, sizeof(valBuf), value, decimals, unit);
    int16_t valX = x + w - VAL_W + 2;
    d.fillRect(valX, y + 2, VAL_W - 4, h - 4, focused ? 0x18A3 : 0x1082);
    d.drawRect(valX, y + 2, VAL_W - 4, h - 4,
               editing ? Color::BORDER : focused ? Color::ACCENT : Color::BORDER);
    d.setTextDatum(MR_DATUM);
    d.setTextColor(focused ? Color::TEXT_FOCUS : Color::TEXT, focused ? 0x18A3 : 0x1082);
    d.drawString(valBuf, valX + VAL_W - 6, y + h / 2);
}

void Display::drawSwitchRow(int16_t x, int16_t y, int16_t w,
                            const char* label, bool value, bool focused) {
    TFT_eSprite& d = _spr;
    int16_t h = ROW_H - 2;

    uint16_t bg, borderColor, textColor;
    if (focused) {
        if (_anim.focusTransition < 1) {
            uint8_t bl = (uint8_t)(_anim.focusTransition * 255);
            bg = blendColor(Color::BG, Color::PANEL, bl);
            borderColor = blendColor(Color::BORDER, Color::ACCENT, bl);
            textColor = blendColor(Color::TEXT, Color::TEXT_FOCUS, bl);
        } else {
            bg = Color::PANEL;
            borderColor = Color::ACCENT;
            textColor = Color::TEXT_FOCUS;
        }
    } else {
        bg = Color::BG;
        borderColor = Color::BORDER;
        textColor = Color::TEXT;
    }

    d.fillRect(x, y, w, h, bg);
    d.drawRect(x, y, w, h, borderColor);

    if (_editMode && focused && _anim.aniSweepActive) {
        drawSweepAnimation(x, y, w, h);
        textColor = Color::BG;
        bg = Color::ACCENT;
    } else if (!_editMode && focused && _anim.aniSweepExit && _anim.aniSweepActive) {
        drawSweepAnimation(x, y, w, h);
        textColor = Color::ACCENT;
        bg = Color::BG;
    }

    d.setTextDatum(ML_DATUM);
    d.setTextColor(textColor);
    d.setTextSize(1);
    d.drawString(label, x + 4, y + h / 2);

    constexpr int16_t PW = 36, PH = 16;
    int16_t px = x + w - PW - 6;
    int16_t py = y + (h - PH) / 2;

    uint16_t pillBg = value ? (_editMode && focused) ? Color::BORDER : Color::ACCENT
                            : Color::BORDER;
    d.fillRoundRect(px, py, PW, PH, PH / 2, pillBg);

    int16_t knobX = value ? px + PW - PH / 2 - 1 : px + PH / 2 + 1;
    d.fillCircle(knobX, py + PH / 2, PH / 2 - 2, Color::TEXT_DIM);
}

void Display::drawInputRow(int16_t x, int16_t y, int16_t w, const char* label,
                            float value, const char* unit, bool focused) {
    TFT_eSprite& d = _spr;
    int16_t h = ROW_H - 2;

    uint16_t bg, borderColor, textColor;
    getFocusColors(focused, bg, borderColor, textColor);

    d.fillRect(x, y, w, h, bg);
    d.drawRect(x, y, w, h, borderColor);

    d.setTextDatum(ML_DATUM);
    d.setTextColor(textColor);
    d.setTextSize(1);
    d.drawString(label, x + 4, y + h / 2);

    constexpr int16_t VAL_W = 60;
    int16_t valX = x + w - VAL_W - 4;
    char valBuf[12];
    snprintf(valBuf, sizeof(valBuf), "%.1f %s", value, unit ? unit : "");

    constexpr uint16_t VAL_BG_FOCUSED = 0x18A3;
    constexpr uint16_t VAL_BG_NORMAL  = 0x1082;
    uint16_t valBg = focused ? VAL_BG_FOCUSED : VAL_BG_NORMAL;
    d.fillRect(valX, y + 2, VAL_W, h - 4, valBg);
    d.drawRect(valX, y + 2, VAL_W, h - 4, borderColor);
    d.setTextDatum(MC_DATUM);
    d.setTextColor(textColor);
    d.drawString(valBuf, valX + VAL_W / 2, y + h / 2);
}

void Display::drawNavButton(int16_t x, int16_t y, int16_t w, int16_t h,
                             const char* label, bool focused, bool holdProgress,
                             float holdFrac) {
    TFT_eSprite& d = _spr;

    uint16_t bg, borderColor, textColor;
    getFocusColors(focused, bg, borderColor, textColor);

    d.fillRect(x, y, w, h, bg);
    d.drawRect(x, y, w, h, borderColor);

    d.setTextDatum(MC_DATUM);
    d.setTextColor(textColor);
    d.setTextSize(1);
    d.drawString(label, x + w / 2, y + h / 2);

    if (holdProgress && holdFrac > 0) {
        int16_t barW = (int16_t)(holdFrac * (w - 4));
        d.fillRect(x + 2, y + h - 4, barW, 3, Color::ACCENT2);
    }
}

void Display::drawFilterTypeRow(int16_t x, int16_t y, int16_t w,
                                 EQFilterType current, bool focused,
                                 bool editing) {
    TFT_eSprite& d = _spr;

    constexpr uint8_t N = (uint8_t)EQFilterType::EQ_FILTER_TYPE_COUNT - 2;
    int16_t h = ROW_H - 2;
    constexpr int16_t LABEL_W = 36, GAP = 2;

    uint16_t rowBg = focused ? Color::PANEL : Color::BG;
    d.fillRect(x, y, w, h, rowBg);
    d.drawRect(x, y, w, h, focused ? Color::ACCENT : Color::BORDER);

    d.setTextDatum(ML_DATUM);
    d.setTextColor(focused ? Color::TEXT_FOCUS : Color::TEXT, rowBg);
    d.setTextSize(1);
    d.drawString("Type", x + 4, y + h / 2);

    int16_t pillAreaX = x + LABEL_W;
    int16_t pillAreaW = w - LABEL_W - 2;
    int16_t pillW = (pillAreaW - (N - 1) * GAP) / N;

    for (uint8_t i = 0; i < N; i++) {
        bool isActive = ((uint8_t)current == i);
        int16_t px = pillAreaX + i * (pillW + GAP);
        int16_t py = y + 3;
        int16_t ph = h - 6;

        uint16_t pillBg, pillFg;
        if (isActive && editing) {
            pillBg = Color::ACCENT;
            pillFg = Color::BG;
        } else if (isActive) {
            pillBg = Color::ACCENT;
            pillFg = Color::BG;
        } else {
            pillBg = focused ? 0x1884 : Color::BORDER;
            pillFg = focused ? Color::TEXT : Color::TEXT_DIM;
        }

        d.fillRoundRect(px, py, pillW, ph, 3, pillBg);
        if (isActive && editing) {
            d.drawRoundRect(px - 1, py - 1, pillW + 2, ph + 2, 4, Color::TEXT_FOCUS);
        } else if (isActive) {
            d.drawRoundRect(px, py, pillW, ph, 3, Color::TEXT);
        }

        d.setTextColor(pillFg, pillBg);
        d.setTextDatum(MC_DATUM);
        d.setTextSize(1);
        d.drawString(EQ_FILTER_TYPE_NAMES[i], px + pillW / 2, py + ph / 2);
    }

    if (editing) {
        d.setTextColor(Color::ACCENT2, rowBg);
        d.setTextDatum(MR_DATUM);
        d.setTextSize(1);
        d.drawString("< >", x + w - 2, y + h / 2);
    }
}

void Display::drawSideBars(int16_t x, int16_t y, int16_t h) {
    TFT_eSprite& d = _spr;
    constexpr int16_t BAR_W = 10;

    // CPU bar
    uint8_t cpuPct = (uint8_t)(_cpuTenths / 10);
    if (cpuPct > 100) cpuPct = 100;
    int16_t  cpuH   = (int16_t)((uint32_t)cpuPct * h / 100);
    uint16_t cpuCol = cpuPct > 80 ? Color::RED : cpuPct > 60 ? Color::YELLOW : Color::GREEN;
    d.drawRect(x, y, BAR_W, h, Color::BORDER);
    if (cpuH > 0) d.fillRect(x + 1, y + h - cpuH, BAR_W - 2, cpuH, cpuCol);
    d.setTextDatum(TC_DATUM);
    d.setTextColor(Color::TEXT_DIM, Color::BG);
    d.setTextSize(1);
    d.drawString("C", x + BAR_W / 2, y - 10);

    // Heap bar
    int16_t  hx     = x + BAR_W + 2;
    int16_t  heapH  = (int16_t)((uint32_t)_heapPct * h / 100);
    uint16_t heapCol = _heapPct > 80 ? Color::RED : _heapPct > 60 ? Color::YELLOW : Color::GREEN;
    d.drawRect(hx, y, BAR_W, h, Color::BORDER);
    if (heapH > 0) d.fillRect(hx + 1, y + h - heapH, BAR_W - 2, heapH, heapCol);
    d.setTextDatum(TC_DATUM);
    d.setTextColor(Color::TEXT_DIM, Color::BG);
    d.drawString("H", hx + BAR_W / 2, y - 10);
}

void Display::drawConnectionBadge(int16_t x, int16_t y) {
    TFT_eSprite& d = _spr;
    d.fillRect(x, y, 16, 16, Color::BG);
    if (_connectStatus == ConnectStatus::WIFI_WS_CONNECTED) {
        d.drawXBitmap(x, y, wifi_icon_bits, ASSETS_ICON_W, ASSETS_ICON_H, Color::GREEN);
    } else if (_connectStatus == ConnectStatus::WIFI_ENABLE) {
        d.drawXBitmap(x, y, wifi_icon_bits, ASSETS_ICON_W, ASSETS_ICON_H, Color::ACCENT);
    } else if (_connectStatus == ConnectStatus::WIFI_DISABLE) {
        d.drawXBitmap(x, y, wifi_icon_x_bits, ASSETS_ICON_W, ASSETS_ICON_H, Color::TEXT_DIM);
    } else if (_connectStatus == ConnectStatus::SERIAL_CONNECTED) {
        d.drawXBitmap(x, y, usb_icon_bits, ASSETS_ICON_W, ASSETS_ICON_H, Color::GREEN);
    }
}

static float biquadMagnitudeDb(uint8_t type,float freq,float f0,float Q,float gainDb,float fs){
    float A=powf(10,gainDb/40); float w0=TWO_PI*f0/fs, w=TWO_PI*freq/fs;
    float cosW0=cosf(w0), sinW0=sinf(w0), cosW=cosf(w), cos2W=cosf(2*w), sinW=sinf(w), sin2W=sinf(2*w);
    float b0=1,b1=0,b2=0,a0=1,a1=0,a2=0; if(Q<0.001f)Q=0.001f; float alpha;
    switch(type){
        case 0: alpha=sinW0/(2*Q); b0=1+alpha*A; b1=-2*cosW0; b2=1-alpha*A; a0=1+alpha/A; a1=-2*cosW0; a2=1-alpha/A; break;
        case 1:{alpha=sinW0/2*sqrtf((A+1/A)*(1/Q-1)+2); float sqrtA=sqrtf(A), twoSqrtAAlpha=2*sqrtA*alpha; b0=A*((A+1)-(A-1)*cosW0+twoSqrtAAlpha); b1=2*A*((A-1)-(A+1)*cosW0); b2=A*((A+1)-(A-1)*cosW0-twoSqrtAAlpha); a0=(A+1)+(A-1)*cosW0+twoSqrtAAlpha; a1=-2*((A-1)+(A+1)*cosW0); a2=(A+1)+(A-1)*cosW0-twoSqrtAAlpha; break;}
        case 2:{alpha=sinW0/2*sqrtf((A+1/A)*(1/Q-1)+2); float sqrtA=sqrtf(A), twoSqrtAAlpha=2*sqrtA*alpha; b0=A*((A+1)+(A-1)*cosW0+twoSqrtAAlpha); b1=-2*A*((A-1)+(A+1)*cosW0); b2=A*((A+1)+(A-1)*cosW0-twoSqrtAAlpha); a0=(A+1)-(A-1)*cosW0+twoSqrtAAlpha; a1=2*((A-1)-(A+1)*cosW0); a2=(A+1)-(A-1)*cosW0-twoSqrtAAlpha; break;}
        case 3: alpha=sinW0/(2*Q); b0=(1-cosW0)/2; b1=1-cosW0; b2=(1-cosW0)/2; a0=1+alpha; a1=-2*cosW0; a2=1-alpha; break;
        case 4: alpha=sinW0/(2*Q); b0=(1+cosW0)/2; b1=-(1+cosW0); b2=(1+cosW0)/2; a0=1+alpha; a1=-2*cosW0; a2=1-alpha; break;
        case 5: alpha=sinW0/(2*Q); b0=alpha; b1=0; b2=-alpha; a0=1+alpha; a1=-2*cosW0; a2=1-alpha; break;
        case 6: alpha=sinW0/(2*Q); b0=1; b1=-2*cosW0; b2=1; a0=1+alpha; a1=-2*cosW0; a2=1-alpha; break;
        default: return 0;
    }
    float invA0=1/a0; b0*=invA0; b1*=invA0; b2*=invA0; a1*=invA0; a2*=invA0;
    float numReal=b0+b1*cosW+b2*cos2W, numImag=-(b1*sinW+b2*sin2W);
    float denReal=1+a1*cosW+a2*cos2W, denImag=-(a1*sinW+a2*sin2W);
    float numMagSq=numReal*numReal+numImag*numImag, denMagSq=denReal*denReal+denImag*denImag;
    if(denMagSq<1e-20f)return 0; return 10*log10f(numMagSq/denMagSq);
}

void Display::drawEqCurve(const EqBandDesc* bands, uint8_t nBands,
                          int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
    TFT_eSprite& d = _spr;
    // Use rw as step count so each pixel column is drawn exactly once.
    // DISP_W was used before, causing multiple draws per pixel when rw < DISP_W.
    const int steps = rw;
    constexpr float FS = 96000.0f;
    int16_t prevY = -1;
    for (int px = 0; px < steps; px++) {
        float logF  = (float)px / steps;
        float freq  = 20.0f * powf(1000.0f, logF);
        float totalDb = 0.0f;
        for (uint8_t b = 0; b < nBands; b++) {
            if (!bands[b].enabled) continue;
            totalDb += biquadMagnitudeDb((uint8_t)bands[b].type, freq,
                                         bands[b].freq, bands[b].q,
                                         bands[b].gain, FS);
        }
        totalDb = totalDb < -24.0f ? -24.0f : (totalDb > 24.0f ? 24.0f : totalDb);
        int16_t curY = ry + rh / 2 - (int16_t)(totalDb * rh / 48);
        int16_t cx   = rx + px;
        if (prevY >= 0 && px > 0)
            d.drawLine(cx - 1, prevY, cx, curY, Color::ACCENT);
        prevY = curY;
    }
}

void Display::drawDrcCurve(float threshold, float ratio, float pregain,
                           int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
    TFT_eSprite& d = _spr;
    constexpr float    RANGE_DB   = 90.0f;
    constexpr uint16_t COL_BG     = 0x0821; // dark graph background
    constexpr uint16_t COL_GRID   = 0x18C3; // dim grid lines
    constexpr uint16_t COL_UNITY  = 0xC618; // unity-gain diagonal (grey)

    auto dbToX = [&](float db) -> int16_t {
        return rx + (int16_t)((db + RANGE_DB) / RANGE_DB * rw);
    };
    auto dbToY = [&](float db) -> int16_t {
        return ry + rh - (int16_t)((db + RANGE_DB) / RANGE_DB * rh);
    };
    auto transferFn = [&](float inputDb) -> float {
        if (inputDb <= threshold) return inputDb;
        return threshold + (inputDb - threshold) / ratio;
    };

    d.fillRect(rx, ry, rw, rh, COL_BG);

    // Grid lines
    d.setTextSize(1);
    const int8_t gridSteps[] = { -80, -70, -60, -50, -40, -30, -20, -10, 0 };
    for (int8_t db : gridSteps) {
        int16_t  gx      = dbToX(db);
        int16_t  gy      = dbToY(db);
        uint16_t gridCol = (db == 0) ? Color::BORDER : COL_GRID;
        d.drawFastVLine(gx, ry, rh, gridCol);
        d.drawFastHLine(rx, gy, rw, gridCol);
        if (db != 0 && rx > 16) {
            char lbl[6];
            snprintf(lbl, sizeof(lbl), "%d", db);
            d.setTextColor(Color::TEXT_DIM, COL_BG);
            d.setTextDatum(MR_DATUM);
            d.drawString(lbl, rx - 2, gy);
        }
    }

    // Unity-gain diagonal (dotted)
    for (int px = 0; px < rw; px += 4) {
        float   db = ((float)px / rw) * RANGE_DB - RANGE_DB;
        int16_t ux = dbToX(db), uy = dbToY(db);
        d.drawPixel(ux, uy, COL_UNITY);
    }

    // Threshold crosshair (dotted red)
    int16_t tx = dbToX(threshold), ty = dbToY(threshold);
    for (int16_t py = ry; py < ry + rh; py += 4) d.drawPixel(tx, py, Color::RED);
    for (int16_t px = rx; px < rx + rw; px += 4) d.drawPixel(px, ty, Color::RED);

    // Transfer function curve (green)
    int16_t prevX = -1, prevY = -1;
    for (int px = 0; px <= rw; px++) {
        float   inputDb  = ((float)px / rw) * RANGE_DB - RANGE_DB;
        float   outputDb = transferFn(inputDb) + pregain;
        int16_t cx = rx + px, cy = dbToY(outputDb);
        if (prevX >= 0) d.drawLine(prevX, prevY, cx, cy, Color::GREEN);
        prevX = cx; prevY = cy;
    }

    // Labels
    if (rh > 40) {
        int16_t lx = rx + 4, ly = ry + rh - 32;
        char thrLbl[16], ratLbl[12];
        snprintf(thrLbl, sizeof(thrLbl), "Thr %.0fdB", threshold);
        snprintf(ratLbl, sizeof(ratLbl), "%.0f:1",     ratio);
        d.setTextDatum(ML_DATUM);
        d.setTextSize(1);
        d.setTextColor(Color::RED,   COL_BG); d.drawString(thrLbl, lx, ly);
        d.setTextColor(Color::GREEN, COL_BG); d.drawString(ratLbl, lx, ly + 12);
    }
}

void Display::updateAnimations() {
    uint32_t now = millis();
    bool needsRedraw = false;

    // Focus transition animation
    if (_anim.focusTransition < 1) {
        uint32_t elapsed = now - _anim.focusStartMs;
        _anim.focusTransition = (float)elapsed / AnimationState::FOCUS_ANIM_MS;
        if (_anim.focusTransition > 1) {
            _anim.focusTransition = 1;
        }
        needsRedraw = true;
    }

    // Edit mode sweep animation
    if (_anim.aniSweepActive && _anim.aniSweepProgress < 1) {
        uint32_t elapsed = now - _anim.aniSweepStartMs;
        _anim.aniSweepProgress = (float)elapsed / AnimationState::SWEEP_ANIM_MS;
        if (_anim.aniSweepProgress > 1) {
            _anim.aniSweepProgress = 1;
        }
        needsRedraw = true;
    }

    // Value animation
    if (_anim.valueAnimating) {
        uint32_t elapsed = now - _anim.valueStartMs;
        _anim.valueProgress = (float)elapsed / AnimationState::VALUE_ANIM_MS;
        if (_anim.valueProgress >= 1) {
            _anim.valueProgress = 1;
            _anim.valueAnimating = false;
        }
        needsRedraw = true;
    }

    // Volume animation (splash → main menu with deceleration toward target)
    if (_anim.volAnimating) {
        uint32_t elapsed = now - _anim.volStartMs;
        float t = (float)elapsed / AnimationState::VOL_ANIM_MS;
        if (t >= 1) {
            t = 1;
            _anim.volAnimating = false;
        }
        // Ease-out cubic: fast start, slow near target
        float eased = 1 - (1 - t) * (1 - t) * (1 - t);
        _anim.volDisplay = _anim.volFrom + (_anim.volTo - _anim.volFrom) * eased;
        needsRedraw = true;
    }

    // Preset slot sweep (entry/exit when changing target)
    if (_anim.presetSweepActive) {
        uint32_t elapsed = now - _anim.presetSweepStartMs;
        _anim.presetSweepProgress = (float)elapsed / AnimationState::PRESET_SWEEP_MS;

        if (_anim.presetSweepProgress >= 1) {
            _anim.presetSweepProgress = 1;
            if (!_anim.presetSweepPhase2) { // Finished phase 1 (exit old preset), start phase 2 (entry new)
                // Save final BG for old slot (not target = BG color)
                if (_anim.presetPrevSlot >= 0 && _anim.presetPrevSlot < MAX_PRESET_SLOTS) {
                    _anim.presetSweepFinishedBg[_anim.presetPrevSlot] = Color::BG;
                }
                _anim.presetSweepStartMs = millis();
                _anim.presetSweepProgress = 0.0f;
                _anim.presetSweepPhase2 = true;
            } else { // Finished phase 2 (entry new preset)
                // Save final BG for new slot (is target = accent color)
                if (_anim.presetSweepSlot >= 0 && _anim.presetSweepSlot < 8) {
                    _anim.presetSweepFinishedBg[_anim.presetSweepSlot] = Color::ACCENT;
                }
                _anim.presetSweepActive = false;
                _anim.presetPrevSlot = -1; // Clear previous slot after full animation cycle
            }
        }
        needsRedraw = true;
    }

    // Module toggle sweep
    if (_anim.modSweepActive) {
        uint32_t elapsed = now - _anim.modSweepStartMs;
        _anim.modSweepProgress = (float)elapsed / AnimationState::MOD_SWEEP_MS;
        if (_anim.modSweepProgress >= 1) {
            _anim.modSweepProgress = 1;
            _anim.modSweepActive = false;
        }
        needsRedraw = true;
    }

    // Save button sweep (phase 1 = entry→accent, phase 2 = exit→BG)
    if (_anim.saveSweepActive) {
        uint32_t elapsed = now - _anim.saveSweepStartMs;
        _anim.saveSweepProgress = (float)elapsed / AnimationState::SAVE_SWEEP_MS;
        if (_anim.saveSweepProgress >= 1) {
            if (!_anim.saveSweepPhase2) {
                // Phase 1 done → start phase 2 (exit)
                _anim.saveSweepPhase2 = true;
                _anim.saveSweepProgress = 0;
                _anim.saveSweepStartMs = now;
            } else {
                // Phase 2 done → finished, save final BG
                _anim.saveSweepFinalBg = Color::BG;
                _anim.saveSweepActive = false;
                _anim.saveSweepPhase2 = false;
            }
        }
        needsRedraw = true;
    }

    if (needsRedraw) {
        _dirty = true;
    }

    // Clear sweep when done and not in edit mode
    if (_anim.aniSweepActive && _anim.aniSweepProgress >= 1 && !_dirty && !_editMode) {
        _anim.aniSweepActive = false;
        _dirty = true;
    }
}

void Display::startFocusTransition(){_anim.prevFocusIdx=_focusIdx; _anim.focusTransition=0; _anim.focusStartMs=millis();}
void Display::toggleEditMode(){
    bool wasEdit=_editMode; _editMode=!_editMode;
    if(!wasEdit&&_editMode){_anim.aniSweepExit=false; _anim.aniSweepActive=true; _anim.aniSweepProgress=0; _anim.aniSweepStartMs=millis();}
    if(wasEdit&&!_editMode){_anim.aniSweepExit=true; _anim.aniSweepProgress=0; _anim.aniSweepStartMs=millis();}
}
void Display::startValueAnimation(float from,float to){_anim.valueFrom=from;_anim.valueTo=to;_anim.valueProgress=0;_anim.valueStartMs=millis();_anim.valueAnimating=true;}
float Display::easeOutCubic(float t){float f=1-t; return 1-f*f*f;}
float Display::easeInOutQuad(float t){return t<0.5f?2*t*t:1-(-2*t+2)*(-2*t+2)/2;}
float Display::easeOutElastic(float t){constexpr float c4=(2*PI)/3; if(t==0)return 0; if(t==1)return 1; return powf(2,-10*t)*sinf((t*10-0.75f)*c4)+1;}

void Display::drawSweepAnimation(int16_t x, int16_t y, int16_t w, int16_t h,
                                 bool exit, float progress, uint16_t entryColor) {
    TFT_eSprite& d = _spr;

    // Done — fill final color
    if (progress >= 1) {
        d.fillRect(x, y, w, h, exit ? Color::BG : entryColor);
        return;
    }

    float eased = easeInOutQuad(progress);
    constexpr int16_t glowWidth = 20;

    if (!exit) {
        // Entry sweep: accent sweeps left → right
        int16_t sweepX = (int16_t)(eased * w);
        if (sweepX > 0) {
            d.fillRect(x, y, sweepX, h, entryColor);
        }
        if (sweepX < w && sweepX > 0) {
            for (int16_t dx = 0; dx < glowWidth && (sweepX + dx) < w; dx++) {
                uint8_t alpha = 255 - (uint8_t)((float)dx / glowWidth * 255);
                uint16_t col = blendColor(entryColor, Color::BG, 255 - alpha);
                d.drawFastVLine(x + sweepX + dx, y, h, col);
            }
        }
        int16_t remainW = w - sweepX - glowWidth;
        if (remainW > 0) {
            d.fillRect(x + sweepX + glowWidth, y, remainW, h, Color::BG);
        }
    } else {
        // Exit sweep: BG sweeps left → right (reveals accent behind)
        int16_t sweepX = (int16_t)(eased * w);
        if (sweepX > 0) {
            d.fillRect(x, y, sweepX, h, Color::BG);
        }
        if (sweepX < w && sweepX > 0) {
            for (int16_t dx = 0; dx < glowWidth && (sweepX + dx) < w; dx++) {
                uint8_t alpha = 255 - (uint8_t)((float)dx / glowWidth * 255);
                uint16_t col = blendColor(Color::BG, entryColor, 255 - alpha);
                d.drawFastVLine(x + sweepX + dx, y, h, col);
            }
        }
        int16_t remainW = w - sweepX - glowWidth;
        if (remainW > 0) {
            d.fillRect(x + sweepX + glowWidth, y, remainW, h, entryColor);
        }
    }
}

void Display::drawSweepAnimation(int16_t x, int16_t y, int16_t w, int16_t h,
                                 bool exit, float progress) {
    drawSweepAnimation(x, y, w, h, exit, progress, Color::ACCENT);
}

// Legacy: uses _anim state directly
void Display::drawSweepAnimation(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (!_editMode && _anim.aniSweepProgress >= 1) {
        _spr.fillRect(x, y, w, h, Color::BG);
        return;
    }
    if (_anim.aniSweepProgress >= 1) {
        _spr.fillRect(x, y, w, h, Color::ACCENT);
        return;
    }
    float eased = easeInOutQuad(_anim.aniSweepProgress);
    constexpr int16_t glowWidth = 20;

    if (!_anim.aniSweepExit) {
        int16_t sweepX = (int16_t)(eased * w);
        if (sweepX > 0) _spr.fillRect(x, y, sweepX, h, Color::ACCENT);
        if (sweepX < w && sweepX > 0) {
            for (int16_t dx = 0; dx < glowWidth && (sweepX + dx) < w; dx++) {
                uint8_t alpha = 255 - (uint8_t)((float)dx / glowWidth * 255);
                uint16_t col = blendColor(Color::ACCENT, Color::BG, 255 - alpha);
                _spr.drawFastVLine(x + sweepX + dx, y, h, col);
            }
        }
        int16_t remainW = w - sweepX - glowWidth;
        if (remainW > 0) _spr.fillRect(x + sweepX + glowWidth, y, remainW, h, Color::BG);
    } else {
        int16_t sweepX = (int16_t)(eased * w);
        if (sweepX > 0) _spr.fillRect(x, y, sweepX, h, Color::BG);
        if (sweepX < w && sweepX > 0) {
            for (int16_t dx = 0; dx < glowWidth && (sweepX + dx) < w; dx++) {
                uint8_t alpha = 255 - (uint8_t)((float)dx / glowWidth * 255);
                uint16_t col = blendColor(Color::BG, Color::ACCENT, 255 - alpha);
                _spr.drawFastVLine(x + sweepX + dx, y, h, col);
            }
        }
        int16_t remainW = w - sweepX - glowWidth;
        if (remainW > 0) _spr.fillRect(x + sweepX + glowWidth, y, remainW, h, Color::ACCENT);
    }
}

float Display::getAnimatedValue(float current){
    if(!_anim.valueAnimating) return current;
    float eased=easeOutCubic(_anim.valueProgress);
    return _anim.valueFrom+(_anim.valueTo-_anim.valueFrom)*eased;
}

void Display::drawTest(){_dirty=true;}