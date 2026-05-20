import { connectLocalSignaling, setStatus } from './common.js';

const preview = document.querySelector('#preview');
const remoteAudio = document.querySelector('#remote-audio');
const status = document.querySelector('#status');

let controlWs = null;
let roomWs = null;
let localBridge = null;
let pc = null;
let dataChannel = null;
let videoStream = null;
let micStream = null;
let room = null;
let appConfig = {};
let mode = 'lan';
let joinedRoom = false;
let offerSent = false;
let peerHint = 'not started';
let captureSource = 'none';
let lastCommand = 'idle';
let lastError = '';
let passengerConnected = false;

function roomBaseUrl(selectedMode = mode) {
  if (selectedMode === 'lan') {
    return appConfig.lan?.roomServerUrl ?? '';
  }
  if (selectedMode === 'secure-lan') {
    return appConfig.secureLan?.tunnelUrl ?? '';
  }
  const internetUrl = appConfig.internet?.enabled === true ? (appConfig.internet?.roomServerUrl ?? '') : '';
  return internetUrl || (appConfig.lan?.roomServerUrl ?? '');
}

function driverShareUrl() {
  if (!room?.passengerInviteUrl) {
    return '';
  }
  const origin = new URL(room.passengerInviteUrl).origin;
  return `${origin}/driver-share.html?invite=${encodeURIComponent(room.passengerInviteUrl)}`;
}

function statusPayload(extra = '') {
  const payload = {
    type: 'driverRoomStatus',
    enabled: appConfig.driverRoom?.enabled !== false,
    hostConnected: true,
    electronAvailable: Boolean(window.coDriverVrHost?.captureSource),
    running: Boolean(room),
    mode,
    roomId: room?.roomId ?? '',
    passengerInviteUrl: room?.passengerInviteUrl ?? '',
    driverShareUrl: driverShareUrl(),
    inviteReady: Boolean(room?.passengerInviteUrl),
    passengerJoined: passengerConnected,
    signalingConnected: roomWs?.readyState === WebSocket.OPEN,
    peerState: pc?.connectionState ?? 'not created',
    iceState: pc?.iceConnectionState ?? 'not created',
    directConnected: pc?.connectionState === 'connected',
    directFailed: pc?.connectionState === 'failed' || pc?.iceConnectionState === 'failed',
    captureSource,
    micEnabled: Boolean(micStream?.getAudioTracks()[0]?.enabled),
    lastCommand,
    lastError,
    summary: summaryText(extra),
  };
  return payload;
}

function summaryText(extra = '') {
  if (lastError) {
    return `Error: ${lastError}`;
  }
  if (!room) {
    return 'Stopped';
  }
  if (pc?.connectionState === 'connected') {
    return `Connected (${mode}, ${captureSource})`;
  }
  if (passengerConnected) {
    return `Passenger joined; ${peerHint}`;
  }
  return `Invite ready (${mode}); waiting for passenger`;
}

function publishStatus(extra = '') {
  const payload = statusPayload(extra);
  controlWs?.readyState === WebSocket.OPEN && controlWs.send(JSON.stringify(payload));
  setStatus(status, [
    `mode: ${payload.mode}`,
    `room: ${payload.roomId || 'not created'}`,
    `invite: ${payload.inviteReady ? payload.passengerInviteUrl : 'not ready'}`,
    `passenger: ${payload.passengerJoined ? 'joined' : 'waiting'}`,
    `signaling: ${payload.signalingConnected ? 'connected' : 'offline'}`,
    `peer: ${payload.peerState} (${peerHint})`,
    `ice: ${payload.iceState}`,
    `capture: ${payload.captureSource}`,
    `mic: ${payload.micEnabled ? 'on' : 'off'}`,
    extra,
  ].filter(Boolean));
}

async function loadConfig() {
  const response = await fetch('/api/config', { cache: 'no-store' });
  appConfig = await response.json();
  const preferred = appConfig.driverRoom?.defaultMode;
  if (preferred === 'secure-lan' && appConfig.secureLan?.enabled === true && appConfig.secureLan?.tunnelUrl) {
    mode = 'secure-lan';
  } else {
    mode = preferred === 'internet' && appConfig.internet?.enabled === true ? 'internet' : 'lan';
  }
}

function remoteWsUrl(baseUrl, roomId) {
  const url = new URL(baseUrl);
  url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
  url.pathname = '/signal';
  url.searchParams.set('roomId', roomId);
  url.hash = '';
  return url.toString();
}

function sendRemote(message) {
  if (roomWs?.readyState === WebSocket.OPEN) {
    roomWs.send(JSON.stringify(message));
  }
}

function ensureLocalBridge() {
  if (localBridge?.readyState === WebSocket.OPEN) {
    return;
  }
  localBridge = connectLocalSignaling('driver-host-bridge', (message) => {
    if (message.type === 'poseStatus') {
      publishStatus(`local pose: ${message.udpPayload}`);
    }
  });
}

async function ensureCapture() {
  if (videoStream?.active) {
    return;
  }
  if (!window.coDriverVrHost?.captureSource) {
    throw new Error('Electron capture helper is unavailable. Install dependencies and launch with npm run start:host.');
  }
  const source = await window.coDriverVrHost.captureSource();
  if (!source?.id) {
    throw new Error('Could not find a desktop capture source.');
  }
  captureSource = source.name ?? source.id;
  videoStream?.getTracks().forEach((track) => track.stop());
  videoStream = await navigator.mediaDevices.getUserMedia({
    audio: false,
    video: {
      mandatory: {
        chromeMediaSource: 'desktop',
        chromeMediaSourceId: source.id,
        minWidth: Number(appConfig.video?.width ?? 1280),
        maxWidth: Number(appConfig.video?.width ?? 1280),
        minHeight: Number(appConfig.video?.height ?? 720),
        maxHeight: Number(appConfig.video?.height ?? 720),
        minFrameRate: Number(appConfig.video?.frameRate ?? 60),
        maxFrameRate: Number(appConfig.video?.frameRate ?? 60),
      },
    },
  });
  preview.srcObject = videoStream;
  publishStatus('Captured RBR companion window.');
}

async function ensureMicrophone() {
  if (micStream?.active) {
    return;
  }
  micStream = await navigator.mediaDevices.getUserMedia({ audio: true, video: false });
  publishStatus('Driver microphone enabled.');
}

async function createRoom(selectedMode = mode) {
  mode = ['lan', 'secure-lan', 'internet'].includes(selectedMode) ? selectedMode : 'lan';
  const baseUrl = roomBaseUrl(mode).replace(/\/+$/, '');
  if (!baseUrl) {
    throw new Error(`No ${mode} room server URL is configured.`);
  }
  const response = await fetch(`${baseUrl}/api/rooms`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({
      visibility: 'private',
      driverName: appConfig.internet?.driverName ?? 'Driver',
      passengerConfig: {
        pageHand: appConfig.openRbrVr?.roadbookVR?.pageHand ?? 'right',
        handTracking: appConfig.handTracking ?? {},
      },
    }),
  });
  const body = await response.json();
  if (!response.ok) {
    throw new Error(body.error ?? 'Room creation failed.');
  }
  room = body;
  passengerConnected = false;
  joinedRoom = false;
  offerSent = false;
  peerHint = 'room created';
  publishStatus('Room created.');
}

function connectRoomSignaling() {
  roomWs?.close();
  joinedRoom = false;
  offerSent = false;
  const baseUrl = roomBaseUrl(mode);
  roomWs = new WebSocket(remoteWsUrl(baseUrl, room.roomId));
  roomWs.addEventListener('open', () => {
    sendRemote({ type: 'room:join', role: 'driver', roomId: room.roomId, token: room.driverToken });
    publishStatus('Driver joined room signaling.');
  });
  roomWs.addEventListener('message', async (event) => {
    const message = JSON.parse(event.data);
    if (message.type === 'signal:answer' && pc) {
      await pc.setRemoteDescription(message.description);
      publishStatus('Passenger answer received.');
    } else if (message.type === 'signal:candidate' && pc) {
      await pc.addIceCandidate(message.candidate).catch(() => {});
    } else if (message.type === 'room:joined') {
      joinedRoom = true;
      peerHint = 'signaling joined';
      publishStatus('Room join accepted.');
    } else if (message.type === 'room:status') {
      passengerConnected = message.passengerConnected === true;
      if (passengerConnected) {
        lastError = '';
      }
      if (passengerConnected) {
        peerHint = 'passenger joined; connecting direct';
        await sendOffer();
      }
      publishStatus();
    } else if (message.type === 'room:error') {
      const error = message.error ?? 'Room error.';
      if (error === 'The other peer is not connected yet.') {
        peerHint = passengerConnected ? peerHint : 'waiting for passenger';
      } else {
        lastError = error;
      }
      publishStatus();
    }
  });
  roomWs.addEventListener('close', () => {
    publishStatus('Room signaling closed.');
  });
}

async function sendOffer() {
  if (!joinedRoom || !pc || offerSent) {
    return;
  }
  const offer = await pc.createOffer();
  await pc.setLocalDescription(offer);
  sendRemote({ type: 'signal:offer', description: pc.localDescription });
  offerSent = true;
  publishStatus('Offer sent.');
}

async function startPeer() {
  ensureLocalBridge();
  pc?.close();
  pc = new RTCPeerConnection({ iceServers: room.iceServers ?? [] });
  pc.addEventListener('connectionstatechange', () => {
    if (pc.connectionState === 'connected') {
      peerHint = 'direct connection established';
    } else if (pc.connectionState === 'failed') {
      peerHint = 'direct connection failed; relay would be required';
    } else {
      peerHint = pc.connectionState;
    }
    publishStatus();
  });
  pc.addEventListener('iceconnectionstatechange', () => {
    if (pc.iceConnectionState === 'failed') {
      peerHint = 'direct connection failed; relay would be required';
      publishStatus('No TURN relay is configured in the free build.');
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
    params.degradationPreference = appConfig.video?.degradationPreference ?? 'maintain-framerate';
    params.encodings = [{
      maxBitrate: Number(appConfig.video?.maxBitrate ?? 8000000),
      maxFramerate: Number(appConfig.video?.frameRate ?? 60),
      scaleResolutionDownBy: Number(appConfig.video?.scaleResolutionDownBy ?? 1),
    }];
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
    }
  });
  dataChannel.addEventListener('open', () => publishStatus('Data channel open.'));
  connectRoomSignaling();
}

async function startRoom(command = {}) {
  lastCommand = 'start';
  lastError = '';
  const selectedMode = ['lan', 'secure-lan', 'internet'].includes(command.mode) ? command.mode : mode;
  await ensureCapture();
  if (command.mic === true) {
    await ensureMicrophone().catch((error) => {
      lastError = `Microphone unavailable: ${error.message}`;
    });
  }
  await createRoom(selectedMode);
  await startPeer();
}

async function stopRoom() {
  lastCommand = 'stop';
  lastError = '';
  sendRemote({ type: 'room:leave' });
  roomWs?.close();
  roomWs = null;
  pc?.close();
  pc = null;
  dataChannel = null;
  room = null;
  passengerConnected = false;
  joinedRoom = false;
  offerSent = false;
  peerHint = 'stopped';
  publishStatus('Room stopped.');
}

async function recreateRoom(command = {}) {
  await stopRoom();
  await startRoom({ ...command, mode: command.mode ?? mode });
}

async function toggleMic() {
  lastCommand = 'toggleMic';
  lastError = '';
  if (!micStream?.active) {
    await ensureMicrophone();
  } else {
    const track = micStream.getAudioTracks()[0];
    if (track) {
      track.enabled = !track.enabled;
    }
  }
  publishStatus();
}

async function handleCommand(message) {
  try {
    if (message.command === 'start') {
      await startRoom(message);
    } else if (message.command === 'stop') {
      await stopRoom();
    } else if (message.command === 'recreate') {
      await recreateRoom(message);
    } else if (message.command === 'mic') {
      await toggleMic();
    } else if (message.command === 'status') {
      publishStatus();
    }
  } catch (error) {
    lastError = error.message ?? String(error);
    publishStatus();
  }
}

await loadConfig();
controlWs = connectLocalSignaling('driver-host', (message) => {
  if (message.type === 'driverRoomCommand') {
    handleCommand(message);
  }
}, () => publishStatus('Driver host connected.'));

if (appConfig.driverRoom?.autoCreateOnStreamerStart === true) {
  startRoom().catch((error) => {
    lastError = error.message ?? String(error);
    publishStatus();
  });
}
