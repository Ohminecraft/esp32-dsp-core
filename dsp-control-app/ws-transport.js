/**
 * @file ws-transport.js
 * @brief WebSocket transport layer for Electron main process
 */

const WebSocket = require('ws');

class WsTransport {
    constructor() {
        this.ws = null;
        this.url = '';
        this.onDataCb = null;
        this.onDisconnectCb = null;
    }

    connect(url) {
        return new Promise((resolve, reject) => {
            if (this.ws) {
                this.ws.close();
                this.ws = null;
            }

            this.url = url;
            try {
                this.ws = new WebSocket(url);
                this.ws.binaryType = 'arraybuffer';

                this.ws.on('open', () => {
                    resolve(true);
                });

                this.ws.on('message', (data) => {
                    if (this.onDataCb) {
                        // data is a Buffer in Node.js
                        this.onDataCb(new Uint8Array(data));
                    }
                });

                this.ws.on('close', () => {
                    if (this.onDisconnectCb) this.onDisconnectCb();
                    this.ws = null;
                });

                this.ws.on('error', (err) => {
                    reject(err);
                });
            } catch (e) {
                reject(e);
            }
        });
    }

    disconnect() {
        return new Promise((resolve) => {
            if (this.ws) {
                this.ws.close();
                this.ws = null;
            }
            resolve();
        });
    }

    send(dataArray) {
        return new Promise((resolve, reject) => {
            if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
                reject(new Error("WebSocket not connected"));
                return;
            }
            this.ws.send(new Uint8Array(dataArray), (err) => {
                if (err) reject(err);
                else resolve();
            });
        });
    }

    onData(cb) {
        this.onDataCb = cb;
    }

    onDisconnected(cb) {
        this.onDisconnectCb = cb;
    }
}

module.exports = WsTransport;
