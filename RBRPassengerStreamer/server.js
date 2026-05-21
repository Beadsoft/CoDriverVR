import crypto from 'node:crypto';
import { execFile, spawn } from 'node:child_process';
import dgram from 'node:dgram';
import { appendFileSync, existsSync, readFileSync } from 'node:fs';
import http from 'node:http';
import { readFile, writeFile } from 'node:fs/promises';
import { extname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import os from 'node:os';
import { setTimeout as sleep } from 'node:timers/promises';

const root = fileURLToPath(new URL('.', import.meta.url));
const publicRoot = join(root, 'public');
const configPath = join(root, 'config.json');
const exampleConfigPath = join(root, 'config.example.json');
const openRbrVrTomlPath = process.env.OPENRBRVR_TOML ?? 'C:\\richard burns rally\\Plugins\\openRBRVR.toml';
const crashLogPath = join(root, 'streamer-crash.log');

function crashLog(message) {
  try {
    appendFileSync(crashLogPath, `[${new Date().toISOString()}] ${message}\n`);
  } catch {
  }
}

function readJson(path) {
  return JSON.parse(readFileSync(path, 'utf8').replace(/^\uFEFF/, ''));
}

function mergeConfig(base, override) {
  if (!override || typeof override !== 'object' || Array.isArray(override)) {
    return base;
  }
  const result = { ...base };
  for (const [key, value] of Object.entries(override)) {
    result[key] =
      value && typeof value === 'object' && !Array.isArray(value)
        ? mergeConfig(base?.[key] ?? {}, value)
        : value;
  }
  return result;
}

let config = mergeConfig(
  existsSync(exampleConfigPath) ? readJson(exampleConfigPath) : {},
  existsSync(configPath) ? readJson(configPath) : {},
);

const port = Number(process.env.RBR_PASSENGER_STREAM_PORT ?? config.server?.streamPort ?? 7790);
const poseHost = process.env.RBR_PASSENGER_POSE_HOST ?? config.server?.poseHost ?? '127.0.0.1';
const posePort = Number(process.env.RBR_PASSENGER_POSE_PORT ?? config.server?.posePort ?? 7791);
const udp = dgram.createSocket('udp4');
const clients = new Map();
const rooms = new Map();
let latestPose = null;
let electronHostProcess = null;
let pendingDriverRoomCommand = null;
let secureLanTunnelProcess = null;
let secureLanTunnelLog = [];
let secureLanTunnelError = '';
let driverShareControlToken = String(config.driverRoom?.shareControlToken ?? '');
if (!driverShareControlToken) {
  driverShareControlToken = crypto.randomBytes(24).toString('base64url');
  config = mergeConfig(config, { driverRoom: { shareControlToken: driverShareControlToken } });
  await writeFile(configPath, `${JSON.stringify(config, null, 2)}\n`, 'utf8');
}
let driverRoomStatus = {
  enabled: true,
  hostConnected: false,
  electronAvailable: false,
  running: false,
  mode: config.driverRoom?.defaultMode ?? 'secure-lan',
  roomId: '',
  passengerInviteUrl: '',
  driverShareUrl: '',
  inviteReady: false,
  passengerJoined: false,
  signalingConnected: false,
  peerState: 'not created',
  iceState: 'not created',
  directConnected: false,
  directFailed: false,
  captureSource: 'none',
  micEnabled: false,
  lastCommand: 'idle',
  lastError: '',
  summary: 'Stopped',
};

function withDriverShareControlToken(value) {
  if (!value) {
    return value;
  }
  try {
    const url = new URL(value);
    url.searchParams.set('control', driverShareControlToken);
    return url.toString();
  } catch {
    return value;
  }
}

function normalizeOrigin(value) {
  try {
    const url = new URL(value);
    url.pathname = '';
    url.search = '';
    url.hash = '';
    return url.toString().replace(/\/+$/, '');
  } catch {
    return '';
  }
}

process.on('uncaughtException', (error) => {
  crashLog(`[uncaughtException] ${error.stack ?? error.message ?? error}`);
  console.error('[fatal] uncaught exception', error);
});

process.on('unhandledRejection', (reason) => {
  crashLog(`[unhandledRejection] ${reason?.stack ?? reason}`);
  console.error('[fatal] unhandled rejection', reason);
});

process.on('exit', (code) => {
  crashLog(`[exit] code=${code}`);
});

udp.on('error', (error) => {
  console.error('[udp] pose socket error', error);
});

const mime = new Map([
  ['.html', 'text/html; charset=utf-8'],
  ['.js', 'text/javascript; charset=utf-8'],
  ['.css', 'text/css; charset=utf-8'],
  ['.svg', 'image/svg+xml'],
]);

function localAddresses() {
  return Object.values(os.networkInterfaces())
    .flat()
    .filter((entry) => entry && entry.family === 'IPv4' && !entry.internal)
    .map((entry) => entry.address);
}

function normalizeRemoteAddress(address) {
  return String(address ?? '').replace(/^::ffff:/, '');
}

function preferredLanAddress() {
  const preferred = config.lan?.preferAddress;
  if (preferred && preferred !== 'auto') {
    return preferred;
  }
  const addresses = localAddresses();
  return addresses.find((address) => address.startsWith('192.168.'))
    ?? addresses.find((address) => address.startsWith('10.'))
    ?? addresses.find((address) => /^172\.(1[6-9]|2\d|3[0-1])\./.test(address))
    ?? addresses[0]
    ?? '127.0.0.1';
}

function lanRoomServerUrl() {
  return `http://${preferredLanAddress()}:${Number(config.lan?.roomServerPort ?? port)}`;
}

function secureLanRoomServerUrl() {
  const tunnelUrl = normalizeOrigin(config.secureLan?.tunnelUrl ?? '');
  return tunnelUrl.startsWith('https://') ? tunnelUrl : '';
}

function modeRoomServerUrl(mode) {
  if (mode === 'secure-lan') {
    return secureLanRoomServerUrl();
  }
  if (mode === 'internet' && config.internet?.enabled === true) {
    return normalizeOrigin(config.internet?.roomServerUrl ?? '');
  }
  return lanRoomServerUrl();
}

function driverRoomPublicStatus() {
  const hostConnected = [...clients.values()].some((client) => client.role === 'driver-host');
  return {
    ...driverRoomStatus,
    enabled: config.driverRoom?.enabled !== false,
    hostConnected,
    mode: driverRoomStatus.mode || config.driverRoom?.defaultMode || 'lan',
    driverShareUrl: withDriverShareControlToken(driverRoomStatus.driverShareUrl),
    summary: driverRoomStatus.lastError || driverRoomStatus.summary || (driverRoomStatus.running ? 'Running' : 'Stopped'),
  };
}

function isLocalhostUrl(value) {
  try {
    const url = new URL(value);
    return ['127.0.0.1', 'localhost', '::1'].includes(url.hostname);
  } catch {
    return false;
  }
}

function defaultDriverRoomMode() {
  if (
    config.driverRoom?.defaultMode === 'secure-lan'
    && config.secureLan?.enabled === true
    && secureLanRoomServerUrl()
  ) {
    return 'secure-lan';
  }
  if (
    config.driverRoom?.defaultMode === 'internet'
    && config.internet?.enabled === true
    && config.internet?.roomServerUrl
    && !isLocalhostUrl(config.internet.roomServerUrl)
  ) {
    return 'internet';
  }
  return 'lan';
}

function useLocalhostQuestUrl() {
  return config.quest?.connectionMode === 'usb' && config.quest?.useAdbReverse === true;
}

function questLocalOrigin() {
  return useLocalhostQuestUrl() ? `http://127.0.0.1:${port}` : lanRoomServerUrl();
}

function electronExecutable() {
  const exe = join(root, 'node_modules', 'electron', 'dist', process.platform === 'win32' ? 'electron.exe' : 'electron');
  return existsSync(exe) ? exe : null;
}

function childProcessEnv(extra = {}) {
  const env = {};
  let pathValue = '';
  for (const [key, value] of Object.entries(process.env)) {
    if (key.toLowerCase() === 'path') {
      pathValue ||= value ?? '';
    } else {
      env[key] = value;
    }
  }
  if (pathValue) {
    env.Path = pathValue;
  }
  return { ...env, ...extra };
}

function launchElectronHost() {
  if ([...clients.values()].some((client) => client.role === 'driver-host')) {
    return { ok: true, alreadyConnected: true };
  }
  if (electronHostProcess && !electronHostProcess.killed) {
    return { ok: true, alreadyStarted: true };
  }
  const exe = electronExecutable();
  if (!exe) {
    return { ok: false, error: 'Electron is not installed. Run npm install in RBRPassengerStreamer or use the browser fallback.' };
  }
  try {
    electronHostProcess = spawn(exe, [root], {
      cwd: root,
      env: childProcessEnv({ RBR_PASSENGER_STREAM_PORT: String(port) }),
      stdio: 'ignore',
      windowsHide: true,
    });
  } catch (error) {
    return { ok: false, error: `Electron host failed to start: ${error.message}` };
  }
  electronHostProcess.on('error', (error) => {
    driverRoomStatus = {
      ...driverRoomStatus,
      hostConnected: false,
      lastError: `Electron host failed to start: ${error.message}`,
      summary: `Electron host failed to start: ${error.message}`,
    };
    electronHostProcess = null;
  });
  electronHostProcess.on('exit', () => {
    electronHostProcess = null;
  });
  return { ok: true, pid: electronHostProcess.pid };
}

function sendDriverRoomCommand(command, fields = {}) {
  const host = [...clients.values()].find((client) => client.role === 'driver-host');
  if (!host?.socket || host.socket.destroyed) {
    pendingDriverRoomCommand = { command, fields };
    return false;
  }
  send(host.socket, { type: 'driverRoomCommand', command, ...fields });
  pendingDriverRoomCommand = null;
  return true;
}

function wsAccept(key) {
  return crypto
    .createHash('sha1')
    .update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
    .digest('base64');
}

function encodeFrame(text) {
  const payload = Buffer.from(text);
  if (payload.length < 126) {
    return Buffer.concat([Buffer.from([0x81, payload.length]), payload]);
  }
  if (payload.length <= 0xffff) {
    const header = Buffer.alloc(4);
    header[0] = 0x81;
    header[1] = 126;
    header.writeUInt16BE(payload.length, 2);
    return Buffer.concat([header, payload]);
  }
  const header = Buffer.alloc(10);
  header[0] = 0x81;
  header[1] = 127;
  header.writeBigUInt64BE(BigInt(payload.length), 2);
  return Buffer.concat([header, payload]);
}

function send(socket, message) {
  if (socket.destroyed) {
    return;
  }
  try {
    socket.write(encodeFrame(JSON.stringify(message)));
  } catch (error) {
    console.warn('[ws] send failed', error.message);
    socket.destroy();
  }
}

function encodePong(payload = Buffer.alloc(0)) {
  if (payload.length > 125) {
    payload = payload.subarray(0, 125);
  }
  return Buffer.concat([Buffer.from([0x8a, payload.length]), payload]);
}

function decodeFrames(state, chunk) {
  state.buffer = Buffer.concat([state.buffer, chunk]);
  const messages = [];

  for (;;) {
    if (state.buffer.length < 2) {
      return messages;
    }

    const first = state.buffer[0];
    const second = state.buffer[1];
    const opcode = first & 0x0f;
    const masked = (second & 0x80) !== 0;
    let offset = 2;
    let length = second & 0x7f;

    if (length === 126) {
      if (state.buffer.length < offset + 2) return messages;
      length = state.buffer.readUInt16BE(offset);
      offset += 2;
    } else if (length === 127) {
      if (state.buffer.length < offset + 8) return messages;
      length = Number(state.buffer.readBigUInt64BE(offset));
      offset += 8;
    }

    const maskLength = masked ? 4 : 0;
    if (state.buffer.length < offset + maskLength + length) {
      return messages;
    }

    const mask = masked ? state.buffer.subarray(offset, offset + 4) : null;
    offset += maskLength;
    const payload = Buffer.from(state.buffer.subarray(offset, offset + length));
    state.buffer = state.buffer.subarray(offset + length);

    if (opcode === 0x8) {
      state.socket.end();
      return messages;
    }
    if (opcode === 0x9) {
      state.socket.write(encodePong(payload));
      continue;
    }
    if (opcode !== 0x1) {
      continue;
    }
    if (mask) {
      for (let i = 0; i < payload.length; i++) {
        payload[i] ^= mask[i % 4];
      }
    }
    messages.push(payload.toString('utf8'));
  }
}

function clientList() {
  return [...clients.values()].map((client) => ({ id: client.id, role: client.role }));
}

function broadcastStatus() {
  const status = { type: 'status', clients: clientList() };
  for (const client of clients.values()) {
    send(client.socket, status);
  }
}

function isLocalRequest(req) {
  const address = normalizeRemoteAddress(req.socket.remoteAddress);
  return address === '127.0.0.1' || address === '::1' || localAddresses().includes(address);
}

function corsHeaders() {
  return {
    'access-control-allow-origin': '*',
    'access-control-allow-methods': 'GET,POST,OPTIONS',
    'access-control-allow-headers': 'content-type',
  };
}

function sendJson(res, status, value) {
  res.writeHead(status, { 'content-type': 'application/json; charset=utf-8', ...corsHeaders() });
  res.end(JSON.stringify(value, null, 2));
}

function sendErrorJson(res, status, error) {
  sendJson(res, status, {
    error: error?.message ?? String(error),
    code: error?.code,
    stdout: error?.stdout ?? '',
    stderr: error?.stderr ?? '',
  });
}

async function readJsonBody(req) {
  const chunks = [];
  for await (const chunk of req) {
    chunks.push(chunk);
  }
  if (!chunks.length) {
    return {};
  }
  return JSON.parse(Buffer.concat(chunks).toString('utf8'));
}

async function saveConfig() {
  await writeFile(configPath, `${JSON.stringify(config, null, 2)}\n`, 'utf8');
}

function numberOrFallback(value, fallback) {
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function readOpenRbrVrConfig() {
  if (!existsSync(openRbrVrTomlPath)) {
    return {
      exists: false,
      path: openRbrVrTomlPath,
      passengerVR: {
        enabled: false,
        cameraOffset: [-0.55, 0.02, 0.05],
        cameraYawDegrees: 0,
        posePort,
      },
      roadbookVR: {
        enabled: false,
        lockHand: 'left',
        pageHand: 'right',
      },
    };
  }

  const text = readFileSync(openRbrVrTomlPath, 'utf8');
  const passengerSection = text.match(/^\[PassengerVR\]\s*(.*?)(?=^\[|$)/ms)?.[1] ?? '';
  const roadbookSection = text.match(/^\[RoadbookVR\]\s*(.*?)(?=^\[|$)/ms)?.[1] ?? '';
  const offset = passengerSection
    .match(/cameraOffset\s*=\s*\[([^\]]+)\]/)?.[1]
    ?.split(',')
    .map((value) => Number(value.trim()))
    .filter(Number.isFinite);

  return {
    exists: true,
    path: openRbrVrTomlPath,
    passengerVR: {
      enabled: /enabled\s*=\s*true/i.test(passengerSection),
      cameraOffset: offset?.length === 3 ? offset : [-0.55, 0.02, 0.05],
      cameraYawDegrees: Number(passengerSection.match(/cameraYawDegrees\s*=\s*([-0-9.]+)/)?.[1] ?? 0),
      renderMode: passengerSection.match(/renderMode\s*=\s*["']([^"']+)["']/)?.[1] ?? 'stereo',
      posePort: Number(passengerSection.match(/posePort\s*=\s*([0-9]+)/)?.[1] ?? posePort),
    },
    roadbookVR: {
      enabled: /enabled\s*=\s*true/i.test(roadbookSection),
      lockHand: roadbookSection.match(/lockHand\s*=\s*["']([^"']+)["']/)?.[1] ?? 'left',
      pageHand: roadbookSection.match(/pageHand\s*=\s*["']([^"']+)["']/)?.[1] ?? 'right',
    },
  };
}

function publicConfig() {
  return {
    ...config,
    lan: {
      ...(config.lan ?? {}),
      addresses: localAddresses(),
      roomServerUrl: lanRoomServerUrl(),
    },
    secureLan: {
      ...(config.secureLan ?? {}),
      tunnelUrl: secureLanRoomServerUrl(),
      running: Boolean(secureLanTunnelProcess),
      lastError: secureLanTunnelError,
      log: secureLanTunnelLog.slice(-8),
    },
    openRbrVr: readOpenRbrVrConfig(),
  };
}

async function saveOpenRbrVrPassengerConfig(passengerVR) {
  let text = existsSync(openRbrVrTomlPath) ? readFileSync(openRbrVrTomlPath, 'utf8') : '';
  const offset = [
    numberOrFallback(passengerVR?.cameraOffset?.[0], -0.55),
    numberOrFallback(passengerVR?.cameraOffset?.[1], 0.02),
    numberOrFallback(passengerVR?.cameraOffset?.[2], 0.05),
  ];
  const yaw = numberOrFallback(passengerVR?.cameraYawDegrees, 0);
  const renderMode = passengerVR?.renderMode === 'mono' ? 'mono' : 'stereo';
  const portNumber = Math.round(numberOrFallback(passengerVR?.posePort, posePort));
  const enabled = passengerVR?.enabled !== false;
  const section = [
    '[PassengerVR]',
    `enabled = ${enabled}`,
    `cameraOffset = [${offset.map((value) => Number(value).toFixed(3)).join(', ')}]`,
    `cameraYawDegrees = ${yaw.toFixed(3)}`,
    `renderMode = "${renderMode}"`,
    `streamHost = "0.0.0.0"`,
    `streamPort = ${port}`,
    `posePort = ${portNumber}`,
    `recenterKey = "QuestMenu"`,
    '',
  ].join('\r\n');

  if (/^\[PassengerVR\]\s*.*?(?=^\[|$)/ms.test(text)) {
    text = text.replace(/^\[PassengerVR\]\s*.*?(?=^\[|$)/ms, section);
  } else {
    text = `${text.trimEnd()}\r\n\r\n${section}`;
  }

  await writeFile(openRbrVrTomlPath, text, 'utf8');
  return readOpenRbrVrConfig();
}

function adbPath() {
  return config.quest?.adbPath && existsSync(config.quest.adbPath) ? config.quest.adbPath : 'adb';
}

function runAdb(args, timeoutMs = 10000) {
  return new Promise((resolve, reject) => {
    execFile(adbPath(), args, { timeout: timeoutMs, windowsHide: true }, (error, stdout, stderr) => {
      if (error) {
        error.stdout = stdout;
        error.stderr = stderr;
        reject(error);
        return;
      }
      resolve({ stdout, stderr });
    });
  });
}

function parseAdbDevices(output) {
  return output
    .split(/\r?\n/)
    .slice(1)
    .map((line) => line.trim())
    .filter(Boolean)
    .map((line) => {
      const [serial, state, ...details] = line.split(/\s+/);
      return {
        serial,
        state,
        details: details.join(' '),
        isWifi: /^\d{1,3}(\.\d{1,3}){3}:\d+$/.test(serial),
        selected:
          serial === config.quest?.serial ||
          serial === config.quest?.wifiSerial ||
          (config.quest?.connectionMode === 'wifi' && serial === config.quest?.wifiSerial),
      };
    });
}

function activeQuestSerial() {
  if (config.quest?.connectionMode === 'wifi' && config.quest?.wifiSerial) {
    return config.quest.wifiSerial;
  }
  return config.quest?.serial ?? '';
}

async function adbArgsForActiveQuest() {
  const serial = activeQuestSerial();
  if (serial && /^\d{1,3}(\.\d{1,3}){3}:\d+$/.test(serial)) {
    await runAdb(['connect', serial], 10000);
  }
  return serial ? ['-s', serial] : [];
}

function questViewerUrl() {
  const path = config.quest?.launchPath?.startsWith('/') ? config.quest.launchPath : `/${config.quest?.launchPath ?? 'quest.html'}`;
  return `${questLocalOrigin()}${path}`;
}

function secureLanTunnelStatus() {
  return {
    enabled: config.secureLan?.enabled === true,
    running: Boolean(secureLanTunnelProcess),
    tunnelUrl: secureLanRoomServerUrl(),
    lastError: secureLanTunnelError,
    log: secureLanTunnelLog.slice(-12),
  };
}

function cloudflaredExecutable() {
  const configured = String(config.secureLan?.cloudflaredPath || 'bin\\cloudflared.exe');
  const localPath = join(root, configured);
  return existsSync(localPath) ? localPath : configured;
}

async function saveSecureLanTunnelUrl(tunnelUrl) {
  const normalized = normalizeOrigin(tunnelUrl);
  if (!normalized.startsWith('https://')) {
    return;
  }
  config = mergeConfig(config, {
    secureLan: {
      enabled: true,
      tunnelUrl: normalized,
    },
  });
  await saveConfig();
}

function appendSecureLanTunnelLog(text) {
  const lines = String(text).split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
  for (const line of lines) {
    secureLanTunnelLog.push(line);
    const match = line.match(/https:\/\/[a-z0-9-]+\.trycloudflare\.com/i);
    if (match) {
      saveSecureLanTunnelUrl(match[0]).catch((error) => {
        secureLanTunnelError = error.message;
      });
    }
  }
  secureLanTunnelLog = secureLanTunnelLog.slice(-80);
}

async function startSecureLanTunnel() {
  if (secureLanTunnelProcess) {
    return secureLanTunnelStatus();
  }

  secureLanTunnelError = '';
  secureLanTunnelLog = [];
  const target = `http://127.0.0.1:${port}`;
  const child = spawn(cloudflaredExecutable(), ['tunnel', '--url', target], {
    cwd: root,
    windowsHide: true,
    env: childProcessEnv(),
  });
  secureLanTunnelProcess = child;

  child.stdout?.on('data', (chunk) => appendSecureLanTunnelLog(chunk));
  child.stderr?.on('data', (chunk) => appendSecureLanTunnelLog(chunk));
  child.on('error', (error) => {
    secureLanTunnelError = `cloudflared failed to start: ${error.message}`;
    secureLanTunnelProcess = null;
  });
  child.on('exit', (code) => {
    secureLanTunnelError = code === 0 ? '' : `cloudflared exited with code ${code}`;
    secureLanTunnelProcess = null;
  });

  const deadline = Date.now() + 20000;
  while (!secureLanRoomServerUrl() && !secureLanTunnelError && Date.now() < deadline) {
    await sleep(250);
  }
  return secureLanTunnelStatus();
}

async function stopSecureLanTunnel() {
  if (secureLanTunnelProcess) {
    secureLanTunnelProcess.kill();
    secureLanTunnelProcess = null;
  }
  return secureLanTunnelStatus();
}

async function handleLocalApi(req, res, url) {
  if (!isLocalRequest(req)) {
    sendJson(res, 403, { error: 'ADB and config controls are only available from localhost.' });
    return true;
  }

  if (url.pathname === '/api/adb/devices') {
    const { stdout, stderr } = await runAdb(['devices', '-l']);
    sendJson(res, 200, { devices: parseAdbDevices(stdout), stdout, stderr, config: config.quest ?? {} });
    return true;
  }

  if (url.pathname === '/api/quest/select' && req.method === 'POST') {
    const body = await readJsonBody(req);
    const mode = body.connectionMode === 'wifi' ? 'wifi' : 'usb';
    config = mergeConfig(config, {
      quest: {
        connectionMode: mode,
        serial: mode === 'usb' ? String(body.serial ?? config.quest?.serial ?? '') : config.quest?.serial,
        wifiSerial: mode === 'wifi' ? String(body.serial ?? body.wifiSerial ?? config.quest?.wifiSerial ?? '') : config.quest?.wifiSerial,
        useAdbReverse: mode === 'usb',
      },
    });
    await saveConfig();
    sendJson(res, 200, { ok: true, quest: config.quest });
    return true;
  }

  if (url.pathname === '/api/config' && req.method === 'POST') {
    const body = await readJsonBody(req);
    config = mergeConfig(config, body);
    await saveConfig();
    sendJson(res, 200, { ok: true, config });
    return true;
  }

  if (url.pathname === '/api/openrbrvr/config') {
    if (req.method === 'GET') {
      sendJson(res, 200, readOpenRbrVrConfig());
      return true;
    }
    if (req.method === 'POST') {
      const body = await readJsonBody(req);
      sendJson(res, 200, { ok: true, ...(await saveOpenRbrVrPassengerConfig(body.passengerVR ?? body)) });
      return true;
    }
  }

  if (url.pathname === '/api/adb/connect-wifi' && req.method === 'POST') {
    const body = await readJsonBody(req);
    const wifiSerial = String(body.wifiSerial ?? '').trim();
    if (!/^\d{1,3}(\.\d{1,3}){3}:\d+$/.test(wifiSerial)) {
      sendJson(res, 400, { error: 'wifiSerial must look like 192.168.1.50:5555.' });
      return true;
    }
    const result = await runAdb(['connect', wifiSerial], 10000);
    config = mergeConfig(config, { quest: { connectionMode: 'wifi', wifiSerial } });
    await saveConfig();
    sendJson(res, 200, { ok: true, wifiSerial, ...result, quest: config.quest });
    return true;
  }

  if (url.pathname === '/api/adb/enable-wifi' && req.method === 'POST') {
    const body = await readJsonBody(req);
    const usbSerial = String(body.usbSerial ?? config.quest?.serial ?? '').trim();
    const portNumber = Number(body.port ?? config.quest?.wifiPort ?? 5555);
    const usbArgs = usbSerial ? ['-s', usbSerial] : [];
    let questIp = String(body.questIp ?? '').trim();

    if (!questIp) {
      const route = await runAdb([...usbArgs, 'shell', 'ip', 'route'], 10000);
      const match = route.stdout.match(/\bsrc\s+(\d{1,3}(?:\.\d{1,3}){3})/);
      questIp = match?.[1] ?? '';
    }

    if (!questIp) {
      sendJson(res, 400, { error: 'Could not determine Quest WiFi IP. Enter it manually or connect the Quest by USB.' });
      return true;
    }

    const tcpip = await runAdb([...usbArgs, 'tcpip', String(portNumber)], 10000);
    await sleep(2000);
    const wifiSerial = `${questIp}:${portNumber}`;
    const connect = await runAdb(['connect', wifiSerial], 10000);
    config = mergeConfig(config, {
      quest: {
        connectionMode: 'wifi',
        serial: usbSerial || config.quest?.serial,
        wifiSerial,
        wifiPort: portNumber,
      },
    });
    await saveConfig();
    sendJson(res, 200, { ok: true, wifiSerial, tcpip, connect, quest: config.quest });
    return true;
  }

  if (url.pathname === '/api/adb/launch' && req.method === 'POST') {
    const adbArgs = await adbArgsForActiveQuest();
    if (useLocalhostQuestUrl()) {
      await runAdb([...adbArgs, 'reverse', `tcp:${port}`, `tcp:${port}`], 10000);
    }
    const urlToOpen = questViewerUrl();
    const result = await runAdb([...adbArgs, 'shell', 'am', 'start', '-a', 'android.intent.action.VIEW', '-d', urlToOpen], 10000);
    sendJson(res, 200, { ok: true, url: urlToOpen, result, quest: config.quest });
    return true;
  }

  if (url.pathname === '/api/driver-room/status' && req.method === 'GET') {
    sendJson(res, 200, driverRoomPublicStatus());
    return true;
  }

  if (url.pathname === '/api/secure-lan/status' && req.method === 'GET') {
    sendJson(res, 200, secureLanTunnelStatus());
    return true;
  }

  if (url.pathname === '/api/secure-lan/start' && req.method === 'POST') {
    const status = await startSecureLanTunnel();
    if (!status.tunnelUrl) {
      sendJson(res, 503, {
        ok: false,
        error: status.lastError || 'cloudflared did not produce an HTTPS tunnel URL. Install cloudflared or set secureLan.cloudflaredPath.',
        status,
      });
      return true;
    }
    sendJson(res, 200, { ok: true, status });
    return true;
  }

  if (url.pathname === '/api/secure-lan/stop' && req.method === 'POST') {
    sendJson(res, 200, { ok: true, status: await stopSecureLanTunnel() });
    return true;
  }

  if (url.pathname === '/api/driver-room/start' && req.method === 'POST') {
    const body = await readJsonBody(req);
    const launch = launchElectronHost();
    if (!launch.ok) {
      driverRoomStatus = { ...driverRoomStatus, hostConnected: false, lastError: launch.error, summary: launch.error };
      sendJson(res, 503, { ok: false, error: launch.error, status: driverRoomPublicStatus() });
      return true;
    }
    const mode = ['lan', 'secure-lan', 'internet'].includes(body.mode) ? body.mode : defaultDriverRoomMode();
    if (mode === 'secure-lan' && !secureLanRoomServerUrl()) {
      sendJson(res, 409, { ok: false, error: 'Start the HTTPS LAN tunnel before starting a secure-LAN room.', status: driverRoomPublicStatus() });
      return true;
    }
    sendDriverRoomCommand('start', { mode, mic: body.mic === true });
    sendJson(res, 202, { ok: true, command: 'start', status: driverRoomPublicStatus() });
    return true;
  }

  if (url.pathname === '/api/driver-room/stop' && req.method === 'POST') {
    sendDriverRoomCommand('stop');
    sendJson(res, 202, { ok: true, command: 'stop', status: driverRoomPublicStatus() });
    return true;
  }

  if (url.pathname === '/api/driver-room/recreate' && req.method === 'POST') {
    const body = await readJsonBody(req);
    const launch = launchElectronHost();
    if (!launch.ok) {
      sendJson(res, 503, { ok: false, error: launch.error, status: driverRoomPublicStatus() });
      return true;
    }
    const mode = ['lan', 'secure-lan', 'internet'].includes(body.mode) ? body.mode : driverRoomStatus.mode || defaultDriverRoomMode();
    if (mode === 'secure-lan' && !secureLanRoomServerUrl()) {
      sendJson(res, 409, { ok: false, error: 'Start the HTTPS LAN tunnel before recreating a secure-LAN room.', status: driverRoomPublicStatus() });
      return true;
    }
    sendDriverRoomCommand('recreate', { mode });
    sendJson(res, 202, { ok: true, command: 'recreate', status: driverRoomPublicStatus() });
    return true;
  }

  if (url.pathname === '/api/driver-room/mic' && req.method === 'POST') {
    sendDriverRoomCommand('mic');
    sendJson(res, 202, { ok: true, command: 'mic', status: driverRoomPublicStatus() });
    return true;
  }

  if (url.pathname === '/api/driver-room/share-on-quest' && req.method === 'POST') {
    const current = driverRoomPublicStatus();
    const invite = current.passengerInviteUrl;
    if (!invite) {
      sendJson(res, 409, { ok: false, error: 'Create a passenger room before opening the Quest share page.', status: current });
      return true;
    }
    const adbArgs = await adbArgsForActiveQuest();
    if (useLocalhostQuestUrl()) {
      await runAdb([...adbArgs, 'reverse', `tcp:${port}`, `tcp:${port}`], 10000);
    }
    const fallbackShareUrl = `${modeRoomServerUrl(current.mode) || questLocalOrigin()}/driver-share.html?invite=${encodeURIComponent(invite)}`;
    const urlToOpen = withDriverShareControlToken(current.driverShareUrl || fallbackShareUrl);
    const result = await runAdb([...adbArgs, 'shell', 'am', 'start', '-a', 'android.intent.action.VIEW', '-d', urlToOpen], 10000);
    sendJson(res, 200, { ok: true, url: urlToOpen, result, status: current });
    return true;
  }

  return false;
}

async function handleDriverSharePageApi(req, res, url) {
  if (url.pathname !== '/api/driver-room/close-share-page' || req.method !== 'POST') {
    return false;
  }

  const body = await readJsonBody(req);
  const token = String(body.token ?? url.searchParams.get('control') ?? '');
  if (!token || token !== driverShareControlToken) {
    sendJson(res, 403, { ok: false, error: 'Invalid share-page control token.' });
    return true;
  }

  sendJson(res, 202, { ok: true, command: 'close-share-page' });

  (async () => {
    try {
      const adbArgs = await adbArgsForActiveQuest();
      await runAdb([
        ...adbArgs,
        'shell',
        'am',
        'start',
        '-a',
        'android.intent.action.MAIN',
        '-c',
        'android.intent.category.LAUNCHER',
        '-n',
        'com.oculus.xrstreamingclient/.MainActivity',
      ], 10000);
      await sleep(700);
      await runAdb([...adbArgs, 'shell', 'am', 'force-stop', 'com.oculus.browser'], 10000);
    } catch (error) {
      console.warn(`[driver-share] Could not return Quest to Link: ${error.message}`);
    }
  })();

  return true;
}

function numberOrZero(value) {
  const number = Number(value);
  return Number.isFinite(number) ? number : 0;
}

function normalizePose(message) {
  return {
    yaw: numberOrZero(message.yaw),
    pitch: numberOrZero(message.pitch),
    roll: numberOrZero(message.roll),
  };
}

function mapPose(raw) {
  const axisMap = config.pose?.axisMap ?? {};
  const mapped = {};

  for (const axis of ['yaw', 'pitch', 'roll']) {
    const rule = axisMap[axis] ?? {};
    const source = ['yaw', 'pitch', 'roll'].includes(rule.source) ? rule.source : axis;
    const scale = numberOrZero(rule.scale ?? 1);
    const offset = numberOrZero(rule.offset ?? 0);
    mapped[axis] = raw[source] * scale + offset;
  }

  return mapped;
}

function sendUdpPose(mapped, source, raw = mapped) {
  const payload = `${mapped.yaw.toFixed(3)} ${mapped.pitch.toFixed(3)} ${mapped.roll.toFixed(3)}`;
  udp.send(Buffer.from(payload), posePort, poseHost);

  latestPose = {
    source,
    raw,
    mapped,
    udpPayload: payload,
    sentAt: new Date().toISOString(),
  };

  const message = { type: 'poseStatus', ...latestPose };
  for (const client of clients.values()) {
    send(client.socket, message);
  }
}

function sendUdpControl(payload, status) {
  udp.send(Buffer.from(payload), posePort, poseHost);
  latestPose = {
    source: status.source,
    raw: status.raw ?? null,
    mapped: status.mapped ?? null,
    udpPayload: payload,
    sentAt: new Date().toISOString(),
  };
  const message = { type: 'poseStatus', ...latestPose };
  for (const client of clients.values()) {
    send(client.socket, message);
  }
}

function sendUdpHandPose(message, source) {
  const side = message.side === 'right' ? 'right' : 'left';
  const valid = message.valid === false ? 0 : 1;
  const position = message.position ?? {};
  const orientation = message.orientation ?? {};
  const pinch = message.pinch === true ? 1 : 0;
  const values = [
    numberOrZero(position.x),
    numberOrZero(position.y),
    numberOrZero(position.z),
    numberOrZero(orientation.x),
    numberOrZero(orientation.y),
    numberOrZero(orientation.z),
    numberOrZero(orientation.w ?? 1),
  ].map((value) => value.toFixed(4));
  sendUdpControl(`handPose ${side} ${valid} ${values.join(' ')} ${pinch}`, {
    source,
    raw: { side, valid: Boolean(valid), position, orientation, pinch: Boolean(pinch) },
  });
}

function sendUdpRoadbook(message, source) {
  const allowed = new Set(['nextPage', 'previousPage', 'toggleVisible', 'resetPage']);
  if (!allowed.has(message.command)) {
    return;
  }
  sendUdpControl(`roadbook ${message.command}`, { source, raw: { command: message.command } });
}

function roomToken(bytes = 18) {
  return crypto.randomBytes(bytes).toString('base64url');
}

function hashPassword(password, salt = roomToken(12)) {
  if (!password) return null;
  const digest = crypto.scryptSync(String(password), salt, 32).toString('base64url');
  return { salt, digest };
}

function verifyPassword(password, stored) {
  if (!stored) return true;
  const candidate = hashPassword(password, stored.salt);
  return crypto.timingSafeEqual(Buffer.from(candidate.digest), Buffer.from(stored.digest));
}

function requestOrigin(req) {
  const host = req.headers['x-forwarded-host'] ?? req.headers.host ?? `127.0.0.1:${port}`;
  const proto = req.headers['x-forwarded-proto'] ?? 'http';
  return `${proto}://${host}`.replace(/\/+$/, '');
}

function roomPublic(room) {
  return {
    roomId: room.roomId,
    visibility: room.visibility,
    createdAt: room.createdAt,
    hasPassword: Boolean(room.passwordHash),
    driverConnected: Boolean(room.driver?.socket),
    passengerConnected: Boolean(room.passenger?.socket),
  };
}

function roomStatus(room) {
  return { type: 'room:status', ...roomPublic(room) };
}

function roomSend(client, message) {
  if (client?.socket) {
    send(client.socket, message);
  }
}

function roomBroadcast(room) {
  const status = roomStatus(room);
  roomSend(room.driver, status);
  roomSend(room.passenger, status);
}

function authenticateRoom(room, message) {
  if (message.role === 'driver') {
    return message.token === room.driverToken;
  }
  if (message.role === 'passenger') {
    const tokenOk = room.visibility === 'open' || message.token === room.passengerToken;
    return tokenOk && verifyPassword(message.password ?? '', room.passwordHash);
  }
  return false;
}

function joinRoom(state, message) {
  const room = rooms.get(state.roomId);
  if (!room) {
    send(state.socket, { type: 'room:error', error: 'Room not found.' });
    return;
  }
  if (!authenticateRoom(room, message)) {
    send(state.socket, { type: 'room:error', error: 'Room token or password was rejected.' });
    return;
  }
  if (message.role === 'passenger' && room.passenger?.socket && !room.passenger.socket.destroyed) {
    send(state.socket, { type: 'room:error', error: 'Passenger seat is already occupied.' });
    return;
  }
  state.role = message.role;
  if (message.role === 'driver') {
    room.driver = state;
    clearTimeout(room.expiryTimer);
  } else {
    room.passenger = state;
  }
  send(state.socket, { type: 'room:joined', room: roomPublic(room), role: message.role, iceServers: [{ urls: ['stun:stun.l.google.com:19302'] }] });
  if (message.role === 'passenger') {
    send(state.socket, { type: 'passenger:config', config: room.passengerConfig ?? {} });
  }
  roomBroadcast(room);
}

function relayRoomSignal(state, message) {
  const room = rooms.get(state.roomId);
  if (!room) return;
  const target = state.role === 'driver' ? room.passenger : room.driver;
  if (!target?.socket || target.socket.destroyed) {
    send(state.socket, { type: 'room:error', error: 'The other peer is not connected yet.' });
    return;
  }
  send(target.socket, { ...message, from: state.role });
}

function handleRoomMessage(state, message) {
  if (message.type === 'room:join') {
    joinRoom(state, message);
  } else if (message.type === 'room:leave') {
    state.socket.end();
  } else if (message.type === 'signal:offer' || message.type === 'signal:answer' || message.type === 'signal:candidate') {
    relayRoomSignal(state, message);
  }
}

function closeRoomSocket(state) {
  const room = rooms.get(state.roomId);
  if (!room) return;
  if (room.driver === state) {
    room.driver = null;
    clearTimeout(room.expiryTimer);
    room.expiryTimer = setTimeout(() => {
      const current = rooms.get(state.roomId);
      if (current && !current.driver?.socket) {
        roomSend(current.passenger, { type: 'room:error', error: 'Driver disconnected. Room expired.' });
        current.passenger?.socket?.end();
        rooms.delete(state.roomId);
      }
    }, 20000);
  } else if (room.passenger === state) {
    room.passenger = null;
  }
  if (rooms.has(state.roomId)) {
    roomBroadcast(room);
  }
}

function relay(from, message) {
  if (message.type === 'hello') {
    from.role = message.role;
    send(from.socket, { type: 'hello', id: from.id, role: from.role, clients: clientList(), config: publicConfig() });
    if (latestPose) {
      send(from.socket, { type: 'poseStatus', ...latestPose });
    }
    if (from.role === 'driver-host') {
      driverRoomStatus = { ...driverRoomStatus, hostConnected: true, summary: driverRoomStatus.summary || 'Driver host connected' };
      if (pendingDriverRoomCommand) {
        sendDriverRoomCommand(pendingDriverRoomCommand.command, pendingDriverRoomCommand.fields);
      }
    }
    broadcastStatus();
    return;
  }

  if (message.type === 'driverRoomStatus') {
    driverRoomStatus = {
      ...driverRoomStatus,
      ...message,
      hostConnected: true,
      updatedAt: new Date().toISOString(),
    };
    broadcastStatus();
    return;
  }

  if (message.type === 'pose') {
    const raw = normalizePose(message);
    sendUdpPose(mapPose(raw), from.role ?? 'raw', raw);
    return;
  }

  if (message.type === 'mappedPose') {
    const mapped = normalizePose(message);
    sendUdpPose(mapped, from.role ?? 'mapped', mapped);
    return;
  }

  if (message.type === 'handPose') {
    sendUdpHandPose(message, from.role ?? 'hand');
    return;
  }

  if (message.type === 'roadbook') {
    sendUdpRoadbook(message, from.role ?? 'roadbook');
    return;
  }

  for (const client of clients.values()) {
    if (client.id !== from.id) {
      send(client.socket, { ...message, from: from.role ?? from.id });
    }
  }
}

const server = http.createServer(async (req, res) => {
  try {
    const url = new URL(req.url ?? '/', `http://${req.headers.host}`);

    if (req.method === 'OPTIONS') {
      res.writeHead(204, corsHeaders());
      res.end();
      return;
    }

    if (url.pathname === '/api/config' && req.method === 'GET') {
      sendJson(res, 200, publicConfig());
      return;
    }

    if (url.pathname === '/api/status') {
      sendJson(res, 200, { clients: clientList(), latestPose });
      return;
    }

    if (url.pathname === '/api/rooms' && req.method === 'POST') {
      const body = await readJsonBody(req);
      const roomId = roomToken(8);
      const room = {
        roomId,
        visibility: body.visibility === 'open' ? 'open' : 'private',
        driverToken: roomToken(),
        passengerToken: roomToken(),
        passwordHash: hashPassword(body.password ?? ''),
        passengerConfig: body.passengerConfig && typeof body.passengerConfig === 'object' ? body.passengerConfig : {},
        createdAt: new Date().toISOString(),
        driver: null,
        passenger: null,
        expiryTimer: null,
      };
      rooms.set(roomId, room);
      sendJson(res, 200, {
        roomId,
        driverToken: room.driverToken,
        passengerInviteUrl: `${requestOrigin(req)}/join/${roomId}#token=${room.passengerToken}`,
        iceServers: [{ urls: ['stun:stun.l.google.com:19302'] }],
      });
      return;
    }

    if (url.pathname === '/api/rooms/open') {
      sendJson(res, 200, [...rooms.values()].filter((room) => room.visibility === 'open').map(roomPublic));
      return;
    }

    if (await handleDriverSharePageApi(req, res, url)) {
      return;
    }

    if (url.pathname.startsWith('/api/adb/') || url.pathname.startsWith('/api/driver-room/') || url.pathname.startsWith('/api/secure-lan/') || url.pathname === '/api/quest/select' || url.pathname === '/api/config' || url.pathname === '/api/openrbrvr/config') {
      try {
        if (await handleLocalApi(req, res, url)) {
          return;
        }
      } catch (error) {
        sendErrorJson(res, 500, error);
        return;
      }
    }

    const pathname = url.pathname.startsWith('/join/') ? '/passenger.html' : (url.pathname === '/' ? '/index.html' : url.pathname);
    const filePath = join(publicRoot, pathname.replace(/^\/+/, ''));
    const body = await readFile(filePath);
    res.writeHead(200, {
      'content-type': mime.get(extname(filePath)) ?? 'application/octet-stream',
      'cache-control': 'no-store',
    });
    res.end(body);
  } catch (error) {
    console.error('[http]', req.url, error);
    res.writeHead(404, { 'content-type': 'text/plain; charset=utf-8' });
    res.end('Not found');
  }
});

server.on('error', (error) => {
  console.error('[server]', error);
});

server.on('upgrade', (req, socket) => {
  const key = req.headers['sec-websocket-key'];
  if (!key) {
    socket.destroy();
    return;
  }

  socket.write(
    [
      'HTTP/1.1 101 Switching Protocols',
      'Upgrade: websocket',
      'Connection: Upgrade',
      `Sec-WebSocket-Accept: ${wsAccept(key)}`,
      '',
      '',
    ].join('\r\n'),
  );

  const url = new URL(req.url ?? '/', `http://${req.headers.host}`);
  const roomId = url.searchParams.get('roomId');
  const id = crypto.randomUUID();
  const state = { id, socket, role: 'unknown', buffer: Buffer.alloc(0), roomId };
  if (!roomId) {
    clients.set(id, state);
  }

  socket.on('data', (chunk) => {
    for (const text of decodeFrames(state, chunk)) {
      try {
        const message = JSON.parse(text);
        if (state.roomId) {
          handleRoomMessage(state, message);
        } else {
          relay(state, message);
        }
      } catch (error) {
        send(socket, { type: 'error', message: String(error) });
      }
    }
  });

  socket.on('error', (error) => {
    console.warn('[ws] socket error', error.message);
    if (state.roomId) {
      closeRoomSocket(state);
    } else {
      clients.delete(id);
      if (state.role === 'driver-host') {
        driverRoomStatus = { ...driverRoomStatus, hostConnected: false, summary: driverRoomStatus.running ? 'Driver host disconnected' : 'Stopped' };
      }
      broadcastStatus();
    }
    socket.destroy();
  });

  socket.on('close', () => {
    if (state.roomId) {
      closeRoomSocket(state);
    } else {
      clients.delete(id);
      if (state.role === 'driver-host') {
        driverRoomStatus = { ...driverRoomStatus, hostConnected: false, summary: driverRoomStatus.running ? 'Driver host disconnected' : 'Stopped' };
      }
      broadcastStatus();
    }
  });
});

server.listen(port, '0.0.0.0', () => {
  console.log(`RBR Passenger Streamer listening on http://127.0.0.1:${port}`);
  console.log(`Setup URL: http://127.0.0.1:${port}/setup.html`);
  console.log(`Harness URL: http://127.0.0.1:${port}/harness.html`);
  for (const address of localAddresses()) {
    console.log(`Quest URL: http://${address}:${port}/quest.html`);
    console.log(`Streamer URL: http://${address}:${port}/streamer.html`);
    console.log(`Harness URL: http://${address}:${port}/harness.html`);
    console.log(`LAN room server URL: http://${address}:${port}`);
  }
  if (config.secureLan?.autoStartTunnel === true) {
    startSecureLanTunnel()
      .then((status) => {
        if (status.tunnelUrl) {
          console.log(`HTTPS LAN tunnel URL: ${status.tunnelUrl}`);
        } else if (status.lastError) {
          console.warn(`[secure-lan] ${status.lastError}`);
        }
      })
      .catch((error) => console.warn(`[secure-lan] ${error.message}`));
  }
  if (config.driverRoom?.autoCreateOnStreamerStart === true) {
    const launch = launchElectronHost();
    if (!launch.ok) {
      console.warn(`[driver-room] ${launch.error}`);
    }
  }
  console.log(`Forwarding headset pose to ${poseHost}:${posePort}`);
  console.log(`Pose axis map: ${JSON.stringify(config.pose?.axisMap ?? {})}`);
});
