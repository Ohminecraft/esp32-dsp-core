/**
 * @file battery_monitor.h
 * @brief INA226-based battery monitor with coulomb counter and NVS persistence.
 *
 * Uses the RobTillaart/INA226 library (https://github.com/RobTillaart/INA226)
 * for the I2C/register layer instead of a hand-rolled driver — it already
 * handles calibration error codes, conversion-ready polling, and rounding
 * for the calibration register.
 *
 * Topology note: charging current bypasses the shunt (common in pack designs),
 * so the coulomb counter only tracks discharge current (I > 0 drawn from pack).
 *
 * Cell chemistry: Li-Ion  (3.0 V min / 4.2 V max per cell)
 * INA226 address: 0x40
 * Shunt:          0.1 Ω (100 mΩ)
 */

#pragma once

#include <cstdint>
#include <functional>
#include <INA226.h>

// ── I2C address ──────────────────────────────────────────────────────────────
static constexpr uint8_t INA226_ADDR = 0x40;

// ── Li-Ion cell limits ───────────────────────────────────────────────────────
static constexpr float LIION_CELL_MIN_V  = 3.4f;   // hard cutoff → shutdown
static constexpr float LIION_CELL_MAX_V  = 4.2f;
static constexpr float LIION_CELL_WARN_V = 3.5f;   // warning threshold — must stay above LIION_CELL_MIN_V,
                                                    // otherwise the WARNING state in the update() state
                                                    // machine is unreachable (CRITICAL always fires first)

// ── NVS keys ─────────────────────────────────────────────────────────────────
static constexpr const char* NVS_BAT_NS         = "bat_cfg";
static constexpr const char* NVS_KEY_CELL_S     = "cell_s";      // uint8
static constexpr const char* NVS_KEY_CELL_MAH   = "cell_mah";    // uint32
static constexpr const char* NVS_KEY_SHUNT_MOHM = "shunt_mohm";  // uint32 (mΩ)
static constexpr const char* NVS_KEY_COULOMBS   = "coulombs";    // float (mAh consumed)
static constexpr const char* NVS_KEY_LEARN_CAP  = "learn_cap";   // float (learned full-cycle capacity, mAh)

// ── Config struct (persisted to NVS) ─────────────────────────────────────────
struct BatteryConfig {
    uint8_t  cellS     = 5;      // series cell count (1–6)
    uint32_t cellMah    = 3000;  // capacity per cell in mAh
    uint32_t shuntMOhm = 100;    // shunt resistance in mΩ (default 100 = 0.1 Ω)
};

// ── Runtime state ─────────────────────────────────────────────────────────────
enum class BatteryState : uint8_t {
    UNKNOWN   = 0,
    NORMAL    = 1,
    WARNING   = 2,   // voltage < cell_warn × cellS
    CRITICAL  = 3,   // voltage < cell_min  × cellS → triggers shutdown
    CHARGING  = 4,   // current negative (if shunt sees charge current)
};

struct BatteryStatus {
    float        busVoltage    = 0.0f;  // V  — pack voltage
    float        currentMa     = 0.0f;  // mA — discharge positive, charge negative
    float        powerMw       = 0.0f;  // mW
    float        soc           = 0.0f;  // 0.0–1.0
    float        consumedMah   = 0.0f;  // mAh discharged since last full
    float        remainingMah  = 0.0f;  // mAh left = cellMah × soc (blended OCV+CC, not a raw subtraction)
    float        learnedCapacityMah = 0.0f; // measured full-cycle capacity (EMA over completed cycles)
    float        stateOfHealthPct   = 0.0f; // learnedCapacityMah / cellMah × 100, 0 until a full cycle completes
    BatteryState state         = BatteryState::UNKNOWN;
    bool         inaPresent    = false;
};

// ── BatteryMonitor class ──────────────────────────────────────────────────────
class BatteryMonitor {
public:
    BatteryMonitor() : _ina(INA226_ADDR) {}

    /**
     * @brief Initialise INA226 (via lib), load NVS config, restore coulomb counter.
     * @param sdaPin  I2C SDA GPIO
     * @param sclPin  I2C SCL GPIO
     * @return true if INA226 was found and configured.
     */
    bool begin(uint8_t sdaPin, uint8_t sclPin);

    /** Call from a FreeRTOS task or periodic timer (≥1 Hz recommended). */
    void update();

    /** Save current config + coulomb counter to NVS. */
    void saveConfig();

    /** Load config from NVS. Returns false if no config found (uses defaults). */
    bool loadConfig();

    // ── Accessors ────────────────────────────────────────────────────────────
    const BatteryStatus& getStatus()  const { return _status; }
    const BatteryConfig& getConfig()  const { return _cfg; }
    BatteryConfig&       editConfig()       { return _cfg; }

    /** Apply edited config (recalculates calibration via lib). */
    void applyConfig();

    /**
     * @brief Register a shutdown callback.
     * Called once when state transitions to CRITICAL.
     * Caller should set g_userShutdownRequest = true inside cb.
     */
    void onCritical(std::function<void()> cb) { _onCritical = cb; }

    /** Reset coulomb counter to full (call after charging). */
    void resetCoulombCounter();

    /** Check if the battery monitor is present (INA226 detected). */
    bool isPresent() const { return _status.inaPresent; }

private:
    bool  configure();             // sets AVG/CT + calibration via INA226 lib
    void  updateSoC();              // OCV-based SoC estimate blended with coulomb counter
    float ocvToSoc(float cellV) const; // Li-Ion OCV → SoC lookup
    void  persistLearnedCapacity(); // saves _learnedCapacityMah to NVS (called on each completed full cycle)

    INA226         _ina;            // RobTillaart/INA226 driver instance

    BatteryConfig  _cfg;
    BatteryStatus  _status;

    float          _consumedMah   = 0.0f;  // coulomb counter accumulator
    uint32_t       _lastUpdateMs  = 0;
    uint32_t       _lastNvsSaveMs = 0;
    bool           _criticalFired = false;

    // Full-cycle capacity learning (SOH). _cycleArmed is only true while the
    // current discharge run started from a confirmed full charge (top OCV
    // anchor) with no charging activity since — see update()/updateSoC().
    float          _learnedCapacityMah = 0.0f;
    float          _cycleAccumMah      = 0.0f;
    bool           _cycleArmed         = false;

    uint8_t        _sdaPin = 21;
    uint8_t        _sclPin = 22;

    std::function<void()> _onCritical;

    static constexpr uint32_t NVS_SAVE_INTERVAL_MS = 30000; // save every 30 s
    static constexpr uint32_t UPDATE_INTERVAL_MS    = 1000;  // INA226 poll rate
};