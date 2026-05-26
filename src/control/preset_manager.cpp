/**
 * @file preset_manager.cpp
 * @brief NVS preset storage — AutoEQ replaced by ISF1 + ISF2
 *
 * IMPORTANT: DSP_MODULE_COUNT changed from 11 → 12 (ISF2 added).
 * sizeof(PresetData) is different from old builds — old presets will
 * be automatically detected as incompatible and re-initialized.
 */

#include "preset_manager.h"
#include "../utils/debug_log.h"

#define TAG "PRESET"

// ─────────────────────────────────────────────────────────────────────────────
// Compact storage structs (NVS — no computed coefficients)
// ─────────────────────────────────────────────────────────────────────────────

struct ISFBandData {
    uint8_t  enabled;
    uint8_t  type;
    uint16_t f0;
    int16_t  gain;    // Q8.8
    uint16_t Q;       // Q6.10
};  // 8 bytes

struct ISFPresetData {
    int16_t     thresholdDb;   // Q8.8
    int16_t     pregainDb;     // Q8.8
    uint8_t     _pad;          // alignment
    ISFBandData bands[MAX_EQ_BANDS];
};  // 2+2+1+1+10*8 = 86 bytes

struct ISFInstanceData {
    uint8_t      numPresets;
    uint8_t      _pad[1];
    int16_t      rmsMs;        // RMS window ms
    int16_t      slewMs;       // slew time per index step ms
    ISFPresetData presets[ISF_MAX_PRESETS];
};  // 1+1+2+2+10*86 = 866 bytes

// ─────────────────────────────────────────────────────────────────────────────
// PresetData — full NVS blob
// ─────────────────────────────────────────────────────────────────────────────

struct PresetData {
    // ── Basic ─────────────────────────────────────────────────────────────────
    uint16_t en_mask;       // Chain enable bits (now 12 bits for 12 modules)
    int16_t  vol_db;
    int16_t  pre_vol_db;

    // ── Compander ─────────────────────────────────────────────────────────────
    int32_t cp_thresholdDb, cp_ratioBelow, cp_ratioAbove;
    int32_t cp_attackMs, cp_releaseMs, cp_pregainQ412;

    // ── Exciter ───────────────────────────────────────────────────────────────
    int32_t ex_cutoffFreq, ex_dry, ex_wet;

    // ── Dynamic Bass ──────────────────────────────────────────────────────────
    int32_t db_cutoffFreq, db_gainBoost, db_enhanced;
    int32_t db_boostfullthreshold, db_neutralthreshold;
    int32_t db_clipfullthreshold, db_clipattack, db_cliprelease;

    // ── DRC ───────────────────────────────────────────────────────────────────
    int32_t drc_thresholdDb, drc_ratio, drc_attackMs, drc_releaseMs, drc_pregainQ412;
    int32_t drc_mode;

    // ── EQ1 / EQ2 ─────────────────────────────────────────────────────────────
    int16_t        eq1_pregain_q88;
    EQFilterParams eq1_bands[MAX_EQ_BANDS];
    int16_t        eq2_pregain_q88;
    EQFilterParams eq2_bands[MAX_EQ_BANDS];

    // ── Dynamic EQ ────────────────────────────────────────────────────────────
    int32_t deq_lowThresh, deq_normThresh, deq_highThresh;
    int32_t deq_attackMs, deq_releaseMs;
    int16_t deq_low_pregain_q88;
    EQFilterParams deq_low_bands[MAX_EQ_BANDS];
    int16_t deq_high_pregain_q88;
    EQFilterParams deq_high_bands[MAX_EQ_BANDS];

    // ── Left Right EQ ─────────────────────────────────────────────────────────
    int16_t        eql_pregain_q88;
    EQFilterParams eql_bands[MAX_EQ_BANDS];
    int16_t        eqr_pregain_q88;
    EQFilterParams eqr_bands[MAX_EQ_BANDS];

    // ── ISF (replaces AutoEQ) ─────────────────────────────────────────────────
    ISFInstanceData isf1;  // 866 bytes
    ISFInstanceData isf2;  // 866 bytes

    bool valid;
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper: default ISFInstanceData
// ─────────────────────────────────────────────────────────────────────────────

static void makeDefaultIsfInstance(ISFInstanceData& inst,
                                   bool withLoudnessCurve,
                                   int32_t sampleRate)
{
    memset(&inst, 0, sizeof(ISFInstanceData));
    inst.rmsMs  = ISF_DEFAULT_RMS_MS;
    inst.slewMs = ISF_DEFAULT_SLEW_MS;

    if (!withLoudnessCurve) {
        // Flat passthrough — 1 preset at -96 dB threshold
        inst.numPresets = 1;
        inst.presets[0].thresholdDb = (int16_t)(-96.0f * 256.0f);
        inst.presets[0].pregainDb   = 0;
        return;
    }

    // ISF1: loudness-dependent bass EQ
    // 5 presets covering -50 to -10 dBFS
    // Quiet → heavy bass boost; Loud → less boost
    inst.numPresets = 5;

    struct CurvePoint { float threshDb; float bassGain; float presenceGain; };
    static const CurvePoint curve[5] = {
        { -96.0f,  +6.0f, +2.0f },  // Preset 0: very quiet
        { -50.0f,  +4.5f, +1.5f },  // Preset 1: quiet
        { -35.0f,  +3.0f, +1.0f },  // Preset 2: normal
        { -25.0f,  +1.5f,  0.0f },  // Preset 3: loud
        { -15.0f,   0.0f, -1.0f },  // Preset 4: very loud
    };

    for (int p = 0; p < 5; p++) {
        ISFPresetData& pd = inst.presets[p];
        pd.thresholdDb = (int16_t)(curve[p].threshDb * 256.0f);
        pd.pregainDb   = 0;

        // Band 0: Low shelf 80 Hz (bass boost)
        pd.bands[0].enabled = 1;
        pd.bands[0].type    = EQ_FILTER_TYPE_LOW_SHELF;
        pd.bands[0].f0      = 80;
        pd.bands[0].gain    = (int16_t)(curve[p].bassGain * 256.0f);
        pd.bands[0].Q       = 724;  // 0.707 * 1024

        // Band 1: Peaking 200 Hz (low-mid cleanup)
        pd.bands[1].enabled = 1;
        pd.bands[1].type    = EQ_FILTER_TYPE_PEAKING;
        pd.bands[1].f0      = 200;
        pd.bands[1].gain    = (int16_t)(-1.0f * 256.0f);  // slight cut
        pd.bands[1].Q       = 1448;  // ~1.414 * 1024

        // Band 2: Peaking 3kHz (presence)
        pd.bands[2].enabled = 1;
        pd.bands[2].type    = EQ_FILTER_TYPE_PEAKING;
        pd.bands[2].f0      = 3000;
        pd.bands[2].gain    = (int16_t)(curve[p].presenceGain * 256.0f);
        pd.bands[2].Q       = 1448;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: copy ISFInstanceData → IndexSelectableFilter
// ─────────────────────────────────────────────────────────────────────────────

static void loadIsfInstance(IndexSelectableFilter& isf,
                             const ISFInstanceData& data,
                             int32_t sampleRate)
{
    isf.setRmsWindowMs(data.rmsMs  > 0 ? data.rmsMs  : ISF_DEFAULT_RMS_MS);
    isf.setSlewMs     (data.slewMs > 0 ? data.slewMs : ISF_DEFAULT_SLEW_MS);
    isf.setNumPresets (data.numPresets);

    for (int p = 0; p < data.numPresets && p < ISF_MAX_PRESETS; p++) {
        const ISFPresetData& pd = data.presets[p];
        ISFPreset preset;
        preset.thresholdDb = pd.thresholdDb;
        preset.filters.setPregain(pd.pregainDb);
        for (int b = 0; b < MAX_EQ_BANDS; b++) {
            EQFilterParams params;
            params.enabled = pd.bands[b].enabled != 0;
            params.type    = pd.bands[b].type;
            params.f0      = pd.bands[b].f0;
            params.gain    = pd.bands[b].gain;
            params.Q       = pd.bands[b].Q;
            preset.filters.setBand(b, params);
        }
        isf.setPreset((uint8_t)p, preset, ISF_SET_ALL);
    }

}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: copy IndexSelectableFilter → ISFInstanceData
// ─────────────────────────────────────────────────────────────────────────────

static void saveIsfInstance(ISFInstanceData& data,
                             IndexSelectableFilter& isf)
{
    data.rmsMs      = (int16_t)isf.getRmsWindowMs();
    data.slewMs     = (int16_t)isf.getSlewMs();
    data.numPresets = isf.getNumPresets();

    for (int p = 0; p < data.numPresets && p < ISF_MAX_PRESETS; p++) {
        const ISFPreset& preset = isf.getPreset((uint8_t)p);
        ISFPresetData& pd = data.presets[p];
        pd.thresholdDb = preset.thresholdDb;
        pd.pregainDb   = preset.filters.getPregain();
        for (int b = 0; b < ISF_MAX_BANDS; b++) {
            EQFilterParams currentBandPreset = preset.filters.getBandParams(b);
            pd.bands[b].enabled = currentBandPreset.enabled;
            pd.bands[b].type    = currentBandPreset.type;
            pd.bands[b].f0      = currentBandPreset.f0;
            pd.bands[b].gain    = currentBandPreset.gain;
            pd.bands[b].Q       = currentBandPreset.Q;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// init
// ─────────────────────────────────────────────────────────────────────────────

void PresetManager::init() {
    for (int i = 0; i < MAX_PRESET_SLOTS; i++) {
        if (!hasPreset(i)) saveDefault(i);
    }
}

void PresetManager::saveCurrentSlotIndex(uint8_t slot) {
    if (slot >= MAX_PRESET_SLOTS) return;
    _prefs.begin("meta", false);
    _prefs.putUChar("cur_slot", slot);
    _prefs.end();
}

uint8_t PresetManager::getCurrentSlotIndex() {
    _prefs.begin("meta", true);
    uint8_t slot = _prefs.getUChar("cur_slot", 0);
    _prefs.end();
    if (slot >= MAX_PRESET_SLOTS) return 0;
    return slot;
}

// ─────────────────────────────────────────────────────────────────────────────
// saveDefault
// ─────────────────────────────────────────────────────────────────────────────

void PresetManager::saveDefault(uint8_t slot) {
    LOG_INFO(TAG, "Initializing default preset for slot %d", slot);

    static PresetData pd;
    memset(&pd, 0, sizeof(PresetData));
    pd.valid = true;

    // Chain: [0]=preGain enabled, [11]=postGain enabled
    pd.en_mask    = (1u << 0) | (1u << 11);
    pd.vol_db     = 0;
    pd.pre_vol_db = 0;

    // Compander defaults
    pd.cp_thresholdDb  = -2000;
    pd.cp_ratioBelow   = 100;
    pd.cp_ratioAbove   = 400;
    pd.cp_attackMs     = 10;
    pd.cp_releaseMs    = 200;
    pd.cp_pregainQ412  = 4096;

    // Exciter defaults
    pd.ex_cutoffFreq = 3000;
    pd.ex_dry        = 100;
    pd.ex_wet        = 30;

    // Dynamic Bass defaults
    pd.db_cutoffFreq          = 60;
    pd.db_gainBoost           = 600;
    pd.db_clipattack          = 600;
    pd.db_cliprelease         = 200;
    pd.db_clipfullthreshold   = -800;
    pd.db_neutralthreshold    = -1600;
    pd.db_boostfullthreshold  = -2400;

    // DRC defaults
    pd.drc_thresholdDb = -1500;
    pd.drc_ratio       = 400;
    pd.drc_attackMs    = 5;
    pd.drc_releaseMs   = 160;
    pd.drc_pregainQ412 = 4096;
    pd.drc_mode        = DRC_MODE_FULLBAND;

    // DynEQ defaults
    pd.deq_lowThresh  = -4000;
    pd.deq_normThresh = -2000;
    pd.deq_highThresh = -600;
    pd.deq_attackMs   = 10;
    pd.deq_releaseMs  = 100;

    // EQ bands: all disabled defaults
    for (int b = 0; b < MAX_EQ_BANDS; b++) {
        EQFilterParams def;
        def.enabled = false;
        def.f0      = 1000;
        def.Q       = 724;
        def.gain    = 0;
        def.type    = 0;
        pd.eq1_bands[b] = pd.eq2_bands[b] =
        pd.deq_low_bands[b] = pd.deq_high_bands[b] =
        pd.eql_bands[b] = pd.eqr_bands[b] = def;
    }

    // ISF defaults:
    //   ISF1 = loudness-dependent bass curve (5 presets)
    //   ISF2 = flat passthrough (1 preset, ready for user customization)
    makeDefaultIsfInstance(pd.isf1, true,  DSP_SAMPLE_RATE_DEFAULT);
    makeDefaultIsfInstance(pd.isf2, false, DSP_SAMPLE_RATE_DEFAULT);

    String key = getSlotKey(slot);
    _prefs.begin(key.c_str(), false);
    _prefs.putBytes("blob", &pd, sizeof(PresetData));
    _prefs.end();

    Serial.printf("sizeof(PresetData) = %d bytes\n", sizeof(PresetData));
    Serial.printf("sizeof(EQFilterParams) = %d bytes\n", sizeof(EQFilterParams));
}

// ─────────────────────────────────────────────────────────────────────────────
// savePreset
// ─────────────────────────────────────────────────────────────────────────────

bool PresetManager::savePreset(uint8_t slot, DspPipeline& pipeline) {
    if (slot >= MAX_PRESET_SLOTS) return false;

    static PresetData pd;
    memset(&pd, 0, sizeof(PresetData));
    pd.valid = true;

    // Enable mask
    uint16_t mask = 0;
    DspModule** chain = pipeline.getChain();
    for (size_t i = 0; i < pipeline.getChainLength(); i++) {
        if (chain[i]->isEnabled()) mask |= (1u << i);
    }
    pd.en_mask    = mask;
    pd.vol_db     = pipeline.getPostGain()._gainDb;
    pd.pre_vol_db = pipeline.getPreGain()._gainDb;

    // Compander
    pd.cp_thresholdDb = pipeline.getCompander()._thresholdDbInt;
    pd.cp_ratioBelow  = pipeline.getCompander()._ratioBelowQ88;
    pd.cp_ratioAbove  = pipeline.getCompander()._ratioAboveQ88;
    pd.cp_attackMs    = pipeline.getCompander()._attackMs;
    pd.cp_releaseMs   = pipeline.getCompander()._releaseMs;
    pd.cp_pregainQ412 = pipeline.getCompander()._pregainQ412;

    // Exciter
    pd.ex_cutoffFreq = pipeline.getExciter()._fCut;
    pd.ex_dry        = pipeline.getExciter()._dry;
    pd.ex_wet        = pipeline.getExciter()._wet;

    // Dynamic Bass
    pd.db_cutoffFreq         = pipeline.getDynamicBass().getCutoffFreq();
    pd.db_gainBoost          = pipeline.getDynamicBass().getGainBoost();
    pd.db_enhanced           = pipeline.getDynamicBass().getEnhanced();
    pd.db_clipfullthreshold  = pipeline.getDynamicBass().getClipFullThresh();
    pd.db_neutralthreshold   = pipeline.getDynamicBass().getNeutralThresh();
    pd.db_boostfullthreshold = pipeline.getDynamicBass().getBoostFullThresh();
    pd.db_clipattack         = pipeline.getDynamicBass().getClipAttack();
    pd.db_cliprelease        = pipeline.getDynamicBass().getClipRelease();

    // DRC (fullband band[3])
    pd.drc_thresholdDb = pipeline.getDrc()._bands[3].thresholdDbInt;
    pd.drc_ratio       = pipeline.getDrc()._bands[3].ratioX100;
    pd.drc_attackMs    = pipeline.getDrc()._bands[3].attackMs;
    pd.drc_releaseMs   = pipeline.getDrc()._bands[3].releaseMs;
    pd.drc_pregainQ412 = pipeline.getDrc()._bands[3].pregainQ412;
    pd.drc_mode        = (int32_t)pipeline.getDrc()._mode;

    // EQ1 / EQ2
    pd.eq1_pregain_q88 = pipeline.getEqDsp_1().getPregain();
    for (int i = 0; i < MAX_EQ_BANDS; i++)
        pd.eq1_bands[i] = pipeline.getEqDsp_1()._params[i];

    pd.eq2_pregain_q88 = pipeline.getEqDsp_2().getPregain();
    for (int i = 0; i < MAX_EQ_BANDS; i++)
        pd.eq2_bands[i] = pipeline.getEqDsp_2()._params[i];

    // DynEQ
    pd.deq_lowThresh  = pipeline.getDynamicEq()._lowThreshDb;
    pd.deq_normThresh = pipeline.getDynamicEq()._normalThreshDb;
    pd.deq_highThresh = pipeline.getDynamicEq()._highThreshDb;
    pd.deq_attackMs   = pipeline.getDynamicEq()._attackMs;
    pd.deq_releaseMs  = pipeline.getDynamicEq()._releaseMs;

    pd.deq_low_pregain_q88  = pipeline.getDynamicEq().getEqLow().getPregain();
    for (int i = 0; i < MAX_EQ_BANDS; i++)
        pd.deq_low_bands[i] = pipeline.getDynamicEq()._eqLow._params[i];

    pd.deq_high_pregain_q88 = pipeline.getDynamicEq().getEqHigh().getPregain();
    for (int i = 0; i < MAX_EQ_BANDS; i++)
        pd.deq_high_bands[i] = pipeline.getDynamicEq()._eqHigh._params[i];

    // Left Right EQ
    pd.eql_pregain_q88 = pipeline.getLeftRightEq().getEqLeft().getPregain();
    for (int i = 0; i < MAX_EQ_BANDS; i++)
        pd.eql_bands[i] = pipeline.getLeftRightEq().getEqLeft()._params[i];

    pd.eqr_pregain_q88 = pipeline.getLeftRightEq().getEqRight().getPregain();
    for (int i = 0; i < MAX_EQ_BANDS; i++)
        pd.eqr_bands[i] = pipeline.getLeftRightEq().getEqRight()._params[i];

    // ISF
    saveIsfInstance(pd.isf1, pipeline.getIsf1());
    saveIsfInstance(pd.isf2, pipeline.getIsf2());

    String key = getSlotKey(slot);
    _prefs.begin(key.c_str(), false);
    _prefs.putBytes("blob", &pd, sizeof(PresetData));
    _prefs.end();

    LOG_INFO(TAG, "Saved preset to slot %d", slot);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// loadPreset
// ─────────────────────────────────────────────────────────────────────────────

bool PresetManager::loadPreset(uint8_t slot, DspPipeline& pipeline) {
    if (slot >= MAX_PRESET_SLOTS) return false;

    String key = getSlotKey(slot);
    _prefs.begin(key.c_str(), true);
    static PresetData pd;
    memset(&pd, 0, sizeof(PresetData));
    size_t len = _prefs.getBytesLength("blob");
    if (len != sizeof(PresetData)) {
        _prefs.end();
        LOG_WARN(TAG, "Slot %d incompatible (got %u, expected %u). Re-initializing.",
            slot, len, sizeof(PresetData));
        saveDefault(slot);
        return false;
    }
    _prefs.getBytes("blob", &pd, sizeof(PresetData));
    _prefs.end();

    if (!pd.valid) {
        LOG_WARN(TAG, "Slot %d invalid flag.", slot);
        return false;
    }

    // Sanity guards
    if (pd.cp_pregainQ412 <= 0) pd.cp_pregainQ412 = 4096;
    if (pd.cp_ratioBelow  < 10) pd.cp_ratioBelow  = 100;
    if (pd.cp_ratioAbove  < 100) pd.cp_ratioAbove = 400;
    if (pd.cp_attackMs    <= 0) pd.cp_attackMs    = 10;
    if (pd.cp_releaseMs   <= 0) pd.cp_releaseMs   = 100;
    if (pd.drc_pregainQ412 <= 0) pd.drc_pregainQ412 = 4096;
    if (pd.drc_ratio       < 100) pd.drc_ratio = 400;
    if (pd.drc_attackMs   <= 0) pd.drc_attackMs  = 5;
    if (pd.drc_releaseMs  <= 0) pd.drc_releaseMs = 160;
    if (pd.drc_mode < DRC_MODE_FULLBAND || pd.drc_mode > DRC_MODE_3BAND_FULLBAND)
        pd.drc_mode = DRC_MODE_FULLBAND;

    // Apply enable mask
    DspModule** chain = pipeline.getChain();
    for (size_t i = 0; i < pipeline.getChainLength(); i++) {
        chain[i]->setEnabled((pd.en_mask >> i) & 1);
    }

    pipeline.getPostGain().setGainDb(pd.vol_db);
    pipeline.getPreGain().setGainDb(pd.pre_vol_db);

    pipeline.getCompander().setThreshold(pd.cp_thresholdDb);
    pipeline.getCompander().setRatioBelow(pd.cp_ratioBelow);
    pipeline.getCompander().setRatioAbove(pd.cp_ratioAbove);
    pipeline.getCompander().setAttackTime(pd.cp_attackMs);
    pipeline.getCompander().setReleaseTime(pd.cp_releaseMs);
    pipeline.getCompander().setPregain(pd.cp_pregainQ412);

    pipeline.getExciter().setCutoffFreq(pd.ex_cutoffFreq);
    pipeline.getExciter().setDry(pd.ex_dry);
    pipeline.getExciter().setWet(pd.ex_wet);

    pipeline.getDynamicBass().setCutoffFreq(pd.db_cutoffFreq);
    pipeline.getDynamicBass().setGainBoost(pd.db_gainBoost);
    pipeline.getDynamicBass().setEnhanced(pd.db_enhanced);
    pipeline.getDynamicBass().setBoostFullThreshold(pd.db_boostfullthreshold);
    pipeline.getDynamicBass().setNeutralThreshold(pd.db_neutralthreshold);
    pipeline.getDynamicBass().setClipFullThreshold(pd.db_clipfullthreshold);
    pipeline.getDynamicBass().setClipAttack(pd.db_clipattack);
    pipeline.getDynamicBass().setClipRelease(pd.db_cliprelease);

    pipeline.getEqDsp_1().setPregain(pd.eq1_pregain_q88);
    for (int i = 0; i < MAX_EQ_BANDS; i++)
        pipeline.getEqDsp_1().setBand(i, pd.eq1_bands[i]);

    pipeline.getEqDsp_2().setPregain(pd.eq2_pregain_q88);
    for (int i = 0; i < MAX_EQ_BANDS; i++)
        pipeline.getEqDsp_2().setBand(i, pd.eq2_bands[i]);

    pipeline.getDynamicEq().setLowEnergyThreshold(pd.deq_lowThresh);
    pipeline.getDynamicEq().setNormalEnergyThreshold(pd.deq_normThresh);
    pipeline.getDynamicEq().setHighEnergyThreshold(pd.deq_highThresh);
    pipeline.getDynamicEq().setAttackTime(pd.deq_attackMs);
    pipeline.getDynamicEq().setReleaseTime(pd.deq_releaseMs);

    pipeline.getDynamicEq().getEqLow().setPregain(pd.deq_low_pregain_q88);
    for (int i = 0; i < MAX_EQ_BANDS; i++)
        pipeline.getDynamicEq().setEqLowBand(i, pd.deq_low_bands[i]);

    pipeline.getDynamicEq().getEqHigh().setPregain(pd.deq_high_pregain_q88);
    for (int i = 0; i < MAX_EQ_BANDS; i++)
        pipeline.getDynamicEq().setEqHighBand(i, pd.deq_high_bands[i]);

    pipeline.getDrc().setThreshold(3, pd.drc_thresholdDb);
    pipeline.getDrc().setRatio(3, pd.drc_ratio);
    pipeline.getDrc().setAttackTime(3, pd.drc_attackMs);
    pipeline.getDrc().setReleaseTime(3, pd.drc_releaseMs);
    pipeline.getDrc().setPregain(3, pd.drc_pregainQ412);
    pipeline.getDrc().setMode((DRCMode)pd.drc_mode);

    pipeline.getLeftRightEq().getEqLeft().setPregain(pd.eql_pregain_q88);
    for (int i = 0; i < MAX_EQ_BANDS; i++)
        pipeline.getLeftRightEq().setEqLeft(i, pd.eql_bands[i]);

    pipeline.getLeftRightEq().getEqRight().setPregain(pd.eqr_pregain_q88);
    for (int i = 0; i < MAX_EQ_BANDS; i++)
        pipeline.getLeftRightEq().setEqRight(i, pd.eqr_bands[i]);

    // ISF
    loadIsfInstance(pipeline.getIsf1(), pd.isf1, pipeline.getIsf1()._sampleRate);
    loadIsfInstance(pipeline.getIsf2(), pd.isf2, pipeline.getIsf2()._sampleRate);

    LOG_INFO(TAG, "Loaded preset from slot %d", slot);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// hasPreset / getSlotKey
// ─────────────────────────────────────────────────────────────────────────────

bool PresetManager::hasPreset(uint8_t slot) {
    if (slot >= MAX_PRESET_SLOTS) return false;
    String key = getSlotKey(slot);
    _prefs.begin(key.c_str(), true);
    size_t len = _prefs.getBytesLength("blob");
    _prefs.end();
    return len == sizeof(PresetData);
}

String PresetManager::getSlotKey(uint8_t slot) {
    return "dsp_s" + String(slot);
}