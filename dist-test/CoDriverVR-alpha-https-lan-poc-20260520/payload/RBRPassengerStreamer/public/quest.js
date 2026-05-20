import { connectSignaling, makePeerConnection, setStatus } from './common.js';

const connectButton = document.querySelector('#connect');
const vrButton = document.querySelector('#vr');
const recenterButton = document.querySelector('#recenter');
const video = document.querySelector('#remote');
const canvas = document.querySelector('#xr-canvas');
const status = document.querySelector('#status');

let ws;
let pc;
let gl;
let program;
let solidProgram;
let videoTexture;
let vertexBuffer;
let solidVertexBuffer;
let videoCanvas;
let videoCanvasCtx;
let videoFrameCount = 0;
let lastVideoTime = 0;
let lastVideoQuality = '';
let session;
let referenceSpace;
let appConfig = {};
let basePose = { yaw: 0, pitch: 0, roll: 0 };
let latestAbsolutePose = { yaw: 0, pitch: 0, roll: 0 };
let latestPoseStatus = '';
let latestHandStatus = '';
let lastPoseSent = 0;
let lastPoseSample = null;
let smoothedPose = null;
let handGesture = {
  left: { pinching: false, startX: 0, fired: false, lastCommandAt: 0 },
  right: { pinching: false, startX: 0, fired: false, lastCommandAt: 0 },
};

function angleDelta(a, b) {
  let delta = a - b;
  while (delta > 180) delta -= 360;
  while (delta < -180) delta += 360;
  return delta;
}

function multiply4(a, b) {
  const out = new Float32Array(16);
  for (let col = 0; col < 4; col++) {
    for (let row = 0; row < 4; row++) {
      out[col * 4 + row] =
        a[0 * 4 + row] * b[col * 4 + 0] +
        a[1 * 4 + row] * b[col * 4 + 1] +
        a[2 * 4 + row] * b[col * 4 + 2] +
        a[3 * 4 + row] * b[col * 4 + 3];
    }
  }
  return out;
}

function translationMatrix(x, y, z) {
  return new Float32Array([
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    x, y, z, 1,
  ]);
}

function identityMatrix() {
  return new Float32Array([
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1,
  ]);
}

function signal(message) {
  if (ws?.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(message));
  }
}

function updateStatus(extra = '') {
  const quality = video.getVideoPlaybackQuality?.();
  if (quality) {
    lastVideoQuality = `decoded ${quality.totalVideoFrames} dropped ${quality.droppedVideoFrames}`;
  }
  setStatus(status, [
    `signaling: ${ws?.readyState === WebSocket.OPEN ? 'connected' : 'offline'}`,
    `peer: ${pc?.connectionState ?? 'not created'}`,
    `video: ${video.videoWidth || 0}x${video.videoHeight || 0} t=${video.currentTime.toFixed(2)} frames=${videoFrameCount}`,
    lastVideoQuality,
    `webxr: ${navigator.xr ? 'available' : 'not available'}`,
    latestPoseStatus,
    latestHandStatus,
    extra,
  ].filter(Boolean));
}

async function loadConfig() {
  const response = await fetch('/api/config', { cache: 'no-store' });
  appConfig = await response.json();
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
    yaw: latestAbsolutePose.yaw - basePose.yaw,
    pitch: latestAbsolutePose.pitch - basePose.pitch,
    roll: latestAbsolutePose.roll - basePose.roll,
  };
  return compensatePose(relative);
}

function compensatePose(relative) {
  const now = performance.now();
  const predictionMs = Number(appConfig.pose?.predictionMs ?? 70);
  const smoothing = Math.max(0, Math.min(0.95, Number(appConfig.pose?.smoothing ?? 0.18)));
  let compensated = { ...relative };

  if (lastPoseSample) {
    const dt = Math.max(1, now - lastPoseSample.time);
    compensated = {
      yaw: relative.yaw + (angleDelta(relative.yaw, lastPoseSample.pose.yaw) / dt) * predictionMs,
      pitch: relative.pitch + (angleDelta(relative.pitch, lastPoseSample.pose.pitch) / dt) * predictionMs,
      roll: relative.roll + (angleDelta(relative.roll, lastPoseSample.pose.roll) / dt) * predictionMs,
    };
  }

  lastPoseSample = { time: now, pose: { ...relative } };

  if (!smoothedPose || smoothing <= 0) {
    smoothedPose = { ...compensated };
  } else {
    smoothedPose = {
      yaw: smoothedPose.yaw + angleDelta(compensated.yaw, smoothedPose.yaw) * (1 - smoothing),
      pitch: smoothedPose.pitch + angleDelta(compensated.pitch, smoothedPose.pitch) * (1 - smoothing),
      roll: smoothedPose.roll + angleDelta(compensated.roll, smoothedPose.roll) * (1 - smoothing),
    };
  }

  return { ...smoothedPose };
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

function createProgram(vertexSource, fragmentSource) {
  const programObject = gl.createProgram();
  gl.attachShader(programObject, compileShader(gl.VERTEX_SHADER, vertexSource));
  gl.attachShader(programObject, compileShader(gl.FRAGMENT_SHADER, fragmentSource));
  gl.linkProgram(programObject);
  if (!gl.getProgramParameter(programObject, gl.LINK_STATUS)) {
    throw new Error(gl.getProgramInfoLog(programObject));
  }
  return programObject;
}

function initGl() {
  if (gl) {
    return;
  }

  gl = canvas.getContext('webgl', { alpha: false, antialias: false, xrCompatible: true });
  const videoVs = `
    attribute vec3 position;
    attribute vec2 uv;
    uniform mat4 mvp;
    varying vec2 vUv;
    void main() {
      vUv = uv;
      gl_Position = mvp * vec4(position, 1.0);
    }
  `;
  const videoFs = `
    precision mediump float;
    varying vec2 vUv;
    uniform sampler2D frame;
    void main() {
      gl_FragColor = texture2D(frame, vUv);
    }
  `;
  program = createProgram(videoVs, videoFs);

  solidProgram = createProgram(`
    attribute vec2 position;
    void main() {
      gl_Position = vec4(position, 0.0, 1.0);
    }
  `, `
    precision mediump float;
    uniform vec4 color;
    void main() {
      gl_FragColor = color;
    }
  `);

  vertexBuffer = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer);
  configureVideoQuad('left');

  solidVertexBuffer = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, solidVertexBuffer);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
    -0.96,  0.72,
     0.96,  0.72,
    -0.96, -0.72,
     0.96, -0.72,
  ]), gl.STATIC_DRAW);

  videoTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, videoTexture);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
}

function configureVideoQuad(eye = 'left') {
  const framedMode = appConfig.viewer?.mode === 'framed';
  const stereoLayout = appConfig.viewer?.stereoLayout ?? 'mono';
  const swapEyes = appConfig.viewer?.swapEyes === true;
  const quad = framedMode ? appConfig.viewer?.quad ?? {} : {};
  const panelWidth = Number.isFinite(quad.widthMeters) ? quad.widthMeters : 1.7;
  const panelHeight = Number.isFinite(quad.heightMeters) ? quad.heightMeters : 0.96;
  const left = framedMode ? -panelWidth / 2 : -1;
  const right = framedMode ? panelWidth / 2 : 1;
  const top = framedMode ? panelHeight / 2 : 1;
  const bottom = framedMode ? -panelHeight / 2 : -1;
  const topUv = appConfig.viewer?.flipY ? 1 : 0;
  const bottomUv = appConfig.viewer?.flipY ? 0 : 1;
  let leftUv = 0;
  let rightUv = 1;

  if (stereoLayout === 'side-by-side') {
    const useRightHalf = swapEyes ? eye !== 'right' : eye === 'right';
    leftUv = useRightHalf ? 0.5 : 0;
    rightUv = useRightHalf ? 1 : 0.5;
  }

  gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
    left,  top, 0, leftUv, topUv,
    right, top, 0, rightUv, topUv,
    left,  bottom, 0, leftUv, bottomUv,
    right, bottom, 0, rightUv, bottomUv,
  ]), gl.STATIC_DRAW);
}

function drawSolid(color) {
  if (!gl || !solidProgram) {
    return;
  }
  gl.useProgram(solidProgram);
  gl.bindBuffer(gl.ARRAY_BUFFER, solidVertexBuffer);
  const position = gl.getAttribLocation(solidProgram, 'position');
  const colorLocation = gl.getUniformLocation(solidProgram, 'color');
  gl.enableVertexAttribArray(position);
  gl.vertexAttribPointer(position, 2, gl.FLOAT, false, 8, 0);
  gl.uniform4fv(colorLocation, color);
  gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
}

function updateVideoCanvas() {
  if (video.readyState < HTMLMediaElement.HAVE_CURRENT_DATA || !video.videoWidth || !video.videoHeight) {
    return false;
  }
  if (!videoCanvas) {
    videoCanvas = document.createElement('canvas');
    videoCanvasCtx = videoCanvas.getContext('2d', { alpha: false });
  }
  if (videoCanvas.width !== video.videoWidth || videoCanvas.height !== video.videoHeight) {
    videoCanvas.width = video.videoWidth;
    videoCanvas.height = video.videoHeight;
  }
  videoCanvasCtx.drawImage(video, 0, 0, videoCanvas.width, videoCanvas.height);
  if (video.currentTime !== lastVideoTime) {
    lastVideoTime = video.currentTime;
    videoFrameCount++;
  }
  return true;
}

function drawVideo() {
  if (!gl || !updateVideoCanvas()) {
    return;
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
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, videoCanvas);
  gl.uniform1i(gl.getUniformLocation(program, 'frame'), 0);
  gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
}

function drawVideoForView(view, viewerPose) {
  configureVideoQuad(view.eye);

  if (appConfig.viewer?.mode !== 'framed') {
    gl.useProgram(program);
    gl.uniformMatrix4fv(gl.getUniformLocation(program, 'mvp'), false, identityMatrix());
    drawVideo();
    return;
  }

  const quad = appConfig.viewer?.quad ?? {};
  const distance = Number.isFinite(quad.distanceMeters) ? quad.distanceMeters : 1.35;
  const verticalOffset = Number.isFinite(quad.verticalOffsetMeters) ? quad.verticalOffsetMeters : 0;
  const model = multiply4(viewerPose.transform.matrix, translationMatrix(0, verticalOffset, -distance));
  const viewModel = multiply4(view.transform.inverse.matrix, model);
  const mvp = multiply4(view.projectionMatrix, viewModel);

  gl.useProgram(program);
  gl.uniformMatrix4fv(gl.getUniformLocation(program, 'mvp'), false, mvp);
  drawVideo();
}

function handTrackingConfig() {
  return {
    enabled: appConfig.handTracking?.enabled !== false,
    pinchThresholdMeters: Number(appConfig.handTracking?.pinchThresholdMeters ?? 0.035),
    swipeThresholdMeters: Number(appConfig.handTracking?.swipeThresholdMeters ?? 0.14),
    debounceMs: Number(appConfig.handTracking?.debounceMs ?? 450),
  };
}

function roadbookPageHand() {
  return appConfig.openRbrVr?.roadbookVR?.pageHand === 'left' ? 'left' : 'right';
}

function sendRoadbookCommand(command) {
  signal({ type: 'roadbook', command });
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
  const cfg = handTrackingConfig();
  if (!cfg.enabled || !session?.inputSources || typeof frame.getJointPose !== 'function') {
    latestHandStatus = 'hands: disabled';
    return;
  }

  let tracked = 0;
  for (const inputSource of session.inputSources) {
    if (!inputSource.hand || (inputSource.handedness !== 'left' && inputSource.handedness !== 'right')) {
      continue;
    }

    const wrist = inputSource.hand.get('wrist');
    const thumb = inputSource.hand.get('thumb-tip');
    const index = inputSource.hand.get('index-finger-tip');
    if (!wrist || !thumb || !index) {
      continue;
    }

    const wristPose = frame.getJointPose(wrist, referenceSpace);
    const thumbPose = frame.getJointPose(thumb, referenceSpace);
    const indexPose = frame.getJointPose(index, referenceSpace);
    if (!wristPose || !thumbPose || !indexPose) {
      signal({ type: 'handPose', side: inputSource.handedness, valid: false });
      continue;
    }

    tracked++;
    const pinchDistance = jointDistance(thumbPose.transform.position, indexPose.transform.position);
    const pinching = pinchDistance <= cfg.pinchThresholdMeters;
    const position = relativePosition(wristPose.transform.position, viewerPose);
    const orientation = wristPose.transform.orientation;
    signal({
      type: 'handPose',
      side: inputSource.handedness,
      valid: true,
      position,
      orientation: { x: orientation.x, y: orientation.y, z: orientation.z, w: orientation.w },
      pinch: pinching,
    });

    if (inputSource.handedness === roadbookPageHand()) {
      const state = handGesture[inputSource.handedness];
      const now = performance.now();
      if (pinching && !state.pinching) {
        state.pinching = true;
        state.startX = position.x;
        state.fired = false;
      } else if (!pinching) {
        state.pinching = false;
        state.fired = false;
      } else if (!state.fired && now - state.lastCommandAt >= cfg.debounceMs) {
        const dx = position.x - state.startX;
        if (Math.abs(dx) >= cfg.swipeThresholdMeters) {
          sendRoadbookCommand(dx > 0 ? 'nextPage' : 'previousPage');
          state.fired = true;
          state.lastCommandAt = now;
        }
      }
    }
  }

  latestHandStatus = tracked ? `hands: ${tracked} tracked` : 'hands: not tracking';
}

async function connect() {
  if (!appConfig.server) {
    await loadConfig().catch(() => {});
  }
  pc?.close();
  pc = makePeerConnection(signal);
  pc.addTransceiver('video', { direction: 'recvonly' });
  pc.addEventListener('connectionstatechange', () => updateStatus());
  pc.addEventListener('track', (event) => {
    video.srcObject = event.streams[0];
    for (const track of event.streams[0].getVideoTracks()) {
      track.addEventListener('mute', () => updateStatus('Video track muted.'));
      track.addEventListener('unmute', () => updateStatus('Video track unmuted.'));
      track.addEventListener('ended', () => updateStatus('Video track ended.'));
    }
    video.play().catch((error) => updateStatus(`Video play failed: ${error.message}`));
    vrButton.disabled = false;
    updateStatus('Video track received.');
  });

  ws = connectSignaling('viewer', async (message) => {
    if (message.type === 'offer') {
      await pc.setRemoteDescription(message.description);
      const answer = await pc.createAnswer();
      await pc.setLocalDescription(answer);
      signal({ type: 'answer', description: pc.localDescription });
      updateStatus('Answer sent.');
    } else if (message.type === 'candidate') {
      await pc.addIceCandidate(message.candidate).catch(() => {});
    } else if (message.type === 'status' || message.type === 'hello') {
      if (message.config) {
        appConfig = message.config;
      }
      updateStatus();
    } else if (message.type === 'poseStatus') {
      latestPoseStatus = message.mapped
        ? `pose mapped: yaw ${message.mapped.yaw.toFixed(1)} pitch ${message.mapped.pitch.toFixed(1)} roll ${message.mapped.roll.toFixed(1)}`
        : `control: ${message.udpPayload ?? message.source ?? 'sent'}`;
      updateStatus();
    }
  });
}

async function enterVr() {
  if (!navigator.xr) {
    updateStatus('WebXR is not available in this browser.');
    return;
  }
  initGl();
  await gl.makeXRCompatible();
  await video.play().catch(() => {});
  session = await navigator.xr.requestSession('immersive-vr', { optionalFeatures: ['hand-tracking'] });
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

  if (pose) {
    for (const view of pose.views) {
      const viewport = layer.getViewport(view);
      gl.viewport(viewport.x, viewport.y, viewport.width, viewport.height);
      drawSolid([0.02, 0.08, 0.12, 1.0]);
      drawVideoForView(view, pose);
    }

    const poseIntervalMs = 1000 / Number(appConfig.pose?.sendHz ?? 60);
    if (time - lastPoseSent > poseIntervalMs) {
      lastPoseSent = time;
      const euler = quaternionToRawEuler(pose.transform.orientation);
      signal({ type: 'pose', ...euler });
    }
    emitHandTracking(frame, pose);
  }
}

connectButton.addEventListener('click', connect);
vrButton.addEventListener('click', enterVr);
recenterButton.addEventListener('click', () => {
  basePose = { ...latestAbsolutePose };
  updateStatus('Recentered raw headset pose.');
});

if (navigator.xr) {
  navigator.xr.isSessionSupported('immersive-vr').then((supported) => {
    vrButton.disabled = !supported;
    updateStatus(supported ? 'Quest Browser can enter immersive VR.' : 'Immersive VR not supported.');
  });
}

loadConfig().finally(updateStatus);
setInterval(() => {
  if (video.srcObject && video.paused) {
    video.play().catch(() => {});
  }
  updateStatus();
}, 1000);
