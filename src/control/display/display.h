/**
 * @file display.h
 * @brief TFT display UI — ST7789 320×240, encoder-only navigation
 *
 * Screen hierarchy:
 *   SPLASH → MAIN_MENU
 *                ├── SETTINGS
 *                └── DSP_LIST
 *                       ├── EFFECT_COMMON
 *                       ├── EFFECT_EQ_COMMON → EFFECT_EQ_GRAPH → EQ_BAND_EDIT
 *                       ├── EFFECT_ISF_COMMON → EFFECT_EQ_COMMON (per preset)
 *                       └── EFFECT_DRC_COMMON → EFFECT_DRC_GRAPH
 *
 * Encoder interaction (no push button):
 *   - Rotate CW/CCW   : navigate focus or change value in edit mode
 *   - Focus on [SELECT] item and hold 1.2 s : auto-confirm (enter / toggle)
 *   - The focused item is always highlighted; actionable items are labelled
 *     [SELECT], [BACK], [EDIT], etc.
 */

#pragma once

#include <TFT_eSPI.h>
#include <Arduino.h>
#include "dsp_types.h"
#include "pin_config.h"
#include "../param/preset_manager.h"

extern volatile bool g_userShutdownRequest;

// ─── Forward declarations ──────────────────────────────────────────────────────
class DspPipeline;
class PresetManager;
class ParametricEQ;
class DspModule;

// ─── Display dimensions ────────────────────────────────────────────────────────
static constexpr int DISP_W = 320;
static constexpr int DISP_H = 240;

// ─── Colour palette (RGB565) ───────────────────────────────────────────────────
namespace Color {
    static constexpr uint16_t BG          = 0x0841; // #080808 near-black
    static constexpr uint16_t PANEL       = 0x10A2; // #101010 card bg
    static constexpr uint16_t BORDER      = 0x2945; // #294545
    static constexpr uint16_t ACCENT      = 0xFFFF; // white
    static constexpr uint16_t ACCENT2     = 0xBFFE; // cyan
    static constexpr uint16_t TEXT        = 0xDEDB; // light grey
    static constexpr uint16_t TEXT_DIM    = 0x7BCF; // dim grey
    static constexpr uint16_t TEXT_FOCUS  = 0xFFFF; // white when focused
    static constexpr uint16_t GREEN       = 0x07E0;
    static constexpr uint16_t RED         = 0xF800;
    static constexpr uint16_t YELLOW      = 0xFFE0;
    static constexpr uint16_t SLIDER_BG   = 0x2945;
    static constexpr uint16_t SLIDER_FILL = 0x07FF;
}

// ─── Screen IDs ───────────────────────────────────────────────────────────────
enum class ScreenID : uint8_t {
    SPLASH,
    MAIN_MENU,
    SETTINGS,
    DSP_LIST,
    EFFECT_COMMON,
    EFFECT_EQ_COMMON,
    EFFECT_EQ_GRAPH,
    EQ_BAND_EDIT,
    EFFECT_ISF_COMMON,
    DRC_CONFIG,
    DRC_BAND_SELECT,
    DRC_BAND_PARAMS,
    KEYBOARD,
};

// ─── Module IDs mirrored for display (subset, add more as needed) ──────────────
enum class DisplayModuleID : uint8_t {
    NONE = 0,
    PRE_GAIN,
    COMPANDER,
    EXCITER,
    DYNAMIC_BASS,
    DYNAMIC_EQ_THRESH,
    DYNAMIC_EQ_LOW,
    DYNAMIC_EQ_HIGH,
    EQ1,
    EQ2,
    LEFT_EQ,
    RIGHT_EQ,
    ISF1,
    ISF2,
    DRC,
    POST_GAIN,
};

// ─── Param descriptor (for generic param screens) ─────────────────────────────
enum class ParamWidget : uint8_t {
    SLIDER,       // slider + value box
    SWITCH,       // toggle switch
    NUMBER_INPUT, // value box only (no slider, e.g. freq in Hz)
    LABEL,        // read-only info row
};

struct ParamDesc {
    const char*  name;
    ParamWidget  widget;
    float        minVal;
    float        maxVal;
    float        step;
    const char*  unit;      // "dB", "ms", "Hz", "%" — shown after value
    float*       valuePtr;  // pointer into live pipeline state
    bool*        boolPtr;   // used for SWITCH widgets (nullptr otherwise)
};

// Short display names for the segmented type selector
static constexpr const char* EQ_FILTER_TYPE_NAMES[] = {
    "PK", "LS", "HS", "LP", "HP", "BP", "NOTCH"
};

// Which filter types use the Gain parameter (LP/HP/BP/NOTCH are gain-less)
static constexpr bool EQ_FILTER_HAS_GAIN[] = {
    true,  // PK
    true,  // LS
    true,  // HS
    false, // LP
    false, // HP
    false, // BP
    false, // NOTCH
};

// ─── EQ band descriptor ───────────────────────────────────────────────────────
struct EqBandDesc {
    bool         enabled;
    EQFilterType type;
    float        freq;
    float        gain;   // ignored when !EQ_FILTER_HAS_GAIN[type]
    float        q;
};

// ─── Keyboard state ───────────────────────────────────────────────────────────
static constexpr uint8_t KB_MAX_LEN = 12;

struct KeyboardState {
    char    buf[KB_MAX_LEN + 1];
    uint8_t len;
    uint8_t cursorCol; // 0-based col in the key grid
    uint8_t cursorRow; // 0-based row
    bool    allowDot;  // false for integer-only inputs (freq)
    float*  target;    // where to write on ENTER
    float   minVal;
    float   maxVal;
    ScreenID returnScreen;
};

// ─── Animation system ─────────────────────────────────────────────────────────
/**
 * Animation state for smooth UI transitions (AAA-game style)
 */
struct AnimationState {
    // Focus transition (when moving between items)
    uint8_t  prevFocusIdx;
    float    focusTransition;     // 0.0 → 1.0 (0 = fully at prev, 1 = fully at current)
    uint32_t focusStartMs;
    
    // Edit mode color sweep (left-to-right wipe when entering edit)
    bool     aniSweepActive;
    bool     aniSweepExit;  // true = right-to-left (exit animation)
    float    aniSweepProgress;   // 0.0 (left) → 1.0 (right)
    uint32_t aniSweepStartMs;
    
    // Value animation (smooth number transitions)
    float    valueFrom;
    float    valueTo;
    float    valueProgress;       // 0.0 → 1.0
    uint32_t valueStartMs;
    bool     valueAnimating;
    
    // Volume animation on splash → main menu transition
    bool     volAnimating;
    float    volFrom;
    float    volTo;
    uint32_t volStartMs;
    static constexpr uint16_t VOL_ANIM_MS = 800;  // slower for volume
    
    // Save preset button animation state
    bool     saveAnimating;
    uint32_t saveAnimStartMs;
    
    // Timing constants
    static constexpr uint16_t FOCUS_ANIM_MS = 200;
    static constexpr uint16_t SWEEP_ANIM_MS = 300;
    static constexpr uint16_t VALUE_ANIM_MS = 150;
    
    void reset() {
        prevFocusIdx = 0;
        focusTransition = 1.0f;
        focusStartMs = 0;
        aniSweepActive = false;
        aniSweepExit = false;
        aniSweepProgress = 0.0f;
        aniSweepStartMs = 0;
        valueFrom = 0.0f;
        valueTo = 0.0f;
        valueProgress = 1.0f;
        valueStartMs = 0;
        valueAnimating = false;
        volAnimating = false;
        volFrom = 0.0f;
        volTo = 0.0f;
        volStartMs = 0;
        saveAnimating = false;
        saveAnimStartMs = 0;
    }
};

// ─── Navigation stack entry ───────────────────────────────────────────────────
struct NavEntry {
    ScreenID         screen;
    DisplayModuleID  module;
    uint8_t          subContext; // e.g. ISF preset index, DRC band index
};

static constexpr uint8_t NAV_STACK_DEPTH = 8;

// ─── Encoder event ────────────────────────────────────────────────────────────
enum class EncoderEvent : int8_t {
    NONE      = 0,
    CW        = +1,
    CCW       = -1,
    SW        = 2,   // single click
    SW_DOUBLE = 3,   // double click  (< 350 ms between presses)
    SW_HOLD3  = 4,   // hold ≥ 3 s   → go to DSP list / back to main
    SW_HOLD5  = 5,   // hold ≥ 5 s   → power off
};

// ─── Display class ────────────────────────────────────────────────────────────
class Display {
public:
    // ── Lifecycle ─────────────────────────────────────────────────────────────
    void init();

    /**
     * Call from controlTask every loop iteration (non-blocking).
     * Handles encoder input, auto-confirm timeout, and redraws dirty regions.
     */
    void update(EncoderEvent enc);

    /**
     * Inject live stats (called from controlTask ~500ms interval).
     */
    void setStats(uint16_t cpuUsageTenths, uint8_t heapPct,
                  uint32_t sampleRate,     bool wifiConnected,
                  bool clockAbsent);

    /**
     * Inject pipeline + preset manager pointers after init.
     */
    void setPipeline(DspPipeline* pipeline, PresetManager* presetMgr);

    // ── Keyboard helper ───────────────────────────────────────────────────────
    void pushKeyboard(float* target, float minVal, float maxVal, bool allowDot, ScreenID returnTo);

    // ── Legacy test ───────────────────────────────────────────────────────────
    void drawTest();

private:
    TFT_eSPI   _tft;
    TFT_eSprite _spr{&_tft};   // full-screen sprite (DMA double-buffer)
    bool        _sprReady = false;

    // ── State ─────────────────────────────────────────────────────────────────
    NavEntry  _navStack[NAV_STACK_DEPTH];
    uint8_t   _navTop = 0;           // index of current top

    uint8_t   _focusIdx   = 0;       // focused item in current screen
    uint8_t   _itemCount  = 0;       // total navigable items in current screen
    bool      _editMode   = false;   // true = encoder changes value, not focus

    uint32_t  _focusHoldStartMs = 0; // millis() when actionable item was focused
    bool      _holdTracking     = false;
    static constexpr uint32_t AUTO_CONFIRM_MS = 1200;

    bool      _dirty = true;         // full redraw needed

    // ── Main menu auto-save debounce ──────────────────────────────────────────
    uint32_t  _mainMenuLastEditMs = 0;
    bool      _mainMenuAutosaveArmed = false;

    KeyboardState _kb;

    // ── Preset slot UI state ──────────────────────────────────────────────────
    bool      _presetSaveMode = false;   // false = load slots, true = save slots
    uint8_t   _presetTargetSlot = 0;

    // ── Live stats (injected) ─────────────────────────────────────────────────
    uint16_t _cpuTenths   = 0;
    uint8_t  _heapPct     = 0;
    uint32_t _sampleRate  = 0;
    bool     _wifiConn    = false;
    bool     _clockAbsent = true;

    // ── Live meter data (for DSP list screen) ─────────────────────────────────
    float    _companderEnv    = 0.0f;  // envelope 0..1
    float    _companderGainDb = 0.0f;  // gain reduction dB (negative)
    float    _drcGainDb[4]    = {0.0f, 0.0f, 0.0f, 0.0f}; // per-band gain reduction
    float    _dynBassAlpha    = 0.0f;  // -1 (clip) to +1 (boost)
    float    _dynBassEnergyDb = -96.0f; // energy in dBFS
    float    _dynEqAlphaLow   = 0.0f;  // 0..1 low EQ contribution
    float    _dynEqAlphaHigh  = 0.0f;  // 0..1 high EQ contribution
    float    _dynEqEnergyDb   = -96.0f; // energy in dBFS
    uint32_t _lastMeterUpdate = 0;
    static constexpr uint32_t METER_UPDATE_INTERVAL_MS = 100; // 10 Hz refresh

    // ── Pipeline (injected) ───────────────────────────────────────────────────
    DspPipeline*   _pipeline   = nullptr;
    PresetManager* _presetMgr  = nullptr;

    // ── Navigation ────────────────────────────────────────────────────────────
    void pushScreen(ScreenID s, DisplayModuleID mod = DisplayModuleID::NONE,
                    uint8_t subCtx = 0);
    void popScreen();
    NavEntry& currentNav();
    ScreenID  currentScreen() const;

    // ── Main update dispatch ───────────────────────────────────────────────────
    void handleEncoder(EncoderEvent enc);
    void handleAutoConfirm();
    void onConfirm();          // called when focused item is "entered"
    void onFocusChanged();     // called after focus moves

    // ── Per-screen draw ────────────────────────────────────────────────────────
    void draw();
    void drawSplash();
    void drawMainMenu();
    void drawSettings();
    void drawDspList();
    void drawPresetSlots();
    void drawEffectCommon();
    void drawEffectEqCommon();
    void drawEffectEqGraph();
    void drawEqBandEdit();
    void drawEffectIsfCommon();
    void drawDrcConfig();
    void drawDrcBandSelect();
    void drawDrcBandParams();
    void drawKeyboard();

    // ── Per-screen item count (for focus wrapping) ─────────────────────────────
    uint8_t getItemCount() const;

    // ── Per-screen encoder-in-edit handling ───────────────────────────────────
    void editDelta(int8_t dir); // called when _editMode && encoder turned

    // ── Common widgets (draw into sprite) ─────────────────────────────────────
    /**
     * Draw a horizontal slider row.
     * @param x,y     top-left of the row
     * @param w       total width
     * @param label   parameter name
     * @param value   current value
     * @param minV    minimum
     * @param maxV    maximum
     * @param unit    unit string ("dB", "ms", …)
     * @param focused highlight this row
     * @param editing show edit cursor on slider thumb
     */
    void drawSliderRow(int16_t x, int16_t y, int16_t w,
                       const char* label, float value,
                       float minV, float maxV, const char* unit,
                       bool focused, bool editing);

    // Overload with decimal control
    void drawSliderRow(int16_t x, int16_t y, int16_t w,
                       const char* label, float value,
                       float minV, float maxV, const char* unit,
                       bool focused, bool editing, uint8_t decimals);

    /**
     * Draw a switch row (boolean toggle).
     */
    void drawSwitchRow(int16_t x, int16_t y, int16_t w,
                       const char* label, bool value,
                       bool focused);

    /**
     * Draw a value-only input row (e.g. for RMS window, freq without slider).
     */
    void drawInputRow(int16_t x, int16_t y, int16_t w,
                      const char* label, float value, const char* unit,
                      bool focused);

    /**
     * Draw a segmented filter-type selector row.
     * Shows all 7 types as pill buttons; active one is highlighted.
     * When focused + editing, encoder CW/CCW cycles through types.
     *
     * @param x,y,w   row geometry
     * @param current currently selected type
     * @param focused row is focused
     * @param editing encoder is changing this value
     */
    void drawFilterTypeRow(int16_t x, int16_t y, int16_t w,
                           EQFilterType current,
                           bool focused, bool editing);

    void drawSideBars(int16_t x, int16_t y, int16_t h);

    /**
     * Draw WiFi status badge.
     */
    void drawWifiBadge(int16_t x, int16_t y);

    /**
     * Draw a nav button (e.g. [BACK], [GRAPH], [SELECT]).
     */
    void drawNavButton(int16_t x, int16_t y, int16_t w, int16_t h,
                       const char* label, bool focused, bool holdProgress,
                       float holdFrac = 0.0f);

    /**
     * Draw EQ frequency-response curve from band descriptors.
     * @param bands   array of band descriptors
     * @param nBands  length of array
     * @param rx,ry   top-left of graph rect
     * @param rw,rh   width / height of graph rect
     */
    void drawEqCurve(const EqBandDesc* bands, uint8_t nBands,
                     int16_t rx, int16_t ry, int16_t rw, int16_t rh);

    /**
     * Draw DRC input/output curve for one band.
     */
    void drawDrcCurve(float threshold, float ratio, float pregain,
                      int16_t rx, int16_t ry, int16_t rw, int16_t rh);

    // ── Splash helpers ────────────────────────────────────────────────────────
    uint32_t _splashStartMs = 0;
    static constexpr uint32_t SPLASH_DURATION_MS = 3000;

    // ── DSP list scroll ───────────────────────────────────────────────────────
    int8_t   _dspListScroll  = 0;   // first visible module index
    static constexpr uint8_t DSP_LIST_VISIBLE = 6;

    // ── Effect common scroll ──────────────────────────────────────────────────
    int8_t   _paramScroll    = 0;   // first visible param row

    // ── ISF sub-state ─────────────────────────────────────────────────────────
    uint8_t  _isfEditPreset  = 0;   // which ISF preset is being edited

    // ── DRC sub-state ─────────────────────────────────────────────────────────
    uint8_t  _drcMode        = 0;   // DRC mode (0-4)
    uint8_t  _drcActiveBand  = 0;   // selected band for params (0-3)
    uint8_t  _drcCfType      = 2;   // crossover filter type

    // ── EQ band edit sub-state ────────────────────────────────────────────────
    EQFilterType _bandEditType = EQFilterType::EQ_FILTER_TYPE_PEAKING;  // type of band being edited

    float _maxRangeParam = 16.0f;
    uint8_t _eqBandCount = 6;

    // ── Keyboard context (for applying value after ENTER) ────────────────────
    NavEntry _kbContext;
    uint8_t  _kbParamIdx = 0;

public:
    struct UiParam {
        const char* name;
        const char* unit;
        float minVal;
        float maxVal;
        float step;
        uint8_t decimals;
    };

private:
    int8_t VISIBLE_ROWS_COMMON = 6;
    int8_t VISIBLE_ROWS_EQ = 4;
    int8_t VISIBLE_ROWS_DRC_PARAMS = 3;
    int8_t VISIBLE_ROWS_DRC_CONFIG = 4;
    int8_t VISIBLE_ISF_PRESET = 3;

    MainMenuParam _mmparam;

    // ── Animation state ───────────────────────────────────────────────────────
    AnimationState _anim;
    
    // ── Animation helpers ─────────────────────────────────────────────────────
    void updateAnimations();
    void startFocusTransition();
    void startValueAnimation(float from, float to);
    void toggleEditMode();
    // ── Easing functions ──────────────────────────────────────────────────────
    static float easeOutCubic(float t);
    static float easeInOutQuad(float t);
    static float easeOutElastic(float t);
    
    // ── Animation drawing helpers ─────────────────────────────────────────────
    void drawSweepAnimation(int16_t x, int16_t y, int16_t w, int16_t h);
    float getAnimatedValue(float current);

    uint8_t getScreenParams(NavEntry nav, UiParam* outParams);
    float getParamValue(NavEntry nav, uint8_t idx);
    void setParamValue(NavEntry nav, uint8_t idx, float val, UiParam* param);
    ParametricEQ* getEqPtr(DisplayModuleID id);
    DspModule* getModulePtr(DisplayModuleID id);

    // ── Utility ───────────────────────────────────────────────────────────────
    static void formatFloat(char* buf, uint8_t bufLen,
                            float val, uint8_t decimals);
    static uint16_t blendColor(uint16_t a, uint16_t b, uint8_t t); // t 0-255
};
