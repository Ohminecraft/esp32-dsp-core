/**
 * @file serial.js
 * @brief Serial port manager for Electron main process
 */

class SerialPortManager {
    constructor() {
        this._port = null;
        this._dataCallback = null;
        this._errorCallback = null;
        this._disconnectCallback = null;
        this._SerialPort = null;
    }

    async _loadSerialPort() {
        if (!this._SerialPort) {
            const { SerialPort } = require('serialport');
            this._SerialPort = SerialPort;
        }
    }

    async listPorts() {
        await this._loadSerialPort();
        const { SerialPort } = require('serialport');
        const ports = await SerialPort.list();
        return ports.map(p => ({
            path: p.path,
            manufacturer: p.manufacturer || '',
            vendorId: p.vendorId || '',
            productId: p.productId || ''
        }));
    }

    async connect(portPath, baudRate = 115200) {
        await this._loadSerialPort();
        if (this._port && this._port.isOpen) {
            await this.disconnect();
        }

        return new Promise((resolve, reject) => {
            this._port = new this._SerialPort({
                path: portPath,
                baudRate: baudRate,
                dataBits: 8,
                stopBits: 1,
                parity: 'none',
                autoOpen: false
            });

            this._port.open((err) => {
                if (err) {
                    reject(err.message);
                    return;
                }

                this._port.on('data', (data) => {
                    if (this._dataCallback) this._dataCallback(data);
                });

                this._port.on('error', (err) => {
                    if (this._errorCallback) this._errorCallback(err);
                });

                this._port.on('close', () => {
                    if (this._disconnectCallback) this._disconnectCallback();
                });

                resolve(true);
            });
        });
    }

    async disconnect() {
        if (this._port && this._port.isOpen) {
            return new Promise((resolve) => {
                this._port.close(() => resolve(true));
            });
        }
        return true;
    }

    async send(buffer) {
        if (!this._port || !this._port.isOpen) return false;
        return new Promise((resolve, reject) => {
            this._port.write(buffer, (err) => {
                if (err) reject(err.message);
                else resolve(true);
            });
        });
    }

    isConnected() {
        return this._port ? this._port.isOpen : false;
    }

    onData(cb)       { this._dataCallback = cb; }
    onError(cb)      { this._errorCallback = cb; }
    onDisconnect(cb) { this._disconnectCallback = cb; }
}

module.exports = { SerialPortManager };
