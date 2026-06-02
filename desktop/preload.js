const { contextBridge, ipcRenderer } = require('electron');

// Minimal, safe bridge between the renderer UI and the main process.
contextBridge.exposeInMainWorld('fluxgate', {
  findBinary: () => ipcRenderer.invoke('proxy:find-binary'),
  startProxy: (opts) => ipcRenderer.invoke('proxy:start', opts),
  stopProxy: () => ipcRenderer.invoke('proxy:stop'),
  proxyState: () => ipcRenderer.invoke('proxy:state'),
  onLog: (cb) => ipcRenderer.on('proxy-log', (_e, d) => cb(d)),
  onState: (cb) => ipcRenderer.on('proxy-state', (_e, d) => cb(d)),
  platform: process.platform,
});
