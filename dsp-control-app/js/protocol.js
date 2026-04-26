/**
 * @file protocol.js
 * @brief UART protocol — frame builder/parser matching ESP32 firmware exactly
 *
 * Frame: | 0xAA | 0x55 | CMD(1B) | MODULE(1B) | LEN_LO | LEN_HI | DATA(NB) | CRC8(1B) |
 * CRC8:  XOR of CMD + MODULE + LEN bytes + DATA bytes
 */

// ─── Constants (match firmware config.h / uart_protocol.h) ─────────────

export const SYNC1 = 0xAA;
export const SYNC2 = 0x55;

export const CMD = {
    SET_PARAM: 0x01,
    GET_PARAM: 0x02,
    ENABLE_MODULE: 0x03,
    DISABLE_MODULE: 0x04,
    GET_MODULE_STATUS: 0x05,
    SET_EQ_BAND: 0x10,
    GET_EQ_BAND: 0x11,
    SET_DYNEQ_LOW_BAND: 0x12,
    SET_DYNEQ_HIGH_BAND: 0x13,
    SET_DYNEQ_THRESH: 0x14,
    SAVE_PRESET: 0x20,
    LOAD_PRESET: 0x21,
    SET_INPUT_SOURCE: 0x30,
    SET_OUTPUT_SOURCE: 0x31,
    SET_SYSTEM_INFO: 0x32, // Note: keeping numeric IDs consistent with ESP32
    GET_ALL_STATE: 0x33,
    REPORT_CPU_USAGE: 0x40,
    ACK_RESPONSE: 0xFE,
    ERROR: 0xFF
};

export const MODULE = {
    COMPANDER: 0x02,
    EXCITER: 0x03,
    DYNAMIC_BASS: 0x04,
    DYNAMIC_EQ: 0x05,
    EQ_DSP_1: 0x06,
    EQ_DSP_2: 0x07,
    DRC: 0x08,
    VOLUME: 0x09,
    SYSTEM: 0xF0
};

export const MODULE_NAMES = {
    [MODULE.COMPANDER]: 'Compander',
    [MODULE.EXCITER]: 'Exciter',
    [MODULE.DYNAMIC_BASS]: 'Dynamic Bass',
    [MODULE.DYNAMIC_EQ]: 'Dynamic EQ',
    [MODULE.EQ_DSP_1]: 'Parmetric EQ 1',
    [MODULE.EQ_DSP_2]: 'Parmetric EQ 2',
    [MODULE.DRC]: 'Dynamic Range Compression',
    [MODULE.VOLUME]: 'Volume',
};

export const MODULE_ORDER = [
    MODULE.NOISE_GATE, MODULE.COMPANDER, MODULE.EXCITER,
    MODULE.DYNAMIC_BASS, MODULE.DYNAMIC_EQ, MODULE.EQ_DSP_1, MODULE.EQ_DSP_2,
    MODULE.DRC, MODULE.VOLUME, MODULE.SOFT_CLIP
];

export const EQ_FILTER_TYPES = [
    'Peaking', 'Low Shelf', 'High Shelf', 'Low Pass',
    'High Pass', 'Band Pass', 'Notch'
];

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

// ─── Convenience Builders ──────────────────────────────────────────────

export function buildEnableModule(moduleId) { return buildFrame(CMD.ENABLE_MODULE, moduleId); }
export function buildDisableModule(moduleId) { return buildFrame(CMD.DISABLE_MODULE, moduleId); }
export function buildGetModuleStatus(moduleId) { return buildFrame(CMD.GET_MODULE_STATUS, moduleId); }
export function buildGetSystemInfo() { return buildFrame(CMD.GET_SYSTEM_INFO, MODULE.SYSTEM); }

export function buildSetParam(moduleId, paramId, value) {
    // Data: paramId(1B) + value(4B LE)
    const data = [
        paramId,
        value & 0xFF,
        (value >> 8) & 0xFF,
        (value >> 16) & 0xFF,
        (value >> 24) & 0xFF
    ];
    return buildFrame(CMD.SET_PARAM, moduleId, data);
}

export function buildSetEqBand(moduleId, band, pregainDb, enabled, type, freq, gainQ88, qQ610) {
    // Data: pregain(2B LE) + band(1B) + enabled(1B) + type(1B) + freq(2B LE) + gain(2B LE) + Q(2B LE) = 11 bytes
    const pg = dbToQ88(pregainDb);
    const data = [
        pg & 0xFF, (pg >> 8) & 0xFF,
        band,
        enabled ? 1 : 0,
        type,
        freq & 0xFF, (freq >> 8) & 0xFF,
        gainQ88 & 0xFF, (gainQ88 >> 8) & 0xFF,
        qQ610 & 0xFF, (qQ610 >> 8) & 0xFF
    ];
    return buildFrame(CMD.SET_EQ_BAND, moduleId, data);
}

export function buildSetDynEqBand(isHigh, band, pregainDb, enabled, type, freq, gainQ88, qQ610) {
    const cmd = isHigh ? CMD.SET_DYNEQ_HIGH_BAND : CMD.SET_DYNEQ_LOW_BAND;
    const pg = dbToQ88(pregainDb);
    const data = [
        pg & 0xFF, (pg >> 8) & 0xFF,
        band, enabled ? 1 : 0, type,
        freq & 0xFF, (freq >> 8) & 0xFF,
        gainQ88 & 0xFF, (gainQ88 >> 8) & 0xFF,
        qQ610 & 0xFF, (qQ610 >> 8) & 0xFF
    ];
    return buildFrame(cmd, MODULE.DYNAMIC_EQ, data);
}

export function buildSetDynEqThresholds(low, normal, high, attackMs, releaseMs) {
    const data = [
        ...int32ToLE(low), ...int32ToLE(normal), ...int32ToLE(high),
        ...int32ToLE(attackMs), ...int32ToLE(releaseMs)
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

// ─── Data conversion helpers ───────────────────────────────────────────

function int32ToLE(value) {
    const v = value | 0; // Ensure integer
    return [v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF];
}

export function leToInt32(b, offset = 0) {
    let val = b[offset] | (b[offset + 1] << 8) | (b[offset + 2] << 16) | (b[offset + 3] << 24);
    return val;
}

export function leToInt16(b, offset = 0) {
    let val = b[offset] | (b[offset + 1] << 8);
    if (val >= 0x8000) val -= 0x10000;
    return val;
}

// ─── EQ format converters ──────────────────────────────────────────────

/** dB float to Q8.8 int16 (e.g., -3.0 → -768) */
export function dbToQ88(db) { return Math.round(db * 256); }

/** dB float to Q31 int32 */
export function dbToQ31(db) {
    return Math.round(Math.pow(10, db / 20) * 2147483647);
}
/** Q8.8 int16 to dB float */
export function q88ToDb(q88) { return q88 / 256; }
/** Q factor float to Q6.10 int16 (e.g., 0.707 → 724) */
export function qToQ610(q) { return Math.round(q * 1024); }
/** Q6.10 to Q factor float */
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
                if (this._dataLen > 64) { this._state = 'SYNC1'; break; }
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