/**
 * @file battery_monitor.cpp
 * @brief INA226-based battery monitor implementation.
 *
 * INA226 config used:
 *   AVG = 16 samples, VBUS CT = 1.1 ms, VSHUNT CT = 1.1 ms, mode = continuous
 *   CONFIG register = 0x4527
 *
 * Calibration formula (from datasheet):
 *   CAL = 0.00512 / (CurrentLSB × Rshunt)
 *   CurrentLSB = maxCurrent / 32768
 *   We target maxCurrent = 20 A → CurrentLSB = 610.35 µA ≈ 0.0006104 A
 *
 * Shunt voltage LSB = 2.5 µV (fixed by INA226 hardware)
 */

#include "battery_monitor.h"
#include "../../utils/debug_log.h"
#include <Wire.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <Arduino.h>
#include <cmath>
#include <algorithm>

// ── INA226 CONFIG register value ─────────────────────────────────────────────
// Bits[15:13] = 010 (reset bit clear)
// Bits[11:9]  = 010 (AVG = 16)
// Bits[8:6]   = 100 (VBUS CT = 1.1 ms)
// Bits[5:3]   = 100 (VSHUNT CT = 1.1 ms)
// Bits[2:0]   = 111 (continuous shunt + bus)
static constexpr uint16_t INA226_CONFIG_VALUE = 0x4527;

// INA226 fixed shunt voltage LSB = 2.5 µV
static constexpr float SHUNT_LSB_UV = 2.5f;

// Bus voltage LSB = 1.25 mV
static constexpr float BUS_LSB_MV   = 1.25f;

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
// I2C helpers
// ─────────────────────────────────────────────────────────────────────────────

void BatteryMonitor::writeReg(uint8_t reg, uint16_t val) {
    Wire.beginTransmission(INA226_ADDR);
    Wire.write(reg);
    Wire.write((uint8_t)(val >> 8));
    Wire.write((uint8_t)(val & 0xFF));
    Wire.endTransmission();
}

uint16_t BatteryMonitor::readReg(uint8_t reg) {
    Wire.beginTransmission(INA226_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)INA226_ADDR, (uint8_t)2);
    if (Wire.available() < 2) return 0xFFFF;
    uint16_t hi = Wire.read();
    uint16_t lo = Wire.read();
    return (uint16_t)((hi << 8) | lo);
}

// ─────────────────────────────────────────────────────────────────────────────
// Calibration — recomputed whenever config changes
// ─────────────────────────────────────────────────────────────────────────────

void BatteryMonitor::configure() {
    // currentLSB in Amperes, targeting max 20 A range
    float rShuntOhm  = (float)_cfg.shuntMOhm / 1000.0f;
    float currentLSB = 20.0f / 32768.0f;                 // ~610 µA
    float calRaw     = 0.00512f / (currentLSB * rShuntOhm);
    uint16_t calReg  = (uint16_t)calRaw;

    writeReg(INA226_REG_CONFIG,      INA226_CONFIG_VALUE);
    writeReg(INA226_REG_CALIBRATION, calReg);

    // Store currentLSB for later use in update() — encode in µA as integer
    // We re-derive it in update() from the same formula to stay self-contained.
    (void)currentLSB; // used inline in update()
}

// ─────────────────────────────────────────────────────────────────────────────
// begin()
// ─────────────────────────────────────────────────────────────────────────────

bool BatteryMonitor::begin(uint8_t sdaPin, uint8_t sclPin) {
    _sdaPin = sdaPin;
    _sclPin = sclPin;

    Wire.begin(sdaPin, sclPin);

    // Check manufacturer ID (should be 0x5449)
    uint16_t mfrId = readReg(INA226_REG_MFR_ID);
    if (mfrId != 0x5449) {
        _status.inaPresent = false;
        return false;
        LOG_ERROR("BATT", "Unable to detect Ina226");
    }

    _status.inaPresent = true;

    loadConfig();
    configure();

    // Restore coulomb counter from NVS
    nvs_handle_t nvs;
    if (nvs_open(NVS_BAT_NS, NVS_READONLY, &nvs) == ESP_OK) {
        uint32_t rawBits = 0;
        if (nvs_get_u32(nvs, NVS_KEY_COULOMBS, &rawBits) == ESP_OK) {
            _consumedMah = *reinterpret_cast<float*>(&rawBits);
            if (!std::isfinite(_consumedMah) || _consumedMah < 0.0f)
                _consumedMah = 0.0f;
        }
        nvs_close(nvs);
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

    // ── Read bus voltage ──────────────────────────────────────────────────────
    int16_t busRaw   = (int16_t)readReg(INA226_REG_BUS_V);
    _status.busVoltage = busRaw * BUS_LSB_MV / 1000.0f;   // → Volts

    // ── Read shunt voltage → current ─────────────────────────────────────────
    // INA226 shunt reg is signed 16-bit, LSB = 2.5 µV
    int16_t shuntRaw = (int16_t)readReg(INA226_REG_SHUNT_V);
    float shuntUv    = shuntRaw * SHUNT_LSB_UV;            // µV
    float rShuntOhm  = (float)_cfg.shuntMOhm / 1000.0f;
    _status.currentMa = (shuntUv / 1e6f) / rShuntOhm * 1000.0f; // → mA

    // ── Power ────────────────────────────────────────────────────────────────
    _status.powerMw = _status.busVoltage * _status.currentMa;

    // ── Coulomb counter (only track discharge; positive current = discharging) ─
    if (_status.currentMa > 0.0f) {
        _consumedMah += _status.currentMa * dtS / 3600.0f;
        float totalMah = (float)(_cfg.cellMah);
        if (_consumedMah > totalMah) _consumedMah = totalMah;
    }
    _status.consumedMah = _consumedMah;

    // ── SoC ──────────────────────────────────────────────────────────────────
    updateSoC();

    // ── State machine ─────────────────────────────────────────────────────────
    float packMinV = LIION_CELL_MIN_V  * _cfg.cellS;
    float packWarnV = LIION_CELL_WARN_V * _cfg.cellS;

    BatteryState newState;
    if (_status.currentMa < -50.0f) {          // >50 mA flowing in = charging
        newState = BatteryState::CHARGING;
    } else if (_status.busVoltage < packMinV) {
        newState = BatteryState::CRITICAL;
    } else if (_status.busVoltage < packWarnV) {
        newState = BatteryState::WARNING;
    } else {
        newState = BatteryState::NORMAL;
    }

    _status.state = newState;

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
            memcpy(&rawBits, &_consumedMah, sizeof(rawBits));
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
    if (cellV <= OCV_V[0])                    return OCV_SOC[0];
    if (cellV >= OCV_V[OCV_TABLE_SIZE - 1])   return OCV_SOC[OCV_TABLE_SIZE - 1];
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
    float ocvSoc   = ocvToSoc(cellV);

    // Coulomb counter SoC
    float totalMah  = (float)_cfg.cellMah;
    float ccSoc     = (totalMah > 0.0f)
                      ? 1.0f - (_consumedMah / totalMah)
                      : 0.0f;
    ccSoc = std::max(0.0f, std::min(1.0f, ccSoc));

    // When at rest (|I| < 50 mA), OCV is reliable → weight it heavily.
    // Under load, trust coulomb counter more.
    float loadFactor = std::min(1.0f, fabsf(_status.currentMa) / 500.0f);
    _status.soc = ocvSoc * (1.0f - loadFactor) + ccSoc * loadFactor;
    _status.soc = std::max(0.0f, std::min(1.0f, _status.soc));
}

// ─────────────────────────────────────────────────────────────────────────────
// Config persistence
// ─────────────────────────────────────────────────────────────────────────────

bool BatteryMonitor::loadConfig() {
    nvs_handle_t nvs;
    if (nvs_open(NVS_BAT_NS, NVS_READONLY, &nvs) != ESP_OK) return false;

    uint8_t  cellS    = _cfg.cellS;
    uint32_t cellMah  = _cfg.cellMah;
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
    memcpy(&rawBits, &_consumedMah, sizeof(rawBits));
    nvs_set_u32(nvs, NVS_KEY_COULOMBS, rawBits);
    nvs_commit(nvs);
    nvs_close(nvs);
}

void BatteryMonitor::applyConfig() {
    configure();
    saveConfig();
}

void BatteryMonitor::resetCoulombCounter() {
    _consumedMah = 0.0f;
    saveConfig();
}