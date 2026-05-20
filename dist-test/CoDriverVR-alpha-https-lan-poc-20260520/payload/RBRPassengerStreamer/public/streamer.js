import { connectSignaling, makePeerConnection, setStatus } from './common.js';

const captureButton = document.querySelector('#capture');
const patternButton = document.querySelector('#pattern');
const offerButton = document.querySelector('#offer');
const togglePreviewButton = document.querySelector('#toggle-preview');
const preview = document.querySelector('#preview');
const status = document.querySelector('#status');

let ws;
let pc;
let stream;
let patternTimer;
let viewerConnected = false;
let lastOfferAt = 0;
let appConfig = {};
let previewVisible = false;

async function loadConfig() {
  const response = await fetch('/api/config', { cache: 'no-store' });
  appConfig = await response.json();
}

function signal(message) {
  if (ws?.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(message));
  }
}

function updateStatus(extra = '') {
  const tracks = stream ? stream.getVideoTracks().map((track) => `${track.label} ${track.readyState}`) : ['no capture'];
  setStatus(status, [
    `signaling: ${ws?.readyState === WebSocket.OPEN ? 'connected' : 'offline'}`,
    `viewer: ${viewerConnected ? 'connected' : 'waiting'}`,
    `display capture: ${navigator.mediaDevices?.getDisplayMedia ? 'available' : 'not available in this browser'}`,
    `capture: ${tracks.join(', ')}`,
    `pc preview: ${previewVisible ? 'visible' : 'hidden'}`,
    `peer: ${pc?.connectionState ?? 'not created'}`,
    `ice: ${pc?.iceConnectionState ?? 'not created'}`,
    extra,
  ]);
}

function applyPreviewVisibility() {
  preview.hidden = !previewVisible;
  if (previewVisible && stream) {
    preview.srcObject = stream;
  } else {
    preview.srcObject = null;
  }
  togglePreviewButton.textContent = previewVisible ? 'Hide PC preview' : 'Show PC preview';
  updateStatus();
}

async function ensurePeerConnection() {
  if (pc) {
    pc.close();
  }

  pc = makePeerConnection(signal);
  pc.addEventListener('connectionstatechange', () => updateStatus());
  pc.addEventListener('iceconnectionstatechange', () => updateStatus());
  pc.addEventListener('icegatheringstatechange', () => updateStatus());

  for (const track of stream.getVideoTracks()) {
    track.contentHint = appConfig.video?.contentHint ?? 'motion';
    const sender = pc.addTrack(track, stream);
    const transceiver = pc.getTransceivers().find((item) => item.sender === sender);
    const preferredCodec = String(appConfig.video?.preferredCodec ?? 'H264').toLowerCase();
    const capabilities = RTCRtpSender.getCapabilities?.('video');
    const codecs = capabilities?.codecs ?? [];
    const preferred = codecs.filter((codec) => codec.mimeType.toLowerCase().includes(preferredCodec.toLowerCase()));
    const rest = codecs.filter((codec) => !codec.mimeType.toLowerCase().includes(preferredCodec.toLowerCase()));
    if (transceiver?.setCodecPreferences && preferred.length) {
      transceiver.setCodecPreferences([...preferred, ...rest]);
    }

    const params = sender.getParameters();
    params.degradationPreference = appConfig.video?.degradationPreference ?? 'maintain-framerate';
    params.encodings = [
      {
        maxBitrate: Number(appConfig.video?.maxBitrate ?? 8000000),
        maxFramerate: Number(appConfig.video?.frameRate ?? 60),
        scaleResolutionDownBy: Number(appConfig.video?.scaleResolutionDownBy ?? 1),
      },
    ];
    await sender.setParameters(params).catch(() => {});
  }

  return pc;
}

async function startOffer() {
  if (!stream) {
    updateStatus('Capture the RBR window first.');
    return;
  }
  if (!viewerConnected) {
    updateStatus('Waiting for Quest viewer to connect before sending offer.');
    return;
  }

  await ensurePeerConnection();
  lastOfferAt = performance.now();
  const offer = await pc.createOffer({
    offerToReceiveAudio: false,
    offerToReceiveVideo: false,
  });
  await pc.setLocalDescription(offer);
  signal({ type: 'offer', description: pc.localDescription });
  updateStatus('Offer sent.');
}

function maybeStartOffer(reason) {
  if (!stream || !viewerConnected) {
    return;
  }
  if (pc && !['closed', 'failed', 'disconnected'].includes(pc.connectionState)) {
    return;
  }
  if (performance.now() - lastOfferAt < 1000) {
    return;
  }
  startOffer().catch((error) => updateStatus(`Offer failed after ${reason}: ${error.message}`));
}

function setStream(nextStream, message) {
  pc?.close();
  pc = null;
  stream?.getTracks().forEach((track) => track.stop());
  stream = nextStream;
  if (previewVisible) {
    preview.srcObject = stream;
  }
  offerButton.disabled = false;
  updateStatus(message);
  maybeStartOffer('capture');
}

function drawPattern(ctx, width, height, startedAt) {
  const elapsed = ((performance.now() - startedAt) / 1000).toFixed(1);
  ctx.fillStyle = '#101418';
  ctx.fillRect(0, 0, width, height);

  ctx.strokeStyle = '#3a4652';
  ctx.lineWidth = 1;
  for (let x = 0; x <= width; x += 80) {
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, height);
    ctx.stroke();
  }
  for (let y = 0; y <= height; y += 80) {
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
  }

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
  ctx.fillText('RBR PASSENGER VIDEO TEST', width / 2 - 310, height / 2 - 38);
  ctx.font = '28px system-ui, sans-serif';
  ctx.fillText(`16:9 1280x720  elapsed ${elapsed}s`, width / 2 - 190, height / 2 + 12);
}

function createPatternStream() {
  clearInterval(patternTimer);
  const canvas = document.createElement('canvas');
  canvas.width = Number(appConfig.video?.width ?? 1280);
  canvas.height = Number(appConfig.video?.height ?? 720);
  const ctx = canvas.getContext('2d');
  const startedAt = performance.now();
  const output = canvas.captureStream(0);
  const [track] = output.getVideoTracks();

  function render() {
    drawPattern(ctx, canvas.width, canvas.height, startedAt);
    track?.requestFrame?.();
  }
  render();
  patternTimer = setInterval(render, 1000 / Number(appConfig.video?.frameRate ?? 60));

  return output;
}

patternButton.addEventListener('click', () => {
  setStream(createPatternStream(), 'Streaming generated test pattern. Use this to verify headset crop, flip, stretch, and alignment.');
});

captureButton.addEventListener('click', async () => {
  try {
    clearInterval(patternTimer);
    if (!navigator.mediaDevices?.getDisplayMedia) {
      updateStatus('This browser cannot open the screen/window picker. Open this page in Chrome or Edge on the RBR PC.');
      return;
    }
    updateStatus('Waiting for the browser window picker. Choose the RBR/openRBRVR companion window.');
    const capture = await navigator.mediaDevices.getDisplayMedia({
      video: {
        frameRate: { ideal: Number(appConfig.video?.frameRate ?? 60), max: Number(appConfig.video?.frameRate ?? 60) },
        width: { ideal: Number(appConfig.video?.width ?? 1280) },
        height: { ideal: Number(appConfig.video?.height ?? 720) },
      },
      audio: false,
    });
    setStream(capture, 'Captured browser-selected window. If this is not RBR, click Share RBR window again and choose the companion window.');
  } catch (error) {
    updateStatus(`Window capture failed: ${error.name ?? 'Error'} ${error.message ?? error}`);
  }
});

offerButton.addEventListener('click', startOffer);
togglePreviewButton.addEventListener('click', () => {
  previewVisible = !previewVisible;
  applyPreviewVisibility();
});

ws = connectSignaling('streamer', async (message) => {
  if (message.type === 'status') {
    viewerConnected = message.clients.some((client) => client.role === 'viewer');
    updateStatus();
    maybeStartOffer('viewer status');
    return;
  }

  if (message.type === 'answer' && pc) {
    if (pc.signalingState !== 'have-local-offer') {
      updateStatus(`Ignored duplicate answer while signaling is ${pc.signalingState}.`);
      return;
    }
    await pc.setRemoteDescription(message.description);
    updateStatus('Answer received.');
  } else if (message.type === 'candidate' && pc) {
    await pc.addIceCandidate(message.candidate).catch(() => {});
  } else if (message.type === 'hello') {
    updateStatus('Connected to signaling server.');
  }
});

window.addEventListener('beforeunload', () => {
  clearInterval(patternTimer);
  pc?.close();
  stream?.getTracks().forEach((track) => track.stop());
});

loadConfig().finally(() => {
  previewVisible = appConfig.pc?.showPreview === true;
  applyPreviewVisibility();
});
