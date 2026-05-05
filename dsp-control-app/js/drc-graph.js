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
        const dpr = window.devicePixelRatio || 1;
        const w   = parent.clientWidth;
        const h   = parent.clientHeight;
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
        const pad  = this._padding;
        const plotW = W - pad.left - pad.right;
        const plotH = H - pad.top  - pad.bottom;

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

        drawLegendItem(legendX, legendY,      '#e84040',   [5, 3], `Threshold ${thresholdDb.toFixed(1)} dB`);
        drawLegendItem(legendX, legendY + 18, 'rgba(200,210,230,0.4)', [4, 4], '1:1');
        drawLegendItem(legendX, legendY + 36, '#22cc55',   [],      `Ratio ${ratio.toFixed(0)}:1`);
    }
}
