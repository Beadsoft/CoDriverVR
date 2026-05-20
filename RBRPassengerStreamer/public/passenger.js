import { parseToken, setStatus, signalUrl } from './common.js';

const joinButton = document.querySelector('#join');
const micButton = document.querySelector('#mic');
const muteButton = document.querySelector('#mute');
const vrButton = document.querySelector('#vr');
const recenterButton = document.querySelector('#recenter');
const passwordInput = document.querySelector('#password');
const video = document.querySelector('#remote');
const remoteAudio = document.querySelector('#remote-audio');
const canvas = document.querySelector('#xr-canvas');
const status = document.querySelector('#status');

let ws;
let pc;
let dataChannel;
let micStream;
let gl;
let program;
let videoTexture;
let vertexBuffer;
let session;
let referenceSpace;
let basePose = { yaw: 0, pitch: 0, roll: 0 };
let latestAbsolutePose = { yaw: 0, pitch: 0, roll: 0 };
let lastPoseSample = null;
let smoothedPose = null;
let latestPoseSent = 0;
let latestHandStatus = '';
let videoFrameCount = 0;
let lastVideoTime = 0;
let lastVideoQuality = '';
let handGesture = {
  left: { pinching: false, startX: 0, fired: false, lastCommandAt: 0 },
  right: { pinching: false, startX: 0, fired: false, lastCommandAt: 0 },
};

const viewerConfig = {
  stereoLayout: 'side-by-side',
  swapEyes: false,
};

const poseConfig = {
  sendHz: 60,
  predictionMs: 70,
  smoothing: 0.18,
};

const handTrackingConfig = {
  enabled: true,
  pinchThresholdMeters: 0.035,
  swipeThresholdMeters: 0.14,
  debounceMs: 450,
  pageHand: 'right',
};

function roomId() {
  return location.pathname.split('/').filter(Boolean).pop();
}

function isLocalhost() {
  return ['localhost', '127.0.0.1', '::1'].includes(location.hostname);
}

function vrSecurityHint() {
  if (window.isSecureContext || isLocalhost()) {
    return '';
  }
  return 'VR mode needs a secure browser origin. LAN HTTP can show the flat stream, but Quest Browser may block immersive VR on this URL.';
}

function updateStatus(extra = '') {
  const quality = video.getVideoPlaybackQuality?.();
  if (quality) {
    lastVideoQuality = `decoded ${quality.totalVideoFrames} dropped ${quality.droppedVideoFrames}`;
  }
  setStatus(status, [
    `room: ${roomId()}`,
    `signaling: ${ws?.readyState === WebSocket.OPEN ? 'connected' : 'offline'}`,
    `peer: ${pc?.connectionState ?? 'not created'}`,
    `video: ${video.videoWidth || 0}x${video.videoHeight || 0} t=${video.currentTime.toFixed(2)} frames=${videoFrameCount}`,
    lastVideoQuality,
    `mic: ${micStream ? (micStream.getAudioTracks()[0]?.enabled ? 'enabled' : 'muted') : 'off'}`,
    `data: ${dataChannel?.readyState ?? 'not open'}`,
    `webxr: ${navigator.xr ? 'available' : 'not available'}`,
    vrSecurityHint(),
    latestHandStatus,
    extra,
  ]);
}

function angleDelta(a, b) {
  let delta = a - b;
  while (delta > 180) delta -= 360;
  while (delta < -180) delta += 360;
  return delta;
}

function send(message) {
  if (ws?.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(message));
  }
}

function sendData(message) {
  if (dataChannel?.readyState === 'open') {
    dataChannel.send(JSON.stringify(message));
  }
}

async function ensureMicrophone() {
  if (micStream) {
    return;
  }
  micStream = await navigator.mediaDevices.getUserMedia({ audio: true, video: false });
  muteButton.disabled = false;
  updateStatus('Passenger microphone enabled.');
}

function quaternionToRawEuler(q) {
  const { x, y, z, w } = q;
  const sinr = 2 * (w * x + y * z);
  const cosr = 1 - 2 * (x * x + y * y);
  const roll = Math.atan2(sinr, cosr);
  const sinp = 2 * (w * y - z * x);
  const pitch = Math.abs(sinp) >= 1 ? Math.sign(sinp) * Math.PI / 2 : Math.asin(sinp);
  const siny = 2 * (w * z + x * y);
  const cosy = 1 - 2 * (y * y + z * z);
  const yaw = Math.atan2(siny, cosy);
  latestAbsolutePose = {
    yaw: (yaw * 180) / Math.PI,
    pitch: (pitch * 180) / Math.PI,
    roll: (roll * 180) / Math.PI,
  };
  const relative = {
    type: 'pose',
    yaw: latestAbsolutePose.yaw - basePose.yaw,
    pitch: latestAbsolutePose.pitch - basePose.pitch,
    roll: latestAbsolutePose.roll - basePose.roll,
  };
  return compensatePose(relative);
}

function compensatePose(relative) {
  const now = performance.now();
  let compensated = { ...relative };
  if (lastPoseSample) {
    const dt = Math.max(1, now - lastPoseSample.time);
    compensated = {
      type: 'pose',
      yaw: relative.yaw + (angleDelta(relative.yaw, lastPoseSample.pose.yaw) / dt) * poseConfig.predictionMs,
      pitch: relative.pitch + (angleDelta(relative.pitch, lastPoseSample.pose.pitch) / dt) * poseConfig.predictionMs,
      roll: relative.roll + (angleDelta(relative.roll, lastPoseSample.pose.roll) / dt) * poseConfig.predictionMs,
    };
  }
  lastPoseSample = { time: now, pose: { ...relative } };
  if (!smoothedPose) {
    smoothedPose = { yaw: compensated.yaw, pitch: compensated.pitch, roll: compensated.roll };
  } else {
    smoothedPose = {
      yaw: smoothedPose.yaw + angleDelta(compensated.yaw, smoothedPose.yaw) * (1 - poseConfig.smoothing),
      pitch: smoothedPose.pitch + angleDelta(compensated.pitch, smoothedPose.pitch) * (1 - poseConfig.smoothing),
      roll: smoothedPose.roll + angleDelta(compensated.roll, smoothedPose.roll) * (1 - poseConfig.smoothing),
    };
  }
  return { type: 'pose', ...smoothedPose };
}

function compileShader(type, source) {
  const shader = gl.createShader(type);
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    throw new Error(gl.getShaderInfoLog(shader));
  }
  return shader;
}

function initGl() {
  gl = canvas.getContext('webgl', { xrCompatible: true });
  const vs = compileShader(gl.VERTEX_SHADER, `
    attribute vec3 position;
    attribute vec2 uv;
    varying vec2 vUv;
    void main() {
      vUv = uv;
      gl_Position = vec4(position, 1.0);
    }
  `);
  const fs = compileShader(gl.FRAGMENT_SHADER, `
    precision mediump float;
    varying vec2 vUv;
    uniform sampler2D frame;
    void main() {
      gl_FragColor = texture2D(frame, vUv);
    }
  `);
  program = gl.createProgram();
  gl.attachShader(program, vs);
  gl.attachShader(program, fs);
  gl.linkProgram(program);
  vertexBuffer = gl.createBuffer();
  configureVideoQuad('left');
  videoTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, videoTexture);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
}

function configureVideoQuad(eye = 'left') {
  let leftUv = 0;
  let rightUv = 1;
  if (viewerConfig.stereoLayout === 'side-by-side') {
    const useRightHalf = viewerConfig.swapEyes ? eye !== 'right' : eye === 'right';
    leftUv = useRightHalf ? 0.5 : 0;
    rightUv = useRightHalf ? 1 : 0.5;
  }
  gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
    -1,  1, 0, leftUv, 0,
     1,  1, 0, rightUv, 0,
    -1, -1, 0, leftUv, 1,
     1, -1, 0, rightUv, 1,
  ]), gl.STATIC_DRAW);
}

function drawVideo() {
  if (!gl || video.readyState < HTMLMediaElement.HAVE_CURRENT_DATA) return;
  if (video.currentTime !== lastVideoTime) {
    lastVideoTime = video.currentTime;
    videoFrameCount++;
  }
  gl.useProgram(program);
  gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer);
  const position = gl.getAttribLocation(program, 'position');
  const uv = gl.getAttribLocation(program, 'uv');
  gl.enableVertexAttribArray(position);
  gl.enableVertexAttribArray(uv);
  gl.vertexAttribPointer(position, 3, gl.FLOAT, false, 20, 0);
  gl.vertexAttribPointer(uv, 2, gl.FLOAT, false, 20, 12);
  gl.activeTexture(gl.TEXTURE0);
  gl.bindTexture(gl.TEXTURE_2D, videoTexture);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, video);
  gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
}

function jointDistance(a, b) {
  const dx = a.x - b.x;
  const dy = a.y - b.y;
  const dz = a.z - b.z;
  return Math.hypot(dx, dy, dz);
}

function relativePosition(position, viewerPose) {
  const head = viewerPose.transform.position;
  return {
    x: position.x - head.x,
    y: position.y - head.y,
    z: position.z - head.z,
  };
}

function emitHandTracking(frame, viewerPose) {
  if (!handTrackingConfig.enabled || !session?.inputSources || typeof frame.getJointPose !== 'function') {
    latestHandStatus = 'hands: disabled';
    return;
  }
  let tracked = 0;
  for (const inputSource of session.inputSources) {
    if (!inputSource.hand || (inputSource.handedness !== 'left' && inputSource.handedness !== 'right')) continue;
    const wrist = inputSource.hand.get('wrist');
    const thumb = inputSource.hand.get('thumb-tip');
    const index = inputSource.hand.get('index-finger-tip');
    if (!wrist || !thumb || !index) continue;
    const wristPose = frame.getJointPose(wrist, referenceSpace);
    const thumbPose = frame.getJointPose(thumb, referenceSpace);
    const indexPose = frame.getJointPose(index, referenceSpace);
    if (!wristPose || !thumbPose || !indexPose) {
      sendData({ type: 'handPose', side: inputSource.handedness, valid: false });
      continue;
    }
    tracked++;
    const pinchDistance = jointDistance(thumbPose.transform.position, indexPose.transform.position);
    const pinching = pinchDistance <= handTrackingConfig.pinchThresholdMeters;
    const position = relativePosition(wristPose.transform.position, viewerPose);
    const orientation = wristPose.transform.orientation;
    sendData({
      type: 'handPose',
      side: inputSource.handedness,
      valid: true,
      position,
      orientation: { x: orientation.x, y: orientation.y, z: orientation.z, w: orientation.w },
      pinch: pinching,
    });

    if (inputSource.handedness === handTrackingConfig.pageHand) {
      const state = handGesture[inputSource.handedness];
      const now = performance.now();
      if (pinching && !state.pinching) {
        state.pinching = true;
        state.startX = position.x;
        state.fired = false;
      } else if (!pinching) {
        state.pinching = false;
        state.fired = false;
      } else if (!state.fired && now - state.lastCommandAt >= handTrackingConfig.debounceMs) {
        const dx = position.x - state.startX;
        if (Math.abs(dx) >= handTrackingConfig.swipeThresholdMeters) {
          sendData({ type: 'roadbook', command: dx > 0 ? 'nextPage' : 'previousPage' });
          state.fired = true;
          state.lastCommandAt = now;
        }
      }
    }
  }
  latestHandStatus = tracked ? `hands: ${tracked} tracked` : 'hands: not tracking';
}

async function joinRoom() {
  await ensureMicrophone().catch(() => updateStatus('Passenger microphone permission was not granted; continuing without passenger voice.'));
  ws?.close();
  pc?.close();
  ws = new WebSocket(signalUrl());
  ws.addEventListener('open', () => {
    send({ type: 'room:join', role: 'passenger', roomId: roomId(), token: parseToken(), password: passwordInput.value });
  });
  ws.addEventListener('message', async (event) => {
    const message = JSON.parse(event.data);
    if (message.type === 'room:joined') {
      if (message.passengerConfig) {
        applyPassengerConfig(message.passengerConfig);
      }
      pc = new RTCPeerConnection({ iceServers: message.iceServers ?? [] });
      pc.addEventListener('connectionstatechange', () => updateStatus());
      pc.addEventListener('icecandidate', (candidateEvent) => {
        if (candidateEvent.candidate) send({ type: 'signal:candidate', candidate: candidateEvent.candidate });
      });
      pc.addEventListener('track', (trackEvent) => {
        const track = trackEvent.track;
        if (track.kind === 'video') {
          video.srcObject = trackEvent.streams[0];
          track.addEventListener('mute', () => updateStatus('Video track muted.'));
          track.addEventListener('unmute', () => updateStatus('Video track unmuted.'));
          track.addEventListener('ended', () => updateStatus('Video track ended.'));
          video.addEventListener('loadedmetadata', () => updateStatus('Video metadata loaded.'), { once: true });
          video.addEventListener('playing', () => updateStatus('Video playing.'), { once: true });
          video.play().catch((error) => updateStatus(`Video play failed: ${error.message}`));
          vrButton.disabled = false;
          updateStatus('Video track received.');
        } else {
          remoteAudio.srcObject = trackEvent.streams[0];
          remoteAudio.play().catch(() => {});
        }
      });
      pc.addEventListener('datachannel', (channelEvent) => {
        dataChannel = channelEvent.channel;
        dataChannel.addEventListener('open', () => updateStatus('Data channel open.'));
      });
      pc.addTransceiver('video', { direction: 'recvonly' });
      pc.addTransceiver('audio', { direction: micStream ? 'sendrecv' : 'recvonly' });
      if (micStream) {
        for (const track of micStream.getAudioTracks()) pc.addTrack(track, micStream);
      }
      updateStatus('Joined. Waiting for driver offer.');
    } else if (message.type === 'signal:offer') {
      await pc.setRemoteDescription(message.description);
      const answer = await pc.createAnswer();
      await pc.setLocalDescription(answer);
      send({ type: 'signal:answer', description: pc.localDescription });
      updateStatus('Answered driver offer.');
    } else if (message.type === 'signal:candidate') {
      await pc.addIceCandidate(message.candidate).catch(() => {});
    } else if (message.type === 'room:error') {
      updateStatus(`room error: ${message.error}`);
    } else if (message.type === 'room:status') {
      updateStatus(`driver ${message.driverConnected ? 'connected' : 'offline'}`);
    } else if (message.type === 'passenger:config') {
      applyPassengerConfig(message.config ?? {});
    }
  });
}

function applyPassengerConfig(nextConfig) {
  if (nextConfig.pageHand === 'left' || nextConfig.pageHand === 'right') {
    handTrackingConfig.pageHand = nextConfig.pageHand;
  }
  const hands = nextConfig.handTracking ?? {};
  if (typeof hands.enabled === 'boolean') handTrackingConfig.enabled = hands.enabled;
  if (Number.isFinite(Number(hands.pinchThresholdMeters))) handTrackingConfig.pinchThresholdMeters = Number(hands.pinchThresholdMeters);
  if (Number.isFinite(Number(hands.swipeThresholdMeters))) handTrackingConfig.swipeThresholdMeters = Number(hands.swipeThresholdMeters);
  if (Number.isFinite(Number(hands.debounceMs))) handTrackingConfig.debounceMs = Number(hands.debounceMs);
}

async function enterVr() {
  if (!window.isSecureContext && !isLocalhost()) {
    updateStatus('Cannot enter VR from LAN HTTP. Use a secure HTTPS passenger link or localhost/ADB mode.');
    return;
  }
  if (!navigator.xr) {
    updateStatus('WebXR not available.');
    return;
  }
  initGl();
  session = await navigator.xr.requestSession('immersive-vr', { optionalFeatures: ['hand-tracking'] });
  await gl.makeXRCompatible();
  session.updateRenderState({ baseLayer: new XRWebGLLayer(session, gl) });
  referenceSpace = await session.requestReferenceSpace('local');
  session.requestAnimationFrame(onXrFrame);
}

function onXrFrame(time, frame) {
  session.requestAnimationFrame(onXrFrame);
  const pose = frame.getViewerPose(referenceSpace);
  const layer = session.renderState.baseLayer;
  gl.bindFramebuffer(gl.FRAMEBUFFER, layer.framebuffer);
  gl.clearColor(0, 0, 0, 1);
  gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  if (!pose) return;
  for (const view of pose.views) {
    const viewport = layer.getViewport(view);
    gl.viewport(viewport.x, viewport.y, viewport.width, viewport.height);
    configureVideoQuad(view.eye);
    drawVideo();
  }
  if (time - latestPoseSent > 1000 / poseConfig.sendHz) {
    latestPoseSent = time;
    sendData(quaternionToRawEuler(pose.transform.orientation));
    emitHandTracking(frame, pose);
  }
}

joinButton.addEventListener('click', () => joinRoom().catch((error) => updateStatus(error.message)));
micButton.addEventListener('click', async () => {
  await ensureMicrophone();
});
muteButton.addEventListener('click', () => {
  const track = micStream?.getAudioTracks()[0];
  if (!track) return;
  track.enabled = !track.enabled;
  muteButton.textContent = track.enabled ? 'Mute mic' : 'Unmute mic';
  sendData({ type: 'muteStatus', muted: !track.enabled });
  updateStatus();
});
vrButton.addEventListener('click', () => enterVr().catch((error) => updateStatus(error.message)));
recenterButton.addEventListener('click', () => {
  basePose = { ...latestAbsolutePose };
  sendData({ type: 'recenter' });
  updateStatus('Recentered passenger pose.');
});

if (navigator.xr) {
  navigator.xr.isSessionSupported('immersive-vr').then((supported) => {
    vrButton.disabled = !supported || !video.srcObject;
    updateStatus(supported ? 'Quest Browser can enter immersive VR.' : 'Immersive VR not supported.');
  });
} else if (!window.isSecureContext && !isLocalhost()) {
  vrButton.disabled = true;
  updateStatus('Flat stream works on LAN HTTP. VR mode needs HTTPS or localhost/ADB mode.');
}

updateStatus();
setInterval(updateStatus, 1000);
