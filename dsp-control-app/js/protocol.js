/**
 * @file protocol.js
 * @brief UART protocol — frame builder/parser matching ESP32 firmware exactly
 *
 * Frame: | 0xAA | 0x55 | CMD(1B) | MODULE(1B) | LEN_LO | LEN_HI | DATA(NB) | CRC8(1B) |
 * CRC8:  XOR of CMD + MODULE + LEN bytes + DATA bytes
 *
 * ── Unified value encoding (protocol v2) ────────────────────────────────
 * Every *measured* parameter (dB, ms, Hz, ratio, Q, %, ...) is transmitted
 * as a 4-byte IEEE-754 float32, little-endian — always the real-world
 * value (e.g. -20.0 for -20 dB, 4.0 for a 4:1 ratio, 5.0 for 5 ms).
 *
 * There is no more Q8.8 / Q4.12 / Q6.10 / ×100 / ×1000 / ×10 scaling table
 * to keep in sync between app.js and the firmware — both sides read/write
 * the exact same real number. The firmware converts to whatever internal
 * fixed-point representation a given DSP module needs *at the boundary*
 * (param_controller.cpp), so this file and the JS UI never have to know
 * about those internal formats.
 *
 * Only structural/selector bytes — module id, param id, band index,
 * enabled flag, filter type, preset index — stay plain uint8, since they
 * are indices/flags, not measurements, and don't benefit from a shared
 * numeric format.
 */

// ─── Constants (match firmware config.h / uart_protocol.h) ─────────────

export const SYNC1 = 0xAA;
export const SYNC2 = 0x55;

export const CMD = {
    SET_PARAM: 0x01,
    ENABLE_MODULE: 0x02,
    DISABLE_MODULE: 0x03,
    SET_EQ_BAND: 0x04,
    SET_DYNEQ_LOW_BAND: 0x05,
    SET_DYNEQ_HIGH_BAND: 0x06,
    SET_DYNEQ_THRESH: 0x07,
    SAVE_PRESET: 0x08,
    LOAD_PRESET: 0x09,
    GET_ALL_STATE: 0x0A,
    SET_ISF_PRESET: 0x0B,   // Set one ISF preset slot
    SET_ISF_BAND_PARAMS: 0x0C,
    GET_ISF_STATE: 0x0D,    // Request ISF state report
    SET_ISF_CONFIG: 0x0E,   // Set RMS/slew config
    WIFI_SCAN: 0x10,
    WIFI_SET_STA: 0x11,
    WIFI_SET_AP: 0x12,
    WIFI_GET_STATUS: 0x13,
    GET_REPORT_CPU_USAGE: 0x39, // Request CPU usage report
    SEND_REPORT_CPU_USAGE: 0x40,
    REPORT_ISF: 0x41,       // Push ISF state
    REPORT_ISF_CONFIG: 0x42, // Push ISF config
    REPORT_ISF_PRESET: 0x43, // Push ISF preset data
    REPORT_ISF_BAND_PER_PRESET: 0x44,
    REPORT_ENABLE_MASK: 0x4B,
    CURRENT_PRESET_INDEX: 0x4A,
    // Live meter reports — sent by firmware when UI polls GET_MODULE_METER
    REPORT_DYNBASS:   0x45, // energyDb(f32) + alpha(f32)
    REPORT_DYNEQ:     0x46, // energyDb(f32) + alphaLow(f32) + alphaHigh(f32)
    REPORT_COMPANDER: 0x47, // envLinear(f32, 0..1) + gainDb(f32)
    REPORT_DRC:       0x48, // gainDb(f32) × 4 bands
    GET_MODULE_METER: 0x49, // Request one meter report; data[0]=moduleId
    ACK_RESPONSE: 0xFE,
    ERROR: 0xFF
};

export const MODULE = {
    PRE_GAIN: 0x01,
    COMPANDER: 0x02,
    EXCITER: 0x03,
    DYNAMIC_BASS: 0x04,
    DYNAMIC_EQ: 0x05,
    EQ_DSP_1: 0x06,
    EQ_DSP_2: 0x07,
    DRC: 0x08,
    POST_GAIN: 0x09,
    LEFTRIGHT_EQ: 0x0A,
    ISF_1: 0x0B,   // Index Selectable Filter instance 1 (replaces AUTO_EQ)
    ISF_2: 0x0C,   // Index Selectable Filter instance 2
    PRE_EQ: 0x0D,  // Pre EQ (3-band tone control: Bass/Mid/Treble)
    SYSTEM: 0xF0
};

export const MODULE_NAMES = {
    [MODULE.PRE_GAIN]: 'Pre Gain',
    [MODULE.PRE_EQ]: 'Pre EQ',
    [MODULE.COMPANDER]: 'Compander',
    [MODULE.EXCITER]: 'Exciter',
    [MODULE.DYNAMIC_BASS]: 'Dynamic Bass',
    [MODULE.DYNAMIC_EQ]: 'Dynamic EQ',
    [MODULE.PRE_EQ]: 'Pre EQ (Tone)',
    [MODULE.EQ_DSP_1]: 'Parametric EQ 1',
    [MODULE.EQ_DSP_2]: 'Parametric EQ 2',
    [MODULE.LEFTRIGHT_EQ]: 'Left Right EQ',
    [MODULE.ISF_1]: 'Index Selectable Filter 1',
    [MODULE.ISF_2]: 'Index Selectable Filter 2',
    [MODULE.DRC]: 'Dynamic Range Compression',
    [MODULE.POST_GAIN]: 'Post Gain',
};

export const MODULE_ORDER = [
    MODULE.PRE_GAIN, MODULE.PRE_EQ, MODULE.COMPANDER, MODULE.EXCITER,
    MODULE.DYNAMIC_BASS, MODULE.DYNAMIC_EQ,
    MODULE.ISF_1, MODULE.ISF_2,
    MODULE.EQ_DSP_1, MODULE.EQ_DSP_2, MODULE.LEFTRIGHT_EQ,
    MODULE.DRC, MODULE.POST_GAIN
];

export const EQ_FILTER_TYPES = [
    'Peaking', 'Low Shelf', 'High Shelf', 'Low Pass',
    'High Pass', 'Band Pass', 'Notch'
];

// Sentinel for "ISF override disabled — use RMS detector".
// Must match IndexSelectableFilter::ISF_OVERRIDE_AUTO in isf.h exactly,
// since it's now sent through as a plain float value (no more int16
// 0x8000 special-case on the wire).
export const ISF_OVERRIDE_AUTO = -9999.0;

// ─── CRC8 (XOR) ───────────────────────────────────────────────────────

function calcCRC8(bytes) {
    let crc = 0;
    for (const b of bytes) crc ^= b;
    return crc;
}

// ─── Frame Builder ─────────────────────────────────────────────────────

export function buildFrame(cmd, moduleId, data = []) {
    const len = data.length;
    const lenLo = len & 0xFF;
    const lenHi = (len >> 8) & 0xFF;

    const frame = [SYNC1, SYNC2, cmd, moduleId, lenLo, lenHi, ...data];
    const crcBytes = [cmd, moduleId, lenLo, lenHi, ...data];
    frame.push(calcCRC8(crcBytes));

    return frame;
}

// ─── Float32 <-> bytes (the ONE numeric wire format) ───────────────────

const _f32buf  = new ArrayBuffer(4);
const _f32view = new DataView(_f32buf);

/** Real-world number → 4 bytes, IEEE-754 float32, little-endian. */
export function floatToLE(value) {
    _f32view.setFloat32(0, value, true);
    return [
        _f32view.getUint8(0), _f32view.getUint8(1),
        _f32view.getUint8(2), _f32view.getUint8(3)
    ];
}

/** 4 bytes, IEEE-754 float32 little-endian → real-world number. */
export function leToFloat(b, offset = 0) {
    _f32view.setUint8(0, b[offset]);
    _f32view.setUint8(1, b[offset + 1]);
    _f32view.setUint8(2, b[offset + 2]);
    _f32view.setUint8(3, b[offset + 3]);
    return _f32view.getFloat32(0, true);
}

// ─── Convenience Builders ──────────────────────────────────────────────

export function buildEnableModule(moduleId) { return buildFrame(CMD.ENABLE_MODULE, moduleId); }
export function buildDisableModule(moduleId) { return buildFrame(CMD.DISABLE_MODULE, moduleId); }
export function buildGetModuleStatus(moduleId) { return buildFrame(CMD.GET_MODULE_STATUS, moduleId); }

/**
 * Set one parameter. `value` is always the real-world number: a boolean
 * flag is 0/1, an enum (DRC mode, crossover type) is its plain index,
 * everything else (dB, ms, Hz, ratio, Q...) is the actual measurement.
 * No pre-scaling — ever — is required from the caller.
 */
export function buildSetParam(moduleId, paramId, value) {
    // Data: paramId(1B) + value(f32 LE, 4B) = 5 bytes
    const data = [paramId, ...floatToLE(value)];
    return buildFrame(CMD.SET_PARAM, moduleId, data);
}

export function buildSetEqBand(moduleId, band, pregainDb, enabled, type, freq, gainDb, q) {
    // Data: pregain(f32) + band(1B) + enabled(1B) + type(1B)
    //       + freq(f32) + gain(f32) + Q(f32) = 19 bytes
    const data = [
        ...floatToLE(pregainDb),
        band,
        enabled ? 1 : 0,
        type,
        ...floatToLE(freq),
        ...floatToLE(gainDb),
        ...floatToLE(q)
    ];
    return buildFrame(CMD.SET_EQ_BAND, moduleId, data);
}

export function buildSetDynEqBand(isHigh, band, pregainDb, enabled, type, freq, gainDb, q) {
    const cmd = isHigh ? CMD.SET_DYNEQ_HIGH_BAND : CMD.SET_DYNEQ_LOW_BAND;
    const data = [
        ...floatToLE(pregainDb),
        band, enabled ? 1 : 0, type,
        ...floatToLE(freq),
        ...floatToLE(gainDb),
        ...floatToLE(q)
    ];
    return buildFrame(cmd, MODULE.DYNAMIC_EQ, data);
}

export function buildSetDynEqThresholds(lowDb, normalDb, highDb, attackMs, releaseMs, lookaheadMs = 0) {
    // Data: low, normal, high, attack, release, lookahead — 6 × f32 = 24 bytes
    const data = [
        ...floatToLE(lowDb), ...floatToLE(normalDb), ...floatToLE(highDb),
        ...floatToLE(attackMs), ...floatToLE(releaseMs), ...floatToLE(lookaheadMs)
    ];
    return buildFrame(CMD.SET_DYNEQ_THRESH, MODULE.DYNAMIC_EQ, data);
}

export function buildSavePreset(slot) { return buildFrame(CMD.SAVE_PRESET, MODULE.SYSTEM, [slot]); }
export function buildLoadPreset(slot) { return buildFrame(CMD.LOAD_PRESET, MODULE.SYSTEM, [slot]); }
export function buildSetInputSource(src) { return buildFrame(CMD.SET_INPUT_SOURCE, MODULE.SYSTEM, [src]); }
export function buildSetOutputSource(src) { return buildFrame(CMD.SET_OUTPUT_SOURCE, MODULE.SYSTEM, [src]); }

export function buildGetAllState() {
    return buildFrame(CMD.GET_ALL_STATE, MODULE.SYSTEM);
}

/** Request a live meter snapshot for one dynamic module. */
export function buildGetModuleMeter(moduleId) {
    return buildFrame(CMD.GET_MODULE_METER, MODULE.SYSTEM, [moduleId]);
}

/**
 * Set lookahead time for Compander / DRC band / Dynamic Bass / Dynamic EQ.
 * `ms` is the real lookahead time in milliseconds (e.g. 5.0 = 5 ms).
 * Just a plain SET_PARAM now — kept as a named helper for call-site clarity.
 */
export function buildSetLookahead(moduleId, paramId, ms) {
    const clamped = Math.max(0, Math.min(10, ms)); // matches firmware's ~10ms max buffers
    return buildSetParam(moduleId, paramId, clamped);
}

// ─── ISF Builders ──────────────────────────────────────────────────────

/**
 * Set one ISF preset slot.
 * @param {number} moduleId  MODULE.ISF_1 or MODULE.ISF_2
 * @param {number} presetIdx 0..9
 * @param {object} preset    { thresholdDb, pregainDb }  — real dB values
 */
export function buildSetIsfPreset(moduleId, presetIdx, preset) {
    // Data: preset_idx(1B) + threshold(f32) + pregain(f32) = 9 bytes
    const data = [
        presetIdx & 0xFF,
        ...floatToLE(preset.thresholdDb || 0),
        ...floatToLE(preset.pregainDb || 0),
    ];
    return buildFrame(CMD.SET_ISF_PRESET, moduleId, data);
}

export function buildSetIsfBandParams(moduleId, presetIdx, bandIdx, presetObj) {
    if (!presetObj || !presetObj.bands || !presetObj.bands[bandIdx]) {
        return null;
    }

    const band = presetObj.bands[bandIdx];

    // Data: presetIdx(1B) + bandIdx(1B) + enabled(1B) + type(1B)
    //       + freq(f32) + gain(f32) + Q(f32) = 16 bytes
    const data = [
        presetIdx,
        bandIdx,
        band.enabled ? 1 : 0,
        band.type,
        ...floatToLE(band.freq),
        ...floatToLE(dbToQ88(band.gain)),
        ...floatToLE(qToQ610(band.q))
    ];

    return buildFrame(CMD.SET_ISF_BAND_PARAMS, moduleId, data);
}

/**
 * Set ISF config (RMS window, slew time, num presets, level override).
 * @param {number} moduleId   MODULE.ISF_1 or MODULE.ISF_2
 * @param {number} numPresets 1..10
 * @param {number} rmsMs      RMS window ms
 * @param {number} slewMs     Slew time per index step ms
 * @param {number|null} overrideDb  null = auto (use RMS), number = override level (dB)
 * @param {number} lookaheadMs
 */
export function buildSetIsfConfig(moduleId, numPresets, rmsMs, slewMs, overrideDb = null, lookaheadMs = 0) {
    const overrideVal = (overrideDb === null) ? ISF_OVERRIDE_AUTO : overrideDb;
    // Data: numPresets(1B) + rmsMs(f32) + slewMs(f32) + override(f32) + lookahead(f32) = 17 bytes
    const data = [
        numPresets & 0xFF,
        ...floatToLE(rmsMs),
        ...floatToLE(slewMs),
        ...floatToLE(overrideVal),
        ...floatToLE(lookaheadMs)
    ];
    return buildFrame(CMD.SET_ISF_CONFIG, moduleId, data);
}

/** Request ISF state for both instances. */
export function buildGetIsfState() {
    return buildFrame(CMD.GET_ISF_STATE, MODULE.ISF_1);
}

// ─── WiFi Builders ──────────────────────────────────────────────────────
// (Unaffected by the numeric-value unification — SSIDs/passwords are text,
//  IP/RSSI/enc flags are already plain single-purpose bytes.)

export function buildWifiScan() { return buildFrame(CMD.WIFI_SCAN, MODULE.SYSTEM); }

export function buildWifiSetSTA(ssid, pass, staticIpStr = "") {
    const enc = new TextEncoder();
    const ssidBytes = enc.encode(ssid);
    const passBytes = enc.encode(pass);
    const data = [
        Math.min(ssidBytes.length, 32), ...ssidBytes.slice(0, 32),
        Math.min(passBytes.length, 64), ...passBytes.slice(0, 64)
    ];
    if (staticIpStr) {
        const parts = staticIpStr.split('.').map(p => parseInt(p));
        if (parts.length === 4) {
            data.push(...parts);
        } else {
            data.push(0,0,0,0);
        }
    } else {
        data.push(0,0,0,0);
    }
    return buildFrame(CMD.WIFI_SET_STA, MODULE.SYSTEM, data);
}

export function buildWifiSetAP() { return buildFrame(CMD.WIFI_SET_AP, MODULE.SYSTEM); }
export function buildWifiGetStatus() { return buildFrame(CMD.WIFI_GET_STATUS, MODULE.SYSTEM); }

// ─── Legacy integer helpers ─────────────────────────────────────────────
// Still used for genuinely integer wire fields that aren't "measured
// values" (enable-mask bitfield, WiFi IP octets, RSSI, preset index...).
// NOT used anymore to encode DSP parameter values — see floatToLE/leToFloat.

export function leToInt32(b, offset = 0) {
    let val = b[offset] | (b[offset + 1] << 8) | (b[offset + 2] << 16) | (b[offset + 3] << 24);
    return val;
}

export function leToInt16(b, offset = 0) {
    let val = b[offset] | (b[offset + 1] << 8);
    if (val >= 0x8000) val -= 0x10000;
    return val;
}

// ─── Deprecated Q-format converters ─────────────────────────────────────
// Kept only for any legacy display/graph code that might still import
// them (e.g. curve-drawing helpers). They are NOT used anywhere in the
// wire protocol anymore — do not reintroduce them into builders/parsers.

/** @deprecated wire format is float32 now; kept for legacy callers only. */
export function dbToQ88(db) { return Math.round(db * 256); }
/** @deprecated */
export function dbToQ31(db) { return Math.round(Math.pow(10, db / 20) * 2147483647); }
/** @deprecated */
export function q88ToDb(q88) { return q88 / 256; }
/** @deprecated */
export function qToQ610(q) { return Math.round(q * 1024); }
/** @deprecated */
export function q610ToQ(q610) { return q610 / 1024; }

// ─── Response Parser ──────────────────────────────────────────────────

export class FrameParser {
    constructor() {
        this._state = 'SYNC1';
        this._cmd = 0;
        this._moduleId = 0;
        this._dataLen = 0;
        this._data = [];
        this._dataIdx = 0;
        this._calcCrc = 0;
        this._callback = null;
    }

    onFrame(cb) { this._callback = cb; }

    feed(bytes) {
        for (const byte of bytes) {
            this._processByte(byte);
        }
    }

    _processByte(b) {
        switch (this._state) {
            case 'SYNC1':
                if (b === SYNC1) this._state = 'SYNC2';
                break;
            case 'SYNC2':
                this._state = (b === SYNC2) ? 'CMD' : 'SYNC1';
                this._calcCrc = 0;
                break;
            case 'CMD':
                this._cmd = b; this._calcCrc ^= b; this._state = 'MODULE';
                break;
            case 'MODULE':
                this._moduleId = b; this._calcCrc ^= b; this._state = 'LEN_LO';
                break;
            case 'LEN_LO':
                this._dataLen = b; this._calcCrc ^= b; this._state = 'LEN_HI';
                break;
            case 'LEN_HI':
                this._dataLen |= (b << 8); this._calcCrc ^= b;
                if (this._dataLen > 256) { this._state = 'SYNC1'; break; }
                this._data = []; this._dataIdx = 0;
                this._state = this._dataLen > 0 ? 'DATA' : 'CRC';
                break;
            case 'DATA':
                this._data.push(b); this._calcCrc ^= b;
                if (++this._dataIdx >= this._dataLen) this._state = 'CRC';
                break;
            case 'CRC':
                if (b === this._calcCrc && this._callback) {
                    this._callback({
                        cmd: this._cmd,
                        moduleId: this._moduleId,
                        data: this._data
                    });
                }
                this._state = 'SYNC1';
                break;
        }
    }
}