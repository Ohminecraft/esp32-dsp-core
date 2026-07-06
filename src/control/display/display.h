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
 *     [SELECT], [BACK], [GRAPH], etc.
 */

#pragma once

#include <TFT_eSPI.h>
#include <Arduino.h>
#include "dsp_types.h"
#include "pin_config.h"
#include "../param/preset_manager.h"
#include "../param/settings_manager.h"
#include "battery_monitor.h"
#include "config.h" // For MAX_PRESET_SLOTS and DSP_MODULE_COUNT

extern volatile bool g_userShutdownRequest;
extern volatile bool g_timerShutdownTriggered;
extern volatile bool g_softLatchPinIsAvailable;
extern volatile uint32_t g_shutdownCountdown;
extern volatile bool g_shutdownButtonIsHolding;

// ─── Forward declarations ──────────────────────────────────────────────────────
class DspPipeline;
class PresetManager;
class ParametricEQ;
class DspModule;
class IndexSelectableFilter;

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

// ─── Connection status enum ───────────────────────────────────────
enum class ConnectStatus : uint8_t {
    WIFI_DISABLE,
    WIFI_ENABLE,
    WIFI_WS_CONNECTED,
    SERIAL_CONNECTED,
};

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
    SW        = 2,   // single click (confirmed on release)
    SW_DOUBLE = 3,   // double click  (< 350 ms between presses)
    SW_HOLD3  = 4,   // hold ≥ 3 s   → go to DSP list / back to main
    SW_HOLD5  = 5,   // hold ≥ 5 s   → power off
    SW_PRESS  = 6,   // button went down (not debounce-confirmed) — for immediate arm
};

// ─── Animation system ─────────────────────────────────────────────────────────
/**
 * Animation state for smooth UI transitions.
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
    float    volDisplay;       // animated volume for display (0.0f - 32.0f)
    uint32_t volStartMs;
    static constexpr uint16_t VOL_ANIM_MS = 1200;  // slower for volume with deceleration
    
    // ── Preset slot sweep (entry/exit when changing target) ─────────────────
    bool     presetSweepActive;
    int8_t   presetSweepSlot;     // new slot being swept
    int8_t   presetPrevSlot;      // old slot being exited (-1 = none)
    bool     presetSweepPhase2;   // false = phase 1 (exit old), true = phase 2 (entry new)
    float    presetSweepProgress;
    uint32_t presetSweepStartMs;
    
    // ── Module (effect) toggle sweep on DSP list ────────────────────────────
    bool     modSweepActive;
    bool     modSweepExit;        // true = exit (disable), false = entry (enable)
    float    modSweepProgress;
    uint32_t modSweepStartMs;
    int8_t   modSweepIdx;         // which module index in MODULE_LIST (-1 = none)
    
    // ── Save preset button animation ────────────────────────────────────────
    bool     saveSweepActive;
    bool     saveSweepPhase2;     // false = entry, true = exit after entry
    float    saveSweepProgress;
    uint32_t saveSweepStartMs;
    uint16_t saveSweepFinalBg;    // BG color to keep after animation completes
    
    // ── Final BG colors after animation completes ──────────────────────────
    // For presets: 0xFFFF = accent (is target), 0x0841 = BG (not target)
    // For modules: 0x07E0 = green (enabled), 0x0841 = BG (disabled)
    uint16_t presetSweepFinishedBg[MAX_PRESET_SLOTS];
    
    // Timing constants
    static constexpr uint16_t FOCUS_ANIM_MS   = 200;
    static constexpr uint16_t SWEEP_ANIM_MS   = 300;
    static constexpr uint16_t VALUE_ANIM_MS   = 400;
    static constexpr uint16_t PRESET_SWEEP_MS = 350;
    static constexpr uint16_t MOD_SWEEP_MS    = 300;
    static constexpr uint16_t SAVE_SWEEP_MS   = 400;
    
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
        volDisplay = 0.0f;
        volStartMs = 0;
        // Preset sweep
        presetSweepActive = false;
        presetSweepSlot = -1;
        presetPrevSlot = -1;
        presetSweepPhase2 = false;
        presetSweepProgress = 0.0f;
        presetSweepStartMs = 0;
        // Module sweep
        modSweepActive = false;
        modSweepExit = false;
        modSweepProgress = 0.0f;
        modSweepStartMs = 0;
        modSweepIdx = -1;
        // Save sweep
        saveSweepActive = false;
        saveSweepPhase2 = false;
        saveSweepProgress = 0.0f;
        saveSweepStartMs = 0;
        saveSweepFinalBg = 0x0841; // Color::BG
        // Clear finished BG arrays
        for (uint8_t i = 0; i < MAX_PRESET_SLOTS; i++) presetSweepFinishedBg[i] = Color::BG;
    }
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
                  uint32_t sampleRate,     ConnectStatus connectStatus,
                  bool clockAbsent);

    /**
     * Inject pipeline + preset manager pointers after init.
     */
    void setPipeline(DspPipeline* pipeline, PresetManager* presetMgr);

    /**
     * Inject settings manager pointer after init.
     */
    void setSettings(SettingsManager* settingsMgr);

    // ── Keyboard helper ───────────────────────────────────────────────────────
    void pushKeyboard(float* target, float minVal, float maxVal, bool allowDot, ScreenID returnTo);


    void setBattery(BatteryMonitor* b) { _battery = b; }

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
    ConnectStatus _connectStatus = ConnectStatus::WIFI_DISABLE;
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
    static constexpr uint32_t METER_UPDATE_INTERVAL_MS = 40; // 25 Hz refresh

    // ── Pipeline (injected) ───────────────────────────────────────────────────
    DspPipeline*     _pipeline    = nullptr;
    PresetManager*   _presetMgr   = nullptr;
    SettingsManager* _settingsMgr = nullptr;

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
    void drawSliderRow(int16_t x, int16_t y, int16_t w,
                       const char* label, float value,
                       float minV, float maxV, const char* unit,
                       bool focused, bool editing);

    void drawSliderRow(int16_t x, int16_t y, int16_t w,
                       const char* label, float value,
                       float minV, float maxV, const char* unit,
                       bool focused, bool editing, uint8_t decimals);

    void drawSwitchRow(int16_t x, int16_t y, int16_t w,
                       const char* label, bool value,
                       bool focused);

    void drawInputRow(int16_t x, int16_t y, int16_t w,
                      const char* label, float value, const char* unit,
                      bool focused);

    void drawInputRow(int16_t x, int16_t y, int16_t w,
                      const char* label, float value, const char* unit,
                      bool focused, uint8_t decimals);

    void drawFilterTypeRow(int16_t x, int16_t y, int16_t w,
                           EQFilterType current,
                           bool focused, bool editing);

    void drawSideBars(int16_t x, int16_t y, int16_t h);
    void drawConnectionBadge(int16_t x, int16_t y);

    void drawNavButton(int16_t x, int16_t y, int16_t w, int16_t h,
                       const char* label, bool focused, bool holdProgress,
                       float holdFrac = 0.0f);

    void drawEqCurve(const EqBandDesc* bands, uint8_t nBands,
                     int16_t rx, int16_t ry, int16_t rw, int16_t rh, float pregainDb = 0.0f);

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
    EQFilterType _bandEditType = EQFilterType::EQ_FILTER_TYPE_PEAKING;

    float _maxRangeParam = 16.0f;
    uint8_t _eqBandCount = 6;

    // ── Keyboard context (for applying value after ENTER) ────────────────────
    NavEntry _kbContext;
    uint8_t  _kbParamIdx = 0;

public:
    bool wifiOnOffTriggered = false; // set to true when user toggles WiFi in settings
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
    bool _mainmenufocus = false;

    // ── Animation state ───────────────────────────────────────────────────────
    AnimationState _anim;


    BatteryMonitor* _battery = nullptr;
    uint8_t  _mainMenuTab          = 0;
    uint8_t  _mainMenuActiveParam  = 0;   // 0=Vol,1=Bass,2=Mid,3=Treble
    bool     _mmTabReturnViaHold   = false;
    bool     _powerOffUserRequest  = false;
    bool     _powerOffScreenActive = false;
    uint8_t  _screenFadeOutValue   = 255; // 0 = fully black, 255 = fully visible
    uint32_t _powerOffStartMs      = 0;
    uint32_t _lastBatteryDrawMs    = 0;
    bool     _settingsWifi         = false;
    bool     _settingsTrigger      = false;
    std::function<void()> onBtKick;

    // ── Global hold tracking (3s = Settings, 5s = Shutdown) ──────────────────
    bool     _globalHoldArmed    = false;
    uint32_t _globalHoldStartMs  = 0;

    // ── Main menu tab-return hold (hold SW 500 ms to go back to tab bar) ─────
    bool     _mmTabReturnArmed   = false;
    uint32_t _mmTabReturnStartMs = 0;
    static constexpr uint32_t TAB_RETURN_HOLD_MS = 500;

    float   _isfLevelDb[2];
    uint8_t _isfActiveA[2];
    uint8_t _isfActiveB[2];

    void drawBatteryWidget(int16_t x, int16_t y);
    void drawPowerOffScreen();
    
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
    void drawSweepAnimation(int16_t x, int16_t y, int16_t w, int16_t h, bool exit, float progress);
    void drawSweepAnimation(int16_t x, int16_t y, int16_t w, int16_t h, bool exit, float progress, uint16_t entryColor);
    float getAnimatedValue(float current);
    void getFocusColors(bool, uint16_t&, uint16_t&, uint16_t&);

    uint8_t getScreenParams(NavEntry nav, UiParam* outParams);
    float getParamValue(NavEntry nav, uint8_t idx);
    void setParamValue(NavEntry nav, uint8_t idx, float val, UiParam* param);
    ParametricEQ* getEqPtr(DisplayModuleID id);
    IndexSelectableFilter* getIsfPtr(DisplayModuleID id);
    DspModule* getModulePtr(DisplayModuleID id);

    // ── Utility ───────────────────────────────────────────────────────────────
    static void formatFloat(char* buf, uint8_t bufLen,
                            float val, uint8_t decimals, const char* unit = nullptr);
    static uint16_t blendColor(uint16_t a, uint16_t b, uint8_t t);
};