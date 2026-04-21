/**
 * @file preload.js
 * @brief Context bridge — exposes serial API to renderer safely
 */

const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('serialAPI', {
    listPorts:   ()                     => ipcRenderer.invoke('serial:list-ports'),
    connect:     (port, baud)           => ipcRenderer.invoke('serial:connect', port, baud),
    disconnect:  ()                     => ipcRenderer.invoke('serial:disconnect'),
    send:        (frameArray)           => ipcRenderer.invoke('serial:send', frameArray),
    isConnected: ()                     => ipcRenderer.invoke('serial:is-connected'),

    onData:         (cb) => ipcRenderer.on('serial:data', (_, data) => cb(data)),
    onError:        (cb) => ipcRenderer.on('serial:error', (_, msg) => cb(msg)),
    onDisconnected: (cb) => ipcRenderer.on('serial:disconnected', () => cb()),
});
