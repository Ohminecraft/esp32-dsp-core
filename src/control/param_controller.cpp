/**
 * @file param_controller.cpp
 * @brief Parameter dispatcher implementation
 */

#include "param_controller.h"
#include "../utils/debug_log.h"

#define TAG "CTRL"

void ParamController::init(DspPipeline *pipeline, AudioInput *input,
                           AudioOutput *output, UartProtocol *uart,
                           PresetManager *presetMgr) {
  _pipeline = pipeline;
  _input = input;
  _output = output;
  _uart = uart;
  _presetMgr = presetMgr;
}

void ParamController::handleCommand(const UartCommand &cmd) {
  if (!cmd.valid)
    return;

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
    handleSetDynEqBand(cmd, false);
    break;

  case CMD_SET_DYNEQ_HIGH_BAND:
    handleSetDynEqBand(cmd, true);
    break;

  case CMD_SET_DYNEQ_THRESH:
    handleSetDynEqThresholds(cmd);
    break;
  case CMD_GET_SYSTEM_INFO:
    handleGetSystemInfo(cmd);
    break;

  case CMD_SAVE_PRESET:
    if (cmd.dataLen > 0)
      _presetMgr->savePreset(cmd.data[0], *_pipeline);
    _uart->sendAck(MODULE_ID_SYSTEM, 0);
    break;

  case CMD_LOAD_PRESET:
    if (cmd.dataLen > 0)
      _presetMgr->loadPreset(cmd.data[0], *_pipeline);
    _uart->sendAck(MODULE_ID_SYSTEM, 0);
    break;

  case CMD_GET_ALL_STATE:
    handleGetAllState(cmd);
    break;

  case CMD_GET_MODULE_STATUS: {
    DspModule *mod = _pipeline->getModuleById(cmd.moduleId);
    if (mod) {
      uint8_t status = mod->isEnabled() ? 1 : 0;
      _uart->sendAck(cmd.moduleId, 0, &status, 1);
    } else {
      _uart->sendError(0x01); // Module not found
    }
    break;
  }

  default:
    _uart->sendError(0x02); // Unknown command
    break;
  }
}

void ParamController::handleEnableDisable(const UartCommand &cmd, bool enable) {
  DspModule *mod = _pipeline->getModuleById(cmd.moduleId);
  if (mod) {
    mod->setEnabled(enable);
    _uart->sendAck(cmd.moduleId, 0);
    LOG_INFO(TAG, "%s %s", mod->getName(), enable ? "ENABLED" : "DISABLED");
  } else {
    _uart->sendError(0x01);
  }
}

void ParamController::handleSetParam(const UartCommand &cmd) {
  if (cmd.dataLen < 5) {
    _uart->sendError(0x03);
    return;
  } // Invalid data length

  uint8_t paramId = cmd.data[0];
  int32_t value = extractInt32(&cmd.data[1]);

  switch (cmd.moduleId) {
  case MODULE_ID_NOISE_GATE: {
    NoiseGate &ng = _pipeline->getNoiseGate();
    switch (paramId) {
    case 0:
      ng.setLowerThreshold(value);
      break;
    case 1:
      ng.setUpperThreshold(value);
      break;
    case 2:
      ng.setAttackTime(value);
      break;
    case 3:
      ng.setReleaseTime(value);
      break;
    case 4:
      ng.setHoldTime(value);
      break;
    default:
      _uart->sendError(0x04);
      return;
    }
    break;
  }

  case MODULE_ID_COMPANDER: {
    Compander &cp = _pipeline->getCompander();
    switch (paramId) {
    case 0:
      cp.setThreshold(value);
      break;
    case 1:
      cp.setRatioBelow(value);
      break;
    case 2:
      cp.setRatioAbove(value);
      break;
    case 3:
      cp.setAttackTime(value);
      break;
    case 4:
      cp.setReleaseTime(value);
      break;
    case 5:
      cp.setPregain(value);
      break;
    default:
      _uart->sendError(0x04);
      return;
    }
    break;
  }

  case MODULE_ID_EXCITER: {
    Exciter &ex = _pipeline->getExciter();
    switch (paramId) {
    case 0:
      ex.setCutoffFreq(value);
      break;
    case 1:
      ex.setDry(value);
      break;
    case 2:
      ex.setWet(value);
      break;
    default:
      _uart->sendError(0x04);
      return;
    }
    break;
  }

  case MODULE_ID_VIRTUAL_BASS: {
    VirtualBass &vb = _pipeline->getVirtualBass();
    switch (paramId) {
    case 0:
      vb.setCutoffFreq(value);
      break;
    case 1:
      vb.setIntensity(value);
      break;
    case 2:
      vb.setEnhanced(value);
      break;
    default:
      _uart->sendError(0x04);
      return;
    }
    break;
  }

  case MODULE_ID_BASS_CLASSIC: {
    BassClassic &bc = _pipeline->getBassClassic();
    switch (paramId) {
    case 0:
      bc.setCutoffFreq(value);
      break;
    case 1:
      bc.setIntensity(value);
      break;
    default:
      _uart->sendError(0x04);
      return;
    }
    break;
  }

  case MODULE_ID_STEREO_WIDEN: {
    StereoWidener &sw = _pipeline->getStereoWidener();
    switch (paramId) {
    case 0:
      sw.setIntensity(value);
      break;
    default:
      _uart->sendError(0x04);
      return;
    }
    break;
  }

  case MODULE_ID_VOLUME: {
    VolumeControl &vol = _pipeline->getVolume();
    switch (paramId) {
    case 0:
      vol.setGainDb((int16_t)value);
      break;
    case 1:
      vol.setMute(value != 0);
      break;
    default:
      _uart->sendError(0x04);
      return;
    }
    break;
  }

  case MODULE_ID_SOFT_CLIP: {
    SoftClipper &sc = _pipeline->getSoftClipper();
    switch (paramId) {
    case 0:
      sc.setThreshold(value);
      break;
    case 1:
      sc.setMode(value);
      break;
    default:
      _uart->sendError(0x04);
      return;
    }
    break;
  }

  case MODULE_ID_DRC: {
    DRC &drc = _pipeline->getDrc();
    // paramId encodes: upper nibble = band (0-3), lower nibble = param
    uint8_t band = (paramId >> 4) & 0x0F;
    uint8_t param = paramId & 0x0F;
    switch (param) {
    case 0:
      drc.setThreshold(band, value);
      break;
    case 1:
      drc.setRatio(band, value);
      break;
    case 2:
      drc.setAttackTime(band, value);
      break;
    case 3:
      drc.setReleaseTime(band, value);
      break;
    case 4:
      drc.setPregain(band, value);
      break;
    case 5:
      drc.setMode((DRCMode)value);
      break;
    default:
      _uart->sendError(0x04);
      return;
    }
    break;
  }

  default:
    _uart->sendError(0x01); // Module not found
    return;
  }

  _uart->sendAck(cmd.moduleId, 0);
}

void ParamController::handleSetEqBand(const UartCommand &cmd) {
  // Data: pregain(2B) + band(1B) + type(1B) + freq(2B) + gain(2B) + Q(2B) = 10 bytes
  if (cmd.dataLen < 10) {
    _uart->sendError(0x03);
    return;
  }

  int16_t pregain_db = extractInt16(&cmd.data[0]);
  uint8_t band = cmd.data[2];
  EQFilterParams params;
  params.enabled = cmd.data[3];
  params.type = (int16_t)cmd.data[4];
  params.f0 = extractInt16(&cmd.data[5]);
  params.gain = extractInt16(&cmd.data[7]);
  params.Q = extractInt16(&cmd.data[9]);

  ParametricEQ *eq = nullptr;
  if (cmd.moduleId == MODULE_ID_EQ_DSP) {
    eq = &_pipeline->getEqDsp();
    eq->setPregain(pregain_db);
  } else if (cmd.moduleId == MODULE_ID_EQ_DSP_POST) {
    eq = &_pipeline->getEqDspPost();
    eq->setPregain(pregain_db); // q8.8 format;
  }

  if (eq && band < MAX_EQ_BANDS) {
    eq->setBand(band, params); // q8.8 format
  } else {
    _uart->sendError(0x04);
  }
}

void ParamController::handleSetDynEqBand(const UartCommand &cmd, bool isHigh) {
  if (cmd.dataLen < 10) {
    _uart->sendError(0x03);
    return;
  }

  int16_t pregain_db = extractInt16(&cmd.data[0]);
  uint8_t band = cmd.data[2];
  EQFilterParams params;
  params.enabled = cmd.data[3];
  params.type = (int16_t)cmd.data[4];
  params.f0 = extractInt16(&cmd.data[5]);
  params.gain = extractInt16(&cmd.data[7]);
  params.Q = extractInt16(&cmd.data[9]);

  DynamicEQ &deq = _pipeline->getDynamicEq();
  if (band < MAX_EQ_BANDS) {
    if (isHigh) {
      deq.setEqHighBand(band, params);
      deq.getEqHigh().setPregain(pregain_db); // q8.8 format
    } else {
      deq.setEqLowBand(band, params);
      deq.getEqLow().setPregain(pregain_db); // q8.8 format
    }
    _uart->sendAck(cmd.moduleId, 0);
  } else {
    _uart->sendError(0x04);
  }
}

void ParamController::handleSetDynEqThresholds(const UartCommand &cmd) {
  // Data: low(4B) + normal(4B) + high(4B) + atk(4B) + rel(4B) = 20 bytes
  if (cmd.dataLen < 20) {
    _uart->sendError(0x03);
    return;
  }

  DynamicEQ &deq = _pipeline->getDynamicEq();
  deq.setLowEnergyThreshold(extractInt32(&cmd.data[0]));
  deq.setNormalEnergyThreshold(extractInt32(&cmd.data[4]));
  deq.setHighEnergyThreshold(extractInt32(&cmd.data[8]));
  deq.setAttackTime(extractInt32(&cmd.data[12]));
  deq.setReleaseTime(extractInt32(&cmd.data[16]));

  _uart->sendAck(cmd.moduleId, 0);
}
void ParamController::handleGetSystemInfo(const UartCommand &cmd) {
  uint8_t info[6];
  info[0] = static_cast<uint8_t>(DSP_SAMPLE_RATE >> 8);
  info[1] = DSP_SAMPLE_RATE & 0xFF;
  info[2] = DSP_NUM_CHANNELS;
  info[3] = DSP_FRAME_SIZE & 0xFF;
  info[4] = DSP_MODULE_COUNT;
  info[5] = 0; // Reserved

  _uart->sendAck(MODULE_ID_SYSTEM, 0, info, sizeof(info));
}

int32_t ParamController::extractInt32(const uint8_t *data) {
  return (int32_t)data[0] | ((int32_t)data[1] << 8) | ((int32_t)data[2] << 16) |
         ((int32_t)data[3] << 24);
}

int16_t ParamController::extractInt16(const uint8_t *data) {
  return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

void ParamController::handleGetAllState(const UartCommand &cmd) {
  // We send back individual packets for each parameter and EQ band,
  // exactly replicating a master sending config to a slave.
  uint8_t pkt[16];

  // Enable states
  DspModule **chain = _pipeline->getChain();
  for (size_t i = 0; i < _pipeline->getChainLength(); i++) {
    DspModule *mod = chain[i];
    if (mod->isEnabled())
      _uart->sendFrame(CMD_ENABLE_MODULE, mod->getModuleId(), nullptr, 0);
    else
      _uart->sendFrame(CMD_DISABLE_MODULE, mod->getModuleId(), nullptr, 0);
  }

  auto sendPkt = [&](uint8_t mid, uint8_t pIndex, int32_t val) {
    pkt[0] = pIndex;
    pkt[1] = val & 0xFF;
    pkt[2] = (val >> 8) & 0xFF;
    pkt[3] = (val >> 16) & 0xFF;
    pkt[4] = (val >> 24) & 0xFF;
    _uart->sendFrame(CMD_SET_PARAM, mid, pkt, 5);
  };

  // Volume
  sendPkt(MODULE_ID_VOLUME, 0, _pipeline->getVolume()._gainDb);

  // NG
  sendPkt(MODULE_ID_NOISE_GATE, 0, _pipeline->getNoiseGate()._lowerThreshDb);
  sendPkt(MODULE_ID_NOISE_GATE, 1, _pipeline->getNoiseGate()._upperThreshDb);
  sendPkt(MODULE_ID_NOISE_GATE, 2, _pipeline->getNoiseGate()._attackMs);
  sendPkt(MODULE_ID_NOISE_GATE, 3, _pipeline->getNoiseGate()._releaseMs);
  sendPkt(MODULE_ID_NOISE_GATE, 4, _pipeline->getNoiseGate()._holdMs);

  // CP
  sendPkt(MODULE_ID_COMPANDER, 0, _pipeline->getCompander()._thresholdDbInt);
  sendPkt(MODULE_ID_COMPANDER, 1, _pipeline->getCompander()._ratioBelowQ88);
  sendPkt(MODULE_ID_COMPANDER, 2, _pipeline->getCompander()._ratioAboveQ88);
  sendPkt(MODULE_ID_COMPANDER, 3, _pipeline->getCompander()._attackMs);
  sendPkt(MODULE_ID_COMPANDER, 4, _pipeline->getCompander()._releaseMs);
  sendPkt(MODULE_ID_COMPANDER, 5, _pipeline->getCompander()._pregainQ412);

  // EX
  sendPkt(MODULE_ID_EXCITER, 0, _pipeline->getExciter()._fCut);
  sendPkt(MODULE_ID_EXCITER, 1, _pipeline->getExciter()._dry);
  sendPkt(MODULE_ID_EXCITER, 2, _pipeline->getExciter()._wet);

  // VB
  sendPkt(MODULE_ID_VIRTUAL_BASS, 0, _pipeline->getVirtualBass()._fCut);
  sendPkt(MODULE_ID_VIRTUAL_BASS, 1, _pipeline->getVirtualBass()._intensity);
  sendPkt(MODULE_ID_VIRTUAL_BASS, 2, _pipeline->getVirtualBass()._enhanced);

  // BC
  sendPkt(MODULE_ID_BASS_CLASSIC, 0, _pipeline->getBassClassic()._fCut);
  sendPkt(MODULE_ID_BASS_CLASSIC, 1, _pipeline->getBassClassic()._intensity);

  // SW
  sendPkt(MODULE_ID_STEREO_WIDEN, 0, _pipeline->getStereoWidener()._intensity);

  // DRC
  sendPkt(MODULE_ID_DRC, 0, _pipeline->getDrc()._bands[0].thresholdDbInt);
  sendPkt(MODULE_ID_DRC, 1, _pipeline->getDrc()._bands[0].ratioQ88);
  sendPkt(MODULE_ID_DRC, 2, _pipeline->getDrc()._bands[0].attackMs);
  sendPkt(MODULE_ID_DRC, 3, _pipeline->getDrc()._bands[0].releaseMs);
  sendPkt(MODULE_ID_DRC, 4, _pipeline->getDrc()._bands[0].pregainQ412);

  // SC
  sendPkt(MODULE_ID_SOFT_CLIP, 0, _pipeline->getSoftClipper()._thresholdDb);

  // EQ bands
  auto sendEq = [&](uint8_t cmdEq, uint8_t mid, ParametricEQ &eq) {
    int16_t pregain_db = eq.getPregain();
    for (uint8_t i = 0; i < MAX_EQ_BANDS; i++) {
      pkt[0] = pregain_db & 0xFF;
      pkt[1] = (pregain_db >> 8) & 0xFF;
      pkt[2] = i;
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
  sendEq(CMD_SET_EQ_BAND, MODULE_ID_EQ_DSP, _pipeline->getEqDsp());
  sendEq(CMD_SET_EQ_BAND, MODULE_ID_EQ_DSP_POST, _pipeline->getEqDspPost());

  // DynEQ
  uint8_t deqPkt[20];
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
  _uart->sendFrame(CMD_SET_DYNEQ_THRESH, MODULE_ID_DYNAMIC_EQ, deqPkt, 20);

  sendEq(CMD_SET_DYNEQ_LOW_BAND, MODULE_ID_DYNAMIC_EQ, deq._eqLow);
  sendEq(CMD_SET_DYNEQ_HIGH_BAND, MODULE_ID_DYNAMIC_EQ, deq._eqHigh);

  // Tell host we're done
  _uart->sendAck(MODULE_ID_SYSTEM, 0);
}
