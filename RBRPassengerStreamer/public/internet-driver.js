import { connectLocalSignaling, setStatus } from './common.js';

const serverUrlInput = document.querySelector('#server-url');
const useLanServerButton = document.querySelector('#use-lan-server');
const usePublicServerButton = document.querySelector('#use-public-server');
const visibilityInput = document.querySelector('#visibility');
const passwordInput = document.querySelector('#password');
const createButton = document.querySelector('#create');
const patternButton = document.querySelector('#pattern');
const captureButton = document.querySelector('#capture');
const micButton = document.querySelector('#mic');
const muteButton = document.querySelector('#mute');
const connectButton = document.querySelector('#connect');
const inviteInput = document.querySelector('#invite');
const copyInviteButton = document.querySelector('#copy-invite');
const shareInviteButton = document.querySelector('#share-invite');
const messengerInviteButton = document.querySelector('#messenger-invite');
const preview = document.querySelector('#preview');
const remoteAudio = document.querySelector('#remote-audio');
const status = document.querySelector('#status');

let config = {};
let room = null;
let ws = null;
let pc = null;
let localBridge = null;
let videoStream = null;
let micStream = null;
let dataChannel = null;
let patternFrame = 0;
let joinedRoom = false;
let offerSent = false;
let peerHint = 'not started';

function updateStatus(extra = '') {
  setStatus(status, [
    `room: ${room?.roomId ?? 'not created'}`,
    `signaling: ${ws?.readyState === WebSocket.OPEN ? 'connected' : 'offline'}`,
    `peer: ${pc?.connectionState ?? 'not created'} (${peerHint})`,
    `video: ${videoStream ? videoStream.getVideoTracks().map((track) => track.label).join(', ') : 'not selected'}`,
    `mic: ${micStream ? (micStream.getAudioTracks()[0]?.enabled ? 'enabled' : 'muted') : 'off'}`,
    `data: ${dataChannel?.readyState ?? 'not created'}`,
    extra,
  ].filter(Boolean));
}

async function loadConfig() {
  const response = await fetch('/api/config', { cache: 'no-store' });
  config = await response.json();
  serverUrlInput.value = config.internet?.enabled ? (config.internet?.roomServerUrl ?? '') : (config.lan?.roomServerUrl ?? config.internet?.roomServerUrl ?? '');
}

function isLocalRoomServer(value) {
  try {
    const url = new URL(value);
    return ['localhost', '127.0.0.1', '::1'].includes(url.hostname);
  } catch {
    return false;
  }
}

function assertPublicRoomServer(baseUrl) {
  if (!baseUrl) {
    throw new Error('Set a public room server URL first.');
  }
  const usingLanDefault = baseUrl === config.lan?.roomServerUrl;
  if (config.internet?.requirePublicUrl === true && !usingLanDefault && isLocalRoomServer(baseUrl)) {
    throw new Error('Internet mode needs a public room server URL. Deploy the Cloudflare Worker and use its workers.dev URL.');
  }
  if (config.internet?.requirePublicUrl === true && !usingLanDefault && !baseUrl.startsWith('https://')) {
    throw new Error('Internet mode needs an HTTPS room server URL.');
  }
}

function remoteWsUrl(baseUrl, roomId) {
  const url = new URL(baseUrl);
  url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
  url.pathname = '/signal';
  if (roomId) {
    url.searchParams.set('roomId', roomId);
  }
  url.hash = '';
  return url.toString();
}

function sendRemote(message) {
  if (ws?.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(message));
  }
}

function waitForRemoteOpen() {
  if (ws?.readyState === WebSocket.OPEN) {
    return Promise.resolve();
  }
  return new Promise((resolve, reject) => {
    ws.addEventListener('open', resolve, { once: true });
    ws.addEventListener('error', () => reject(new Error('Could not connect to room server.')), { once: true });
  });
}

function drawPattern(ctx, width, height, startedAt) {
  const elapsed = ((performance.now() - startedAt) / 1000).toFixed(1);
  ctx.fillStyle = '#101418';
  ctx.fillRect(0, 0, width, height);
  ctx.strokeStyle = '#ffffff';
  ctx.lineWidth = 4;
  ctx.strokeRect(24, 24, width - 48, height - 48);
  ctx.beginPath();
  ctx.moveTo(width / 2, 24);
  ctx.lineTo(width / 2, height - 24);
  ctx.moveTo(24, height / 2);
  ctx.lineTo(width - 24, height / 2);
  ctx.stroke();
  ctx.fillStyle = '#f04e45';
  ctx.fillRect(32, 32, 220, 96);
  ctx.fillStyle = '#46b36d';
  ctx.fillRect(width - 252, 32, 220, 96);
  ctx.fillStyle = '#f3c74f';
  ctx.fillRect(32, height - 128, 220, 96);
  ctx.fillStyle = '#4ba3f2';
  ctx.fillRect(width - 252, height - 128, 220, 96);
  ctx.fillStyle = '#ffffff';
  ctx.font = '700 34px system-ui, sans-serif';
  ctx.fillText('LEFT TOP', 52, 94);
  ctx.fillText('RIGHT TOP', width - 232, 94);
  ctx.fillText('LEFT BOTTOM', 52, height - 66);
  ctx.fillText('RIGHT BOTTOM', width - 232, height - 66);
  ctx.font = '700 42px system-ui, sans-serif';
  ctx.fillText('INTERNET PASSENGER TEST', width / 2 - 300, height / 2 - 36);
  ctx.font = '28px system-ui, sans-serif';
  ctx.fillText(`16:9 1280x720  elapsed ${elapsed}s`, width / 2 - 190, height / 2 + 16);
}

function createPatternStream() {
  cancelAnimationFrame(patternFrame);
  const canvas = document.createElement('canvas');
  canvas.width = 1280;
  canvas.height = 720;
  const ctx = canvas.getContext('2d');
  const startedAt = performance.now();
  function render() {
    drawPattern(ctx, canvas.width, canvas.height, startedAt);
    patternFrame = requestAnimationFrame(render);
  }
  render();
  return canvas.captureStream(60);
}

function setVideoStream(stream, message) {
  videoStream?.getTracks().forEach((track) => track.stop());
  videoStream = stream;
  preview.srcObject = videoStream;
  connectButton.disabled = !room;
  updateStatus(message);
}

function inviteText() {
  return inviteInput.value.trim();
}

function setInviteActionsEnabled(enabled) {
  copyInviteButton.disabled = !enabled;
  shareInviteButton.disabled = !enabled;
  messengerInviteButton.disabled = !enabled;
}

async function copyInvite(message = 'Invite link copied.') {
  const text = inviteText();
  if (!text) {
    updateStatus('Create a room first.');
    return false;
  }
  await navigator.clipboard.writeText(text);
  updateStatus(message);
  return true;
}

function ensureLocalBridge() {
  if (localBridge?.readyState === WebSocket.OPEN) {
    return;
  }
  localBridge = connectLocalSignaling('internet-driver-bridge', (message) => {
    if (message.type === 'poseStatus') {
      updateStatus(`local pose: ${message.udpPayload}`);
    }
  });
}

async function ensureMicrophone() {
  if (micStream) {
    return;
  }
  micStream = await navigator.mediaDevices.getUserMedia({ audio: true, video: false });
  muteButton.disabled = false;
  updateStatus('Driver microphone enabled.');
}

async function createRoom() {
  const baseUrl = serverUrlInput.value.trim().replace(/\/+$/, '');
  assertPublicRoomServer(baseUrl);
  const response = await fetch(`${baseUrl}/api/rooms`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({
      visibility: visibilityInput.value,
      password: passwordInput.value,
      driverName: config.internet?.driverName ?? 'Driver',
      passengerConfig: {
        pageHand: config.openRbrVr?.roadbookVR?.pageHand ?? 'right',
        handTracking: config.handTracking ?? {},
      },
    }),
  });
  room = await response.json();
  if (!response.ok) {
    throw new Error(room.error ?? 'Room creation failed.');
  }
  inviteInput.value = room.passengerInviteUrl;
  setInviteActionsEnabled(true);
  connectButton.disabled = !videoStream;
  updateStatus('Room created. Share the invite link with the passenger.');
}

async function sendOffer() {
  if (!joinedRoom || !pc || offerSent) {
    return;
  }
  const offer = await pc.createOffer();
  await pc.setLocalDescription(offer);
  sendRemote({ type: 'signal:offer', description: pc.localDescription });
  offerSent = true;
  updateStatus('Offer sent. Waiting for passenger answer.');
}

function connectSignaling() {
  ws?.close();
  joinedRoom = false;
  offerSent = false;
  peerHint = 'signaling';
  ws = new WebSocket(remoteWsUrl(serverUrlInput.value.trim(), room.roomId));
  ws.addEventListener('open', () => {
    sendRemote({ type: 'room:join', role: 'driver', roomId: room.roomId, token: room.driverToken });
    updateStatus('Driver joined room signaling.');
  });
  ws.addEventListener('message', async (event) => {
    const message = JSON.parse(event.data);
    if (message.type === 'signal:answer' && pc) {
      await pc.setRemoteDescription(message.description);
      updateStatus('Passenger answer received.');
    } else if (message.type === 'signal:candidate' && pc) {
      await pc.addIceCandidate(message.candidate).catch(() => {});
    } else if (message.type === 'room:status') {
      updateStatus(`room status: passenger ${message.passengerConnected ? 'connected' : 'waiting'}`);
      if (message.passengerConnected) {
        peerHint = 'passenger joined; connecting direct';
        await sendOffer();
      }
    } else if (message.type === 'room:joined') {
      joinedRoom = true;
      updateStatus('Driver room join accepted.');
    } else if (message.type === 'room:error') {
      updateStatus(`room error: ${message.error}`);
    }
  });
}

async function startPeer() {
  if (!room || !videoStream) {
    updateStatus('Create a room and select video first.');
    return;
  }

  await ensureMicrophone().catch(() => updateStatus('Driver microphone permission was not granted; continuing without driver voice.'));
  ensureLocalBridge();
  connectSignaling();
  await waitForRemoteOpen();
  pc?.close();
  pc = new RTCPeerConnection({ iceServers: room.iceServers ?? [] });
  pc.addEventListener('connectionstatechange', () => {
    if (pc.connectionState === 'connected') {
      peerHint = 'direct connection established';
    } else if (pc.connectionState === 'failed') {
      peerHint = 'direct connection failed; relay would be required';
    } else if (pc.connectionState === 'connecting') {
      peerHint = 'direct connecting';
    } else {
      peerHint = pc.connectionState;
    }
    updateStatus();
  });
  pc.addEventListener('iceconnectionstatechange', () => {
    if (pc.iceConnectionState === 'failed') {
      peerHint = 'direct connection failed; relay would be required';
      updateStatus('No TURN relay is configured in the free build.');
    }
  });
  pc.addEventListener('icecandidate', (event) => {
    if (event.candidate) {
      sendRemote({ type: 'signal:candidate', candidate: event.candidate });
    }
  });
  pc.addEventListener('track', (event) => {
    remoteAudio.srcObject = event.streams[0];
    remoteAudio.play().catch(() => {});
  });

  for (const track of videoStream.getVideoTracks()) {
    const sender = pc.addTrack(track, videoStream);
    const params = sender.getParameters();
    params.encodings = [{ maxBitrate: 6000000, maxFramerate: 60 }];
    await sender.setParameters(params).catch(() => {});
  }
  if (micStream) {
    for (const track of micStream.getAudioTracks()) {
      pc.addTrack(track, micStream);
    }
  }
  pc.addTransceiver('audio', { direction: 'sendrecv' });

  dataChannel = pc.createDataChannel('passenger-control', { ordered: false, maxRetransmits: 0 });
  dataChannel.addEventListener('message', (event) => {
    const message = JSON.parse(event.data);
    if (['pose', 'handPose', 'roadbook'].includes(message.type) && localBridge?.readyState === WebSocket.OPEN) {
      localBridge.send(JSON.stringify(message));
    } else if (message.type === 'recenter') {
      updateStatus('Passenger requested recenter.');
    } else if (message.type === 'muteStatus') {
      updateStatus(`passenger mic: ${message.muted ? 'muted' : 'live'}`);
    }
  });
  dataChannel.addEventListener('open', () => updateStatus('Data channel open.'));

  updateStatus('Peer ready. Waiting for passenger to join.');
}

createButton.addEventListener('click', () => createRoom().catch((error) => updateStatus(error.message)));
patternButton.addEventListener('click', () => setVideoStream(createPatternStream(), 'Using generated internet test pattern.'));
captureButton.addEventListener('click', async () => {
  cancelAnimationFrame(patternFrame);
  const stream = await navigator.mediaDevices.getDisplayMedia({
    video: { frameRate: { ideal: 60, max: 60 }, width: { ideal: 1280 }, height: { ideal: 720 } },
    audio: false,
  });
  setVideoStream(stream, 'RBR passenger view selected.');
});
micButton.addEventListener('click', async () => {
  await ensureMicrophone();
});
muteButton.addEventListener('click', () => {
  const track = micStream?.getAudioTracks()[0];
  if (!track) return;
  track.enabled = !track.enabled;
  muteButton.textContent = track.enabled ? 'Mute mic' : 'Unmute mic';
  dataChannel?.send(JSON.stringify({ type: 'muteStatus', muted: !track.enabled }));
  updateStatus();
});
connectButton.addEventListener('click', () => startPeer().catch((error) => updateStatus(error.message)));
copyInviteButton.addEventListener('click', () => copyInvite().catch((error) => updateStatus(`Copy failed: ${error.message}`)));
shareInviteButton.addEventListener('click', async () => {
  const text = inviteText();
  if (!text) {
    updateStatus('Create a room first.');
    return;
  }
  if (navigator.share) {
    await navigator.share({ title: 'CoDriverVR passenger invite', text: 'Join my RBR passenger VR room', url: text });
    updateStatus('Invite shared.');
    return;
  }
  await copyInvite('Share sheet unavailable; invite link copied.');
});
messengerInviteButton.addEventListener('click', async () => {
  if (await copyInvite('Invite link copied. Opening Messenger.')) {
    window.open('https://www.messenger.com/', '_blank', 'noopener');
  }
});
useLanServerButton.addEventListener('click', () => {
  serverUrlInput.value = config.lan?.roomServerUrl ?? '';
  updateStatus(`Using LAN room server: ${serverUrlInput.value}`);
});
usePublicServerButton.addEventListener('click', () => {
  serverUrlInput.value = config.internet?.roomServerUrl ?? '';
  updateStatus(`Using public room server: ${serverUrlInput.value}`);
});

window.addEventListener('beforeunload', () => {
  sendRemote({ type: 'room:leave' });
  ws?.close();
  pc?.close();
  localBridge?.close();
  videoStream?.getTracks().forEach((track) => track.stop());
  micStream?.getTracks().forEach((track) => track.stop());
  cancelAnimationFrame(patternFrame);
});

loadConfig().finally(updateStatus);
