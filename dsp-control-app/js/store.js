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

        // Dynamic EQ
        this.dynamicEq = {
            lowThresh: -4000, normalThresh: -2000, highThresh: -600,
            attackMs: 50, releaseMs: 200,
            eqLow: makeEqState(),
            eqHigh: makeEqState()
        };

        // Compander
        this.compander = {
            threshold: -2000, ratioBelow: 100, ratioAbove: 400,
            attackMs: 10, releaseMs: 100, pregain: 4096
        };

        // Exciter
        this.exciter = { cutoffFreq: 3000, dry: 100, wet: 30 };

        // Dynamic Bass
        this.dynamicBass = { cutoffFreq: 80, gainBoost: 600, enhanced: 0, boostthreshold: -2400, neutralthreshold: -1600, clipthreshold: -800, clipattack: 600, cliprelease: 200 };

        // DRC (WIP)
        this.drc = {
            mode: 0,
            bands: [
                { threshold: -300, ratio: 10, attackMs: 1, releaseMs: 50, pregain: 4096 },
                { threshold: -300, ratio: 10, attackMs: 1, releaseMs: 50, pregain: 4096 },
                { threshold: -300, ratio: 10, attackMs: 1, releaseMs: 50, pregain: 4096 },
                { threshold: -300, ratio: 10, attackMs: 1, releaseMs: 50, pregain: 4096 }
            ]
        };

        // PreGain
        this.preGain = { gainDb: 0, mute: false };

        // PostGain
        this.postGain = { gainDb: 0, mute: false };


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
            default: return MODULE.EQ_DSP_1;
        }
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