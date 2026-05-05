/**
 * @file param_controller.cpp
 * @brief Parameter dispatcher implementation
 */

#include "param_controller.h"
#include "../utils/debug_log.h"

#define TAG "CTRL"

void ParamController::init(DspPipeline *pipeline, AudioInput *input,
                           AudioOutput *output, UartProtocol *uart,
                           PresetManager *presetMgr, WiFiManager *wifiMgr) {
  _pipeline  = pipeline;
  _input     = input;
  _output    = output;
  _uart      = uart;
  _presetMgr = presetMgr;
  _wifiMgr   = wifiMgr;
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

  case CMD_SAVE_PRESET:
    if (cmd.dataLen > 0)
      _presetMgr->savePreset(cmd.data[0], *_pipeline);
    _uart->sendAck(MODULE_ID_SYSTEM, 0);
    break;

  case CMD_LOAD_PRESET:
    if (cmd.dataLen > 0) {
      _presetMgr->loadPreset(cmd.data[0], *_pipeline);
      // Automatically send all state back to app so UI updates
      handleGetAllState(cmd);
    } else {
      _uart->sendAck(MODULE_ID_SYSTEM, 0);
    }
    break;

  case CMD_GET_ALL_STATE:
    handleGetAllState(cmd);
    break;

  // ── WiFi configuration commands ─────────────────────────────────────────
  case CMD_WIFI_SCAN:
    handleWifiScan(cmd);
    break;

  case CMD_WIFI_SET_STA:
    handleWifiSetSTA(cmd);
    break;

  case CMD_WIFI_SET_AP:
    handleWifiSetAP(cmd);
    break;

  case CMD_WIFI_GET_STATUS:
    handleWifiGetStatus(cmd);
    break;

  default:
    LOG_ERROR(TAG, "Unknown command ID: 0x%02X from module: 0x%02X", cmd.cmd, cmd.moduleId);
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

  case MODULE_ID_DYNAMIC_BASS: {
    DynamicBass &db = _pipeline->getDynamicBass();
    switch (paramId) {
    case 0:
      db.setCutoffFreq(value);
      break;
    case 1:
      db.setGainBoost(value);
      break;
    case 2:
      db.setEnhanced(value);
      break;
    case 3:
      db.setBoostFullThreshold(value);
      break;
    case 4:
      db.setNeutralThreshold(value);
      break;
    case 5:
      db.setClipFullThreshold(value);
      break;
    case 6:
      db.setClipAttack(value);
      break;
    case 7:
      db.setClipRelease(value);
      break;
    default:
      _uart->sendError(0x04);
      return;
    }
    break;
  }

  case MODULE_ID_PRE_GAIN: {
    VolumeControl &vol = _pipeline->getPreGain();
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

  case MODULE_ID_DRC: {
    DRC &drc = _pipeline->getDrc();

    // ── Global params (paramId 0x10 - 0x1F) ────────────────────────
    if (paramId == 0x10) { drc.setMode((DRCMode)value);             break; }
    if (paramId == 0x11) { drc.setCrossoverType((DRCCrossoverType)value); break; }
    if (paramId == 0x12) { drc.setCrossoverFreq(0, value);          break; }
    if (paramId == 0x13) { drc.setCrossoverFreq(1, value);          break; }
    if (paramId == 0x14) { drc.setCrossoverQ(0, value);             break; }
    if (paramId == 0x15) { drc.setCrossoverQ(1, value);             break; }

    // ── Per-band params (paramId 0x20 - 0x3F) ──────────────────────
    // Encoding: paramId = 0x20 + band*8 + param
    //   band 0 → 0x20-0x27, band 1 → 0x28-0x2F
    //   band 2 → 0x30-0x37, band 3 (fullband) → 0x38-0x3F
    // param: 0=threshold, 1=ratio, 2=attack, 3=release, 4=pregain
    if (paramId >= 0x20 && paramId <= 0x3F) {
      uint8_t band  = (paramId - 0x20) >> 3;  // 0-3
      uint8_t param = (paramId - 0x20) & 0x07; // 0-4
      switch (param) {
        case 0: drc.setThreshold(band, value);   break;
        case 1: drc.setRatio(band, value);        break;
        case 2: drc.setAttackTime(band, value);   break;
        case 3: drc.setReleaseTime(band, value);  break;
        case 4: drc.setPregain(band, value);      break;
        default: _uart->sendError(0x04); return;
      }
      break;
    }

    _uart->sendError(0x04);
    return;
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
  uint8_t realBand = band;
  if (cmd.moduleId == MODULE_ID_EQ_DSP_1) {
    eq = &_pipeline->getEqDsp_1();
    eq->setPregain(pregain_db);
  } else if (cmd.moduleId == MODULE_ID_EQ_DSP_2) {
    eq = &_pipeline->getEqDsp_2();
    eq->setPregain(pregain_db); // q8.8 format;
  } else if (cmd.moduleId == MODULE_ID_LEFTRIGHT_EQ) {
    if (band & 0x80) {
      eq = &_pipeline->getLeftRightEq().getEqRight();
      realBand = band & 0x7F;
    } else {
      eq = &_pipeline->getLeftRightEq().getEqLeft();
    }
    eq->setPregain(pregain_db);
  }

  if (eq && realBand < MAX_EQ_BANDS) {
    eq->setBand(realBand, params); // q8.8 format
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

//void ParamController::handleIsAlive(const UartCommand &cmd) {
  // Dummy handler for heartbeat
//  _uart->sendAck(MODULE_ID_SYSTEM, 0);
//}

int32_t ParamController::extractInt32(const uint8_t *data) {
  return (int32_t)data[0] | ((int32_t)data[1] << 8) | ((int32_t)data[2] << 16) |
         ((int32_t)data[3] << 24);
}

int16_t ParamController::extractInt16(const uint8_t *data) {
  return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

void ParamController::handleGetAllState(const UartCommand &cmd) {
  _uart->startBatch();
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
  sendPkt(MODULE_ID_PRE_GAIN, 0, _pipeline->getPreGain()._gainDb);

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

  // DB
  sendPkt(MODULE_ID_DYNAMIC_BASS, 0, _pipeline->getDynamicBass().getCutoffFreq());
  sendPkt(MODULE_ID_DYNAMIC_BASS, 1, _pipeline->getDynamicBass().getGainBoost());
  sendPkt(MODULE_ID_DYNAMIC_BASS, 2, _pipeline->getDynamicBass().getEnhanced());
  sendPkt(MODULE_ID_DYNAMIC_BASS, 3, _pipeline->getDynamicBass().getBoostFullThresh());
  sendPkt(MODULE_ID_DYNAMIC_BASS, 4, _pipeline->getDynamicBass().getNeutralThresh());
  sendPkt(MODULE_ID_DYNAMIC_BASS, 5, _pipeline->getDynamicBass().getClipFullThresh());
  sendPkt(MODULE_ID_DYNAMIC_BASS, 6, _pipeline->getDynamicBass().getClipAttack());
  sendPkt(MODULE_ID_DYNAMIC_BASS, 7, _pipeline->getDynamicBass().getClipRelease());

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
  sendEq(CMD_SET_EQ_BAND, MODULE_ID_EQ_DSP_1, _pipeline->getEqDsp_1());
  sendEq(CMD_SET_EQ_BAND, MODULE_ID_EQ_DSP_2, _pipeline->getEqDsp_2());
  sendEq(CMD_SET_EQ_BAND, MODULE_ID_LEFTRIGHT_EQ, _pipeline->getLeftRightEq().getEqLeft(), false);
  sendEq(CMD_SET_EQ_BAND, MODULE_ID_LEFTRIGHT_EQ, _pipeline->getLeftRightEq().getEqRight(), true);

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
  _uart->endBatch();
}

// ── WiFi handlers ─────────────────────────────────────────────────────────────

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

