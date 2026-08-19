/**
 * @file eq-graph.js
 * @brief Interactive EQ frequency response graph — Canvas-based
 *
 * Features:
 * - Log frequency axis (20 Hz – 20 kHz)
 * - dB scale (-24 dB to +24 dB)
 * - Draggable EQ band nodes
 * - Scroll wheel adjusts Q
 * - Individual + combined response curves
 * - Smooth Bézier rendering
 * - 60fps when dirty, idle otherwise
 *
 * Performance model:
 * - Band curves are cached per-band (Float32Array). Recomputed only when that
 *   band's params change. Cache key = JSON.stringify of {type,freq,gain,q}.
 * - Combined curve is recomputed only when the band-set changes structurally
 *   or any param changes.
 * - During drag, only the dragged band + combined curve are recomputed.
 * - A single rAF loop drives rendering; event handlers only set _dirty=true.
 *   No secondary rAF paths.
 */

import { store } from './store.js';
import { computeBandCurve, computeCombinedCurve, freqAtIndex, indexAtFreq, designBiquad, biquadMagnitudeDb } from './eq-designer.js';

const TWO_PI = 2 * Math.PI;
const DSP_SAMPLE_RATE = 96000;  // Must match firmware config.h DSP_SAMPLE_RATE
const FREQ_MIN = 20;
const FREQ_MAX = 20000;
const DB_MIN = -24;
const DB_MAX = 24;
const NUM_POINTS = 512;

// ─── Curve Cache ─────────────────────────────────────────────────────────────
// Keyed per-band. Survives across frames; entries evicted when band disabled.

function _bandCacheKey(band) {
    // Fast string key — avoids JSON.stringify overhead
    return `${band.type}|${band.freq}|${band.gain}|${band.q}`;
}

class CurveCache {
    constructor() {
        this._bandCurves    = new Map(); // bandIndex → { key, curve: Float32Array }
        this._combined      = null;      // Float32Array | null
        this._combinedKey   = '';        // hash of all active band params
    }

    /** Return cached band curve or null if stale/missing. */
    getBand(idx, band) {
        const entry = this._bandCurves.get(idx);
        if (!entry) return null;
        return entry.key === _bandCacheKey(band) ? entry.curve : null;
    }

    setBand(idx, band, curve) {
        this._bandCurves.set(idx, { key: _bandCacheKey(band), curve });
    }

    evictBand(idx) {
        this._bandCurves.delete(idx);
    }

    /** Combined curve valid only when combinedKey matches. */
    getCombined(key) {
        return this._combinedKey === key ? this._combined : null;
    }

    setCombined(key, curve) {
        this._combinedKey = key;
        this._combined    = curve;
    }

    invalidateCombined() {
        this._combined    = null;
        this._combinedKey = '';
    }

    /** Call when the entire EQ target switches (active EQ changes). */
    clear() {
        this._bandCurves.clear();
        this._combined    = null;
        this._combinedKey = '';
    }
}

// Grid frequencies
const FREQ_MARKERS = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
const FREQ_LABELS = ['20', '50', '100', '200', '500', '1k', '2k', '5k', '10k', '20k'];
const DB_MARKERS = [-24, -18, -12, -6, 0, 6, 12, 18, 24];

// Colors
const COLORS = {
    bg: '#1a1a2e',
    gridLine: 'rgba(255, 255, 255, 0.06)',
    gridLabel: 'rgba(255, 255, 255, 0.35)',
    zeroLine: 'rgba(16, 185, 129, 0.2)',
    combined: '#10b981',
    combinedFill: 'rgba(16, 185, 129, 0.08)',
    bandNormal: 'rgba(136, 146, 176, 0.3)',
    bandSelected: '#8b5cf6',
    nodeNormal: '#8892b0',
    nodeHover: '#ccd6f6',
    nodeSelected: '#8b5cf6',
    nodeGlow: 'rgba(139, 92, 246, 0.4)',
};

const BAND_COLORS = [
    '#ff6b6b', '#ffa06b', '#ffd93d', '#6bcb77',
    '#4ecdc4', '#45b7d1', '#96c4ff', '#a78bfa',
    '#f472b6', '#fb923c'
];

export class EQGraph {
    /**
     * @param {HTMLCanvasElement|string} canvasOrId
     * @param {string} moduleId  — e.g. String(MODULE.EQ_DSP_1), 'DYNEQ_LOW', 'isf1', etc.
     *                             Used to determine what this instance renders,
     *                             independent of store.graphMode/activeEq globals.
     */
    constructor(canvasOrId, moduleId = '') {
        if (typeof canvasOrId === 'string') {
            this.canvas = document.getElementById(canvasOrId);
        } else {
            this.canvas = canvasOrId;
        }
        this.ctx = this.canvas.getContext('2d');
        this.dpr = window.devicePixelRatio || 1;

        // Which accordion this graph belongs to — drives what gets rendered
        this._moduleId = moduleId;

        // Graph area (padded)
        this.pad = { left: 55, right: 20, top: 20, bottom: 35 };

        // Interaction state
        this.dragBand = -1;
        this.hoverBand = -1;
        this.selectedBand = -1;
        this.isDragging = false;
        this.mouseX = 0;
        this.mouseY = 0;

        // Dirty flag — single rAF loop, no secondary paths
        this._dirty = true;
        this._raf = null;
        this._resizeObserver = null;
        this._boundHandlers = {};

        // Curve caches — one for normal EQ, one for ISF (per instance+preset)
        this._eqCache  = new CurveCache();
        this._isfCache = new Map(); // `${which}:${pIdx}` → CurveCache

        // Pre-computed x-positions for NUM_POINTS frequencies (rebuilt on resize)
        this._xLut = null;
        this._xLutW = 0; // gw when LUT was built

        this._setupResize();
        this._setupMouse();
        this._setupStoreListeners();
        this._startLoop();
    }

    // ─── Coordinate Conversion ───────────────────────────────────

    get gx() { return this.pad.left; }
    get gy() { return this.pad.top; }
    get gw() { return this.canvas.width / this.dpr - this.pad.left - this.pad.right; }
    get gh() { return this.canvas.height / this.dpr - this.pad.top - this.pad.bottom; }

    freqToX(freq) {
        const logMin = Math.log10(FREQ_MIN);
        const logMax = Math.log10(FREQ_MAX);
        return this.gx + (Math.log10(freq) - logMin) / (logMax - logMin) * this.gw;
    }

    xToFreq(x) {
        const logMin = Math.log10(FREQ_MIN);
        const logMax = Math.log10(FREQ_MAX);
        const t = (x - this.gx) / this.gw;
        return Math.pow(10, logMin + t * (logMax - logMin));
    }

    dbToY(db) {
        return this.gy + (1 - (db - DB_MIN) / (DB_MAX - DB_MIN)) * this.gh;
    }

    yToDb(y) {
        return DB_MAX - (y - this.gy) / this.gh * (DB_MAX - DB_MIN);
    }

    // ─── X LUT (pre-computed pixel positions) ───────────────────

    /** Rebuild x-position lookup table when graph width changes. */
    _rebuildXLut() {
        const gw = this.gw;
        if (this._xLut && this._xLutW === gw) return; // still valid
        this._xLut = new Float32Array(NUM_POINTS);
        for (let i = 0; i < NUM_POINTS; i++) {
            this._xLut[i] = this.freqToX(freqAtIndex(i, NUM_POINTS));
        }
        this._xLutW = gw;
    }

    // ─── Per-instance mode resolution ───────────────────────────
    // Each EQGraph knows what to render based on its own _moduleId,
    // NOT on the global store.graphMode / store.activeEq.

    /** Returns the render mode string for this instance. */
    _getMyGraphMode() {
        switch (this._moduleId) {
            case 'DYNEQ_LOW':
            case 'DYNEQ_HIGH':  return 'dynamicEq';
            case 'isf1':        return 'isf1';
            case 'isf2':        return 'isf2';
            case 'PRE_EQ':       return 'preEq';
            default:            return 'eq';
        }
    }

    /** Returns the EQ state object this instance should read/write. */
    _getMyEqState() {
        switch (this._moduleId) {
            case 'DYNEQ_LOW':   return store.dynamicEq.eqLow;
            case 'DYNEQ_HIGH':  return store.dynamicEq.eqHigh;
            case 'EQ_LEFT':     return store.leftRightEq.eqLeft;
            case 'EQ_RIGHT':    return store.leftRightEq.eqRight;
            case 'EQ_DSP_1':    return store.eq1;
            case 'EQ_DSP_2':    return store.eq2;
            case 'PRE_EQ':      return store.preEq;
            case 'isf1':
            case 'isf2':        return null;
            default:            return null;
        }
    }

    /** ISF instance key for this graph ('isf1' | 'isf2' | null). */
    _getMyIsfWhich() {
        if (this._moduleId === 'isf1') return 'isf1';
        if (this._moduleId === 'isf2') return 'isf2';
        return null;
    }

    // ─── ISF Cache Accessor ──────────────────────────────────────

    _getIsfCache(which, pIdx) {
        const key = `${which}:${pIdx}`;
        if (!this._isfCache.has(key)) this._isfCache.set(key, new CurveCache());
        return this._isfCache.get(key);
    }

    _clearIsfCache(which) {
        // Evict all presets for this instance
        for (const k of this._isfCache.keys()) {
            if (k.startsWith(`${which}:`)) this._isfCache.delete(k);
        }
    }

    // ─── Rendering ───────────────────────────────────────────────

    markDirty() { this._dirty = true; }

    _startLoop() {
        const loop = () => {
            if (this._dirty) {
                this._dirty = false;
                try { this._render(); } catch (e) { console.warn('EQGraph render error:', e); }
            }
            this._raf = requestAnimationFrame(loop);
        };
        loop();
    }

    _render() {
        const w = this.canvas.width / this.dpr;
        const h = this.canvas.height / this.dpr;

        // Skip rendering if canvas has no dimensions
        if (w < 10 || h < 10) return;

        // Rebuild x-LUT if graph width changed (resize)
        this._rebuildXLut();

        const ctx = this.ctx;
        ctx.save();
        ctx.clearRect(0, 0, w, h);

        // Background
        ctx.fillStyle = COLORS.bg;
        ctx.fillRect(0, 0, w, h);

        this._drawGrid(ctx);

        // Each instance renders its own content, not the global graphMode
        const myMode = this._getMyGraphMode();

        if (myMode === 'dynamicEq') {
            this._drawDynEqOverlay(ctx);
            this._drawBandCurves(ctx);
            this._drawNodes(ctx);
        } else if (myMode === 'isf1' || myMode === 'isf2') {
            this._drawIsfOverlay(ctx, myMode);
            this._drawBandCurves(ctx);
            this._drawNodes(ctx);
        } else {
            this._drawBandCurves(ctx);
            this._drawCombinedCurve(ctx);
            this._drawNodes(ctx);
        }

        ctx.restore();
    }

    // ─── ISF Overlay ──────────────────────────────────────────────────────
    // Draws all preset curves for one ISF instance.
    // Active preset = thick solid, others = thin dashed.
    // Highlighted preset tab = slewIndex's floor/ceil.

    _drawIsfOverlay(ctx, which) {
        const isf = store.getIsfInstance(which);
        if (!isf) return;

        const activeA  = isf.activeA || 0;
        const activeB  = isf.activeB || 0;
        const slewIdx  = isf.slewIndex || 0;
        const blend    = slewIdx - Math.floor(slewIdx);

        // Preset colors — cycle through BAND_COLORS
        const colors = [
            '#ff6b6b','#ffa06b','#ffd93d','#6bcb77',
            '#4ecdc4','#45b7d1','#96c4ff','#a78bfa',
            '#f472b6','#fb923c'
        ];

        // Draw inactive curves first (below)
        for (let p = 0; p < isf.numPresets; p++) {
            if (p === activeA || p === activeB) continue;
            const preset = isf.presets[p];
            const col = colors[p % colors.length];
            this._drawOverlayCurve(ctx, preset.bands.slice(0, preset.numBands), col,
                `P${p+1}`, false, preset.pregainDb || 0, `${which}:overlay:${p}`);
        }

        // Draw B (next) curve
        if (activeB !== activeA && blend > 0.001) {
            const presetB = isf.presets[activeB];
            const colB = colors[activeB % colors.length];
            this._drawOverlayCurve(ctx, presetB.bands.slice(0, presetB.numBands), colB,
                `P${activeB+1}`, false, presetB.pregainDb || 0, `${which}:overlay:${activeB}`);
        }

        // Draw A (current) curve — always on top, thick
        const presetA = isf.presets[activeA];
        const colA = colors[activeA % colors.length];
        this._drawOverlayCurve(ctx, presetA.bands.slice(0, presetA.numBands), colA,
            `P${activeA+1}`, true, presetA.pregainDb || 0, `${which}:overlay:${activeA}`);

        // Draw selected preset (if different from active A) with dashed highlight
        const selP = store.getActiveIsfPreset ? store.getActiveIsfPreset() : 0;
        if (selP !== activeA) {
            const presetSel = isf.presets[selP];
            const colSel = colors[selP % colors.length];
            // Reuse the overlay cache for this preset
            const cacheId = `overlay:${which}:overlay:${selP}`;
            let selKey = String(presetSel.pregainDb || 0);
            for (let b = 0; b < presetSel.numBands; b++) {
                const bd = presetSel.bands[b];
                if (bd.enabled) selKey += `|${b}:${_bandCacheKey(bd)}`;
            }
            if (!this._isfCache.has(cacheId)) this._isfCache.set(cacheId, new CurveCache());
            const selCache = this._isfCache.get(cacheId);
            let combined = selCache.getCombined(selKey);
            if (!combined) {
                combined = computeCombinedCurve(
                    presetSel.bands.slice(0, presetSel.numBands),
                    DSP_SAMPLE_RATE, NUM_POINTS, presetSel.pregainDb || 0);
                selCache.setCombined(selKey, combined);
            }
            const xLut = this._xLut;
            ctx.strokeStyle = colSel;
            ctx.lineWidth   = 2;
            ctx.setLineDash([4, 3]);
            ctx.beginPath();
            for (let i = 0; i < NUM_POINTS; i++) {
                const x = xLut[i];
                const y = this.dbToY(combined[i]);
                if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
            }
            ctx.stroke();
            ctx.setLineDash([]);
        }

        // Legend
        ctx.font = '10px Inter, sans-serif';
        ctx.textAlign = 'left';
        const lx = this.gx + 8;
        let ly = this.gy + 14;
        for (let p = 0; p < Math.min(isf.numPresets, 10); p++) {
            const col = colors[p % colors.length];
            ctx.fillStyle = col;
            ctx.fillRect(lx + p * 65, ly - 8, 10, 3);
            ctx.fillText(`P${p+1}: ${isf.presets[p].thresholdDb.toFixed(0)}dB`, lx + p * 65 + 14, ly);
        }
    }

    _drawDynEqOverlay(ctx) {
        const dLow = store.dynamicEq.eqLow;
        const dHigh = store.dynamicEq.eqHigh;

        // Combined curve for EQ Low (cyan) — highlight if this instance IS dynLow
        this._drawOverlayCurve(ctx, dLow.bands, '#64ffda', 'EQ Low', this._moduleId === 'DYNEQ_LOW', dLow.pregain || 0, 'dynLow');
        // Combined curve for EQ High (orange) — highlight if this instance IS dynHigh
        this._drawOverlayCurve(ctx, dHigh.bands, '#ffa06b', 'EQ High', this._moduleId === 'DYNEQ_HIGH', dHigh.pregain || 0, 'dynHigh');

        // Legend
        ctx.font = '11px Inter, sans-serif';
        const legendX = this.gx + 10;
        const legendY = this.gy + 18;

        ctx.fillStyle = '#64ffda';
        ctx.fillRect(legendX, legendY - 8, 12, 3);
        ctx.fillStyle = this._moduleId === 'DYNEQ_LOW' ? '#64ffda' : 'rgba(100,255,218,0.5)';
        ctx.textAlign = 'left';
        ctx.fillText('EQ Low', legendX + 16, legendY);

        ctx.fillStyle = '#ffa06b';
        ctx.fillRect(legendX + 80, legendY - 8, 12, 3);
        ctx.fillStyle = this._moduleId === 'DYNEQ_HIGH' ? '#ffa06b' : 'rgba(255,160,107,0.5)';
        ctx.fillText('EQ High', legendX + 96, legendY);
    }

    // Legend
    /**
     * _drawOverlayCurve: draws a combined response for an arbitrary band array.
     * cacheKey uniquely identifies the owner (e.g. 'isf1:2' or 'dynLow').
     * Curve is cached; recomputed only when band params change.
     */
    _drawOverlayCurve(ctx, bands, color, label, isActive, pregainDb = 0, cacheKey = label) {
        // Build combined-key from bands + pregain
        let key = String(pregainDb);
        for (let b = 0; b < bands.length; b++) {
            const bd = bands[b];
            if (bd.enabled) key += `|${b}:${_bandCacheKey(bd)}`;
        }

        // Use a dedicated CurveCache per overlay (stored in _isfCache keyed by cacheKey)
        const cacheId = `overlay:${cacheKey}`;
        if (!this._isfCache.has(cacheId)) this._isfCache.set(cacheId, new CurveCache());
        const cache = this._isfCache.get(cacheId);

        let combined = cache.getCombined(key);
        if (!combined) {
            combined = computeCombinedCurve(bands, DSP_SAMPLE_RATE, NUM_POINTS, pregainDb);
            cache.setCombined(key, combined);
        }

        const xLut  = this._xLut;
        const zeroY = this.dbToY(0);

        // Fill
        ctx.beginPath();
        ctx.moveTo(this.gx, zeroY);
        for (let i = 0; i < NUM_POINTS; i++) {
            ctx.lineTo(xLut[i], this.dbToY(combined[i]));
        }
        ctx.lineTo(this.gx + this.gw, zeroY);
        ctx.closePath();
        ctx.fillStyle = isActive ? `${color}18` : `${color}08`;
        ctx.fill();

        // Stroke
        ctx.strokeStyle = isActive ? color : `${color}66`;
        ctx.lineWidth   = isActive ? 2.5 : 1.5;
        ctx.setLineDash(isActive ? [] : [6, 4]);
        ctx.beginPath();
        for (let i = 0; i < NUM_POINTS; i++) {
            const x = xLut[i];
            const y = this.dbToY(combined[i]);
            if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
        }
        ctx.stroke();
        ctx.setLineDash([]);
    }

    _drawGrid(ctx) {
        ctx.font = '11px "Inter", sans-serif';

        // Frequency grid lines
        for (let i = 0; i < FREQ_MARKERS.length; i++) {
            const x = this.freqToX(FREQ_MARKERS[i]);
            ctx.strokeStyle = COLORS.gridLine;
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(x, this.gy);
            ctx.lineTo(x, this.gy + this.gh);
            ctx.stroke();

            ctx.fillStyle = COLORS.gridLabel;
            ctx.textAlign = 'center';
            ctx.fillText(FREQ_LABELS[i], x, this.gy + this.gh + 16);
        }

        // dB grid lines
        for (const db of DB_MARKERS) {
            const y = this.dbToY(db);
            ctx.strokeStyle = db === 0 ? COLORS.zeroLine : COLORS.gridLine;
            ctx.lineWidth = db === 0 ? 1.5 : 1;
            ctx.beginPath();
            ctx.moveTo(this.gx, y);
            ctx.lineTo(this.gx + this.gw, y);
            ctx.stroke();

            ctx.fillStyle = COLORS.gridLabel;
            ctx.textAlign = 'right';
            ctx.fillText(`${db > 0 ? '+' : ''}${db}`, this.gx - 8, y + 4);
        }

        // Border
        ctx.strokeStyle = 'rgba(255,255,255, 0.1)';
        ctx.lineWidth = 1;
        ctx.strokeRect(this.gx, this.gy, this.gw, this.gh);
    }

    /**
     * Draw individual band curves.
     * Each band curve is cached in a Float32Array keyed by {type,freq,gain,q}.
     * Only the dragged band (or newly changed band) is recomputed.
     */
    _drawBandCurves(ctx) {
        const xLut   = this._xLut;
        const myMode = this._getMyGraphMode();

        if (myMode === 'isf1' || myMode === 'isf2') {
            const which  = myMode;
            const isf    = store.getIsfInstance(which);
            const pIdx   = store.getActiveIsfPreset ? store.getActiveIsfPreset() : 0;
            const preset = isf?.presets[pIdx];
            if (!preset) return;

            const cache = this._getIsfCache(which, pIdx);

            preset.bands.forEach((band, i) => {
                if (!band.enabled) { cache.evictBand(i); return; }
                let curve = cache.getBand(i, band);
                if (!curve) {
                    curve = computeBandCurve(band, DSP_SAMPLE_RATE, NUM_POINTS);
                    cache.setBand(i, band, curve);
                }
                const color = BAND_COLORS[i % BAND_COLORS.length];
                ctx.strokeStyle = i === this.selectedBand ? color : `${color}44`;
                ctx.lineWidth   = i === this.selectedBand ? 1.5 : 1;
                ctx.setLineDash([]);
                ctx.beginPath();
                for (let j = 0; j < NUM_POINTS; j++) {
                    const x = xLut[j];
                    const y = this.dbToY(curve[j]);
                    if (j === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
                }
                ctx.stroke();
            });
            return;
        }

        const eq = this._getMyEqState();
        if (!eq || !eq.bands.some(b => b.enabled)) return;

        const cache = this._eqCache;
        for (let b = 0; b < eq.bands.length; b++) {
            const band = eq.bands[b];
            if (!band.enabled) { cache.evictBand(b); continue; }
            let curve = cache.getBand(b, band);
            if (!curve) {
                curve = computeBandCurve(band, DSP_SAMPLE_RATE, NUM_POINTS);
                cache.setBand(b, band, curve);
                cache.invalidateCombined();
            }
            const color = BAND_COLORS[b % BAND_COLORS.length];
            ctx.strokeStyle = b === this.selectedBand ? color : `${color}44`;
            ctx.lineWidth   = b === this.selectedBand ? 1.5 : 1;
            ctx.setLineDash([]);
            ctx.beginPath();
            for (let i = 0; i < NUM_POINTS; i++) {
                const x = xLut[i];
                const y = this.dbToY(curve[i]);
                if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
            }
            ctx.stroke();
        }
    }

    _drawCombinedCurve(ctx) {
        const eq    = this._getMyEqState();
        if (!eq) return;
        const cache  = this._eqCache;
        const xLut   = this._xLut;

        // Build a cheap key from all active band params + pregain
        // so the combined curve is recomputed only when something truly changed.
        let combinedKey = String(eq.pregain || 0);
        for (let b = 0; b < eq.bands.length; b++) {
            const bd = eq.bands[b];
            if (bd.enabled) combinedKey += `|${b}:${_bandCacheKey(bd)}`;
        }

        let combined = cache.getCombined(combinedKey);
        if (!combined) {
            combined = computeCombinedCurve(eq.bands, DSP_SAMPLE_RATE, NUM_POINTS, eq.pregain || 0);
            cache.setCombined(combinedKey, combined);
        }

        // Fill under curve
        ctx.beginPath();
        const zeroY = this.dbToY(0);
        ctx.moveTo(this.gx, zeroY);
        for (let i = 0; i < NUM_POINTS; i++) {
            ctx.lineTo(xLut[i], this.dbToY(combined[i]));
        }
        ctx.lineTo(this.gx + this.gw, zeroY);
        ctx.closePath();

        const grad = ctx.createLinearGradient(0, this.gy, 0, this.gy + this.gh);
        grad.addColorStop(0,   'rgba(100, 255, 218, 0.12)');
        grad.addColorStop(0.5, 'rgba(100, 255, 218, 0.03)');
        grad.addColorStop(1,   'rgba(100, 255, 218, 0.12)');
        ctx.fillStyle = grad;
        ctx.fill();

        // Stroke combined curve
        ctx.strokeStyle = COLORS.combined;
        ctx.lineWidth   = 2.5;
        ctx.beginPath();
        for (let i = 0; i < NUM_POINTS; i++) {
            const x = xLut[i];
            const y = this.dbToY(combined[i]);
            if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
        }
        ctx.stroke();
    }

    _drawNodes(ctx) {
        const myMode = this._getMyGraphMode();

        if (myMode === 'isf1' || myMode === 'isf2') {
            const isf  = store.getIsfInstance(myMode);
            const pIdx = store.getActiveIsfPreset ? store.getActiveIsfPreset() : 0;
            const preset = isf?.presets[pIdx];
            if (!preset) return;
            preset.bands.forEach((band, i) => {
                if (!band.enabled) return;
                const x = this.freqToX(band.freq);
                const y = this.dbToY(band.gain);
                const color = BAND_COLORS[i % BAND_COLORS.length];
                ctx.beginPath();
                ctx.arc(x, y, 7, 0, TWO_PI);
                ctx.fillStyle = color;
                ctx.fill();
                ctx.strokeStyle = '#fff';
                ctx.lineWidth = 1.5;
                ctx.stroke();
                ctx.fillStyle = '#fff';
                ctx.font = 'bold 9px Inter';
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillText(i + 1, x, y);
            });
            return;
        }

        const eq = this._getMyEqState();
        if (!eq) return;

        for (let b = 0; b < eq.bands.length; b++) {
            const band = eq.bands[b];
            if (!band.enabled) continue;

            const x = this.freqToX(band.freq);
            const y = this.dbToY(band.gain);
            const r = 7;
            const color = BAND_COLORS[b % BAND_COLORS.length];
            const isSelected = b === this.selectedBand;
            const isHover = b === this.hoverBand;

            // Glow for selected
            if (isSelected) {
                ctx.shadowColor = color;
                ctx.shadowBlur = 16;
            }

            // Node circle
            ctx.beginPath();
            ctx.arc(x, y, r, 0, TWO_PI);
            ctx.fillStyle = isSelected ? color : (isHover ? COLORS.nodeHover : COLORS.nodeNormal);
            ctx.fill();
            ctx.strokeStyle = color;
            ctx.lineWidth = 2;
            ctx.stroke();

            ctx.shadowColor = 'transparent';
            ctx.shadowBlur = 0;

            // Band number label
            ctx.fillStyle = isSelected ? '#fff' : 'rgba(255,255,255,0.6)';
            ctx.font = 'bold 9px Inter, sans-serif';
            ctx.textAlign = 'center';
            ctx.fillText(b + 1, x, y - 12);
        }
    }

    // ─── Mouse Interaction ───────────────────────────────────────

    _setupMouse() {
        const c = this.canvas;

        this._boundHandlers.mousedown = (e) => this._onMouseDown(e);
        this._boundHandlers.mousemove = (e) => this._onMouseMove(e);
        this._boundHandlers.mouseup = (e) => this._onMouseUp(e);
        this._boundHandlers.mouseleave = () => this._onMouseLeave();
        this._boundHandlers.wheel = (e) => this._onWheel(e);
        this._boundHandlers.dblclick = (e) => this._onDblClick(e);

        let lastTap = 0;
        this._boundHandlers.touchstart = (e) => {
            if (e.touches.length > 0) {
                const now = Date.now();
                if (now - lastTap < 300) {
                    this._onDblClick(e.touches[0]);
                    e.preventDefault();
                } else {
                    this._onMouseDown(e.touches[0]);
                    if (this.isDragging) e.preventDefault();
                }
                lastTap = now;
            }
        };
        this._boundHandlers.touchmove = (e) => {
            if (this.isDragging) e.preventDefault();
            if (e.touches.length > 0) this._onMouseMove(e.touches[0]);
        };
        this._boundHandlers.touchend = (e) => {
            if (this.isDragging) e.preventDefault();
            this._onMouseUp(e);
        };
        this._boundHandlers.touchcancel = (e) => this._onMouseLeave();

        c.addEventListener('mousedown', this._boundHandlers.mousedown);
        c.addEventListener('mousemove', this._boundHandlers.mousemove);
        c.addEventListener('mouseup', this._boundHandlers.mouseup);
        c.addEventListener('mouseleave', this._boundHandlers.mouseleave);
        c.addEventListener('wheel', this._boundHandlers.wheel, { passive: false });
        c.addEventListener('dblclick', this._boundHandlers.dblclick);

        c.addEventListener('touchstart', this._boundHandlers.touchstart, { passive: false });
        c.addEventListener('touchmove', this._boundHandlers.touchmove, { passive: false });
        c.addEventListener('touchend', this._boundHandlers.touchend, { passive: false });
        c.addEventListener('touchcancel', this._boundHandlers.touchcancel);
    }

    _getPos(e) {
        const rect = this.canvas.getBoundingClientRect();
        return { x: e.clientX - rect.left, y: e.clientY - rect.top };
    }

    _hitTestBand(x, y) {
        let bands;
        const myMode = this._getMyGraphMode();
        if (myMode === 'isf1' || myMode === 'isf2') {
            const isf  = store.getIsfInstance(myMode);
            const pIdx = store.getActiveIsfPreset ? store.getActiveIsfPreset() : 0;
            bands = isf?.presets[pIdx]?.bands.slice(0, isf.presets[pIdx].numBands) ?? [];
        } else {
            const eq = this._getMyEqState();
            bands = eq ? eq.bands : [];
        }
        let closest = -1, minDist = 15;
        for (let b = 0; b < bands.length; b++) {
            if (!bands[b].enabled) continue;
            const dist = Math.hypot(x - this.freqToX(bands[b].freq), y - this.dbToY(bands[b].gain));
            if (dist < minDist) { minDist = dist; closest = b; }
        }
        return closest;
    }

    _onMouseDown(e) {
        const { x, y } = this._getPos(e);
        const band = this._hitTestBand(x, y);

        if (band >= 0) {
            this.selectedBand = band;
            this.dragBand = band;
            this.isDragging = true;
            this.canvas.style.cursor = 'grabbing';
            store.emit('eq:band-selected', band);
        } else {
            this.selectedBand = -1;
            store.emit('eq:band-selected', -1);
        }
        this.markDirty();
    }

    _onMouseMove(e) {
        const { x, y } = this._getPos(e);
        this.mouseX = x;
        this.mouseY = y;

        if (this.isDragging && this.dragBand >= 0) {
            const myMode = this._getMyGraphMode();
            const freq  = Math.max(FREQ_MIN, Math.min(FREQ_MAX, this.xToFreq(x)));
            const gain  = Math.max(DB_MIN,   Math.min(DB_MAX,   this.yToDb(y)));
            const freqR = Math.round(freq);
            let gainR = 0;
            if (myMode !== 'preEq') gainR = Math.round(gain * 10) / 10;
            else gainR = 0;

            if (myMode === 'isf1' || myMode === 'isf2') {
                store.updateIsfEqBand(myMode, this.dragBand, freqR, gainR);
            } else {
                this._myUpdateEqBand(this.dragBand, { freq: freqR, gain: gainR });
            }
        } else {
            // Hover detection
            const band = this._hitTestBand(x, y);
            if (band !== this.hoverBand) {
                this.hoverBand = band;
                this.canvas.style.cursor = band >= 0 ? 'grab' : 'crosshair';
                this.markDirty();
            }
        }
    }

    _onMouseUp() {
        if (this.isDragging) {
            this.isDragging = false;
            this.dragBand   = -1;
            this.canvas.style.cursor = this.hoverBand >= 0 ? 'grab' : 'crosshair';
        }
    }

    _onMouseLeave() {
        this.hoverBand  = -1;
        this.isDragging = false;
        this.dragBand   = -1;
        this.canvas.style.cursor = 'default';
        this.markDirty();
    }

    _onWheel(e) {
        e.preventDefault();
        const { x, y } = this._getPos(e);
        const band  = this.selectedBand >= 0 ? this.selectedBand : this._hitTestBand(x, y);
        if (band < 0) return;
        const delta   = e.deltaY > 0 ? -0.1 : 0.1;
        const myMode  = this._getMyGraphMode();

        if (myMode === 'isf1' || myMode === 'isf2') {
            const isf  = store.getIsfInstance(myMode);
            const pIdx = store.getActiveIsfPreset ? store.getActiveIsfPreset() : 0;
            const preset = isf?.presets[pIdx];
            if (!preset?.bands[band]) return;
            preset.bands[band].q = Math.max(0.1, Math.min(20,
                Math.round((preset.bands[band].q + delta) * 100) / 100));
            store.emit('isf:band-changed', myMode, pIdx, band);
            store.emit('isf:eq-changed');
            return;
        }

        const eq = this._getMyEqState();
        if (!eq) return;
        const q = Math.max(0.1, Math.min(20,
            Math.round((eq.bands[band].q + delta) * 100) / 100));
        this._myUpdateEqBand(band, { q });
    }

    _onDblClick(e) {
        const { x, y } = this._getPos(e);
        const band   = this._hitTestBand(x, y);
        const freq   = Math.round(this.xToFreq(x));
        const gain   = Math.round(this.yToDb(y) * 10) / 10;
        const inGraph = x >= this.gx && x <= this.gx + this.gw &&
                        y >= this.gy && y <= this.gy + this.gh;
        const myMode  = this._getMyGraphMode();

        if (myMode === 'isf1' || myMode === 'isf2') {
            const isf  = store.getIsfInstance(myMode);
            const pIdx = store.getActiveIsfPreset ? store.getActiveIsfPreset() : 0;
            const preset = isf?.presets[pIdx];
            if (!preset) return;
            if (band >= 0) {
                preset.bands[band].gain = 0;
                store.emit('isf:eq-changed', myMode, pIdx);
            } else if (inGraph) {
                const slot = preset.bands.findIndex(b => !b.enabled);
                if (slot === -1) return;
                Object.assign(preset.bands[slot], { enabled: true, freq, gain, q: 0.707, type: 0 });
                preset.numBands = preset.bands.filter(b => b.enabled).length;
                this.selectedBand = slot;
                store.emit('isf:band-added', myMode, pIdx, slot);
            }
            return;
        }

        if (band >= 0) {
            this._myUpdateEqBand(band, { gain: 0 });
        } else if (inGraph) {
            const idx = this._myAddEqBand(freq, gain, 0.707, 0);
            if (idx !== null) {
                this.selectedBand = idx;
                store.emit('eq:band-selected', idx);
            }
        }
    }

    // ─── Instance-local EQ mutation helpers ─────────────────────
    // These operate on _getMyEqState() directly, bypassing
    // store.addEqBand/updateEqBand which always use store.getActiveEqState().

    _myAddEqBand(freq, gain, q, type) {
        const eq = this._getMyEqState();
        if (!eq) return null;
        const slot = eq.bands.findIndex(b => !b.enabled);
        if (slot === -1) return null;
        Object.assign(eq.bands[slot], { enabled: true, freq, gain, q, type });
        this._eqCache.invalidateCombined();
        store.emit('eq:changed');
        store.emit('eq:structure-changed');
        return slot;
    }

    _myUpdateEqBand(index, changes) {
        const eq = this._getMyEqState();
        if (!eq || index < 0 || index >= eq.bands.length) return;
        Object.assign(eq.bands[index], changes);
        this._eqCache.evictBand(index);
        this._eqCache.invalidateCombined();
        store.emit('eq:changed');
        store.emit('eq:band-updated', index);
    }

    _myRemoveEqBand(index) {
        const eq = this._getMyEqState();
        if (!eq || index < 0 || index >= eq.bands.length) return;
        eq.bands[index].enabled = false;
        Object.assign(eq.bands[index], { type: 0, freq: 1000, gain: 0, q: 0.707 });
        this._eqCache.evictBand(index);
        this._eqCache.invalidateCombined();
        store.emit('eq:changed');
        store.emit('eq:structure-changed');
    }

    // ─── Resize ──────────────────────────────────────────────────

    _setupResize() {
        this._boundHandlers.resize = () => this.forceResize();

        window.addEventListener(
            'resize',
            this._boundHandlers.resize
        );

        this._resizeObserver = new ResizeObserver(() => {
            this.forceResize();
        });

        this._resizeObserver.observe(
            this.canvas.parentElement
        );

        this.forceResize();
    }

    forceResize() {
        const parent = this.canvas.parentElement;
        if (!parent) return;

        const rect = parent.getBoundingClientRect();
        if (rect.width < 10 || rect.height < 10) return;

        console.log(`EQGraph resize: ${rect.width}x${rect.height}, dpr=${this.dpr}`);

        // Set canvas pixel dimensions
        this.canvas.width = Math.round(rect.width * this.dpr);
        this.canvas.height = Math.round(rect.height * this.dpr);
        this.canvas.style.width = Math.round(rect.width) + 'px';
        this.canvas.style.height = Math.round(rect.height) + 'px';

        // Reset and apply DPR scaling
        this.ctx.setTransform(1, 0, 0, 1, 0, 0);
        this.ctx.scale(this.dpr, this.dpr);

        // Invalidate x-LUT — pixel positions change with new graph width
        this._xLut   = null;
        this._xLutW  = 0;

        this.markDirty();
    }

    // ─── Store Listeners ─────────────────────────────────────────

    _setupStoreListeners() {
        // ── EQ ──
        // eq:changed → invalidate combined only (individual band caches survive;
        // _drawBandCurves will miss on the changed band and recompute it).
        this._boundHandlers.eqChanged = () => {
            this._eqCache.invalidateCombined();
            this.markDirty();
        };
        this._boundHandlers.eqBandUpdated = (idx) => {
            this._eqCache.invalidateCombined();
            this.markDirty();
        };
        this._boundHandlers.storeActiveChanged = () => {
            // Switching active EQ target — blow the whole eq cache
            this._eqCache.clear();
            this.selectedBand = -1;
            this.markDirty();
        };

        // ── ISF ──
        // isf:band-dragging — emitted by store.updateIsfEqBand during drag.
        // Evict only the dragged band's entry so _drawBandCurves recomputes it.
        this._boundHandlers.isfBandDragging = (which, pIdx, bandIdx) => {
            const cache = this._getIsfCache(which, pIdx);
            cache.evictBand(bandIdx);
            cache.invalidateCombined();
            // Also evict the overlay combined cache for this preset
            const overlayKey = `overlay:${which}:overlay:${pIdx}`;
            if (this._isfCache.has(overlayKey)) {
                this._isfCache.get(overlayKey).invalidateCombined();
            }
            this.markDirty();
        };
        // isf:eq-changed — a band was added/removed/changed via panel (not drag)
        this._boundHandlers.isfEqChanged = (which, pIdx) => {
            if (which !== undefined && pIdx !== undefined) {
                this._getIsfCache(which, pIdx).clear();
                const overlayKey = `overlay:${which}:overlay:${pIdx}`;
                if (this._isfCache.has(overlayKey)) this._isfCache.get(overlayKey).clear();
            } else {
                // Blanket clear for this instance's ISF cache
                const myIsf = this._getMyIsfWhich();
                if (myIsf) this._clearIsfCache(myIsf);
            }
            this.markDirty();
        };
        this._boundHandlers.isfStateUpdated = () => { this.markDirty(); };
        this._boundHandlers.isfPresetChanged = () => { this.markDirty(); };
        this._boundHandlers.isfPresetDataUpdated = (which, pIdx) => {
            this._getIsfCache(which, pIdx).clear();
            const overlayKey = `overlay:${which}:overlay:${pIdx}`;
            if (this._isfCache.has(overlayKey)) this._isfCache.get(overlayKey).clear();
            this.markDirty();
        };
        this._boundHandlers.isfInstanceChanged = () => {
            // Switching ISF1↔ISF2 — reset selected band, rest already cached
            this.selectedBand = -1;
            this.markDirty();
        };

        // Register all listeners
        store.on('eq:changed',               this._boundHandlers.eqChanged);
        store.on('eq:band-updated',          this._boundHandlers.eqBandUpdated);
        store.on('eq:active-changed',        this._boundHandlers.storeActiveChanged);
        store.on('eq:structure-changed',     this._boundHandlers.eqChanged);

        store.on('isf:band-dragging',        this._boundHandlers.isfBandDragging);
        store.on('isf:eq-changed',           this._boundHandlers.isfEqChanged);
        store.on('isf:band-changed',         this._boundHandlers.isfEqChanged);
        store.on('isf:band-added',           this._boundHandlers.isfEqChanged);
        store.on('isf:state-updated',        this._boundHandlers.isfStateUpdated);
        store.on('isf:preset-changed',       this._boundHandlers.isfPresetChanged);
        store.on('isf:preset-selected',      this._boundHandlers.isfPresetChanged);
        store.on('isf:preset-data-updated',  this._boundHandlers.isfPresetDataUpdated);
        store.on('isf:instance-changed',     this._boundHandlers.isfInstanceChanged);
    }

    // ─── Public ──────────────────────────────────────────────────

    selectBand(index) {
        this.selectedBand = index;
        this.markDirty();
    }

    destroy() {
        if (this._raf) cancelAnimationFrame(this._raf);

        if (this._resizeObserver) {
            this._resizeObserver.disconnect();
        }

        window.removeEventListener('resize', this._boundHandlers.resize);

        const c = this.canvas;
        if (c) {
            c.removeEventListener('mousedown', this._boundHandlers.mousedown);
            c.removeEventListener('mousemove', this._boundHandlers.mousemove);
            c.removeEventListener('mouseup', this._boundHandlers.mouseup);
            c.removeEventListener('mouseleave', this._boundHandlers.mouseleave);
            c.removeEventListener('wheel', this._boundHandlers.wheel);
            c.removeEventListener('dblclick', this._boundHandlers.dblclick);

            c.removeEventListener('touchstart', this._boundHandlers.touchstart);
            c.removeEventListener('touchmove', this._boundHandlers.touchmove);
            c.removeEventListener('touchend', this._boundHandlers.touchend);
            c.removeEventListener('touchcancel', this._boundHandlers.touchcancel);
        }

        store.off('eq:changed',              this._boundHandlers.eqChanged);
        store.off('eq:band-updated',         this._boundHandlers.eqBandUpdated);
        store.off('eq:active-changed',       this._boundHandlers.storeActiveChanged);
        store.off('eq:structure-changed',    this._boundHandlers.eqChanged);

        store.off('isf:band-dragging',       this._boundHandlers.isfBandDragging);
        store.off('isf:eq-changed',          this._boundHandlers.isfEqChanged);
        store.off('isf:band-changed',        this._boundHandlers.isfEqChanged);
        store.off('isf:band-added',          this._boundHandlers.isfEqChanged);
        store.off('isf:state-updated',       this._boundHandlers.isfStateUpdated);
        store.off('isf:preset-changed',      this._boundHandlers.isfPresetChanged);
        store.off('isf:preset-selected',     this._boundHandlers.isfPresetChanged);
        store.off('isf:preset-data-updated', this._boundHandlers.isfPresetDataUpdated);
        store.off('isf:instance-changed',    this._boundHandlers.isfInstanceChanged);
    }
}