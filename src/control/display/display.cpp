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
#include "customfonts/Century751BT_12pt_GFX.h"
#include <cstdio>
#include <cstring>
#include <cmath>

#define TAG "Display"

// ─── Layout constants ──────────────────────────────────────────────────────────
static constexpr int16_t SIDEBAR_W    = 28;   // left stats column
static constexpr int16_t SIDEBAR_X    = 2;
static constexpr int16_t CONTENT_X    = SIDEBAR_W + 4;
static constexpr int16_t CONTENT_W    = DISP_W - CONTENT_X - 2;
static constexpr int16_t ROW_H        = 28;   // param row height
static constexpr int16_t NAV_BTN_H    = 26;
static constexpr int16_t HEADER_H     = 20;
static constexpr int16_t FOOTER_Y     = DISP_H - NAV_BTN_H - 2;

// ─── Module list (order matches pipeline) ──────────────────────────────────────
struct ModuleEntry {
    DisplayModuleID id;
    const char*     name;
    bool            hasGraph;  // true = has eq/drc graph screen
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
static constexpr uint8_t MODULE_COUNT =
    (uint8_t)(sizeof(MODULE_LIST) / sizeof(MODULE_LIST[0]));
static constexpr uint8_t PRESET_SLOT_COUNT = MAX_PRESET_SLOTS;
static constexpr uint8_t DSP_LIST_PRESET_SAVE_IDX = PRESET_SLOT_COUNT;
static constexpr uint8_t DSP_LIST_MODULE_START_IDX = PRESET_SLOT_COUNT + 1;

// ─── Keyboard layout ───────────────────────────────────────────────────────────
static const char* KB_ROWS[] = { "789", "456", "123", "0.-" };
static const char* KB_ACTIONS[] = { "DEL", "CANCEL", "ENTER" };
static constexpr uint8_t KB_COLS      = 3;
static constexpr uint8_t KB_NUM_ROWS  = 4;
static constexpr uint8_t KB_ACT_COLS  = 3;
// Total navigable items in keyboard = 4*3 + 3 = 15

// ─── Settings items ────────────────────────────────────────────────────────────
struct SettingItem {
    const char* name;
    bool*       value;   // nullptr → sub-action (e.g. shutdown)
};

// ─── EQ band slot helpers (fixed indices 0..MAX_EQ_BANDS-1) ───────────────────

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

// ─── Module / EQ lookup helpers ───────────────────────────────────────────────

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
    case DisplayModuleID::LEFT_EQ:         return &_pipeline->getLeftRightEq().getEqLeft(); // Left channel
    case DisplayModuleID::RIGHT_EQ:        return &_pipeline->getLeftRightEq().getEqRight(); // Right channel
    default: return nullptr;
    }
}

// ─── Parameter descriptor tables ─────────────────────────────────────────────
// Each module has a static table of UiParam descriptors.
// getScreenParams() fills outParams and returns count.
// Param indices must match getParamValue() / setParamValue() switch cases.

uint8_t Display::getScreenParams(NavEntry nav, UiParam* outParams) {
    switch (nav.module) {

    // ── Pre Gain / Post Gain (VolumeControl) ──────────────────────────────────
    case DisplayModuleID::PRE_GAIN:
    case DisplayModuleID::POST_GAIN: {
        static const UiParam p[] = {
            { "Gain",  "dB", -96.0f, 24.0f,  1.0f, 1 },
            { "Mute",  "",    0.0f,   1.0f,   1.0f, 0 },  // switch
            { "Mono",  "",    0.0f,   1.0f,   1.0f, 0 },  // switch
        };
        if (outParams) memcpy(outParams, p, sizeof(p));
        return 3;
    }

    // ── Compander ─────────────────────────────────────────────────────────────
    case DisplayModuleID::COMPANDER: {
        static const UiParam p[] = {
            { "Threshold",   "dB",  -60.0f,  0.0f,   1.0f, 1 },
            { "Ratio Below", ":1",   0.10f, 10.0f,   0.10f, 2 },
            { "Ratio Above", ":1",   0.10f, 10.0f,   0.10f, 2 },
            { "Attack",      "ms",   1.0f, 2000.0f,  1.0f, 0 },
            { "Release",     "ms",  10.0f, 2000.0f,  1.0f, 0 },
            { "Lookahead",   "ms",   0.0f,  10.0f,   1.0f, 0 },
        };
        if (outParams) memcpy(outParams, p, sizeof(p));
        return 6;
    }

    // ── Exciter ───────────────────────────────────────────────────────────────
    case DisplayModuleID::EXCITER: {
        static const UiParam p[] = {
            { "Cutoff Freq", "Hz",  300.0f, 10000.0f, 100.0f, 0 },
            { "Dry",         "%",    0.0f,   100.0f,   1.0f,  0 },
            { "Wet",         "%",    0.0f,   100.0f,   1.0f,  0 },
        };
        if (outParams) memcpy(outParams, p, sizeof(p));
        return 3;
    }

    // ── Dynamic Bass ──────────────────────────────────────────────────────────
    case DisplayModuleID::DYNAMIC_BASS: {
        static const UiParam p[] = {
            { "Cutoff Freq",    "Hz",  30.0f, 300.0f,  5.0f, 0 },
            { "Gain Boost",     "dB",  0.0f,  20.0f,   1.0f, 1 },
            { "Enhanced",       "",    0.0f,   1.0f,   1.0f, 0 },  // switch
            { "Boost Full Thr", "dB", -60.0f,  0.0f,  0.1f,  2 },
            { "Neutral Thr",    "dB", -60.0f,  0.0f,  0.1f,  2 },
            { "Clip Full Thr",  "dB", -60.0f,  0.0f,  0.1f,  2 },
            { "Attack",         "ms",  1.0f, 2000.0f,  1.0f, 0 },
            { "Release",        "ms",  1.0f, 2000.0f,  1.0f, 0 },
            { "Lookahead",      "ms",  0.0f,  10.0f,   1.0f, 0 },
        };
        if (outParams) memcpy(outParams, p, sizeof(p));
        return 9;
    }

    // ── Dynamic EQ Thresholds ─────────────────────────────────────────────────
    case DisplayModuleID::DYNAMIC_EQ_THRESH: {
        static const UiParam p[] = {
            { "Low Thresh",    "dB", -96.0f, 0.0f, 1.0f, 1 },
            { "Normal Thresh", "dB", -96.0f, 0.0f, 1.0f, 1 },
            { "High Thresh",   "dB", -96.0f, 0.0f, 1.0f, 1 },
            { "Attack",        "ms",  1.0f, 2000.0f, 1.0f, 0 },
            { "Release",       "ms",  1.0f, 2000.0f, 1.0f, 0 },
            { "Lookahead",     "ms",  0.0f,  10.0f,  1.0f, 0 },
        };
        if (outParams) memcpy(outParams, p, sizeof(p));
        return 6;
    }

    // ── ISF Common (shared for ISF1 / ISF2 — rms window, slew, lookahead) ────
    case DisplayModuleID::ISF1:
    case DisplayModuleID::ISF2: {
        static const UiParam p[] = {
            { "RMS Window",  "ms",      10.0f, 2000.0f, 10.0f, 0 },
            { "Slew Time",   "ms/step", 10.0f, 2000.0f, 10.0f, 0 },
            { "Lookahead",   "ms",       0.0f,   10.0f,  1.0f, 0 },
        };
        if (outParams) memcpy(outParams, p, sizeof(p));
        return 3;
    }

    default:
        return 0;
    }
}

// ─── getParamValue ────────────────────────────────────────────────────────────
float Display::getParamValue(NavEntry nav, uint8_t idx) {
    if (!_pipeline) return 0.0f;

    switch (nav.module) {

    case DisplayModuleID::PRE_GAIN:
    case DisplayModuleID::POST_GAIN: {
        VolumeControl* v = (nav.module == DisplayModuleID::PRE_GAIN)
                           ? &_pipeline->getPreGain() : &_pipeline->getPostGain();
        switch (idx) {
        case 0: return DB_Q8_TO_FLOAT(v->getGainDb());
        case 1: return v->isMuted() ? 1.0f : 0.0f;
        case 2: return v->isMono()  ? 1.0f : 0.0f;
        }
        break;
    }

    case DisplayModuleID::COMPANDER: {
        Compander& c = _pipeline->getCompander();
        switch (idx) {
        case 0: return (float)c._thresholdDbInt / 100.0f;
        case 1: return (float)c._ratioBelowQ88 / 256.0f;
        case 2: return (float)c._ratioAboveQ88 / 256.0f;
        case 3: return (float)c._attackMs;
        case 4: return (float)c._releaseMs;
        case 5: return c._lookaheadMs;
        }
        break;
    }

    case DisplayModuleID::EXCITER: {
        Exciter& e = _pipeline->getExciter();
        switch (idx) {
        case 0: return (float)e._fCut;
        case 1: return (float)e._dry;
        case 2: return (float)e._wet;
        }
        break;
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
        }
        break;
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
        }
        break;
    }

    case DisplayModuleID::ISF1:
    case DisplayModuleID::ISF2: {
        IndexSelectableFilter& isf = (nav.module == DisplayModuleID::ISF1)
                                     ? _pipeline->getIsf1() : _pipeline->getIsf2();
        switch (idx) {
        case 0: return (float)isf.getRmsWindowMs();
        case 1: return (float)isf.getSlewMs();
        case 2: return isf.getLookaheadMs();
        }
        break;
    }

    default: break;
    }
    return 0.0f;
}

// ─── setParamValue ────────────────────────────────────────────────────────────
void Display::setParamValue(NavEntry nav, uint8_t idx, float val, UiParam* param) {
    if (!_pipeline) return;

    switch (nav.module) {

    case DisplayModuleID::PRE_GAIN:
    case DisplayModuleID::POST_GAIN: {
        VolumeControl* v = (nav.module == DisplayModuleID::PRE_GAIN)
                           ? &_pipeline->getPreGain() : &_pipeline->getPostGain();
        switch (idx) {
        case 0: {
            v->setGainDb(FLOAT_TO_DB_Q8(val));
            break;
        }
        case 1: v->setMute(val >= 0.5f);  break;
        case 2: v->setMono(val >= 0.5f);  break;
        }
        break;
    }

    case DisplayModuleID::COMPANDER: {
        Compander& c = _pipeline->getCompander();
        switch (idx) {
        case 0: c.setThreshold((int32_t)(val * 100.0f));    break;
        case 1: c.setRatioBelow((int32_t)(val * 256.0f));   break;
        case 2: c.setRatioAbove((int32_t)(val * 256.0f));   break;
        case 3: c.setAttackTime((int32_t)val);              break;
        case 4: c.setReleaseTime((int32_t)val);             break;
        case 5: {
            c.setLookahead(val);
            if (val > 0 && param) {
                c.setAttackTime((int32_t)param[3].minVal);
                c.setReleaseTime((int32_t)param[4].minVal);
            }
            break;
        }
        }
        break;
    }

    case DisplayModuleID::EXCITER: {
        Exciter& e = _pipeline->getExciter();
        switch (idx) {
        case 0: e.setCutoffFreq((int32_t)val); break;
        case 1: e.setDry((int32_t)val);        break;
        case 2: e.setWet((int32_t)val);        break;
        }
        break;
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
            if (val > 0 && param) {
                b.setClipAttack((int32_t)param[6].minVal);
                b.setClipRelease((int32_t)param[7].minVal);
            }
            break;
        }
        }
        break;
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
            if (val > 0 && param) {
                d.setAttackTime((int32_t)param[3].minVal);
                d.setReleaseTime((int32_t)param[4].minVal);
            }
            break;
        }
        break;
        }
    }

    case DisplayModuleID::ISF1:
    case DisplayModuleID::ISF2: {
        IndexSelectableFilter& isf = (nav.module == DisplayModuleID::ISF1)
                                     ? _pipeline->getIsf1() : _pipeline->getIsf2();
        DBG_PRINTLN(idx);
        DBG_PRINTLN(val);
        switch (idx) {
        case 0: isf.setRmsWindowMs((int32_t)val); break;
        case 1: isf.setSlewMs((int32_t)val);      break;
        case 2: isf.setLookahead(val);            break;
        }
        break;
    }

    default: break;
    }
}

// ─── Helpers ──────────────────────────────────────────────────────────────────
void Display::formatFloat(char* buf, uint8_t bufLen, float val, uint8_t decimals) {
    // Simple fixed-point formatter avoiding printf %.*f on embedded
    char fmt[8];
    snprintf(fmt, sizeof(fmt), "%%.%df", (int)decimals);
    snprintf(buf, bufLen, fmt, val);
}

uint16_t Display::blendColor(uint16_t a, uint16_t b, uint8_t t) {
    // t=0 → a, t=255 → b
    uint8_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    uint8_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    uint8_t rr = ar + ((int)(br - ar) * t / 255);
    uint8_t rg = ag + ((int)(bg - ag) * t / 255);
    uint8_t rb = ab + ((int)(bb - ab) * t / 255);
    return (uint16_t)((rr << 11) | (rg << 5) | rb);
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────
void Display::init() {
    _tft.init();
    _tft.setRotation(1);   // landscape, USB/power on left
    _tft.fillScreen(Color::BG);

    // Allocate full-frame sprite (320*240*2 = 150 KB — ensure PSRAM or enough DRAM)
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
    
    // Initialize animation state
    _anim.reset();

    LOG_INFO(TAG, "Display initialized (ST7789 %dx%d)", _tft.width(), _tft.height());
}

void Display::setPipeline(DspPipeline* pipeline, PresetManager* presetMgr) {
    _pipeline  = pipeline;
    _presetMgr = presetMgr;

    if (_presetMgr && _presetMgr->hasMainMenuParam()) {
        _presetMgr->loadMainMenuParam(&_mmparam);
    } else {
        _mmparam.vol = 32;
        _mmparam.bass = 50;
        _mmparam.mid = 50;
        _mmparam.treble = 50;
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

// ─── Keyboard helper ──────────────────────────────────────────────────────────
void Display::pushKeyboard(float* target, float minVal, float maxVal, bool allowDot, ScreenID returnTo) {
    _kb.target       = target;
    _kb.minVal       = minVal;
    _kb.maxVal       = maxVal;
    _kb.allowDot     = allowDot;
    _kb.returnScreen = returnTo;
    _kb.len          = 0;
    _kb.buf[0]       = '\0';
    _kb.cursorCol    = 0;
    _kb.cursorRow    = 0;
    
    // Pre-fill current value
    if (target) {
        char tmp[KB_MAX_LEN + 1];
        formatFloat(tmp, sizeof(tmp), *target, allowDot ? 2 : 0);
        strncpy(_kb.buf, tmp, KB_MAX_LEN);
        _kb.len = strlen(_kb.buf);
    }
    
    pushScreen(ScreenID::KEYBOARD);
}

void Display::setStats(uint16_t cpuTenths, uint8_t heapPct,
                       uint32_t sampleRate,  bool wifiConnected,
                       bool clockAbsent) {
    bool changed = (cpuTenths  != _cpuTenths)  ||
                   (heapPct    != _heapPct)     ||
                   (sampleRate != _sampleRate)  ||
                   (wifiConnected != _wifiConn) ||
                   (clockAbsent  != _clockAbsent);
    _cpuTenths   = cpuTenths;
    _heapPct     = 100 - heapPct;
    _sampleRate  = sampleRate;
    _wifiConn    = wifiConnected;
    _clockAbsent = clockAbsent;
    if (changed) _dirty = true;
}

// ─── Navigation stack ─────────────────────────────────────────────────────────
void Display::pushScreen(ScreenID s, DisplayModuleID mod, uint8_t subCtx) {
    if (_navTop < NAV_STACK_DEPTH - 1) _navTop++;
    _navStack[_navTop] = { s, mod, subCtx };
    _focusIdx  = 0;
    _editMode  = false;
    _paramScroll = 0;
    _holdTracking = false;

    // Track enabled band count when entering EQ screens (slots stay fixed)
    if (s == ScreenID::EFFECT_EQ_COMMON || s == ScreenID::EFFECT_EQ_GRAPH) {
        ParametricEQ* eq = getEqPtr(mod);
        if (eq) {
            _eqBandCount = countEnabledEqBands(eq);
            if (_eqBandCount == 0) _eqBandCount = 1;
        }
    }

    // Load band type when entering band edit
    if (s == ScreenID::EQ_BAND_EDIT) {
        ParametricEQ* eq = getEqPtr(mod);
        if (eq && subCtx < MAX_EQ_BANDS) {
            _bandEditType = (EQFilterType)eq->getBandParams(subCtx).type;
        }
    }

    // Focus first enabled band when entering EQ graph
    if (s == ScreenID::EFFECT_EQ_GRAPH) {
        ParametricEQ* eq = getEqPtr(mod);
        if (eq) {
            for (uint8_t i = 0; i < MAX_EQ_BANDS; i++) {
                if (eq->getBandParams(i).enabled) {
                    _focusIdx = i;
                    break;
                }
            }
        }
    }

    _dirty = true;
}

void Display::popScreen() {
    if (_navTop > 0) _navTop--;
    _focusIdx  = 0;
    _editMode  = false;
    _paramScroll = 0;
    _holdTracking = false;
    _dirty = true;
}

NavEntry& Display::currentNav() { return _navStack[_navTop]; }
ScreenID  Display::currentScreen() const { return _navStack[_navTop].screen; }

// ─── Main update ──────────────────────────────────────────────────────────────
void Display::update(EncoderEvent enc) {
    // ── Splash auto-advance ────────────────────────────────────────────────────
    if (currentScreen() == ScreenID::SPLASH) {
        if (millis() - _splashStartMs >= SPLASH_DURATION_MS) {
            // Replace splash with main menu (don't stack)
            _navStack[_navTop] = { ScreenID::MAIN_MENU,
                                   DisplayModuleID::NONE, 0 };
            _focusIdx = 0;
            _dirty    = true;
        }
    }

    // ── Live meter polling (10 Hz refresh for main menu and DSP list) ─────────
    if ((currentScreen() == ScreenID::MAIN_MENU || currentScreen() == ScreenID::DSP_LIST) && _pipeline) {
        uint32_t now = millis();
        if (now - _lastMeterUpdate >= METER_UPDATE_INTERVAL_MS) {
            _lastMeterUpdate = now;
            
            // Update Compander meter
            Compander& comp = _pipeline->getCompander();
            if (comp.isEnabled()) {
                _companderEnv = comp.getEnvLinear();
                _companderGainDb = comp.getGainDb();
            } else {
                _companderEnv = 0.0f;
                _companderGainDb = 0.0f;
            }
            
            // Update DRC meter (all 4 bands)
            DRC& drc = _pipeline->getDrc();
            if (drc.isEnabled()) {
                for (uint8_t i = 0; i < 4; i++) {
                    _drcGainDb[i] = drc.getBandGainDb(i);
                }
            } else {
                for (uint8_t i = 0; i < 4; i++) {
                    _drcGainDb[i] = 0.0f;
                }
            }
            
            // Update Dynamic Bass meter
            DynamicBass& dynBass = _pipeline->getDynamicBass();
            if (dynBass.isEnabled()) {
                _dynBassAlpha = dynBass.getAlpha();
                _dynBassEnergyDb = dynBass.getEnergyDb();
            } else {
                _dynBassAlpha = 0.0f;
                _dynBassEnergyDb = -96.0f;
            }
            
            // Update Dynamic EQ meter
            DynamicEQ& dynEq = _pipeline->getDynamicEq();
            if (dynEq.isEnabled()) {
                _dynEqAlphaLow = dynEq.getAlphaLow();
                _dynEqAlphaHigh = dynEq.getAlphaHigh();
                _dynEqEnergyDb = dynEq.getEnergyDb();
            } else {
                _dynEqAlphaLow = 0.0f;
                _dynEqAlphaHigh = 0.0f;
                _dynEqEnergyDb = -96.0f;
            }
            
            _dirty = true; // Redraw to show updated meters
        }
    }

    // ── Update animations ─────────────────────────────────────────────────────
    updateAnimations();
    
    if (enc != EncoderEvent::NONE) {
        handleEncoder(enc);
    }

    // ── Main Menu Auto-save (3s idle after editing Vol/Bass/Mid/Treble) ──────
    if (_mainMenuAutosaveArmed && _pipeline && _presetMgr) {
        if (millis() - _mainMenuLastEditMs >= 3000) {
            _mainMenuAutosaveArmed = false;
            _presetMgr->saveMainMenuParam(&_mmparam);
            LOG_INFO(TAG, "Saved Main Menu Param.");
        }
    }

    if (_dirty) {
        draw();
        _dirty = false;
    }
}

// ─── Encoder handling ─────────────────────────────────────────────────────────
void Display::handleEncoder(EncoderEvent enc) {
    ScreenID s = currentScreen();
    int8_t   dir = 0;
    if      (enc == EncoderEvent::CW)  dir = +1;
    else if (enc == EncoderEvent::CCW) dir = -1;

    // ── SW_HOLD5: power off (any screen) ──────────────────────────────────────
    if (enc == EncoderEvent::SW_HOLD5) {
#ifdef SOFT_LATCH_SHUTDOWN
        g_userShutdownRequest = true;
#endif
        return;
    }

    // ── SW_HOLD3: context-dependent action ────────────────────────────────────
    if (enc == EncoderEvent::SW_HOLD3) {
        if (s == ScreenID::MAIN_MENU) {
            // MAIN_MENU: hold 3s → SETTING
            pushScreen(ScreenID::SETTINGS);
            _dirty = true;
            return;
        }
        // Other screens: hold 3s → back to MAIN_MENU
        if (s != ScreenID::SPLASH) {
            while (_navTop > 0) _navTop--;
            _navStack[_navTop] = { ScreenID::MAIN_MENU, DisplayModuleID::NONE, 0 };
            _focusIdx   = 0;
            _editMode   = false;
            _paramScroll = 0;
            _holdTracking = false;
            _dirty = true;
        }
        return;
    }

    // ── Rotation in edit mode ─────────────────────────────────────────────────
    if (dir != 0 && _editMode) {
        editDelta(dir);
        _dirty = true;
        return;
    }

    // ── Rotation in navigation mode ───────────────────────────────────────────
    if (dir != 0) {
        // KEYBOARD: always navigate grid with CW/CCW (no focus index)
        if (s == ScreenID::KEYBOARD) {
            editDelta(dir);
            _dirty = true;
            return;
        }
        
        // MAIN_MENU: CW/CCW directly edits the focused row (Vol/Bass/Mid/Treble)
        if (s == ScreenID::MAIN_MENU && _focusIdx < 4) {
            editDelta(dir);
            _dirty = true;
            return;
        }

        // Save current focus BEFORE any calculations for animation
        uint8_t oldFocus = _focusIdx;
        
        int16_t next = (int16_t)_focusIdx + dir;
        _itemCount = getItemCount();
        if (next < 0)           next = (int16_t)(_itemCount - 1);
        if (next >= _itemCount) next = 0;

        // EQ graph: skip disabled fixed slots while navigating nodes
        if (s == ScreenID::EFFECT_EQ_GRAPH && (uint8_t)next < MAX_EQ_BANDS) {
            ParametricEQ* eq = getEqPtr(currentNav().module);
            uint8_t tries = 0;
            while (eq && tries < MAX_EQ_BANDS &&
                   !eq->getBandParams((uint8_t)next).enabled) {
                next += dir;
                if (next < 0)           next = (int16_t)(_itemCount - 1);
                if (next >= _itemCount) next = 0;
                if ((uint8_t)next >= MAX_EQ_BANDS) break;
                tries++;
            }
        }

        // EFFECT_COMMON: skip attack/release when lookahead > 1
        if (s == ScreenID::EFFECT_COMMON && _pipeline) {
            NavEntry& nav = currentNav();
            uint8_t paramCount = _itemCount - 1; // exclude BACK
            uint8_t tries = 0;
            
            while (tries < paramCount) {
                bool shouldSkip = false;
                
                if ((uint8_t)next < paramCount) {
                    if (nav.module == DisplayModuleID::COMPANDER) {
                        float lookahead = getParamValue(nav, 5);
                        if (lookahead > 0 && ((uint8_t)next == 3 || (uint8_t)next == 4)) {
                            shouldSkip = true;
                        }
                    } else if (nav.module == DisplayModuleID::DYNAMIC_BASS) {
                        float lookahead = getParamValue(nav, 8);
                        if (lookahead > 0 && ((uint8_t)next == 6 || (uint8_t)next == 7)) {
                            shouldSkip = true;
                        }
                    } else if (nav.module == DisplayModuleID::DYNAMIC_EQ_THRESH) {
                        float lookahead = getParamValue(nav, 5);
                        if (lookahead > 0 && ((uint8_t)next == 3 || (uint8_t)next == 4)) {
                            shouldSkip = true;
                        }
                    }
                }
                
                if (!shouldSkip) break;
                
                next += dir;
                if (next < 0)           next = (int16_t)(_itemCount - 1);
                if (next >= _itemCount) next = 0;
                tries++;
            }
        }

        // Apply new focus and trigger animation
        _anim.prevFocusIdx = oldFocus;  // Use saved old focus
        _focusIdx     = (uint8_t)next;
        _holdTracking = false;
        
        // Trigger focus animation (don't overwrite prevFocusIdx!)
        _anim.focusTransition = 0.0f;
        _anim.focusStartMs = millis();
        
        onFocusChanged();
        _dirty = true;
        return;
    }

    // ── Button events ─────────────────────────────────────────────────────────
    if (enc == EncoderEvent::SW_DOUBLE) {
        switch (s) {
        case ScreenID::MAIN_MENU:
            // Double click → DSP_LIST
            pushScreen(ScreenID::DSP_LIST);
            _anim.reset();
            _anim.aniSweepActive = true;
            break;
        case ScreenID::DSP_LIST: {
            const uint8_t backIdx = getItemCount() - 1;
            if (_focusIdx == backIdx) {
                popScreen();
            } else if (_focusIdx < PRESET_SLOT_COUNT) {
                if (_presetMgr && _pipeline) {
                    _presetTargetSlot = _focusIdx;
                    _presetMgr->loadPreset(_presetTargetSlot, *_pipeline);
                    _presetMgr->saveCurrentSlotIndex(_presetTargetSlot);
                }
            } else if (_focusIdx == DSP_LIST_PRESET_SAVE_IDX) {
                if (_presetMgr && _pipeline) {
                    _presetMgr->savePreset(_presetTargetSlot, *_pipeline);
                    _presetMgr->saveCurrentSlotIndex(_presetTargetSlot);
                }
            } else {
                uint8_t moduleIdx = _focusIdx - DSP_LIST_MODULE_START_IDX;
                if (moduleIdx < MODULE_COUNT) {
                    const ModuleEntry& m = MODULE_LIST[moduleIdx];
                    if (m.isIsf)       pushScreen(ScreenID::EFFECT_ISF_COMMON, m.id);
                    else if (m.isDrc)  pushScreen(ScreenID::DRC_CONFIG, m.id);
                    else if (m.isEq)   pushScreen(ScreenID::EFFECT_EQ_COMMON,  m.id);
                    else               pushScreen(ScreenID::EFFECT_COMMON,     m.id);
                }
            }
            break;
        }
        case ScreenID::EFFECT_ISF_COMMON:
            // Don't intercept cfg params (idx 0-2) - let them reach onConfirm() for keyboard
            break;
        default:
            break;
        }
        _dirty = true;
        return;
    }

    if (enc == EncoderEvent::SW) {
        // Single click behavior per screen
        if (s == ScreenID::MAIN_MENU) {
            // Cycle focus: Vol→Bass→Mid→Treble→Vol
            uint8_t oldIdx = _focusIdx;
            _focusIdx = (_focusIdx + 1) % 4;
            _holdTracking = false;
            
            // Trigger focus transition animation
            if (oldIdx != _focusIdx) {
                startFocusTransition();
            }
            
            _dirty = true;
            return;
        }
        if (s == ScreenID::SETTINGS) {
            // Single click: toggle edit mode for switches or execute action
            if (_focusIdx == 3) {
                // [BACK]
                onConfirm();
            } else {
                // Toggle edit mode
                _editMode = !_editMode;
            }
            _dirty = true;
            return;
        }
        if (s == ScreenID::DSP_LIST) {
            const uint8_t backIdx = getItemCount() - 1;
            if (_focusIdx == backIdx) {
                popScreen();
                _dirty = true;
                return;
            }
            if (_focusIdx < PRESET_SLOT_COUNT) {
                if (_presetMgr && _pipeline) {
                    _presetTargetSlot = _focusIdx;
                    _presetMgr->loadPreset(_presetTargetSlot, *_pipeline);
                    _presetMgr->saveCurrentSlotIndex(_presetTargetSlot);
                }
                _dirty = true;
                return;
            }
            if (_focusIdx == DSP_LIST_PRESET_SAVE_IDX) {
                if (_presetMgr && _pipeline) {
                    _presetMgr->savePreset(_presetTargetSlot, *_pipeline);
                    _presetMgr->saveCurrentSlotIndex(_presetTargetSlot);
                }
                _dirty = true;
                return;
            }
            if (_focusIdx >= DSP_LIST_MODULE_START_IDX) {
                uint8_t moduleIdx = _focusIdx - DSP_LIST_MODULE_START_IDX;
                if (moduleIdx < MODULE_COUNT) {
                    DspModule* mod = getModulePtr(MODULE_LIST[moduleIdx].id);
                    if (mod && mod->getModuleId() != MODULE_ID_PRE_GAIN && mod->getModuleId() != MODULE_ID_POST_GAIN) mod->setEnabled(!mod->isEnabled());
                }
                _dirty = true;
                return;
            }
        }
        if (s == ScreenID::MAIN_MENU && _focusIdx < 4) {
            _mainMenuLastEditMs = millis();
            _mainMenuAutosaveArmed = true;
        }
        if (s == ScreenID::EFFECT_EQ_COMMON) {
            uint8_t cnt = getItemCount();
            if (_focusIdx == 0) {
                _editMode = !_editMode;
            } else if (_focusIdx >= 1 && _focusIdx <= MAX_EQ_BANDS) {
                ParametricEQ* eq = getEqPtr(currentNav().module);
                toggleEqBandSlot(eq, (uint8_t)(_focusIdx - 1));
                _eqBandCount = countEnabledEqBands(eq);
                _editMode = false;
            } else {
                onConfirm(); // GRAPH / BACK
            }
            _dirty = true;
            return;
        }
        // All other screens: call onConfirm
        onConfirm();
        _dirty = true;
        return;
    }
}

void Display::onFocusChanged() {
    // Start focus transition animation
    startFocusTransition();
    
    ScreenID s   = currentScreen();
    uint8_t  cnt = getItemCount();

    if (s == ScreenID::EFFECT_COMMON) {
        // Items: 0..(paramCount-1) = params, last = [BACK]
        // VISIBLE_ROWS = 6 (matches drawEffectCommon layout)
        uint8_t paramCount = (cnt > 0) ? cnt - 1 : 0; // exclude BACK
        if (_focusIdx >= paramCount) return; // on BACK — don't scroll
        if ((int8_t)_focusIdx < _paramScroll)
            _paramScroll = (int8_t)_focusIdx;
        if ((int8_t)_focusIdx >= _paramScroll + VISIBLE_ROWS_COMMON)
            _paramScroll = (int8_t)_focusIdx - VISIBLE_ROWS_COMMON + 1;
    }
    else if (s == ScreenID::EFFECT_EQ_COMMON) {
        // Items: 0=pregain, 1..MAX_EQ_BANDS=fixed band slots, cnt-2=GRAPH, cnt-1=BACK
        
        uint8_t navStart = (cnt >= 2) ? cnt - 2 : 0;
        if (_focusIdx == 0) {
            _paramScroll = 0; // pregain always at top
            return;
        }
        if (_focusIdx >= navStart) return; // on nav button — don't scroll
        int8_t bandFocus = (int8_t)(_focusIdx - 1); // 0-based band
        if (bandFocus < _paramScroll)
            _paramScroll = bandFocus;
        if (bandFocus >= _paramScroll + VISIBLE_ROWS_EQ)
            _paramScroll = bandFocus - VISIBLE_ROWS_EQ + 1;
    }
    else if (s == ScreenID::DRC_BAND_PARAMS) {
        // Scroll per-band params (focus 0..5 = params)
        int8_t pi = _focusIdx;
        if (pi >= 6) return; // BACK/GRAPH buttons

        if (pi < _paramScroll)
            _paramScroll = pi;
        if (pi >= _paramScroll + VISIBLE_ROWS_DRC_PARAMS)
            _paramScroll = pi - VISIBLE_ROWS_DRC_PARAMS + 1;
    }
    else if (s == ScreenID::DRC_CONFIG) {
        // Scroll crossover params (focus 1..4: freq1, q1, freq2, q2)
        int8_t pi = _focusIdx - 1;
        if (pi < 0 || pi >= 4) return;

        if (pi < _paramScroll)
            _paramScroll = pi;
        if (pi >= _paramScroll + VISIBLE_ROWS_DRC_CONFIG)
            _paramScroll = pi - VISIBLE_ROWS_DRC_CONFIG + 1;
    }
    else if (s == ScreenID::EFFECT_ISF_COMMON) {
        // Items: 0..2=cfg params, 3=+Add, 4=-Del, 5..(5+N-1)=preset rows, last=BACK
        // Cfg params + nav buttons are fixed; preset rows scroll
        constexpr uint8_t cfgEnd = 5; // first preset index
        uint8_t backIdx = cnt - 1;
        if (_focusIdx < cfgEnd || _focusIdx == backIdx) return; // no scroll
        // Preset rows: scroll to show focused preset
        uint8_t presetIdx = _focusIdx - cfgEnd;
        if (presetIdx < (uint8_t)_paramScroll)
            _paramScroll = presetIdx;
        if (presetIdx >= (uint8_t)(_paramScroll + VISIBLE_ISF_PRESET))
            _paramScroll = presetIdx - VISIBLE_ISF_PRESET + 1;
    }
}

// ─── Confirm action ───────────────────────────────────────────────────────────
void Display::onConfirm() {
    ScreenID s = currentScreen();
    NavEntry& nav = currentNav();

    switch (s) {
    // ── Settings ──────────────────────────────────────────────────────────────
    case ScreenID::SETTINGS:
        // Items: 0=wifi toggle, 1=trigger toggle, 2=[SHUTDOWN], 3=[BACK]
        if (_focusIdx == 3) {
            popScreen();
        } else if (_focusIdx == 2) {
            // Shutdown action (placeholder)
            // TODO: implement shutdown logic
        } else {
            // Wifi/Trigger toggles: single-click enters edit, another click exits
            _editMode = !_editMode;
        }
        break;

    // ── Effect common ─────────────────────────────────────────────────────────
    case ScreenID::EFFECT_COMMON: {
        // Last item is always [BACK]
        uint8_t cnt = getItemCount();
        if (_focusIdx == cnt - 1) {
            popScreen();
        } else {
            // Toggle edit mode with animation
            toggleEditMode();
        }
        break;
    }

    // ── EQ common ─────────────────────────────────────────────────────────────
    case ScreenID::EFFECT_EQ_COMMON: {
        uint8_t cnt = getItemCount();
        if (_focusIdx == cnt - 1) {
            popScreen();
        } else if (_focusIdx == cnt - 2) {
            pushScreen(ScreenID::EFFECT_EQ_GRAPH, nav.module, nav.subContext);
        }
        break;
    }

    // ── EQ graph: enter band edit only for enabled slots ─────────────────────
    case ScreenID::EFFECT_EQ_GRAPH:
        if (_focusIdx == getItemCount() - 1) {
            popScreen();
        } else if (_focusIdx < MAX_EQ_BANDS) {
            ParametricEQ* eq = getEqPtr(nav.module);
            if (eq && eq->getBandParams(_focusIdx).enabled) {
                pushScreen(ScreenID::EQ_BAND_EDIT, nav.module, _focusIdx);
            }
        }
        break;

    // ── EQ band edit ──────────────────────────────────────────────────────────
    case ScreenID::EQ_BAND_EDIT: {
        uint8_t cnt     = getItemCount();
        uint8_t backIdx = cnt - 1;
        if (_focusIdx == backIdx) {
            popScreen();
        } else {
            toggleEditMode();
        }
        break;
    }

    // ── ISF common ────────────────────────────────────────────────────────────
    case ScreenID::EFFECT_ISF_COMMON: {
        uint8_t cnt = getItemCount();
        uint8_t backIdx = cnt - 1;
        uint8_t firstPresetIdx = 5; // 3 cfg + 2 add/del buttons
        
        if (_focusIdx == backIdx) {
            popScreen();
        } else if (_focusIdx == 3) {
            // +Add preset
            if (_pipeline) {
                auto& isf = (nav.module == DisplayModuleID::ISF1)
                            ? _pipeline->getIsf1() : _pipeline->getIsf2();
                if (isf.getNumPresets() < ISF_MAX_PRESETS) {
                    isf.setNumPresets(isf.getNumPresets() + 1);
                }
            }
        } else if (_focusIdx == 4) {
            // -Del preset
            if (_pipeline) {
                auto& isf = (nav.module == DisplayModuleID::ISF1)
                            ? _pipeline->getIsf1() : _pipeline->getIsf2();
                if (isf.getNumPresets() > 1) {
                    isf.setNumPresets(isf.getNumPresets() - 1);
                }
            }
        } else if (_focusIdx >= firstPresetIdx) {
            // Preset row: enter its EQ edit screen
            uint8_t presetIdx = _focusIdx - firstPresetIdx;
            _isfEditPreset = presetIdx;
            // Push EQ common for this ISF preset
            pushScreen(ScreenID::EFFECT_EQ_COMMON, nav.module, presetIdx);
        } else if (_focusIdx < 3) {
            // Cfg param (0=RMS Window, 1=Slew Time, 2=Lookahead): push keyboard
            _kbContext = nav;
            _kbParamIdx = _focusIdx;
            
            UiParam params[3];
            getScreenParams(nav, params);
            
            float curVal = getParamValue(nav, _focusIdx);
            static float tempVal; // temp storage for keyboard target
            tempVal = curVal;
            
            pushKeyboard(&tempVal, 
                        params[_focusIdx].minVal, 
                        params[_focusIdx].maxVal,
                        false, // integer only for these params
                        ScreenID::EFFECT_ISF_COMMON);
        }
        break;
    }

    // ── DRC config ────────────────────────────────────────────────────────────
    case ScreenID::DRC_CONFIG: {
        uint8_t cnt = getItemCount();
        if (_focusIdx == cnt - 1) {
            popScreen(); // BACK
        } else if (_focusIdx == cnt - 2) {
            pushScreen(ScreenID::DRC_BAND_SELECT, nav.module); // NEXT to band select
        } else if (_focusIdx == 0 || _focusIdx == 1) {
            toggleEditMode(); // Mode/CF type selector
        } else {
            // Crossover freq/Q params: push keyboard
            _kbContext = nav;
            _kbParamIdx = _focusIdx - 2;
            static float tempVal;
            
            // Read actual value from DRC object
            if (_pipeline) {
                DRC& drc = _pipeline->getDrc();
                if (_kbParamIdx == 0) tempVal = (float)drc._fc[0];
                else if (_kbParamIdx == 1) tempVal = Q_Q610_TO_FLOAT(drc._qLp);
                else if (_kbParamIdx == 2) tempVal = (float)drc._fc[1];
                else if (_kbParamIdx == 3) tempVal = Q_Q610_TO_FLOAT(drc._qHp);
                else tempVal = 0.0f;
            } else {
                tempVal = 0.0f;
            }
            
            bool isQ = (_kbParamIdx == 1 || _kbParamIdx == 3);
            float minV = isQ ? 0.1f : 20.0f;
            float maxV = isQ ? 4.0f : 20000.0f;
            
            pushKeyboard(&tempVal, minV, maxV, isQ, ScreenID::DRC_CONFIG);
        }
        break;
    }

    // ── DRC band select ───────────────────────────────────────────────────────
    case ScreenID::DRC_BAND_SELECT:
        if (_focusIdx == getItemCount() - 1) {
            popScreen();
        } else {
            _drcActiveBand = _focusIdx;
            pushScreen(ScreenID::DRC_BAND_PARAMS, nav.module);
        }
        break;

    // ── DRC band params ───────────────────────────────────────────────────────
    case ScreenID::DRC_BAND_PARAMS: {
        uint8_t cnt = getItemCount();
        if (_focusIdx == cnt - 1) {
            popScreen();
        } else if (_focusIdx < 6) {
            // Param rows 0-5: toggle edit mode
            toggleEditMode();
        }
        break;
    }

    // ── Keyboard ──────────────────────────────────────────────────────────────
    case ScreenID::KEYBOARD: {
        // Handle key press for currently focused key
        if (_kb.cursorRow < KB_NUM_ROWS) {
            // Numeric key (0-9, ., -)
            char key = KB_ROWS[_kb.cursorRow][_kb.cursorCol];
            if (_kb.len < KB_MAX_LEN) {
                if (key == '.' && !_kb.allowDot) break; // no dot for integers
                _kb.buf[_kb.len++] = key;
                _kb.buf[_kb.len]   = '\0';
            }
        } else {
            // Action key (DEL, CANCEL, ENTER)
            const char* action = KB_ACTIONS[_kb.cursorCol];
            if (strcmp(action, "DEL") == 0) {
                if (_kb.len > 0) _kb.len--;
                _kb.buf[_kb.len] = '\0';
            } else if (strcmp(action, "CANCEL") == 0) {
                popScreen();
            } else if (strcmp(action, "ENTER") == 0) {
                // Parse and apply value
                float val = atof(_kb.buf);
                val = constrain(val, _kb.minVal, _kb.maxVal);
                DBG_PRINTLN(val);
                
                // Write back to target (temp storage)
                if (_kb.target) *_kb.target = val;
                
                // Apply via setParamValue if we have context
                if (_kb.returnScreen == ScreenID::EFFECT_ISF_COMMON) {
                    setParamValue(_kbContext, _kbParamIdx, val, nullptr);
                } else if (_kb.returnScreen == ScreenID::DRC_CONFIG && _pipeline) {
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

    default:
        break;
    }
}

// ─── Edit delta ───────────────────────────────────────────────────────────────
void Display::editDelta(int8_t dir) {
    ScreenID  s   = currentScreen();
    NavEntry& nav = currentNav();

    // ── MAIN_MENU: edit vol/bass/mid/treble directly ───────────────────────────
    if (s == ScreenID::MAIN_MENU && _focusIdx < 4 && _pipeline) {
        float oldValue = 0.0f, newValue = 0.0f;
        
        if (_focusIdx == 0) {
            VolumeControl& pre = _pipeline->getPreGain();

            oldValue = (float)_mmparam.vol;
            _mmparam.vol += dir;
            _mmparam.vol = constrain(_mmparam.vol, 0, 32);
            newValue = (float)_mmparam.vol;

            float db = -32.0f + _mmparam.vol * 32.0f / 32.0f;
            pre.setGainDb(FLOAT_TO_DB_Q8(db));
            pre.setMute(_mmparam.vol <= 0);
        } else if (_focusIdx == 1) { // Bass
            ParametricEQ& pre = _pipeline->getPreEq();

            float oldBass = ((float)_mmparam.bass / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam;
            _mmparam.bass += dir;
            _mmparam.bass = constrain(_mmparam.bass, 0, 100);
            float newBass = ((float)_mmparam.bass / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam;

            oldValue = oldBass;
            newValue = newBass;

            EQFilterParams p = pre.getBandParams(0);
            p.gain = FLOAT_TO_DB_Q8(newBass);
            pre.setBand(0, p);
        } else if (_focusIdx == 2) { // Mid
            ParametricEQ& pre = _pipeline->getPreEq();

            float oldMid = ((float)_mmparam.mid / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam;
            _mmparam.mid += dir;
            _mmparam.mid = constrain(_mmparam.mid, 0, 100);
            float newMid = ((float)_mmparam.mid / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam;

            oldValue = oldMid;
            newValue = newMid;

            EQFilterParams p = pre.getBandParams(1);
            p.gain = FLOAT_TO_DB_Q8(newMid);
            pre.setBand(1, p);
        } else if (_focusIdx == 3) { // Treble
            ParametricEQ& pre = _pipeline->getPreEq();

            float oldTreble = ((float)_mmparam.treble / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam;
            _mmparam.treble += dir;
            _mmparam.treble = constrain(_mmparam.treble, 0, 100);
            float newTreble = ((float)_mmparam.treble / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam;

            oldValue = oldTreble;
            newValue = newTreble;

            EQFilterParams p = pre.getBandParams(2);
            p.gain = FLOAT_TO_DB_Q8(newTreble);
            pre.setBand(2, p);
        }
        
        // Trigger value animation
        startValueAnimation(oldValue, newValue);
        
        // Reset auto-save timer — save after 3s idle
        _mainMenuLastEditMs = millis();
        _mainMenuAutosaveArmed = true;
        
        _dirty = true;
        return;
    }

    // ── EQ_BAND_EDIT: filter type cycling ─────────────────────────────────────
    if (s == ScreenID::EQ_BAND_EDIT && _focusIdx == 0) {
        ParametricEQ* eq = getEqPtr(nav.module);
        uint8_t bandIdx = nav.subContext;
        EQFilterType oldType = _bandEditType;

        int8_t t = (int8_t)_bandEditType + dir;
        // Skip ORDER1 variants (indices COUNT-2 and COUNT-1)
        if (t < 0) t = (int8_t)EQFilterType::EQ_FILTER_TYPE_COUNT - 3;
        if (t >= (int8_t)EQFilterType::EQ_FILTER_TYPE_COUNT - 2) t = 0;
        _bandEditType = (EQFilterType)t;

        // Keep focus valid when gain row appears/disappears
        bool oldGain = EQ_FILTER_HAS_GAIN[(uint8_t)oldType];
        bool newGain = EQ_FILTER_HAS_GAIN[(uint8_t)_bandEditType];
        if (oldGain && !newGain && _focusIdx > 1) _focusIdx--;
        else if (!oldGain && newGain && _focusIdx > 0) _focusIdx++;
        _itemCount = getItemCount();
        if (_focusIdx >= _itemCount) _focusIdx = _itemCount - 1;

        if (eq && bandIdx < MAX_EQ_BANDS) {
            EQFilterParams p = eq->getBandParams(bandIdx);
            p.type = (int16_t)_bandEditType;
            eq->setBand(bandIdx, p);
        }
        _dirty = true;
        return;
    }

    // ── KEYBOARD: navigate keys ───────────────────────────────────────────────
    if (s == ScreenID::KEYBOARD) {
        // CW/CCW moves cursor in keyboard grid
        int8_t col = _kb.cursorCol;
        int8_t row = _kb.cursorRow;
        
        if (dir > 0) {
            col++;
            if (row < KB_NUM_ROWS) {
                if (col >= KB_COLS) { col = 0; row++; }
            } else {
                if (col >= KB_ACT_COLS) { col = 0; row = 0; }
            }
        } else {
            col--;
            if (col < 0) {
                if (row > 0) { row--; col = (row < KB_NUM_ROWS) ? KB_COLS - 1 : KB_ACT_COLS - 1; }
                else { row = KB_NUM_ROWS; col = KB_ACT_COLS - 1; }
            }
        }
        
        // Clamp row
        if (row < 0) row = KB_NUM_ROWS;
        if (row > KB_NUM_ROWS) row = 0;
        
        _kb.cursorCol = col;
        _kb.cursorRow = row;
        _dirty = true;
        return;
    }

    // ── EQ_BAND_EDIT: freq / gain / Q sliders ─────────────────────────────────
    if (s == ScreenID::EQ_BAND_EDIT) {
        ParametricEQ* eq = getEqPtr(nav.module);
        if (!eq) return;
        uint8_t bandIdx = nav.subContext;
        EQFilterParams p = eq->getBandParams(bandIdx);
        
        bool hasGain = EQ_FILTER_HAS_GAIN[(uint8_t)_bandEditType];
        uint8_t qItem = hasGain ? 3 : 2;

        if (_focusIdx == 1) {
            // Freq: step 10 Hz (faster)
            float newF = constrain((float)p.f0 + dir * 10.0f, 20.0f, 20000.0f);
            p.f0    = (uint16_t)newF;
            p.type  = (int16_t)_bandEditType;
            eq->setBand(bandIdx, p);
            _dirty = true;
        } else if (hasGain && _focusIdx == 2) {
            // Gain: step 0.5 dB
            float curGain = DB_Q8_TO_FLOAT(p.gain);
            float newGain = constrain(curGain + dir * 0.5f, -24.0f, 24.0f);
            p.gain  = FLOAT_TO_DB_Q8(newGain);
            p.type  = (int16_t)_bandEditType;
            eq->setBand(bandIdx, p);
            _dirty = true;
        } else if (_focusIdx == qItem) {
            // Q: step 0.1
            float curQ  = Q_Q610_TO_FLOAT(p.Q);
            float newQ  = constrain(curQ + dir * 0.1f, 0.1f, 10.0f);
            p.Q    = FLOAT_TO_Q_Q610(newQ);
            p.type = (int16_t)_bandEditType;
            eq->setBand(bandIdx, p);
            _dirty = true;
        }
        return;
    }

    // ── EFFECT_EQ_COMMON: pregain or band navigate ─────────────────────────────
    if (s == ScreenID::EFFECT_EQ_COMMON) {
        ParametricEQ* eq = getEqPtr(nav.module);
        if (_focusIdx == 0 && eq) {
            // Pregain: step 1 dB
            float cur = DB_Q8_TO_FLOAT((int16_t)(eq->getPregain()));
            float nxt = constrain(cur + dir * 1.0f, -24.0f, 24.0f);
            eq->setPregain((int16_t)FLOAT_TO_DB_Q8(nxt));
            _dirty = true;
        }
        // Band rows: toggle via encoder press in handleEncoder
        return;
    }

    // ── DRC_CONFIG: Mode/CF Type selectors ────────────────────────────────────
    if (s == ScreenID::DRC_CONFIG && _pipeline && _editMode) {
        DRC& drc = _pipeline->getDrc();
        if (_focusIdx == 0) {
            // Mode selector
            int8_t m = (int8_t)drc._mode + dir;
            if (m < 0) m = 4;
            if (m > 4) m = 0;
            drc.setMode((DRCMode)m);
            _dirty = true;
            return;
        } else if (_focusIdx == 1) {
            // CF Type selector
            int8_t t = (int8_t)drc._cfType + dir;
            if (t < 2) t = 4;
            if (t > 4) t = 2;
            drc.setCrossoverType((DRCCrossoverType)t);
            _dirty = true;
            return;
        }
    }

    // ── EFFECT_COMMON / DRC_BAND_PARAMS: generic param editing ─────────────
    if (s == ScreenID::EFFECT_COMMON || s == ScreenID::DRC_BAND_PARAMS) {
        if (s == ScreenID::EFFECT_COMMON) {
            uint8_t paramCount = getItemCount() - 1; // exclude BACK
            if (_focusIdx >= paramCount) return;

            UiParam params[16];
            uint8_t n = getScreenParams(nav, params);
            if (_focusIdx >= n) return;

            // Skip attack/release editing when lookahead > 1
            bool skipParam = false;
            if (nav.module == DisplayModuleID::COMPANDER && _pipeline) {
                float lookahead = getParamValue(nav, 5);
                if (lookahead > 0 && (_focusIdx == 3 || _focusIdx == 4)) {
                    skipParam = true;
                }
            } else if (nav.module == DisplayModuleID::DYNAMIC_BASS && _pipeline) {
                float lookahead = getParamValue(nav, 8);
                if (lookahead > 0 && (_focusIdx == 6 || _focusIdx == 7)) {
                    skipParam = true;
                }
            } else if (nav.module == DisplayModuleID::DYNAMIC_EQ_THRESH && _pipeline) {
                float lookahead = getParamValue(nav, 5);
                if (lookahead > 0 && (_focusIdx == 3 || _focusIdx == 4)) {
                    skipParam = true;
                }
            }
            
            if (skipParam) return;

            float cur = getParamValue(nav, _focusIdx);
            float nxt = constrain(cur + dir * params[_focusIdx].step,
                                  params[_focusIdx].minVal,
                                  params[_focusIdx].maxVal);
            setParamValue(nav, _focusIdx, nxt, params);
            _dirty = true;
        }
        if (s == ScreenID::DRC_BAND_PARAMS && _pipeline) {
            if (_focusIdx >= 6) return; // BACK button only now
            uint8_t pi = _focusIdx;
            DRC& drc = _pipeline->getDrc();
            uint8_t band = _drcActiveBand;

            const UiParam drcParams[] = {
                { "Pregain",   "dB",  -24.0f, 24.0f,   1.0f,  0 },
                { "Threshold", "dB",  -60.0f,  0.0f,   0.5f,  1 },
                { "Ratio",     ":1",    1.0f, 100.0f,  1.0f,  0 },
                { "Attack",    "ms",    1.0f, 2000.0f, 10.0f, 0 },
                { "Release",   "ms",    1.0f, 2000.0f, 10.0f, 0 },
                { "Lookahead", "ms",    0.0f,  10.0f,  1.0f,  0 },
            };

            float cur = 0.0f;
            if (pi == 0) {
                // Pregain: Q4.12 → linear → dB
                float pregainLinear = PREGAIN_Q412_TO_FLOAT(drc._bands[band].pregainQ412);
                cur = (pregainLinear > 0.0001f) ? 20.0f * log10f(pregainLinear) : -96.0f;
            }
            else if (pi == 1) cur = DRC_TH_TO_FLOAT_DB(drc._bands[band].thresholdDbInt);
            else if (pi == 2) cur = (float)drc._bands[band].ratioX100 / 100.0f;
            else if (pi == 3) cur = (float)drc._bands[band].attackMs;
            else if (pi == 4) cur = (float)drc._bands[band].releaseMs;
            else if (pi == 5) cur = drc._bands[band].lookaheadMs;

            float nxt = constrain(cur + dir * drcParams[pi].step,
                                  drcParams[pi].minVal, drcParams[pi].maxVal);

            if (pi == 0) {
                // dB → linear → Q4.12
                nxt = roundf(nxt); // Round to integer dB
                float linearGain = powf(10.0f, nxt / 20.0f);
                drc.setPregain(band, FLOAT_TO_PREGAIN_Q412(linearGain));
            }
            else if (pi == 1) drc.setThreshold(band, FLOAT_DB_TO_DRC_TH(nxt));
            else if (pi == 2) drc.setRatio(band, (int32_t)(nxt * 100.0f));
            else if (pi == 3) drc.setAttackTime(band, (int32_t)nxt);
            else if (pi == 4) drc.setReleaseTime(band, (int32_t)nxt);
            else if (pi == 5) drc.setLookahead(band, nxt);
            _dirty = true;
        }
        return;
    }

    // ── SETTINGS: toggle value with CW/CCW ────────────────────────────────────
    if (s == ScreenID::SETTINGS && _editMode) {
        // Settings items are switches — any rotation toggles
        // TODO: bind to real globals (g_wifiShutdownActive, trigger GPIO)
        // For now, just exit edit mode when user rotates (placeholder behavior)
        _editMode = false;
        _dirty = true;
        return;
    }

    (void)dir;
}

// ─── Item counts ──────────────────────────────────────────────────────────────
uint8_t Display::getItemCount() const {
    switch (currentScreen()) {
    case ScreenID::SPLASH:    return 0;
    case ScreenID::MAIN_MENU: return 4;   // vol,bass,mid,treble only (no buttons)
    case ScreenID::SETTINGS:  return 4;   // wifi,trigger,[SHUTDOWN],[BACK]
    case ScreenID::DSP_LIST:  return PRESET_SLOT_COUNT + 1 + MODULE_COUNT + 1; // slots + [SAVE PRESET] + modules + [BACK]

    case ScreenID::EFFECT_COMMON: {
        // Dynamic: param count + 1 (BACK)
        NavEntry nav = _navStack[_navTop];
        uint8_t n = const_cast<Display*>(this)->getScreenParams(nav, nullptr);
        return (n > 0) ? n + 1 : 8; // fallback 7 params + BACK
    }

    case ScreenID::EFFECT_EQ_COMMON:
        // pregain(1) + fixed band slots(MAX_EQ_BANDS) + GRAPH + BACK
        return 1 + MAX_EQ_BANDS + 2;

    case ScreenID::EFFECT_EQ_GRAPH:
        // one focus item per fixed slot + [BACK]
        return MAX_EQ_BANDS + 1;

    case ScreenID::EQ_BAND_EDIT:
        // Type(0), Freq(1), [Gain(2)], Q(2or3), BACK(last)
        return EQ_FILTER_HAS_GAIN[(uint8_t)_bandEditType] ? 5 : 4;

    case ScreenID::EFFECT_ISF_COMMON: {
        // 3 cfg params + +Add + -Del + preset rows + BACK
        uint8_t nPresets = 0;
        if (_pipeline) {
            auto& isf = (_navStack[_navTop].module == DisplayModuleID::ISF1)
                        ? _pipeline->getIsf1() : _pipeline->getIsf2();
            nPresets = isf.getNumPresets();
        }
        return 3 + 2 + nPresets + 1; // cfg + add/del + presets + BACK
    }

    case ScreenID::DRC_CONFIG:
        // Mode + CF Type + 4 crossover params (freq1,q1,freq2,q2) + NEXT + BACK
        return 2 + 4 + 2;

    case ScreenID::DRC_BAND_SELECT: {
        // Dynamic band count based on mode + BACK
        if (!_pipeline) return 1;
        DRC& drc = _pipeline->getDrc();
        uint8_t bandCount = 1;
        switch (drc._mode) {
            case DRC_MODE_FULLBAND: bandCount = 1; break;
            case DRC_MODE_2BAND: bandCount = 2; break;
            case DRC_MODE_2BAND_FULLBAND: bandCount = 3; break;
            case DRC_MODE_3BAND: bandCount = 3; break;
            case DRC_MODE_3BAND_FULLBAND: bandCount = 4; break;
        }
        return bandCount + 1;
    }

    case ScreenID::DRC_BAND_PARAMS:
        // 6 per-band params + BACK
        return 6 + 1;
    case ScreenID::KEYBOARD:         return KB_NUM_ROWS * KB_COLS + KB_ACT_COLS;
    default:                         return 0;
    }
}

// ─── Master draw ──────────────────────────────────────────────────────────────
void Display::draw() {
    TFT_eSprite& d = _spr;
    if (!_sprReady) return;

    d.fillSprite(Color::BG);

    switch (currentScreen()) {
    case ScreenID::SPLASH:             drawSplash();           break;
    case ScreenID::MAIN_MENU:          drawMainMenu();         break;
    case ScreenID::SETTINGS:           drawSettings();         break;
    case ScreenID::DSP_LIST:           drawDspList();          break;
    case ScreenID::EFFECT_COMMON:      drawEffectCommon();     break;
    case ScreenID::EFFECT_EQ_COMMON:   drawEffectEqCommon();   break;
    case ScreenID::EFFECT_EQ_GRAPH:    drawEffectEqGraph();    break;
    case ScreenID::EQ_BAND_EDIT:       drawEqBandEdit();       break;
    case ScreenID::EFFECT_ISF_COMMON:  drawEffectIsfCommon();  break;
    case ScreenID::DRC_CONFIG:         drawDrcConfig();        break;
    case ScreenID::DRC_BAND_SELECT:    drawDrcBandSelect();    break;
    case ScreenID::DRC_BAND_PARAMS:    drawDrcBandParams();    break;
    case ScreenID::KEYBOARD:           drawKeyboard();         break;
    default: break;
    }

    d.pushSprite(0, 0);
}

// ─── Splash screen ────────────────────────────────────────────────────────────
void Display::drawSplash() {
    TFT_eSprite& d = _spr;

    // Background gradient (horizontal bands)
    for (int16_t y = 0; y < DISP_H; y++) {
        uint16_t c = blendColor(Color::BG, Color::PANEL,
                                (uint8_t)(y * 255 / DISP_H));
        d.drawFastHLine(0, y, DISP_W, c);
    }

    // Title
    d.setTextDatum(MC_DATUM);
    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextSize(3);
    d.drawString("DSP CORE", DISP_W / 2, DISP_H / 2 - 30);

    d.setTextColor(Color::TEXT, Color::BG);
    d.setTextSize(1);
    d.drawString("v" FIRMWARE_VERSION, DISP_W / 2, DISP_H / 2 + 4);
    d.drawString("by Nagumo", DISP_W / 2, DISP_H / 2 + 18);

    // Loading bar
    uint32_t elapsed = millis() - _splashStartMs;
    float    frac    = (float)elapsed / SPLASH_DURATION_MS;
    if (frac > 1.0f) frac = 1.0f;
    int16_t barW = (int16_t)(frac * 200.0f);
    int16_t barX = (DISP_W - 200) / 2;
    int16_t barY = DISP_H / 2 + 40;
    d.drawRect(barX, barY, 200, 6, Color::BORDER);
    d.fillRect(barX + 1, barY + 1, barW - 2, 4, Color::ACCENT);

    _dirty = true; // keep redrawing until done
}

// ─── Main menu ────────────────────────────────────────────────────────────────
void Display::drawMainMenu() {
    TFT_eSprite& d = _spr;
    // Layout: 0=Vol, 1=Bass, 2=Mid, 3=Treble
    // Auto-save is handled from update() when user stops turning encoder.

    // ── Left sidebar ──────────────────────────────────────────────────────────
    drawSideBars(SIDEBAR_X, HEADER_H + 4, DISP_H - HEADER_H - 40);
    drawWifiBadge(SIDEBAR_X, DISP_H - 38);

    // ── Sample rate ───────────────────────────────────────────────────────────
    d.setTextDatum(ML_DATUM);
    d.setTextColor(Color::TEXT_DIM, Color::BG);
    d.setTextSize(1);
    char srBuf[12];
    if (_sampleRate > 0)
        snprintf(srBuf, sizeof(srBuf), "%lukHz", (unsigned long)(_sampleRate / 1000));
    else
        snprintf(srBuf, sizeof(srBuf), "--");
    d.drawString(srBuf, SIDEBAR_X, DISP_H - 10);

    // ── Header ────────────────────────────────────────────────────────────────
    d.setTextDatum(ML_DATUM);
    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextSize(1);
    d.drawString("ESP32 DSP CORE", CONTENT_X, 10);
    d.drawFastHLine(CONTENT_X, HEADER_H, CONTENT_W, Color::BORDER);

    // ── Volume / tone params with animations ─────────────────────────────────
    const char* labels[] = { "Vol", "Bass", "Mid", "Treble" };
    float       values[] = {
        (float)_mmparam.vol,
        ((float)_mmparam.bass / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam,
        ((float)_mmparam.mid / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam,
        ((float)_mmparam.treble / 100.0f) * (_maxRangeParam * 2.0f) - _maxRangeParam
    };
    const char* units[]  = { "",    "dB",  "dB",  "dB"   };
    float       mins[]   = { 0.0f, -_maxRangeParam, -_maxRangeParam, -_maxRangeParam };
    float       maxs[]   = { 32.0f, _maxRangeParam, _maxRangeParam, _maxRangeParam };
    uint8_t     decimals[] = { 0, 1, 1, 1 };

    // Animated parameter display
    constexpr int16_t PARAM_AREA_Y = HEADER_H + 8;
    constexpr int16_t PARAM_AREA_H = 140;
    
    // Draw all 4 params as compact indicators
    constexpr int16_t COMPACT_H = 24;
    constexpr int16_t COMPACT_GAP = 6;
    int16_t compactY = PARAM_AREA_Y;
    
    for (uint8_t i = 0; i < 4; i++) {
        bool isFocused = (_focusIdx == i);
        
        // Apply focus transition animation
        float focusBlend = 0.0f;
        if (_anim.focusTransition < 1.0f) {
            if (i == _anim.prevFocusIdx) {
                focusBlend = 1.0f - _anim.focusTransition; // fade out
            } else if (i == _focusIdx) {
                focusBlend = _anim.focusTransition; // fade in
            }
        } else if (isFocused) {
            focusBlend = 1.0f;
        }
        
        // Smooth value animation
        float displayValue = values[i];
        if (_anim.valueAnimating && isFocused) {
            displayValue = getAnimatedValue(displayValue);
        }
        
        // Color interpolation based on focus
        uint16_t bgColor = Color::BG;
        uint16_t borderColor = Color::BORDER;
        uint16_t textColor = Color::TEXT;
        
        if (focusBlend > 0.0f) {
            uint8_t blend = (uint8_t)(focusBlend * 255.0f);
            bgColor = blendColor(Color::BG, Color::PANEL, blend);
            borderColor = blendColor(Color::BORDER, Color::ACCENT, blend);
            textColor = blendColor(Color::TEXT, Color::TEXT_FOCUS, blend);
        }
        
        // Draw compact param indicator
        d.fillRoundRect(CONTENT_X, compactY, CONTENT_W, COMPACT_H, 3, bgColor);
        d.drawRoundRect(CONTENT_X, compactY, CONTENT_W, COMPACT_H, 3, borderColor);
        
        // Apply edit mode sweep if focused
        if (isFocused && _anim.aniSweepActive) {
            drawSweepAnimation(CONTENT_X, compactY, CONTENT_W, COMPACT_H);
        }
        
        // Label
        d.setTextColor(textColor, bgColor);
        d.setTextDatum(ML_DATUM);
        d.setTextSize(1);
        d.drawString(labels[i], CONTENT_X + 8, compactY + COMPACT_H / 2);
        
        // Value
        char valStr[16];
        formatFloat(valStr, sizeof(valStr), displayValue, decimals[i]);
        d.setTextDatum(MR_DATUM);
        
        if (isFocused) {
            // Focused item: larger, bold value
            //d.setFreeFont(&Century751BT12);
            //d.setTextSize(1);
            d.drawString(String(valStr) + String(units[i]), CONTENT_X + CONTENT_W - 8, compactY + COMPACT_H / 2);
            //d.setTextFont(1);
        } else {
            // Non-focused: smaller value
            d.setTextSize(1);
            d.drawString(String(valStr) + String(units[i]), CONTENT_X + CONTENT_W - 8, compactY + COMPACT_H / 2);
        }
        
        compactY += COMPACT_H + COMPACT_GAP;
    }

    // ── Live Meters Section (compact panel below sliders) ────────────────────
    int16_t meterY = HEADER_H + 4 + 4 * ROW_H + 8;
    int16_t meterAreaH = DISP_H - meterY - 8;
    
    if (meterAreaH > 60 && _pipeline) {
        // Draw meter panel background
        d.fillRoundRect(CONTENT_X, meterY, CONTENT_W, meterAreaH, 4, Color::PANEL);
        d.drawRoundRect(CONTENT_X, meterY, CONTENT_W, meterAreaH, 4, Color::BORDER);
        
        // Panel title
        d.setTextColor(Color::TEXT_DIM, Color::PANEL);
        d.setTextDatum(TC_DATUM);
        d.setTextSize(1);
        d.drawString("LIVE METERS", CONTENT_X + CONTENT_W / 2, meterY + 4);
        
        int16_t rowY = meterY + 18;
        constexpr int16_t rowH = 15;
        constexpr int16_t labelW = 56;
        constexpr int16_t meterW = CONTENT_W - labelW - 16;
        constexpr int16_t labelX = CONTENT_X + 6;
        constexpr int16_t meterX = CONTENT_X + labelW + 4;
        
        // Compander meter (horizontal gain reduction bar)
        Compander& comp = _pipeline->getCompander();
        if (comp.isEnabled()) {
            d.setTextColor(Color::TEXT, Color::PANEL);
            d.setTextDatum(ML_DATUM);
            d.drawString("Comp", labelX, rowY + rowH / 2);
            
            constexpr int16_t mH = 8;
            int16_t mY = rowY + (rowH - mH) / 2;
            d.drawRect(meterX, mY, meterW, mH, Color::BORDER);
            
            float gr = -_companderGainDb;
            if (gr < 0.0f) gr = 0.0f;
            float frac = constrain(gr / 30.0f, 0.0f, 1.0f);
            int16_t fillW = (int16_t)(frac * (meterW - 2));
            
            uint16_t col = gr > 20.0f ? Color::RED : gr > 12.0f ? Color::YELLOW : Color::GREEN;
            if (fillW > 0) {
                d.fillRect(meterX + 1, mY + 1, fillW, mH - 2, col);
            }
        }
        rowY += rowH;
        
        // Dynamic Bass meter (bi-directional alpha bar)
        DynamicBass& dynBass = _pipeline->getDynamicBass();
        if (dynBass.isEnabled()) {
            d.setTextColor(Color::TEXT, Color::PANEL);
            d.setTextDatum(ML_DATUM);
            d.drawString("D.Bass", labelX, rowY + rowH / 2);
            
            constexpr int16_t mH = 8;
            int16_t mY = rowY + (rowH - mH) / 2;
            d.drawRect(meterX, mY, meterW, mH, Color::BORDER);
            
            int16_t centerX = meterX + meterW / 2;
            d.drawFastVLine(centerX, mY, mH, Color::TEXT_DIM);
            
            float alpha = constrain(_dynBassAlpha, -1.0f, 1.0f);
            if (alpha < 0.0f) {
                int16_t fillW = (int16_t)((-alpha) * (meterW / 2 - 2));
                if (fillW > 0) d.fillRect(centerX - fillW, mY + 1, fillW, mH - 2, Color::RED);
            } else if (alpha > 0.0f) {
                int16_t fillW = (int16_t)(alpha * (meterW / 2 - 2));
                if (fillW > 0) d.fillRect(centerX + 1, mY + 1, fillW, mH - 2, Color::GREEN);
            }
        }
        rowY += rowH;
        
        // Dynamic EQ meter (dual stacked bars for Low/High)
        DynamicEQ& dynEq = _pipeline->getDynamicEq();
        if (dynEq.isEnabled()) {
            d.setTextColor(Color::TEXT, Color::PANEL);
            d.setTextDatum(ML_DATUM);
            d.drawString("D.EQ", labelX, rowY + rowH / 2);
            
            constexpr int16_t barH = 4;
            constexpr int16_t gap = 1;
            int16_t mY = rowY + (rowH - barH * 2 - gap) / 2;
            
            // Low EQ bar (top, orange)
            d.drawRect(meterX, mY, meterW, barH, Color::BORDER);
            float alphaLow = constrain(_dynEqAlphaLow, 0.0f, 1.0f);
            int16_t fillWLow = (int16_t)(alphaLow * (meterW - 2));
            if (fillWLow > 0) d.fillRect(meterX + 1, mY + 1, fillWLow, barH - 2, Color::ACCENT2);
            
            // High EQ bar (bottom, cyan)
            d.drawRect(meterX, mY + barH + gap, meterW, barH, Color::BORDER);
            float alphaHigh = constrain(_dynEqAlphaHigh, 0.0f, 1.0f);
            int16_t fillWHigh = (int16_t)(alphaHigh * (meterW - 2));
            if (fillWHigh > 0) d.fillRect(meterX + 1, mY + barH + gap + 1, fillWHigh, barH - 2, Color::ACCENT);
        }
        rowY += rowH;
        
        // DRC meter (4 vertical band bars)
        DRC& drc = _pipeline->getDrc();
        if (drc.isEnabled()) {
            d.setTextColor(Color::TEXT, Color::PANEL);
            d.setTextDatum(ML_DATUM);
            d.drawString("DRC", labelX, rowY + rowH / 2);
            
            constexpr int16_t vBarH = 12;
            int16_t mY = rowY + (rowH - vBarH) / 2;
            int16_t barW = (meterW / 4) - 2;
            
            for (uint8_t b = 0; b < 4; b++) {
                int16_t bx = meterX + b * (barW + 2);
                d.drawRect(bx, mY, barW, vBarH, Color::BORDER);
                
                float gr = -_drcGainDb[b];
                if (gr < 0.0f) gr = 0.0f;
                float frac = constrain(gr / 30.0f, 0.0f, 1.0f);
                int16_t fillH = (int16_t)(frac * (vBarH - 2));
                
                uint16_t col = gr > 20.0f ? Color::RED : gr > 12.0f ? Color::YELLOW : Color::GREEN;
                if (fillH > 0) {
                    d.fillRect(bx + 1, mY + vBarH - 1 - fillH, barW - 2, fillH, col);
                }
            }
        }
    }

    // Note: No SETTINGS/DSP LIST buttons on main menu anymore — use encoder actions:
    //   - Double-click → DSP LIST
    //   - Hold 3s      → SETTINGS
}

// ─── Settings ────────────────────────────────────────────────────────────────
void Display::drawSettings() {
    TFT_eSprite& d = _spr;

    // Header
    d.setTextDatum(ML_DATUM);
    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextSize(1);
    d.drawString("SETTINGS", CONTENT_X, 10);
    d.drawFastHLine(CONTENT_X, HEADER_H, CONTENT_W, Color::BORDER);

    // Items
    struct { const char* name; bool active; } items[] = {
        { "WiFi",     !false },   // TODO: bind to g_wifiShutdownActive
        { "Trigger",  false  },   // TODO: bind to trigger GPIO state
        { "Shutdown", false  },   // action
    };

    for (uint8_t i = 0; i < 3; i++) {
        int16_t y = HEADER_H + 4 + i * ROW_H;
        bool focused = (_focusIdx == i);
        if (i < 2) {
            drawSwitchRow(CONTENT_X, y, CONTENT_W,
                          items[i].name, items[i].active, focused);
        } else {
            // Shutdown action row
            drawNavButton(CONTENT_X, y, CONTENT_W, ROW_H - 2,
                          "SHUTDOWN (hold 5s in main)",
                          focused, false, 0.0f);
        }
    }

    // Back
    float holdFrac = (_holdTracking && _focusIdx == 3)
        ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS : 0.0f;
    drawNavButton(CONTENT_X, FOOTER_Y, CONTENT_W, NAV_BTN_H,
                  "BACK", _focusIdx == 3,
                  _holdTracking && _focusIdx == 3, holdFrac);
}

// ─── DSP list ────────────────────────────────────────────────────────────────
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

    // Row 1: 3 horizontal preset boxes (with focus transition animation)
    constexpr int16_t presetBoxW = (CONTENT_W - 12) / PRESET_SLOT_COUNT; // 3 boxes with 4px gaps
    constexpr int16_t presetBoxH = 32;
    
    for (uint8_t i = 0; i < PRESET_SLOT_COUNT; i++) {
        int16_t x = CONTENT_X + i * (presetBoxW + 4);
        bool focused = (_focusIdx == i);
        bool has = _presetMgr ? _presetMgr->hasPreset(i) : false;
        bool isTarget = (i == _presetTargetSlot);

        // Focus transition animation
        uint16_t bg, border, textColor;
        if (focused) {
            if (_anim.focusTransition < 1.0f) {
                uint8_t blend = (uint8_t)(_anim.focusTransition * 255.0f);
                bg = blendColor(Color::BG, Color::PANEL, blend);
                border = blendColor(Color::BORDER, Color::ACCENT, blend);
            } else {
                bg = Color::PANEL;
                border = Color::ACCENT;
            }
        } else {
            bg = Color::BG;
            border = Color::BORDER;
        }
        
        d.fillRect(x, yPos, presetBoxW, presetBoxH, bg);
        d.drawRect(x, yPos, presetBoxW, presetBoxH, border);

        if (isTarget && _anim.aniSweepActive) {
            // Sweep animation will overwrite background
            drawSweepAnimation(x, yPos, presetBoxW, presetBoxH);
            // After sweep, force text color to contrast with accent
            textColor = Color::ACCENT;
            bg = Color::BG; // for text background
        } else if (_anim.aniSweepActive && _anim.aniSweepExit) {
            // Sweep animation will overwrite background
            drawSweepAnimation(x, yPos, presetBoxW, presetBoxH);
            // After sweep, force text color to contrast with accent
            textColor = Color::ACCENT;
            bg = Color::ACCENT; // for text background
        }

        // Preset label
        char label[8];
        snprintf(label, sizeof(label), "P%u", (unsigned)(i + 1));
        d.setTextColor(isTarget ? textColor : focused ? Color::TEXT_FOCUS : Color::TEXT, bg);
        d.setTextDatum(MC_DATUM);
        d.drawString(label, x + presetBoxW / 2, yPos + presetBoxH / 2 - 4);

        // Status dot
        /*
        uint16_t dot = isTarget ? Color::YELLOW : (has ? Color::GREEN : Color::RED);
        d.fillCircle(x + presetBoxW / 2, yPos + presetBoxH - 8, 3, dot);
        */
    }
    yPos += presetBoxH + 6;

    // Row 2: SAVE PRESET button (with focus transition animation)
    constexpr int16_t saveW = 180;
    constexpr int16_t saveH = 28;
    int16_t saveX = CONTENT_X + (CONTENT_W - saveW) / 2;
    bool saveFocused = (_focusIdx == DSP_LIST_PRESET_SAVE_IDX);

    uint16_t saveBg, saveBorder;
    if (saveFocused) {
        if (_anim.focusTransition < 1.0f) {
            uint8_t blend = (uint8_t)(_anim.focusTransition * 255.0f);
            saveBg = blendColor(Color::BG, Color::PANEL, blend);
            saveBorder = blendColor(Color::BORDER, Color::ACCENT, blend);
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

    char saveLabel[24];
    snprintf(saveLabel, sizeof(saveLabel), "SAVE PRESET %u", (unsigned)(_presetTargetSlot + 1));
    d.setTextColor(saveFocused ? Color::TEXT_FOCUS : Color::TEXT, saveBg);
    d.setTextDatum(MC_DATUM);
    d.drawString(saveLabel, saveX + saveW / 2, yPos + saveH / 2);
    yPos += saveH + 8;

    // Modules: scrollable vertical list
    uint8_t moduleStartRow = PRESET_SLOT_COUNT + 1; // First module list index
    constexpr uint8_t visibleModuleRows = 4;
    
    uint8_t firstModule = 0;
    if (_focusIdx >= DSP_LIST_MODULE_START_IDX && _focusIdx < backIdx) {
        uint8_t moduleIdx = _focusIdx - DSP_LIST_MODULE_START_IDX;
        if (moduleIdx >= visibleModuleRows - 1) {
            firstModule = moduleIdx - (visibleModuleRows - 2);
        }
    }

    for (uint8_t i = 0; i < visibleModuleRows && (firstModule + i) < MODULE_COUNT; i++) {
        uint8_t mi = firstModule + i;
        uint8_t itemIdx = DSP_LIST_MODULE_START_IDX + mi;
        bool focused = (_focusIdx == itemIdx);

        // Focus transition animation
        uint16_t bg, border;
        if (focused) {
            if (_anim.focusTransition < 1.0f) {
                uint8_t blend = (uint8_t)(_anim.focusTransition * 255.0f);
                bg = blendColor(Color::BG, Color::PANEL, blend);
                border = blendColor(Color::BORDER, Color::ACCENT, blend);
            } else {
                bg = Color::PANEL;
                border = Color::ACCENT;
            }
        } else {
            bg = Color::BG;
            border = Color::BORDER;
        }

        d.fillRect(CONTENT_X, yPos, CONTENT_W, ROW_H - 2, bg);
        d.drawRect(CONTENT_X, yPos, CONTENT_W, ROW_H - 2, border);

        d.setTextColor(focused ? Color::TEXT_FOCUS : Color::TEXT, bg);
        d.setTextDatum(ML_DATUM);
        d.drawString(MODULE_LIST[mi].name, CONTENT_X + 6, yPos + (ROW_H - 2) / 2);

        DspModule* mod = getModulePtr(MODULE_LIST[mi].id);
        bool isOn = mod ? mod->isEnabled() : false;

        // ── Live Meter Rendering ──────────────────────────────────────────────
        if (isOn) {
            if (MODULE_LIST[mi].id == DisplayModuleID::COMPANDER) {
                // Compander: Horizontal meter (gain reduction 0 to -30 dB)
                constexpr int16_t mW = 50;
                constexpr int16_t mH = 6;
                int16_t mX = CONTENT_X + CONTENT_W - 75;
                int16_t mY = yPos + (ROW_H - 2) / 2 - mH / 2;
                
                d.drawRect(mX, mY, mW, mH, Color::BORDER);
                
                float gr = -_companderGainDb; // Gain reduction is negative
                if (gr < 0.0f) gr = 0.0f;
                float frac = constrain(gr / 30.0f, 0.0f, 1.0f);
                int16_t fillW = (int16_t)(frac * (mW - 2));
                
                uint16_t meterColor = gr > 20.0f ? Color::RED 
                                    : gr > 12.0f ? Color::YELLOW 
                                    : Color::GREEN;
                if (fillW > 0) {
                    d.fillRect(mX + 1, mY + 1, fillW, mH - 2, meterColor);
                }
            } 
            else if (MODULE_LIST[mi].id == DisplayModuleID::DRC) {
                // DRC: 4 vertical band meters (gain reduction 0 to -30 dB)
                constexpr int16_t mAreaW = 50;
                constexpr int16_t mH = 14;
                int16_t mX = CONTENT_X + CONTENT_W - 75;
                int16_t mY = yPos + (ROW_H - 2) / 2 - mH / 2;
                
                int16_t barW = (mAreaW / 4) - 2;
                for (uint8_t b = 0; b < 4; b++) {
                    int16_t bx = mX + b * (barW + 2);
                    d.drawRect(bx, mY, barW, mH, Color::BORDER);
                    
                    float gr = -_drcGainDb[b]; // Gain reduction is negative
                    if (gr < 0.0f) gr = 0.0f;
                    float frac = constrain(gr / 30.0f, 0.0f, 1.0f);
                    int16_t fillH = (int16_t)(frac * (mH - 2));
                    
                    uint16_t meterColor = gr > 20.0f ? Color::RED 
                                        : gr > 12.0f ? Color::YELLOW 
                                        : Color::GREEN;
                    if (fillH > 0) {
                        d.fillRect(bx + 1, mY + mH - 1 - fillH, barW - 2, fillH, meterColor);
                    }
                }
            }
            else if (MODULE_LIST[mi].id == DisplayModuleID::DYNAMIC_BASS) {
                // Dynamic Bass: Horizontal bar showing alpha (-1 to +1)
                // Center = neutral, left = clip zone (red), right = boost zone (green)
                constexpr int16_t mW = 50;
                constexpr int16_t mH = 8;
                int16_t mX = CONTENT_X + CONTENT_W - 75;
                int16_t mY = yPos + (ROW_H - 2) / 2 - mH / 2;
                
                d.drawRect(mX, mY, mW, mH, Color::BORDER);
                
                // Center line (neutral point at alpha = 0)
                int16_t centerX = mX + mW / 2;
                d.drawFastVLine(centerX, mY, mH, Color::TEXT_DIM);
                
                // Alpha: -1 (full clip protection) to +1 (full boost)
                float alpha = constrain(_dynBassAlpha, -1.0f, 1.0f);
                
                if (alpha < 0.0f) {
                    // Clip protection zone: fill left from center
                    int16_t fillW = (int16_t)((-alpha) * (mW / 2 - 2));
                    if (fillW > 0) {
                        d.fillRect(centerX - fillW, mY + 1, fillW, mH - 2, Color::RED);
                    }
                } else if (alpha > 0.0f) {
                    // Boost zone: fill right from center
                    int16_t fillW = (int16_t)(alpha * (mW / 2 - 2));
                    if (fillW > 0) {
                        d.fillRect(centerX + 1, mY + 1, fillW, mH - 2, Color::GREEN);
                    }
                }
            }
            else if (MODULE_LIST[mi].id == DisplayModuleID::DYNAMIC_EQ_THRESH) {
                // Dynamic EQ: 2 horizontal bars showing Low/High alpha (0..1)
                constexpr int16_t mW = 50;
                constexpr int16_t barH = 4;
                constexpr int16_t gap = 2;
                int16_t mX = CONTENT_X + CONTENT_W - 75;
                int16_t mY = yPos + (ROW_H - 2) / 2 - barH - gap / 2;
                
                // Low EQ alpha bar (top) - warm orange
                d.drawRect(mX, mY, mW, barH, Color::BORDER);
                float alphaLow = constrain(_dynEqAlphaLow, 0.0f, 1.0f);
                int16_t fillWLow = (int16_t)(alphaLow * (mW - 2));
                if (fillWLow > 0) {
                    d.fillRect(mX + 1, mY + 1, fillWLow, barH - 2, Color::ACCENT2);
                }
                
                // High EQ alpha bar (bottom) - bright cyan
                int16_t mY2 = mY + barH + gap;
                d.drawRect(mX, mY2, mW, barH, Color::BORDER);
                float alphaHigh = constrain(_dynEqAlphaHigh, 0.0f, 1.0f);
                int16_t fillWHigh = (int16_t)(alphaHigh * (mW - 2));
                if (fillWHigh > 0) {
                    d.fillRect(mX + 1, mY2 + 1, fillWHigh, barH - 2, Color::ACCENT);
                }
            }
        }

        /*
        uint16_t dot = isOn ? Color::GREEN : Color::RED;
        d.fillCircle(CONTENT_X + CONTENT_W - 10, yPos + (ROW_H - 2) / 2, 4, dot);
        */

        yPos += ROW_H;
    }

    // BACK button at bottom
    int16_t backY = DISP_H - NAV_BTN_H - 4;
    bool backFocused = (_focusIdx == backIdx);
    
    uint16_t backBg = backFocused ? Color::PANEL : Color::BG;
    d.fillRect(CONTENT_X, backY, CONTENT_W, NAV_BTN_H, backBg);
    d.drawRect(CONTENT_X, backY, CONTENT_W, NAV_BTN_H,
               backFocused ? Color::ACCENT : Color::BORDER);
    
    d.setTextColor(backFocused ? Color::TEXT_FOCUS : Color::TEXT, backBg);
    d.setTextDatum(MC_DATUM);
    d.drawString("BACK", CONTENT_X + CONTENT_W / 2, backY + NAV_BTN_H / 2);

    // Scroll indicator for modules
    if (MODULE_COUNT > visibleModuleRows) {
        int16_t moduleAreaY = HEADER_H + 4 + presetBoxH + 6 + saveH + 8;
        int16_t trackH = backY - moduleAreaY - 4;
        int16_t thumbH = trackH * visibleModuleRows / MODULE_COUNT;
        int16_t thumbY = moduleAreaY + trackH * firstModule / MODULE_COUNT;
        d.fillRect(DISP_W - 4, moduleAreaY, 3, trackH, Color::BORDER);
        d.fillRect(DISP_W - 4, thumbY, 3, thumbH, Color::ACCENT);
    }
}

// ─── Effect common ────────────────────────────────────────────────────────────
void Display::drawEffectCommon() {
    TFT_eSprite& d = _spr;
    NavEntry& nav  = currentNav();

    // Module name
    const char* modName = "Effect";
    for (uint8_t i = 0; i < MODULE_COUNT; i++) {
        if (MODULE_LIST[i].id == nav.module) { modName = MODULE_LIST[i].name; break; }
    }

    d.setTextDatum(ML_DATUM);
    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextSize(1);
    d.drawString(modName, CONTENT_X, 10);
    d.drawFastHLine(CONTENT_X, HEADER_H, CONTENT_W, Color::BORDER);

    // Build param list from module
    UiParam params[16];
    uint8_t paramCount = getScreenParams(nav, params);
    constexpr uint8_t VISIBLE = 6;

    for (uint8_t i = 0; i < VISIBLE; i++) {
        uint8_t pi = (uint8_t)(_paramScroll + i);
        if (pi >= paramCount) break;
        int16_t y    = HEADER_H + 4 + i * ROW_H;
        bool focused = (_focusIdx == pi);
        bool editing = focused && _editMode;
        float val    = getParamValue(nav, pi);

        // Detect switch params (minVal==0, maxVal==1, step==1, no unit)
        bool isSwitch = (params[pi].minVal == 0.0f && params[pi].maxVal == 1.0f
                         && params[pi].step == 1.0f && params[pi].unit[0] == '\0');
        if (isSwitch) {
            drawSwitchRow(CONTENT_X, y, CONTENT_W, params[pi].name, val >= 0.5f, focused);
        } else {
            drawSliderRow(CONTENT_X, y, CONTENT_W,
                          params[pi].name, val,
                          params[pi].minVal, params[pi].maxVal, params[pi].unit,
                          focused, editing);
        }
    }

    // Back
    uint8_t backIdx = getItemCount() - 1;
    float holdFrac  = (_holdTracking && _focusIdx == backIdx)
        ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS : 0.0f;
    drawNavButton(CONTENT_X, FOOTER_Y, CONTENT_W, NAV_BTN_H,
                  "BACK", _focusIdx == backIdx,
                  _holdTracking && _focusIdx == backIdx, holdFrac);
}

// ─── EQ common ───────────────────────────────────────────────────────────────
void Display::drawEffectEqCommon() {
    TFT_eSprite& d = _spr;
    NavEntry& nav  = currentNav();

    const char* modName = "EQ";
    for (uint8_t i = 0; i < MODULE_COUNT; i++)
        if (MODULE_LIST[i].id == nav.module) { modName = MODULE_LIST[i].name; break; }

    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextDatum(ML_DATUM);
    d.setTextSize(1);
    d.drawString(modName, CONTENT_X, 10);
    d.drawFastHLine(CONTENT_X, HEADER_H, CONTENT_W, Color::BORDER);

    // Live pregain
    float pregainDb = 0.0f;
    ParametricEQ* eq = getEqPtr(nav.module);
    if (eq) pregainDb = DB_Q8_TO_FLOAT((int16_t)eq->getPregain());

    bool pregFocused = (_focusIdx == 0);
    drawSliderRow(CONTENT_X, HEADER_H + 4, CONTENT_W,
                  "Pregain", pregainDb, -24.0f, 24.0f, "dB",
                  pregFocused, pregFocused && _editMode);

    // Band rows — fixed slots 0..MAX_EQ_BANDS-1 (scrollable)
    constexpr uint8_t VISIBLE = 4;
    for (uint8_t i = 0; i < VISIBLE; i++) {
        uint8_t bi = (uint8_t)(_paramScroll + i);
        if (bi >= MAX_EQ_BANDS) break;
        int16_t y     = HEADER_H + 4 + (i + 1) * ROW_H;
        bool focused  = (_focusIdx == bi + 1);
        uint16_t rowBg = focused ? Color::PANEL : Color::BG;

        d.fillRect(CONTENT_X, y, CONTENT_W, ROW_H - 2, rowBg);
        d.drawRect (CONTENT_X, y, CONTENT_W, ROW_H - 2,
                    focused ? Color::ACCENT : Color::BORDER);
        d.setTextDatum(ML_DATUM);

        bool bandEnabled = false;
        char label[20];
        if (eq) {
            const EQFilterParams& bp = eq->getBandParams(bi);
            bandEnabled = (bp.enabled != 0);
            if (bandEnabled) {
                const char* tname = EQ_FILTER_TYPE_NAMES[(uint8_t)bp.type];
                snprintf(label, sizeof(label), "B%d [%s] %dHz", bi + 1, tname, (int)bp.f0);
            } else {
                snprintf(label, sizeof(label), "B%d  (off)", bi + 1);
            }
        } else {
            snprintf(label, sizeof(label), "Band %d", bi + 1);
        }

        d.setTextColor(bandEnabled
                           ? (focused ? Color::TEXT_FOCUS : Color::TEXT)
                           : Color::TEXT_DIM,
                       rowBg);
        d.drawString(label, CONTENT_X + 6, y + (ROW_H - 2) / 2);

        d.setTextColor(bandEnabled ? Color::RED : Color::GREEN, rowBg);
        d.setTextDatum(MR_DATUM);
        d.drawString(bandEnabled ? "Press to DEL" : "Press ADD",
                     CONTENT_W + 20, y + (ROW_H - 2) / 2);
    }

    // GRAPH / BACK nav buttons
    uint8_t cnt      = getItemCount();
    uint8_t graphIdx = cnt - 2;
    uint8_t backIdx  = cnt - 1;
    int16_t btnY     = FOOTER_Y - NAV_BTN_H - 2;
    float hfGraph = (_holdTracking && _focusIdx == graphIdx)
        ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS : 0.0f;
    float hfBack = (_holdTracking && _focusIdx == backIdx)
        ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS : 0.0f;

    drawNavButton(CONTENT_X, btnY, CONTENT_W, NAV_BTN_H,
                  "GRAPH", _focusIdx == graphIdx,
                  _holdTracking && _focusIdx == graphIdx, hfGraph);
    drawNavButton(CONTENT_X, FOOTER_Y, CONTENT_W, NAV_BTN_H,
                  "BACK", _focusIdx == backIdx,
                  _holdTracking && _focusIdx == backIdx, hfBack);
}

// ─── EQ graph ────────────────────────────────────────────────────────────────
void Display::drawEffectEqGraph() {
    TFT_eSprite& d = _spr;
    NavEntry&    nav = currentNav();

    // Graph area: full width, padded to 190px height to leave room for info + BACK
    constexpr int16_t GX = 0;
    constexpr int16_t GY = 0;
    constexpr int16_t GW = DISP_W;
    constexpr int16_t GH = 190;

    // Background
    d.fillRect(GX, GY, GW, GH, 0x0821);
    d.drawRect(GX, GY, GW, GH, Color::BORDER);

    // Grid lines (dB) — labels on left inside graph, no overflow
    const int8_t dbLines[] = { -24, -18, -12, -6, 0, 6, 12, 18, 24 };
    for (int8_t db : dbLines) {
        int16_t y = GY + GH / 2 - (int16_t)(db * GH / 48);
        uint16_t c = (db == 0) ? Color::BORDER : 0x18C3;
        d.drawFastHLine(GX, y, GW, c);
        if (db != 0) {
            char lbl[6]; snprintf(lbl, sizeof(lbl), "%+d", (int)db);
            d.setTextColor(Color::TEXT_DIM, 0x0821);
            d.setTextDatum(ML_DATUM);
            d.setTextSize(1);
            d.drawString(lbl, GX + 2, y - 4);
        }
    }

    // Load all fixed band slots from pipeline
    EqBandDesc bands[MAX_EQ_BANDS];
    ParametricEQ* eq  = getEqPtr(nav.module);

    for (uint8_t i = 0; i < MAX_EQ_BANDS; i++) {
        if (eq) {
            const EQFilterParams& bp = eq->getBandParams(i);
            bands[i].enabled = (bp.enabled != 0);
            bands[i].type    = (EQFilterType)bp.type;
            bands[i].freq    = (float)bp.f0;
            bands[i].gain    = DB_Q8_TO_FLOAT(bp.gain);
            bands[i].q       = Q_Q610_TO_FLOAT(bp.Q);
        } else {
            bands[i] = { (i == 0), EQFilterType::EQ_FILTER_TYPE_PEAKING,
                         1000.0f, 0.0f, 0.707f };
        }
    }

    drawEqCurve(bands, MAX_EQ_BANDS, GX, GY, GW, GH);

    // Node dots (fixed slot index)
    for (uint8_t i = 0; i < MAX_EQ_BANDS; i++) {
        if (!bands[i].enabled) continue;
        float   logF = log10f(bands[i].freq / 20.0f) / log10f(20000.0f / 20.0f);
        int16_t nx   = GX + (int16_t)(logF * GW);
        int16_t ny   = GY + GH / 2 - (int16_t)(bands[i].gain * GH / 48.0f);
        bool focused = (_focusIdx == i);
        uint16_t col = focused ? Color::ACCENT : Color::ACCENT2;
        d.fillCircle(nx, ny, focused ? 7 : 5, col);
        d.drawCircle(nx, ny, focused ? 7 : 5, Color::TEXT);
        char num[4]; snprintf(num, sizeof(num), "%d", i + 1);
        d.setTextColor(Color::BG, col);
        d.setTextDatum(MC_DATUM);
        d.setTextSize(1);
        d.drawString(num, nx, ny);
    }

    // ── Band info row — left side, below graph, no overlap ───────────────────
    if (_focusIdx < MAX_EQ_BANDS && bands[_focusIdx].enabled) {
        const EqBandDesc& fb = bands[_focusIdx];
        const char* typeName = EQ_FILTER_TYPE_NAMES[(uint8_t)fb.type];
        char info[48];
        if (EQ_FILTER_HAS_GAIN[(uint8_t)fb.type])
            snprintf(info, sizeof(info), "B%d [%s] %.0fHz %.1fdB Q%.2f",
                     _focusIdx + 1, typeName, fb.freq, fb.gain, fb.q);
        else
            snprintf(info, sizeof(info), "B%d [%s] %.0fHz  Q%.2f",
                     _focusIdx + 1, typeName, fb.freq, fb.q);

        d.setTextColor(Color::TEXT, Color::BG);
        d.setTextDatum(ML_DATUM);
        d.setTextSize(1);
        d.drawString(info, 4, GH + 2);   // y=192: info row, left-aligned
    }

    // BACK button — bottom-right, clear of info text
    float hf = (_holdTracking && _focusIdx == getItemCount() - 1)
        ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS : 0.0f;
    drawNavButton(230, 208, 80, 24,                         // x=230, y=208
                  "BACK", _focusIdx == getItemCount() - 1,
                  _holdTracking && _focusIdx == getItemCount() - 1, hf);
}

// ─── EQ band edit ─────────────────────────────────────────────────────────────
//
// Focus layout (items 0-N, [BACK] always last):
//   0  : Type selector row   (7 pill buttons, editing = encoder cycles type)
//   1  : Freq slider/input
//   2  : Gain slider/input   (hidden + skipped when type has no gain)
//   2/3: Q slider/input
//   last: [BACK]
//
// Item count = 4 (with gain) or 3 (without gain, LP/HP/BP/NOTCH)
//
void Display::drawEqBandEdit() {
    TFT_eSprite& d = _spr;
    NavEntry& nav  = currentNav();

    // Load current band data
    ParametricEQ* eq = getEqPtr(nav.module);
    uint8_t bandIdx  = nav.subContext;
    float curFreq = 1000.0f, curGain = 0.0f, curQ = 0.707f;
    if (eq) {
        const EQFilterParams& bp = eq->getBandParams(bandIdx);
        curFreq = (float)bp.f0;
        curGain = DB_Q8_TO_FLOAT(bp.gain);
        curQ    = Q_Q610_TO_FLOAT(bp.Q);
    }

    // Header
    char title[22];
    snprintf(title, sizeof(title), "Band %d  Edit", bandIdx + 1);
    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextDatum(ML_DATUM);
    d.setTextSize(1);
    d.drawString(title, CONTENT_X, 10);
    d.drawFastHLine(CONTENT_X, HEADER_H, CONTENT_W, Color::BORDER);

    bool hasGain = EQ_FILTER_HAS_GAIN[(uint8_t)_bandEditType];

    // ── Item 0: Filter type selector ──────────────────────────────────────────
    drawFilterTypeRow(CONTENT_X, HEADER_H + 4, CONTENT_W,
                      _bandEditType,
                      _focusIdx == 0, _focusIdx == 0 && _editMode);

    // ── Item 1: Freq ──────────────────────────────────────────────────────────
    drawSliderRow(CONTENT_X, HEADER_H + 4 + ROW_H, CONTENT_W,
                  "Freq", curFreq, 20.0f, 20000.0f, "Hz",
                  _focusIdx == 1, _focusIdx == 1 && _editMode);

    // ── Item 2 (optional): Gain ───────────────────────────────────────────────
    uint8_t qItem   = hasGain ? 3 : 2;
    uint8_t backItem = qItem + 1;

    if (hasGain) {
        drawSliderRow(CONTENT_X, HEADER_H + 4 + 2 * ROW_H, CONTENT_W,
                      "Gain", curGain, -24.0f, 24.0f, "dB",
                      _focusIdx == 2, _focusIdx == 2 && _editMode);
    } else {
        // Draw a dim label so the user knows gain is N/A for this filter type
        int16_t y = HEADER_H + 4 + 2 * ROW_H;
        d.fillRect(CONTENT_X, y, CONTENT_W, ROW_H - 2, Color::BG);
        d.setTextColor(Color::TEXT_DIM, Color::BG);
        d.setTextDatum(ML_DATUM);
        d.drawString("Gain  —  N/A for this type", CONTENT_X + 4, y + (ROW_H - 2) / 2);
    }

    // ── Q ─────────────────────────────────────────────────────────────────────
    drawSliderRow(CONTENT_X, HEADER_H + 4 + 3 * ROW_H, CONTENT_W,
                  "Q", curQ, 0.1f, 10.0f, "",
                  _focusIdx == qItem, _focusIdx == qItem && _editMode);

    // ── [BACK] ────────────────────────────────────────────────────────────────
    float hf = (_holdTracking && _focusIdx == backItem)
        ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS : 0.0f;
    drawNavButton(CONTENT_X, FOOTER_Y, CONTENT_W, NAV_BTN_H,
                  "BACK", _focusIdx == backItem,
                  _holdTracking && _focusIdx == backItem, hf);
}

// ─── ISF common ──────────────────────────────────────────────────────────────
void Display::drawEffectIsfCommon() {
    TFT_eSprite& d = _spr;

    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextDatum(ML_DATUM);
    d.setTextSize(1);
    d.drawString("ISF EQ Config", CONTENT_X, 10);
    d.drawFastHLine(CONTENT_X, HEADER_H, CONTENT_W, Color::BORDER);

    // Config params (input only, no slider per spec)
    const char* cfgNames[] = { "RMS Window", "Slew Time", "Lookahead" };
    const char* cfgUnits[] = { "ms", "ms/step", "ms" };

    // Read actual values from ISF object
    NavEntry& nav = currentNav();
    float cfgVals[3] = { 0.0f, 0.0f, 0.0f };
    for (uint8_t i = 0; i < 3; i++) {
        cfgVals[i] = getParamValue(nav, i);
    }

    for (uint8_t i = 0; i < 3; i++) {
        int16_t y = HEADER_H + 4 + i * ROW_H;
        drawInputRow(CONTENT_X, y, CONTENT_W,
                     cfgNames[i], cfgVals[i], cfgUnits[i],
                     _focusIdx == i);
    }

    // Total preset label + +Add / -Remove
    int16_t presetLabelY = HEADER_H + 4 + 3 * ROW_H;
    d.setTextColor(Color::TEXT_DIM, Color::BG);
    d.setTextDatum(ML_DATUM);
    d.drawString("Presets:", CONTENT_X, presetLabelY + 8);

    int16_t btnW = (CONTENT_W / 2) - 2;
    drawNavButton(CONTENT_X + 60, presetLabelY, btnW / 2, NAV_BTN_H - 2,
                  "+Add", _focusIdx == 3, false, 0.0f);
    drawNavButton(CONTENT_X + 60 + btnW / 2 + 2, presetLabelY, btnW / 2, NAV_BTN_H - 2,
                  "-Del", _focusIdx == 4, false, 0.0f);

    // Preset list - read actual count from ISF object
    uint8_t isfPresetCount = 0;
    if (_pipeline) {
        auto& isf = (nav.module == DisplayModuleID::ISF1)
                    ? _pipeline->getIsf1() : _pipeline->getIsf2();
        isfPresetCount = isf.getNumPresets();
    }

    // Draw visible presets using scroll offset
    constexpr uint8_t VISIBLE_ISF_PRESET = 4;
    for (uint8_t i = 0; i < VISIBLE_ISF_PRESET && ((uint8_t)_paramScroll + i) < isfPresetCount; i++) {
        uint8_t presetIdx = (uint8_t)_paramScroll + i;
        int16_t y = presetLabelY + NAV_BTN_H + 2 + i * 22;
        if (y + 22 > FOOTER_Y) break;
        bool focused = (_focusIdx == 5 + presetIdx);
        uint16_t rowBg = focused ? Color::PANEL : Color::BG;
        d.fillRect(CONTENT_X, y, CONTENT_W - 60, 20, rowBg);
        d.drawRect (CONTENT_X, y, CONTENT_W - 60, 20,
                    focused ? Color::ACCENT : Color::BORDER);
        char plabel[12]; snprintf(plabel, sizeof(plabel), "Preset %d", presetIdx + 1);
        d.setTextColor(focused ? Color::TEXT_FOCUS : Color::TEXT, rowBg);
        d.setTextDatum(ML_DATUM);
        d.drawString(plabel, CONTENT_X + 4, y + 10);

        // Edit button
        drawNavButton(CONTENT_X + CONTENT_W - 56, y, 54, 20,
                      "EDIT", focused, false, 0.0f);
    }

    // Back
    uint8_t cnt = getItemCount();
    float hf = (_holdTracking && _focusIdx == cnt - 1)
        ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS : 0.0f;
    drawNavButton(CONTENT_X, FOOTER_Y, CONTENT_W, NAV_BTN_H,
                  "BACK", _focusIdx == cnt - 1,
                  _holdTracking && _focusIdx == cnt - 1, hf);
}

// ─── DRC Config: Mode + Crossover Settings ─────────────────────────────────
static const char* DRC_MODE_NAMES[] = {
    "Fullband", "2 Band", "2 Band + FB", "3 Band", "3 Band + FB"
};
static const char* DRC_CF_TYPE_NAMES[] = {
    "", "", "LR2", "LR4", "Linkwitz"
};

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

    // Item 0: Mode selector
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
            d.drawString("< >", CONTENT_X + CONTENT_W - 6 - d.textWidth(modeStr) - 4, y + (ROW_H - 2) / 2);
        }
    }

    // Item 1: CF Type selector
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
            d.drawString("< >", CONTENT_X + CONTENT_W - 6 - d.textWidth(cfStr) - 4, y + (ROW_H - 2) / 2);
        }
    }

    // Items 2-5: Crossover params (read from DRC object)
    const char* xoverNames[] = { "Freq 1", "Q 1", "Freq 2", "Q 2" };
    const char* xoverUnits[] = { "Hz", "", "Hz", "" };
    float xoverVals[4] = { 200.0f, 0.707f, 3000.0f, 0.707f };
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
        drawInputRow(CONTENT_X, y, CONTENT_W, xoverNames[i], xoverVals[i], xoverUnits[i], focused);
    }

    // NEXT + BACK
    uint8_t cnt = getItemCount();
    float hf = _holdTracking ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS : 0.0f;
    int16_t halfW = (CONTENT_W - 2) / 2;
    drawNavButton(CONTENT_X, FOOTER_Y, halfW, NAV_BTN_H,
                  "NEXT", _focusIdx == cnt - 2,
                  _holdTracking && _focusIdx == cnt - 2, hf);
    drawNavButton(CONTENT_X + halfW + 2, FOOTER_Y, halfW, NAV_BTN_H,
                  "BACK", _focusIdx == cnt - 1,
                  _holdTracking && _focusIdx == cnt - 1, hf);
}

// ─── DRC Band Select: Dynamic tabs based on mode ──────────────────────────
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
            case DRC_MODE_FULLBAND: bandCount = 1; break;
            case DRC_MODE_2BAND: bandCount = 2; break;
            case DRC_MODE_2BAND_FULLBAND: bandCount = 3; break;
            case DRC_MODE_3BAND: bandCount = 3; break;
            case DRC_MODE_3BAND_FULLBAND: bandCount = 4; break;
        }
    }

    const char* bandLabels[] = { "Band 1", "Band 2", "Band 3", "Fullband" };

    // Draw band tabs in grid
    int16_t tabW = (CONTENT_W - 8) / 2;
    int16_t tabH = 40;

    for (uint8_t i = 0; i < bandCount; i++) {
        uint8_t col = i % 2;
        uint8_t row = i / 2;
        int16_t x = CONTENT_X + col * (tabW + 4);
        int16_t y = HEADER_H + 4 + row * (tabH + 4);
        bool focused = (_focusIdx == i);

        uint16_t bg = focused ? Color::ACCENT : Color::PANEL;
        uint16_t fg = focused ? Color::BG : Color::TEXT;
        d.fillRoundRect(x, y, tabW, tabH, 4, bg);
        d.drawRoundRect(x, y, tabW, tabH, 4, focused ? Color::TEXT_FOCUS : Color::BORDER);

        d.setTextColor(fg, bg);
        d.setTextDatum(MC_DATUM);
        d.drawString(bandLabels[i], x + tabW / 2, y + tabH / 2);
    }

    // BACK
    uint8_t cnt = getItemCount();
    float hf = _holdTracking ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS : 0.0f;
    drawNavButton(CONTENT_X, FOOTER_Y, CONTENT_W, NAV_BTN_H,
                  "BACK", _focusIdx == cnt - 1,
                  _holdTracking && _focusIdx == cnt - 1, hf);
}

// ─── DRC Band Params: Per-band settings + compression curve ───────────────
void Display::drawDrcBandParams() {
    TFT_eSprite& d = _spr;

    d.setTextColor(Color::ACCENT, Color::BG);
    d.setTextDatum(ML_DATUM);
    d.setTextSize(1);
    char title[24];
    snprintf(title, sizeof(title), "DRC Band %u", (unsigned)(_drcActiveBand + 1));
    d.drawString(title, CONTENT_X, 10);
    d.drawFastHLine(CONTENT_X, HEADER_H, CONTENT_W, Color::BORDER);

    const char* bpNames[] = { "Pregain", "Threshold", "Ratio",
                               "Attack", "Release", "Lookahead" };
    const char* bpUnits[] = { "dB", "dB", ":1", "ms", "ms", "ms" };

    float bpVals[6] = { 0.0f };
    if (_pipeline) {
        DRC& drc = _pipeline->getDrc();
        uint8_t band = _drcActiveBand;
        float pregainLinear = PREGAIN_Q412_TO_FLOAT(drc._bands[band].pregainQ412);
        bpVals[0] = (pregainLinear > 0.0001f) ? roundf(20.0f * log10f(pregainLinear)) : -96.0f;
        bpVals[1] = DRC_TH_TO_FLOAT_DB(drc._bands[band].thresholdDbInt);
        bpVals[2] = (float)drc._bands[band].ratioX100 / 100.0f;
        bpVals[3] = (float)drc._bands[band].attackMs;
        bpVals[4] = (float)drc._bands[band].releaseMs;
        bpVals[5] = drc._bands[band].lookaheadMs;
    }

    // Param rows (scrollable)
    static const float bpMins[] = { -24.0f, -60.0f, 1.0f, 1.0f, 1.0f, 0.0f };
    static const float bpMaxs[] = {  24.0f,   0.0f, 100.0f, 2000.0f, 2000.0f, 10.0f };
    for (uint8_t i = 0; i < VISIBLE_ROWS_DRC_PARAMS; i++) {
        uint8_t pi = (uint8_t)(_paramScroll + i);
        if (pi >= 6) break;
        int16_t y = HEADER_H + 4 + i * ROW_H;
        bool focused = (_focusIdx == pi);
        bool editing = focused && _editMode;
        drawSliderRow(CONTENT_X, y, CONTENT_W,
                      bpNames[pi], bpVals[pi], bpMins[pi], bpMaxs[pi], bpUnits[pi],
                      focused, editing);
    }

    // Compression curve graph
    int16_t graphY = HEADER_H + 4 + VISIBLE_ROWS_DRC_PARAMS * ROW_H + 2;
    int16_t graphH = FOOTER_Y - graphY - 4;
    if (graphH > 30) {
        constexpr int16_t GX = 30;
        int16_t GW = CONTENT_W - GX - 4;
        drawDrcCurve(bpVals[1], bpVals[2], bpVals[0],
                     CONTENT_X + GX, graphY, GW, graphH);
    }

    // BACK only (graph embedded above)
    uint8_t cnt = getItemCount();
    float hf = _holdTracking ? (float)(millis() - _focusHoldStartMs) / AUTO_CONFIRM_MS : 0.0f;
    drawNavButton(CONTENT_X, FOOTER_Y, CONTENT_W, NAV_BTN_H,
                  "BACK", _focusIdx == cnt - 1,
                  _holdTracking && _focusIdx == cnt - 1, hf);
}

// ─── Keyboard ─────────────────────────────────────────────────────────────────
void Display::drawKeyboard() {
    TFT_eSprite& d = _spr;

    // Current value display
    char valBuf[KB_MAX_LEN + 4];
    snprintf(valBuf, sizeof(valBuf), "> %s_", _kb.buf);
    d.setTextColor(Color::TEXT_FOCUS, Color::BG);
    d.setTextDatum(ML_DATUM);
    d.setTextSize(2);
    d.drawString(valBuf, 4, 10);

    // Key grid
    constexpr int16_t KEY_W = 60, KEY_H = 36, KEY_GAP = 4;
    constexpr int16_t KBX   = (DISP_W - KB_COLS * (KEY_W + KEY_GAP)) / 2;
    constexpr int16_t KBY   = 40;

    for (uint8_t r = 0; r < KB_NUM_ROWS; r++) {
        for (uint8_t c = 0; c < KB_COLS; c++) {
            bool     focused = (_kb.cursorRow == r && _kb.cursorCol == c);
            char     label[2] = { KB_ROWS[r][c], '\0' };
            int16_t  kx = KBX + c * (KEY_W + KEY_GAP);
            int16_t  ky = KBY + r * (KEY_H + KEY_GAP);

            // Disable dot if not allowed
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

    // Action row: DEL, CANCEL, ENTER
    constexpr int16_t ACT_W = DISP_W / 3 - 4;
    constexpr int16_t ACTY  = KBY + KB_NUM_ROWS * (KEY_H + KEY_GAP) + 4;
    const uint16_t actColors[] = { Color::YELLOW, Color::TEXT_DIM, Color::GREEN };
    for (uint8_t i = 0; i < KB_ACT_COLS; i++) {
        bool     focused = (_kb.cursorRow == KB_NUM_ROWS && _kb.cursorCol == i);
        int16_t  ax = 2 + i * (ACT_W + 4);
        uint16_t bg = focused ? actColors[i] : Color::PANEL;
        d.fillRect(ax, ACTY, ACT_W, KEY_H, bg);
        d.drawRect(ax, ACTY, ACT_W, KEY_H, Color::BORDER);
        d.setTextColor(focused ? Color::BG : actColors[i], bg);
        d.setTextDatum(MC_DATUM);
        d.setTextSize(1);
        d.drawString(KB_ACTIONS[i], ax + ACT_W / 2, ACTY + KEY_H / 2);
    }
}

// ─── Common widgets ───────────────────────────────────────────────────────────
void Display::drawSliderRow(int16_t x, int16_t y, int16_t w,
                            const char* label, float value,
                            float minV, float maxV, const char* unit,
                            bool focused, bool editing) {
    drawSliderRow(x, y, w, label, value, minV, maxV, unit, focused, editing, 2);
}

void Display::drawSliderRow(int16_t x, int16_t y, int16_t w,
                            const char* label, float value,
                            float minV, float maxV, const char* unit,
                            bool focused, bool editing, uint8_t decimals) {
    TFT_eSprite& d = _spr;
    int16_t h = ROW_H - 2;

    // Determine base colors
    uint16_t bg, borderColor, textColor;
    
    if (focused) {
        // Focused item colors
        if (_anim.focusTransition < 1.0f) {
            // Animating into focus
            uint8_t blend = (uint8_t)(_anim.focusTransition * 255.0f);
            bg = blendColor(Color::BG, Color::PANEL, blend);
            borderColor = blendColor(Color::BORDER, Color::ACCENT, blend);
            textColor = blendColor(Color::TEXT, Color::TEXT_FOCUS, blend);
        } else {
            // Fully focused
            bg = Color::PANEL;
            borderColor = Color::ACCENT;
            textColor = Color::TEXT_FOCUS;
        }
    } else {
        // Not focused
        bg = Color::BG;
        borderColor = Color::BORDER;
        textColor = Color::TEXT;
    }
    
    // Draw base
    d.fillRect(x, y, w, h, bg);
    d.drawRect(x, y, w, h, borderColor);
    
    // Apply edit mode sweep OVER base if editing
    if (editing && focused && _anim.aniSweepActive) {
        // Sweep animation will overwrite background
        drawSweepAnimation(x, y, w, h);
        // After sweep, force text color to contrast with accent
        textColor = Color::BG;
        bg = Color::ACCENT; // for text background
    }
    else if (!editing && focused && _anim.aniSweepExit && _anim.aniSweepActive) {
        // Exit animation in progress - draw reverse sweep
        drawSweepAnimation(x, y, w, h);
        textColor = Color::ACCENT;
        bg = Color::BG;
    }

    // Label
    d.setTextDatum(ML_DATUM);
    d.setTextColor(textColor, bg);
    d.setTextSize(1);
    d.drawString(label, x + 4, y + h / 2);

    // Slider track
    constexpr int16_t LABEL_W  = 70;
    constexpr int16_t VAL_W    = 44;
    constexpr int16_t TRACK_PAD = 4;
    int16_t trackX = x + LABEL_W;
    int16_t trackW = w - LABEL_W - VAL_W - TRACK_PAD * 2;
    int16_t trackY = y + h / 2 - 3;
    int16_t trackH = 6;

    d.fillRect(trackX, trackY, trackW, trackH, Color::SLIDER_BG);

    float norm   = (maxV > minV) ? (value - minV) / (maxV - minV) : 0.0f;
    norm          = norm < 0.0f ? 0.0f : (norm > 1.0f ? 1.0f : norm);
    int16_t fillW = (int16_t)(norm * trackW);
    if (fillW > 0)
        d.fillRect(trackX, trackY, fillW, trackH, Color::SLIDER_FILL);

    // Thumb
    int16_t thumbX = trackX + fillW;
    d.fillCircle(thumbX, trackY + trackH / 2, editing ? 6 : 4,
                 editing ? Color::ACCENT2 : Color::ACCENT);
    if (editing)
        d.drawCircle(thumbX, trackY + trackH / 2, 6, Color::BORDER);

    // Value box
    char valBuf[10];
    formatFloat(valBuf, sizeof(valBuf), value, decimals);
    int16_t valX = x + w - VAL_W + 2;
    d.fillRect(valX, y + 2, VAL_W - 4, h - 4, focused ? 0x18A3 : 0x1082);
    d.drawRect (valX, y + 2, VAL_W - 4, h - 4, editing ? Color::BORDER : focused ? Color::ACCENT : Color::BORDER);
    d.setTextDatum(MR_DATUM);
    d.setTextColor(focused ? Color::TEXT_FOCUS : Color::TEXT, focused ? 0x18A3 : 0x1082);
    d.drawString(valBuf, valX + VAL_W - 6, y + h / 2);

    // Unit
    if (unit && unit[0]) {
        d.setTextColor(Color::TEXT_DIM, bg);
        d.setTextDatum(ML_DATUM);
        char unitBuf[6];
        snprintf(unitBuf, sizeof(unitBuf), "%s", unit);
        // unit fits after the value box — use tooltip trick: overprint on bg
    }
}

void Display::drawSwitchRow(int16_t x, int16_t y, int16_t w,
                            const char* label, bool value, bool focused) {
    TFT_eSprite& d = _spr;
    int16_t h = ROW_H - 2;

    // Determine base colors
    uint16_t bg, borderColor, textColor;
    
    if (focused) {
        // Focused item colors
        if (_anim.focusTransition < 1.0f) {
            // Animating into focus
            uint8_t blend = (uint8_t)(_anim.focusTransition * 255.0f);
            bg = blendColor(Color::BG, Color::PANEL, blend);
            borderColor = blendColor(Color::BORDER, Color::ACCENT, blend);
            textColor = blendColor(Color::TEXT, Color::TEXT_FOCUS, blend);
        } else {
            // Fully focused
            bg = Color::PANEL;
            borderColor = Color::ACCENT;
            textColor = Color::TEXT_FOCUS;
        }
    } else {
        // Not focused
        bg = Color::BG;
        borderColor = Color::BORDER;
        textColor = Color::TEXT;
    }
    
    // Draw base
    d.fillRect(x, y, w, h, bg);
    d.drawRect(x, y, w, h, borderColor);
    
    // Apply edit mode sweep OVER base if editing
    if (_editMode && focused && _anim.aniSweepActive) {
        drawSweepAnimation(x, y, w, h);
        textColor = Color::BG;
        bg = Color::ACCENT;
    } else if (!_editMode && focused && _anim.aniSweepExit && _anim.aniSweepActive) {
        // Exit animation in progress - draw reverse sweep
        drawSweepAnimation(x, y, w, h);
        textColor = Color::ACCENT;
        bg = Color::BG;
    }

    d.setTextDatum(ML_DATUM);
    d.setTextColor(textColor, bg);
    d.setTextSize(1);
    d.drawString(label, x + 4, y + h / 2);

    // Switch pill
    constexpr int16_t PW = 36, PH = 16;
    int16_t px = x + w - PW - 6;
    int16_t py = y + (h - PH) / 2;
    uint16_t pillBg = value ? (_editMode && focused) ? Color::BORDER : Color::ACCENT : Color::BORDER;
    d.fillRoundRect(px, py, PW, PH, PH / 2, pillBg);
    int16_t knobX = value ? px + PW - PH / 2 - 1 : px + PH / 2 + 1;
    d.fillCircle(knobX, py + PH / 2, PH / 2 - 2, Color::TEXT_DIM);
}

void Display::drawInputRow(int16_t x, int16_t y, int16_t w,
                           const char* label, float value,
                           const char* unit, bool focused) {
    TFT_eSprite& d = _spr;
    int16_t h = ROW_H - 2;

    uint16_t bg, borderColor;
    
    if (focused) {
        // Focused item colors
        if (_anim.focusTransition < 1.0f) {
            // Animating into focus
            uint8_t blend = (uint8_t)(_anim.focusTransition * 255.0f);
            bg = blendColor(Color::BG, Color::PANEL, blend);
            borderColor = blendColor(Color::BORDER, Color::ACCENT, blend);
        } else {
            // Fully focused
            bg = Color::PANEL;
            borderColor = Color::ACCENT;
        }
    } else {
        // Not focused
        bg = Color::BG;
        borderColor = Color::BORDER;
    }

    d.fillRect(x, y, w, h, bg);
    d.drawRect(x, y, w, h, borderColor);

    d.setTextDatum(ML_DATUM);
    d.setTextColor(focused ? Color::TEXT_FOCUS : Color::TEXT, bg);
    d.setTextSize(1);
    d.drawString(label, x + 4, y + h / 2);

    // Value box (right-aligned, wider than slider version)
    constexpr int16_t VAL_W = 60;
    int16_t valX = x + w - VAL_W - 4;
    char valBuf[12];
    snprintf(valBuf, sizeof(valBuf), "%.1f %s", value, unit ? unit : "");
    d.fillRect(valX, y + 2, VAL_W, h - 4, focused ? 0x18A3 : 0x1082);
    d.drawRect (valX, y + 2, VAL_W, h - 4, borderColor);
    d.setTextDatum(MC_DATUM);
    d.setTextColor(focused ? Color::TEXT_FOCUS : Color::TEXT, focused ? 0x18A3 : 0x1082);
    d.drawString(valBuf, valX + VAL_W / 2, y + h / 2);
}

void Display::drawNavButton(int16_t x, int16_t y, int16_t w, int16_t h,
                            const char* label, bool focused,
                            bool holdProgress, float holdFrac) {
    TFT_eSprite& d = _spr;

    uint16_t bg, borderColor, textColor;
    
    if (focused) {
        // Focused item colors
        if (_anim.focusTransition < 1.0f) {
            // Animating into focus
            uint8_t blend = (uint8_t)(_anim.focusTransition * 255.0f);
            bg = blendColor(Color::BG, Color::PANEL, blend);
            borderColor = blendColor(Color::BORDER, Color::ACCENT, blend);
            textColor = blendColor(Color::TEXT, Color::TEXT_FOCUS, blend);
        } else {
            // Fully focused
            bg = Color::PANEL;
            borderColor = Color::ACCENT;
            textColor = Color::TEXT_FOCUS;
        }
    } else {
        // Not focused
        bg = Color::BG;
        borderColor = Color::BORDER;
        textColor = Color::TEXT;
    }
    
    // Draw base
    d.fillRect(x, y, w, h, bg);
    d.drawRect(x, y, w, h, borderColor);

    d.fillRoundRect(x, y, w, h, 4, bg);
    d.drawRoundRect(x, y, w, h, 4, focused ? Color::TEXT_FOCUS : Color::BORDER);

    d.setTextDatum(MC_DATUM);
    d.setTextColor(textColor, bg);
    d.setTextSize(1);
    d.drawString(label, x + w / 2, y + h / 2);

    // Hold progress bar at bottom of button
    if (holdProgress && holdFrac > 0.0f) {
        int16_t barW = (int16_t)(holdFrac * (w - 4));
        d.fillRect(x + 2, y + h - 4, barW, 3, Color::ACCENT2);
    }
}

// ─── Filter type row ─────────────────────────────────────────────────────────
//
// Renders 7 pill buttons in a single row:
//   [ PK ] [ LS ] [ HS ] [ LP ] [ HP ] [ BP ] [NOTCH]
//
// Active type: filled with ACCENT (cyan)
// Focused+editing: active type also gets a bright white ring
// The row itself has a focus border when focused.
//
void Display::drawFilterTypeRow(int16_t x, int16_t y, int16_t w,
                                EQFilterType current,
                                bool focused, bool editing) {
    TFT_eSprite& d = _spr;
    constexpr uint8_t N      = (uint8_t)EQFilterType::EQ_FILTER_TYPE_COUNT - 2; // 7
    int16_t h                = ROW_H - 2;
    constexpr int16_t LABEL_W = 36;    // "Type" label width
    constexpr int16_t GAP     = 2;

    // Row background + border
    uint16_t rowBg = focused ? Color::PANEL : Color::BG;
    d.fillRect(x, y, w, h, rowBg);
    d.drawRect (x, y, w, h, focused ? Color::ACCENT : Color::BORDER);

    // "Type" label on the left
    d.setTextDatum(ML_DATUM);
    d.setTextColor(focused ? Color::TEXT_FOCUS : Color::TEXT, rowBg);
    d.setTextSize(1);
    d.drawString("Type", x + 4, y + h / 2);

    // Pill area — distribute remaining width evenly
    int16_t pillAreaX = x + LABEL_W;
    int16_t pillAreaW = w - LABEL_W - 2;
    int16_t pillW     = (pillAreaW - (N - 1) * GAP) / N;

    for (uint8_t i = 0; i < N; i++) {
        bool isActive = ((uint8_t)current == i);
        int16_t px = pillAreaX + i * (pillW + GAP);
        int16_t py = y + 3;
        int16_t ph = h - 6;

        uint16_t pillBg, pillFg;
        if (isActive && editing) {
            // Active + being changed: bright fill + white ring
            pillBg = Color::ACCENT;
            pillFg = Color::BG;
        } else if (isActive) {
            // Active but not in edit mode: filled accent
            pillBg = Color::ACCENT;
            pillFg = Color::BG;
        } else {
            // Inactive
            pillBg = focused ? 0x1884 : Color::BORDER;
            pillFg = focused ? Color::TEXT : Color::TEXT_DIM;
        }

        d.fillRoundRect(px, py, pillW, ph, 3, pillBg);

        if (isActive && editing) {
            // Extra highlight ring when actively cycling this type
            d.drawRoundRect(px - 1, py - 1, pillW + 2, ph + 2, 4, Color::TEXT_FOCUS);
        } else if (isActive) {
            d.drawRoundRect(px, py, pillW, ph, 3, Color::TEXT);
        }

        d.setTextColor(pillFg, pillBg);
        d.setTextDatum(MC_DATUM);
        // "NOTCH" is long — use size 1 always; others fit at size 1 too
        d.setTextSize(1);
        d.drawString(EQ_FILTER_TYPE_NAMES[i], px + pillW / 2, py + ph / 2);
    }

    // If editing: show hint at far right
    if (editing) {
        d.setTextColor(Color::ACCENT2, rowBg);
        d.setTextDatum(MR_DATUM);
        d.setTextSize(1);
        d.drawString("< >", x + w - 2, y + h / 2);
    }
}

// ─── Side bars ────────────────────────────────────────────────────────────────
void Display::drawSideBars(int16_t x, int16_t y, int16_t h) {
    TFT_eSprite& d = _spr;
    constexpr int16_t BAR_W = 10;

    // CPU bar
    d.drawRect(x, y, BAR_W, h, Color::BORDER);
    uint8_t cpuPct = (uint8_t)(_cpuTenths / 10);
    if (cpuPct > 100) cpuPct = 100;
    int16_t cpuH = (int16_t)((uint32_t)cpuPct * h / 100);
    uint16_t cpuCol = cpuPct > 80 ? Color::RED
                    : cpuPct > 60 ? Color::YELLOW
                    :               Color::GREEN;
    d.fillRect(x + 1, y + h - cpuH, BAR_W - 2, cpuH, cpuCol);

    d.setTextDatum(TC_DATUM);
    d.setTextColor(Color::TEXT_DIM, Color::BG);
    d.setTextSize(1);
    d.drawString("C", x + BAR_W / 2, y - 10);

    // Heap bar
    int16_t hx = x + BAR_W + 2;
    d.drawRect(hx, y, BAR_W, h, Color::BORDER);
    int16_t heapH = (int16_t)((uint32_t)_heapPct * h / 100);
    uint16_t heapCol = _heapPct < 20 ? Color::RED
                     : _heapPct < 40 ? Color::YELLOW
                     :                 Color::GREEN;
    d.fillRect(hx + 1, y + h - heapH, BAR_W - 2, heapH, heapCol);

    d.setTextDatum(TC_DATUM);
    d.setTextColor(Color::TEXT_DIM, Color::BG);
    d.drawString("H", hx + BAR_W / 2, y - 10);
}

// ─── WiFi badge ───────────────────────────────────────────────────────────────
void Display::drawWifiBadge(int16_t x, int16_t y) {
    TFT_eSprite& d = _spr;
    // Simple text badge — can replace with icon sprite later
    uint16_t col = _wifiConn ? Color::ACCENT : Color::TEXT_DIM;
    d.setTextDatum(ML_DATUM);
    d.setTextColor(col, Color::BG);
    d.setTextSize(1);
    d.drawString(_wifiConn ? "W+" : "W-", x, y);
}

// ─── EQ curve (simplified biquad magnitude sweep) ─────────────────────────────
// ─── Biquad magnitude helper ───────────────────────────────────────────────
static float biquadMagnitudeDb(uint8_t type, float freq, float f0, float Q, float gainDb, float fs) {    
    float A = powf(10.0f, gainDb / 40.0f);  // sqrt(10^(dB/20))
    float w0 = TWO_PI * f0 / fs;
    float w = TWO_PI * freq / fs;
    
    float cosW0 = cosf(w0);
    float sinW0 = sinf(w0);
    float cosW = cosf(w);
    float cos2W = cosf(2.0f * w);
    float sinW = sinf(w);
    float sin2W = sinf(2.0f * w);
    
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a0 = 1.0f, a1 = 0.0f, a2 = 0.0f;
    
    if (Q < 0.001f) Q = 0.001f;
    float alpha;
    
    switch (type) {
        case 0: // Peaking
            alpha = sinW0 / (2.0f * Q);
            b0 = 1.0f + alpha * A;
            b1 = -2.0f * cosW0;
            b2 = 1.0f - alpha * A;
            a0 = 1.0f + alpha / A;
            a1 = -2.0f * cosW0;
            a2 = 1.0f - alpha / A;
            break;
            
        case 1: { // Low Shelf
            alpha = sinW0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / Q - 1.0f) + 2.0f);
            float sqrtA = sqrtf(A);
            float twoSqrtAAlpha = 2.0f * sqrtA * alpha;
            b0 = A * ((A + 1.0f) - (A - 1.0f) * cosW0 + twoSqrtAAlpha);
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosW0);
            b2 = A * ((A + 1.0f) - (A - 1.0f) * cosW0 - twoSqrtAAlpha);
            a0 = (A + 1.0f) + (A - 1.0f) * cosW0 + twoSqrtAAlpha;
            a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosW0);
            a2 = (A + 1.0f) + (A - 1.0f) * cosW0 - twoSqrtAAlpha;
            break;
        }
            
        case 2: { // High Shelf
            alpha = sinW0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / Q - 1.0f) + 2.0f);
            float sqrtA = sqrtf(A);
            float twoSqrtAAlpha = 2.0f * sqrtA * alpha;
            b0 = A * ((A + 1.0f) + (A - 1.0f) * cosW0 + twoSqrtAAlpha);
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosW0);
            b2 = A * ((A + 1.0f) + (A - 1.0f) * cosW0 - twoSqrtAAlpha);
            a0 = (A + 1.0f) - (A - 1.0f) * cosW0 + twoSqrtAAlpha;
            a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosW0);
            a2 = (A + 1.0f) - (A - 1.0f) * cosW0 - twoSqrtAAlpha;
            break;
        }
            
        case 3: // Low Pass
            alpha = sinW0 / (2.0f * Q);
            b0 = (1.0f - cosW0) / 2.0f;
            b1 = 1.0f - cosW0;
            b2 = (1.0f - cosW0) / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosW0;
            a2 = 1.0f - alpha;
            break;
            
        case 4: // High Pass
            alpha = sinW0 / (2.0f * Q);
            b0 = (1.0f + cosW0) / 2.0f;
            b1 = -(1.0f + cosW0);
            b2 = (1.0f + cosW0) / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosW0;
            a2 = 1.0f - alpha;
            break;
            
        case 5: // Band Pass
            alpha = sinW0 / (2.0f * Q);
            b0 = alpha;
            b1 = 0.0f;
            b2 = -alpha;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosW0;
            a2 = 1.0f - alpha;
            break;
            
        case 6: // Notch
            alpha = sinW0 / (2.0f * Q);
            b0 = 1.0f;
            b1 = -2.0f * cosW0;
            b2 = 1.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosW0;
            a2 = 1.0f - alpha;
            break;
            
        default: // Passthrough
            return 0.0f;
    }
    
    // Normalize by a0
    float invA0 = 1.0f / a0;
    b0 *= invA0;
    b1 *= invA0;
    b2 *= invA0;
    a1 *= invA0;
    a2 *= invA0;
    
    // Magnitude calculation: H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
    // where z = e^(j*w)
    float numReal = b0 + b1 * cosW + b2 * cos2W;
    float numImag = -(b1 * sinW + b2 * sin2W);
    
    float denReal = 1.0f + a1 * cosW + a2 * cos2W;
    float denImag = -(a1 * sinW + a2 * sin2W);
    
    float numMagSq = numReal * numReal + numImag * numImag;
    float denMagSq = denReal * denReal + denImag * denImag;
    
    if (denMagSq < 1e-20f) return 0.0f;
    return 10.0f * log10f(numMagSq / denMagSq);
}

void Display::drawEqCurve(const EqBandDesc* bands, uint8_t nBands,
                          int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
    TFT_eSprite& d = _spr;
    constexpr int STEPS = DISP_W;
    constexpr float FS = 96000.0f; // Sample rate
    
    int16_t prevY = -1;
    for (int px = 0; px < STEPS; px++) {
        float logF = (float)px / STEPS;
        float freq = 20.0f * powf(1000.0f, logF); // 20Hz → 20kHz
        
        float totalDb = 0.0f;
        for (uint8_t b = 0; b < nBands; b++) {
            if (!bands[b].enabled) continue;
            
            totalDb += biquadMagnitudeDb(
                (uint8_t)bands[b].type,
                freq,
                bands[b].freq,
                bands[b].q,
                bands[b].gain,
                FS
            );
        }
        
        totalDb = totalDb < -24.0f ? -24.0f : (totalDb > 24.0f ? 24.0f : totalDb);
        int16_t curY = ry + rh / 2 - (int16_t)(totalDb * rh / 48.0f);
        int16_t cx   = rx + px * rw / STEPS;
        
        if (prevY >= 0 && px > 0)
            d.drawLine(cx - rw / STEPS, prevY, cx, curY, Color::ACCENT);
        prevY = curY;
    }
}

// ─── DRC curve ────────────────────────────────────────────────────────────────
// Rewritten to match drc-graph.js styling: proper grid, labels, legend
void Display::drawDrcCurve(float threshold, float ratio, float pregain,
                           int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
    TFT_eSprite& d = _spr;
    constexpr float RANGE_DB = 90.0f; // -90 to 0 dB range

    // Helper functions
    auto dbToX = [&](float db) -> int16_t {
        return rx + (int16_t)((db + RANGE_DB) / RANGE_DB * rw);
    };
    auto dbToY = [&](float db) -> int16_t {
        return ry + rh - (int16_t)((db + RANGE_DB) / RANGE_DB * rh);
    };

    // Background
    d.fillRect(rx, ry, rw, rh, 0x0821);

    // Grid lines (every 10 dB)
    const int8_t gridSteps[] = { -80, -70, -60, -50, -40, -30, -20, -10, 0 };
    d.setTextSize(1);
    for (int8_t db : gridSteps) {
        int16_t gx = dbToX(db);
        int16_t gy = dbToY(db);
        
        // Vertical + horizontal grid
        uint16_t gridCol = (db == 0) ? Color::BORDER : 0x18C3;
        d.drawFastVLine(gx, ry, rh, gridCol);
        d.drawFastHLine(rx, gy, rw, gridCol);
        
        // Y-axis labels (left side)
        if (db != 0 && rx > 16) {
            char lbl[6];
            snprintf(lbl, sizeof(lbl), "%d", db);
            d.setTextColor(Color::TEXT_DIM, 0x0821);
            d.setTextDatum(MR_DATUM);
            d.drawString(lbl, rx - 2, gy);
        }
    }

    // 1:1 reference line (dotted diagonal)
    d.setTextSize(1);
    for (int px = 0; px < rw; px += 4) {
        float db = ((float)px / rw) * RANGE_DB - RANGE_DB;
        int16_t x = dbToX(db);
        int16_t y = dbToY(db);
        d.drawPixel(x, y, 0xC618); // dim white
    }

    // Threshold line (red dashed)
    int16_t tx = dbToX(threshold);
    int16_t ty = dbToY(threshold);
    for (int16_t y = ry; y < ry + rh; y += 4) {
        d.drawPixel(tx, y, Color::RED);
    }
    for (int16_t x = rx; x < rx + rw; x += 4) {
        d.drawPixel(x, ty, Color::RED);
    }

    // Compression curve (bright green)
    auto transferFn = [&](float inputDb) -> float {
        if (inputDb <= threshold) return inputDb;
        return threshold + (inputDb - threshold) / ratio;
    };

    int16_t prevX = -1, prevY = -1;
    for (int px = 0; px <= rw; px++) {
        float inputDb  = ((float)px / rw) * RANGE_DB - RANGE_DB;
        float outputDb = transferFn(inputDb) + pregain;
        int16_t cx = rx + px;
        int16_t cy = dbToY(outputDb);
        
        if (prevX >= 0) {
            d.drawLine(prevX, prevY, cx, cy, Color::GREEN);
        }
        prevX = cx;
        prevY = cy;
    }

    // Legend (bottom-left corner)
    if (rh > 40) {
        int16_t lx = rx + 4;
        int16_t ly = ry + rh - 32;
        
        // Threshold
        d.setTextColor(Color::RED, 0x0821);
        d.setTextDatum(ML_DATUM);
        d.setTextSize(1);
        char thrLbl[16];
        snprintf(thrLbl, sizeof(thrLbl), "Thr %.0fdB", threshold);
        d.drawString(thrLbl, lx, ly);
        
        // Ratio
        d.setTextColor(Color::GREEN, 0x0821);
        char ratLbl[12];
        snprintf(ratLbl, sizeof(ratLbl), "%.0f:1", ratio);
        d.drawString(ratLbl, lx, ly + 12);
    }
}

// ─── Animation system ─────────────────────────────────────────────────────────

/**
 * Update all active animations. Call from update() each frame.
 */
void Display::updateAnimations() {
    uint32_t now = millis();
    bool needsRedraw = false;
    
    // Update focus transition
    if (_anim.focusTransition < 1.0f) {
        uint32_t elapsed = now - _anim.focusStartMs;
        _anim.focusTransition = (float)elapsed / AnimationState::FOCUS_ANIM_MS;
        if (_anim.focusTransition > 1.0f) {
            _anim.focusTransition = 1.0f;
        }
        needsRedraw = true;
    }
    
    // Update sweep animation
    if (_anim.aniSweepActive && _anim.aniSweepProgress < 1.0f) {
        uint32_t elapsed = now - _anim.aniSweepStartMs;
        _anim.aniSweepProgress = (float)elapsed / AnimationState::SWEEP_ANIM_MS;
        if (_anim.aniSweepProgress > 1.0f) {
            _anim.aniSweepProgress = 1.0f;
        }
        needsRedraw = true;
    }
    
    // Update value animation
    if (_anim.valueAnimating) {
        uint32_t elapsed = now - _anim.valueStartMs;
        _anim.valueProgress = (float)elapsed / AnimationState::VALUE_ANIM_MS;
        if (_anim.valueProgress >= 1.0f) {
            _anim.valueProgress = 1.0f;
            _anim.valueAnimating = false;
        }
        needsRedraw = true;
    }
    
    if (needsRedraw) {
        _dirty = true;
    }

    if (_anim.aniSweepActive && _anim.aniSweepProgress >= 1.0 && !_dirty && !_editMode) {
        _anim.aniSweepActive = false;
    }
}

/**
 * Start focus transition animation when focus index changes.
 */
void Display::startFocusTransition() {
    _anim.prevFocusIdx = _focusIdx;
    _anim.focusTransition = 0.0f;
    _anim.focusStartMs = millis();
}

/**
 * Toggle edit mode and trigger sweep animation.
 * Enter = left-to-right sweep
 * Exit = right-to-left sweep (reverse)
 */
void Display::toggleEditMode() {
    bool wasEdit = _editMode;
    _editMode = !_editMode;
    
    // Trigger sweep animation when entering edit mode
    if (!wasEdit && _editMode) {
        _anim.aniSweepExit = false;
        _anim.aniSweepActive = true;
        _anim.aniSweepProgress = 0.0f;
        _anim.aniSweepStartMs = millis();
    }
    
    // Reverse sweep animation when exiting edit mode
    if (wasEdit && !_editMode) {
        _anim.aniSweepExit = true;
        _anim.aniSweepProgress = 0.0f;
        _anim.aniSweepStartMs = millis();
    }
}

/**
 * Start value animation for smooth number transitions.
 */
void Display::startValueAnimation(float from, float to) {
    _anim.valueFrom = from;
    _anim.valueTo = to;
    _anim.valueProgress = 0.0f;
    _anim.valueStartMs = millis();
    _anim.valueAnimating = true;
}

/**
 * Ease-out cubic: smooth deceleration
 */
float Display::easeOutCubic(float t) {
    float f = 1.0f - t;
    return 1.0f - f * f * f;
}

/**
 * Ease-in-out quadratic: smooth acceleration and deceleration
 */
float Display::easeInOutQuad(float t) {
    return t < 0.5f 
        ? 2.0f * t * t 
        : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) / 2.0f;
}

/**
 * Ease-out elastic: bouncy effect (optional, for special occasions)
 */
float Display::easeOutElastic(float t) {
    constexpr float c4 = (2.0f * PI) / 3.0f;
    
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    
    return powf(2.0f, -10.0f * t) * sinf((t * 10.0f - 0.75f) * c4) + 1.0f;
}

/**
 * Draw color sweep (horizontal wipe effect).
 * Call this for the focused row when in edit mode.
 * Enter = left-to-right sweep
 * Exit = right-to-left sweep (reversed)
 */
void Display::drawSweepAnimation(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (!_editMode && _anim.aniSweepProgress >= 1.0f) {
        // Fully transitioned out - just fill with background
        _spr.fillRect(x, y, w, h, Color::BG);
        return;
    }
    
    if (_anim.aniSweepProgress >= 1.0f) {
        // Fully transitioned in - just fill with accent
        _spr.fillRect(x, y, w, h, Color::ACCENT);
        return;
    }
    
    // Apply easing
    float eased = easeInOutQuad(_anim.aniSweepProgress);
    
    constexpr int16_t glowWidth = 20;
    
    if (!_anim.aniSweepExit) {
        // ── Enter edit mode: sweep LEFT → RIGHT ─────────────────────────────────
        int16_t sweepX = (int16_t)(eased * w);
        
        // Draw swept area (accent color)
        if (sweepX > 0) {
            _spr.fillRect(x, y, sweepX, h, Color::ACCENT);
        }
        
        // Draw transition glow at the sweep edge
        if (sweepX < w && sweepX > 0) {
            for (int16_t dx = 0; dx < glowWidth && (sweepX + dx) < w; dx++) {
                uint8_t alpha = 255 - (uint8_t)((float)dx / glowWidth * 255);
                uint16_t col = blendColor(Color::ACCENT, Color::BG, 255 - alpha);
                _spr.drawFastVLine(x + sweepX + dx, y, h, col);
            }
        }
        
        // Draw unswept area (background)
        int16_t remainW = w - sweepX - glowWidth;
        if (remainW > 0) {
            _spr.fillRect(x + sweepX + glowWidth, y, remainW, h, Color::BG);
        }
    } else {
        // ── Enter edit mode: sweep LEFT → RIGHT ─────────────────────────────────
        int16_t sweepX = (int16_t)(eased * w);
        
        // Draw swept area (accent color)
        if (sweepX > 0) {
            _spr.fillRect(x, y, sweepX, h, Color::BG);
        }
        
        // Draw transition glow at the sweep edge
        if (sweepX < w && sweepX > 0) {
            for (int16_t dx = 0; dx < glowWidth && (sweepX + dx) < w; dx++) {
                uint8_t alpha = 255 - (uint8_t)((float)dx / glowWidth * 255);
                uint16_t col = blendColor(Color::BG, Color::ACCENT, 255 - alpha);
                _spr.drawFastVLine(x + sweepX + dx, y, h, col);
            }
        }
        
        // Draw unswept area (background)
        int16_t remainW = w - sweepX - glowWidth;
        if (remainW > 0) {
            _spr.fillRect(x + sweepX + glowWidth, y, remainW, h, Color::ACCENT);
        }
    }
}

/**
 * Get animated value for smooth number transitions.
 * Returns interpolated value between _anim.valueFrom and _anim.valueTo.
 */
float Display::getAnimatedValue(float current) {
    if (!_anim.valueAnimating) {
        return current;
    }
    
    float eased = easeOutCubic(_anim.valueProgress);
    return _anim.valueFrom + (_anim.valueTo - _anim.valueFrom) * eased;
}

// ─── Legacy test ──────────────────────────────────────────────────────────────
void Display::drawTest() {
    // Redirect to splash for now
    _dirty = true;
}
