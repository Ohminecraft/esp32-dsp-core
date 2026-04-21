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
    constructor(canvasOrId) {
        if (typeof canvasOrId === 'string') {
            this.canvas = document.getElementById(canvasOrId);
        } else {
            this.canvas = canvasOrId;
        }
        this.ctx = this.canvas.getContext('2d');
        this.dpr = window.devicePixelRatio || 1;

        // Graph area (padded)
        this.pad = { left: 55, right: 20, top: 20, bottom: 35 };

        // Interaction state
        this.dragBand = -1;
        this.hoverBand = -1;
        this.selectedBand = -1;
        this.isDragging = false;
        this.mouseX = 0;
        this.mouseY = 0;

        // Dirty flag
        this._dirty = true;
        this._raf = null;
        this._resizeObserver = null;
        this._boundHandlers = {};

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

        const ctx = this.ctx;
        ctx.save();
        ctx.clearRect(0, 0, w, h);

        // Background
        ctx.fillStyle = COLORS.bg;
        ctx.fillRect(0, 0, w, h);

        this._drawGrid(ctx);

        if (store.graphMode === 'dynamicEq') {
            this._drawDynEqOverlay(ctx);
            this._drawBandCurves(ctx);
            this._drawNodes(ctx);
        } else {
            this._drawBandCurves(ctx);
            this._drawCombinedCurve(ctx);
            this._drawNodes(ctx);
        }

        ctx.restore();
    }

    _drawDynEqOverlay(ctx) {
        const dLow = store.dynamicEq.eqLow;
        const dHigh = store.dynamicEq.eqHigh;

        // Combined curve for EQ Low (cyan)
        this._drawOverlayCurve(ctx, dLow.bands, '#64ffda', 'EQ Low', store.activeEq === 'dynLow');
        // Combined curve for EQ High (orange)
        this._drawOverlayCurve(ctx, dHigh.bands, '#ffa06b', 'EQ High', store.activeEq === 'dynHigh');

        // Legend
        ctx.font = '11px Inter, sans-serif';
        const legendX = this.gx + 10;
        const legendY = this.gy + 18;

        ctx.fillStyle = '#64ffda';
        ctx.fillRect(legendX, legendY - 8, 12, 3);
        ctx.fillStyle = store.activeEq === 'dynLow' ? '#64ffda' : 'rgba(100,255,218,0.5)';
        ctx.textAlign = 'left';
        ctx.fillText('EQ Low', legendX + 16, legendY);

        ctx.fillStyle = '#ffa06b';
        ctx.fillRect(legendX + 80, legendY - 8, 12, 3);
        ctx.fillStyle = store.activeEq === 'dynHigh' ? '#ffa06b' : 'rgba(255,160,107,0.5)';
        ctx.fillText('EQ High', legendX + 96, legendY);
    }

    _drawOverlayCurve(ctx, bands, color, label, isActive) {
        const combined = computeCombinedCurve(bands, DSP_SAMPLE_RATE, NUM_POINTS);

        // Fill
        ctx.beginPath();
        const zeroY = this.dbToY(0);
        ctx.moveTo(this.gx, zeroY);
        for (let i = 0; i < NUM_POINTS; i++) {
            const freq = freqAtIndex(i, NUM_POINTS);
            ctx.lineTo(this.freqToX(freq), this.dbToY(combined[i]));
        }
        ctx.lineTo(this.gx + this.gw, zeroY);
        ctx.closePath();
        ctx.fillStyle = isActive ? `${color}18` : `${color}08`;
        ctx.fill();

        // Stroke
        ctx.strokeStyle = isActive ? color : `${color}66`;
        ctx.lineWidth = isActive ? 2.5 : 1.5;
        ctx.setLineDash(isActive ? [] : [6, 4]);
        ctx.beginPath();
        for (let i = 0; i < NUM_POINTS; i++) {
            const freq = freqAtIndex(i, NUM_POINTS);
            const x = this.freqToX(freq);
            const y = this.dbToY(combined[i]);
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
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

    _drawBandCurves(ctx) {
        const eq = store.getActiveEqState();
        if (!eq.bands.some(b => b.enabled)) return;

        for (let b = 0; b < eq.bands.length; b++) {
            const band = eq.bands[b];
            if (!band.enabled) continue;

            const curve = computeBandCurve(band, DSP_SAMPLE_RATE, NUM_POINTS);
            const color = BAND_COLORS[b % BAND_COLORS.length];

            ctx.strokeStyle = b === this.selectedBand
                ? color
                : `${color}44`;
            ctx.lineWidth = b === this.selectedBand ? 1.5 : 1;
            ctx.beginPath();

            for (let i = 0; i < NUM_POINTS; i++) {
                const freq = freqAtIndex(i, NUM_POINTS);
                const x = this.freqToX(freq);
                const y = this.dbToY(curve[i]);
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            ctx.stroke();
        }
    }

    _drawCombinedCurve(ctx) {
        const eq = store.getActiveEqState();
        const combined = computeCombinedCurve(eq.bands, DSP_SAMPLE_RATE, NUM_POINTS, eq.pregain || 0);

        // Fill under curve
        ctx.beginPath();
        const zeroY = this.dbToY(0);
        ctx.moveTo(this.gx, zeroY);

        for (let i = 0; i < NUM_POINTS; i++) {
            const freq = freqAtIndex(i, NUM_POINTS);
            const x = this.freqToX(freq);
            const y = this.dbToY(combined[i]);
            ctx.lineTo(x, y);
        }

        ctx.lineTo(this.gx + this.gw, zeroY);
        ctx.closePath();

        const grad = ctx.createLinearGradient(0, this.gy, 0, this.gy + this.gh);
        grad.addColorStop(0, 'rgba(100, 255, 218, 0.12)');
        grad.addColorStop(0.5, 'rgba(100, 255, 218, 0.03)');
        grad.addColorStop(1, 'rgba(100, 255, 218, 0.12)');
        ctx.fillStyle = grad;
        ctx.fill();

        // Stroke combined curve
        ctx.strokeStyle = COLORS.combined;
        ctx.lineWidth = 2.5;
        ctx.beginPath();
        for (let i = 0; i < NUM_POINTS; i++) {
            const freq = freqAtIndex(i, NUM_POINTS);
            const x = this.freqToX(freq);
            const y = this.dbToY(combined[i]);
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.stroke();
    }

    _drawNodes(ctx) {
        const eq = store.getActiveEqState();

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

        c.addEventListener('mousedown', this._boundHandlers.mousedown);
        c.addEventListener('mousemove', this._boundHandlers.mousemove);
        c.addEventListener('mouseup', this._boundHandlers.mouseup);
        c.addEventListener('mouseleave', this._boundHandlers.mouseleave);
        c.addEventListener('wheel', this._boundHandlers.wheel, { passive: false });
        c.addEventListener('dblclick', this._boundHandlers.dblclick);
    }

    _getPos(e) {
        const rect = this.canvas.getBoundingClientRect();
        return { x: e.clientX - rect.left, y: e.clientY - rect.top };
    }

    _hitTestBand(x, y) {
        const eq = store.getActiveEqState();
        let closest = -1;
        let minDist = 15; // px threshold

        for (let b = 0; b < eq.bands.length; b++) {
            if (!eq.bands[b].enabled) continue;
            const bx = this.freqToX(eq.bands[b].freq);
            const by = this.dbToY(eq.bands[b].gain);
            const dist = Math.hypot(x - bx, y - by);
            if (dist < minDist) {
                minDist = dist;
                closest = b;
            }
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
            // Clamp to graph area
            const freq = Math.max(FREQ_MIN, Math.min(FREQ_MAX, this.xToFreq(x)));
            const gain = Math.max(DB_MIN, Math.min(DB_MAX, this.yToDb(y)));

            store.updateEqBand(this.dragBand, {
                freq: Math.round(freq),
                gain: Math.round(gain * 10) / 10
            });
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
            this.dragBand = -1;
            this.canvas.style.cursor = this.hoverBand >= 0 ? 'grab' : 'crosshair';
        }
    }

    _onMouseLeave() {
        this.hoverBand = -1;
        this.isDragging = false;
        this.dragBand = -1;
        this.canvas.style.cursor = 'default';
        this.markDirty();
    }

    _onWheel(e) {
        e.preventDefault();
        const { x, y } = this._getPos(e);
        const band = this.selectedBand >= 0 ? this.selectedBand : this._hitTestBand(x, y);
        if (band < 0) return;

        const eq = store.getActiveEqState();
        let q = eq.bands[band].q;

        // Scroll up = narrow Q (higher Q), down = wider Q (lower Q)
        const delta = e.deltaY > 0 ? -0.1 : 0.1;
        q = Math.max(0.1, Math.min(20, q + delta));
        q = Math.round(q * 100) / 100;

        store.updateEqBand(band, { q });
    }

    _onDblClick(e) {
        const { x, y } = this._getPos(e);
        const band = this._hitTestBand(x, y);

        if (band >= 0) {
            // Double-click on band → reset to 0 dB
            store.updateEqBand(band, { gain: 0 });
        } else if (x >= this.gx && x <= this.gx + this.gw &&
            y >= this.gy && y <= this.gy + this.gh) {
            // Double-click on empty area → add new band
            const freq = Math.round(this.xToFreq(x));
            const gain = Math.round(this.yToDb(y) * 10) / 10;
            const idx = store.addEqBand(freq, gain, 0.707, 0);
            this.selectedBand = idx;
            store.emit('eq:band-selected', idx);
        }
    }

    // ─── Resize ──────────────────────────────────────────────────

    _setupResize() {
        this._resizeObserver = null;
        this._boundHandlers.resize = () => this.forceResize();

        window.addEventListener('resize', this._boundHandlers.resize);
        setTimeout(() => this.forceResize(), 50);
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

        // Re-observe new parent (debounced to avoid loops)
        if (window.ResizeObserver) {
            if (this._resizeObserver) this._resizeObserver.disconnect();
            let resizeTimeout = null;
            this._resizeObserver = new ResizeObserver(() => {
                clearTimeout(resizeTimeout);
                resizeTimeout = setTimeout(() => this.forceResize(), 100);
            });
            this._resizeObserver.observe(parent);
        }

        this.markDirty();
    }

    // ─── Store Listeners ─────────────────────────────────────────

    _setupStoreListeners() {
        this._boundHandlers.storeChanged = () => this.markDirty();
        this._boundHandlers.storeActiveChanged = () => {
            this.selectedBand = -1;
            this.markDirty();
        };

        store.on('eq:changed', this._boundHandlers.storeChanged);
        store.on('eq:active-changed', this._boundHandlers.storeActiveChanged);
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
        }

        store.off('eq:changed', this._boundHandlers.storeChanged);
        store.off('eq:active-changed', this._boundHandlers.storeActiveChanged);
    }
}