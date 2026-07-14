/**
 * @file battery_monitor.cpp
 * @brief INA226-based battery monitor implementation.
 *
 * Uses RobTillaart/INA226 for the I2C layer and calibration-register math.
 * Configuration is kept equivalent to the original driver:
 *   AVG = 16 samples, VBUS CT = 1.1 ms, VSHUNT CT = 1.1 ms, mode = continuous
 *   (continuous shunt+bus is the library's default mode, no need to set it again)
 *
 * Calibration: uses setMaxCurrentShunt(maxCurrent, shuntOhm) instead of the
 * hand-written CAL = 0.00512 / (CurrentLSB × Rshunt) formula — the library
 * computes it for you, with normalization to reduce truncation/rounding
 * error, and returns an error code if shunt × maxCurrent would exceed the
 * 81.9 mV the INA226 can actually measure.
 */

#include "battery_monitor.h"
#include "../../utils/debug_log.h"
#include "pin_config.h"
#include <Wire.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <Arduino.h>
#include <cmath>
#include <algorithm>
#include <cstring>

// ── Li-Ion OCV → SoC lookup table (per-cell, 11 points) ─────────────────────
// Source: typical Li-Ion discharge curve
static constexpr uint8_t OCV_TABLE_SIZE = 11;
static constexpr float OCV_V[OCV_TABLE_SIZE] = {
    3.00f, 3.20f, 3.40f, 3.55f, 3.65f,
    3.75f, 3.80f, 3.85f, 3.90f, 4.00f, 4.20f
};
static constexpr float OCV_SOC[OCV_TABLE_SIZE] = {
    0.00f, 0.05f, 0.10f, 0.20f, 0.30f,
    0.45f, 0.55f, 0.65f, 0.75f, 0.90f, 1.00f
};

// ─────────────────────────────────────────────────────────────────────────────
// Calibration — recomputed whenever config changes
// ─────────────────────────────────────────────────────────────────────────────

bool BatteryMonitor::configure() {
    // AVG = 16, VBUS CT = VSHUNT CT = 1.1 ms (same as original 0x4527 config).
    // The library's default mode is already ShuntBusContinuous (7), so no
    // need to set it explicitly.
    _ina.setAverage(INA226_16_SAMPLES);
    _ina.setBusVoltageConversionTime(INA226_1100_us);
    _ina.setShuntVoltageConversionTime(INA226_1100_us);

    float rShuntOhm = (float)_cfg.shuntMOhm / 1000.0f;

    // The INA226 shunt ADC only covers ±81.92 mV (hardware limit).
    // The original code's "maxCurrent = 20 A" target was never physically
    // reachable with a 100 mΩ shunt: 20 A x 0.1 Ω = 2000 mV, way above
    // 81.92 mV — the hand-rolled driver just never validated that, so the
    // bug stayed silent (real measurable range was capped at ~0.82 A all
    // along). RobTillaart's setMaxCurrentShunt() correctly rejects it with
    // INA226_ERR_SHUNTVOLTAGE_HIGH.
    //
    // Derive the actual max current the configured shunt allows, with a
    // ~10% margin under the hardware ceiling, and cap at 20 A (the
    // application's intended upper bound) in case shuntMOhm is later
    // changed to something very small.
    constexpr float INA226_MAX_SHUNT_V = 0.073f; // 73 mV, ~10% margin under 81.92 mV
    float maxCurrent = std::min(20.0f, INA226_MAX_SHUNT_V / rShuntOhm);

    int err = _ina.setMaxCurrentShunt(maxCurrent, rShuntOhm);
    if (err != INA226_ERR_NONE) {
        LOG_ERROR("BATT", "Ina226 calibration failed, err=0x%04X", err);
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// begin()
// ─────────────────────────────────────────────────────────────────────────────

bool BatteryMonitor::begin(uint8_t sdaPin, uint8_t sclPin) {
    _sdaPin = sdaPin;
    _sclPin = sclPin;

    #if defined(BATT_CHARGING) && BATT_CHARGING > 0
        pinMode(BATT_CHARGING, INPUT);
    #endif
    Wire.begin(sdaPin, sclPin);

    // _ina.begin() only checks whether the address ACKs on the bus.
    // Also check the Manufacturer ID (0x5449) to make sure it's really an INA226.
    if (!_ina.begin() || _ina.getManufacturerID() != 0x5449) {
        _status.inaPresent = false;
        LOG_ERROR("BATT", "Unable to detect Ina226");
        return false;
    }

    _status.inaPresent = true;

    loadConfig();
    if (!configure()) {
        // Chip is still present, just current/power readings will be wrong
        // until calibration is fixed (e.g. by setting a more sane shuntMOhm).
        LOG_ERROR("BATT", "Ina226 found but calibration failed");
    }

    // Restore coulomb counter from NVS
    nvs_handle_t nvs;
    if (nvs_open(NVS_BAT_NS, NVS_READONLY, &nvs) == ESP_OK) {
        uint32_t rawBits = 0;
        if (nvs_get_u32(nvs, NVS_KEY_COULOMBS, &rawBits) == ESP_OK) {
            std::memcpy(&_consumedMah, &rawBits, sizeof(_consumedMah));
            if (!std::isfinite(_consumedMah) || _consumedMah < 0.0f)
                _consumedMah = 0.0f;
        }
        nvs_close(nvs);
    }

    // Clamp against the *current* config in case cellMah was edited between
    // the last save and this boot (e.g. user changed pack capacity) — without
    // this, a restored _consumedMah larger than the new totalMah would make
    // updateSoC()'s ccSoc clamp to 0 forever until the pack hits LIION_CELL_MAX_V.
    float totalMahAtBoot = (float)_cfg.cellMah;
    if (_consumedMah > totalMahAtBoot) _consumedMah = totalMahAtBoot;

    // Restore learned (measured) full-cycle capacity, used for SOH%.
    // Defaults to the nominal cellMah until at least one full charge→empty
    // cycle has completed with no interruption (see updateSoC()).
    if (nvs_open(NVS_BAT_NS, NVS_READONLY, &nvs) == ESP_OK) {
        uint32_t rawBits = 0;
        if (nvs_get_u32(nvs, NVS_KEY_LEARN_CAP, &rawBits) == ESP_OK) {
            std::memcpy(&_learnedCapacityMah, &rawBits, sizeof(_learnedCapacityMah));
        }
        nvs_close(nvs);
    }
    if (!std::isfinite(_learnedCapacityMah) ||
        _learnedCapacityMah < 0.2f * totalMahAtBoot ||
        _learnedCapacityMah > 1.5f * totalMahAtBoot) {
        // No sane learned value yet (first boot, or cellMah changed a lot) —
        // seed with the nominal capacity so SOH starts at 100% instead of 0%.
        _learnedCapacityMah = totalMahAtBoot;
    }

    _lastUpdateMs  = millis();
    _lastNvsSaveMs = millis();
    LOG_INFO("BATT", "Ina226 was intialized successfully");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// update() — call at ≥ 1 Hz
// ─────────────────────────────────────────────────────────────────────────────

void BatteryMonitor::update() {
    if (!_status.inaPresent) return;

    uint32_t now = millis();
    if (now - _lastUpdateMs < UPDATE_INTERVAL_MS) return;
    float dtS = (float)(now - _lastUpdateMs) / 1000.0f;
    _lastUpdateMs = now;

    // ── Read directly through the library (sign + LSB scaling handled for us) ──
    _status.busVoltage = _ina.getBusVoltage();    // V
    _status.currentMa  = _ina.getCurrent_mA();    // mA — discharge positive, charge negative
    _status.powerMw    = _ina.getPower_mW();      // mW (measured by the chip, not a manual V×I)

    // NOTE: the sign convention (positive = discharge) depends on shunt wiring
    // polarity. Double-check on real hardware after flashing — if it's
    // reversed, swap the V+/V- shunt connections or flip the sign of
    // currentMa here.

    // ── Coulomb counter (only track discharge; positive current = discharging) ─
    if (_status.currentMa > 0.0f) {
        float dischargedMah = _status.currentMa * dtS / 3600.0f;
        _consumedMah += dischargedMah;
        float totalMah = (float)(_cfg.cellMah);
        if (_consumedMah > totalMah) _consumedMah = totalMah;

        // Runs alongside _consumedMah but is never clamped to totalMah —
        // it's only meaningful once a full cycle completes (see updateSoC()),
        // at which point its raw value IS the measured capacity for that cycle.
        if (_cycleArmed) _cycleAccumMah += dischargedMah;
    }

    // ── SoC ──────────────────────────────────────────────────────────────────
    // (may re-anchor _consumedMah at the OCV table's top/bottom — see below)
    updateSoC();
    _status.consumedMah = _consumedMah;

    // ── State machine ─────────────────────────────────────────────────────────
    float packMinV  = LIION_CELL_MIN_V  * _cfg.cellS;
    float packWarnV = LIION_CELL_WARN_V * _cfg.cellS;

    BatteryState newState;
    if (_status.currentMa < 0.0f || digitalRead(BATT_CHARGING)) {          // >0.0 mA flowing in = charging
        newState = BatteryState::CHARGING;
    } else if (_status.busVoltage < packMinV) {
        newState = BatteryState::CRITICAL;
    } else if (_status.busVoltage < packWarnV) {
        newState = BatteryState::WARNING;
    } else {
        newState = BatteryState::NORMAL;
    }

    _status.state = newState;

    // Any charge activity — even a partial charge that never reaches
    // LIION_CELL_MAX_V — invalidates the in-progress full-cycle capacity
    // measurement, because charge current bypasses the shunt and can't be
    // subtracted back out. Disarm here; updateSoC() re-arms cleanly the
    // next time the pack actually reaches full.
    if (newState == BatteryState::CHARGING) {
        _cycleArmed = false;
    }

    // Fire shutdown callback once on CRITICAL
    if (newState == BatteryState::CRITICAL && !_criticalFired) {
        _criticalFired = true;
        if (_onCritical) _onCritical();
    }
    if (newState != BatteryState::CRITICAL) {
        _criticalFired = false;  // re-arm if voltage recovers (e.g. charger connected)
    }

    // ── Periodic NVS save ─────────────────────────────────────────────────────
    if (now - _lastNvsSaveMs >= NVS_SAVE_INTERVAL_MS) {
        _lastNvsSaveMs = now;
        nvs_handle_t nvs;
        if (nvs_open(NVS_BAT_NS, NVS_READWRITE, &nvs) == ESP_OK) {
            uint32_t rawBits;
            std::memcpy(&rawBits, &_consumedMah, sizeof(rawBits));
            nvs_set_u32(nvs, NVS_KEY_COULOMBS, rawBits);
            nvs_commit(nvs);
            nvs_close(nvs);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SoC — blend OCV estimate + coulomb counter
// ─────────────────────────────────────────────────────────────────────────────

float BatteryMonitor::ocvToSoc(float cellV) const {
    if (cellV <= OCV_V[0])                   return OCV_SOC[0];
    if (cellV >= OCV_V[OCV_TABLE_SIZE - 1])  return OCV_SOC[OCV_TABLE_SIZE - 1];
    for (uint8_t i = 1; i < OCV_TABLE_SIZE; i++) {
        if (cellV <= OCV_V[i]) {
            float t = (cellV - OCV_V[i-1]) / (OCV_V[i] - OCV_V[i-1]);
            return OCV_SOC[i-1] + t * (OCV_SOC[i] - OCV_SOC[i-1]);
        }
    }
    return 1.0f;
}

void BatteryMonitor::updateSoC() {
    float cellV    = _status.busVoltage / (float)_cfg.cellS;
    float totalMah = (float)_cfg.cellMah;

    // Anchor full/empty against the real-world voltage limits (charger
    // cutoff / hard shutdown threshold) rather than the raw OCV table
    // extremes (3.00 V / 4.20 V), since a real pack's charger will stop at
    // LIION_CELL_MAX_V and the system shuts down at LIION_CELL_MIN_V long
    // before the table's edges are ever reached. At these points the
    // voltage reading is unambiguous regardless of instantaneous current,
    // so we re-anchor the coulomb counter instead of blending — otherwise
    // SoC could drift below 100% (or above 0%) forever, since _consumedMah
    // only ever moves one direction without an explicit reset.
    if (cellV >= LIION_CELL_MAX_V) {
        _consumedMah = 0.0f;
        _status.soc  = 1.0f;
        _status.remainingMah = totalMah;

        // Confirmed full charge — safe to start (or restart) a full-cycle
        // capacity measurement from here.
        _cycleArmed    = true;
        _cycleAccumMah = 0.0f;

        _status.learnedCapacityMah = _learnedCapacityMah;
        _status.stateOfHealthPct   = (totalMah > 0.0f) ? (_learnedCapacityMah / totalMah) * 100.0f : 0.0f;
        return;
    }
    if (cellV <= LIION_CELL_MIN_V) {
        _consumedMah = totalMah;
        _status.soc  = 0.0f;
        _status.remainingMah = 0.0f;

        // If we made it here on an uninterrupted discharge from a confirmed
        // full charge, _cycleAccumMah is a real measurement of this cycle's
        // capacity. Fold it into the learned estimate with an EMA so a
        // single noisy cycle (voltage sag under a current spike right at
        // the threshold, etc.) can't swing SOH wildly.
        if (_cycleArmed &&
            _cycleAccumMah > 0.2f * totalMah &&
            _cycleAccumMah < 1.5f * totalMah) {
            constexpr float LEARN_ALPHA = 0.2f; // weight given to each new full cycle
            _learnedCapacityMah = (_learnedCapacityMah <= 0.0f)
                ? _cycleAccumMah
                : LEARN_ALPHA * _cycleAccumMah + (1.0f - LEARN_ALPHA) * _learnedCapacityMah;
            persistLearnedCapacity();
        }
        _cycleArmed = false;

        _status.learnedCapacityMah = _learnedCapacityMah;
        _status.stateOfHealthPct   = (totalMah > 0.0f) ? (_learnedCapacityMah / totalMah) * 100.0f : 0.0f;
        return;
    }

    float ocvSoc = ocvToSoc(cellV);

    // Coulomb counter SoC
    float ccSoc = (totalMah > 0.0f)
                  ? 1.0f - (_consumedMah / totalMah)
                  : 0.0f;
    ccSoc = std::max(0.0f, std::min(1.0f, ccSoc));

    // When at rest (|I| < 50 mA), OCV is reliable → weight it heavily.
    // Under load, trust coulomb counter more.
    //
    // Only *discharge* current should pull the estimate toward ccSoc.
    // _consumedMah is not decremented while charging (see topology note
    // in the header — charge current bypasses the shunt), so during a
    // charge cycle ccSoc is a stale, frozen number. Weighting it by
    // fabsf(currentMa) — which is large while charging too — used to
    // make the blended SoC sit near that frozen value and barely move
    // until the pack hit LIION_CELL_MAX_V. Clamping current to >=0 here
    // means charging relies on the rising OCV instead, which actually
    // tracks progress.
    float loadFactor = std::min(1.0f, std::max(0.0f, _status.currentMa) / 500.0f);
    _status.soc = ocvSoc * (1.0f - loadFactor) + ccSoc * loadFactor;
    _status.soc = std::max(0.0f, std::min(1.0f, _status.soc));

    // Remaining capacity derived from the blended SoC rather than a plain
    // totalMah - _consumedMah subtraction, so it inherits the same OCV
    // correction (and won't drift the way a pure coulomb count would).
    _status.remainingMah = totalMah * _status.soc;

    _status.learnedCapacityMah = _learnedCapacityMah;
    _status.stateOfHealthPct   = (totalMah > 0.0f) ? (_learnedCapacityMah / totalMah) * 100.0f : 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Config persistence
// ─────────────────────────────────────────────────────────────────────────────

bool BatteryMonitor::loadConfig() {
    nvs_handle_t nvs;
    if (nvs_open(NVS_BAT_NS, NVS_READONLY, &nvs) != ESP_OK) return false;

    uint8_t  cellS     = _cfg.cellS;
    uint32_t cellMah   = _cfg.cellMah;
    uint32_t shuntMOhm = _cfg.shuntMOhm;

    nvs_get_u8 (nvs, NVS_KEY_CELL_S,     &cellS);
    nvs_get_u32(nvs, NVS_KEY_CELL_MAH,   &cellMah);
    nvs_get_u32(nvs, NVS_KEY_SHUNT_MOHM, &shuntMOhm);
    nvs_close(nvs);

    _cfg.cellS      = cellS;
    _cfg.cellMah    = cellMah;
    _cfg.shuntMOhm  = shuntMOhm;
    return true;
}

void BatteryMonitor::saveConfig() {
    nvs_handle_t nvs;
    if (nvs_open(NVS_BAT_NS, NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_u8 (nvs, NVS_KEY_CELL_S,     _cfg.cellS);
    nvs_set_u32(nvs, NVS_KEY_CELL_MAH,   _cfg.cellMah);
    nvs_set_u32(nvs, NVS_KEY_SHUNT_MOHM, _cfg.shuntMOhm);
    uint32_t rawBits;
    std::memcpy(&rawBits, &_consumedMah, sizeof(rawBits));
    nvs_set_u32(nvs, NVS_KEY_COULOMBS, rawBits);
    nvs_commit(nvs);
    nvs_close(nvs);
}

void BatteryMonitor::persistLearnedCapacity() {
    // Called only on a completed full cycle (rare — normal usage is a
    // handful of full charge/discharge cycles a day at most), so writing
    // immediately instead of batching with the 30 s periodic save is fine
    // and doesn't add meaningful flash wear.
    nvs_handle_t nvs;
    if (nvs_open(NVS_BAT_NS, NVS_READWRITE, &nvs) != ESP_OK) return;
    uint32_t rawBits;
    std::memcpy(&rawBits, &_learnedCapacityMah, sizeof(rawBits));
    nvs_set_u32(nvs, NVS_KEY_LEARN_CAP, rawBits);
    nvs_commit(nvs);
    nvs_close(nvs);
}

void BatteryMonitor::applyConfig() {
    // If the user just lowered cellMah, an already-restored _consumedMah
    // from the old (larger) capacity could exceed the new totalMah, which
    // would pin ccSoc at 0 inside updateSoC() until the next full-charge
    // re-anchor. Clamp here too, same reasoning as in begin().
    float totalMah = (float)_cfg.cellMah;
    if (_consumedMah > totalMah) _consumedMah = totalMah;

    if (!configure()) {
        LOG_ERROR("BATT", "Ina226 re-calibration failed");
    }
    saveConfig();
}

void BatteryMonitor::resetCoulombCounter() {
    _consumedMah = 0.0f;
    saveConfig();
}