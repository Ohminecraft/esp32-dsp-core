/**
 * @file store.js
 * @brief Centralized state management with event-driven updates
 *
 * All module parameters live here. When a parameter changes:
 * 1. Store updates immediately (UI redraws)
 * 2. UART command is sent in parallel (with debounce)
 * 3. ACK confirms the change
 */

import { MODULE, MODULE_ORDER } from './protocol.js';

class EventEmitter {
    constructor() { this._listeners = {}; }
    on(event, fn) { (this._listeners[event] ||= []).push(fn); }
    off(event, fn) { this._listeners[event] = (this._listeners[event] || []).filter(f => f !== fn); }
    emit(event, ...args) { (this._listeners[event] || []).forEach(fn => fn(...args)); }
}

class Store extends EventEmitter {
    constructor() {
        super();

        // Module enable states
        this.modules = {};
        MODULE_ORDER.forEach(id => {
            this.modules[id] = { enabled: false };
        });

        // ── EQ helper: create 10 pre-allocated disabled slots ──────────
        const makeEqState = () => ({
            pregain: 0,
            bands: Array.from({ length: 10 }, () => ({
                enabled: false, type: 0, freq: 1000, gain: 0, q: 0.707
            }))
        });

        // EQ1 / EQ2 — 10 fixed slots each
        this.eq1 = makeEqState();
        this.eq2 = makeEqState();

        this.leftRightEq = {
            eqLeft: makeEqState(),
            eqRight: makeEqState()
        };

        // Pre EQ (3-band tone control: Bass/Mid/Treble)
        this.preEq = {
            enabled: true,
            pregainDb: 0,
            bands: [
                { enabled: true, type: 1, freq: 100, gain: 0, q: 0.707 },  // Bass - Low Shelf
                { enabled: true, type: 0, freq: 1000, gain: 0, q: 0.707 }, // Mid - Peaking
                { enabled: true, type: 2, freq: 10000, gain: 0, q: 0.707 } // Treble - High Shelf
            ]
        };

        // Dynamic EQ
        this.dynamicEq = {
            lowThresh: -4000, normalThresh: -2000, highThresh: -600,
            attackMs: 50, releaseMs: 200, lookaheadMs: 0,
            eqLow: makeEqState(),
            eqHigh: makeEqState()
        };

        // Compander
        this.compander = {
            threshold: -2000, ratioBelow: 100, ratioAbove: 400,
            attackMs: 10, releaseMs: 100, pregain: 4096,
            lookaheadMs: 0  // 0 = disabled; unit: ms × 10 (e.g. 50 = 5.0ms)
        };

        // Exciter
        this.exciter = { cutoffFreq: 3000, dry: 100, wet: 30 };

        // Dynamic Bass
        this.dynamicBass = { cutoffFreq: 80, gainBoost: 600, enhanced: 0, boostthreshold: -2400, neutralthreshold: -1600, clipthreshold: -800, clipattack: 600, cliprelease: 200, lookaheadMs: 0 };

        // ── ISF state ──────────────────────────────────────────────────
        // Helper: create one ISF instance state
        const makeIsfBand = () => ({
            enabled: false, type: 0, freq: 1000, gain: 0, q: 0.707
        });
        const makeIsfPreset = (thresholdDb = -96) => ({
            thresholdDb,
            pregainDb: 0,
            bands: Array.from({ length: 10 }, makeIsfBand),
            numBands: 0
        });
        const makeIsfInstance = () => ({
            numPresets: 1,
            rmsMs: 300,
            slewMs: 500,
            lookaheadMs: 0,
            overrideDb: null,           // null = auto RMS
            presets: Array.from({ length: 10 }, (_, i) => makeIsfPreset(-96 + i * 5)),
            // Runtime state from firmware (read-only)
            currentLevelDb: -96,
            slewIndex: 0,               // fractional
            activeA: 0,
            activeB: 0
        });
        this.isf1 = makeIsfInstance();
        this.isf2 = makeIsfInstance();

        // Active ISF tab: 'isf1' or 'isf2'
        this._activeIsfInstance = 'isf1';
        // Active preset index within the active ISF instance
        this._activeIsfPreset = 0;

        // DRC — multi-band with crossover
        // Band mapping: band[0]=Fullband, band[1]=Low, band[2]=Mid, band[3]=High
        this.drc = {
            mode: 0,           // 0=Fullband, 1=2Band, 2=3Band
            cfType: 0,         // 0=Butterworth1, 1=LR2, 2=LR4, 3=Q-Ctrl (matches dsp_types.h enum)
            fc1: 300,          // crossover freq 1 (Hz)
            fc2: 2000,         // crossover freq 2 (Hz)
            qLp: 717,          // Q LP (Q6.10: 717 ≈ 0.70) - only used when cfType === 3 (Q-Ctrl)
            qHp: 717,          // Q HP (Q6.10) - only used when cfType === 3 (Q-Ctrl)
            activeBand: 0,     // which band is shown in UI (0 = fullband default)
            bands: [
                { threshold: -1500, ratio: 400, attackMs: 5,  releaseMs: 50,  pregain: 4096, lookaheadMs: 0 }, // band[0] = Fullband
                { threshold: -1500, ratio: 400, attackMs: 5,  releaseMs: 50,  pregain: 4096, lookaheadMs: 0 }, // band[1] = Low
                { threshold: -1500, ratio: 400, attackMs: 5,  releaseMs: 50,  pregain: 4096, lookaheadMs: 0 }, // band[2] = Mid
                { threshold: -1500, ratio: 400, attackMs: 5,  releaseMs: 160, pregain: 4096, lookaheadMs: 0 }  // band[3] = High
            ]
        };

        // PreGain
        this.preGain = { gainDb: 0, mute: false, mono: false };

        // PostGain
        this.postGain = { gainDb: 0, mute: false, mono: false };


        // System
        this.system = {
            connected: false,
            portPath: '',
            activePreset: 0,
            transport: 'serial' // 'serial' | 'websocket'
        };

        // WiFi
        this.wifi = {
            mode: 'unknown',
            ssid: '',
            ip: '',
            rssi: 0,
            scanResults: []
        };

        // Selected module (for right panel)
        this.selectedModule = MODULE.EQ_DSP_1;

        // Active EQ target for graph: 'eq1', 'eq2', 'dynLow', 'dynHigh'
        this.activeEq = 'eq1';

        // Graph mode: 'eq' or 'dynamicEq'
        this.graphMode = 'eq';
    }

    // ─── Module Enable/Disable ───────────────────────────────────

    setModuleEnabled(moduleId, enabled) {
        if (!this.modules[moduleId]) {
            // Auto-register unknown module IDs received from firmware
            this.modules[moduleId] = { enabled: false };
        }
        this.modules[moduleId].enabled = enabled;
        this.emit('module:enabled', moduleId, enabled);
    }

    // ─── EQ ──────────────────────────────────────────────────────

    getActiveEqState() {
        switch (this.activeEq) {
            case 'eq1': return this.eq1;
            case 'eq2': return this.eq2;
            case 'dynLow': return this.dynamicEq.eqLow;
            case 'dynHigh': return this.dynamicEq.eqHigh;
            case 'eqLeft': return this.leftRightEq.eqLeft;
            case 'eqRight': return this.leftRightEq.eqRight;
            case 'preEq': return this.preEq;
            default: return this.eq1;
        }
    }

    getActiveEqModuleId() {
        switch (this.activeEq) {
            case 'eq1': return MODULE.EQ_DSP_1;
            case 'eq2': return MODULE.EQ_DSP_2;
            case 'dynLow': return MODULE.DYNAMIC_EQ;
            case 'dynHigh': return MODULE.DYNAMIC_EQ;
            case 'eqLeft': return MODULE.LEFTRIGHT_EQ;
            case 'eqRight': return MODULE.LEFTRIGHT_EQ;
            case 'preEq': return MODULE.PRE_EQ;
            default: return MODULE.EQ_DSP_1;
        }
    }

    // ─── ISF ──────────────────────────────────────────────────────

    getActiveIsf() {
        return this._activeIsfInstance === 'isf1' ? this.isf1 : this.isf2;
    }

    getIsfInstance(which) {
        return which === 'isf1' ? this.isf1 : this.isf2;
    }

    getIsfModuleId(which) {
        return (which === 'isf1') ? MODULE.ISF_1 : MODULE.ISF_2;
    }

    setActiveIsfInstance(which) {
        this._activeIsfInstance = which;
        this.emit('isf:instance-changed', which);
    }

    setActiveIsfPreset(idx) {
        this._activeIsfPreset = idx;
        this.emit('isf:preset-changed', idx);
    }

    getActiveIsfPreset() {
        return this._activeIsfPreset;
    }

    updateIsfConfig(which, rmsMs, slewMs, lookaheadMs) {
        const isf = this.getIsfInstance(which);
        isf.rmsMs = rmsMs;
        isf.slewMs = slewMs;
        isf.lookaheadMs = lookaheadMs;
    }

    /** Update ISF runtime state from firmware REPORT_ISF frame. */
    updateIsfState(which, levelDb, slewIndex, activeA, activeB) {
        const isf = this.getIsfInstance(which);
        isf.currentLevelDb = levelDb;
        isf.slewIndex      = slewIndex;
        isf.activeA        = activeA;
        isf.activeB        = activeB;
        this.emit('isf:state-updated', which);
    }

    /** Update one ISF preset from firmware REPORT_ISF_PRESET. */
    updateIsfPreset(which, presetIdx, preset) {
        const isf = this.getIsfInstance(which);
        if (presetIdx < 0 || presetIdx >= 5) return;
        const target = isf.presets[presetIdx];
        if (preset.thresholdDb !== undefined) target.thresholdDb = preset.thresholdDb;
        if (preset.pregainDb   !== undefined) target.pregainDb   = preset.pregainDb;

        if (preset.bands && Array.isArray(preset.bands)) {
            target.bands.forEach(b => { b.enabled = false; });
            preset.bands.forEach((fb, i) => {
                if (i >= 5) return;
                Object.assign(target.bands[i], fb);
                target.bands[i].enabled = fb.enabled !== false;
            });
            target.numBands = target.bands.filter(b => b.enabled).length;
        }
        if (presetIdx >= isf.numPresets) isf.numPresets = presetIdx + 1;
        this.emit('isf:preset-data-updated', which, presetIdx);
    }

    updateIsfEqBand(which, bandIdx, freqIn, gainIn) {
        const isf = this.getIsfInstance(which);
        const pIdx = this.getActiveIsfPreset ? this.getActiveIsfPreset() : 0;
        const preset = isf.presets[pIdx];
        Object.assign(preset.bands[bandIdx], { freq: freqIn, gain: gainIn });
        this.emit('isf:band-dragging', store.graphMode, pIdx, bandIdx);
    }

    setActiveEq(which) {
        this.activeEq = which;
        this.graphMode = (which === 'dynLow' || which === 'dynHigh') ? 'dynamicEq' : 'eq';
        this.emit('eq:active-changed');
    }

    addEqBand(freq = 1000, gain = 0, q = 1.0, type = 0) {
        const eq = this.getActiveEqState();
        const slot = eq.bands.findIndex(b => !b.enabled);
        if (slot === -1) return null;  // All 10 slots in use
        Object.assign(eq.bands[slot], { enabled: true, freq, gain, q, type });
        this.emit('eq:changed');
        this.emit('eq:structure-changed');
        return slot;
    }

    updateEqBand(index, changes) {
        const eq = this.getActiveEqState();
        if (index < 0 || index >= eq.bands.length) return;
        Object.assign(eq.bands[index], changes);
        this.emit('eq:changed');
        this.emit('eq:band-updated', index);
    }

    removeEqBand(index) {
        const eq = this.getActiveEqState();
        if (index < 0 || index >= eq.bands.length) return;
        eq.bands[index].enabled = false;
        // Reset to defaults so slot is clean for next use
        Object.assign(eq.bands[index], { type: 0, freq: 1000, gain: 0, q: 0.707 });
        this.emit('eq:changed');
        this.emit('eq:structure-changed');
    }

    resetEqBands() {
        const eq = this.getActiveEqState();
        eq.bands.forEach(b => {
            b.enabled = false;
            b.type = 0; b.freq = 1000; b.gain = 0; b.q = 0.707;
        });
        this.emit('eq:changed');
        this.emit('eq:structure-changed');
    }

    /** Number of enabled bands in the active EQ */
    getEnabledBandCount() {
        return this.getActiveEqState().bands.filter(b => b.enabled).length;
    }

    // ─── Generic Parameter Update ────────────────────────────────

    updateParam(section, key, value) {
        if (this[section] && key in this[section]) {
            this[section][key] = value;
            this.emit('param:changed', section, key, value);
        }
    }

    // ─── Selection ───────────────────────────────────────────────

    selectModule(moduleId) {
        this.selectedModule = moduleId;
        this.emit('module:selected', moduleId);
    }

    // ─── Presets ─────────────────────────────────────────────────

    setActivePreset(index) {
        this.system.activePreset = index;
        this.emit('preset:active-changed', index);
    }

    // ─── Connection ──────────────────────────────────────────────

    setConnected(connected, portPath = '') {
        this.system.connected = connected;
        this.system.portPath = portPath;
        this.emit('connection:changed', connected);
    }
}

export const store = new Store();