const { app, BrowserWindow, ipcMain, shell } = require('electron');
const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');

let win = null;
let proxy = null; // child process handle

// Try to locate a FluxGate binary near the repo or on PATH.
function findBinary() {
  const candidates = [
    path.resolve(__dirname, '..', 'build', 'FluxGate'),
    path.resolve(__dirname, '..', 'build', 'fluxgate'),
    '/usr/local/bin/fluxgate',
    '/opt/homebrew/bin/fluxgate',
    path.join(app.getPath('home'), '.fluxgate', 'bin', 'fluxgate'),
  ];
  for (const c of candidates) {
    try { if (fs.existsSync(c)) return c; } catch (_) {}
  }
  return 'fluxgate'; // fall back to PATH lookup
}

function createWindow() {
  win = new BrowserWindow({
    width: 1180,
    height: 800,
    minWidth: 920,
    minHeight: 600,
    backgroundColor: '#070a11',
    icon: path.join(__dirname, 'assets', 'icon.png'),
    titleBarStyle: process.platform === 'darwin' ? 'hiddenInset' : 'default',
    trafficLightPosition: { x: 16, y: 18 },
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });
  win.loadFile(path.join(__dirname, 'renderer', 'index.html'));

  // Open external links (e.g. GitHub) in the system browser, not the app.
  win.webContents.setWindowOpenHandler(({ url }) => {
    shell.openExternal(url);
    return { action: 'deny' };
  });

  win.on('closed', () => { win = null; });
}

function sendLog(line, stream) {
  if (win && !win.isDestroyed()) win.webContents.send('proxy-log', { line, stream });
}
function sendState() {
  if (win && !win.isDestroyed())
    win.webContents.send('proxy-state', { running: !!proxy, pid: proxy ? proxy.pid : null });
}

ipcMain.handle('proxy:find-binary', () => findBinary());

ipcMain.handle('proxy:start', (_e, { binary, args }) => {
  if (proxy) return { ok: false, error: 'already running' };
  const bin = binary || findBinary();
  try {
    proxy = spawn(bin, args || [], { env: process.env });
  } catch (err) {
    return { ok: false, error: String(err) };
  }
  proxy.stdout.on('data', (d) => sendLog(d.toString(), 'out'));
  proxy.stderr.on('data', (d) => sendLog(d.toString(), 'err'));
  proxy.on('error', (err) => { sendLog('spawn error: ' + err.message + '\n', 'err'); proxy = null; sendState(); });
  proxy.on('exit', (code, sig) => {
    sendLog(`\n[fluxgate exited: code=${code} signal=${sig || '-'}]\n`, 'err');
    proxy = null;
    sendState();
  });
  sendState();
  return { ok: true, pid: proxy.pid, binary: bin };
});

ipcMain.handle('proxy:stop', () => {
  if (!proxy) return { ok: true };
  try { proxy.kill('SIGTERM'); } catch (_) {}
  return { ok: true };
});

ipcMain.handle('proxy:state', () => ({ running: !!proxy, pid: proxy ? proxy.pid : null }));

app.whenReady().then(() => {
  if (process.platform === 'darwin' && app.dock) {
    try { app.dock.setIcon(path.join(__dirname, 'assets', 'icon.png')); } catch (_) {}
  }
  createWindow();
});

app.on('window-all-closed', () => {
  if (proxy) { try { proxy.kill('SIGTERM'); } catch (_) {} }
  if (process.platform !== 'darwin') app.quit();
});
app.on('activate', () => { if (BrowserWindow.getAllWindows().length === 0) createWindow(); });
app.on('before-quit', () => { if (proxy) { try { proxy.kill('SIGTERM'); } catch (_) {} } });
