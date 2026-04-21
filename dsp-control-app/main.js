/**
 * @file main.js
 * @brief Electron main process — window creation and serial IPC
 */

const { app, BrowserWindow, ipcMain } = require('electron');
const path = require('path');
const { SerialPortManager } = require('./js/serial');

let mainWindow = null;
let serialManager = null;

function createWindow() {
    mainWindow = new BrowserWindow({
        width: 1400,
        height: 900,
        minWidth: 1100,
        minHeight: 650,
        backgroundColor: '#f0f2f5',
        titleBarStyle: 'hidden',
        titleBarOverlay: {
            color: '#ffffff',
            symbolColor: '#6b7280',
            height: 36
        },
        webPreferences: {
            preload: path.join(__dirname, 'preload.js'),
            nodeIntegration: false,
            contextIsolation: true
        }
    });

    mainWindow.loadFile('index.html');

    if (process.argv.includes('--dev')) {
        mainWindow.webContents.openDevTools({ mode: 'detach' });
    }
}

app.whenReady().then(() => {
    serialManager = new SerialPortManager();

    // ─── Serial IPC Handlers ───────────────────────────────────────

    ipcMain.handle('serial:list-ports', async () => {
        return await serialManager.listPorts();
    });

    ipcMain.handle('serial:connect', async (event, portPath, baudRate) => {
        return await serialManager.connect(portPath, baudRate || 115200);
    });

    ipcMain.handle('serial:disconnect', async () => {
        return await serialManager.disconnect();
    });

    ipcMain.handle('serial:send', async (event, frameArray) => {
        return await serialManager.send(Buffer.from(frameArray));
    });

    ipcMain.handle('serial:is-connected', () => {
        return serialManager.isConnected();
    });

    // Forward received data to renderer
    serialManager.onData((data) => {
        if (mainWindow && !mainWindow.isDestroyed()) {
            mainWindow.webContents.send('serial:data', Array.from(data));
        }
    });

    serialManager.onError((err) => {
        if (mainWindow && !mainWindow.isDestroyed()) {
            mainWindow.webContents.send('serial:error', err.message);
        }
    });

    serialManager.onDisconnect(() => {
        if (mainWindow && !mainWindow.isDestroyed()) {
            mainWindow.webContents.send('serial:disconnected');
        }
    });

    createWindow();

    app.on('activate', () => {
        if (BrowserWindow.getAllWindows().length === 0) createWindow();
    });
});

app.on('window-all-closed', () => {
    if (serialManager) serialManager.disconnect();
    if (process.platform !== 'darwin') app.quit();
});
