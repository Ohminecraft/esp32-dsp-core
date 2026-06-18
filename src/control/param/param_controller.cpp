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
                // REPORT_DYNBASS: energyDb(int16 Q8.8) + alpha(int16 Q8.8)
                const int16_t eDb = (int16_t)(_pipeline->getDynamicBass().getEnergyDb() * 256.0f);
                const int16_t alp = (int16_t)(_pipeline->getDynamicBass().getAlpha()    * 256.0f);
                uint8_t d[4] = {
                    (uint8_t)(eDb     & 0xFF), (uint8_t)((eDb >> 8) & 0xFF),
                    (uint8_t)(alp     & 0xFF), (uint8_t)((alp >> 8) & 0xFF)
                };
                _uart->sendFrame(CMD_REPORT_DYNBASS, MODULE_ID_DYNAMIC_BASS, d, sizeof(d));
            }

            else if (modId == MODULE_ID_DYNAMIC_EQ && _pipeline->getDynamicEq().isEnabled()) {
                // REPORT_DYNEQ: energyDb(int16 Q8.8) + alphaLow(int16 Q8.8) + alphaHigh(int16 Q8.8)
                const DynamicEQ& deq = _pipeline->getDynamicEq();
                const int16_t eDb  = (int16_t)(deq.getEnergyDb()  * 256.0f);
                const int16_t aLow = (int16_t)(deq.getAlphaLow()  * 256.0f);
                const int16_t aHi  = (int16_t)(deq.getAlphaHigh() * 256.0f);
                uint8_t d[6] = {
                    (uint8_t)(eDb  & 0xFF), (uint8_t)((eDb  >> 8) & 0xFF),
                    (uint8_t)(aLow & 0xFF), (uint8_t)((aLow >> 8) & 0xFF),
                    (uint8_t)(aHi  & 0xFF), (uint8_t)((aHi  >> 8) & 0xFF)
                };
                _uart->sendFrame(CMD_REPORT_DYNEQ, MODULE_ID_DYNAMIC_EQ, d, sizeof(d));
            }

            else if (modId == MODULE_ID_COMPANDER && _pipeline->getCompander().isEnabled()) {
                // REPORT_COMPANDER: envLinear(uint16 Q1.14) + gainDb(int16 Q8.8)
                // Q1.14: 0..1.0 → 0..16384, clamp tại 16383
                const Compander& comp   = _pipeline->getCompander();
                const float      envLin = comp.getEnvLinear();
                const float      gainDb = comp.getGainDb();
                const uint16_t   envQ   = (uint16_t)(envLin >= 1.0f ? 16383 : (uint16_t)(envLin * 16384.0f));
                const int16_t    gQ88   = (int16_t)(gainDb * 256.0f);
                uint8_t d[4] = {
                    (uint8_t)(envQ & 0xFF), (uint8_t)((envQ >> 8) & 0xFF),
                    (uint8_t)(gQ88 & 0xFF), (uint8_t)((gQ88 >> 8) & 0xFF)
                };
                _uart->sendFrame(CMD_REPORT_COMPANDER, MODULE_ID_COMPANDER, d, sizeof(d));
            }

            else if (modId == MODULE_ID_DRC && _pipeline->getDrc().isEnabled()) {
                // REPORT_DRC: 4 × gainDb(int16 Q8.8) — bands 0(Low), 1(Mid), 2(High), 3(Full)
                const DRC& drc = _pipeline->getDrc();
                uint8_t d[8];
                for (int b = 0; b < 4; b++) {
                    const int16_t gQ88 = (int16_t)(drc.getBandGainDb((uint8_t)b) * 256.0f);
                    d[b * 2]     = (uint8_t)(gQ88 & 0xFF);
                    d[b * 2 + 1] = (uint8_t)((gQ88 >> 8) & 0xFF);
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

        #ifndef ONLY_SERIAL
        case CMD_WIFI_SCAN:   handleWifiScan(cmd);      break;
        case CMD_WIFI_SET_STA: handleWifiSetSTA(cmd);   break;
        case CMD_WIFI_SET_AP:  handleWifiSetAP(cmd);    break;
        case CMD_WIFI_GET_STATUS: handleWifiGetStatus(cmd); break;
        #endif

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
// ─────────────────────────────────────────────────────────────────────────────

void ParamController::handleSetIsfCommonPresetParams(const UartCommand& cmd) {
    IndexSelectableFilter* isf = resolveIsf(cmd.moduleId);
    if (!isf) {
        LOG_WARN(TAG, "SET_ISF_PRESET: unknown moduleId 0x%02X", cmd.moduleId);
        return;
    }

    // Minimum: preset_idx(1) + threshold(2) + pregain(2) = 5 bytes
    if (cmd.dataLen < 5) {
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

    ISFPreset preset;
    preset.thresholdDb = extractInt16(&cmd.data[1]);
    preset.filters.setPregain(extractInt16(&cmd.data[3]));

    isf->setPreset(presetIdx, preset, ISF_SET_COMMON);

    LOG_INFO(TAG, "ISF%d preset[%d] threshold=%.1f dB pregain=%.1f dB",
        (cmd.moduleId == MODULE_ID_ISF_1) ? 1 : 2,
        presetIdx,
        (float)preset.thresholdDb / 256.0f,
        (float)preset.filters.getPregain() / 256.0f);

    _uart->sendAck(cmd.moduleId, 0);
}

void ParamController::handleSetIsfBandParams(const UartCommand& cmd) {
    IndexSelectableFilter* isf = resolveIsf(cmd.moduleId);
    // Layout presetIdx(1) + bandIdx(1) + enabled(1) + type(1) + freq(2) + gain(2) + Q(2) = 10 bytes
    if (cmd.dataLen < 10) {
        LOG_WARN(TAG, "SET_ISF_BAND: data frame to short (%d)", cmd.dataLen);
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

    ISFPreset preset;
    EQFilterParams params;
    params.enabled = cmd.data[2] != 0;
    params.type = cmd.data[3];
    params.f0 = extractUint16(&cmd.data[4]);
    params.gain = extractInt16(&cmd.data[6]);
    params.Q = extractUint16(&cmd.data[8]);
    preset.filters.setBand(bandIdx, params);

    isf->setPreset(presetIdx, preset, ISF_SET_BAND, bandIdx);

    LOG_INFO(TAG, "SET_ISF_BANDS: ISF%d, presetIdx:%d, bandIdx:%d, f0:%d, gain:%.1f, Q:%.1f", 
        (cmd.moduleId == MODULE_ID_ISF_1) ? 1 : 2,
        presetIdx,
        bandIdx,
        params.f0,
        (float)params.gain / 256,
        (float)params.Q / 256);

    _uart->sendAck(cmd.moduleId, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// handleSetIsfConfig
//
// Data: num_presets(1) + rms_ms(2) + slew_ms(2) + override_q88(2) = 7 bytes
// override_q88 == INT16_MIN → auto (use RMS detector)
// ─────────────────────────────────────────────────────────────────────────────

void ParamController::handleSetIsfConfig(const UartCommand& cmd) {
    IndexSelectableFilter* isf = resolveIsf(cmd.moduleId);
    if (!isf) {
        LOG_WARN(TAG, "SET_ISF_CONFIG: unknown moduleId 0x%02X", cmd.moduleId);
        return;
    }

    if (cmd.dataLen < 7) {
        LOG_WARN(TAG, "SET_ISF_CONFIG: frame too short (%d)", cmd.dataLen);
        _uart->sendAck(cmd.moduleId, 1);
        return;
    }

    uint8_t numPresets    = cmd.data[0];
    int16_t rmsMs         = extractInt16(&cmd.data[1]);
    int16_t slewMs        = extractInt16(&cmd.data[3]);
    int16_t overrideQ88   = extractInt16(&cmd.data[5]);
    int32_t lookahead     = extractInt32(&cmd.data[7]);

    if (numPresets > 0) isf->setNumPresets(numPresets);
    if (rmsMs  > 0)    isf->setRmsWindowMs(rmsMs);
    if (slewMs > 0)    isf->setSlewMs(slewMs);
    if (lookahead > 0) isf->setLookahead((float)lookahead / 10.0f);

    // INT16_MIN (-32768) = auto; anything else = override level in Q8.8
    float overrideDb = (overrideQ88 == (int16_t)0x8000)
        ? IndexSelectableFilter::ISF_OVERRIDE_AUTO
        : (float)overrideQ88 / 256.0f;
    isf->setOverrideDb(overrideDb);

    LOG_INFO(TAG, "ISF%d config: presets=%d rms=%dms slew=%dms override=%.1f, lookahead=%f",
        (cmd.moduleId == MODULE_ID_ISF_1) ? 1 : 2,
        numPresets, rmsMs, slewMs, overrideDb, (float)lookahead / 10.0f);

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
// Data layout (8 bytes):
//   [0]     instance     uint8   0=ISF1, 1=ISF2
//   [1..2]  level_q88    int16   current RMS level dB (Q8.8)
//   [3..4]  slew_q88     int16   fractional index * 256
//   [5]     active_a     uint8   floor(slewIndex)
//   [6]     active_b     uint8   ceil(slewIndex)
//   [7]     num_presets  uint8
// ─────────────────────────────────────────────────────────────────────────────

void ParamController::sendIsfState(uint8_t instanceIdx) {
    IndexSelectableFilter& isf = (instanceIdx == 0)
        ? _pipeline->getIsf1()
        : _pipeline->getIsf2();

    float levelDb   = isf.getCurrentLevelDb();
    float slewIdx   = isf.getSlewIndex();

    // Clamp level to Q8.8 representable range [-128, 127]
    if (levelDb < -128.0f) levelDb = -128.0f;
    if (levelDb >  127.0f) levelDb =  127.0f;

    int16_t levelQ88    = (int16_t)(levelDb * 256.0f);
    int16_t slewQ88     = (int16_t)(slewIdx * 256.0f);
    int32_t lookahead = (int32_t)(isf.getLookaheadMs() * 10.0f + 0.5f);

    uint8_t pkt[12];
    pkt[0]  = instanceIdx;
    pkt[1]  = (uint8_t)(levelQ88 & 0xFF);
    pkt[2]  = (uint8_t)((levelQ88 >> 8) & 0xFF);
    pkt[3]  = (uint8_t)(slewQ88 & 0xFF);
    pkt[4]  = (uint8_t)((slewQ88 >> 8) & 0xFF);
    pkt[5]  = (uint8_t)(lookahead & 0xFF);
    pkt[6]  = (uint8_t)((lookahead >> 8) & 0xFF);
    pkt[7]  = (uint8_t)((lookahead >> 16) & 0xFF);
    pkt[8]  = (uint8_t)((lookahead >> 24) & 0xFF);
    pkt[9]  = (uint8_t)isf.getActiveA();
    pkt[10] = (uint8_t)isf.getActiveB();
    pkt[11] = isf.getNumPresets();

    _uart->sendFrame(CMD_REPORT_ISF, MODULE_ID_ISF_1, pkt, sizeof(pkt));
}

// ─────────────────────────────────────────────────────────────────────────────
// handleGetAllState — send full DSP state snapshot to UI
// ─────────────────────────────────────────────────────────────────────────────

void ParamController::handleGetAllState(const UartCommand& cmd) {
    _uart->startBatch();
    uint8_t pkt[16];
    // --- Module enable mask ---
    uint16_t mask = 0;
    DspModule** chain = _pipeline->getChain();
    for (size_t i = 0; i < _pipeline->getChainLength(); i++) {
        if (chain[i]->isEnabled()) mask |= (1u << i);
    }
    uint8_t maskPkt[2] = { (uint8_t)(mask & 0xFF), (uint8_t)(mask >> 8) };
    _uart->sendFrame(CMD_REPORT_ENABLE_MASK, MODULE_ID_SYSTEM, maskPkt, 2);

    // --- Send ISF presets for both instances ---
    // (UI needs to know preset configs on connect)
    for (uint8_t inst = 0; inst < 2; inst++) {
        IndexSelectableFilter& isf = (inst == 0)
            ? _pipeline->getIsf1()
            : _pipeline->getIsf2();
        uint8_t modId = (inst == 0) ? MODULE_ID_ISF_1 : MODULE_ID_ISF_2;

        for (uint8_t p = 0; p < isf.getNumPresets(); p++) {
            const ISFPreset& preset = isf.getPreset(p);
            if (!preset.valid) continue;

            // Build preset report packet
            // Layout preset(1) + threshold(2) + pregain(2) = 5 bytes
            uint8_t report_pkt[5];
            report_pkt[0] = p;
            report_pkt[1] = (uint8_t)(preset.thresholdDb & 0xFF);
            report_pkt[2] = (uint8_t)((preset.thresholdDb >> 8) & 0xFF);
            report_pkt[3] = (uint8_t)(preset.filters.getPregain() & 0xFF);
            report_pkt[4] = (uint8_t)((preset.filters.getPregain() >> 8) & 0xFF);
            _uart->sendFrame(CMD_REPORT_ISF_PRESET, modId, report_pkt, sizeof(report_pkt));


            // Build band report packet
            // Layout presetIdx(1) + bandIdx(1) + enable(1) + type(1) + freq(2) + gain(2) + Q(2) = 10 bytes
            for (int b = 0; b < MAX_EQ_BANDS; b++) {
                uint8_t band_buf[10];
                EQFilterParams band = preset.filters.getBandParams(b);
                band_buf[0] = p;
                band_buf[1] = b;
                band_buf[2] = band.enabled ? 1 : 0;
                band_buf[3] = band.type;
                band_buf[4] = (uint8_t)(band.f0 & 0xFF);
                band_buf[5] = (uint8_t)((band.f0 >> 8) & 0xFF);
                band_buf[6] = (uint8_t)(band.gain & 0xFF);
                band_buf[7] = (uint8_t)((band.gain >> 8) & 0xFF);
                band_buf[8] = (uint8_t)(band.Q & 0xFF);
                band_buf[9] = (uint8_t)((band.Q >> 8) & 0xFF);
                _uart->sendFrame(CMD_REPORT_ISF_BAND_PER_PRESET, modId, band_buf, sizeof(band_buf));
            }
        }
    }

    // --- ISF state for both instances ---
    sendIsfState(0);
    sendIsfState(1);

    auto sendPkt = [&](uint8_t mid, uint8_t pIndex, int32_t val) {
        pkt[0] = pIndex;
        pkt[1] = val & 0xFF;
        pkt[2] = (val >> 8) & 0xFF;
        pkt[3] = (val >> 16) & 0xFF;
        pkt[4] = (val >> 24) & 0xFF;
        _uart->sendFrame(CMD_SET_PARAM, mid, pkt, 5);
    };

    // Volume
    sendPkt(MODULE_ID_POST_GAIN, 0, _pipeline->getPostGain().getGainDb());
    sendPkt(MODULE_ID_POST_GAIN, 1,  _pipeline->getPostGain().isMuted() ? 1 : 0);
    sendPkt(MODULE_ID_POST_GAIN, 2, _pipeline->getPostGain().isMono()   ? 1 : 0);
    sendPkt(MODULE_ID_PRE_GAIN, 0, _pipeline->getPreGain().getGainDb());
    sendPkt(MODULE_ID_PRE_GAIN, 1, _pipeline->getPreGain().isMuted() ? 1 : 0);
    sendPkt(MODULE_ID_PRE_GAIN, 2, _pipeline->getPreGain().isMono()  ? 1 : 0);

    // CP
    sendPkt(MODULE_ID_COMPANDER, 0, _pipeline->getCompander()._thresholdDbInt);
    sendPkt(MODULE_ID_COMPANDER, 1, _pipeline->getCompander()._ratioBelowQ88);
    sendPkt(MODULE_ID_COMPANDER, 2, _pipeline->getCompander()._ratioAboveQ88);
    sendPkt(MODULE_ID_COMPANDER, 3, _pipeline->getCompander()._attackMs);
    sendPkt(MODULE_ID_COMPANDER, 4, _pipeline->getCompander()._releaseMs);
    sendPkt(MODULE_ID_COMPANDER, 5, _pipeline->getCompander()._pregainQ412);
    // Lookahead: float ms → ms×10 as int32
    sendPkt(MODULE_ID_COMPANDER, 6, (int32_t)(_pipeline->getCompander()._lookaheadMs * 10.0f + 0.5f));

    // EX
    sendPkt(MODULE_ID_EXCITER, 0, _pipeline->getExciter()._fCut);
    sendPkt(MODULE_ID_EXCITER, 1, _pipeline->getExciter()._dry);
    sendPkt(MODULE_ID_EXCITER, 2, _pipeline->getExciter()._wet);

    // DB
    sendPkt(MODULE_ID_DYNAMIC_BASS, 0, _pipeline->getDynamicBass().getCutoffFreq());
    sendPkt(MODULE_ID_DYNAMIC_BASS, 1, _pipeline->getDynamicBass().getGainBoost());
    sendPkt(MODULE_ID_DYNAMIC_BASS, 2, _pipeline->getDynamicBass().getEnhanced());
    sendPkt(MODULE_ID_DYNAMIC_BASS, 3, _pipeline->getDynamicBass().getBoostFullThresh());
    sendPkt(MODULE_ID_DYNAMIC_BASS, 4, _pipeline->getDynamicBass().getNeutralThresh());
    sendPkt(MODULE_ID_DYNAMIC_BASS, 5, _pipeline->getDynamicBass().getClipFullThresh());
    sendPkt(MODULE_ID_DYNAMIC_BASS, 6, _pipeline->getDynamicBass().getClipAttack());
    sendPkt(MODULE_ID_DYNAMIC_BASS, 7, _pipeline->getDynamicBass().getClipRelease());
    sendPkt(MODULE_ID_DYNAMIC_BASS, 8, (int32_t)(_pipeline->getDynamicBass()._lookaheadMs * 10.0f + 0.5f));

    // DRC — send mode + fullband (band[3]) params using new encoding
    {
        DRC &drc = _pipeline->getDrc();
        sendPkt(MODULE_ID_DRC, 0x10, (int32_t)drc._mode);
        // Fullband band[3]: paramId = 0x20 + 3*8 + param = 0x38 + param
        sendPkt(MODULE_ID_DRC, 0x38 + 0, drc._bands[3].thresholdDbInt);
        sendPkt(MODULE_ID_DRC, 0x38 + 1, drc._bands[3].ratioX100);
        sendPkt(MODULE_ID_DRC, 0x38 + 2, drc._bands[3].attackMs);
        sendPkt(MODULE_ID_DRC, 0x38 + 3, drc._bands[3].releaseMs);
        sendPkt(MODULE_ID_DRC, 0x38 + 4, drc._bands[3].pregainQ412);
        // Lookahead per band: pBase+5, ms×10 as int32
        for (int b = 0; b < 4; b++) {
            int32_t laVal = (int32_t)(drc._bands[b].lookaheadMs * 10.0f + 0.5f);
            sendPkt(MODULE_ID_DRC, (uint8_t)(0x20 + b * 8 + 5), laVal);
        }
    }

    // EQ bands
    auto sendEq = [&](uint8_t cmdEq, uint8_t mid, ParametricEQ &eq, bool isRight = false) {
        int16_t pregain_db = eq.getPregain();
        for (uint8_t i = 0; i < MAX_EQ_BANDS; i++) {
        pkt[0] = pregain_db & 0xFF;
        pkt[1] = (pregain_db >> 8) & 0xFF;
        pkt[2] = isRight ? (i | 0x80) : i;
        pkt[3] = eq._params[i].enabled ? 1 : 0;
        pkt[4] = eq._params[i].type;
        pkt[5] = eq._params[i].f0 & 0xFF;
        pkt[6] = (eq._params[i].f0 >> 8) & 0xFF;
        pkt[7] = eq._params[i].gain & 0xFF;
        pkt[8] = (eq._params[i].gain >> 8) & 0xFF;
        pkt[9] = eq._params[i].Q & 0xFF;
        pkt[10] = (eq._params[i].Q >> 8) & 0xFF;
        _uart->sendFrame(cmdEq, mid, pkt, 11);
        }
    };
    sendEq(CMD_SET_EQ_BAND, MODULE_ID_PRE_EQ, _pipeline->getPreEq());
    sendEq(CMD_SET_EQ_BAND, MODULE_ID_EQ_DSP_1, _pipeline->getEqDsp_1());
    sendEq(CMD_SET_EQ_BAND, MODULE_ID_EQ_DSP_2, _pipeline->getEqDsp_2());
    sendEq(CMD_SET_EQ_BAND, MODULE_ID_LEFTRIGHT_EQ, _pipeline->getLeftRightEq().getEqLeft(), false);
    sendEq(CMD_SET_EQ_BAND, MODULE_ID_LEFTRIGHT_EQ, _pipeline->getLeftRightEq().getEqRight(), true);

    // DynEQ
    uint8_t deqPkt[24];
    auto write32 = [&](int offset, int32_t val) {
        deqPkt[offset] = val & 0xFF;
        deqPkt[offset + 1] = (val >> 8) & 0xFF;
        deqPkt[offset + 2] = (val >> 16) & 0xFF;
        deqPkt[offset + 3] = (val >> 24) & 0xFF;
    };
    DynamicEQ &deq = _pipeline->getDynamicEq();
    write32(0, deq._lowThreshDb);
    write32(4, deq._normalThreshDb);
    write32(8, deq._highThreshDb);
    write32(12, deq._attackMs);
    write32(16, deq._releaseMs);
    write32(20, (int32_t)(deq._lookaheadMs * 10.0f + 0.5f));
    _uart->sendFrame(CMD_SET_DYNEQ_THRESH, MODULE_ID_DYNAMIC_EQ, deqPkt, 24);

    sendEq(CMD_SET_DYNEQ_LOW_BAND, MODULE_ID_DYNAMIC_EQ, deq._eqLow);
    sendEq(CMD_SET_DYNEQ_HIGH_BAND, MODULE_ID_DYNAMIC_EQ, deq._eqHigh);

    pkt[0] = _presetMgr->getCurrentPresetIndex();
    _uart->sendFrame(CMD_GET_CURRENT_PRESET_INDEX, MODULE_ID_SYSTEM, pkt, 1);

    // Tell host we're done
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
    // pregain(2) + band(1) + enabled(1) + type(1) + freq(2) + gain(2) + Q(2) = 11 bytes
    if (cmd.dataLen < 11) { _uart->sendAck(cmd.moduleId, 1); return; }

    int16_t pregainQ88 = extractInt16(&cmd.data[0]);
    uint8_t bandIdx    = cmd.data[2];
    EQFilterParams params;
    params.enabled = cmd.data[3] != 0;
    params.type    = cmd.data[4];
    params.f0      = extractUint16(&cmd.data[5]);
    params.gain    = extractInt16(&cmd.data[7]);
    params.Q       = extractUint16(&cmd.data[9]);

    ParametricEQ *eq = nullptr;
    uint8_t realBand = bandIdx;
    if (cmd.moduleId == MODULE_ID_EQ_DSP_1) {
        eq = &_pipeline->getEqDsp_1();
        eq->setPregain(pregainQ88);
    } else if (cmd.moduleId == MODULE_ID_EQ_DSP_2) {
        eq = &_pipeline->getEqDsp_2();
        eq->setPregain(pregainQ88); // q8.8 format;
    } else if (cmd.moduleId == MODULE_ID_LEFTRIGHT_EQ) {
        if (bandIdx & 0x80) {
            eq = &_pipeline->getLeftRightEq().getEqRight();
            realBand = bandIdx & 0x7F;
        } else {
            eq = &_pipeline->getLeftRightEq().getEqLeft();
        }
        eq->setPregain(pregainQ88);
    } else if (cmd.moduleId == MODULE_ID_DYNAMIC_EQ) {
        if (cmd.cmd == CMD_SET_DYNEQ_LOW_BAND) {
            eq = &_pipeline->getDynamicEq().getEqLow();
        } else if (cmd.cmd == CMD_SET_DYNEQ_HIGH_BAND) {
            eq = &_pipeline->getDynamicEq().getEqHigh();
        }
        eq->setPregain(pregainQ88);
    } else if (cmd.moduleId == MODULE_ID_PRE_EQ) {
        eq = &_pipeline->getPreEq();
    } else {
        _uart->sendAck(cmd.moduleId, 1); return;
    }
    if (eq && realBand < MAX_EQ_BANDS) {
        eq->setBand(realBand, params); // q8.8 format
    } else {
        _uart->sendAck(cmd.moduleId, 1); return;
    }
    LOG_INFO(TAG, "PARAMETRIC_EQ: eq:%s pregain:%f dB, bandIdx:%d, type:%d, freq:%d Hz, gain:%f dB, Q:%f",
        (cmd.moduleId == MODULE_ID_LEFTRIGHT_EQ) ? "Left/Right" :
        (cmd.moduleId == MODULE_ID_DYNAMIC_EQ && cmd.cmd == CMD_SET_DYNEQ_LOW_BAND) ? "DynamicEQ LOW" :
        (cmd.moduleId == MODULE_ID_DYNAMIC_EQ && cmd.cmd == CMD_SET_DYNEQ_HIGH_BAND) ? "DynamicEQ HIGH" :
        (cmd.moduleId == MODULE_ID_PRE_EQ) ? "Pre" :
        (cmd.moduleId == MODULE_ID_EQ_DSP_1) ? "1" : "2",
        (float)pregainQ88 / 256.0f, bandIdx, params.type, params.f0, (float)params.gain / 256.0f, (float)params.Q / 256.0f);
    _uart->sendAck(cmd.moduleId, 0);
}

void ParamController::handleSetDynEqThresholds(const UartCommand& cmd) {
    // low(4)+normal(4)+high(4)+attack(4)+release(4) = 20 bytes
    if (cmd.dataLen < 24) { _uart->sendAck(cmd.moduleId, 1); return; }
    DynamicEQ& deq = _pipeline->getDynamicEq();
    deq.setLowEnergyThreshold(extractInt32(&cmd.data[0]));
    deq.setNormalEnergyThreshold(extractInt32(&cmd.data[4]));
    deq.setHighEnergyThreshold(extractInt32(&cmd.data[8]));
    deq.setAttackTime(extractInt32(&cmd.data[12]));
    deq.setReleaseTime(extractInt32(&cmd.data[16]));
    deq.setLookahead((float)extractInt32(&cmd.data[20]) / 10.0f);
    _uart->sendAck(cmd.moduleId, 0);
}

void ParamController::handleSetParam(const UartCommand& cmd) {
    // paramId(1) + value(4) = 5 bytes
    if (cmd.dataLen < 5) { _uart->sendAck(cmd.moduleId, 1); return; }
    uint8_t paramId  = cmd.data[0];
    int32_t value    = extractInt32(&cmd.data[1]);

    switch (cmd.moduleId) {
        case MODULE_ID_PRE_GAIN:
            if (paramId == 0) _pipeline->getPreGain().setGainDb((int16_t)value);
            else if (paramId == 1) _pipeline->getPreGain().setMute(value != 0);
            else if (paramId == 2) _pipeline->getPreGain().setMono(value != 0);
            break;
        case MODULE_ID_POST_GAIN:
            if (paramId == 0) _pipeline->getPostGain().setGainDb((int16_t)value);
            else if (paramId == 1) _pipeline->getPostGain().setMute(value != 0);
            else if (paramId == 2) _pipeline->getPostGain().setMono(value != 0);
            break;
        case MODULE_ID_COMPANDER:
            switch (paramId) {
                case 0: _pipeline->getCompander().setThreshold(value);   break;
                case 1: _pipeline->getCompander().setRatioBelow(value);  break;
                case 2: _pipeline->getCompander().setRatioAbove(value);  break;
                case 3: _pipeline->getCompander().setAttackTime(value);  break;
                case 4: _pipeline->getCompander().setReleaseTime(value); break;
                case 5: _pipeline->getCompander().setPregain(value);     break;
                case 6: _pipeline->getCompander().setLookahead((float)value / 10.0f); break; // ms×10 → ms
            }
            break;
        case MODULE_ID_EXCITER:
            switch (paramId) {
                case 0: _pipeline->getExciter().setCutoffFreq(value); break;
                case 1: _pipeline->getExciter().setDry(value);        break;
                case 2: _pipeline->getExciter().setWet(value);        break;
            }
            break;
        case MODULE_ID_DYNAMIC_BASS:
            switch (paramId) {
                case 0: _pipeline->getDynamicBass().setCutoffFreq(value);         break;
                case 1: _pipeline->getDynamicBass().setGainBoost(value);          break;
                case 2: _pipeline->getDynamicBass().setEnhanced(value);           break;
                case 3: _pipeline->getDynamicBass().setBoostFullThreshold(value); break;
                case 4: _pipeline->getDynamicBass().setNeutralThreshold(value);   break;
                case 5: _pipeline->getDynamicBass().setClipFullThreshold(value);  break;
                case 6: _pipeline->getDynamicBass().setClipAttack(value);          break;
                case 7: _pipeline->getDynamicBass().setClipRelease(value);         break;
                case 8: _pipeline->getDynamicBass().setLookahead((float)value / 10.0f); break; // ms×10 → ms
            }
            break;
        case MODULE_ID_DRC: {
            DRC &drc = _pipeline->getDrc();

            // ── Global params (paramId 0x10 - 0x1F) ────────────────────────
            if (paramId == 0x10) { drc.setMode((DRCMode)value);                   break; }
            if (paramId == 0x11) { drc.setCrossoverType((DRCCrossoverType)value); break; }
            if (paramId == 0x12) { drc.setCrossoverFreq(0, value);                break; }
            if (paramId == 0x13) { drc.setCrossoverFreq(1, value);                break; }
            if (paramId == 0x14) { drc.setCrossoverQ(0, value);                   break; }
            if (paramId == 0x15) { drc.setCrossoverQ(1, value);                   break; }

            // ── Per-band params (paramId 0x20 - 0x3F) ──────────────────────
            // Encoding: paramId = 0x20 + band*8 + param
            //   band 0 → 0x20-0x27, band 1 → 0x28-0x2F
            //   band 2 → 0x30-0x37, band 3 (fullband) → 0x38-0x3F
            // param: 0=threshold, 1=ratio, 2=attack, 3=release, 4=pregain
            if (paramId >= 0x20 && paramId <= 0x3F) {
                uint8_t band  = (paramId - 0x20) >> 3;  // 0-3
                uint8_t param = (paramId - 0x20) & 0x07; // 0-4
                switch (param) {
                    case 0: drc.setThreshold(band, value);    break;
                    case 1: drc.setRatio(band, value);        break;
                    case 2: drc.setAttackTime(band, value);   break;
                    case 3: drc.setReleaseTime(band, value);  break;
                    case 4: drc.setPregain(band, value);      break;
                    case 5: drc.setLookahead(band, (float)value / 10.0f); break; // ms×10 → ms
                    default: _uart->sendError(0x04); return;
                }
            }
            break;
        }
            LOG_WARN(TAG, "SET_PARAM: unhandled moduleId 0x%02X", cmd.moduleId);
            _uart->sendAck(cmd.moduleId, 1);
            return;
    }
    _uart->sendAck(cmd.moduleId, 0);
}

// WiFi handlers — unchanged, just dispatch
#include <WiFi.h>

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