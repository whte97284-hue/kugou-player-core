// Preload script for Electron
// 这里可以暴露安全的 API 给渲染进程

const { contextBridge, ipcRenderer } = require('electron');

// 暴露给网页的 API
contextBridge.exposeInMainWorld('electronAPI', {
    // 获取应用版本
    getVersion: () => '2.1.0',

    // 检查是否在 Electron 环境中运行
    isElectron: () => true
});

console.log('Electron preload script loaded');
