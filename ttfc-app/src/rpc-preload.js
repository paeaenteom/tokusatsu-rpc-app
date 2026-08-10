// ============================================================
//  TOKU RPC 미니 창 — preload (안전한 IPC 브릿지)
// ============================================================

const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('rpcAPI', {
  getStatus: () => ipcRenderer.invoke('rpc-get-status'),
  getSettings: () => ipcRenderer.invoke('rpc-get-settings'),
  setSetting: (key, value) => ipcRenderer.invoke('rpc-set-setting', key, value),
  toggleRPC: () => ipcRenderer.invoke('rpc-toggle'),
  reconnect: () => ipcRenderer.invoke('rpc-reconnect'),
  onStatus: (cb) => ipcRenderer.on('rpc-status-update', (e, s) => cb(s)),

  // 언어
  getI18n: () => ipcRenderer.invoke('i18n-get'),
  setLang: (setting) => ipcRenderer.invoke('i18n-set', setting),
  onLangChange: (cb) => ipcRenderer.on('lang-changed', (e, p) => cb(p)),

  // 콘솔 (에러 확인용)
  getConsole: () => ipcRenderer.invoke('console-get'),
  clearConsole: () => ipcRenderer.invoke('console-clear'),
  onConsoleLine: (cb) => ipcRenderer.on('console-line', (e, line) => cb(line)),
});
