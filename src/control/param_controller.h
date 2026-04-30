/**
 * @file param_controller.h
 * @brief Parameter dispatcher — routes UART commands to DSP modules
 */

#ifndef PARAM_CONTROLLER_H
#define PARAM_CONTROLLER_H

#include "../audio/audio_input.h"
#include "../audio/audio_output.h"
#include "../dsp/dsp_pipeline.h"
#include "preset_manager.h"
#include "uart_protocol.h"
#include "wifi_manager.h"

class ParamController {
public:
  void init(DspPipeline *pipeline, AudioInput *input, AudioOutput *output,
            UartProtocol *uart, PresetManager *presetMgr,
            WiFiManager *wifiMgr = nullptr);

  /**
   * Process a received command and dispatch to appropriate module.
   */
  void handleCommand(const UartCommand &cmd);

private:
  DspPipeline *_pipeline;
  AudioInput *_input;
  AudioOutput *_output;
  UartProtocol *_uart;
  PresetManager *_presetMgr;
  WiFiManager *_wifiMgr = nullptr;

  void handleEnableDisable(const UartCommand &cmd, bool enable);
  void handleSetParam(const UartCommand &cmd);
  void handleSetEqBand(const UartCommand &cmd);
  void handleSetDynEqBand(const UartCommand &cmd, bool isHigh);
  void handleSetDynEqThresholds(const UartCommand &cmd);
  void handleIsAlive(const UartCommand &cmd);
  void handleGetAllState(const UartCommand &cmd);
  void handleWifiScan(const UartCommand &cmd);
  void handleWifiSetSTA(const UartCommand &cmd);
  void handleWifiSetAP(const UartCommand &cmd);
  void handleWifiGetStatus(const UartCommand &cmd);

  // Utility: extract int32 from data buffer
  static int32_t extractInt32(const uint8_t *data);
  static int16_t extractInt16(const uint8_t *data);
};

#endif // PARAM_CONTROLLER_H
