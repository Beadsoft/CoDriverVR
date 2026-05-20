import { setStatus } from './common.js';

const refreshButton = document.querySelector('#refresh');
const enableWifiButton = document.querySelector('#enable-wifi');
const connectWifiButton = document.querySelector('#connect-wifi');
const launchButton = document.querySelector('#launch');
const current = document.querySelector('#current');
const devicesElement = document.querySelector('#devices');
const wifiSerialInput = document.querySelector('#wifi-serial');
const wifiPortInput = document.querySelector('#wifi-port');
const cameraXInput = document.querySelector('#camera-x');
const cameraYInput = document.querySelector('#camera-y');
const cameraZInput = document.querySelector('#camera-z');
const cameraYawInput = document.querySelector('#camera-yaw');
const saveCameraButton = document.querySelector('#save-camera');
const panelWidthInput = document.querySelector('#panel-width');
const panelHeightInput = document.querySelector('#panel-height');
const panelDistanceInput = document.querySelector('#panel-distance');
const panelVerticalInput = document.querySelector('#panel-vertical');
const videoWidthInput = document.querySelector('#video-width');
const videoHeightInput = document.querySelector('#video-height');
const videoFpsInput = document.querySelector('#video-fps');
const videoBitrateInput = document.querySelector('#video-bitrate');
const posePredictionInput = document.querySelector('#pose-prediction');
const poseSmoothingInput = document.querySelector('#pose-smoothing');
const saveViewerButton = document.querySelector('#save-viewer');
const internetEnabledInput = document.querySelector('#internet-enabled');
const internetRoomServerInput = document.querySelector('#internet-room-server');
const internetRequirePublicInput = document.querySelector('#internet-require-public');
const testInternetButton = document.querySelector('#test-internet');
const saveInternetButton = document.querySelector('#save-internet');
const lanStatus = document.querySelector('#lan-status');
const saveLanButton = document.querySelector('#save-lan');
const handsEnabledInput = document.querySelector('#hands-enabled');
const handsPinchInput = document.querySelector('#hands-pinch');
const handsSwipeInput = document.querySelector('#hands-swipe');
const handsDebounceInput = document.querySelector('#hands-debounce');
const saveHandsButton = document.querySelector('#save-hands');
const status = document.querySelector('#status');

let questConfig = {};
let appConfig = {};
let openRbrVrConfig = {};
let devices = [];

async function api(path, options = {}) {
  const response = await fetch(path, {
    ...options,
    headers: {
      'content-type': 'application/json',
      ...(options.headers ?? {}),
    },
  });
  const body = await response.json();
  if (!response.ok) {
    throw new Error(body.error ?? `${response.status} ${response.statusText}`);
  }
  return body;
}

function selectedUsbSerial() {
  return document.querySelector('input[name="device"]:checked')?.value || questConfig.serial || '';
}

function numberFrom(input, fallback) {
  const value = Number(input.value);
  return Number.isFinite(value) ? value : fallback;
}

function render() {
  setStatus(current, [
    `mode: ${questConfig.connectionMode ?? 'wifi'}`,
    `usb serial: ${questConfig.serial || '(not set)'}`,
    `wifi serial: ${questConfig.wifiSerial || '(not set)'}`,
    `adb reverse: ${questConfig.useAdbReverse ? 'enabled' : 'disabled'}`,
    `launch path: ${questConfig.launchPath ?? '/quest.html'}`,
  ]);

  wifiSerialInput.value = questConfig.wifiSerial || wifiSerialInput.value;
  wifiPortInput.value = questConfig.wifiPort || wifiPortInput.value || 5555;
  const passenger = openRbrVrConfig.passengerVR ?? {};
  const offset = passenger.cameraOffset ?? [-0.55, 0.02, 0.05];
  cameraXInput.value = offset[0] ?? -0.55;
  cameraYInput.value = offset[1] ?? 0.02;
  cameraZInput.value = offset[2] ?? 0.05;
  cameraYawInput.value = passenger.cameraYawDegrees ?? 0;

  const quad = appConfig.viewer?.quad ?? {};
  panelWidthInput.value = quad.widthMeters ?? 1.7;
  panelHeightInput.value = quad.heightMeters ?? 0.96;
  panelDistanceInput.value = quad.distanceMeters ?? 1.35;
  panelVerticalInput.value = quad.verticalOffsetMeters ?? 0;

  const video = appConfig.video ?? {};
  videoWidthInput.value = video.width ?? 1280;
  videoHeightInput.value = video.height ?? 720;
  videoFpsInput.value = video.frameRate ?? 60;
  videoBitrateInput.value = video.maxBitrate ?? 8000000;

  const pose = appConfig.pose ?? {};
  posePredictionInput.value = pose.predictionMs ?? 70;
  poseSmoothingInput.value = pose.smoothing ?? 0.18;

  const internet = appConfig.internet ?? {};
  internetEnabledInput.checked = internet.enabled === true;
  internetRoomServerInput.value = internet.roomServerUrl ?? '';
  internetRequirePublicInput.checked = internet.requirePublicUrl !== false;

  setStatus(lanStatus, [
    `LAN room server: ${appConfig.lan?.roomServerUrl ?? '(not detected)'}`,
    `LAN addresses: ${(appConfig.lan?.addresses ?? []).join(', ') || '(none)'}`,
    `LAN invites are for same-WiFi testing. Quest WebXR may still require HTTPS or ADB reverse for immersive VR.`,
  ]);

  const hands = appConfig.handTracking ?? {};
  handsEnabledInput.checked = hands.enabled !== false;
  handsPinchInput.value = hands.pinchThresholdMeters ?? 0.035;
  handsSwipeInput.value = hands.swipeThresholdMeters ?? 0.14;
  handsDebounceInput.value = hands.debounceMs ?? 450;

  devicesElement.innerHTML = '';
  for (const device of devices) {
    const row = document.createElement('label');
    row.className = 'device-row';
    const radio = document.createElement('input');
    radio.type = 'radio';
    radio.name = 'device';
    radio.value = device.serial;
    radio.checked = device.selected;
    const text = document.createElement('span');
    text.textContent = `${device.serial}  ${device.state}  ${device.isWifi ? 'WiFi' : 'USB'}  ${device.details}`;
    const select = document.createElement('button');
    select.type = 'button';
    select.textContent = device.isWifi ? 'Use WiFi' : 'Use USB';
    select.addEventListener('click', async () => {
      await selectDevice(device);
    });
    row.append(radio, text, select);
    devicesElement.append(row);
  }
}

async function refreshConfig() {
  appConfig = await api('/api/config');
  openRbrVrConfig = await api('/api/openrbrvr/config');
  questConfig = appConfig.quest ?? questConfig;
  render();
}

async function refreshDevices() {
  setStatus(status, 'Refreshing ADB devices.');
  const result = await api('/api/adb/devices');
  devices = result.devices;
  questConfig = result.config;
  render();
  setStatus(status, result.stdout || 'No devices returned by adb.');
}

async function selectDevice(device) {
  setStatus(status, `Selecting ${device.serial}.`);
  const result = await api('/api/quest/select', {
    method: 'POST',
    body: JSON.stringify({
      connectionMode: device.isWifi ? 'wifi' : 'usb',
      serial: device.serial,
    }),
  });
  questConfig = result.quest;
  render();
  setStatus(status, `Selected ${device.serial}.`);
}

async function enableWifi() {
  const usbSerial = selectedUsbSerial();
  const port = Number(wifiPortInput.value || 5555);
  setStatus(status, `Enabling WiFi ADB from ${usbSerial || 'default USB device'}.`);
  const result = await api('/api/adb/enable-wifi', {
    method: 'POST',
    body: JSON.stringify({ usbSerial, port }),
  });
  questConfig = result.quest;
  wifiSerialInput.value = result.wifiSerial;
  setStatus(status, [
    `WiFi target: ${result.wifiSerial}`,
    result.tcpip?.stdout ?? '',
    result.connect?.stdout ?? '',
  ]);
  await refreshDevices();
}

async function connectWifi() {
  let wifiSerial = wifiSerialInput.value.trim();
  if (/^\d{1,3}(\.\d{1,3}){3}$/.test(wifiSerial)) {
    wifiSerial = `${wifiSerial}:${wifiPortInput.value || 5555}`;
  }
  setStatus(status, `Connecting ${wifiSerial}.`);
  const result = await api('/api/adb/connect-wifi', {
    method: 'POST',
    body: JSON.stringify({ wifiSerial }),
  });
  questConfig = result.quest;
  setStatus(status, result.stdout || `Connected ${wifiSerial}.`);
  await refreshDevices();
}

async function launchViewer() {
  setStatus(status, 'Launching Quest viewer.');
  const result = await api('/api/adb/launch', { method: 'POST', body: '{}' });
  questConfig = result.quest;
  setStatus(status, [`Opened ${result.url}`, result.result?.stdout ?? '']);
}

async function saveCamera() {
  setStatus(status, 'Saving passenger camera to openRBRVR.toml.');
  openRbrVrConfig = await api('/api/openrbrvr/config', {
    method: 'POST',
    body: JSON.stringify({
      passengerVR: {
        enabled: true,
        cameraOffset: [
          numberFrom(cameraXInput, -0.55),
          numberFrom(cameraYInput, 0.02),
          numberFrom(cameraZInput, 0.05),
        ],
        cameraYawDegrees: numberFrom(cameraYawInput, 0),
        renderMode: openRbrVrConfig.passengerVR?.renderMode ?? 'stereo',
        posePort: appConfig.server?.posePort ?? 7791,
      },
    }),
  });
  render();
  setStatus(status, 'Passenger camera saved. Restart RBR to apply it.');
}

async function saveViewer() {
  setStatus(status, 'Saving Quest view and latency settings.');
  const next = {
    viewer: {
      mode: appConfig.viewer?.mode ?? 'fullscreen',
      stereoLayout: appConfig.viewer?.stereoLayout ?? 'side-by-side',
      swapEyes: appConfig.viewer?.swapEyes === true,
      quad: {
        widthMeters: numberFrom(panelWidthInput, 1.7),
        heightMeters: numberFrom(panelHeightInput, 0.96),
        distanceMeters: numberFrom(panelDistanceInput, 1.35),
        verticalOffsetMeters: numberFrom(panelVerticalInput, 0),
      },
      flipY: appConfig.viewer?.flipY ?? false,
    },
    video: {
      ...(appConfig.video ?? {}),
      width: Math.round(numberFrom(videoWidthInput, 1280)),
      height: Math.round(numberFrom(videoHeightInput, 720)),
      frameRate: Math.round(numberFrom(videoFpsInput, 60)),
      maxBitrate: Math.round(numberFrom(videoBitrateInput, 8000000)),
    },
    pose: {
      ...(appConfig.pose ?? {}),
      predictionMs: numberFrom(posePredictionInput, 70),
      smoothing: numberFrom(poseSmoothingInput, 0.18),
    },
  };
  const result = await api('/api/config', { method: 'POST', body: JSON.stringify(next) });
  appConfig = result.config;
  render();
  setStatus(status, 'Settings saved. Refresh Quest and PC streamer pages to use them.');
}

async function saveInternet() {
  setStatus(status, 'Saving internet settings.');
  const result = await api('/api/config', {
    method: 'POST',
    body: JSON.stringify({
      internet: {
        ...(appConfig.internet ?? {}),
        enabled: internetEnabledInput.checked,
        roomServerUrl: internetRoomServerInput.value.trim(),
        requirePublicUrl: internetRequirePublicInput.checked,
      },
    }),
  });
  appConfig = result.config;
  render();
  setStatus(status, appConfig.internet?.enabled ? 'Internet passenger rooms enabled.' : 'Internet passenger rooms disabled.');
}

async function testInternet() {
  const baseUrl = internetRoomServerInput.value.trim().replace(/\/+$/, '');
  if (!baseUrl) {
    setStatus(status, 'Enter a room server URL first.');
    return;
  }
  setStatus(status, `Testing ${baseUrl}.`);
  const response = await fetch(`${baseUrl}/api/rooms/open`, { cache: 'no-store' });
  if (!response.ok) {
    throw new Error(`Room server test failed: ${response.status} ${response.statusText}`);
  }
  setStatus(status, `Room server reachable: ${baseUrl}`);
}

async function saveLan() {
  const result = await api('/api/config', {
    method: 'POST',
    body: JSON.stringify({
      lan: {
        ...(appConfig.lan ?? {}),
        enabled: true,
        roomServerPort: 7790,
        preferAddress: 'auto',
      },
    }),
  });
  appConfig = result.config;
  render();
  setStatus(status, 'LAN defaults saved.');
}

async function saveHands() {
  setStatus(status, 'Saving hand tracking settings.');
  const result = await api('/api/config', {
    method: 'POST',
    body: JSON.stringify({
      handTracking: {
        enabled: handsEnabledInput.checked,
        pinchThresholdMeters: numberFrom(handsPinchInput, 0.035),
        swipeThresholdMeters: numberFrom(handsSwipeInput, 0.14),
        debounceMs: Math.round(numberFrom(handsDebounceInput, 450)),
      },
    }),
  });
  appConfig = result.config;
  render();
  setStatus(status, 'Hand tracking settings saved.');
}

refreshButton.addEventListener('click', () => refreshDevices().catch((error) => setStatus(status, error.message)));
enableWifiButton.addEventListener('click', () => enableWifi().catch((error) => setStatus(status, error.message)));
connectWifiButton.addEventListener('click', () => connectWifi().catch((error) => setStatus(status, error.message)));
launchButton.addEventListener('click', () => launchViewer().catch((error) => setStatus(status, error.message)));
saveCameraButton.addEventListener('click', () => saveCamera().catch((error) => setStatus(status, error.message)));
saveViewerButton.addEventListener('click', () => saveViewer().catch((error) => setStatus(status, error.message)));
testInternetButton.addEventListener('click', () => testInternet().catch((error) => setStatus(status, error.message)));
saveInternetButton.addEventListener('click', () => saveInternet().catch((error) => setStatus(status, error.message)));
saveLanButton.addEventListener('click', () => saveLan().catch((error) => setStatus(status, error.message)));
saveHandsButton.addEventListener('click', () => saveHands().catch((error) => setStatus(status, error.message)));

refreshConfig()
  .then(refreshDevices)
  .catch((error) => setStatus(status, error.message));
