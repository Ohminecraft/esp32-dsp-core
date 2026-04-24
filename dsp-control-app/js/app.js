/**
 * @file app.js
 * @brief Main application — accordion module UI with auto-connect
 */

import { store } from './store.js';
import {
    MODULE, MODULE_NAMES, MODULE_ORDER, CMD, FrameParser,
    buildFrame, buildEnableModule, buildDisableModule,
    buildSetParam, buildSetEqBand, buildSetDynEqBand,
    buildSetDynEqThresholds, buildSavePreset, buildLoadPreset,
    buildGetSystemInfo,
    buildGetAllState, dbToQ88, dbToQ31, qToQ610,
    leToInt16, leToInt32
} from './protocol.js';
import { EQGraph } from './eq-graph.js';

let eqGraph = null;
let parser = new FrameParser();
let sendDebounce = {};
let autoScanInterval = null;
let manualMode = false;
let isFetchingState = false;
let isProbing = false;       // true while a single port probe is in-flight
let isScanning = false;      // true while tryConnect loop is running (re-entrancy guard)
let probeResolver = null;
let probeAborted = false;    // set true to cancel an in-flight probe immediately

// Ports that failed recently — skip until cooldown expires
// Map<portPath, timestamp_ms>
const failedPortCooldown = new Map();
const PORT_COOLDOWN_MS = 12000;  // access-denied ports skip for 12 s
const NO_RESP_COOLDOWN_MS = 6000; // no-response ports skip for 6 s

// ─── Serial ──────────────────────────────────────────────────────────

async function refreshPorts() {
    const ports = await window.serialAPI.listPorts();
    ['port-select', 'lobby-port-select'].forEach(id => {
        const select = document.getElementById(id);
        if (!select) return;
        select.innerHTML = '<option value="">Select Port...</option>';
        ports.forEach(p => {
            const opt = document.createElement('option');
            opt.value = p.path;
            const desc = p.manufacturer ? `(${p.manufacturer})` : '(Unknown)';
            opt.textContent = `${p.path} ${desc}`;
            select.appendChild(opt);
        });
    });
    return ports;
}

async function connectSerial() {
    const port = document.getElementById('port-select').value;
    if (!port) return;
    await abortScanAndWait();   // release any port held by probe before connecting
    try {
        await window.serialAPI.connect(port, 115200);
        onConnected(port);
    } catch (e) { showStatus(`Error: ${e}`, 'error'); }
}

function onConnected(port) {
    store.setConnected(true, port);
    stopAutoScan();
    hideScanOverlay();
    updateStatusUI();
    isFetchingState = true;
    sendFrame(buildGetAllState());
    showStatus('Connected — Fetching DSP State...', 'ok');
}

async function disconnectSerial() {
    await window.serialAPI.disconnect();
    store.setConnected(false);
    updateStatusUI();
}

/** Stop the scan interval and abort any in-flight probe, waiting until the port is released. */
async function abortScanAndWait() {
    stopAutoScan();
    probeAborted = true;
    if (probeResolver) { probeResolver(false); probeResolver = null; }
    // Poll until probePort() fully exits and releases the serial port
    const deadline = Date.now() + 2000;
    while (isProbing && Date.now() < deadline) {
        await new Promise(r => setTimeout(r, 50));
    }
    probeAborted = false;
}

async function sendFrame(frameArray) {
    if (!store.system.connected) return;
    try { await window.serialAPI.send(frameArray); }
    catch (e) { console.error('Send error:', e); }
}

function sendDebounced(key, frameFn, delay = 16) {
    clearTimeout(sendDebounce[key]);
    sendDebounce[key] = setTimeout(() => sendFrame(frameFn()), delay);
}

// ─── Auto-Scan ───────────────────────────────────────────────────────

async function probePort(path, portsLabel) {
    isProbing = true;
    probeAborted = false;
    portsLabel.textContent = `Probing ${path}...`;
    try {
        await window.serialAPI.connect(path, 115200);

        if (probeAborted) { await window.serialAPI.disconnect(); return false; }

        // Short settle then send probe packet
        await new Promise(r => setTimeout(r, 150));
        await window.serialAPI.send(buildGetSystemInfo());

        // Wait up to 800 ms for any valid frame back
        const success = await new Promise(resolve => {
            probeResolver = resolve;
            setTimeout(() => {
                if (probeResolver === resolve) { probeResolver = null; resolve(false); }
            }, 800);
        });

        if (!success || probeAborted) {
            await window.serialAPI.disconnect();
            // No-response: shorter cooldown so we retry sooner than access-denied ports
            if (!failedPortCooldown.has(path)) {
                failedPortCooldown.set(path, Date.now() - (PORT_COOLDOWN_MS - NO_RESP_COOLDOWN_MS));
            }
            return false;
        }
        return true;
    } catch (e) {
        // Access denied or port busy — add full cooldown
        if (/access denied|busy/i.test(e?.message || '')) {
            failedPortCooldown.set(path, Date.now());
        }
        try { await window.serialAPI.disconnect(); } catch (_) { }
        return false;
    } finally {
        isProbing = false;
    }
}

async function startAutoScan() {
    document.getElementById('scan-overlay').classList.remove('hidden');
    const portsLabel = document.getElementById('scan-ports-list');

    const tryConnect = async () => {
        // Re-entrancy guard: only one scan loop at a time
        if (store.system.connected || manualMode || isScanning) return;
        isScanning = true;
        try {
            const ports = await window.serialAPI.listPorts();
            if (!ports.length) {
                portsLabel.textContent = 'No serial ports found...';
                return;
            }

            // Purge expired cooldowns
            const now = Date.now();
            for (const [p, t] of failedPortCooldown) {
                if (now - t > PORT_COOLDOWN_MS) failedPortCooldown.delete(p);
            }

            const candidates = ports.filter(p => !failedPortCooldown.has(p.path));
            if (!candidates.length) {
                const waitSec = Math.ceil(NO_RESP_COOLDOWN_MS / 1000);
                portsLabel.textContent = `All ports cooling down — retrying in ~${waitSec}s...`;
                return;
            }

            for (const p of candidates) {
                if (store.system.connected || manualMode || probeAborted) return;
                const found = await probePort(p.path, portsLabel);
                if (found) { onConnected(p.path); return; }
            }

            portsLabel.textContent = `Scanned ${ports.length} port(s) — no DSP response. Retrying...`;
        } catch (e) {
            console.error('Scan error:', e);
            portsLabel.textContent = `Scan error: ${e.message || e}`;
        } finally {
            isScanning = false;
        }
    };

    stopAutoScan();
    await tryConnect();
    if (!store.system.connected && !manualMode) {
        autoScanInterval = setInterval(tryConnect, 5000);
    }
}

function stopAutoScan() { if (autoScanInterval) { clearInterval(autoScanInterval); autoScanInterval = null; } }
function hideScanOverlay() { document.getElementById('scan-overlay').classList.add('hidden'); }

async function enterManualLobby() {
    manualMode = true;
    await abortScanAndWait();   // wait for probe to release port before user connects
    document.getElementById('scan-overlay').classList.remove('hidden');
    document.getElementById('scan-auto-content').style.display = 'none';
    document.getElementById('scan-manual-content').style.display = 'block';
    refreshPorts();
}

function showManualConnect() {
    enterManualLobby();
    // Also ensure top bar controls are visible for later
    ['port-select', 'btn-refresh', 'btn-connect'].forEach(id => document.getElementById(id).style.display = '');
}

// ─── Response ────────────────────────────────────────────────────────

const readInt16 = (d, o) => { const v = d[o] | (d[o + 1] << 8); return v >= 32768 ? v - 65536 : v; };
const readInt32 = (d, o) => { const v = d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24); return v; };

parser.onFrame((frame) => {
    if (isProbing && probeResolver) {
        probeResolver(true);
        probeResolver = null;
        return;
    }

    if (frame.cmd === CMD.ACK_RESPONSE && frame.data[0] === 0) {
        if (isFetchingState) {
            isFetchingState = false;
            
            // Rebuild the entire parameter UI
            buildAccordionModules();
            buildBottomBar();
            
            // Force Graph to redraw everything
            store.emit('eq:changed');
            store.emit('state:loaded');
            store.emit('eq:structure-changed');
            
            showStatus('State synchronized successfully!', 'ok');
        }
    }
    else if (frame.cmd === CMD.ERROR) showStatus(`Error: 0x${frame.data[0]?.toString(16)}`, 'error');
    else if (frame.cmd === CMD.ENABLE_MODULE) store.setModuleEnabled(frame.moduleId, true);
    else if (frame.cmd === CMD.DISABLE_MODULE) store.setModuleEnabled(frame.moduleId, false);
    else if (frame.cmd === CMD.SET_PARAM && frame.data.length >= 5) {
        const pIndex = frame.data[0];
        const val = readInt32(frame.data, 1);

        switch (frame.moduleId) {
            case MODULE.VOLUME: if (pIndex === 0) store.updateParam('volume', 'gainDb', val); break;
            case MODULE.NOISE_GATE:
                if (pIndex === 0) store.updateParam('noiseGate', 'lowerThresh', val);
                else if (pIndex === 1) store.updateParam('noiseGate', 'upperThresh', val);
                else if (pIndex === 2) store.updateParam('noiseGate', 'attackMs', val);
                else if (pIndex === 3) store.updateParam('noiseGate', 'releaseMs', val);
                else if (pIndex === 4) store.updateParam('noiseGate', 'holdMs', val);
                break;
            case MODULE.COMPANDER:
                if (pIndex === 0) store.updateParam('compander', 'threshold', val);
                else if (pIndex === 1) store.updateParam('compander', 'ratioBelow', val);
                else if (pIndex === 2) store.updateParam('compander', 'ratioAbove', val);
                else if (pIndex === 3) store.updateParam('compander', 'attackMs', val);
                else if (pIndex === 4) store.updateParam('compander', 'releaseMs', val);
                break;
            case MODULE.EXCITER:
                if (pIndex === 0) store.updateParam('exciter', 'cutoffFreq', val);
                else if (pIndex === 1) store.updateParam('exciter', 'dry', val);
                else if (pIndex === 2) store.updateParam('exciter', 'wet', val);
                break;
            case MODULE.VIRTUAL_BASS:
                if (pIndex === 0) store.updateParam('virtualBass', 'cutoffFreq', val);
                else if (pIndex === 1) store.updateParam('virtualBass', 'intensity', val);
                else if (pIndex === 2) store.updateParam('virtualBass', 'enhanced', val);
                break;
            case MODULE.BASS_CLASSIC:
                if (pIndex === 0) store.updateParam('bassClassic', 'cutoffFreq', val);
                else if (pIndex === 1) store.updateParam('bassClassic', 'intensity', val);
                break;
            case MODULE.STEREO_WIDEN:
                if (pIndex === 0) store.updateParam('stereoWidener', 'intensity', val);
                break;
            case MODULE.DRC: {
                // pIndex encodes band in upper nibble, param in lower nibble
                // handleGetAllState sends pIndex 0-3 for band 0 (no nibble encoding)
                const drcBandIdx = (pIndex >> 4) & 0x0F;
                const drcParam = pIndex & 0x0F;
                const drcBand = store.drc.bands[drcBandIdx];
                if (drcBand) {
                    if (drcParam === 0) drcBand.threshold = val;
                    else if (drcParam === 1) drcBand.ratio = val;
                    else if (drcParam === 2) drcBand.attackMs = val;
                    else if (drcParam === 3) drcBand.releaseMs = val;
                    else if (drcParam === 4) drcBand.pregain = val;
                }
                break;
            }
            case MODULE.SOFT_CLIP:
                if (pIndex === 0) {
                    const db = 20 * Math.log10(val / 2147483647);
                    store.updateParam('softClipper', 'threshold', Math.round(db * 100));
                }
                break;
        }
    }
    else if (frame.cmd === CMD.SET_EQ_BAND && frame.data.length >= 11) {
        const d = frame.data;
        const pregain = leToInt16(d, 0);
        const b = d[2];
        const gain = leToInt16(d, 7);
        const qVal = leToInt16(d, 9);
        const changes = { enabled: d[3] === 1, type: d[4], freq: d[5] | (d[6] << 8), gain: gain / 256, q: qVal / 1024 };
        const eqState = frame.moduleId === MODULE.EQ_DSP_1 ? store.eq1 : store.eq2;
        eqState.pregain = pregain / 256;
        Object.assign(eqState.bands[b], changes);
        store.emit('eq:changed');
    }
    else if ((frame.cmd === CMD.SET_DYNEQ_LOW_BAND || frame.cmd === CMD.SET_DYNEQ_HIGH_BAND) && frame.data.length >= 11) {
        const d = frame.data;
        const pregain = leToInt16(d, 0);
        const b = d[2];
        const gain = leToInt16(d, 7);
        const qVal = leToInt16(d, 9);
        const changes = { enabled: d[3] === 1, type: d[4], freq: d[5] | (d[6] << 8), gain: gain / 256, q: qVal / 1024 };
        const eqTarget = frame.cmd === CMD.SET_DYNEQ_HIGH_BAND ? store.dynamicEq.eqHigh : store.dynamicEq.eqLow;
        eqTarget.pregain = pregain / 256;
        Object.assign(eqTarget.bands[b], changes);
        store.emit('eq:changed');
    }
    else if (frame.cmd === CMD.SET_DYNEQ_THRESH && frame.data.length >= 20) {
        store.updateParam('dynamicEq', 'lowThresh', leToInt32(frame.data, 0));
        store.updateParam('dynamicEq', 'normalThresh', leToInt32(frame.data, 4));
        store.updateParam('dynamicEq', 'highThresh', leToInt32(frame.data, 8));
        store.updateParam('dynamicEq', 'attackMs', leToInt32(frame.data, 12));
        store.updateParam('dynamicEq', 'releaseMs', leToInt32(frame.data, 16));
    }
    else if (frame.cmd === CMD.REPORT_CPU_USAGE && frame.data.length >= 3) {
        const cpu10 = frame.data[0] | (frame.data[1] << 8);
        const heapPct = frame.data[2];
        updateCpuUI(cpu10 / 10.0, 100 - heapPct);
    }
});

// ─── Accordion Builder ───────────────────────────────────────────────

// Extended module list: split Dynamic EQ into DynEQ Thresh, DynEQ Low, DynEQ High
const ACCORDION_MODULES = [
    { id: MODULE.NOISE_GATE, name: 'Noise Gate', icon: '🔇' },
    { id: MODULE.COMPANDER, name: 'Compander', icon: '📊' },
    { id: MODULE.EXCITER, name: 'Exciter', icon: '✨' },
    { id: MODULE.VIRTUAL_BASS, name: 'Virtual Bass', icon: '🔊' },
    { id: MODULE.BASS_CLASSIC, name: 'Bass Classic', icon: '🎵' },
    { id: MODULE.STEREO_WIDEN, name: 'Stereo Widener', icon: '🎧' },
    { id: 'DYNEQ_THRESH', name: 'Dynamic EQ — Thresholds', icon: '⚡', parentId: MODULE.DYNAMIC_EQ },
    { id: 'DYNEQ_LOW', name: 'Dynamic EQ — Low', icon: '🔉', parentId: MODULE.DYNAMIC_EQ },
    { id: 'DYNEQ_HIGH', name: 'Dynamic EQ — High', icon: '🔊', parentId: MODULE.DYNAMIC_EQ },
    { id: MODULE.EQ_DSP_1, name: 'Parmetric EQ 1', icon: '📈' },
    { id: MODULE.EQ_DSP_2, name: 'Parmetric EQ 2', icon: '📉' },
    { id: MODULE.DRC, name: 'Dynamic Range Compression', icon: '🛡️' },
    { id: MODULE.VOLUME, name: 'Volume', icon: '🔈' },
    { id: MODULE.SOFT_CLIP, name: 'Soft Clipper', icon: '🔧' },
];

function buildAccordionModules() {
    const container = document.getElementById('modules-list');
    
    // Remember which accordion was open
    const openModuleId = document.querySelector('.accordion.open')?.dataset.moduleId;
    
    container.innerHTML = '';

    ACCORDION_MODULES.forEach(mod => {
        const acc = document.createElement('div');
        acc.className = 'accordion';
        acc.dataset.moduleId = mod.id;

        // Header
        const header = document.createElement('div');
        header.className = 'accordion-header';

        const toggle = document.createElement('input');
        toggle.type = 'checkbox';
        toggle.className = 'acc-toggle';
        const realModId = mod.parentId || mod.id;
        if (typeof realModId === 'number') {
            toggle.checked = store.modules[realModId]?.enabled || false;
            toggle.addEventListener('change', (e) => {
                e.stopPropagation();
                store.setModuleEnabled(realModId, toggle.checked);
                sendFrame(toggle.checked ? buildEnableModule(realModId) : buildDisableModule(realModId));
                // Sync all sibling toggles that share the same module ID
                document.querySelectorAll('.accordion').forEach(acc => {
                    const accMod = ACCORDION_MODULES.find(m => String(m.id) === acc.dataset.moduleId);
                    if (accMod && (accMod.parentId === realModId || accMod.id === realModId)) {
                        const sibToggle = acc.querySelector('.acc-toggle');
                        if (sibToggle && sibToggle !== toggle) sibToggle.checked = toggle.checked;
                    }
                });
            });
        }

        const title = document.createElement('span');
        title.className = 'acc-title';
        title.textContent = `${mod.icon}  ${mod.name}`;

        const chevron = document.createElement('span');
        chevron.className = 'acc-chevron';
        chevron.textContent = '▼';

        header.appendChild(toggle);
        header.appendChild(title);
        header.appendChild(chevron);

        const EQ_MODULE_IDS = [String(MODULE.EQ_DSP_1), String(MODULE.EQ_DSP_2), 'DYNEQ_LOW', 'DYNEQ_HIGH'];
        const isEqModule = EQ_MODULE_IDS.includes(String(mod.id));

        header.addEventListener('click', (e) => {
            if (e.target === toggle) return;

            if (isEqModule) {
                const wasOpen = acc.classList.contains('open');

                if (wasOpen) {
                    // Close this EQ accordion
                    acc.classList.remove('open');
                    unmountGraph();
                } else {
                    // Close ALL other EQ accordions first
                    document.querySelectorAll('.accordion').forEach(other => {
                        if (other !== acc && EQ_MODULE_IDS.includes(other.dataset.moduleId)) {
                            other.classList.remove('open');
                        }
                    });

                    // Open this one
                    acc.classList.add('open');

                    // Set active EQ
                    if (mod.id === MODULE.EQ_DSP_1) store.setActiveEq('eq1');
                    else if (mod.id === MODULE.EQ_DSP_2) store.setActiveEq('eq2');
                    else if (mod.id === 'DYNEQ_LOW') store.setActiveEq('dynLow');
                    else if (mod.id === 'DYNEQ_HIGH') store.setActiveEq('dynHigh');

                    // Mount graph
                    mountGraphToAccordion(acc);
                }
            } else {
                // Non-EQ module: simple toggle
                acc.classList.toggle('open');
            }
        });

        // Body
        const body = document.createElement('div');
        body.className = 'accordion-body';
        buildModuleBody(body, mod);

        acc.appendChild(header);
        acc.appendChild(body);
        container.appendChild(acc);

        // Restore open state
        if (mod.id === openModuleId || (mod.id === Number(openModuleId))) {
            acc.classList.add('open');
            // If it was an EQ module, we need to remount the graph
            if (isEqModule) {
                setTimeout(() => mountGraphToAccordion(acc), 0);
            }
        }
    });
}

function buildModuleBody(body, mod) {
    switch (mod.id) {
        case MODULE.NOISE_GATE:
            addSlider(body, 'Lower Thresh', -9000, 0, 100, 'dB',
                () => store.noiseGate.lowerThresh,
                (v) => { store.noiseGate.lowerThresh = v; sendFrame(buildSetParam(MODULE.NOISE_GATE, 0, v)); },
                null, 0.01);
            addSlider(body, 'Upper Thresh', -9000, 0, 100, 'dB',
                () => store.noiseGate.upperThresh,
                (v) => { store.noiseGate.upperThresh = v; sendFrame(buildSetParam(MODULE.NOISE_GATE, 1, v)); },
                null, 0.01);
            addSlider(body, 'Attack', 1, 100, 1, 'ms',
                () => store.noiseGate.attackMs,
                (v) => { store.noiseGate.attackMs = v; sendFrame(buildSetParam(MODULE.NOISE_GATE, 2, v)); });
            addSlider(body, 'Release', 10, 500, 1, 'ms',
                () => store.noiseGate.releaseMs,
                (v) => { store.noiseGate.releaseMs = v; sendFrame(buildSetParam(MODULE.NOISE_GATE, 3, v)); });
            addSlider(body, 'Hold', 10, 500, 1, 'ms',
                () => store.noiseGate.holdMs,
                (v) => { store.noiseGate.holdMs = v; sendFrame(buildSetParam(MODULE.NOISE_GATE, 4, v)); });
            break;

        case MODULE.COMPANDER:
            addSlider(body, 'Threshold', -9000, 0, 100, 'dB',
                () => store.compander.threshold,
                (v) => { store.compander.threshold = v; sendFrame(buildSetParam(MODULE.COMPANDER, 0, v)); },
                null, 0.01);
            addSlider(body, 'Ratio Below', 10, 1000, 10, '',
                () => store.compander.ratioBelow,
                (v) => { store.compander.ratioBelow = v; sendFrame(buildSetParam(MODULE.COMPANDER, 1, v)); },
                null, 0.01);
            addSlider(body, 'Ratio Above', 10, 1000, 10, '',
                () => store.compander.ratioAbove,
                (v) => { store.compander.ratioAbove = v; sendFrame(buildSetParam(MODULE.COMPANDER, 2, v)); },
                null, 0.01);
            addSlider(body, 'Attack', 1, 200, 1, 'ms',
                () => store.compander.attackMs,
                (v) => { store.compander.attackMs = v; sendFrame(buildSetParam(MODULE.COMPANDER, 3, v)); });
            addSlider(body, 'Release', 10, 1000, 1, 'ms',
                () => store.compander.releaseMs,
                (v) => { store.compander.releaseMs = v; sendFrame(buildSetParam(MODULE.COMPANDER, 4, v)); });
            break;

        case MODULE.EXCITER:
            addSlider(body, 'Cutoff Freq', 300, 10000, 100, 'Hz',
                () => store.exciter.cutoffFreq,
                (v) => { store.exciter.cutoffFreq = v; sendFrame(buildSetParam(MODULE.EXCITER, 0, v)); });
            addSlider(body, 'Dry', 0, 100, 1, '%',
                () => store.exciter.dry,
                (v) => { store.exciter.dry = v; sendFrame(buildSetParam(MODULE.EXCITER, 1, v)); });
            addSlider(body, 'Wet', 0, 100, 1, '%',
                () => store.exciter.wet,
                (v) => { store.exciter.wet = v; sendFrame(buildSetParam(MODULE.EXCITER, 2, v)); });
            break;

        case MODULE.VIRTUAL_BASS:
            addSlider(body, 'Cutoff Freq', 30, 300, 5, 'Hz',
                () => store.virtualBass.cutoffFreq,
                (v) => { store.virtualBass.cutoffFreq = v; sendFrame(buildSetParam(MODULE.VIRTUAL_BASS, 0, v)); });
            addSlider(body, 'Intensity', 0, 100, 1, '%',
                () => store.virtualBass.intensity,
                (v) => { store.virtualBass.intensity = v; sendFrame(buildSetParam(MODULE.VIRTUAL_BASS, 1, v)); });
            addSwitch(body, 'Enhanced',
                () => store.virtualBass.enhanced > 0,
                (v) => { store.virtualBass.enhanced = v ? 1 : 0; sendFrame(buildSetParam(MODULE.VIRTUAL_BASS, 2, v ? 1 : 0)); });
            break;

        case MODULE.BASS_CLASSIC:
            addSlider(body, 'Cutoff Freq', 30, 300, 5, 'Hz',
                () => store.bassClassic.cutoffFreq,
                (v) => { store.bassClassic.cutoffFreq = v; sendFrame(buildSetParam(MODULE.BASS_CLASSIC, 0, v)); });
            addSlider(body, 'Intensity', 0, 100, 1, '%',
                () => store.bassClassic.intensity,
                (v) => { store.bassClassic.intensity = v; sendFrame(buildSetParam(MODULE.BASS_CLASSIC, 1, v)); });
            break;

        case MODULE.STEREO_WIDEN:
            addSlider(body, 'Intensity', 0, 100, 1, '%',
                () => store.stereoWidener.intensity,
                (v) => { store.stereoWidener.intensity = v; sendFrame(buildSetParam(MODULE.STEREO_WIDEN, 0, v)); });
            break;

        case MODULE.EQ_DSP_1:
            buildEqBandPanel(body, MODULE.EQ_DSP_1, 'eq1');
            break;

        case MODULE.EQ_DSP_2:
            buildEqBandPanel(body, MODULE.EQ_DSP_2, 'eq2');
            break;

        case 'DYNEQ_THRESH': {
            const dbFmt = (v) => `${(v / 100).toFixed(2)} dB`;

            // Build sliders and wire cross-constraints:
            //   highThresh >= normalThresh >= lowThresh
            const { slider: slLow, valInput: vsLow } =
                addSlider(body, 'Low Thresh', -6000, 0, 100, 'dB',
                    () => store.dynamicEq.lowThresh,
                    (v) => { store.dynamicEq.lowThresh = v; sendFrame(buildSetDynEqThresholds(v, store.dynamicEq.normalThresh, store.dynamicEq.highThresh, store.dynamicEq.attackMs, store.dynamicEq.releaseMs)); },
                    null, 0.01);

            const { slider: slNorm, valInput: vsNorm } =
                addSlider(body, 'Normal Thresh', -6000, 0, 100, 'dB',
                    () => store.dynamicEq.normalThresh,
                    (v) => { store.dynamicEq.normalThresh = v; sendFrame(buildSetDynEqThresholds(store.dynamicEq.lowThresh, v, store.dynamicEq.highThresh, store.dynamicEq.attackMs, store.dynamicEq.releaseMs)); },
                    null, 0.01);

            const { slider: slHigh, valInput: vsHigh } =
                addSlider(body, 'High Thresh', -6000, 0, 100, 'dB',
                    () => store.dynamicEq.highThresh,
                    (v) => { store.dynamicEq.highThresh = v; sendFrame(buildSetDynEqThresholds(store.dynamicEq.lowThresh, store.dynamicEq.normalThresh, v, store.dynamicEq.attackMs, store.dynamicEq.releaseMs)); },
                    null, 0.01);

            // Constraint enforcement (runs after addSlider's own listener)
            slLow.addEventListener('input', () => {
                const v = parseFloat(slLow.value);
                // low must not exceed normal
                if (v > parseFloat(slNorm.value)) {
                    slNorm.value = v;
                    store.dynamicEq.normalThresh = v;
                    vsNorm.value = (v * 0.01).toFixed(2);
                    // normal raising might also need to push high up
                    if (v > parseFloat(slHigh.value)) {
                        slHigh.value = v;
                        store.dynamicEq.highThresh = v;
                        vsHigh.value = (v * 0.01).toFixed(2);
                    }
                }
            });

            slNorm.addEventListener('input', () => {
                const v = parseFloat(slNorm.value);
                // clamp low ≤ normal
                if (parseFloat(slLow.value) > v) {
                    slLow.value = v;
                    store.dynamicEq.lowThresh = v;
                    vsLow.value = (v * 0.01).toFixed(2);
                }
                // clamp high ≥ normal
                if (parseFloat(slHigh.value) < v) {
                    slHigh.value = v;
                    store.dynamicEq.highThresh = v;
                    vsHigh.value = (v * 0.01).toFixed(2);
                }
            });

            slHigh.addEventListener('input', () => {
                const v = parseFloat(slHigh.value);
                // high must not be below normal
                if (v < parseFloat(slNorm.value)) {
                    slNorm.value = v;
                    store.dynamicEq.normalThresh = v;
                    vsNorm.value = (v * 0.01).toFixed(2);
                    // normal dropping might also need to push low down
                    if (v < parseFloat(slLow.value)) {
                        slLow.value = v;
                        store.dynamicEq.lowThresh = v;
                        vsLow.value = (v * 0.01).toFixed(2);
                    }
                }
            });

            addSlider(body, 'Attack', 1, 500, 1, 'ms',
                () => store.dynamicEq.attackMs,
                (v) => { store.dynamicEq.attackMs = v; sendFrame(buildSetDynEqThresholds(store.dynamicEq.lowThresh, store.dynamicEq.normalThresh, store.dynamicEq.highThresh, v, store.dynamicEq.releaseMs)); });
            addSlider(body, 'Release', 10, 2000, 1, 'ms',
                () => store.dynamicEq.releaseMs,
                (v) => { store.dynamicEq.releaseMs = v; sendFrame(buildSetDynEqThresholds(store.dynamicEq.lowThresh, store.dynamicEq.normalThresh, store.dynamicEq.highThresh, store.dynamicEq.attackMs, v)); });

            /*
            const syncBtn = document.createElement('button');
            syncBtn.textContent = 'Sync to Hardware';
            syncBtn.className = 'btn btn-primary btn-sm';
            syncBtn.style.marginTop = '8px';
            syncBtn.addEventListener('click', () => {
                const d = store.dynamicEq;
                sendFrame(buildSetDynEqThresholds(d.lowThresh, d.normalThresh, d.highThresh, d.attackMs, d.releaseMs));
            });
            body.appendChild(syncBtn);
            */
            break;
        }

        case 'DYNEQ_LOW':
            buildDynEqBandPanel(body, false);
            break;

        case 'DYNEQ_HIGH':
            buildDynEqBandPanel(body, true);
            break;

        case MODULE.DRC:
            addSlider(body, 'Threshold', -9000, 0, 100, 'dB',
                () => store.drc.bands[3].threshold,
                (v) => { store.drc.bands[3].threshold = v; sendFrame(buildSetParam(MODULE.DRC, 0x30, v)); },
                null, 0.01);
            addSlider(body, 'Ratio', 100, 1000, 10, ':1',
                () => store.drc.bands[3].ratio,
                (v) => { store.drc.bands[3].ratio = v; sendFrame(buildSetParam(MODULE.DRC, 0x31, v)); },
                null, 0.01);
            addSlider(body, 'Attack', 1, 200, 1, 'ms',
                () => store.drc.bands[3].attackMs,
                (v) => { store.drc.bands[3].attackMs = v; sendFrame(buildSetParam(MODULE.DRC, 0x32, v)); });
            addSlider(body, 'Release', 10, 1000, 1, 'ms',
                () => store.drc.bands[3].releaseMs,
                (v) => { store.drc.bands[3].releaseMs = v; sendFrame(buildSetParam(MODULE.DRC, 0x33, v)); });
            break;

        case MODULE.VOLUME:
            addSlider(body, 'Gain', -9600, 1800, 25, 'dB',
                () => store.volume.gainDb,
                (v) => { store.volume.gainDb = v; sendFrame(buildSetParam(MODULE.VOLUME, 0, v)); },
                null, 0.01);
            break;

        case MODULE.SOFT_CLIP:
            addSlider(body, 'Threshold', -6000, 0, 10, 'dB',
                () => store.softClipper.threshold,
                (v) => {
                    store.softClipper.threshold = v;
                    sendFrame(buildSetParam(MODULE.SOFT_CLIP, 0, dbToQ31(v / 100)));
                }, null, 0.01);
            break;
    }
}

// ─── EQ Band Panel ───────────────────────────────────────────────────

function buildEqBandPanel(container, moduleId, eqKey) {
    const eq = store[eqKey];
    const enabledCount = eq.bands.filter(b => b.enabled).length;

    if (enabledCount === 0) {
        const hint = document.createElement('p');
        hint.className = 'hint';
        hint.textContent = 'Double-click the EQ graph to add bands, or press + Add Band';
        container.appendChild(hint);
    }

    // Show only enabled slots, preserving their real index for firmware sync
    eq.bands.forEach((band, i) => {
        if (!band.enabled) return;
        container.appendChild(buildBandRow(band, i, (idx, changes) => {
            store.updateEqBand(idx, changes);
            syncEqBand(moduleId, idx);
        }, (idx) => {
            store.removeEqBand(idx);
            syncEqBand(moduleId, idx);  // Tell firmware this slot is now disabled
        }));
    });

    const actions = document.createElement('div');
    actions.className = 'eq-actions';

    if (enabledCount >= 10) {
        const msg = document.createElement('span');
        msg.className = 'band-limit-msg';
        msg.textContent = '⚠ Max 10 bands';
        actions.appendChild(msg);
    } else {
        const addBtn = document.createElement('button');
        addBtn.textContent = '+ Add Band';
        addBtn.className = 'btn btn-outline btn-sm';
        addBtn.addEventListener('click', () => {
            const slot = store.addEqBand(1000, 0, 0.707, 0);
            if (slot !== null) syncEqBand(moduleId, slot);
        });
        actions.appendChild(addBtn);
    }

    const resetBtn = document.createElement('button');
    resetBtn.textContent = 'Reset';
    resetBtn.className = 'btn btn-sm';
    resetBtn.style.color = 'var(--accent-red)';
    resetBtn.addEventListener('click', () => {
        store.resetEqBands();
        eq.bands.forEach((_, i) => syncEqBand(moduleId, i));
    });
    actions.appendChild(resetBtn);

    const exportBtn = document.createElement('button');
    exportBtn.textContent = 'Export C++';
    exportBtn.className = 'btn btn-outline btn-sm';
    exportBtn.style.marginLeft = 'auto';
    exportBtn.addEventListener('click', () => exportEqToCpp(eqKey));
    actions.appendChild(exportBtn);
    container.appendChild(actions);
}

// ─── Dynamic EQ Band Panel ───────────────────────────────────────────

function buildDynEqBandPanel(container, isHigh) {
    const eqKey = isHigh ? 'eqHigh' : 'eqLow';
    const eq = store.dynamicEq[eqKey];
    const enabledCount = eq.bands.filter(b => b.enabled).length;

    if (enabledCount === 0) {
        const hint = document.createElement('p');
        hint.className = 'hint';
        hint.textContent = 'No bands. Click + to add.';
        container.appendChild(hint);
    }

    eq.bands.forEach((band, i) => {
        if (!band.enabled) return;
        container.appendChild(buildBandRow(band, i, (idx, changes) => {
            Object.assign(eq.bands[idx], changes);
            store.emit('eq:changed');
            store.emit('eq:band-updated', idx);
            syncDynEqBand(isHigh, idx);
        }, (idx) => {
            eq.bands[idx].enabled = false;
            Object.assign(eq.bands[idx], { type: 0, freq: 1000, gain: 0, q: 0.707 });
            syncDynEqBand(isHigh, idx);
            store.emit('eq:changed');
            store.emit('eq:structure-changed');
        }));
    });

    const actions = document.createElement('div');
    actions.className = 'eq-actions';

    if (enabledCount >= 10) {
        const msg = document.createElement('span');
        msg.className = 'band-limit-msg';
        msg.textContent = '⚠ Max 10 bands';
        actions.appendChild(msg);
    } else {
        const addBtn = document.createElement('button');
        addBtn.textContent = '+ Add Band';
        addBtn.className = 'btn btn-outline btn-sm';
        addBtn.addEventListener('click', () => {
            const slot = eq.bands.findIndex(b => !b.enabled);
            if (slot === -1) return;
            Object.assign(eq.bands[slot], { enabled: true, freq: 1000, gain: 0, q: 0.707, type: 0 });
            store.emit('eq:changed');
            store.emit('eq:structure-changed');
            syncDynEqBand(isHigh, slot);
        });
        actions.appendChild(addBtn);
    }

    const exportBtn = document.createElement('button');
    exportBtn.textContent = 'Export C++';
    exportBtn.className = 'btn btn-outline btn-sm';
    exportBtn.style.marginLeft = 'auto';
    exportBtn.addEventListener('click', () => exportEqToCpp(isHigh ? 'dynHigh' : 'dynLow'));
    actions.appendChild(exportBtn);

    container.appendChild(actions);
}

// ─── Shared Band Row Builder ─────────────────────────────────────────

function buildBandRow(band, index, onUpdate, onDelete) {
    const row = document.createElement('div');
    row.className = 'eq-band-row';
    row.dataset.bandIndex = index;

    const num = document.createElement('span');
    num.className = 'band-num';
    num.textContent = index + 1;
    row.appendChild(num);

    // Type
    const typeSelect = document.createElement('select');
    typeSelect.className = 'band-type';
    ['PK', 'LS', 'HS', 'LP', 'HP', 'BP', 'Notch'].forEach((t, i) => {
        const opt = document.createElement('option');
        opt.value = i; opt.textContent = t;
        if (i === band.type) opt.selected = true;
        typeSelect.appendChild(opt);
    });
    typeSelect.addEventListener('change', () => onUpdate(index, { type: parseInt(typeSelect.value) }));
    row.appendChild(typeSelect);

    // Freq
    row.appendChild(createNumInput(band.freq, 20, 20000, 1, 'Hz', 'freq', (v) => onUpdate(index, { freq: v })));
    // Gain
    row.appendChild(createNumInput(band.gain, -24, 24, 0.5, 'dB', 'gain', (v) => onUpdate(index, { gain: v })));
    // Q
    row.appendChild(createNumInput(band.q, 0.1, 20, 0.1, 'Q', 'q', (v) => onUpdate(index, { q: v })));

    // Delete
    const delBtn = document.createElement('button');
    delBtn.textContent = '×';
    delBtn.className = 'btn-del';
    delBtn.addEventListener('click', () => onDelete(index));
    row.appendChild(delBtn);

    return row;
}

function createNumInput(value, min, max, step, unit, fieldKey, onChange) {
    const wrap = document.createElement('div');
    wrap.className = 'num-input-wrap';
    const input = document.createElement('input');
    input.type = 'number';
    input.value = value; input.min = min; input.max = max; input.step = step;
    input.className = 'num-input';
    if (fieldKey) input.dataset.field = fieldKey;
    const unitSpan = document.createElement('span');
    unitSpan.className = 'num-unit';
    unitSpan.textContent = unit;
    input.addEventListener('input', () => {
        let v = parseFloat(input.value);
        if (isNaN(v)) return;
        v = Math.max(min, Math.min(max, v));
        onChange(v);
    });
    wrap.appendChild(input);
    wrap.appendChild(unitSpan);
    return wrap;
}

// ─── Rebuild a single accordion body ─────────────────────────────────

function rebuildAccordionBody(moduleId) {
    const acc = document.querySelector(`.accordion[data-module-id="${moduleId}"]`);
    if (!acc) return;
    const body = acc.querySelector('.accordion-body');
    body.innerHTML = '';
    const mod = ACCORDION_MODULES.find(m => m.id === moduleId);
    if (mod) buildModuleBody(body, mod);
}

// ─── EQ Hardware Sync ────────────────────────────────────────────────

function syncEqBand(moduleId, index) {
    let eq;
    if (moduleId === MODULE.EQ_DSP_1) eq = store.eq1;
    else if (moduleId === MODULE.EQ_DSP_2) eq = store.eq2;
    // For Dynamic EQ, use a different function (syncDynEqBand)
    if (!eq) return;

    const band = eq.bands[index];
    if (!band) return;
    const frame = buildSetEqBand(moduleId, index, eq.pregain, band.enabled, band.type, band.freq, dbToQ88(band.gain), qToQ610(band.q));
    sendDebounced(`eq_${moduleId}_${index}`, () => frame, 16);
}

function syncEqToHardware(moduleId) {
    if (moduleId === MODULE.EQ_DSP_1) {
        store.eq1.bands.forEach((_, i) => syncEqBand(moduleId, i));
    } else if (moduleId === MODULE.EQ_DSP_2) {
        store.eq2.bands.forEach((_, i) => syncEqBand(moduleId, i));
    } else if (moduleId === MODULE.DYNAMIC_EQ) {
        // Sync both sets for Dynamic EQ
        store.dynamicEq.eqLow.bands.forEach((_, i) => syncDynEqBand(false, i));
        store.dynamicEq.eqHigh.bands.forEach((_, i) => syncDynEqBand(true, i));
    }
}

function syncDynEqBand(isHigh, index) {
    const eqKey = isHigh ? 'eqHigh' : 'eqLow';
    const eq = store.dynamicEq[eqKey];
    const band = eq.bands[index];
    if (!band) return;
    const frame = buildSetDynEqBand(isHigh, index, eq.pregain, band.enabled, band.type, band.freq, dbToQ88(band.gain), qToQ610(band.q));
    sendDebounced(`dyneq_${eqKey}_${index}`, () => frame, 16);
}

// ─── Slider Builder ──────────────────────────────────────────────────

// formatter(rawValue) → display string. Default: "${v} ${unit}"
function addSlider(container, label, min, max, step, unit, getter, setter, formatter = null, displayScale = 1.0) {
    const row = document.createElement('div');
    row.className = 'param-row';

    const lbl = document.createElement('label');
    lbl.textContent = label;
    lbl.className = 'param-label';

    const slider = document.createElement('input');
    slider.type = 'range';
    slider.min = min; slider.max = max; slider.step = step;
    slider.className = 'param-slider';
    slider.value = getter();

    const valInput = document.createElement('input');
    valInput.type = 'text';
    valInput.className = 'param-input';
    
    const updateInputFromSlider = () => {
        const v = parseFloat(slider.value);
        valInput.value = (v * displayScale).toFixed(displayScale < 1 ? 2 : 0);
    };

    updateInputFromSlider();

    slider.addEventListener('input', () => {
        updateInputFromSlider();
        const v = parseFloat(slider.value);
        clearTimeout(sendDebounce[`slider_${label}`]);
        sendDebounce[`slider_${label}`] = setTimeout(() => setter(v), 16);
    });

    valInput.addEventListener('change', () => {
        let v = parseFloat(valInput.value) / displayScale;
        if (isNaN(v)) { updateInputFromSlider(); return; }
        v = Math.max(min, Math.min(max, v));
        // Round to nearest step
        v = Math.round(v / step) * step;
        slider.value = v;
        updateInputFromSlider();
        setter(v);
    });

    // Enter key support
    valInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') valInput.blur();
    });

    row.appendChild(lbl);
    row.appendChild(slider);
    
    const unitSpan = document.createElement('span');
    unitSpan.textContent = unit ? ` ${unit}` : '';
    unitSpan.style.fontSize = '10px';
    unitSpan.style.color = 'var(--text-dim)';
    unitSpan.style.minWidth = '20px';

    row.appendChild(valInput);
    row.appendChild(unitSpan);
    container.appendChild(row);
    
    return { slider, valInput, fmt: (v) => (v * displayScale).toFixed(2) }; 
}

function addSwitch(container, label, getter, setter) {
    const row = document.createElement('div');
    row.className = 'param-row';

    const lbl = document.createElement('label');
    lbl.textContent = label;
    lbl.className = 'param-label';

    const wrap = document.createElement('div');
    wrap.className = 'switch-wrap';

    const tog = document.createElement('input');
    tog.type = 'checkbox';
    tog.className = 'param-switch';
    tog.checked = getter();

    tog.addEventListener('change', () => {
        setter(tog.checked);
    });

    wrap.appendChild(tog);
    row.appendChild(lbl);
    row.appendChild(wrap);
    container.appendChild(row);
}

// ─── UI Helpers ──────────────────────────────────────────────────────

function showStatus(msg, type = 'info') {
    const el = document.getElementById('status-text');
    el.textContent = msg;
    el.className = 'status-' + type;
}

function exportEqToCpp(eqContextTarget) {
    let eq;
    let targetName = 'eq1';

    if (eqContextTarget === 'eq1') { eq = store.eq1; targetName = 'eq1'; }
    else if (eqContextTarget === 'eq2') { eq = store.eq2; targetName = 'eq2'; }
    else if (eqContextTarget === 'dynLow') { eq = store.dynamicEq.eqLow; targetName = 'dynamic_eq_low'; }
    else if (eqContextTarget === 'dynHigh') { eq = store.dynamicEq.eqHigh; targetName = 'dynamic_eq_high'; }
    else return;

    let cpp = `// Exported EQ params (${targetName})\n`;
    const filterTypes = ['FILTER_PEAK', 'FILTER_LO_SHELF', 'FILTER_HI_SHELF', 'FILTER_LOWPASS', 'FILTER_HIGHPASS', 'FILTER_BANDPASS', 'FILTER_NOTCH'];

    eq.bands.forEach((b, i) => {
        const typeStr = filterTypes[b.type] || 'FILTER_PEAK';
        cpp += `dsp->${targetName}->setBand(${i}, ${typeStr}, ${b.freq}, ${b.gain}, ${b.q}f);\n`;
    });

    navigator.clipboard.writeText(cpp).then(() => {
        showStatus('C++ Code copied to clipboard', 'ok');
    }).catch(err => {
        console.error('Copy failed', err);
        showStatus('Failed to copy', 'error');
    });
}

// ─── Graph Container Helpers ─────────────────────────────────────────

function mountGraphToAccordion(acc) {
    unmountGraph(); // clean up any existing

    const body = acc.querySelector('.accordion-body');
    if (!body) return;

    const container = document.createElement('div');
    container.className = 'eq-graph-container';
    body.insertBefore(container, body.firstChild);

    const canvas = document.createElement('canvas');
    canvas.style.width = '100%';
    canvas.style.height = '100%';
    canvas.style.display = 'block';
    container.appendChild(canvas);

    // Controls wrapper for Pregain
    const controls = document.createElement('div');
    controls.className = 'eq-controls';
    body.insertBefore(controls, container.nextSibling);

    // Pregain slider below graph
    const moduleId = acc.dataset.moduleId;
    let eqState;
    if (moduleId === String(MODULE.EQ_DSP_1)) eqState = store.eq1;
    else if (moduleId === String(MODULE.EQ_DSP_2)) eqState = store.eq2;
    else if (moduleId === 'DYNEQ_LOW') eqState = store.dynamicEq.eqLow;
    else if (moduleId === 'DYNEQ_HIGH') eqState = store.dynamicEq.eqHigh;

    if (eqState) {
        addSlider(controls, 'Pregain', -2400, 2400, 50, 'dB',
            () => eqState.pregain * 100,
            (v) => {
                eqState.pregain = v / 100;
                store.emit('eq:changed');
                // Sync via first enabled band or band 0
                let bandIdx = eqState.bands.findIndex(b => b.enabled);
                if (bandIdx === -1) bandIdx = 0;
                
                if (moduleId === String(MODULE.EQ_DSP_1) || moduleId === String(MODULE.EQ_DSP_2)) {
                    syncEqBand(parseInt(moduleId), bandIdx);
                } else {
                    syncDynEqBand(moduleId === 'DYNEQ_HIGH', bandIdx);
                }
            },
            null, 0.01);
    }

    // Initialize graph instance
    eqGraph = new EQGraph(canvas);
}

function updateCpuUI(usage, heapPct) {
    const cpuContainer = document.getElementById('cpu-container');
    const cpuBar = document.getElementById('cpu-bar-fill');
    const cpuText = document.getElementById('cpu-value');
    const heapContainer = document.getElementById('heap-container');
    const heapBar = document.getElementById('heap-bar-fill');
    const heapText = document.getElementById('heap-value');

    if (!cpuContainer || !cpuBar || !cpuText) return;

    // Show containers if they were hidden
    if (cpuContainer.style.display === 'none' && store.system.connected) {
        cpuContainer.style.display = 'flex';
        if (heapContainer) heapContainer.style.display = 'flex';
    }

    cpuText.textContent = `${usage.toFixed(1)}%`;
    cpuBar.style.width = `${usage}%`;

    // Dynamic color for CPU
    if (usage > 90) cpuBar.style.background = 'var(--accent-red)';
    else if (usage > 75) cpuBar.style.background = 'var(--accent-orange)';
    else cpuBar.style.background = 'var(--accent-green)';

    // Update Heap
    if (heapContainer && heapBar && heapText) {
        heapText.textContent = `${heapPct}%`;
        heapBar.style.width = `${heapPct}%`;
        // Reverse color logic for heap (low heap = red)
        if (heapPct > 85) heapBar.style.background = 'var(--accent-red)';
        else if (heapPct > 70) heapBar.style.background = 'var(--accent-orange)';
        else heapBar.style.background = 'var(--accent-purple)';
    }
}

function unmountGraph() {
    if (eqGraph) {
        eqGraph.destroy();
        eqGraph = null;
    }
    // Remove containers entirely
    document.querySelectorAll('.eq-graph-container, .eq-controls').forEach(el => el.remove());
}

function updateStatusUI() {
    const badge = document.getElementById('connection-status');
    const label = document.getElementById('connection-label');
    const btnDis = document.getElementById('btn-disconnect');
    const cpuContainer = document.getElementById('cpu-container');
    const heapContainer = document.getElementById('heap-container');
    if (store.system.connected) {
        badge.className = 'connected'; badge.id = 'connection-status';
        label.textContent = `Connected — ${store.system.portPath}`;
        btnDis.style.display = '';
        if (cpuContainer) cpuContainer.style.display = 'flex';
        if (heapContainer) heapContainer.style.display = 'flex';
        ['port-select', 'btn-refresh', 'btn-connect'].forEach(id => document.getElementById(id).style.display = 'none');
    } else {
        badge.className = 'disconnected'; badge.id = 'connection-status';
        label.textContent = 'Disconnected';
        btnDis.style.display = 'none';
        if (cpuContainer) cpuContainer.style.display = 'none';
        if (heapContainer) heapContainer.style.display = 'none';
        if (manualMode) ['port-select', 'btn-refresh', 'btn-connect'].forEach(id => document.getElementById(id).style.display = '');
    }
}

function buildBottomBar() {
    const volSlider = document.getElementById('master-vol');
    const volLabel = document.getElementById('master-vol-label');
    volSlider.value = store.volume.gainDb;
    volLabel.textContent = `${(store.volume.gainDb / 100).toFixed(1)} dB`;
    volSlider.addEventListener('input', () => {
        const v = parseInt(volSlider.value);
        store.updateParam('volume', 'gainDb', v);
        volLabel.textContent = `${(v / 100).toFixed(1)} dB`;
        sendDebounced('master_vol', () => buildSetParam(MODULE.VOLUME, 0, v), 16);
    });

    /*
    document.getElementById('input-source').addEventListener('change', (e) => {
        store.system.inputSource = parseInt(e.target.value);
        sendFrame(buildSetInputSource(store.system.inputSource));
    });
    document.getElementById('output-source').addEventListener('change', (e) => {
        store.system.outputSource = parseInt(e.target.value);
        sendFrame(buildSetOutputSource(store.system.outputSource));
    });
    */

    for (let i = 0; i < 8; i++) {
        const btn = document.getElementById(`preset-${i}`);
        if (!btn) continue;
        btn.addEventListener('click', () => {
            isFetchingState = true;
            store.setActivePreset(i);
            sendFrame(buildLoadPreset(i));
            showStatus(`Synchronizing Preset ${i + 1}...`, 'info');
        });
        btn.addEventListener('contextmenu', (e) => {
            e.preventDefault();
            store.setActivePreset(i);
            sendFrame(buildSavePreset(i));
            showStatus(`Saved preset ${i + 1}`, 'ok');
        });
    }

    document.getElementById('btn-save-preset').addEventListener('click', () => {
        const idx = store.system.activePreset;
        sendFrame(buildSavePreset(idx));
        showStatus(`Saved to preset ${idx + 1}`, 'ok');
    });

    store.on('preset:active-changed', (idx) => {
        for (let i = 0; i < 8; i++) {
            const btn = document.getElementById(`preset-${i}`);
            if (btn) btn.classList.toggle('active', i === idx);
        }
    });

    // Initial highlight
    const initialPreset = store.system.activePreset;
    for (let i = 0; i < 8; i++) {
        const btn = document.getElementById(`preset-${i}`);
        if (btn) btn.classList.toggle('active', i === initialPreset);
    }
}

// ─── Init ────────────────────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('btn-refresh').addEventListener('click', refreshPorts);
    document.getElementById('btn-connect').addEventListener('click', connectSerial);
    document.getElementById('btn-disconnect').addEventListener('click', async () => {
        await disconnectSerial();
        enterManualLobby();
    });

    // Lobby buttons
    const lobbyManual = document.getElementById('btn-lobby-manual');
    if (lobbyManual) lobbyManual.addEventListener('click', enterManualLobby);

    const lobbyRefresh = document.getElementById('btn-lobby-refresh');
    if (lobbyRefresh) lobbyRefresh.addEventListener('click', refreshPorts);

    const lobbyConnect = document.getElementById('btn-lobby-connect');
    if (lobbyConnect) {
        lobbyConnect.addEventListener('click', async () => {
            const port = document.getElementById('lobby-port-select').value;
            if (!port) return;
            await abortScanAndWait();
            try {
                await window.serialAPI.connect(port, 115200);
                onConnected(port);
            } catch (e) {
                showStatus(`Connect failed: ${e}`, 'error');
                alert(`Connect failed: ${e}`);
            }
        });
    }

    window.serialAPI.onData((bytes) => parser.feed(bytes));
    window.serialAPI.onDisconnected(() => {
        store.setConnected(false); updateStatusUI();
        showStatus('Disconnected', 'error');
        manualMode = false; startAutoScan();
    });

    // Build UI
    buildAccordionModules();
    buildBottomBar();
    updateStatusUI();

    // EQ Graph
    eqGraph = null;

    store.on('eq:band-selected', (index) => {
        document.querySelectorAll('.eq-band-row').forEach((row, i) => row.classList.toggle('selected', i === index));
    });

    // Handle soft updates from canvas dragging to avoid redrawing DOM
    store.on('eq:band-updated', (index) => {
        if (store.activeEq === 'dynLow') {
            syncDynEqBand(false, index);
        } else if (store.activeEq === 'dynHigh') {
            syncDynEqBand(true, index);
        } else {
            const mid = store.getActiveEqModuleId();
            syncEqBand(mid, index);
        }

        // Scope DOM updates to the active accordion only
        const accId = store.activeEq === 'eq1' ? MODULE.EQ_DSP_1 :
            store.activeEq === 'eq2' ? MODULE.EQ_DSP_2 :
                store.activeEq === 'dynLow' ? 'DYNEQ_LOW' : 'DYNEQ_HIGH';
        const activeAcc = document.querySelector(`.accordion[data-module-id="${accId}"].open`);
        if (!activeAcc) return;

        const eqState = store.getActiveEqState();
        const band = eqState.bands[index];
        if (!band) return;

        const row = activeAcc.querySelector(`.eq-band-row[data-band-index="${index}"]`);
        if (row) {
            const fFreq = row.querySelector('input[data-field="freq"]');
            const fGain = row.querySelector('input[data-field="gain"]');
            const fQ = row.querySelector('input[data-field="q"]');
            const targetActiveElem = document.activeElement;

            if (fFreq && fFreq !== targetActiveElem) fFreq.value = band.freq;
            if (fGain && fGain !== targetActiveElem) fGain.value = band.gain;
            if (fQ && fQ !== targetActiveElem) fQ.value = band.q;
        }
    });

    // Handle structural state changes (e.g. presets loaded, bands added/removed)
    const rebuildStructural = () => {
        unmountGraph();

        const rebuildIds = [MODULE.EQ_DSP_1, MODULE.EQ_DSP_2, 'DYNEQ_LOW', 'DYNEQ_HIGH'];
        let activeAcc = null;

        rebuildIds.forEach(id => {
            const acc = document.querySelector(`.accordion[data-module-id="${id}"]`);
            if (acc && acc.classList.contains('open')) {
                const body = acc.querySelector('.accordion-body');
                body.innerHTML = '';
                const mod = ACCORDION_MODULES.find(m => String(m.id) === String(id));
                if (mod) buildModuleBody(body, mod);
                activeAcc = acc;
            }
        });

        if (activeAcc) mountGraphToAccordion(activeAcc);
    };

    store.on('state:loaded', rebuildStructural);
    store.on('eq:structure-changed', rebuildStructural);

    // Start auto-scan
    startAutoScan();
});