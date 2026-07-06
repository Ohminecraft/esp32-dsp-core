/**
 * @file drc-graph.js
 * @brief DRC Compression Curve — Canvas-based graph
 *
 * Draws the input/output dB transfer function for a compressor band,
 * matching the style in the MVSilicon reference UI.
 */

export class DRCGraph {
    /**
     * @param {HTMLCanvasElement} canvas
     */
    constructor(canvas) {
        this._canvas = canvas;
        this._ctx    = canvas.getContext('2d');

        // Visual config
        this._rangeDb = 90;     // show -90 to 0 dB on both axes
        this._padding = { top: 20, right: 20, bottom: 48, left: 52 };

        this._resizeObserver = new ResizeObserver(() => this._onResize());
        this._resizeObserver.observe(canvas.parentElement);
        this._onResize();
    }

    destroy() {
        this._resizeObserver.disconnect();
    }

    _onResize() {
        const parent = this._canvas.parentElement;
        if (!parent) return;
        const w   = parent.clientWidth;
        const h   = parent.clientHeight;
        if (w < 10 || h < 10) return; // parent is hidden or collapsed

        const dpr = window.devicePixelRatio || 1;
        this._canvas.width  = w * dpr;
        this._canvas.height = h * dpr;
        this._canvas.style.width  = `${w}px`;
        this._canvas.style.height = `${h}px`;
        this._ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        this.draw();
    }

    /** @param {number} thresholdDb  e.g. -15.0 */
    /** @param {number} ratio        e.g. 4.0 (4:1) */
    draw(thresholdDb = -15, ratio = 4) {
        const ctx  = this._ctx;
        const W    = this._canvas.width  / (window.devicePixelRatio || 1);
        const H    = this._canvas.height / (window.devicePixelRatio || 1);
        if (W < 10 || H < 10) return; // skip drawing if canvas is too small or hidden

        const pad  = this._padding;
        const plotW = W - pad.left - pad.right;
        const plotH = H - pad.top  - pad.bottom;
        if (plotW <= 0 || plotH <= 0) return;

        this._thresholdDb = thresholdDb;
        this._ratio       = ratio;

        // ── Background ───────────────────────────────────────────────────
        ctx.clearRect(0, 0, W, H);
        ctx.fillStyle = '#0e1015';
        ctx.fillRect(0, 0, W, H);

        // Plot area background
        ctx.fillStyle = '#141820';
        ctx.fillRect(pad.left, pad.top, plotW, plotH);

        // ── Grid ─────────────────────────────────────────────────────────
        const dbToX = (db) => pad.left + ((db + this._rangeDb) / this._rangeDb) * plotW;
        const dbToY = (db) => pad.top  + plotH - ((db + this._rangeDb) / this._rangeDb) * plotH;

        ctx.strokeStyle = '#2a3042';
        ctx.lineWidth = 1;

        const gridSteps = [-80, -70, -60, -50, -40, -30, -20, -10, 0];
        gridSteps.forEach(db => {
            const x = dbToX(db);
            const y = dbToY(db);

            // Vertical grid
            ctx.beginPath();
            ctx.moveTo(x, pad.top);
            ctx.lineTo(x, pad.top + plotH);
            ctx.stroke();

            // Horizontal grid
            ctx.beginPath();
            ctx.moveTo(pad.left, y);
            ctx.lineTo(pad.left + plotW, y);
            ctx.stroke();

            // X axis labels
            ctx.fillStyle = '#8899bb';
            ctx.font = '10px Inter, sans-serif';
            ctx.textAlign = 'center';
            ctx.fillText(`${db}`, x, pad.top + plotH + 16);

            // Y axis labels
            ctx.textAlign = 'right';
            ctx.fillText(`${db}`, pad.left - 6, y + 3);
        });

        // ── Axes labels ──────────────────────────────────────────────────
        ctx.fillStyle = '#99aac8';
        ctx.font = 'bold 11px Inter, sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText('Input Level / dB', pad.left + plotW / 2, H - 8);

        ctx.save();
        ctx.translate(14, pad.top + plotH / 2);
        ctx.rotate(-Math.PI / 2);
        ctx.fillText('Output Level / dB', 0, 0);
        ctx.restore();

        // ── Title ────────────────────────────────────────────────────────
        ctx.fillStyle = '#ccd9ee';
        ctx.font = 'bold 12px Inter, sans-serif';
        ctx.textAlign = 'left';
        ctx.fillText('Dynamic Range Compression Curve', pad.left + 4, pad.top - 5);

        // ── 1:1 reference line (dotted white) ────────────────────────────
        ctx.strokeStyle = 'rgba(200,210,230,0.35)';
        ctx.lineWidth = 1.5;
        ctx.setLineDash([4, 4]);
        ctx.beginPath();
        ctx.moveTo(dbToX(-this._rangeDb), dbToY(-this._rangeDb));
        ctx.lineTo(dbToX(0), dbToY(0));
        ctx.stroke();
        ctx.setLineDash([]);

        // ── Threshold line (red dashed) ───────────────────────────────────
        const tx = dbToX(thresholdDb);
        const ty = dbToY(thresholdDb);

        ctx.strokeStyle = '#e84040';
        ctx.lineWidth = 1.5;
        ctx.setLineDash([5, 3]);
        // Vertical
        ctx.beginPath();
        ctx.moveTo(tx, pad.top);
        ctx.lineTo(tx, pad.top + plotH);
        ctx.stroke();
        // Horizontal
        ctx.beginPath();
        ctx.moveTo(pad.left, ty);
        ctx.lineTo(pad.left + plotW, ty);
        ctx.stroke();
        ctx.setLineDash([]);

        // ── Compression curve (bright green) ─────────────────────────────
        // Transfer function:
        //   input <= threshold: output = input (1:1)
        //   input > threshold:  output = threshold + (input - threshold) / ratio
        const transferFn = (inputDb) => {
            if (inputDb <= thresholdDb) return inputDb;
            return thresholdDb + (inputDb - thresholdDb) / ratio;
        };

        const grad = ctx.createLinearGradient(pad.left, 0, pad.left + plotW, 0);
        grad.addColorStop(0, '#1aee6a');
        grad.addColorStop(1, '#22cc55');

        ctx.strokeStyle = grad;
        ctx.lineWidth = 2.5;
        ctx.shadowColor = '#22ff66';
        ctx.shadowBlur = 6;

        ctx.beginPath();
        for (let px = 0; px <= plotW; px++) {
            const inputDb  = ((px / plotW) * this._rangeDb) - this._rangeDb;
            const outputDb = transferFn(inputDb);
            const x = pad.left + px;
            const y = dbToY(outputDb);
            if (px === 0) ctx.moveTo(x, y);
            else          ctx.lineTo(x, y);
        }
        ctx.stroke();
        ctx.shadowBlur = 0;

        // ── Legend ────────────────────────────────────────────────────────
        const legendX = pad.left + 12;
        const legendY = pad.top + plotH - 60;

        const drawLegendItem = (x, y, color, dash, label) => {
            ctx.strokeStyle = color;
            ctx.lineWidth = 1.5;
            ctx.setLineDash(dash);
            ctx.beginPath();
            ctx.moveTo(x, y + 6);
            ctx.lineTo(x + 22, y + 6);
            ctx.stroke();
            ctx.setLineDash([]);
            ctx.fillStyle = '#aabbcc';
            ctx.font = '10px Inter, sans-serif';
            ctx.textAlign = 'left';
            ctx.fillText(label, x + 28, y + 10);
        };

        drawLegendItem(legendX, legendY,      '#e84040',   [5, 3], `Threshold ${thresholdDb} dB`);
        drawLegendItem(legendX, legendY + 18, 'rgba(200,210,230,0.4)', [4, 4], '1:1');
        drawLegendItem(legendX, legendY + 36, '#22cc55',   [],      `Ratio ${ratio.toFixed(0)}:1`);

        // Save base image for live meter overlay (putImageData = no full redraw needed)
        this._baseImageData = ctx.getImageData(0, 0, this._canvas.width, this._canvas.height);
        this._dbToX = dbToX;
        this._dbToY = dbToY;
        this._plotW = plotW;
        this._plotH = plotH;
    }

    /**
     * Overlay the live operating point on the compression curve.
     * Called from renderDrcMeter() every ~500ms without redrawing the full graph.
     *
     * @param {number} gainDb   Current gain reduction in dB for active band (≤ 0)
     */
    updateLiveMeter(gainDb) {
        if (!this._baseImageData || !this._dbToX) return;

        const ctx  = this._ctx;
        const dpr  = window.devicePixelRatio || 1;
        const pad  = this._padding;
        const dbToX = this._dbToX;
        const dbToY = this._dbToY;

        // Restore the static base graph
        ctx.putImageData(this._baseImageData, 0, 0);

        const th    = this._thresholdDb ?? -15;
        const ratio = this._ratio       ?? 4;
        const slope = 1 - 1 / ratio;    // slopeAbove = (1 − 1/R)

        // Reverse the compression transfer function to get input level:
        //   gainDb = (th - inputDb) * slope   →   inputDb = th - gainDb / slope
        const isCompressing = gainDb < -0.2 && slope > 0.001;
        const inputDb  = isCompressing
            ? Math.max(-this._rangeDb, Math.min(0, th - gainDb / slope))
            : null;
        const outputDb = inputDb !== null
            ? (inputDb <= th ? inputDb : th + (inputDb - th) / ratio)
            : null;

        if (inputDb === null) return;   // no compression → nothing to show

        const ox = dbToX(inputDb);
        const oy = dbToY(outputDb);

        // ── Crosshair lines ─────────────────────────────────────────────
        ctx.save();
        ctx.strokeStyle = 'rgba(255, 204, 51, 0.55)';
        ctx.lineWidth = 1;
        ctx.setLineDash([3, 3]);

        // Vertical: x-axis → operating point
        ctx.beginPath();
        ctx.moveTo(ox, pad.top + this._plotH);
        ctx.lineTo(ox, oy);
        ctx.stroke();

        // Horizontal: y-axis → operating point
        ctx.beginPath();
        ctx.moveTo(pad.left, oy);
        ctx.lineTo(ox, oy);
        ctx.stroke();

        ctx.setLineDash([]);

        // ── GR bar on right edge ────────────────────────────────────────
        // Full bar height = plotH, fill from top proportional to |gainDb|/rangeDb
        const grBarX  = pad.left + this._plotW + 4;
        const grBarW  = 5;
        const grFill  = Math.min(1, Math.abs(gainDb) / this._rangeDb) * this._plotH;
        ctx.fillStyle = 'rgba(255,204,51,0.18)';
        ctx.fillRect(grBarX, pad.top, grBarW, this._plotH);
        ctx.fillStyle = '#ffcc33';
        ctx.fillRect(grBarX, pad.top, grBarW, grFill);

        // ── Operating point dot ─────────────────────────────────────────
        ctx.shadowColor = '#ffcc33';
        ctx.shadowBlur  = 10;
        ctx.fillStyle   = '#ffcc33';
        ctx.beginPath();
        ctx.arc(ox, oy, 5, 0, Math.PI * 2);
        ctx.fill();
        ctx.shadowBlur = 0;

        // ── GR label near dot ───────────────────────────────────────────
        ctx.fillStyle  = '#ffcc33';
        ctx.font       = 'bold 10px Inter, sans-serif';
        ctx.textAlign  = ox > pad.left + this._plotW * 0.75 ? 'right' : 'left';
        ctx.fillText(`GR ${gainDb} dB`, ox + (ctx.textAlign === 'left' ? 8 : -8), oy - 7);

        ctx.restore();
    }
}