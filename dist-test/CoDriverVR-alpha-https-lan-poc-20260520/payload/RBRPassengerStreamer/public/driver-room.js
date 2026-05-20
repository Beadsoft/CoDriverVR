import { setStatus } from './common.js';

const startButton = document.querySelector('#start-room');
const startTunnelButton = document.querySelector('#start-tunnel');
const stopTunnelButton = document.querySelector('#stop-tunnel');
const startSecureRoomButton = document.querySelector('#start-secure-room');
const stopButton = document.querySelector('#stop-room');
const recreateButton = document.querySelector('#recreate-room');
const shareQuestButton = document.querySelector('#share-quest');
const copyInviteButton = document.querySelector('#copy-invite');
const openInviteLink = document.querySelector('#open-invite');
const inviteInput = document.querySelector('#invite');
const status = document.querySelector('#status');

let latestStatus = null;
let tunnelStatus = null;

function isLocalDriverPage() {
  return ['127.0.0.1', 'localhost', '[::1]'].includes(location.hostname);
}

async function api(path, options = {}) {
  const response = await fetch(path, {
    headers: { 'content-type': 'application/json', ...(options.headers ?? {}) },
    ...options,
  });
  const body = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(body.error ?? `${response.status} ${response.statusText}`);
  }
  return body;
}

function invite() {
  return latestStatus?.passengerInviteUrl ?? '';
}

function updateButtons() {
  const ready = Boolean(invite());
  const running = latestStatus?.running === true;
  const tunnelReady = Boolean(tunnelStatus?.tunnelUrl);
  startButton.disabled = running;
  startSecureRoomButton.disabled = running || !tunnelReady;
  stopButton.disabled = !running;
  recreateButton.disabled = false;
  shareQuestButton.disabled = !ready;
  copyInviteButton.disabled = !ready;
  openInviteLink.href = ready ? invite() : '#';
  openInviteLink.toggleAttribute('aria-disabled', !ready);
}

function preferredRoomMode() {
  if (latestStatus?.mode === 'secure-lan' || tunnelStatus?.tunnelUrl) {
    return 'secure-lan';
  }
  return latestStatus?.mode === 'internet' ? 'internet' : 'lan';
}

function renderStatus(extra = '') {
  inviteInput.value = invite();
  updateButtons();
  setStatus(status, [
    `mode: ${latestStatus?.mode ?? 'unknown'}`,
    `summary: ${latestStatus?.summary ?? 'unknown'}`,
    `host: ${latestStatus?.hostConnected ? 'connected' : 'offline'}`,
    `room: ${latestStatus?.roomId || '(none)'}`,
    `invite: ${latestStatus?.inviteReady ? 'ready' : 'not ready'}`,
    `passenger: ${latestStatus?.passengerJoined ? 'joined' : 'waiting'}`,
    `signaling: ${latestStatus?.signalingConnected ? 'connected' : 'offline'}`,
    `direct WebRTC: ${latestStatus?.directConnected ? 'connected' : latestStatus?.directFailed ? 'failed' : latestStatus?.peerState ?? 'not created'}`,
    `capture: ${latestStatus?.captureSource ?? 'none'}`,
    `https lan tunnel: ${tunnelStatus?.tunnelUrl || (tunnelStatus?.running ? 'starting' : 'not running')}`,
    tunnelStatus?.lastError ? `tunnel error: ${tunnelStatus.lastError}` : '',
    latestStatus?.lastError ? `error: ${latestStatus.lastError}` : '',
    extra,
  ].filter(Boolean));
}

async function refresh(extra = '') {
  if (!isLocalDriverPage()) {
    latestStatus = {
      mode: 'lan',
      summary: 'Open this control page on the driver PC at http://127.0.0.1:7790/internet-driver.html. Passenger headsets should use the invite link only.',
      hostConnected: false,
      running: false,
      inviteReady: false,
      passengerJoined: false,
      signalingConnected: false,
      peerState: 'not available from LAN page',
      captureSource: 'local driver controls only',
    };
    renderStatus('Driver controls are localhost-only by design.');
    return;
  }
  tunnelStatus = await api('/api/secure-lan/status').catch(() => tunnelStatus);
  latestStatus = await api('/api/driver-room/status');
  renderStatus(extra);
}

async function command(path, body = {}) {
  const result = await api(path, {
    method: 'POST',
    body: JSON.stringify(body),
  });
  latestStatus = result.status ?? latestStatus;
  renderStatus(`${result.command ?? 'Command'} sent.`);
  await new Promise((resolve) => setTimeout(resolve, 1200));
  await refresh();
}

async function copyInvite() {
  const value = invite();
  if (!value) {
    renderStatus('No invite is ready yet.');
    return;
  }
  inviteInput.focus();
  inviteInput.select();
  try {
    await navigator.clipboard.writeText(value);
    renderStatus('Invite copied.');
  } catch {
    renderStatus('Clipboard blocked; invite text is selected.');
  }
}

startButton.addEventListener('click', () => command('/api/driver-room/start', { mode: preferredRoomMode() }).catch((error) => renderStatus(error.message)));
startTunnelButton.addEventListener('click', async () => {
  try {
    const result = await api('/api/secure-lan/start', { method: 'POST', body: '{}' });
    tunnelStatus = result.status;
    renderStatus('HTTPS LAN tunnel ready.');
    await refresh();
  } catch (error) {
    renderStatus(error.message);
  }
});
stopTunnelButton.addEventListener('click', async () => {
  try {
    const result = await api('/api/secure-lan/stop', { method: 'POST', body: '{}' });
    tunnelStatus = result.status;
    renderStatus('HTTPS LAN tunnel stopped.');
    await refresh();
  } catch (error) {
    renderStatus(error.message);
  }
});
startSecureRoomButton.addEventListener('click', () => command('/api/driver-room/start', { mode: 'secure-lan' }).catch((error) => renderStatus(error.message)));
stopButton.addEventListener('click', () => command('/api/driver-room/stop').catch((error) => renderStatus(error.message)));
recreateButton.addEventListener('click', () => command('/api/driver-room/recreate', { mode: preferredRoomMode() }).catch((error) => renderStatus(error.message)));
shareQuestButton.addEventListener('click', () => command('/api/driver-room/share-on-quest').catch((error) => renderStatus(error.message)));
copyInviteButton.addEventListener('click', () => copyInvite().catch((error) => renderStatus(error.message)));

await refresh('Automatic driver room controls. This page uses the in-game/Electron capture path.');
setInterval(() => refresh().catch(() => {}), 1500);
