const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('coDriverVrHost', {
  captureSource: () => ipcRenderer.invoke('codrivervr:capture-source'),
});
