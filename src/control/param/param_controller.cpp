/**
 * @file param_controller.cpp
 * @brief Parameter dispatcher implementation
 *
 * ISF command additions:
 *   CMD_SET_ISF_PRESET  (0x0B) → handleSetIsfPreset()
 *   CMD_SET_ISF_CONFIG  (0x0E) → handleSetIsfConfig()
 *   CMD_GET_ISF_STATE   (0x0D) → handleGetIsfState()
 *
 * REPORT_ISF (0x41) is pushed by sendIsfState().
 */

#include "param_controller.h"
#include "../../utils/debug_log.h"
#include <string.h>

#define TAG "PARAM"

// ─────────────────────────────────────────────────────────────────────────────
// init
// ─────────────────────────────────────────────────────────────────────────────

extern volatile bool g_usingWifi;

void ParamController::init(
    DspPipeline* pipeline, UartProtocol* uart, PresetManager* presetMgr, WiFiManager* wifiMgr)
{
    _pipeline  = pipeline;
    _uart      = uart;
    _presetMgr = presetMgr;
    _wifiMgr   = wifiMgr;
}

// ─────────────────────────────────────────────────────────────────────────────
// handleCommand — main dispatch
// ─────────────────────────────────────────────────────────────────────────────

void ParamController::handleCommand(const UartCommand& cmd) {
    switch (cmd.cmd) {
        case CMD_ENABLE_MODULE:
            handleEnableDisable(cmd, true);
            break;
        case CMD_DISABLE_MODULE:
            handleEnableDisable(cmd, false);
            break;
        case CMD_SET_PARAM:
            handleSetParam(cmd);
            break;
        case CMD_SET_EQ_BAND:
            handleSetEqBand(cmd);
            break;
        case CMD_SET_DYNEQ_LOW_BAND:
            handleSetEqBand(cmd);
            break;
        case CMD_SET_DYNEQ_HIGH_BAND:
            handleSetEqBand(cmd);
            break;
        case CMD_SET_DYNEQ_THRESH:
            handleSetDynEqThresholds(cmd);
            break;

        // ── ISF commands ──────────────────────────────────────────────────────
        case CMD_SET_ISF_PRESET:
            handleSetIsfCommonPresetParams(cmd);
            break;
        case CMD_SET_ISF_BAND_PARAMS:
            handleSetIsfBandParams(cmd);
            break;
        case CMD_SET_ISF_CONFIG:
            handleSetIsfConfig(cmd);
            break;
        case CMD_GET_ISF_STATE:
            handleGetIsfState(cmd);
            break;
        case CMD_GET_MODULE_METER: {
            if (cmd.dataLen < 1) break;
            const uint8_t modId = cmd.data[0];

            if (modId == MODULE_ID_DYNAMIC_BASS && _pipeline->getDynamicBass().isEnabled()) {
                // REPORT_DYNBASS: energyDb(f32) + alpha(f32) = 8 bytes
                uint8_t d[8];
                packFloat(&d[0], _pipeline->getDynamicBass().getEnergyDb());
                packFloat(&d[4], _pipeline->getDynamicBass().getAlpha());
                _uart->sendFrame(CMD_REPORT_DYNBASS, MODULE_ID_DYNAMIC_BASS, d, sizeof(d));
            }

            else if (modId == MODULE_ID_DYNAMIC_EQ && _pipeline->getDynamicEq().isEnabled()) {
                // REPORT_DYNEQ: energyDb(f32) + alphaLow(f32) + alphaHigh(f32) = 12 bytes
                const DynamicEQ& deq = _pipeline->getDynamicEq();
                uint8_t d[12];
                packFloat(&d[0], deq.getEnergyDb());
                packFloat(&d[4], deq.getAlphaLow());
                packFloat(&d[8], deq.getAlphaHigh());
                _uart->sendFrame(CMD_REPORT_DYNEQ, MODULE_ID_DYNAMIC_EQ, d, sizeof(d));
            }

            else if (modId == MODULE_ID_COMPANDER && _pipeline->getCompander().isEnabled()) {
                // REPORT_COMPANDER: envLinear(f32) + gainDb(f32) = 8 bytes
                const Compander& comp = _pipeline->getCompander();
                uint8_t d[8];
                packFloat(&d[0], comp.getEnvLinear());
                packFloat(&d[4], comp.getGainDb());
                _uart->sendFrame(CMD_REPORT_COMPANDER, MODULE_ID_COMPANDER, d, sizeof(d));
            }

            else if (modId == MODULE_ID_DRC && _pipeline->getDrc().isEnabled()) {
                // REPORT_DRC: 4 × gainDb(f32) — bands 0(Low), 1(Mid), 2(High), 3(Full) = 16 bytes
                const DRC& drc = _pipeline->getDrc();
                uint8_t d[16];
                for (int b = 0; b < 4; b++) {
                    packFloat(&d[b * 4], drc.getBandGainDb((uint8_t)b));
                }
                _uart->sendFrame(CMD_REPORT_DRC, MODULE_ID_DRC, d, sizeof(d));
            }
            break;
        }

        case CMD_SAVE_PRESET:
            if (cmd.dataLen >= 1) {
                bool ok = _presetMgr->savePreset(cmd.data[0], *_pipeline);
                _uart->sendAck(cmd.moduleId, ok ? 0 : 1);
            }
            break;
        case CMD_LOAD_PRESET:
            if (cmd.dataLen >= 1) {
                bool ok = _presetMgr->loadPreset(cmd.data[0], *_pipeline);
                _presetMgr->saveCurrentSlotIndex(cmd.data[0]);
                if (ok) {
                    // Thay vì sendAck, gửi toàn bộ state ngay (bao gồm ACK ở cuối)
                    handleGetAllState(cmd);
                } else {
                    _uart->sendAck(cmd.moduleId, 1);
                }
            }
            break;
        case CMD_GET_ALL_STATE:
            handleGetAllState(cmd);
            break;

        case CMD_GET_REPORT_CPU_USAGE: {
            uint8_t data[7] = {
                (uint8_t)(s_cpu_usage & 0xFF),
                (uint8_t)((s_cpu_usage >> 8) & 0xFF),
                s_heapPct,
                (uint8_t)(s_fs & 0xFF),
                (uint8_t)((s_fs >> 8) & 0xFF),
                (uint8_t)((s_fs >> 16) & 0xFF),
                (uint8_t)((s_fs >> 24) & 0xFF)
            };
            _uart->sendFrame(CMD_SEND_REPORT_CPU_USAGE, MODULE_ID_SYSTEM, data, sizeof(data));
            break;
        }

        if (g_usingWifi) {
            case CMD_WIFI_SCAN:   handleWifiScan(cmd);      break;
            case CMD_WIFI_SET_STA: handleWifiSetSTA(cmd);   break;
            case CMD_WIFI_SET_AP:  handleWifiSetAP(cmd);    break;
            case CMD_WIFI_GET_STATUS: handleWifiGetStatus(cmd); break;
        }
        default:
            LOG_WARN(TAG, "Unknown command: 0x%02X", cmd.cmd);
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// resolveIsf — helper to get ISF instance from module ID
// ─────────────────────────────────────────────────────────────────────────────

IndexSelectableFilter* ParamController::resolveIsf(uint8_t moduleId) {
    if (moduleId == MODULE_ID_ISF_1) return &_pipeline->getIsf1();
    if (moduleId == MODULE_ID_ISF_2) return &_pipeline->getIsf2();
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// handleSetIsfCommonPresetParams
//
// Parses one ISF preset from UART and writes it to the ISF instance.
// Data layout: preset_idx(1) + thresholdDb(f32) + pregainDb(f32) = 9 bytes
// ─────────────────────────────────────────────────────────────────────────────

void ParamController::handleSetIsfCommonPresetParams(const UartCommand& cmd) {
    IndexSelectableFilter* isf = resolveIsf(cmd.moduleId);
    if (!isf) {
        LOG_WARN(TAG, "SET_ISF_PRESET: unknown moduleId 0x%02X", cmd.moduleId);
        return;
    }

    // preset_idx(1) + thresholdDb(f32) + pregainDb(f32) = 9 bytes
    if (cmd.dataLen < 9) {
        LOG_WARN(TAG, "SET_ISF_PRESET: frame too short (%d)", cmd.dataLen);
        _uart->sendAck(cmd.moduleId, 1);
        return;
    }

    uint8_t presetIdx = cmd.data[0];
    if (presetIdx >= ISF_MAX_PRESETS) {
        LOG_WARN(TAG, "SET_ISF_PRESET: index %d out of range", presetIdx);
        _uart->sendAck(cmd.moduleId, 1);
        return;
    }

    float thresholdDb = extractFloat(&cmd.data[1]);
    float pregainDb   = extractFloat(&cmd.data[5]);

    ISFPreset preset;
    preset.thresholdDb = (int16_t)(thresholdDb * 256.0f);  // Internal Q8.8
    preset.filters.setPregain((int16_t)(pregainDb * 256.0f));

    isf->setPreset(presetIdx, preset, ISF_SET_COMMON);

    LOG_INFO(TAG, "ISF%d preset[%d] threshold=%.1f dB pregain=%.1f dB",
        (cmd.moduleId == MODULE_ID_ISF_1) ? 1 : 2,
        presetIdx, thresholdDb, pregainDb);

    _uart->sendAck(cmd.moduleId, 0);
}

void ParamController::handleSetIsfBandParams(const UartCommand& cmd) {
    IndexSelectableFilter* isf = resolveIsf(cmd.moduleId);
    // Layout: presetIdx(1) + bandIdx(1) + enabled(1) + type(1) + freq(f32) + gain(f32) + Q(f32) = 16 bytes
    if (cmd.dataLen < 16) {
        LOG_WARN(TAG, "SET_ISF_BAND: data frame too short (%d)", cmd.dataLen);
        _uart->sendAck(cmd.moduleId, 1);
        return;
    }

    uint8_t presetIdx = cmd.data[0];
    if (presetIdx >= ISF_MAX_PRESETS) {
        LOG_WARN(TAG, "SET_ISF_BAND: preset index %d out of range", presetIdx);
        _uart->sendAck(cmd.moduleId, 1);
        return;
    }

    uint8_t bandIdx = cmd.data[1];
    if (bandIdx >= MAX_EQ_BANDS) {
        LOG_WARN(TAG, "SET_ISF_BAND: band index %d out of range", bandIdx);
        _uart->sendAck(cmd.moduleId, 1);
        return;
    }

    float freq = extractFloat(&cmd.data[4]);
    float gain = extractFloat(&cmd.data[8]);
    float q    = extractFloat(&cmd.data[12]);

    ISFPreset preset;
    EQFilterParams params;
    params.enabled = cmd.data[2] != 0;
    params.type = cmd.data[3];
    params.f0 = (uint16_t)freq;
    params.gain = (int16_t)(gain);
    params.Q = (uint16_t)(q);
    preset.filters.setBand(bandIdx, params);

    isf->setPreset(presetIdx, preset, ISF_SET_BAND, bandIdx);

    LOG_INFO(TAG, "SET_ISF_BANDS: ISF%d, presetIdx:%d, bandIdx:%d, f0:%.0f, gain:%.1f, Q:%.3f", 
        (cmd.moduleId == MODULE_ID_ISF_1) ? 1 : 2,
        presetIdx, bandIdx, freq, gain, q);

    _uart->sendAck(cmd.moduleId, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// handleSetIsfConfig
//
// Data layout: numPresets(1) + rmsMs(f32) + slewMs(f32) + overrideDb(f32) + lookaheadMs(f32) = 17 bytes
// overrideDb == ISF_OVERRIDE_AUTO (-9999.0) → use RMS detector
// ─────────────────────────────────────────────────────────────────────────────

void ParamController::handleSetIsfConfig(const UartCommand& cmd) {
    IndexSelectableFilter* isf = resolveIsf(cmd.moduleId);
    if (!isf) {
        LOG_WARN(TAG, "SET_ISF_CONFIG: unknown moduleId 0x%02X", cmd.moduleId);
        return;
    }

    if (cmd.dataLen < 17) {
        LOG_WARN(TAG, "SET_ISF_CONFIG: frame too short (%d)", cmd.dataLen);
        _uart->sendAck(cmd.moduleId, 1);
        return;
    }

    uint8_t numPresets   = cmd.data[0];
    float   rmsMs        = extractFloat(&cmd.data[1]);
    float   slewMs       = extractFloat(&cmd.data[5]);
    float   overrideDb   = extractFloat(&cmd.data[9]);
    float   lookaheadMs  = extractFloat(&cmd.data[13]);

    if (numPresets > 0)  isf->setNumPresets(numPresets);
    if (rmsMs > 0)       isf->setRmsWindowMs((int16_t)rmsMs);
    if (slewMs > 0)      isf->setSlewMs((int16_t)slewMs);
    if (lookaheadMs > 0) isf->setLookahead(lookaheadMs);
    isf->setOverrideDb(overrideDb);

    LOG_INFO(TAG, "ISF%d config: presets=%d rms=%.1fms slew=%.1fms override=%.1fdB lookahead=%.1fms",
        (cmd.moduleId == MODULE_ID_ISF_1) ? 1 : 2,
        numPresets, rmsMs, slewMs, overrideDb, lookaheadMs);

    _uart->sendAck(cmd.moduleId, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// handleGetIsfState
// ─────────────────────────────────────────────────────────────────────────────

void ParamController::handleGetIsfState(const UartCommand& cmd) {
    sendIsfState(0);
    sendIsfState(1);
}

// ─────────────────────────────────────────────────────────────────────────────
// sendIsfState — push REPORT_ISF for one instance
//
// Data layout (18 bytes, all numeric values are float32):
//   [0]     instance     uint8   0=ISF1, 1=ISF2
//   [1..4]  levelDb      float32 current RMS level dB
//   [5..8]  slewIndex    float32 fractional preset index
//   [9..12] lookaheadMs  float32 lookahead time in ms
//   [13]    active_a     uint8   floor(slewIndex)
//   [14]    active_b     uint8   ceil(slewIndex)
//   [15]    num_presets  uint8
// ─────────────────────────────────────────────────────────────────────────────

void ParamController::sendIsfState(uint8_t instanceIdx) {
    IndexSelectableFilter& isf = (instanceIdx == 0)
        ? _pipeline->getIsf1()
        : _pipeline->getIsf2();

    float levelDb     = isf.getCurrentLevelDb();
    float slewIdx     = isf.getSlewIndex();
    float lookaheadMs = isf.getLookaheadMs();

    uint8_t pkt[16];
    pkt[0] = instanceIdx;
    packFloat(&pkt[1], levelDb);
    packFloat(&pkt[5], slewIdx);
    packFloat(&pkt[9], lookaheadMs);
    pkt[13] = (uint8_t)isf.getActiveA();
    pkt[14] = (uint8_t)isf.getActiveB();
    pkt[15] = isf.getNumPresets();

    _uart->sendFrame(CMD_REPORT_ISF, MODULE_ID_ISF_1, pkt, sizeof(pkt));
}

// ─────────────────────────────────────────────────────────────────────────────
// handleGetAllState — send full DSP state snapshot to UI
// ─────────────────────────────────────────────────────────────────────────────

void ParamController::handleGetAllState(const UartCommand& cmd) {
    _uart->startBatch();
    uint8_t pkt[20];

    // --- Module enable mask ---
    uint16_t mask = 0;
    DspModule** chain = _pipeline->getChain();
    for (size_t i = 0; i < _pipeline->getChainLength(); i++) {
        if (chain[i]->isEnabled()) mask |= (1u << i);
    }
    uint8_t maskPkt[2] = { (uint8_t)(mask & 0xFF), (uint8_t)(mask >> 8) };
    _uart->sendFrame(CMD_REPORT_ENABLE_MASK, MODULE_ID_SYSTEM, maskPkt, 2);

    // --- Send ISF presets for both instances (float32 format) ---
    for (uint8_t inst = 0; inst < 2; inst++) {
        IndexSelectableFilter& isf = (inst == 0) ? _pipeline->getIsf1() : _pipeline->getIsf2();
        uint8_t modId = (inst == 0) ? MODULE_ID_ISF_1 : MODULE_ID_ISF_2;

        uint8_t buf[13];
        buf[0] = inst;
        packFloat(&buf[1], (float)isf.getRmsWindowMs());
        packFloat(&buf[5], (float)isf.getSlewMs());
        packFloat(&buf[9], (float)isf.getLookaheadMs());
        _uart->sendFrame(CMD_REPORT_ISF_CONFIG, modId, buf, sizeof(buf));

        for (uint8_t p = 0; p < isf.getNumPresets(); p++) {
            const ISFPreset& preset = isf.getPreset(p);
            if (!preset.valid) continue;

            // Preset report: preset(1) + thresholdDb(f32) + pregainDb(f32) = 9 bytes
            uint8_t report_pkt[9];
            report_pkt[0] = p;
            packFloat(&report_pkt[1], (float)preset.thresholdDb / 256.0f);
            packFloat(&report_pkt[5], (float)preset.filters.getPregain() / 256.0f);
            _uart->sendFrame(CMD_REPORT_ISF_PRESET, modId, report_pkt, sizeof(report_pkt));

            // Band report: presetIdx(1) + bandIdx(1) + enable(1) + type(1) + freq(f32) + gain(f32) + Q(f32) = 16 bytes
            for (int b = 0; b < MAX_EQ_BANDS; b++) {
                uint8_t band_buf[16];
                EQFilterParams band = preset.filters.getBandParams(b);
                band_buf[0] = p;
                band_buf[1] = b;
                band_buf[2] = band.enabled ? 1 : 0;
                band_buf[3] = band.type;
                packFloat(&band_buf[4], (float)band.f0);
                packFloat(&band_buf[8], (float)band.gain / 256.0f);
                packFloat(&band_buf[12], (float)band.Q / 1024.0f);
                _uart->sendFrame(CMD_REPORT_ISF_BAND_PER_PRESET, modId, band_buf, sizeof(band_buf));
            }
        }
    }

    // --- ISF state for both instances ---
    sendIsfState(0);
    sendIsfState(1);

    // Helper: send param with float32 value
    auto sendParamF32 = [&](uint8_t mid, uint8_t pIndex, float val) {
        pkt[0] = pIndex;
        packFloat(&pkt[1], val);
        _uart->sendFrame(CMD_SET_PARAM, mid, pkt, 5);
    };

    // Helper: send param with int32 value (for enums/flags)
    auto sendParamI32 = [&](uint8_t mid, uint8_t pIndex, int32_t val) {
        pkt[0] = pIndex;
        pkt[1] = val & 0xFF;
        pkt[2] = (val >> 8) & 0xFF;
        pkt[3] = (val >> 16) & 0xFF;
        pkt[4] = (val >> 24) & 0xFF;
        _uart->sendFrame(CMD_SET_PARAM, mid, pkt, 5);
    };

    // --- Volume (dB as float32) ---
    sendParamF32(MODULE_ID_POST_GAIN, 0, (float)_pipeline->getPostGain().getGainDb() / 256.0f);
    sendParamI32(MODULE_ID_POST_GAIN, 1, _pipeline->getPostGain().isMuted() ? 1 : 0);
    sendParamI32(MODULE_ID_POST_GAIN, 2, _pipeline->getPostGain().isMono() ? 1 : 0);
    sendParamF32(MODULE_ID_PRE_GAIN, 0, (float)_pipeline->getPreGain().getGainDb() / 256.0f);
    sendParamI32(MODULE_ID_PRE_GAIN, 1, _pipeline->getPreGain().isMuted() ? 1 : 0);
    sendParamI32(MODULE_ID_PRE_GAIN, 2, _pipeline->getPreGain().isMono() ? 1 : 0);

    // --- Compander (float32) ---
    sendParamF32(MODULE_ID_COMPANDER, 0, (float)_pipeline->getCompander()._thresholdDb);
    sendParamF32(MODULE_ID_COMPANDER, 1, (float)_pipeline->getCompander()._ratioBelowQ88 / 256.0f);
    sendParamF32(MODULE_ID_COMPANDER, 2, (float)_pipeline->getCompander()._ratioAboveQ88 / 256.0f);
    sendParamF32(MODULE_ID_COMPANDER, 3, (float)_pipeline->getCompander()._attackMs);
    sendParamF32(MODULE_ID_COMPANDER, 4, (float)_pipeline->getCompander()._releaseMs);
    sendParamF32(MODULE_ID_COMPANDER, 5, (float)_pipeline->getCompander()._pregainQ412 / 4096.0f);
    sendParamF32(MODULE_ID_COMPANDER, 6, _pipeline->getCompander()._lookaheadMs);

    // --- Exciter (int values, still sent as float32) ---
    sendParamF32(MODULE_ID_EXCITER, 0, (float)_pipeline->getExciter()._fCut);
    sendParamF32(MODULE_ID_EXCITER, 1, (float)_pipeline->getExciter()._dry);
    sendParamF32(MODULE_ID_EXCITER, 2, (float)_pipeline->getExciter()._wet);

    // --- Dynamic Bass (float32) ---
    sendParamF32(MODULE_ID_DYNAMIC_BASS, 0, (float)_pipeline->getDynamicBass().getCutoffFreq());
    sendParamF32(MODULE_ID_DYNAMIC_BASS, 1, (float)_pipeline->getDynamicBass().getGainBoost() / 100.0f);
    sendParamI32(MODULE_ID_DYNAMIC_BASS, 2, _pipeline->getDynamicBass().getEnhanced());
    sendParamF32(MODULE_ID_DYNAMIC_BASS, 3, (float)_pipeline->getDynamicBass().getBoostFullThresh() / 100.0f);
    sendParamF32(MODULE_ID_DYNAMIC_BASS, 4, (float)_pipeline->getDynamicBass().getNeutralThresh() / 100.0f);
    sendParamF32(MODULE_ID_DYNAMIC_BASS, 5, (float)_pipeline->getDynamicBass().getClipFullThresh() / 100.0f);
    sendParamF32(MODULE_ID_DYNAMIC_BASS, 6, (float)_pipeline->getDynamicBass().getClipAttack());
    sendParamF32(MODULE_ID_DYNAMIC_BASS, 7, (float)_pipeline->getDynamicBass().getClipRelease());
    sendParamF32(MODULE_ID_DYNAMIC_BASS, 8, _pipeline->getDynamicBass()._lookaheadMs);

    // --- DRC (float32) ---
    DRC& drc = _pipeline->getDrc();
    sendParamI32(MODULE_ID_DRC, 0x10, (int32_t)drc._mode);
    sendParamI32(MODULE_ID_DRC, 0x11, (int32_t)drc._cfType);
    sendParamF32(MODULE_ID_DRC, 0x12, drc._fc[0]);
    sendParamF32(MODULE_ID_DRC, 0x13, drc._qLp);
    sendParamF32(MODULE_ID_DRC, 0x14, drc._fc[1]);
    sendParamF32(MODULE_ID_DRC, 0x15, drc._qHp);
    const uint8_t idxbandBase[4] = {0x20, 0x28, 0x30, 0x38};
    for (int p = 0; p < DRC_MAX_BANDS; p++) {
        sendParamF32(MODULE_ID_DRC, idxbandBase[p] + 0, (float)drc._bands[p].thresholdDb);
        sendParamF32(MODULE_ID_DRC, idxbandBase[p] + 1, (float)drc._bands[p].ratioX100 / 100.0f);
        sendParamF32(MODULE_ID_DRC, idxbandBase[p] + 2, (float)drc._bands[p].attackMs);
        sendParamF32(MODULE_ID_DRC, idxbandBase[p] + 3, (float)drc._bands[p].releaseMs);
        sendParamF32(MODULE_ID_DRC, idxbandBase[p] + 4, (float)drc._bands[p].pregain);
        sendParamF32(MODULE_ID_DRC, idxbandBase[p] + 5, drc._bands[p].lookaheadMs);
    }
    // --- EQ bands (float32 format) ---
    auto sendEq = [&](uint8_t cmdEq, uint8_t mid, ParametricEQ& eq, bool isRight = false) {
        float pregainDb = (float)eq.getPregain() / 256.0f;
        for (uint8_t i = 0; i < MAX_EQ_BANDS; i++) {
            // Layout: pregainDb(f32) + band(1) + enabled(1) + type(1) + freq(f32) + gainDb(f32) + Q(f32) = 19 bytes
            uint8_t eqPkt[19];
            packFloat(&eqPkt[0], pregainDb);
            eqPkt[4] = isRight ? (i | 0x80) : i;
            eqPkt[5] = eq._params[i].enabled ? 1 : 0;
            eqPkt[6] = eq._params[i].type;
            packFloat(&eqPkt[7], (float)eq._params[i].f0);
            packFloat(&eqPkt[11], (float)eq._params[i].gain / 256.0f);
            packFloat(&eqPkt[15], (float)eq._params[i].Q / 1024.0f);
            _uart->sendFrame(cmdEq, mid, eqPkt, sizeof(eqPkt));
        }
    };
    sendEq(CMD_SET_EQ_BAND, MODULE_ID_PRE_EQ, _pipeline->getPreEq());
    sendEq(CMD_SET_EQ_BAND, MODULE_ID_EQ_DSP_1, _pipeline->getEqDsp_1());
    sendEq(CMD_SET_EQ_BAND, MODULE_ID_EQ_DSP_2, _pipeline->getEqDsp_2());
    sendEq(CMD_SET_EQ_BAND, MODULE_ID_LEFTRIGHT_EQ, _pipeline->getLeftRightEq().getEqLeft(), false);
    sendEq(CMD_SET_EQ_BAND, MODULE_ID_LEFTRIGHT_EQ, _pipeline->getLeftRightEq().getEqRight(), true);

    // --- DynEQ thresholds (float32 format) ---
    DynamicEQ& deq = _pipeline->getDynamicEq();
    uint8_t deqPkt[24];
    packFloat(&deqPkt[0], (float)deq._lowThreshDb / 100.0f);
    packFloat(&deqPkt[4], (float)deq._normalThreshDb / 100.0f);
    packFloat(&deqPkt[8], (float)deq._highThreshDb / 100.0f);
    packFloat(&deqPkt[12], (float)deq._attackMs);
    packFloat(&deqPkt[16], (float)deq._releaseMs);
    packFloat(&deqPkt[20], deq._lookaheadMs);
    _uart->sendFrame(CMD_SET_DYNEQ_THRESH, MODULE_ID_DYNAMIC_EQ, deqPkt, 24);

    sendEq(CMD_SET_DYNEQ_LOW_BAND, MODULE_ID_DYNAMIC_EQ, deq._eqLow);
    sendEq(CMD_SET_DYNEQ_HIGH_BAND, MODULE_ID_DYNAMIC_EQ, deq._eqHigh);

    // --- Current preset index ---
    pkt[0] = _presetMgr->getCurrentPresetIndex();
    _uart->sendFrame(CMD_GET_CURRENT_PRESET_INDEX, MODULE_ID_SYSTEM, pkt, 1);

    _uart->sendAck(MODULE_ID_SYSTEM, 0);
    _uart->endBatch();
}

// ─────────────────────────────────────────────────────────────────────────────
// Utilities
// ─────────────────────────────────────────────────────────────────────────────

int32_t ParamController::extractInt32(const uint8_t* data) {
    return (int32_t)(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
}

int16_t ParamController::extractInt16(const uint8_t* data) {
    int16_t v = (int16_t)(data[0] | (data[1] << 8));
    return v;
}

uint16_t ParamController::extractUint16(const uint8_t* data) {
    uint16_t v = (uint16_t)(data[0] | data[1] << 8);
    return v;
}

/** Extract IEEE-754 float32 from 4 bytes, little-endian. */
float ParamController::extractFloat(const uint8_t* data) {
    union { uint32_t u; float f; } cvt;
    cvt.u = data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    return cvt.f;
}

/** Pack IEEE-754 float32 into 4 bytes, little-endian. */
void ParamController::packFloat(uint8_t* dest, float value) {
    union { uint32_t u; float f; } cvt;
    cvt.f = value;
    dest[0] = (uint8_t)(cvt.u & 0xFF);
    dest[1] = (uint8_t)((cvt.u >> 8) & 0xFF);
    dest[2] = (uint8_t)((cvt.u >> 16) & 0xFF);
    dest[3] = (uint8_t)((cvt.u >> 24) & 0xFF);
}

// ─────────────────────────────────────────────────────────────────────────────
// Existing handlers — kept unchanged
// (handleEnableDisable, handleSetParam, handleSetEqBand, etc.)
// ─────────────────────────────────────────────────────────────────────────────

void ParamController::handleEnableDisable(const UartCommand& cmd, bool enable) {
    DspModule* mod = _pipeline->getModuleById(cmd.moduleId);
    if (!mod) {
        LOG_WARN(TAG, "Enable/Disable: unknown module 0x%02X", cmd.moduleId);
        _uart->sendAck(cmd.moduleId, 1);
        return;
    }
    mod->setEnabled(enable);
    LOG_INFO(TAG, "Module 0x%02X %s", cmd.moduleId, enable ? "enabled" : "disabled");
    _uart->sendAck(cmd.moduleId, 0);
}

void ParamController::handleSetEqBand(const UartCommand& cmd) {
    // Layout: pregainDb(f32) + band(1) + enabled(1) + type(1) + freq(f32) + gainDb(f32) + Q(f32) = 19 bytes
    if (cmd.dataLen < 19) { _uart->sendAck(cmd.moduleId, 1); return; }

    float pregainDb   = extractFloat(&cmd.data[0]);
    uint8_t bandIdx   = cmd.data[4];
    uint8_t enabled   = cmd.data[5];
    uint8_t type      = cmd.data[6];
    float freq        = extractFloat(&cmd.data[7]);
    float gainDb      = extractFloat(&cmd.data[11]);
    float q           = extractFloat(&cmd.data[15]);

    EQFilterParams params;
    params.enabled = (enabled != 0);
    params.type    = type;
    params.f0      = (uint16_t)freq;
    params.gain    = (int16_t)(gainDb);
    params.Q       = (uint16_t)(q);

    ParametricEQ *eq = nullptr;
    uint8_t realBand = bandIdx;
    if (cmd.moduleId == MODULE_ID_EQ_DSP_1) {
        eq = &_pipeline->getEqDsp_1();
        eq->setPregain((int16_t)(pregainDb * 256.0f));
    } else if (cmd.moduleId == MODULE_ID_EQ_DSP_2) {
        eq = &_pipeline->getEqDsp_2();
        eq->setPregain((int16_t)(pregainDb * 256.0f));
    } else if (cmd.moduleId == MODULE_ID_LEFTRIGHT_EQ) {
        if (bandIdx & 0x80) {
            eq = &_pipeline->getLeftRightEq().getEqRight();
            realBand = bandIdx & 0x7F;
        } else {
            eq = &_pipeline->getLeftRightEq().getEqLeft();
        }
        eq->setPregain((int16_t)(pregainDb * 256.0f));
    } else if (cmd.moduleId == MODULE_ID_DYNAMIC_EQ) {
        if (cmd.cmd == CMD_SET_DYNEQ_LOW_BAND) {
            eq = &_pipeline->getDynamicEq().getEqLow();
        } else if (cmd.cmd == CMD_SET_DYNEQ_HIGH_BAND) {
            eq = &_pipeline->getDynamicEq().getEqHigh();
        }
        eq->setPregain((int16_t)(pregainDb * 256.0f));
    } else if (cmd.moduleId == MODULE_ID_PRE_EQ) {
        eq = &_pipeline->getPreEq();
        eq->setPregain((int16_t)(pregainDb * 256.0f));
    } else {
        _uart->sendAck(cmd.moduleId, 1); return;
    }
    if (eq && realBand < MAX_EQ_BANDS) {
        eq->setBand(realBand, params);
    } else {
        _uart->sendAck(cmd.moduleId, 1); return;
    }
    LOG_INFO(TAG, "PARAMETRIC_EQ: eq:%s pregain:%.1f dB, bandIdx:%d, type:%d, freq:%.0f Hz, gain:%.1f dB, Q:%.3f",
        (cmd.moduleId == MODULE_ID_LEFTRIGHT_EQ) ? "Left/Right" :
        (cmd.moduleId == MODULE_ID_DYNAMIC_EQ && cmd.cmd == CMD_SET_DYNEQ_LOW_BAND) ? "DynamicEQ LOW" :
        (cmd.moduleId == MODULE_ID_DYNAMIC_EQ && cmd.cmd == CMD_SET_DYNEQ_HIGH_BAND) ? "DynamicEQ HIGH" :
        (cmd.moduleId == MODULE_ID_PRE_EQ) ? "Pre" :
        (cmd.moduleId == MODULE_ID_EQ_DSP_1) ? "1" : "2",
        pregainDb, bandIdx, params.type, freq, DB_Q8_TO_FLOAT(gainDb), Q_Q610_TO_FLOAT(q));
    _uart->sendAck(cmd.moduleId, 0);
}

void ParamController::handleSetDynEqThresholds(const UartCommand& cmd) {
    // Layout: lowDb(f32) + normalDb(f32) + highDb(f32) + attackMs(f32) + releaseMs(f32) + lookaheadMs(f32) = 24 bytes
    if (cmd.dataLen < 24) { _uart->sendAck(cmd.moduleId, 1); return; }
    
    float lowDb        = extractFloat(&cmd.data[0]);
    float normalDb     = extractFloat(&cmd.data[4]);
    float highDb       = extractFloat(&cmd.data[8]);
    float attackMs     = extractFloat(&cmd.data[12]);
    float releaseMs    = extractFloat(&cmd.data[16]);
    float lookaheadMs  = extractFloat(&cmd.data[20]);
    
    DynamicEQ& deq = _pipeline->getDynamicEq();
    deq.setLowEnergyThreshold((int32_t)(lowDb * 100.0f));  // dB → ×100
    deq.setNormalEnergyThreshold((int32_t)(normalDb * 100.0f)); // dB → ×100
    deq.setHighEnergyThreshold((int32_t)(highDb * 100.0f)); // dB → ×100
    deq.setAttackTime((int32_t)attackMs);
    deq.setReleaseTime((int32_t)releaseMs);
    deq.setLookahead(lookaheadMs);
    
    LOG_INFO(TAG, "DynEQ thresh: low=%.1f normal=%.1f high=%.1f attack=%.1f release=%.1f lookahead=%.1f",
        lowDb, normalDb, highDb, attackMs, releaseMs, lookaheadMs);
    _uart->sendAck(cmd.moduleId, 0);
}

void ParamController::handleSetParam(const UartCommand& cmd) {
    // Layout: paramId(1) + value(f32) = 5 bytes
    if (cmd.dataLen < 5) { _uart->sendAck(cmd.moduleId, 1); return; }
    
    uint8_t paramId = cmd.data[0];
    float   fValue  = extractFloat(&cmd.data[1]);
    
    // For flags/enums, convert to int; for numeric values, use as-is or convert to internal format
    int32_t iValue = (int32_t)fValue;

    switch (cmd.moduleId) {
        case MODULE_ID_PRE_GAIN:
            if (paramId == 0) _pipeline->getPreGain().setGainDb((int16_t)(fValue * 256.0f)); // dB → Q8.8
            else if (paramId == 1) _pipeline->getPreGain().setMute(iValue != 0);
            else if (paramId == 2) _pipeline->getPreGain().setMono(iValue != 0);
            break;
        case MODULE_ID_POST_GAIN:
            if (paramId == 0) _pipeline->getPostGain().setGainDb((int16_t)(fValue * 256.0f)); // dB → Q8.8
            else if (paramId == 1) _pipeline->getPostGain().setMute(iValue != 0);
            else if (paramId == 2) _pipeline->getPostGain().setMono(iValue != 0);
            break;
        case MODULE_ID_COMPANDER:
            switch (paramId) {
                case 0: _pipeline->getCompander().setThreshold(fValue); break; // dB
                case 1: _pipeline->getCompander().setRatioBelow((int16_t)(fValue * 256.0f)); break; // ratio → Q8.8
                case 2: _pipeline->getCompander().setRatioAbove((int16_t)(fValue * 256.0f)); break; // ratio → Q8.8
                case 3: _pipeline->getCompander().setAttackTime(iValue);  break;
                case 4: _pipeline->getCompander().setReleaseTime(iValue); break;
                case 5: _pipeline->getCompander().setPregain((int16_t)(fValue * 4096.0f)); break; // dB → Q4.12
                case 6: _pipeline->getCompander().setLookahead(fValue); break; // ms direct
            }
            break;
        case MODULE_ID_EXCITER:
            switch (paramId) {
                case 0: _pipeline->getExciter().setCutoffFreq(iValue); break;
                case 1: _pipeline->getExciter().setDry(iValue);        break;
                case 2: _pipeline->getExciter().setWet(iValue);        break;
            }
            break;
        case MODULE_ID_DYNAMIC_BASS:
            switch (paramId) {
                case 0: _pipeline->getDynamicBass().setCutoffFreq(iValue);         break;
                case 1: _pipeline->getDynamicBass().setGainBoost((int32_t)(fValue * 100.0f)); break; // dB → ×100
                case 2: _pipeline->getDynamicBass().setEnhanced(iValue);           break;
                case 3: _pipeline->getDynamicBass().setBoostFullThreshold((int32_t)(fValue * 100.0f)); break; // dB → ×100
                case 4: _pipeline->getDynamicBass().setNeutralThreshold((int32_t)(fValue * 100.0f));   break; // dB → ×100
                case 5: _pipeline->getDynamicBass().setClipFullThreshold((int32_t)(fValue * 100.0f));  break; // dB → ×100
                case 6: _pipeline->getDynamicBass().setClipAttack(iValue);          break;
                case 7: _pipeline->getDynamicBass().setClipRelease(iValue);         break;
                case 8: _pipeline->getDynamicBass().setLookahead(fValue); break; // ms direct
            }
            break;
        case MODULE_ID_DRC: {
            DRC &drc = _pipeline->getDrc();

            // ── Global params (paramId 0x10 - 0x1F) ────────────────────────
            if (paramId == 0x10) { drc.setMode((DRCMode)iValue);                   break; }
            if (paramId == 0x11) { drc.setCrossoverType((DRCCrossoverType)iValue); break; }
            if (paramId == 0x12) { drc.setCrossoverFreq(0, iValue);                break; }
            if (paramId == 0x13) { drc.setCrossoverFreq(1, iValue);                break; }
            if (paramId == 0x14) { drc.setCrossoverQ(0, (int16_t)(fValue * 1024.0f)); break; } // Q → Q6.10
            if (paramId == 0x15) { drc.setCrossoverQ(1, (int16_t)(fValue * 1024.0f)); break; } // Q → Q6.10

            // ── Per-band params (paramId 0x20 - 0x3F) ──────────────────────
            if (paramId >= 0x20 && paramId <= 0x3F) {
                uint8_t band  = (paramId - 0x20) >> 3;  // 0-3
                uint8_t param = (paramId - 0x20) & 0x07;
                switch (param) {
                    case 0: drc.setThreshold(band, fValue);    break; // dB → ×100
                    case 1: drc.setRatio(band, (int32_t)(fValue));        break; // ratio → ×100
                    case 2: drc.setAttackTime(band, iValue);   break;
                    case 3: drc.setReleaseTime(band, iValue);  break;
                    case 4: drc.setPregain(band, fValue);     break;
                    case 5: drc.setLookahead(band, fValue); break; // ms direct
                    default: _uart->sendError(0x04); return;
                }
            }
            break;
        }
        default:
            LOG_WARN(TAG, "SET_PARAM: unhandled moduleId 0x%02X", cmd.moduleId);
            _uart->sendAck(cmd.moduleId, 1);
            return;
    }
    _uart->sendAck(cmd.moduleId, 0);
}

// WiFi handlers — unchanged, just dispatch
#include <WiFi.h>

void ParamController::setWifiManager(WiFiManager* wifiMgr) {
  _wifiMgr = wifiMgr;
}

void ParamController::handleWifiScan(const UartCommand &cmd) {
  if (!_wifiMgr) { _uart->sendError(0x10); return; }

  int n = WiFi.scanComplete();

  if (n == WIFI_SCAN_RUNNING) {
    // Already scanning, tell JS to keep polling
    _uart->sendAck(MODULE_ID_SYSTEM, 0xFF);
    return;
  }

  if (n == WIFI_SCAN_FAILED) {
    // Not started yet, or previous scan deleted. Start a new one.
    _wifiMgr->startScan();
    _uart->sendAck(MODULE_ID_SYSTEM, 0xFF);
    LOG_INFO(TAG, "WiFi scan started");
    return;
  }

  // Scan complete (n >= 0)
  int count = (n > WIFI_MAX_SCAN_RESULTS) ? WIFI_MAX_SCAN_RESULTS : n;
  
  if (count == 0) {
    _uart->sendAck(MODULE_ID_SYSTEM, 0);
    LOG_INFO(TAG, "WiFi scan results sent: 0 networks");
    WiFi.scanDelete(); // Reset state for next time
    return;
  }

  // Send each scan result as a separate frame, but use batching for efficiency
  _uart->startBatch();
  for (int i = 0; i < count && i < WIFI_MAX_SCAN_RESULTS; i++) {
    String entry = _wifiMgr->getScanEntry(i); // "SSID\tRSSI\tencrypted"
    int tab1 = entry.indexOf('\t');
    int tab2 = entry.lastIndexOf('\t');
    if (tab1 < 0) continue;

    String ssid  = entry.substring(0, tab1);
    int8_t rssi  = (int8_t)entry.substring(tab1 + 1, tab2).toInt();
    uint8_t enc  = (entry.substring(tab2 + 1) == "1") ? 1 : 0;
    uint8_t ssidLen = (uint8_t)min((int)ssid.length(), 32);

    uint8_t pkt[36];
    pkt[0] = (uint8_t)i;
    pkt[1] = (uint8_t)count;
    pkt[2] = (uint8_t)rssi;
    pkt[3] = enc;
    memcpy(pkt + 4, ssid.c_str(), ssidLen);
    _uart->sendFrame(CMD_WIFI_SCAN, MODULE_ID_SYSTEM, pkt, 4 + ssidLen);
  }

  _uart->sendAck(MODULE_ID_SYSTEM, 0);
  _uart->endBatch();
  
  LOG_INFO(TAG, "WiFi scan results sent: %d networks", count);
  
  // Clear scan results so the next request forces a fresh scan
  WiFi.scanDelete();
}

void ParamController::handleWifiSetSTA(const UartCommand &cmd) {
  // Payload: ssid_len(1B) + ssid(NB) + pass_len(1B) + pass(MB) + ip(4B, 0=DHCP)
  if (!_wifiMgr || cmd.dataLen < 2) { _uart->sendError(0x03); return; }

  uint8_t ssidLen = cmd.data[0];
  if (cmd.dataLen < (uint16_t)(1 + ssidLen + 1)) { _uart->sendError(0x03); return; }

  char ssid[33] = {};
  memcpy(ssid, &cmd.data[1], min((int)ssidLen, 32));

  uint8_t passLen = cmd.data[1 + ssidLen];
  uint16_t offset = 2 + ssidLen;
  if (cmd.dataLen < offset + passLen) { _uart->sendError(0x03); return; }

  char pass[65] = {};
  memcpy(pass, &cmd.data[offset], min((int)passLen, 64));

  // Optional static IP (4 bytes, 0 = DHCP)
  IPAddress staticIP = INADDR_NONE;
  offset += passLen;
  if (cmd.dataLen >= offset + 4) {
    uint32_t ip = cmd.data[offset] | ((uint32_t)cmd.data[offset+1] << 8)
                | ((uint32_t)cmd.data[offset+2] << 16) | ((uint32_t)cmd.data[offset+3] << 24);
    if (ip != 0) staticIP = IPAddress(ip);
  }

  // ACK first, then switch (WiFi restart may briefly interrupt Serial2)
  _uart->sendAck(MODULE_ID_SYSTEM, 0);
  LOG_INFO(TAG, "WiFi STA requested — SSID: %s", ssid);
  _wifiMgr->setSTAMode(ssid, pass, staticIP);

  // After reconnect, send new status so app gets updated IP
  uint8_t statusBuf[40];
  uint16_t statusLen = 0;
  _wifiMgr->buildStatusPayload(statusBuf, statusLen);
  _uart->sendFrame(CMD_WIFI_GET_STATUS, MODULE_ID_SYSTEM, statusBuf, statusLen);
}

void ParamController::handleWifiSetAP(const UartCommand &cmd) {
  if (!_wifiMgr) { _uart->sendError(0x10); return; }
  _uart->sendAck(MODULE_ID_SYSTEM, 0);
  LOG_INFO(TAG, "WiFi AP mode requested");
  _wifiMgr->setAPMode();

  // Send new status
  uint8_t statusBuf[40];
  uint16_t statusLen = 0;
  _wifiMgr->buildStatusPayload(statusBuf, statusLen);
  _uart->sendFrame(CMD_WIFI_GET_STATUS, MODULE_ID_SYSTEM, statusBuf, statusLen);
}

void ParamController::handleWifiGetStatus(const UartCommand &cmd) {
  if (!_wifiMgr) {
    // WiFi not initialised — send placeholder
    uint8_t noWifi[2] = { 0x00, 0x00 }; // mode=unknown, ip=0
    _uart->sendFrame(CMD_WIFI_GET_STATUS, MODULE_ID_SYSTEM, noWifi, 2);
    return;
  }

  uint8_t statusBuf[40];
  uint16_t statusLen = 0;
  _wifiMgr->buildStatusPayload(statusBuf, statusLen);
  _uart->sendFrame(CMD_WIFI_GET_STATUS, MODULE_ID_SYSTEM, statusBuf, statusLen);
  LOG_INFO(TAG, "WiFi status sent — mode=%s IP=%s",
           _wifiMgr->isAPMode() ? "AP" : "STA",
           _wifiMgr->getIP().toString().c_str());
}