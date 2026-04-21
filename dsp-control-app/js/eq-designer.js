/**
 * @file eq-designer.js
 * @brief Biquad frequency response computation — EXACT MATCH to firmware biquad.cpp
 *
 * Uses the SAME formulas (Robert Bristow-Johnson's Audio EQ Cookbook) and
 * SAME parameter definitions as the ESP32 firmware. This ensures the graph
 * displays exactly what the DSP hardware produces.
 *
 * Filter types match firmware EQFilterType enum:
 *   0=Peaking, 1=Low Shelf, 2=High Shelf, 3=Low Pass,
 *   4=High Pass, 5=Band Pass, 6=Notch
 */

const TWO_PI = 2 * Math.PI;

/**
 * Compute biquad coefficients (a0-normalized) from audio parameters.
 * Returns { b0, b1, b2, a1, a2 } (float, normalized by a0).
 *
 * MATCHES firmware biquad.cpp::design() EXACTLY.
 */
export function designBiquad(type, f0, Q, gainDb, fs) {
    const A = Math.pow(10, gainDb / 40);  // sqrt(10^(dB/20))
    const w0 = TWO_PI * f0 / fs;
    const cosW0 = Math.cos(w0);
    const sinW0 = Math.sin(w0);

    let b0 = 1, b1 = 0, b2 = 0;
    let a0 = 1, a1 = 0, a2 = 0;

    if (Q < 0.001) Q = 0.001;
    let alpha;

    switch (type) {
        case 0: // Peaking
            alpha = sinW0 / (2 * Q);
            b0 = 1 + alpha * A;
            b1 = -2 * cosW0;
            b2 = 1 - alpha * A;
            a0 = 1 + alpha / A;
            a1 = -2 * cosW0;
            a2 = 1 - alpha / A;
            break;

        case 1: { // Low Shelf
            alpha = sinW0 / 2 * Math.sqrt((A + 1 / A) * (1 / Q - 1) + 2);
            const sqrtA = Math.sqrt(A);
            const twoSqrtAAlpha = 2 * sqrtA * alpha;
            b0 = A * ((A + 1) - (A - 1) * cosW0 + twoSqrtAAlpha);
            b1 = 2 * A * ((A - 1) - (A + 1) * cosW0);
            b2 = A * ((A + 1) - (A - 1) * cosW0 - twoSqrtAAlpha);
            a0 = (A + 1) + (A - 1) * cosW0 + twoSqrtAAlpha;
            a1 = -2 * ((A - 1) + (A + 1) * cosW0);
            a2 = (A + 1) + (A - 1) * cosW0 - twoSqrtAAlpha;
            break;
        }

        case 2: { // High Shelf
            alpha = sinW0 / 2 * Math.sqrt((A + 1 / A) * (1 / Q - 1) + 2);
            const sqrtA = Math.sqrt(A);
            const twoSqrtAAlpha = 2 * sqrtA * alpha;
            b0 = A * ((A + 1) + (A - 1) * cosW0 + twoSqrtAAlpha);
            b1 = -2 * A * ((A - 1) + (A + 1) * cosW0);
            b2 = A * ((A + 1) + (A - 1) * cosW0 - twoSqrtAAlpha);
            a0 = (A + 1) - (A - 1) * cosW0 + twoSqrtAAlpha;
            a1 = 2 * ((A - 1) - (A + 1) * cosW0);
            a2 = (A + 1) - (A - 1) * cosW0 - twoSqrtAAlpha;
            break;
        }

        case 3: // Low Pass
            alpha = sinW0 / (2 * Q);
            b0 = (1 - cosW0) / 2;
            b1 = 1 - cosW0;
            b2 = (1 - cosW0) / 2;
            a0 = 1 + alpha;
            a1 = -2 * cosW0;
            a2 = 1 - alpha;
            break;

        case 4: // High Pass
            alpha = sinW0 / (2 * Q);
            b0 = (1 + cosW0) / 2;
            b1 = -(1 + cosW0);
            b2 = (1 + cosW0) / 2;
            a0 = 1 + alpha;
            a1 = -2 * cosW0;
            a2 = 1 - alpha;
            break;

        case 5: // Band Pass
            alpha = sinW0 / (2 * Q);
            b0 = alpha;
            b1 = 0;
            b2 = -alpha;
            a0 = 1 + alpha;
            a1 = -2 * cosW0;
            a2 = 1 - alpha;
            break;

        case 6: // Notch
            alpha = sinW0 / (2 * Q);
            b0 = 1;
            b1 = -2 * cosW0;
            b2 = 1;
            a0 = 1 + alpha;
            a1 = -2 * cosW0;
            a2 = 1 - alpha;
            break;

        default: // Passthrough
            return { b0: 1, b1: 0, b2: 0, a1: 0, a2: 0 };
    }

    // Normalize by a0
    const invA0 = 1 / a0;
    return {
        b0: b0 * invA0,
        b1: b1 * invA0,
        b2: b2 * invA0,
        a1: a1 * invA0,
        a2: a2 * invA0
    };
}

/**
 * Compute magnitude response |H(f)| of a biquad at a given frequency.
 * Returns dB value.
 *
 * H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
 * where z = e^(j*w), w = 2*pi*f/fs
 */
export function biquadMagnitudeDb(coeffs, freq, fs) {
    const w = TWO_PI * freq / fs;
    const cosW = Math.cos(w);
    const cos2W = Math.cos(2 * w);
    const sinW = Math.sin(w);
    const sin2W = Math.sin(2 * w);

    // Numerator: b0 + b1*e^(-jw) + b2*e^(-j2w)
    const numReal = coeffs.b0 + coeffs.b1 * cosW + coeffs.b2 * cos2W;
    const numImag = -(coeffs.b1 * sinW + coeffs.b2 * sin2W);

    // Denominator: 1 + a1*e^(-jw) + a2*e^(-j2w)
    const denReal = 1 + coeffs.a1 * cosW + coeffs.a2 * cos2W;
    const denImag = -(coeffs.a1 * sinW + coeffs.a2 * sin2W);

    const numMagSq = numReal * numReal + numImag * numImag;
    const denMagSq = denReal * denReal + denImag * denImag;

    if (denMagSq < 1e-20) return 0;

    return 10 * Math.log10(numMagSq / denMagSq);
}

/**
 * Compute magnitude response curve for a single biquad,
 * sampled at logarithmically-spaced frequencies.
 *
 * @param {Object} band - { freq, gain, q, type }
 * @param {number} fs - sample rate
 * @param {number} numPoints - number of curve points
 * @returns {Float64Array} dB values at each frequency point
 */
export function computeBandCurve(band, fs = 96000, numPoints = 512) {
    const coeffs = designBiquad(band.type, band.freq, band.q, band.gain, fs);
    const curve = new Float64Array(numPoints);

    for (let i = 0; i < numPoints; i++) {
        const t = i / (numPoints - 1);
        const freq = 20 * Math.pow(1000, t); // 20 Hz to 20 kHz log scale
        curve[i] = biquadMagnitudeDb(coeffs, freq, fs);
    }

    return curve;
}

/**
 * Compute combined (cascaded) magnitude response for multiple EQ bands.
 * Cascading = sum of dB responses.
 *
 * @param {Array} bands - array of { freq, gain, q, type }
 * @param {number} fs
 * @param {number} numPoints
 * @returns {Float64Array} combined dB values
 */
export function computeCombinedCurve(bands, fs = 96000, numPoints = 512, pregainDb = 0) {
    const combined = new Float64Array(numPoints);
    combined.fill(pregainDb);

    for (const band of bands) {
        if (!band.enabled) continue;
        const coeffs = designBiquad(band.type, band.freq, band.q, band.gain, fs);

        for (let i = 0; i < numPoints; i++) {
            const t = i / (numPoints - 1);
            const freq = 20 * Math.pow(1000, t);
            combined[i] += biquadMagnitudeDb(coeffs, freq, fs);
        }
    }

    return combined;
}

/**
 * Frequency at index i (log-spaced, 20Hz – 20kHz).
 */
export function freqAtIndex(i, numPoints = 512) {
    return 20 * Math.pow(1000, i / (numPoints - 1));
}

/**
 * Index for a given frequency.
 */
export function indexAtFreq(freq, numPoints = 512) {
    return (numPoints - 1) * Math.log10(freq / 20) / 3;
}
