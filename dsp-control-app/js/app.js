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
    buildGetAllState, buildGetModuleMeter, buildSetLookahead,
    buildSetIsfPreset, buildSetIsfConfig, buildGetIsfState,
    buildWifiScan, buildWifiSetSTA, buildWifiSetAP, buildWifiGetStatus,
    dbToQ88, dbToQ31, qToQ610,
    leToInt16, leToInt32,
    buildSetIsfBandParams
} from './protocol.js';
import { EQGraph } from './eq-graph.js';
import { DRCGraph } from './drc-graph.js';

let eqGraph = null;
let drcGraph = null;
let parser = new FrameParser();

// ─── Browser Polyfill for Web UI ──────────────────────────────────────
const isBrowser = !window.serialAPI && !window.wsAPI;

if (isBrowser) {
    let webSocket = null;
    let wsDataCb = null;
    let wsDisconnectCb = null;

    window.serialAPI = {
        listPorts: async () => [],
        connect: async () => { throw new Error("Serial API not available in browser."); },
        disconnect: async () => {},
        send: async () => {},
        onData: () => {},
        onDisconnected: () => {}
    };

    window.wsAPI = {
        connect: (url) => {
            return new Promise((resolve, reject) => {
                try {
                    webSocket = new WebSocket(url);
                    webSocket.binaryType = 'arraybuffer';
                    webSocket.onopen = () => resolve();
                    webSocket.onmessage = (e) => {
                        if (wsDataCb) wsDataCb(new Uint8Array(e.data));
                    };
                    webSocket.onclose = () => {
                        if (wsDisconnectCb) wsDisconnectCb();
                        webSocket = null;
                    };
                    webSocket.onerror = (err) => reject(err);
                } catch (e) { reject(e); }
            });
        },
        disconnect: async () => {
            if (webSocket) webSocket.close();
        },
        send: async (data) => {
            if (webSocket && webSocket.readyState === WebSocket.OPEN) {
                webSocket.send(new Uint8Array(data));
            }
        },
        onData: (cb) => { wsDataCb = cb; },
        onDisconnected: (cb) => { wsDisconnectCb = cb; }
    };
}

let sendDebounce = {};
let autoScanInterval = null;
let manualMode = false;
let isFetchingState = false;
let isPendingStaReboot = false; // Tracks if we expect a reboot after submitting WiFi config
let isProbing = false;       // true while a single port probe is in-flight
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
        const isValid = await probePort(port);
        if (isValid) {
            await window.serialAPI.connect(port, 115200);
            document.getElementById('btn-wifi-config').style.display = '';
            onConnected(port);
        } else {
            showStatus(`Error: Device at ${port} is not recognized as a DSP Core.`, 'error');
            alert(`Device at ${port} is not recognized as a DSP Core.`);
        }
    } catch (e) { showStatus(`Error: ${e}`, 'error'); }
}

function onConnected(port) {
    store.setConnected(true, port);
    stopAutoScan();
    hideScanOverlay();
    updateStatusUI();
    isFetchingState = true;
    sendFrame(buildGetAllState());
    sendFrame(buildWifiGetStatus());
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
    try {
        if (store.system.transport === 'serial') {
            await window.serialAPI.send(frameArray);
        } else {
            await window.wsAPI.send(frameArray);
        }
    }
    catch (e) { console.error('Send error:', e); }
}

async function probeWebSocket(url, timeoutMs = 2000) {
    return new Promise((resolve) => {
        let ws;
        const timer = setTimeout(() => {
            if (ws) ws.close();
            resolve(false);
        }, timeoutMs);
        
        try {
            ws = new WebSocket(url);
            ws.onopen = () => {
                clearTimeout(timer);
                ws.close();
                resolve(true);
            };
            ws.onerror = () => {
                clearTimeout(timer);
                ws.close();
                resolve(false);
            };
        } catch (e) {
            clearTimeout(timer);
            resolve(false);
        }
    });
}

async function connectWebSocket(url, silent = false) {
    await abortScanAndWait();
    try {
        await window.wsAPI.connect(url);
        
        // Save successfully connected IP/Hostname to localStorage
        if (url.startsWith('ws://')) {
            const host = url.replace('ws://', '').replace('/ws', '');
            if (!host.includes('.local') && host !== '192.168.4.1') {
                localStorage.setItem('dsp_last_ip', host);
            }
        }
        
        onConnectedWS(url);
    } catch (e) {
        if (!silent) {
            showStatus(`WS Error: ${e}`, 'error');
            alert(`WebSocket connect failed: ${e}`);
        }
        throw e;
    }
}

function onConnectedWS(url) {
    store.setConnected(true, url);
    store.system.transport = 'websocket';
    stopAutoScan();
    hideScanOverlay();
    updateStatusUI();
    isFetchingState = true;
    sendFrame(buildGetAllState());
    sendFrame(buildWifiGetStatus());
    showStatus(`Connected via WiFi (${url}) — Fetching DSP State...`, 'ok');
    switchLobbyScreen('scan-wifi-connected');
}

async function disconnectWebSocket() {
    await window.wsAPI.disconnect();
    store.setConnected(false);
    store.system.transport = 'serial';
    updateStatusUI();
}

function sendDebounced(key, frameFn, delay = 16) {
    clearTimeout(sendDebounce[key]);
    sendDebounce[key] = setTimeout(() => sendFrame(frameFn()), delay);
}

function switchLobbyScreen(screenId) {
    const screens = [
        'scan-auto-content', 
        'scan-manual-content', 
        'scan-wifi-content', 
        'scan-wifi-connected', 
        'lobby-choice-content', 
        'lobby-wifi-searching'
    ];
    screens.forEach(id => {
        const el = document.getElementById(id);
        if (el) el.style.display = (id === screenId) ? 'block' : 'none';
    });
}

function showConnectionLobby() {
    document.getElementById('scan-overlay').classList.remove('hidden');
    switchLobbyScreen('lobby-choice-content');
    document.getElementById('lobby-choice-error').style.display = 'none';
}

async function startAutoScan() {
    showConnectionLobby();
}

// ─── Auto-Scan ───────────────────────────────────────────────────────

async function probePort(path, portsLabel) {
    isProbing = true;
    probeAborted = false;
    if (portsLabel) portsLabel.textContent = `Probing ${path}...`;
    try {
        await window.serialAPI.connect(path, 115200);

        if (probeAborted) { await window.serialAPI.disconnect(); return false; }

        // Short settle then send probe packet
        await new Promise(r => setTimeout(r, 150));
        await window.serialAPI.send(buildGetAllState());

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
        await window.serialAPI.disconnect();
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



function stopAutoScan() { if (autoScanInterval) { clearInterval(autoScanInterval); autoScanInterval = null; } }
function hideScanOverlay() { document.getElementById('scan-overlay').classList.add('hidden'); }

async function enterManualLobby() {
    manualMode = true;
    await abortScanAndWait();   // wait for probe to release port before user connects
    document.getElementById('scan-overlay').classList.remove('hidden');
    switchLobbyScreen('scan-manual-content');
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
            
            setTimeout(() => {
                buildAccordionModules();
                store.emit('eq:changed');
                store.emit('state:loaded');
                store.emit('eq:structure-changed');
                showStatus('State synchronized successfully!', 'ok');
            }, 0);
        }
    }
    else if (frame.cmd === CMD.ACK_RESPONSE && frame.data[0] === 0xFF) {
        // Scanning in progress — poll again in 1.5 seconds if UI is still open
        if (document.getElementById('scan-wifi-content').style.display !== 'none') {
            setTimeout(() => sendFrame(buildWifiScan()), 1500);
        }
    }
    else if (frame.cmd === CMD.ERROR) showStatus(`Error: 0x${frame.data[0]?.toString(16)}`, 'error');
    else if (frame.cmd === CMD.REPORT_ENABLE_MASK && frame.data.length >= 2) {
        // Firmware sends enable mask after GET_ALL_STATE
        // Bits correspond to chain order: [0]=preGain ... [11]=postGain
        const mask = frame.data[0] | (frame.data[1] << 8);
        MODULE_ORDER.forEach((modId, chainIdx) => {
            const enabled = Boolean((mask >> chainIdx) & 1);
            store.setModuleEnabled(modId, enabled);
        });
    }
    else if (frame.cmd === CMD.SET_PARAM && frame.data.length >= 5) {
        const pIndex = frame.data[0];
        const val = readInt32(frame.data, 1);

        switch (frame.moduleId) {
            case MODULE.PRE_GAIN:
                if (pIndex === 0) store.updateParam('preGain', 'gainDb', val);
                else if (pIndex === 1) store.updateParam('preGain', 'mute', val !== 0);
                else if (pIndex === 2) store.updateParam('preGain', 'mono', val !== 0);
                break;
            case MODULE.POST_GAIN:
                if (pIndex === 0) store.updateParam('postGain', 'gainDb', val);
                else if (pIndex === 1) store.updateParam('preGain', 'mute', val !== 0);
                else if (pIndex === 2) store.updateParam('postGain', 'mono', val !== 0);
                break;
            case MODULE.COMPANDER:
                if (pIndex === 0) store.updateParam('compander', 'threshold', val);
                else if (pIndex === 1) store.updateParam('compander', 'ratioBelow', val);
                else if (pIndex === 2) store.updateParam('compander', 'ratioAbove', val);
                else if (pIndex === 3) store.updateParam('compander', 'attackMs', val);
                else if (pIndex === 4) store.updateParam('compander', 'releaseMs', val);
                else if (pIndex === 5) store.updateParam('compander', 'pregain', val);
                else if (pIndex === 6) store.updateParam('compander', 'lookaheadMs', val / 10); // ms×10 → ms
                break;
            case MODULE.EXCITER:
                if (pIndex === 0) store.updateParam('exciter', 'cutoffFreq', val);
                else if (pIndex === 1) store.updateParam('exciter', 'dry', val);
                else if (pIndex === 2) store.updateParam('exciter', 'wet', val);
                break;
            case MODULE.DYNAMIC_BASS:
                if (pIndex === 0) store.updateParam('dynamicBass', 'cutoffFreq', val);
                else if (pIndex === 1) store.updateParam('dynamicBass', 'gainBoost', val);
                else if (pIndex === 2) store.updateParam('dynamicBass', 'enhanced', val);
                else if (pIndex === 3) store.updateParam('dynamicBass', 'boostthreshold', val);
                else if (pIndex === 4) store.updateParam('dynamicBass', 'neutralthreshold', val);
                else if (pIndex === 5) store.updateParam('dynamicBass', 'clipthreshold', val);
                else if (pIndex === 6) store.updateParam('dynamicBass', 'clipattack', val);
                else if (pIndex === 7) store.updateParam('dynamicBass', 'cliprelease', val);
                break;
            case MODULE.DRC: {
                // New encoding:
                //   pIndex 0x10 = mode
                //   pIndex 0x20-0x3F = per-band: band=(pIndex-0x20)>>3, param=(pIndex-0x20)&7
                if (pIndex === 0x10) {
                    store.drc.mode = val;
                } else if (pIndex >= 0x20 && pIndex <= 0x3F) {
                    const bandIdx = (pIndex - 0x20) >> 3;   // 0-3
                    const param   = (pIndex - 0x20) & 0x07; // 0-4
                    const drcBand = store.drc.bands[bandIdx];
                    if (drcBand) {
                        if (param === 0) drcBand.threshold = val;
                        else if (param === 1) drcBand.ratio = val;
                        else if (param === 2) drcBand.attackMs = val;
                        else if (param === 3) drcBand.releaseMs = val;
                        else if (param === 4) drcBand.pregain = val;
                        else if (param === 5) drcBand.lookaheadMs = val / 10; // ms×10 → ms
                    }
                }
                break;
            }
        }
    }
    else if (frame.cmd === CMD.SET_EQ_BAND && frame.data.length >= 11) {
        const d = frame.data;
        const pregain = leToInt16(d, 0);
        const b = d[2];
        const gain = leToInt16(d, 7);
        const qVal = leToInt16(d, 9);
        const changes = { enabled: d[3] === 1, type: d[4], freq: d[5] | (d[6] << 8), gain: gain / 256, q: qVal / 1024 };
        
        let eqState;
        let realBand = b;
        if (frame.moduleId === MODULE.EQ_DSP_1) {
            eqState = store.eq1;
        } else if (frame.moduleId === MODULE.EQ_DSP_2) {
            eqState = store.eq2;
        } else if (frame.moduleId === MODULE.LEFTRIGHT_EQ) {
            if (b & 0x80) {
                eqState = store.leftRightEq.eqRight;
                realBand = b & 0x7F;
            } else {
                eqState = store.leftRightEq.eqLeft;
            }
        }

        if (eqState && eqState.bands[realBand]) {
            eqState.pregain = pregain / 256;
            Object.assign(eqState.bands[realBand], changes);
            store.emit('eq:changed');
        }
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
    else if (frame.cmd === CMD.SEND_REPORT_CPU_USAGE && frame.data.length >= 7) {
        const cpu10 = frame.data[0] | (frame.data[1] << 8);
        const heapPct = frame.data[2];
        const fs = readInt32(frame.data, 3);
        updateCpuUI(cpu10 / 10.0, 100 - heapPct, fs);
    }
    else if (frame.cmd === CMD.REPORT_ISF && frame.data.length >= 7) {
        // Data: instance(1)+level_q88(2)+slew_q88(2)+activeA(1)+activeB(1)+numPresets(1)
        const d = frame.data;
        const instanceIdx = d[0];
        const which = instanceIdx === 0 ? 'isf1' : 'isf2';
        const levelDb  = leToInt16(d, 1) / 256;
        const slewQ88  = leToInt16(d, 3);
        const slewIdx  = slewQ88 / 256;
        const activeA  = d[5];
        const activeB  = d[6];
        store.updateIsfState(which, levelDb, slewIdx, activeA, activeB);
        renderIsfLevelMeter(which, levelDb, slewIdx, activeA, activeB);
    }
    else if (frame.cmd === CMD.REPORT_ISF_BAND_PER_PRESET && frame.data.length >= 10) {
        const d = frame.data;
        const moduleId  = frame.moduleId;
        const which     = (moduleId === MODULE.ISF_1) ? 'isf1' : 'isf2';
        const presetIdx = d[0];
        const bandIdx = d[1];
        const isf = store.getIsfInstance(which);
        const changes = { enabled: d[2] === 1, type: d[3], freq: (d[4] | (d[5] << 8)), gain: leToInt16(d, 6) / 256, q: leToInt16(d, 8) / 1024};
        Object.assign(isf.presets[presetIdx].bands[bandIdx], changes);
        isf.presets[presetIdx].numBands = isf.presets[presetIdx].bands.filter(x => x.enabled).length;
        store.emit("isf:preset-data-update", which, presetIdx);
    }
    else if (frame.cmd == CMD.CURRENT_PRESET_INDEX && frame.data.length >= 1) {
        store.setActivePreset(frame.data[0]);
    }
    // ── Live meter reports ───────────────────────────────────────────────────
    else if (frame.cmd === CMD.REPORT_DYNBASS && frame.data.length >= 4 && !isFetchingState) {
        const energyDb = leToInt16(frame.data, 0) / 256;  // Q8.8 → float dB
        const alpha    = leToInt16(frame.data, 2) / 256;  // Q8.8 → float [-1..1]
        renderDynBassMeter(energyDb, alpha);
    }
    else if (frame.cmd === CMD.REPORT_DYNEQ && frame.data.length >= 6 && !isFetchingState) {
        const energyDb  = leToInt16(frame.data, 0) / 256;
        const alphaLow  = leToInt16(frame.data, 2) / 256;
        const alphaHigh = leToInt16(frame.data, 4) / 256;
        renderDynEqMeter(energyDb, alphaLow, alphaHigh);
    }
    else if (frame.cmd === CMD.REPORT_COMPANDER && frame.data.length >= 4 && !isFetchingState) {
        // envLinear Q1.14 (0..16384 = 0..1.0), gainDb Q8.8
        const envLinear = (frame.data[0] | (frame.data[1] << 8)) / 16384;
        const gainDb    = leToInt16(frame.data, 2) / 256;
        renderCompanderMeter(envLinear, gainDb);
    }
    else if (frame.cmd === CMD.REPORT_DRC && frame.data.length >= 8 && !isFetchingState) {
        // 4 × int16 Q8.8 gain reduction dB (bands 0-2 + fullband)
        const gains = [
            leToInt16(frame.data, 0) / 256,
            leToInt16(frame.data, 2) / 256,
            leToInt16(frame.data, 4) / 256,
            leToInt16(frame.data, 6) / 256,
        ];
        renderDrcMeter(gains);
    }
    else if (frame.cmd === CMD.REPORT_ISF_PRESET && frame.data.length >= 5) {
        // Data: preset_idx(1)+threshold(2)+pregain(2)
        const d = frame.data;
        const moduleId  = frame.moduleId;
        const which     = (moduleId === MODULE.ISF_1) ? 'isf1' : 'isf2';
        const presetIdx = d[0];
        const threshDb  = leToInt16(d, 1) / 256;
        const pregainDb = leToInt16(d, 3) / 256;
        
        store.updateIsfPreset(which, presetIdx, { thresholdDb: threshDb, pregainDb});
        store.emit("isf:preset-data-update", which, presetIdx);
    }
    else if (frame.cmd === CMD.WIFI_GET_STATUS && frame.data.length >= 2) {
        const modeByte = frame.data[0];
        const ip = `${frame.data[1]}.${frame.data[2]}.${frame.data[3]}.${frame.data[4]}`;
        const rssiRaw = frame.data[5];
        const rssi = (rssiRaw > 127) ? rssiRaw - 256 : rssiRaw;
        const ssidLen = frame.data[6] || 0;
        let ssid = '';
        if (ssidLen > 0) {
            ssid = new TextDecoder().decode(new Uint8Array(frame.data.slice(7, 7 + ssidLen)));
        }
        store.wifi.mode = modeByte === 1 ? 'STA' : modeByte === 2 ? 'AP' : 'Unknown';
        store.wifi.ip = ip;
        store.wifi.rssi = rssi;
        store.wifi.ssid = ssid;
        updateWifiUI();
    }
    else if (frame.cmd === CMD.WIFI_SCAN) {
        if (frame.data.length >= 4) {
            const idx = frame.data[0];
            const total = frame.data[1];
            const rssiRaw = frame.data[2];
            const rssi = (rssiRaw > 127) ? rssiRaw - 256 : rssiRaw;
            const enc = frame.data[3];
            const ssid = new TextDecoder().decode(new Uint8Array(frame.data.slice(4)));
            if (idx === 0) store.wifi.scanResults = [];
            store.wifi.scanResults.push({ ssid, rssi, encrypted: enc === 1 });
            if (idx === total - 1) {
                renderWifiList();
            }
        }
    }
});

// ─── Accordion Builder ───────────────────────────────────────────────

// Extended module list: split Dynamic EQ into DynEQ Thresh, DynEQ Low, DynEQ High
const ACCORDION_MODULES = [
    { id: MODULE.PRE_GAIN, name: 'Pre Gain', icon: '🎚️' },
    { id: MODULE.COMPANDER, name: 'Compander', icon: '📊' },
    { id: MODULE.EXCITER, name: 'Exciter', icon: '✨' },
    { id: MODULE.DYNAMIC_BASS, name: 'Dynamic Bass', icon: '🔊' },
    { id: 'DYNEQ_THRESH', name: 'Dynamic EQ — Thresholds', icon: '⚡', parentId: MODULE.DYNAMIC_EQ },
    { id: 'DYNEQ_LOW', name: 'Dynamic EQ — Low', icon: '🔉', parentId: MODULE.DYNAMIC_EQ },
    { id: 'DYNEQ_HIGH', name: 'Dynamic EQ — High', icon: '🔊', parentId: MODULE.DYNAMIC_EQ },
    { id: MODULE.ISF_1, name: 'ISF 1 EQ', icon: '🎛️' },
    { id: MODULE.ISF_2, name: 'ISF 2 EQ', icon: '🎛️' },
    { id: MODULE.EQ_DSP_1, name: 'Parametric EQ 1', icon: '📈' },
    { id: MODULE.EQ_DSP_2, name: 'Parametric EQ 2', icon: '📉' },
    { id: 'EQ_LEFT', name: 'EQ Left', icon: '👈', parentId: MODULE.LEFTRIGHT_EQ },
    { id: 'EQ_RIGHT', name: 'EQ Right', icon: '👉', parentId: MODULE.LEFTRIGHT_EQ },
    { id: MODULE.DRC, name: 'Dynamic Range Compression', icon: '🛡️' },
    { id: MODULE.POST_GAIN, name: 'Post Gain', icon: '🔉' },
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

        const EQ_MODULE_IDS = [String(MODULE.ISF_1), String(MODULE.ISF_2), String(MODULE.EQ_DSP_1), String(MODULE.EQ_DSP_2), 'DYNEQ_LOW', 'DYNEQ_HIGH', 'EQ_LEFT', 'EQ_RIGHT'];
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
                    else if (mod.id === 'EQ_LEFT') store.setActiveEq('eqLeft');
                    else if (mod.id === 'EQ_RIGHT') store.setActiveEq('eqRight');
                    else if (mod.id === MODULE.ISF_1) { store.setActiveIsfInstance('isf1'); store.graphMode = 'isf1'; }
                    else if (mod.id === MODULE.ISF_2) { store.setActiveIsfInstance('isf2'); store.graphMode = 'isf2'; }

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
        case MODULE.COMPANDER:
            body.appendChild(buildGrMeter({ id: 'compander', label: 'Gain Reduction' }));
            addSlider(body, 'Threshold', -6000, 0, 100, 'dB',
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
            addSlider(body, 'Attack', 1, 2000, 1, 'ms',
                () => store.compander.attackMs,
                (v) => { store.compander.attackMs = v; sendFrame(buildSetParam(MODULE.COMPANDER, 3, v)); });
            addSlider(body, 'Release', 10, 2000, 1, 'ms',
                () => store.compander.releaseMs,
                (v) => { store.compander.releaseMs = v; sendFrame(buildSetParam(MODULE.COMPANDER, 4, v)); });
            addSlider(body, 'Lookahead', 0, 100, 1, 'ms',
                () => store.compander.lookaheadMs,
                (v) => {
                    store.compander.lookaheadMs = v;
                    // paramId 6, encoding: ms × 10 → int32
                    sendFrame(buildSetLookahead(MODULE.COMPANDER, 6, v));
                }, null, 0.1);
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

        case MODULE.DYNAMIC_BASS:
            body.appendChild(buildDynMeter({
                id:     'dynbass',
                label:  'Bass Level',
                zones:  [
                    { id: 'dynbass-zone-boost', label: 'Boost',   color: 'var(--accent)' },
                    { id: 'dynbass-zone-flat',  label: 'Neutral', color: 'var(--accent-green)' },
                    { id: 'dynbass-zone-clip',  label: 'Protect', color: 'var(--accent-red)' },
                ]
            }));
            addSlider(body, 'Cutoff Freq', 30, 300, 5, 'Hz',
                () => store.dynamicBass.cutoffFreq,
                (v) => { store.dynamicBass.cutoffFreq = v; sendFrame(buildSetParam(MODULE.DYNAMIC_BASS, 0, v)); });
            addSlider(body, 'Gain Boost', 0, 2000, 10, 'dB',
                () => store.dynamicBass.gainBoost,
                (v) => { store.dynamicBass.gainBoost = v; sendFrame(buildSetParam(MODULE.DYNAMIC_BASS, 1, v)); },
                null, 0.01);
            addSwitch(body, 'Boost Enhanced',
                () => store.dynamicBass.enhanced > 0,
                (v) => { store.dynamicBass.enhanced = v ? 1 : 0; sendFrame(buildSetParam(MODULE.DYNAMIC_BASS, 2, v ? 1 : 0)); });
            addSlider(body, 'Boost Full Thres', -6000, 0, 10, 'dB',
                () => store.dynamicBass.boostthreshold,
                (v) => { store.dynamicBass.boostthreshold = v; sendFrame(buildSetParam(MODULE.DYNAMIC_BASS, 3, v)); },
                null, 0.01);
            addSlider(body, 'Neutral Thres', -6000, 0, 10, 'dB',
                () => store.dynamicBass.neutralthreshold,
                (v) => { store.dynamicBass.neutralthreshold = v; sendFrame(buildSetParam(MODULE.DYNAMIC_BASS, 4, v)); },
                null, 0.01);
            addSlider(body, 'Clip Full Thres', -6000, 0, 10, 'dB',
                () => store.dynamicBass.clipthreshold,
                (v) => { store.dynamicBass.clipthreshold = v; sendFrame(buildSetParam(MODULE.DYNAMIC_BASS, 5, v)); },
                null, 0.01);
            addSlider(body, 'Clip Attack', 0, 2000, 1, 'ms',
                () => store.dynamicBass.clipattack,
                (v) => { store.dynamicBass.clipattack = v; sendFrame(buildSetParam(MODULE.DYNAMIC_BASS, 6, v)); });
            addSlider(body, 'Clip Release', 0, 2000, 1, 'ms',
                () => store.dynamicBass.cliprelease,
                (v) => { store.dynamicBass.cliprelease = v; sendFrame(buildSetParam(MODULE.DYNAMIC_BASS, 7, v)); });
            break;

        case MODULE.AUTO_EQ:
            buildAutoEqPanel(body);
            break;

        case MODULE.ISF_1:
            buildIsfPanel(body, 'isf1');
            break;

        case MODULE.ISF_2:
            buildIsfPanel(body, 'isf2');
            break;

        case MODULE.EQ_DSP_1:
            buildEqBandPanel(body, MODULE.EQ_DSP_1, 'eq1');
            break;

        case MODULE.EQ_DSP_2:
            buildEqBandPanel(body, MODULE.EQ_DSP_2, 'eq2');
            break;

        case 'EQ_LEFT':
            buildEqBandPanel(body, MODULE.LEFTRIGHT_EQ, 'eqLeft');
            break;

        case 'EQ_RIGHT':
            buildEqBandPanel(body, MODULE.LEFTRIGHT_EQ, 'eqRight');
            break;

        case 'DYNEQ_THRESH': {
            body.appendChild(buildDynMeter({
                id:     'dyneq',
                label:  'Signal Level',
                zones:  [
                    { id: 'dyneq-zone-low',  label: 'EQ Low',  color: 'var(--accent)' },
                    { id: 'dyneq-zone-flat', label: 'Neutral', color: 'var(--accent-green)' },
                    { id: 'dyneq-zone-high', label: 'EQ High', color: 'var(--accent-orange)' },
                ]
            }));
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

            addSlider(body, 'Attack', 1, 2000, 1, 'ms',
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
            body.appendChild(buildGrMeter({ id: 'drc', label: 'Gain Reduction', multiband: true }));
            buildDrcPanel(body);
            break;

        case MODULE.PRE_GAIN:
            addSlider(body, 'Gain', -9600, 2400, 25, 'dB',
                () => store.preGain.gainDb,
                (v) => { store.preGain.gainDb = v; sendFrame(buildSetParam(MODULE.PRE_GAIN, 0, v)); },
                null, 0.01);
            addSwitch(body, 'Mute',
                () => store.postGain.mute,
                (v) => { store.postGain.mute = v; sendFrame(buildSetParam(MODULE.POST_GAIN, 1, v ? 1 : 0)); });
            addSwitch(body, 'Mono',
                () => store.preGain.mono,
                (v) => { store.preGain.mono = v; sendFrame(buildSetParam(MODULE.PRE_GAIN, 2, v ? 1 : 0)); });
            break;

        case MODULE.POST_GAIN:
            addSlider(body, 'Gain', -9600, 2400, 25, 'dB',
                () => store.postGain.gainDb,
                (v) => { store.postGain.gainDb = v; sendFrame(buildSetParam(MODULE.POST_GAIN, 0, v)); },
                null, 0.01);
            addSwitch(body, 'Mute',
                () => store.postGain.mute,
                (v) => { store.postGain.mute = v; sendFrame(buildSetParam(MODULE.POST_GAIN, 1, v ? 1 : 0)); });
            addSwitch(body, 'Mono',
                () => store.postGain.mono,
                (v) => { store.postGain.mono = v; sendFrame(buildSetParam(MODULE.POST_GAIN, 2, v ? 1 : 0)); });
            break;
    }
}

// ─── DRC Panel ───────────────────────────────────────────────────────

function buildDrcPanel(container) {
    const d = store.drc;

    // ── Layout: left = graph, right = controls ─────────────────────────
    const layout = document.createElement('div');
    layout.className = 'drc-layout';
    container.appendChild(layout);

    // ── LEFT: Compression Curve Canvas ───────────────────────────────
    const graphWrap = document.createElement('div');
    graphWrap.className = 'drc-graph-wrap';
    layout.appendChild(graphWrap);

    const canvas = document.createElement('canvas');
    graphWrap.appendChild(canvas);

    if (drcGraph) drcGraph.destroy();
    drcGraph = new DRCGraph(canvas);

    const refreshGraph = () => {
        const band = d.bands[d.activeBand];
        const thDb = band.threshold / 100;
        const ratio = band.ratio / 100;
        drcGraph.draw(thDb, ratio);
    };
    refreshGraph();

    // ── RIGHT: Controls ───────────────────────────────────────────────
    const controls = document.createElement('div');
    controls.className = 'drc-controls';
    layout.appendChild(controls);

    // ── Mode selector ────────────────────────────────────────────────
    const modeRow = document.createElement('div');
    modeRow.className = 'drc-row';
    const modeLabel = document.createElement('label');
    modeLabel.textContent = 'Mode';
    const modeSelect = document.createElement('select');
    modeSelect.className = 'drc-select';
    [
        [0, 'Full Band'],
        [1, '2 Band'],
        [2, '2 Band + Full'],
        [3, '3 Band'],
        [4, '3 Band + Full'],
    ].forEach(([val, name]) => {
        const opt = document.createElement('option');
        opt.value = val;
        opt.textContent = name;
        if (d.mode === val) opt.selected = true;
        modeSelect.appendChild(opt);
    });
    modeSelect.addEventListener('change', () => {
        d.mode = parseInt(modeSelect.value);
        sendFrame(buildSetParam(MODULE.DRC, 0x10, d.mode));
        updateCrossoverVisibility();
        updateBandTabs();
    });
    modeRow.appendChild(modeLabel);
    modeRow.appendChild(modeSelect);
    controls.appendChild(modeRow);

    // ── Crossover Filter type ─────────────────────────────────────────
    const cfRow = document.createElement('div');
    cfRow.className = 'drc-row';
    const cfLabel = document.createElement('label');
    cfLabel.textContent = 'Crossover Filter';
    const cfSelect = document.createElement('select');
    cfSelect.className = 'drc-select';
    [
        [1, 'Butterworth, order=1'],
        [2, 'Linkwitz-Riley, order=2'],
        [3, 'Linkwitz-Riley, order=4'],
        [4, 'Q-controlled, order=4'],
    ].forEach(([val, name]) => {
        const opt = document.createElement('option');
        opt.value = val;
        opt.textContent = name;
        if (d.cfType === val) opt.selected = true;
        cfSelect.appendChild(opt);
    });
    cfSelect.addEventListener('change', () => {
        d.cfType = parseInt(cfSelect.value);
        sendFrame(buildSetParam(MODULE.DRC, 0x11, d.cfType));
        updateCrossoverVisibility();
    });
    cfRow.appendChild(cfLabel);
    cfRow.appendChild(cfSelect);
    controls.appendChild(cfRow);

    // ── Crossover Freq 1 + Q LP ───────────────────────────────────────
    const cf1Row = document.createElement('div');
    cf1Row.className = 'drc-row drc-cf-row';
    cf1Row.innerHTML = '<label>Crossover Freq1</label>';
    const fc1Inp = document.createElement('input');
    fc1Inp.type = 'number'; fc1Inp.className = 'drc-num'; fc1Inp.min = 20; fc1Inp.max = 20000; fc1Inp.step = 10;
    fc1Inp.value = d.fc1;
    fc1Inp.addEventListener('change', () => {
        d.fc1 = Math.max(20, Math.min(20000, parseInt(fc1Inp.value) || d.fc1));
        fc1Inp.value = d.fc1;
        sendFrame(buildSetParam(MODULE.DRC, 0x12, d.fc1));
    });
    const qLpLabel = document.createElement('span'); qLpLabel.textContent = 'Q(LP)'; qLpLabel.className = 'drc-qlabel';
    const qLpInp = document.createElement('input');
    qLpInp.type = 'number'; qLpInp.className = 'drc-num drc-q'; qLpInp.min = 0.1; qLpInp.max = 4; qLpInp.step = 0.01;
    qLpInp.value = (d.qLp / 1024).toFixed(2);
    qLpInp.addEventListener('change', () => {
        d.qLp = Math.round(parseFloat(qLpInp.value) * 1024);
        sendFrame(buildSetParam(MODULE.DRC, 0x14, d.qLp));
    });
    cf1Row.appendChild(fc1Inp);
    cf1Row.appendChild(qLpLabel);
    cf1Row.appendChild(qLpInp);
    controls.appendChild(cf1Row);

    // ── Crossover Freq 2 + Q HP ───────────────────────────────────────
    const cf2Row = document.createElement('div');
    cf2Row.className = 'drc-row drc-cf-row';
    cf2Row.innerHTML = '<label>Crossover Freq2</label>';
    const fc2Inp = document.createElement('input');
    fc2Inp.type = 'number'; fc2Inp.className = 'drc-num'; fc2Inp.min = 20; fc2Inp.max = 20000; fc2Inp.step = 10;
    fc2Inp.value = d.fc2;
    fc2Inp.addEventListener('change', () => {
        d.fc2 = Math.max(20, Math.min(20000, parseInt(fc2Inp.value) || d.fc2));
        fc2Inp.value = d.fc2;
        sendFrame(buildSetParam(MODULE.DRC, 0x13, d.fc2));
    });
    const qHpLabel = document.createElement('span'); qHpLabel.textContent = 'Q(HP)'; qHpLabel.className = 'drc-qlabel';
    const qHpInp = document.createElement('input');
    qHpInp.type = 'number'; qHpInp.className = 'drc-num drc-q'; qHpInp.min = 0.1; qHpInp.max = 4; qHpInp.step = 0.01;
    qHpInp.value = (d.qHp / 1024).toFixed(2);
    qHpInp.addEventListener('change', () => {
        d.qHp = Math.round(parseFloat(qHpInp.value) * 1024);
        sendFrame(buildSetParam(MODULE.DRC, 0x15, d.qHp));
    });
    cf2Row.appendChild(fc2Inp);
    cf2Row.appendChild(qHpLabel);
    cf2Row.appendChild(qHpInp);
    controls.appendChild(cf2Row);

    // ── Band selector tabs ────────────────────────────────────────────
    const tabsRow = document.createElement('div');
    tabsRow.className = 'drc-tabs';
    controls.appendChild(tabsRow);

    // ── Per-band sliders ──────────────────────────────────────────────
    const bandControls = document.createElement('div');
    bandControls.className = 'drc-band-controls';
    controls.appendChild(bandControls);

    // Map band index → param base offset
    // Firmware encoding: paramId = 0x20 + band*8 + param  (band 0-3)
    const BAND_PARAM = [0x20, 0x28, 0x30, 0x38];

    const buildBandControls = (bandIdx) => {
        bandControls.innerHTML = '';
        const band = d.bands[bandIdx];
        const pBase = BAND_PARAM[bandIdx];

        addSlider(bandControls, 'Pregain', -7200, 1800, 25, 'dB',
            () => Math.round((Math.log10(band.pregain / 4096) * 20) * 100),
            (v) => {
                band.pregain = Math.round(Math.pow(10, v / 2000) * 4096);
                sendFrame(buildSetParam(MODULE.DRC, pBase + 4, band.pregain));
            }, null, 0.01);

        addSlider(bandControls, 'Threshold', -9000, 0, 50, 'dB',
            () => band.threshold,
            (v) => {
                band.threshold = v;
                sendFrame(buildSetParam(MODULE.DRC, pBase + 0, v));
                refreshGraph();
            }, null, 0.01);

        addSlider(bandControls, 'Ratio', 100, 10000, 100, ':1',
            () => band.ratio,
            (v) => {
                band.ratio = v;
                sendFrame(buildSetParam(MODULE.DRC, pBase + 1, v));
                refreshGraph();
            }, null, 0.01);

        addSlider(bandControls, 'Attack', 1, 2000, 1, 'ms',
            () => band.attackMs,
            (v) => { band.attackMs = v; sendFrame(buildSetParam(MODULE.DRC, pBase + 2, v)); });

        addSlider(bandControls, 'Release', 10, 2000, 1, 'ms',
            () => band.releaseMs,
            (v) => { band.releaseMs = v; sendFrame(buildSetParam(MODULE.DRC, pBase + 3, v)); });
        addSlider(bandControls, 'Lookahead', 0, 100, 1, 'ms',
            () => band.lookaheadMs,
            (v) => {
                band.lookaheadMs = v;
                // pBase + 5: band-specific lookahead, encoding ms × 10 → int32
                sendFrame(buildSetLookahead(MODULE.DRC, pBase + 5, v));
            }, null, 0.1);
    };

    const renderTabs = () => {
        tabsRow.innerHTML = '';
        const visibleBands = getBandCount(d.mode);

        for (let tabPos = 0; tabPos < visibleBands; tabPos++) {
            const bandIdx = tabToBandIdx(tabPos, d.mode);
            const isFullband = (bandIdx === 3);
            const label = isFullband ? '● Full' : `Band ${tabPos + 1}`;
            const tab = document.createElement('button');
            tab.className = 'drc-tab' + (d.activeBand === bandIdx ? ' active' : '');
            tab.textContent = label;
            tab.addEventListener('click', () => {
                d.activeBand = bandIdx;
                refreshGraph();
                buildBandControls(bandIdx);
                tabsRow.querySelectorAll('.drc-tab').forEach((t, idx) => {
                    t.classList.toggle('active', tabToBandIdx(idx, d.mode) === bandIdx);
                });
            });
            tabsRow.appendChild(tab);
        }
    };

    // For fullband-only mode, show fullband = bands[3]
    const getBandCount = (mode) => {
        switch (mode) {
            case 0: return 1;  // fullband only → show index 3
            case 1: return 2;
            case 2: return 3;  // band1, band2, fullband
            case 3: return 3;
            case 4: return 4;
            default: return 1;
        }
    };

    // Map tab position → band index
    const tabToBandIdx = (tabPos, mode) => {
        if (mode === 0) return 3;   // fullband
        if (mode === 2 && tabPos === 2) return 3;   // 2band+full → tab2=fullband
        if (mode === 4 && tabPos === 3) return 3;   // 3band+full → tab3=fullband
        return tabPos;
    };

    const updateBandTabs = () => {
        // Always resolve activeBand to correct band index
        if (d.mode === 0) {
            d.activeBand = 3;  // fullband mode → always show band[3]
        } else {
            const count = getBandCount(d.mode);
            // If current activeBand is out of range, reset to first visible
            const validBandIdx = tabToBandIdx(0, d.mode);
            const validBands = Array.from({length: count}, (_, i) => tabToBandIdx(i, d.mode));
            if (!validBands.includes(d.activeBand)) d.activeBand = validBandIdx;
        }
        renderTabs();
        buildBandControls(d.activeBand);
        refreshGraph();
    };

    const updateCrossoverVisibility = () => {
        const needCf = d.mode !== 0;
        const need2Cf = d.mode === 3 || d.mode === 4;
        const needQ = d.cfType === 4;
        cfRow.style.display  = needCf ? '' : 'none';
        cf1Row.style.display = needCf ? '' : 'none';
        cf2Row.style.display = need2Cf ? '' : 'none';
        qLpLabel.style.display = qLpInp.style.display = needQ ? '' : 'none';
        qHpLabel.style.display = qHpInp.style.display = needQ ? '' : 'none';
    };

    updateCrossoverVisibility();
    updateBandTabs();
}

const BAND_COLORS_JS = [
    '#ff6b6b', '#ffa06b', '#ffd93d', '#6bcb77',
    '#4ecdc4', '#45b7d1', '#96c4ff', '#a78bfa',
    '#f472b6', '#fb923c'
];

// ─── EQ Band Panel ───────────────────────────────────────────────────

function buildEqBandPanel(container, moduleId, eqKey) {
    let eq;
    if (eqKey === 'eqLeft') eq = store.leftRightEq.eqLeft;
    else if (eqKey === 'eqRight') eq = store.leftRightEq.eqRight;
    else if (eqKey === 'eqLow') eq = store.dynamicEq.eqLow;
    else if (eqKey === 'eqHigh') eq = store.dynamicEq.eqHigh;
    else eq = store[eqKey];

    if (!eq) return;
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

    // Nếu accordion này đang mount graph, unmount trước để tránh memory leak
    const hadGraph = !!body.querySelector('.eq-graph-container');
    if (hadGraph) unmountGraph();

    body.innerHTML = '';

    const mod = ACCORDION_MODULES.find(m => m.id === moduleId);
    if (mod) buildModuleBody(body, mod);

    // Remount graph nếu accordion đang mở và trước đó có graph
    if (hadGraph && acc.classList.contains('open')) {
        mountGraphToAccordion(acc);
    }
}

// ─── EQ Hardware Sync ────────────────────────────────────────────────

function syncEqBand(moduleId, index) {
    let eq;
    let realIndex = index;
    if (moduleId === MODULE.EQ_DSP_1) eq = store.eq1;
    else if (moduleId === MODULE.EQ_DSP_2) eq = store.eq2;
    else if (moduleId === MODULE.LEFTRIGHT_EQ) {
        if (store.activeEq === 'eqRight') {
            eq = store.leftRightEq.eqRight;
            realIndex = index | 0x80;
        } else {
            eq = store.leftRightEq.eqLeft;
        }
    }
    if (!eq) return;

    const band = eq.bands[index];
    if (!band) return;
    const frame = buildSetEqBand(moduleId, realIndex, eq.pregain, band.enabled, band.type, band.freq, dbToQ88(band.gain), qToQ610(band.q));
    sendDebounced(`eq_${moduleId}_${realIndex}`, () => frame, 16);
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
    } else if (moduleId === MODULE.LEFTRIGHT_EQ) {
        const prev = store.activeEq;
        store.activeEq = 'eqLeft';
        store.leftRightEq.eqLeft.bands.forEach((_, i) => syncEqBand(moduleId, i));
        store.activeEq = 'eqRight';
        store.leftRightEq.eqRight.bands.forEach((_, i) => syncEqBand(moduleId, i));
        store.activeEq = prev;
    }
}

function syncAutoEqToHardware() {
    sendDebounced('auto_eq_bands', () => buildSetAutoEqTarget(store.autoEq.bands), 16);
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


// ─── Live Meter Widgets ───────────────────────────────────────────────────────
//
// buildDynMeter — level bar + zone indicator pills (Dynamic Bass & Dynamic EQ)
//   opts.id     : unique prefix for element IDs
//   opts.label  : label text
//   opts.zones  : [{ id, label, color }] — zone indicator pills
//
// buildGrMeter — gain reduction bar(s) (Compander & DRC)
//   opts.id        : unique prefix
//   opts.label     : label text
//   opts.multiband : if true, render 4 bars (B/M/H/Full)

function buildDynMeter(opts) {
    const wrap = document.createElement('div');
    wrap.className = 'lm-wrap';

    // Label + level value
    const head = document.createElement('div');
    head.className = 'lm-head';
    const lbl = document.createElement('span');
    lbl.className = 'lm-label';
    lbl.textContent = opts.label;
    const val = document.createElement('span');
    val.className = 'lm-value';
    val.id = `lm-${opts.id}-db`;
    val.textContent = '-- dB';
    head.appendChild(lbl);
    head.appendChild(val);
    wrap.appendChild(head);

    // Level bar track
    const track = document.createElement('div');
    track.className = 'lm-track';
    const fill = document.createElement('div');
    fill.className = 'lm-fill';
    fill.id = `lm-${opts.id}-fill`;
    track.appendChild(fill);
    wrap.appendChild(track);

    // Zone pills
    if (opts.zones && opts.zones.length) {
        const pills = document.createElement('div');
        pills.className = 'lm-zones';
        opts.zones.forEach(z => {
            const pill = document.createElement('span');
            pill.className = 'lm-zone-pill';
            pill.id = z.id;
            pill.textContent = z.label;
            pill.dataset.color = z.color;
            pills.appendChild(pill);
        });
        wrap.appendChild(pills);
    }

    return wrap;
}

function buildGrMeter(opts) {
    const wrap = document.createElement('div');
    wrap.className = 'lm-wrap';

    const head = document.createElement('div');
    head.className = 'lm-head';
    const lbl = document.createElement('span');
    lbl.className = 'lm-label';
    lbl.textContent = opts.label;
    const val = document.createElement('span');
    val.className = 'lm-value lm-value-red';
    val.id = `lm-${opts.id}-gr`;
    val.textContent = '0.0 dB';
    head.appendChild(lbl);
    head.appendChild(val);
    wrap.appendChild(head);

    if (opts.multiband) {
        // 4 bars: Low / Mid / High / Full
        const bandNames = ['Low', 'Mid', 'High', 'Full'];
        const rows = document.createElement('div');
        rows.className = 'lm-gr-rows';
        bandNames.forEach((name, i) => {
            const row = document.createElement('div');
            row.className = 'lm-gr-row';
            const rowLbl = document.createElement('span');
            rowLbl.className = 'lm-gr-row-label';
            rowLbl.textContent = name;
            const track = document.createElement('div');
            track.className = 'lm-track lm-track-sm';
            const fill = document.createElement('div');
            fill.className = 'lm-fill lm-fill-red';
            fill.id = `lm-${opts.id}-fill-${i}`;
            track.appendChild(fill);
            const rowVal = document.createElement('span');
            rowVal.className = 'lm-gr-row-val';
            rowVal.id = `lm-${opts.id}-val-${i}`;
            rowVal.textContent = '0.0';
            row.appendChild(rowLbl);
            row.appendChild(track);
            row.appendChild(rowVal);
            rows.appendChild(row);
        });
        wrap.appendChild(rows);
    } else {
        const track = document.createElement('div');
        track.className = 'lm-track';
        const fill = document.createElement('div');
        fill.className = 'lm-fill lm-fill-red';
        fill.id = `lm-${opts.id}-fill`;
        track.appendChild(fill);
        wrap.appendChild(track);
    }

    return wrap;
}

// ─── Render functions — called from frame handler ─────────────────────────────

function renderDynBassMeter(energyDb, alpha) {
    const fill  = document.getElementById('lm-dynbass-fill');
    const valEl = document.getElementById('lm-dynbass-db');
    if (!fill || !valEl) return;

    // Level bar: map -60..0 dBFS → 0..100%
    const pct = Math.max(0, Math.min(100, (energyDb + 60) / 60 * 100));
    fill.style.width = `${pct}%`;
    fill.style.background = energyDb > -6  ? 'var(--accent-red)'
                          : energyDb > -18 ? 'var(--accent-orange)'
                          :                  'var(--accent-green)';
    valEl.textContent = `${energyDb.toFixed(1)} dB`;

    // Zone pills: alpha [-1..+1]
    //   alpha > 0.05  → Boost active
    //   |alpha| < 0.05 → Neutral
    //   alpha < -0.05 → Protect active
    const boostPill  = document.getElementById('dynbass-zone-boost');
    const flatPill   = document.getElementById('dynbass-zone-flat');
    const clipPill   = document.getElementById('dynbass-zone-clip');
    if (!boostPill) return;

    const setActive = (el, active) => {
        el.style.background = active ? el.dataset.color : '';
        el.style.color      = active ? '#fff' : '';
        el.style.opacity    = active ? '1'   : '0.35';
    };
    setActive(boostPill, alpha >  0.05);
    setActive(flatPill,  Math.abs(alpha) <= 0.05);
    setActive(clipPill,  alpha < -0.05);
}

function renderDynEqMeter(energyDb, alphaLow, alphaHigh) {
    const fill  = document.getElementById('lm-dyneq-fill');
    const valEl = document.getElementById('lm-dyneq-db');
    if (!fill || !valEl) return;

    const pct = Math.max(0, Math.min(100, (energyDb + 60) / 60 * 100));
    fill.style.width = `${pct}%`;
    fill.style.background = energyDb > -6  ? 'var(--accent-red)'
                          : energyDb > -18 ? 'var(--accent-orange)'
                          :                  'var(--accent-green)';
    valEl.textContent = `${energyDb.toFixed(1)} dB`;

    const alphaFlat = Math.max(0, 1 - alphaLow - alphaHigh);
    const lowPill  = document.getElementById('dyneq-zone-low');
    const flatPill = document.getElementById('dyneq-zone-flat');
    const highPill = document.getElementById('dyneq-zone-high');
    if (!lowPill) return;

    const setActive = (el, active, strength) => {
        el.style.background = active ? el.dataset.color : '';
        el.style.color      = active ? '#fff' : '';
        el.style.opacity    = active ? String(0.4 + strength * 0.6) : '0.35';
    };
    setActive(lowPill,  alphaLow  > 0.02, alphaLow);
    setActive(flatPill, alphaFlat > 0.5,  alphaFlat);
    setActive(highPill, alphaHigh > 0.02, alphaHigh);
}

function renderCompanderMeter(envLinear, gainDb) {
    const fill  = document.getElementById('lm-compander-fill');
    const valEl = document.getElementById('lm-compander-gr');
    if (!fill || !valEl) return;

    // GR bar: gainDb is negative (reduction) → map 0..-24 dB → 0..100%
    const grPct = Math.max(0, Math.min(100, (-gainDb) / 24 * 100));
    fill.style.width = `${grPct}%`;
    valEl.textContent = gainDb <= 0
        ? `${gainDb.toFixed(1)} dB`
        : `+${gainDb.toFixed(1)} dB`;
}

function renderDrcMeter(gains) {
    // gains[0..3]: gain reduction dB per band (negative = reduction)
    const grLabel = document.getElementById('lm-drc-gr');
    if (grLabel) grLabel.textContent = `${gains[3].toFixed(1)} dB`;

    gains.forEach((g, i) => {
        const fill = document.getElementById(`lm-drc-fill-${i}`);
        const val  = document.getElementById(`lm-drc-val-${i}`);
        if (!fill) return;
        const pct  = Math.max(0, Math.min(100, (-g) / 24 * 100));
        fill.style.width = `${pct}%`;
        if (val) val.textContent = g.toFixed(1);
    });

    // Update operating point on the compression curve graph
    if (drcGraph) {
        const activeBand = store.drc.activeBand ?? 3;
        drcGraph.updateLiveMeter(gains[activeBand]);
    }
}

// ─── ISF Panel ───────────────────────────────────────────────────────

/**
 * Send all presets of one ISF instance to firmware.
 */
function syncIsfAllPresets(which) {
    const isf = store.getIsfInstance(which);
    const modId = store.getIsfModuleId(which);
    for (let p = 0; p < isf.numPresets; p++) {
        const preset = isf.presets[p];
        sendDebounced(`isf_${which}_preset_${p}`,
            () => buildSetIsfPreset(modId, p, { thresholdDb: preset.thresholdDb, pregainDb: preset.pregainDb}),
            30
        );
        for (let b = 0; b < 10; b++) {
            sendDebounced(`isf_${which}_preset_${p}_band_${b}`, () => buildSetIsfBandParams(modId, p, b, preset), 16);
        }
    }
    // Also send config
    sendDebounced(`isf_${which}_config`,
        () => buildSetIsfConfig(modId, isf.numPresets, isf.rmsMs, isf.slewMs, isf.overrideDb),
        30
    );
}

/**
 * Send one preset to firmware.
 */
function syncIsfPreset(which, presetIdx) {
    const isf  = store.getIsfInstance(which);
    const modId = store.getIsfModuleId(which);
    const preset = isf.presets[presetIdx];
    sendDebounced(`isf_${which}_preset_${presetIdx}`,
        () => buildSetIsfPreset(modId, presetIdx, { thresholdDb: preset.thresholdDb, pregainDb: preset.pregainDb}),
        30
    );
}

function syncIsfBandParams(which, presentIdx, bandIdx) {
    const isf = store.getIsfInstance(which);
    const modId = store.getIsfModuleId(which);
    const preset = isf.presets[presentIdx];

    if (bandIdx === undefined || bandIdx === null || bandIdx < 0) {
        return;
    }
    sendDebounced(`isf_${which}_preset_${presentIdx}_band_${bandIdx}`, () => buildSetIsfBandParams(modId, presentIdx, bandIdx, preset), 16);
}
/**
 * Update live level meter in ISF panel.
 */
function renderIsfLevelMeter(which, levelDb, slewIdx, activeA, activeB) {
    const suffix = which === 'isf1' ? '1' : '2';
    const meter  = document.getElementById(`isf-meter-${suffix}`);
    const label  = document.getElementById(`isf-level-label-${suffix}`);
    const idxLabel = document.getElementById(`isf-idx-label-${suffix}`);
    if (!meter || !label) return;

    // Level bar: map -60..0 dB → 0..100%
    const pct = Math.max(0, Math.min(100, (levelDb + 60) / 60 * 100));
    meter.style.width = `${pct}%`;
    meter.style.background = levelDb > -6 ? 'var(--accent-red)'
        : levelDb > -18 ? 'var(--accent-orange)'
        : 'var(--accent-green)';
    label.textContent = `${levelDb.toFixed(1)} dB`;
    if (idxLabel) {
        const blend = Math.round((slewIdx - Math.floor(slewIdx)) * 100);
        idxLabel.textContent = blend > 1
            ? `Preset ${activeA + 1} → ${activeB + 1} (${blend}%)`
            : `Preset ${activeA + 1}`;
    }

    // Highlight firmware-active preset (separate from editing-active)
    const tabContainer = document.getElementById(`isf-tabs-${suffix}`);
    if (!tabContainer) return;
    tabContainer.querySelectorAll('.isf-tab').forEach((tab, i) => {
        // fw-active = preset firmware đang dùng (theo RMS level)
        tab.classList.toggle('fw-active', i === activeA);
        // fw-next = preset sắp chuyển sang (smooth slew)
        tab.classList.toggle('fw-next',   i === activeB && activeA !== activeB);
        // Không đụng vào class 'active' — class đó dành cho preset đang edit
    });
}

/**
 * Build the complete ISF panel for one instance (isf1 or isf2).
 */
function buildIsfPanel(container, which) {
    const isf    = store.getIsfInstance(which);
    const suffix = which === 'isf1' ? '1' : '2';
    const modId  = store.getIsfModuleId(which);
    let currentPreset = 0;

    // ── Helper: compact labeled number input ─────────────────────────────
    const makeConfigItem = (labelText, val, min, max, step, unit, onChange) => {
        const item = document.createElement('div');
        item.className = 'isf-config-item';
        const lbl = document.createElement('span'); lbl.textContent = labelText;
        const inp = document.createElement('input');
        inp.type = 'number'; inp.min = min; inp.max = max;
        inp.step = step; inp.value = val; inp.className = 'num-input';
        inp.addEventListener('change', () => {
            const v = parseFloat(inp.value);
            if (!isNaN(v)) { inp.value = Math.max(min, Math.min(max, v)); onChange(parseFloat(inp.value)); }
        });
        const u = document.createElement('span'); u.className = 'num-unit'; u.textContent = unit;
        item.appendChild(lbl); item.appendChild(inp); item.appendChild(u);
        return { el: item, inp };
    };

    // ── Config row ────────────────────────────────────────────────────────
    const configRow = document.createElement('div');
    configRow.className = 'isf-config-row';
    const sendConfig = () =>
        sendFrame(buildSetIsfConfig(modId, isf.numPresets, isf.rmsMs, isf.slewMs, isf.overrideDb));

    const { el: rmsEl } = makeConfigItem('RMS Window', isf.rmsMs, 10, 2000, 10, 'ms', v => { isf.rmsMs = v; sendConfig(); });
    const { el: slewEl } = makeConfigItem('Slew Time', isf.slewMs, 10, 5000, 50, 'ms/step', v => { isf.slewMs = v; sendConfig(); });

    const ovItem = document.createElement('div'); ovItem.className = 'isf-config-item';
    const ovChk = document.createElement('input'); ovChk.type = 'checkbox'; ovChk.checked = isf.overrideDb !== null;
    const ovLbl = document.createElement('span'); ovLbl.textContent = 'Override';
    const ovInp = document.createElement('input');
    ovInp.type = 'number'; ovInp.min = -96; ovInp.max = 0; ovInp.step = 1;
    ovInp.value = isf.overrideDb ?? -20; ovInp.className = 'num-input'; ovInp.disabled = isf.overrideDb === null;
    const ovUnit = document.createElement('span'); ovUnit.className = 'num-unit'; ovUnit.textContent = 'dB';
    const applyOv = () => { isf.overrideDb = ovChk.checked ? parseFloat(ovInp.value) : null; ovInp.disabled = !ovChk.checked; sendConfig(); };
    ovChk.addEventListener('change', applyOv); ovInp.addEventListener('change', applyOv);
    ovItem.appendChild(ovChk); ovItem.appendChild(ovLbl); ovItem.appendChild(ovInp); ovItem.appendChild(ovUnit);
    configRow.appendChild(rmsEl); configRow.appendChild(slewEl); configRow.appendChild(ovItem);
    container.appendChild(configRow);

    // ── Live level meter ──────────────────────────────────────────────────
    const meterWrap = document.createElement('div'); meterWrap.className = 'isf-meter-wrap';
    const meterLbl = document.createElement('span'); meterLbl.className = 'isf-meter-label'; meterLbl.textContent = 'Level';
    const meterTrack = document.createElement('div'); meterTrack.className = 'isf-meter-track';
    const meterFill = document.createElement('div'); meterFill.className = 'isf-meter-fill'; meterFill.id = `isf-meter-${suffix}`;
    meterTrack.appendChild(meterFill);
    const levelLabel = document.createElement('span'); levelLabel.className = 'isf-level-label';
    levelLabel.id = `isf-level-label-${suffix}`; levelLabel.textContent = '-- dB';
    const idxLabel = document.createElement('span'); idxLabel.className = 'isf-idx-label';
    idxLabel.id = `isf-idx-label-${suffix}`; idxLabel.textContent = 'Preset 1';
    meterWrap.appendChild(meterLbl); meterWrap.appendChild(meterTrack);
    meterWrap.appendChild(levelLabel); meterWrap.appendChild(idxLabel);
    container.appendChild(meterWrap);

    // ── Tabs wrap ─────────────────────────────────────────────────────────
    const tabsWrap = document.createElement('div'); tabsWrap.className = 'isf-tabs-wrap';
    const tabActions = document.createElement('div'); tabActions.className = 'isf-tab-actions';
    const addPresetBtn = document.createElement('button');
    addPresetBtn.textContent = '+ Preset'; addPresetBtn.className = 'btn btn-outline btn-sm';
    const removePresetBtn = document.createElement('button');
    removePresetBtn.textContent = '− Preset'; removePresetBtn.className = 'btn btn-sm';
    removePresetBtn.style.color = 'var(--accent-red)';
    tabActions.appendChild(addPresetBtn); tabActions.appendChild(removePresetBtn);
    const tabBar = document.createElement('div');
    tabBar.className = 'isf-tab-bar'; tabBar.id = `isf-tabs-${suffix}`;

    // Preset content — persistent in DOM, only bandList.innerHTML rebuilds
    const presetContent = document.createElement('div'); presetContent.className = 'isf-preset-body';
    const metaRow = document.createElement('div'); metaRow.className = 'isf-preset-meta';
    const { el: thrEl, inp: thrInp } = makeConfigItem('Threshold', isf.presets[0].thresholdDb.toFixed(1), -96, 0, 0.5, 'dB', v => {
        isf.presets[currentPreset].thresholdDb = v;
        const tab = tabBar.querySelector(`.isf-tab[data-p="${currentPreset}"]`);
        if (tab) tab.title = `Threshold: ${v.toFixed(1)} dB`;
        syncIsfPreset(which, currentPreset);
    });
    const { el: pgEl, inp: pgInp } = makeConfigItem('Pregain', isf.presets[0].pregainDb.toFixed(1), -24, 24, 0.5, 'dB', v => {
        isf.presets[currentPreset].pregainDb = v;
        store.emit('isf:eq-changed');
        syncIsfPreset(which, currentPreset);
    });
    metaRow.appendChild(thrEl); metaRow.appendChild(pgEl);
    presetContent.appendChild(metaRow);

    const bandList = document.createElement('div'); bandList.className = 'isf-band-list';
    presetContent.appendChild(bandList);

    const actionsRow = document.createElement('div'); actionsRow.className = 'eq-actions';
    const addBandBtn = document.createElement('button');
    addBandBtn.textContent = '+ Add Band'; addBandBtn.className = 'btn btn-outline btn-sm';
    actionsRow.appendChild(addBandBtn);
    const clearBandsBtn = document.createElement('button');
    clearBandsBtn.textContent = 'Clear Bands'; clearBandsBtn.className = 'btn btn-sm';
    clearBandsBtn.style.color = 'var(--accent-red)';
    actionsRow.appendChild(clearBandsBtn);
    presetContent.appendChild(actionsRow);

    tabsWrap.appendChild(tabActions);
    tabsWrap.appendChild(tabBar);
    tabsWrap.appendChild(presetContent);
    container.appendChild(tabsWrap);

    // ── Sync + Refresh ────────────────────────────────────────────────────
    const syncRow = document.createElement('div'); syncRow.className = 'eq-actions';
    const syncAllBtn = document.createElement('button');
    syncAllBtn.textContent = '↑ Sync All Presets'; syncAllBtn.className = 'btn btn-outline btn-sm';
    syncAllBtn.addEventListener('click', () => { syncIsfAllPresets(which); showStatus(`ISF ${suffix} synced`, 'ok'); });
    const refreshBtn = document.createElement('button');
    refreshBtn.textContent = 'Refresh'; refreshBtn.className = 'btn btn-outline btn-sm';
    refreshBtn.addEventListener('click', () => sendFrame(buildGetIsfState()));
    syncRow.appendChild(syncAllBtn); syncRow.appendChild(refreshBtn);
    container.appendChild(syncRow);

    // ── renderBandList — chỉ rebuild band rows, không đụng phần còn lại ──
    const renderBandList = () => {
        bandList.innerHTML = '';
        const preset = isf.presets[currentPreset];
        let hasEnabled = false;
 
        preset.bands.forEach((band, realIdx) => {
            if (!band.enabled) return;
            hasEnabled = true;
 
            const row = buildBandRow(
                band,
                realIdx,
                (idx, changes) => {
                    Object.assign(band, changes);
                    store.emit('isf:eq-changed');
                    syncIsfBandParams(which, currentPreset, idx);
                },
                (idx) => {
                    band.enabled = false;
                    preset.numBands = preset.bands.filter(x => x.enabled).length;
                    Object.assign(band, { type: 0, freq: 1000, gain: 0, q: 0.707 });
                    store.emit('isf:eq-changed');
                    syncIsfBandParams(which, currentPreset, idx);
                    renderBandList();
                }
            );
            bandList.appendChild(row);
        });

        if (!hasEnabled) {
            const hint = document.createElement('p');
            hint.className = 'hint';
            hint.textContent = 'Double-click the graph to add bands, or click + Add Band';
            bandList.appendChild(hint);
        }
        addBandBtn.disabled = (preset.numBands >= 10);
        addBandBtn.textContent = (preset.numBands >= 10) ? "⚠ Max 10 bands" : "+ Add Band";
    };

    // ── switchPreset — cập nhật inputs + band list khi đổi tab ──────────
    const switchPreset = (pIdx) => {
        currentPreset = pIdx;
        store.setActiveIsfPreset(pIdx);
        const preset = isf.presets[pIdx];
        thrInp.value = preset.thresholdDb.toFixed(1);
        pgInp.value  = preset.pregainDb.toFixed(1);
        renderBandList();
        store.emit('isf:preset-selected', which, pIdx);
    };

    addBandBtn.addEventListener('click', () => {
        const preset = isf.presets[currentPreset];
        const slot = preset.bands.findIndex(x => !x.enabled);
        if (slot === -1) return;
        Object.assign(preset.bands[slot], { enabled: true, freq: 1000, gain: 0, q: 0.707, type: 0 });
        preset.numBands = preset.bands.filter(x => x.enabled).length;
        syncIsfBandParams(which, currentPreset, slot);
        store.emit('isf:eq-changed');
        renderBandList();
    });

    clearBandsBtn.addEventListener('click', () => {
        const preset = isf.presets[currentPreset];
        preset.bands.forEach(x => { x.enabled = false; x.freq = 1000; x.gain = 0; x.q = 0.707; x.type = 0; });
        preset.numBands = 0;
        store.emit('isf:eq-changed');
        for(let b = 0; b < 10; b++) {
            syncIsfBandParams(which, currentPreset, b);
        }
        renderBandList();
    });

    // ── Tab bar ──────────────────────────────────────────────────────────
    const rebuildTabs = () => {
        tabBar.innerHTML = '';
        for (let p = 0; p < isf.numPresets; p++) {
            const tab = document.createElement('button');
            tab.className = 'isf-tab btn' + (p === currentPreset ? ' active' : '');
            tab.textContent = `P${p + 1}`;
            tab.dataset.p = p;
            tab.title = `Preset ${p + 1}: threshold ${isf.presets[p].thresholdDb.toFixed(1)} dB`;
            tab.addEventListener('click', () => {
                tabBar.querySelectorAll('.isf-tab').forEach(t => t.classList.remove('active'));
                tab.classList.add('active');
                switchPreset(p);
            });
            tabBar.appendChild(tab);
        }
        addPresetBtn.disabled  = isf.numPresets >= 5;
        removePresetBtn.disabled = isf.numPresets <= 1;
    };


    addPresetBtn.addEventListener('click', () => {
        if (isf.numPresets >= 5) {
            addPresetBtn.disabled = true;
            addPresetBtn.textContent = "⚠ Max 5 presets";
            return;
        } else {
            addPresetBtn.textContent = "+ Preset";
        };
        const p = isf.numPresets++;
        isf.presets[p].thresholdDb = -96 + p * Math.round(96 / isf.numPresets);
        isf.presets[p].numBands = 0; isf.presets[p].pregainDb = 0;
        sendFrame(buildSetIsfConfig(modId, isf.numPresets, isf.rmsMs, isf.slewMs, isf.overrideDb));
        rebuildTabs(); switchPreset(p);
    });

    removePresetBtn.addEventListener('click', () => {
        if (isf.numPresets <= 1) {
            removePresetBtn.disabled = true;
            removePresetBtn.textContent = "⚠ At least 1 preset";
            return;
        } else {
            removePresetBtn.textContent = "− Preset";
        };
        isf.numPresets--;
        sendFrame(buildSetIsfConfig(modId, isf.numPresets, isf.rmsMs, isf.slewMs, isf.overrideDb));
        rebuildTabs(); switchPreset(Math.min(currentPreset, isf.numPresets - 1));
    });

    // ── Event listeners ───────────────────────────────────────────────────

    // Đang drag → update freq/gain inputs live (không rebuild toàn bộ list)
    store.on('isf:band-dragging', (w, pIdx2, bandArrayIdx) => {
        if (w !== which || pIdx2 !== currentPreset) return;
        const preset = isf.presets[pIdx2];
        const draggedBand = preset.bands[bandArrayIdx];
        const modid = store.getIsfModuleId(which);
 
        const activeAcc = document.querySelector(`.accordion[data-module-id="${modId}"].open`);
        if (!activeAcc) return;

        const row = activeAcc.querySelector(`.eq-band-row[data-band-index="${bandArrayIdx}"]`);
        if (row) {
            const fFreq = row.querySelector('input[data-field="freq"]');
            const fGain = row.querySelector('input[data-field="gain"]');
            const fQ = row.querySelector('input[data-field="q"]');
            const targetActiveElem = document.activeElement;

            if (fFreq && fFreq !== targetActiveElem) fFreq.value = draggedBand.freq;
            if (fGain && fGain !== targetActiveElem) fGain.value = draggedBand.gain.toFixed(1);
        }
        syncIsfBandParams(which, currentPreset, bandArrayIdx);
    });
    // Band added từ graph double-click
    store.on('isf:band-added', (w, pIdx2) => {
        if (w !== which) return;
        if (pIdx2 !== undefined && pIdx2 !== currentPreset) {
            tabBar.querySelectorAll('.isf-tab').forEach((t, i) => t.classList.toggle('active', i === pIdx2));
            switchPreset(pIdx2);
        } else {
            renderBandList();
        }
    });

    store.on('isf:preset-data-updated', (w, pIdx2) => {
        if (w !== which) return;
        // Rebuild tabs (thresholds may have changed)
        rebuildTabs();
        // If the updated preset is currently shown, refresh band list
        if (pIdx2 === currentPreset) {
            thrInp.value = isf.presets[currentPreset].thresholdDb.toFixed(1);
            pgInp.value  = isf.presets[currentPreset].pregainDb.toFixed(1);
            renderBandList();
        }
    });

    // ── Initial render ─────────────────────────────────────────────────
    rebuildTabs();
    switchPreset(0);
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
    if (moduleId === String(MODULE.ISF_1)) eqState = null;
    else if (moduleId === String(MODULE.ISF_2)) eqState = null;
    else if (moduleId === String(MODULE.EQ_DSP_1)) eqState = store.eq1;
    else if (moduleId === String(MODULE.EQ_DSP_2)) eqState = store.eq2;
    else if (moduleId === 'DYNEQ_LOW') eqState = store.dynamicEq.eqLow;
    else if (moduleId === 'DYNEQ_HIGH') eqState = store.dynamicEq.eqHigh;
    else if (moduleId === 'EQ_LEFT') eqState = store.leftRightEq.eqLeft;
    else if (moduleId === 'EQ_RIGHT') eqState = store.leftRightEq.eqRight;

    if (eqState) {
        addSlider(controls, 'Pregain', -2400, 2400, 50, 'dB',
            () => (eqState.pregain || 0) * 100,
            (v) => {
                eqState.pregain = v / 100;
                store.emit('eq:changed');
                // Sync via first enabled band or band 0
                let bandIdx = eqState.bands.findIndex(b => b.enabled);
                if (bandIdx === -1) bandIdx = 0;
                
                if (moduleId === String(MODULE.EQ_DSP_1) || moduleId === String(MODULE.EQ_DSP_2)) {
                    syncEqBand(parseInt(moduleId), bandIdx);
                } else if (moduleId === 'EQ_LEFT' || moduleId === 'EQ_RIGHT') {
                    syncEqBand(MODULE.LEFTRIGHT_EQ, bandIdx);
                } else {
                    syncDynEqBand(moduleId === 'DYNEQ_HIGH', bandIdx);
                }
            },
            null, 0.01);
    }

    // Initialize graph instance
    eqGraph = new EQGraph(canvas);
}

function updateCpuUI(usage, heapPct, fs) {
    const cpuContainer = document.getElementById('cpu-container');
    const cpuBar = document.getElementById('cpu-bar-fill');
    const cpuText = document.getElementById('cpu-value');
    const heapContainer = document.getElementById('heap-container');
    const heapBar = document.getElementById('heap-bar-fill');
    const heapText = document.getElementById('heap-value');
    const fsContainer = document.getElementById('fs-container');
    const fsText = document.getElementById('fs-value');

    if (!cpuContainer || !cpuBar || !cpuText) return;

    // Show containers if they were hidden
    if (store.system.connected) {
        if (cpuContainer.style.display === 'none') cpuContainer.style.display = 'flex';
        if (heapContainer && heapContainer.style.display === 'none') heapContainer.style.display = 'flex';
        if (fsContainer && fsContainer.style.display === 'none') fsContainer.style.display = 'flex';
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

    // Update Sample Rate
    if (fsContainer && fsText) {
        if (fs > 0) {
            fsText.textContent = `${(fs / 1000).toFixed(1)} kHz`;
            fsText.style.color = 'var(--accent-green)';
        } else {
            fsText.textContent = 'Clock Absent';
            fsText.style.color = 'var(--accent-red)';
        }
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
        btnDis.style.display = isBrowser ? 'none' : '';
        if (cpuContainer) cpuContainer.style.display = 'flex';
        if (heapContainer) heapContainer.style.display = 'flex';
        ['port-select', 'btn-refresh', 'btn-connect'].forEach(id => document.getElementById(id).style.display = 'none');
        document.getElementById('btn-wifi-config').style.display = 'inline-block';
    } else {
        badge.className = 'disconnected'; badge.id = 'connection-status';
        label.textContent = 'Disconnected';
        btnDis.style.display = 'none';
        if (cpuContainer) cpuContainer.style.display = 'none';
        if (heapContainer) heapContainer.style.display = 'none';
        if (manualMode) ['port-select', 'btn-refresh', 'btn-connect'].forEach(id => document.getElementById(id).style.display = '');
        document.getElementById('btn-wifi-config').style.display = 'none';
    }
}

// ─── WiFi UI Handlers ────────────────────────────────────────────────

function updateWifiUI() {
    const modeEl = document.getElementById('wifi-current-mode');
    if (modeEl) {
        modeEl.textContent = `Current: ${store.wifi.mode} (${store.wifi.ip})`;
    }
    
    // Only show wifi config button if connected and mode is known
    if (store.system.connected && store.wifi.mode !== 'Unknown') {
        document.getElementById('btn-wifi-config').style.display = 'inline-block';
        if (document.getElementById('btn-lobby-wifi')) {
            document.getElementById('btn-lobby-wifi').style.display = 'inline-block';
        }
    } else {
        document.getElementById('btn-wifi-config').style.display = 'none';
        if (document.getElementById('btn-lobby-wifi')) {
            document.getElementById('btn-lobby-wifi').style.display = 'none';
        }
    }
    
    if (store.wifi.mode === 'STA' && store.wifi.ip && store.wifi.ip !== '0.0.0.0') {
        if (!document.getElementById('scan-overlay').classList.contains('hidden')) {
            switchLobbyScreen('scan-wifi-connected');
            
            // Show reboot countdown if we just configured it
            if (isPendingStaReboot) {
                isPendingStaReboot = false;
                
                // Clear any old countdowns
                const oldMsg = document.getElementById('reboot-countdown');
                if (oldMsg) oldMsg.remove();
                
                const msgEl = document.createElement('div');
                msgEl.id = 'reboot-countdown';
                msgEl.style.color = 'var(--accent-red)';
                msgEl.style.fontWeight = 'bold';
                msgEl.style.textAlign = 'center';
                msgEl.style.marginTop = '15px';
                
                // Insert before the buttons
                const btnRow = document.getElementById('scan-wifi-connected').querySelector('div[style*="justify-content: center"]');
                if (btnRow) {
                    document.getElementById('scan-wifi-connected').insertBefore(msgEl, btnRow);
                } else {
                    document.getElementById('scan-wifi-connected').appendChild(msgEl);
                }
                
                let count = 5;
                msgEl.innerHTML = `
                    <div style="background: rgba(255,50,50,0.1); border: 1px solid var(--accent-red); padding: 15px; border-radius: 8px; margin-bottom: 15px;">
                        <div style="font-size: 16px; margin-bottom: 5px; color: var(--accent-red); font-weight: bold;">WiFi Config Saved!</div>
                        <div style="font-size: 14px; color: white;">Rebooting in <span style="color:var(--accent-red); font-size:18px;">${count}</span>s...</div>
                        <div style="margin-top: 10px; font-size: 12px; color: var(--text-dim);">
                            After reboot, connect your phone to <strong>${store.wifi.ssid}</strong> and access:<br/>
                            <strong style="color: var(--accent-green); font-size: 14px;">http://esp32-dsp.local</strong>
                        </div>
                    </div>
                `;
                
                const intv = setInterval(() => {
                    count--;
                    const countSpan = msgEl.querySelector('span');
                    if (count > 0) {
                        if (countSpan) countSpan.textContent = count;
                    } else {
                        clearInterval(intv);
                        msgEl.innerHTML = `
                            <div style="background: var(--accent-green); color: black; padding: 15px; border-radius: 8px; font-weight: bold; text-align: center;">
                                Rebooting...<br/>Please switch to WiFi: ${store.wifi.ssid}
                            </div>
                        `;
                    }
                }, 1000);
            }
        }
        
        document.getElementById('wifi-conn-mode').textContent = store.wifi.mode;
        document.getElementById('wifi-conn-ssid').textContent = store.wifi.ssid;
        document.getElementById('wifi-conn-ip').textContent = store.wifi.ip;
        document.getElementById('wifi-conn-rssi').textContent = `${store.wifi.rssi} dBm`;
        document.getElementById('wifi-conn-ws').textContent = `ws://${store.wifi.ip}/ws`;
    } else {
        // In AP mode or connecting — show config/scan screen instead of info screen
        if (!document.getElementById('scan-overlay').classList.contains('hidden')) {
            document.getElementById('scan-wifi-connected').style.display = 'none';
            // Only show config if not in other lobby screens
            if (document.getElementById('scan-auto-content').style.display === 'none' &&
                document.getElementById('scan-manual-content').style.display === 'none' &&
                document.getElementById('lobby-choice-content').style.display === 'none' &&
                document.getElementById('lobby-wifi-searching').style.display === 'none') {
                document.getElementById('scan-wifi-content').style.display = 'block';
            }
        }
    }
}

function renderWifiList() {
    const list = document.getElementById('wifi-network-list');
    if (!list) return;
    document.getElementById('wifi-scanning-text').style.display = 'none';
    list.innerHTML = '';
    
    if (store.wifi.scanResults.length === 0) {
        list.innerHTML = '<li style="text-align:center; padding: 10px; color: var(--text-dim);">No networks found</li>';
        return;
    }
    
    store.wifi.scanResults.forEach(net => {
        const li = document.createElement('li');
        li.style.padding = '8px';
        li.style.borderBottom = '1px solid var(--border)';
        li.style.cursor = 'pointer';
        li.style.display = 'flex';
        li.style.justifyContent = 'space-between';
        li.innerHTML = `<span>${net.ssid}</span> <span style="color:var(--text-dim); font-size:12px;">${net.rssi} dBm ${net.encrypted ? '🔒' : ''}</span>`;
        
        li.addEventListener('click', () => {
            document.getElementById('wifi-connect-box').style.display = 'block';
            document.getElementById('wifi-selected-ssid-label').textContent = `Connect to ${net.ssid}`;
            document.getElementById('wifi-selected-ssid').value = net.ssid;
            document.getElementById('wifi-pass').value = '';
            document.getElementById('wifi-pass').focus();
        });
        
        list.appendChild(li);
    });
}


function buildPresetLoad(idx) {
    store.setActivePreset(idx);
    MODULE_ORDER.forEach(id => store.setModuleEnabled(id, false));
    isFetchingState = true;
    sendFrame(buildLoadPreset(idx));
    showStatus(`Synchronizing Preset ${idx + 1}...`, 'info');
}

function buildPresetSave(event, idx) {
    event.preventDefault();
    store.setActivePreset(idx);
    sendFrame(buildSavePreset(idx));
}


// ─── Init ────────────────────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('btn-refresh').addEventListener('click', refreshPorts);
    document.getElementById('btn-connect').addEventListener('click', connectSerial);
    document.getElementById('btn-disconnect').addEventListener('click', async () => {
        if (store.system.transport === 'serial') await disconnectSerial();
        else await disconnectWebSocket();
        showConnectionLobby();
    });

    document.getElementById('btn-wifi-config').addEventListener('click', () => {
        document.getElementById('scan-overlay').classList.remove('hidden');
        document.getElementById('scan-auto-content').style.display = 'none';
        document.getElementById('scan-manual-content').style.display = 'none';
        document.getElementById('scan-wifi-content').style.display = 'block';
        document.getElementById('wifi-network-list').innerHTML = '';
        
        sendFrame(buildWifiGetStatus());
        
        if (store.wifi.mode === 'STA' && store.wifi.ip !== '0.0.0.0') {
            document.getElementById('wifi-scanning-text').style.display = 'none';
        } else {
            document.getElementById('wifi-scanning-text').style.display = 'block';
            sendFrame(buildWifiScan());
        }
    });

    // Connection Choice Buttons
    if (isBrowser) {
        const btnSerial = document.getElementById('btn-choice-serial');
        if (btnSerial) btnSerial.style.display = 'none';
    }

    document.getElementById('btn-choice-serial')?.addEventListener('click', () => {
        switchLobbyScreen('scan-manual-content');
        refreshPorts();
    });

    document.getElementById('btn-lobby-manual-back')?.addEventListener('click', showConnectionLobby);

    document.getElementById('btn-choice-wifi')?.addEventListener('click', async () => {
        switchLobbyScreen('lobby-wifi-searching');
        document.getElementById('wifi-search-spinner').style.display = 'block';
        document.getElementById('wifi-search-title').textContent = 'Searching Network';
        document.getElementById('wifi-search-status').textContent = 'Looking for esp32-dsp.local...';
        document.getElementById('wifi-search-fallback').style.display = 'none';
        document.getElementById('wifi-search-cancel-box').style.display = 'block';

        const tasks = [probeWebSocket('ws://esp32-dsp.local/ws', 3000)];
        const lastIp = localStorage.getItem('dsp_last_ip');
        if (lastIp) {
            document.getElementById('wifi-search-status').textContent = `Checking mDNS and last known IP (${lastIp})...`;
            tasks.push(probeWebSocket(`ws://${lastIp}/ws`, 3000));
        }
        
        const results = await Promise.all(tasks);
        
        if (results[0]) {
            await connectWebSocket('ws://esp32-dsp.local/ws', true);
        } else if (lastIp && results[1]) {
            await connectWebSocket(`ws://${lastIp}/ws`, true);
        } else {
            document.getElementById('wifi-search-spinner').style.display = 'none';
            document.getElementById('wifi-search-title').textContent = 'Device Not Found';
            document.getElementById('wifi-search-status').textContent = 'mDNS resolution failed or timed out.';
            document.getElementById('wifi-search-fallback').style.display = 'block';
            document.getElementById('wifi-search-cancel-box').style.display = 'none';
        }
    });

    document.getElementById('btn-wifi-auto-scan')?.addEventListener('click', async () => {
        const btn = document.getElementById('btn-wifi-auto-scan');
        const icon = document.getElementById('wifi-auto-scan-icon');
        const text = document.getElementById('wifi-auto-scan-text');
        const progress = document.getElementById('wifi-auto-scan-progress');
        
        btn.disabled = true;
        icon.innerHTML = '<div class="scan-spinner" style="width:14px;height:14px;border-width:2px;border-top-color:var(--accent-purple);"></div>';
        text.textContent = 'Scanning...';
        progress.style.display = 'block';
        progress.style.color = 'var(--accent-purple)';

        const subnets = ['192.168.1', '192.168.0', '10.0.0', '192.168.4'];
        let foundIp = null;

        for (const sub of subnets) {
            if (foundIp) break;
            progress.textContent = `Sweeping: ${sub}.x...`;
            
            // Batch requests to avoid overwhelming the browser
            for (let i = 1; i <= 254; i += 30) {
                if (foundIp) break;
                const batch = [];
                for (let j = 0; j < 30 && (i + j) <= 254; j++) {
                    const ip = `${sub}.${i + j}`;
                    batch.push(
                        probeWebSocket(`ws://${ip}/ws`, 1200).then(res => {
                            if (res && !foundIp) foundIp = ip;
                        })
                    );
                }
                await Promise.all(batch);
            }
        }

        if (foundIp) {
            progress.textContent = `Found DSP at ${foundIp}! Connecting...`;
            progress.style.color = 'var(--accent-green)';
            await connectWebSocket(`ws://${foundIp}/ws`);
        } else {
            progress.textContent = 'Sweep completed. No DSP Core found.';
            progress.style.color = 'var(--accent-red)';
            btn.disabled = false;
            icon.innerHTML = '🔍';
            text.textContent = 'Auto Scan Subnets';
        }
    });

    document.getElementById('btn-wifi-search-back')?.addEventListener('click', showConnectionLobby);
    document.getElementById('btn-wifi-search-cancel')?.addEventListener('click', showConnectionLobby);

    document.getElementById('btn-wifi-manual-connect')?.addEventListener('click', () => {
        const ip = document.getElementById('wifi-manual-ip').value.trim();
        if (ip) connectWebSocket(`ws://${ip}/ws`);
    });

    // Lobby buttons
    const lobbyManual = document.getElementById('btn-lobby-manual');
    if (lobbyManual) lobbyManual.addEventListener('click', () => {
        document.getElementById('scan-auto-content').style.display = 'none';
        document.getElementById('scan-manual-content').style.display = 'block';
    });

    const lobbyRefresh = document.getElementById('btn-lobby-refresh');
    if (lobbyRefresh) lobbyRefresh.addEventListener('click', refreshPorts);

    const lobbyConnect = document.getElementById('btn-lobby-connect');
    if (lobbyConnect) {
        lobbyConnect.addEventListener('click', async () => {
            const port = document.getElementById('lobby-port-select').value;
            if (!port) return;
            await abortScanAndWait();
            
            lobbyConnect.disabled = true;
            lobbyConnect.textContent = 'Verifying...';
            try {
                const isValid = await probePort(port);
                if (isValid) {
                    await window.serialAPI.connect(port, 115200);
                    onConnected(port);
                } else {
                    alert(`Device at ${port} is not recognized as a DSP Core.`);
                }
            } catch (e) {
                showStatus(`Connect failed: ${e}`, 'error');
                alert(`Connect failed: ${e}`);
            } finally {
                lobbyConnect.disabled = false;
                lobbyConnect.textContent = 'Connect';
            }
        });
    }

    window.serialAPI.onData((bytes) => parser.feed(bytes));
    window.serialAPI.onDisconnected(() => {
        if (store.system.transport !== 'serial') return; // Ignore if we switched transports
        store.setConnected(false); updateStatusUI();
        showStatus('Serial Disconnected', 'error');
        manualMode = false; startAutoScan();
    });

    // WebSocket events
    if (window.wsAPI) {
        window.wsAPI.onData((bytes) => parser.feed(bytes));
        window.wsAPI.onDisconnected(() => {
            if (store.system.transport !== 'websocket') return;
            store.setConnected(false); updateStatusUI();
            showStatus('WiFi Disconnected', 'error');
            
            if (isBrowser) {
                // Auto-reconnect on browser/mobile
                showStatus('Reconnecting to WiFi...', 'info');
                const host = window.location.hostname || '192.168.4.1';
                setTimeout(() => connectWebSocket(`ws://${host}/ws`, true), 1000);
            } else {
                manualMode = false; showConnectionLobby();
            }
        });
    }

    // WiFi Lobby UI bindings
    document.getElementById('btn-lobby-wifi')?.addEventListener('click', () => {
        document.getElementById('scan-auto-content').style.display = 'none';
        document.getElementById('scan-wifi-content').style.display = 'block';
        document.getElementById('wifi-network-list').innerHTML = '';
        document.getElementById('wifi-scanning-text').style.display = 'block';
        sendFrame(buildWifiGetStatus());
        sendFrame(buildWifiScan());
    });
    
    document.getElementById('btn-wifi-rescan')?.addEventListener('click', () => {
        document.getElementById('wifi-network-list').innerHTML = '';
        document.getElementById('wifi-scanning-text').style.display = 'block';
        sendFrame(buildWifiScan());
    });

    document.getElementById('btn-wifi-back')?.addEventListener('click', () => {
        if (store.system.connected) {
            document.getElementById('scan-overlay').classList.add('hidden');
        } else {
            showConnectionLobby();
        }
    });

    document.getElementById('btn-wifi-config')?.addEventListener('click', () => {
        document.getElementById('scan-overlay').classList.remove('hidden');
        
        // Pre-check state to avoid flicker
        if (store.wifi.mode === 'STA' && store.wifi.ip && store.wifi.ip !== '0.0.0.0') {
            switchLobbyScreen('scan-wifi-connected');
        } else {
            switchLobbyScreen('scan-wifi-content');
        }

        document.getElementById('wifi-network-list').innerHTML = '';
        document.getElementById('wifi-scanning-text').style.display = 'block';
        sendFrame(buildWifiGetStatus());
        sendFrame(buildWifiScan());
    });

    document.getElementById('btn-wifi-connect-cancel')?.addEventListener('click', () => {
        document.getElementById('wifi-connect-box').style.display = 'none';
    });

    document.getElementById('btn-wifi-connect-submit')?.addEventListener('click', () => {
        const ssid = document.getElementById('wifi-selected-ssid').value;
        const pass = document.getElementById('wifi-pass').value;
        sendFrame(buildWifiSetSTA(ssid, pass));
        isPendingStaReboot = true;
        document.getElementById('wifi-connect-box').style.display = 'none';
        showStatus('Sending WiFi credentials...', 'info');
    });

    document.getElementById('btn-wifi-set-ap')?.addEventListener('click', () => {
        if(confirm("Switch back to AP Mode?")) {
            sendFrame(buildWifiSetAP());
        }
    });

    document.getElementById('btn-wifi-ws-connect')?.addEventListener('click', () => {
        if (store.wifi.ip && store.wifi.ip !== '0.0.0.0') {
            connectWebSocket(`ws://${store.wifi.ip}/ws`);
        }
    });

    document.getElementById('btn-wifi-ws-back')?.addEventListener('click', () => {
        if (store.system.connected) {
            document.getElementById('scan-overlay').classList.add('hidden');
        } else {
            showConnectionLobby();
        }
    });

    document.getElementById('btn-wifi-connected-ap')?.addEventListener('click', () => {
        if(confirm("Switch back to AP Mode?")) {
            sendFrame(buildWifiSetAP());
        }
    });

    // Build UI
    buildAccordionModules();
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
        } else if (store.activeEq === 'autoEq') {
            syncAutoEqToHardware();
            renderAutoEqMeters();
        } else {
            const mid = store.getActiveEqModuleId();
            syncEqBand(mid, index);
        }

        // Scope DOM updates to the active accordion only
        const accId = store.activeEq === 'eq1' ? MODULE.EQ_DSP_1 :
            store.activeEq === 'eq2' ? MODULE.EQ_DSP_2 :
                store.activeEq === 'dynLow' ? 'DYNEQ_LOW' :
                    store.activeEq === 'dynHigh' ? 'DYNEQ_HIGH' :
                        store.activeEq === 'autoEq' ? MODULE.AUTO_EQ :
                            store.activeEq === 'eqLeft' ? 'EQ_LEFT' : 'EQ_RIGHT';
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

        const rebuildIds = [MODULE.ISF_1, MODULE.ISF_2, MODULE.EQ_DSP_1, MODULE.EQ_DSP_2, 'DYNEQ_LOW', 'DYNEQ_HIGH', 'EQ_LEFT', 'EQ_RIGHT', MODULE.DRC];
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

        if (activeAcc && activeAcc.dataset.moduleId !== String(MODULE.DRC)) {
            mountGraphToAccordion(activeAcc);
        }
    };

    store.on('state:loaded', rebuildStructural);
    store.on('eq:structure-changed', rebuildStructural);
    store.on('isf:instance-changed', (which) => {
        store.graphMode = which;
        if (eqGraph) eqGraph.redraw ? eqGraph.redraw() : null;
    });

    const btn1 = document.getElementById(`preset-0`);
    const btn2 = document.getElementById(`preset-1`);
    const btn3 = document.getElementById(`preset-2`);
    const btn4 = document.getElementById(`preset-3`);
    btn1.addEventListener('click', () => { buildPresetLoad(0); });
    btn2.addEventListener('click', () => { buildPresetLoad(1); });
    btn3.addEventListener('click', () => { buildPresetLoad(2); });
    btn4.addEventListener('click', () => { buildPresetLoad(3); });

    btn1.addEventListener('contextmenu', (e) => { buildPresetSave(e, 0); });
    btn2.addEventListener('contextmenu', (e) => { buildPresetSave(e, 1); });
    btn3.addEventListener('contextmenu', (e) => { buildPresetSave(e, 2); });
    btn4.addEventListener('contextmenu', (e) => { buildPresetSave(e, 3); });

    document.getElementById('btn-save-preset').addEventListener('click', () => {
        const idx = store.system.activePreset;
        sendFrame(buildSavePreset(idx));
        showStatus(`Saved to preset ${idx + 1}`, 'ok');
    });

    store.on('preset:active-changed', (idx) => {
        for (let i = 0; i < 4; i++) {
            const btn = document.getElementById(`preset-${i}`);
            if (btn) btn.classList.toggle('active', i === idx);
        }
    });

    setInterval(() => {
        if (!store.system.connected) return;
        sendFrame(buildFrame(CMD.GET_REPORT_CPU_USAGE, MODULE.SYSTEM));
    }, 2000);

    setInterval(() => {
        if (!store.system.connected) return;

        // ISF: firmware pushes data, client requests every 300ms
        const isf1Open = document.querySelector(`.accordion[data-module-id="${MODULE.ISF_1}"].open`);
        const isf2Open = document.querySelector(`.accordion[data-module-id="${MODULE.ISF_2}"].open`);
        if (isf1Open || isf2Open) sendFrame(buildGetIsfState());

        // Dynamic module meters — only poll when the accordion is open
        // Each returns a REPORT_* frame that renderXxxMeter() handles
        const dynModules = [
            { moduleId: MODULE.DYNAMIC_BASS, domId: MODULE.DYNAMIC_BASS },
            // Dynamic EQ meter shown on the threshold sub-tab (DYNEQ_THRESH)
            { moduleId: MODULE.DYNAMIC_EQ,   domId: 'DYNEQ_THRESH' },
            { moduleId: MODULE.COMPANDER,    domId: MODULE.COMPANDER },
            { moduleId: MODULE.DRC,          domId: MODULE.DRC },
        ];
        dynModules.forEach(({ moduleId, domId }) => {
            const isOpen = document.querySelector(`.accordion[data-module-id="${domId}"].open`);
            if (isOpen) sendFrame(buildGetModuleMeter(moduleId));
        });
    }, 300);

    if (isBrowser) {
        // Running in mobile browser, connect directly via WebSocket
        const host = window.location.hostname || '192.168.4.1';
        connectWebSocket(`ws://${host}/ws`);
    } else {
        // Show choice lobby instead of auto-scan
        showConnectionLobby();
    }
});