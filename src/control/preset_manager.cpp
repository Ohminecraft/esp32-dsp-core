/**
 * @file preset_manager.cpp
 * @brief NVS preset storage implementation
 *
 * Stores basic enable/disable state and key parameters for each module.
 * Full EQ band data is stored as raw bytes.
 */

#include "preset_manager.h"
#include "../utils/debug_log.h"

#define TAG "PRESET"

struct PresetData {
  uint16_t en_mask;
  int16_t vol_db;
  int16_t pre_vol_db;

  // Compander
  int32_t cp_thresholdDb, cp_ratioBelow, cp_ratioAbove, cp_attackMs,
      cp_releaseMs, cp_pregainQ412;
  // Exciter
  int32_t ex_cutoffFreq, ex_dry, ex_wet;
  // Dynamic Bass
  int32_t db_cutoffFreq, db_gainBoost, db_enhanced, db_boostfullthreshold, db_neutralthreshold, db_clipfullthreshold, db_clipattack, db_cliprelease;
  // DRC — fullband (band[3]) settings only in preset
  int32_t drc_thresholdDb, drc_ratio, drc_attackMs, drc_releaseMs, drc_pregainQ412;
  int32_t drc_mode;

  // EQ1 & EQ2
  int16_t eq1_pregain_q88;
  EQFilterParams eq1_bands[MAX_EQ_BANDS];
  int16_t eq2_pregain_q88;
  EQFilterParams eq2_bands[MAX_EQ_BANDS];

  // Dynamic EQ
  int32_t deq_lowThresh, deq_normThresh, deq_highThresh, deq_attackMs,
      deq_releaseMs;
  uint8_t deq_low_cnt;
  int16_t deq_low_pregain_q88;
  EQFilterParams deq_low_bands[MAX_EQ_BANDS];
  uint8_t deq_high_cnt;
  int16_t deq_high_pregain_q88;
  EQFilterParams deq_high_bands[MAX_EQ_BANDS];

  // Left Right EQ
  int16_t eql_pregain_q88;
  EQFilterParams eql_bands[MAX_EQ_BANDS];
  int16_t eqr_pregain_q88;
  EQFilterParams eqr_bands[MAX_EQ_BANDS];

  bool valid;
};

void PresetManager::init() {
  for (int i = 0; i < MAX_PRESET_SLOTS; i++) {
    if (!hasPreset(i)) {
      saveDefault(i);
    }
  }
}

void PresetManager::saveDefault(uint8_t slot) {
  LOG_INFO(TAG, "Initializing default data for Preset Slot %d", slot);
  
  static PresetData pd;
  memset(&pd, 0, sizeof(PresetData));
  pd.valid = true;

  pd.en_mask = (1 << 0) | (1 << 9); // only pre/post gain turn on

  pd.vol_db = 0;
  pd.pre_vol_db = 0;

  pd.cp_thresholdDb = -2000;
  pd.cp_ratioBelow = 100; // 1.00:1
  pd.cp_ratioAbove = 400; // 4.00:1
  pd.cp_attackMs = 10;
  pd.cp_releaseMs = 200;
  pd.cp_pregainQ412 = 4096;

  pd.ex_cutoffFreq = 3000;
  pd.db_cutoffFreq = 60;
  pd.db_gainBoost = 600;   // +6.00 dB default
  pd.db_clipattack = 600;
  pd.db_cliprelease = 200;
  pd.db_clipfullthreshold = -800;
  pd.db_neutralthreshold = -1600;
  pd.db_boostfullthreshold = -2400;

  pd.drc_thresholdDb = -1500;
  pd.drc_ratio = 400;
  pd.drc_attackMs = 5;
  pd.drc_releaseMs = 160;
  pd.drc_pregainQ412 = 4096;
  pd.drc_mode = DRC_MODE_FULLBAND;

  pd.deq_lowThresh = -4000;
  pd.deq_normThresh = -2000;
  pd.deq_highThresh = -600;
  pd.deq_attackMs = 10;
  pd.deq_releaseMs = 100;

  for(int b=0; b<MAX_EQ_BANDS; b++) {
    pd.eq1_bands[b].enabled = false;
    pd.eq1_bands[b].f0 = 1000;
    pd.eq1_bands[b].Q = 724; // 0.707 * 1024
    pd.eq2_bands[b] = pd.eq1_bands[b];
    pd.deq_low_bands[b] = pd.eq1_bands[b];
    pd.deq_high_bands[b] = pd.eq1_bands[b];
    pd.eql_bands[b] = pd.eq1_bands[b];
    pd.eqr_bands[b] = pd.eq1_bands[b];
  }

  String key = getSlotKey(slot);
  _prefs.begin(key.c_str(), false);
  _prefs.putBytes("blob", &pd, sizeof(PresetData));
  _prefs.end();
}

bool PresetManager::savePreset(uint8_t slot, DspPipeline &pipeline) {
  if (slot >= MAX_PRESET_SLOTS)
    return false;

  static PresetData pd;
  memset(&pd, 0, sizeof(PresetData));
  pd.valid = true;

  uint16_t enableMask = 0;
  DspModule **chain = pipeline.getChain();
  for (size_t i = 0; i < pipeline.getChainLength(); i++) {
    if (chain[i]->isEnabled())
      enableMask |= (1 << i);
  }
  pd.en_mask = enableMask;
  pd.vol_db = pipeline.getPostGain()._gainDb;
  pd.pre_vol_db = pipeline.getPreGain()._gainDb;

  // CP
  pd.cp_thresholdDb = pipeline.getCompander()._thresholdDbInt;
  pd.cp_ratioBelow = pipeline.getCompander()._ratioBelowQ88;
  pd.cp_ratioAbove = pipeline.getCompander()._ratioAboveQ88;
  pd.cp_attackMs = pipeline.getCompander()._attackMs;
  pd.cp_releaseMs = pipeline.getCompander()._releaseMs;
  pd.cp_pregainQ412 = pipeline.getCompander()._pregainQ412;

  // EX
  pd.ex_cutoffFreq = pipeline.getExciter()._fCut;
  pd.ex_dry = pipeline.getExciter()._dry;
  pd.ex_wet = pipeline.getExciter()._wet;

  // DB
  pd.db_cutoffFreq = pipeline.getDynamicBass().getCutoffFreq();
  pd.db_gainBoost = pipeline.getDynamicBass().getGainBoost();
  pd.db_enhanced = pipeline.getDynamicBass().getEnhanced();
  pd.db_clipfullthreshold = pipeline.getDynamicBass().getClipFullThresh();
  pd.db_neutralthreshold = pipeline.getDynamicBass().getNeutralThresh();
  pd.db_boostfullthreshold = pipeline.getDynamicBass().getBoostFullThresh();
  pd.db_clipattack = pipeline.getDynamicBass().getClipAttack();
  pd.db_cliprelease = pipeline.getDynamicBass().getClipRelease();

  // DRC — save fullband (band[3]) settings
  pd.drc_thresholdDb  = pipeline.getDrc()._bands[3].thresholdDbInt;
  pd.drc_ratio        = pipeline.getDrc()._bands[3].ratioX100;
  pd.drc_attackMs     = pipeline.getDrc()._bands[3].attackMs;
  pd.drc_releaseMs    = pipeline.getDrc()._bands[3].releaseMs;
  pd.drc_pregainQ412  = pipeline.getDrc()._bands[3].pregainQ412;
  pd.drc_mode         = (int32_t)pipeline.getDrc()._mode;

  // EQs
  pd.eq1_pregain_q88 = pipeline.getEqDsp_1().getPregain();
  for (int i = 0; i < MAX_EQ_BANDS; i++)
    pd.eq1_bands[i] = pipeline.getEqDsp_1()._params[i];

  pd.eq2_pregain_q88 = pipeline.getEqDsp_2().getPregain();
  for (int i = 0; i < MAX_EQ_BANDS; i++)
    pd.eq2_bands[i] = pipeline.getEqDsp_2()._params[i];

  // DynEQ
  pd.deq_lowThresh = pipeline.getDynamicEq()._lowThreshDb;
  pd.deq_normThresh = pipeline.getDynamicEq()._normalThreshDb;
  pd.deq_highThresh = pipeline.getDynamicEq()._highThreshDb;
  pd.deq_attackMs = pipeline.getDynamicEq()._attackMs;
  pd.deq_releaseMs = pipeline.getDynamicEq()._releaseMs;

  pd.deq_low_pregain_q88 = pipeline.getDynamicEq().getEqLow().getPregain();
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

  // Write object
  String key = getSlotKey(slot);
  _prefs.begin(key.c_str(), false);
  _prefs.putBytes("blob", &pd, sizeof(PresetData));
  _prefs.end();
  LOG_INFO(TAG, "Saved preset to slot %d", slot);
  return true;
}

bool PresetManager::loadPreset(uint8_t slot, DspPipeline &pipeline) {
  if (slot >= MAX_PRESET_SLOTS)
    return false;

  String key = getSlotKey(slot);
  _prefs.begin(key.c_str(), true);

  static PresetData pd;
  memset(&pd, 0, sizeof(PresetData));
  size_t len = _prefs.getBytesLength("blob");
  if (len != sizeof(PresetData)) {
    _prefs.end();
    LOG_WARN(TAG, "Preset slot %d is empty or incompatible. Loading defaults...", slot);
    // DO NOT RECURSE! Just load defaults if possible or return false.
    return false; 
  }
  _prefs.getBytes("blob", &pd, sizeof(PresetData));
  _prefs.end();

  if (!pd.valid) {
    LOG_WARN(TAG, "Preset slot %d invalid flag. Loading defaults...", slot);
    return false;
  }

  if (pd.cp_pregainQ412 <= 0) pd.cp_pregainQ412 = 4096;
  if (pd.cp_ratioBelow < 10) pd.cp_ratioBelow = 100;
  if (pd.cp_ratioAbove < 100) pd.cp_ratioAbove = 400;
  if (pd.cp_attackMs <= 0) pd.cp_attackMs = 10;
  if (pd.cp_releaseMs <= 0) pd.cp_releaseMs = 100;

  if (pd.drc_thresholdDb > 0 && pd.drc_ratio < 100) {
    pd.drc_thresholdDb = -1500;
    pd.drc_ratio = 400;
    pd.drc_attackMs = 5;
    pd.drc_releaseMs = 160;
    pd.drc_pregainQ412 = 4096;
    pd.drc_mode = DRC_MODE_FULLBAND;
  }
  if (pd.drc_pregainQ412 <= 0) pd.drc_pregainQ412 = 4096;
  if (pd.drc_ratio < 100) pd.drc_ratio = 400;
  if (pd.drc_attackMs <= 0) pd.drc_attackMs = 5;
  if (pd.drc_releaseMs <= 0) pd.drc_releaseMs = 160;
  if (pd.drc_mode < DRC_MODE_FULLBAND || pd.drc_mode > DRC_MODE_3BAND_FULLBAND) {
    pd.drc_mode = DRC_MODE_FULLBAND;
  }

  DspModule **chain = pipeline.getChain();
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

  // Some of these don't have setters, so we assign directly via friend access
  pipeline.getDynamicBass().setCutoffFreq(pd.db_cutoffFreq);
  pipeline.getDynamicBass().setGainBoost(pd.db_gainBoost);
  pipeline.getDynamicBass().setEnhanced(pd.db_enhanced);
  pipeline.getDynamicBass().setBoostFullThreshold(pd.db_boostfullthreshold);
  pipeline.getDynamicBass().setNeutralThreshold(pd.db_neutralthreshold);
  pipeline.getDynamicBass().setClipFullThreshold(pd.db_clipfullthreshold);
  pipeline.getDynamicBass().setClipAttack(pd.db_clipattack);
  pipeline.getDynamicBass().setClipRelease(pd.db_cliprelease);

  // Bypass setters to avoid missing method errors and force an exact state
  // restoration. EQs via API
  pipeline.getEqDsp_1().setPregain(pd.eq1_pregain_q88);
  for (int i = 0; i < MAX_EQ_BANDS; i++)
    pipeline.getEqDsp_1().setBand(i, pd.eq1_bands[i]);

  pipeline.getEqDsp_2().setPregain(pd.eq2_pregain_q88);
  for (int i = 0; i < MAX_EQ_BANDS; i++)
    pipeline.getEqDsp_2().setBand(i, pd.eq2_bands[i]);

  // DynEQ
  pipeline.getDynamicEq().setLowEnergyThreshold(pd.deq_lowThresh);
  pipeline.getDynamicEq().setNormalEnergyThreshold(pd.deq_normThresh);
  pipeline.getDynamicEq().setHighEnergyThreshold(pd.deq_highThresh);
  pipeline.getDynamicEq().setAttackTime(pd.deq_attackMs);
  pipeline.getDynamicEq().setReleaseTime(pd.deq_releaseMs);

  // Dynamic eq bands
  pipeline.getDynamicEq().getEqLow().setPregain(pd.deq_low_pregain_q88);
  for (int i = 0; i < MAX_EQ_BANDS; i++)
    pipeline.getDynamicEq().setEqLowBand(i, pd.deq_low_bands[i]);

  pipeline.getDynamicEq().getEqHigh().setPregain(pd.deq_high_pregain_q88);
  for (int i = 0; i < MAX_EQ_BANDS; i++)
    pipeline.getDynamicEq().setEqHighBand(i, pd.deq_high_bands[i]);

  // DRC — restore fullband (band[3]) settings
  pipeline.getDrc().setThreshold(3, pd.drc_thresholdDb);
  pipeline.getDrc().setRatio(3, pd.drc_ratio);
  pipeline.getDrc().setAttackTime(3, pd.drc_attackMs);
  pipeline.getDrc().setReleaseTime(3, pd.drc_releaseMs);
  pipeline.getDrc().setPregain(3, pd.drc_pregainQ412);
  pipeline.getDrc().setMode((DRCMode)pd.drc_mode);

  // Left Right EQ
  pipeline.getLeftRightEq().getEqLeft().setPregain(pd.eql_pregain_q88);
  for (int i = 0; i < MAX_EQ_BANDS; i++)
    pipeline.getLeftRightEq().setEqLeft(i, pd.eql_bands[i]);

  pipeline.getLeftRightEq().getEqRight().setPregain(pd.eqr_pregain_q88);
  for (int i = 0; i < MAX_EQ_BANDS; i++)
    pipeline.getLeftRightEq().setEqRight(i, pd.eqr_bands[i]);

  LOG_INFO(TAG, "Loaded preset from slot %d", slot);
  return true;
}

bool PresetManager::hasPreset(uint8_t slot) {
  if (slot >= MAX_PRESET_SLOTS)
    return false;
  String key = getSlotKey(slot);
  _prefs.begin(key.c_str(), true);
  size_t len = _prefs.getBytesLength("blob");
  _prefs.end();
  return len == sizeof(PresetData);
}

String PresetManager::getSlotKey(uint8_t slot) {
  return "dsp_s" + String(slot);
}
