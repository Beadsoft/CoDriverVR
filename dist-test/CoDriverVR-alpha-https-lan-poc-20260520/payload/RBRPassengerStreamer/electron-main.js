import { app, BrowserWindow, desktopCapturer, ipcMain } from 'electron';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const root = dirname(fileURLToPath(import.meta.url));
const streamPort = Number(process.env.RBR_PASSENGER_STREAM_PORT ?? 7790);
let windowRef = null;

function scoreSource(source) {
  const name = source.name.toLowerCase();
  let score = 0;
  if (name.includes('richard burns rally')) score += 100;
  if (name.includes('rbr')) score += 60;
  if (name.includes('openrbrvr')) score += 50;
  if (name.includes('passenger')) score += 40;
  if (name.includes('codrivervr')) score += 10;
  if (name.includes('internet driver') || name.includes('driver host')) score -= 80;
  if (name.includes('codex') || name.includes('browser')) score -= 30;
  return score;
}

async function findCaptureSource() {
  const sources = await desktopCapturer.getSources({
    types: ['window', 'screen'],
    thumbnailSize: { width: 320, height: 180 },
    fetchWindowIcons: false,
  });
  const sorted = sources
    .map((source) => ({ source, score: scoreSource(source) }))
    .sort((a, b) => b.score - a.score);
  return sorted[0]?.source ?? sources[0] ?? null;
}

function createWindow() {
  windowRef = new BrowserWindow({
    width: 960,
    height: 540,
    show: process.env.CODRIVERVR_SHOW_HOST === '1',
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      preload: join(root, 'electron-preload.cjs'),
    },
  });
  windowRef.setMenuBarVisibility(false);
  windowRef.loadURL(`http://127.0.0.1:${streamPort}/driver-host.html`);
}

ipcMain.handle('codrivervr:capture-source', async () => {
  const source = await findCaptureSource();
  if (!source) {
    return null;
  }
  return { id: source.id, name: source.name };
});

app.whenReady().then(createWindow);
app.on('window-all-closed', () => app.quit());
