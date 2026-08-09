/**
 * @file param_controller.h
 * @brief Parameter dispatcher — routes UART commands to DSP modules
 */

#ifndef PARAM_CONTROLLER_H
#define PARAM_CONTROLLER_H

#include "../../audio/audio_io.h"
#include "../../effects/dsp_pipeline.h"
#include "preset_manager.h"
#include "../uart_protocol.h"
#include "../wifi/wifi_manager.h"
#include "../display/battery_monitor.h"

extern volatile uint16_t s_cpu_usage;
extern volatile uint8_t  s_heapPct;
extern volatile uint32_t s_fs;

class ParamController {
public:
    void init(DspPipeline* pipeline,
              UartProtocol* uart, PresetManager* presetMgr,
              WiFiManager* wifiMgr = nullptr);

    /** Wire up the WiFi manager instance so CMD_WIFI_* commands can be handled. */
    void setWifiManager(WiFiManager* wifiMgr);

    /** Wire up the battery monitor instance so CMD_GET_BATTERY_STATUS can report on it. */
    void setBatteryMonitor(BatteryMonitor* batteryMgr);

    /** Call from the control task loop to poll for UART commands. */
    void handleCommand(const UartCommand& cmd);

    void pollWifiStatus();

private:
    DspPipeline*   _pipeline;
    UartProtocol*  _uart;
    PresetManager* _presetMgr;
    WiFiManager*   _wifiMgr = nullptr;
    BatteryMonitor* _batteryMgr = nullptr;

    // ── Existing handlers ─────────────────────────────────────────────────────
    void handleEnableDisable(const UartCommand& cmd, bool enable);
    void handleSetParam(const UartCommand& cmd);
    void handleSetEqBand(const UartCommand& cmd);
    void handleSetDynEqThresholds(const UartCommand& cmd);
    void handleGetAllState(const UartCommand& cmd);
    void handleWifiScan(const UartCommand& cmd);
    void handleWifiSetSTA(const UartCommand& cmd);
    void handleWifiSetAP(const UartCommand& cmd);
    void handleWifiGetStatus(const UartCommand& cmd);

    // ── WiFi config view/edit (view SSID+pass, change root AP, forget STA) ────
    void handleWifiGetConfig(const UartCommand& cmd);
    void handleWifiSetApConfig(const UartCommand& cmd);
    void handleWifiClearSta(const UartCommand& cmd);

    // ── Battery ────────────────────────────────────────────────────────────────
    void handleGetBatteryStatus(const UartCommand& cmd);

    // ── ISF handlers ──────────────────────────────────────────────────────────

    /**
     * CMD_SET_ISF_PRESET — write one preset slot to an ISF instance.
     *
     * Data layout:
     *   [0]     preset_index (uint8)   0..ISF_MAX_PRESETS-1
     *   [1..2]  threshold_q88 (int16)  RMS threshold dB in Q8.8
     *   [3..4]  pregain_q88   (int16)  pregain dB in Q8.8
     *   [5]     num_bands     (uint8)  0..ISF_MAX_BANDS
     *   [6+N*8] bands:
     *     [0]   enabled (uint8, 0/1)
     *     [1]   type    (uint8, EQFilterType)
     *     [2..3] freq   (uint16 LE, Hz)
     *     [4..5] gain   (int16 LE, Q8.8 dB)
     *     [6..7] Q      (uint16 LE, Q6.10)
     *
     * moduleId selects instance: MODULE_ID_ISF_1 or MODULE_ID_ISF_2
     */
    void handleSetIsfCommonPresetParams(const UartCommand& cmd);
    void handleSetIsfBandParams(const UartCommand& cmd);

    /**
     * CMD_SET_ISF_CONFIG — configure RMS window, slew rate, num presets.
     *
     * Data layout:
     *   [0]     num_presets  (uint8)   1..ISF_MAX_PRESETS
     *   [1..2]  rms_ms       (int16)   RMS window in ms
     *   [3..4]  slew_ms      (int16)   slew time per index step in ms
     *   [5..6]  override_q88 (int16)   level override dB in Q8.8;
     *                                  -32768 = auto (use RMS)
     */
    void handleSetIsfConfig(const UartCommand& cmd);

    /**
     * CMD_GET_ISF_STATE — firmware responds with REPORT_ISF for both instances.
     */
    void handleGetIsfState(const UartCommand& cmd);

    /**
     * Send REPORT_ISF frame for one ISF instance.
     *
     * Frame data:
     *   [0]     instance     (uint8)   0=ISF1, 1=ISF2
     *   [1..2]  level_q88    (int16)   current level dB Q8.8
     *   [3..4]  slew_q88     (int16)   fractional index * 256
     *   [5]     active_a     (uint8)   floor(slewIndex)
     *   [6]     active_b     (uint8)   ceil(slewIndex)
     *   [7]     num_presets  (uint8)
     */
    void sendIsfState(uint8_t instanceIdx);

    // ── Utilities ─────────────────────────────────────────────────────────────
    static int32_t extractInt32(const uint8_t* data);
    static int16_t extractInt16(const uint8_t* data);
    static uint16_t extractUint16(const uint8_t* data);
    
    /** Extract IEEE-754 float32 from 4 bytes, little-endian. */
    static float extractFloat(const uint8_t* data);
    
    /** Pack IEEE-754 float32 into 4 bytes, little-endian. */
    static void packFloat(uint8_t* dest, float value);

    /** Resolve ISF instance from module ID. Returns nullptr if not ISF. */
    IndexSelectableFilter* resolveIsf(uint8_t moduleId);
};

#endif // PARAM_CONTROLLER_H