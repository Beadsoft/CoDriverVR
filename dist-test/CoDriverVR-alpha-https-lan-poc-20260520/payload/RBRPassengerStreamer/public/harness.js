import { connectSignaling, setStatus } from './common.js';

const connectButton = document.querySelector('#connect');
const zeroButton = document.querySelector('#zero');
const status = document.querySelector('#status');
const configElement = document.querySelector('#config');

let ws;
let config = {};
let latestPoseStatus = null;
let clients = [];

function signal(message) {
  if (ws?.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(message));
  }
}

function pose(axis, value) {
  return {
    yaw: axis === 'yaw' ? value : 0,
    pitch: axis === 'pitch' ? value : 0,
    roll: axis === 'roll' ? value : 0,
  };
}

function updateStatus(extra = '') {
  const poseLines = latestPoseStatus
    ? latestPoseStatus.mapped
      ? [
          `source: ${latestPoseStatus.source}`,
          `raw:    yaw ${latestPoseStatus.raw.yaw.toFixed(1)} pitch ${latestPoseStatus.raw.pitch.toFixed(1)} roll ${latestPoseStatus.raw.roll.toFixed(1)}`,
          `mapped: yaw ${latestPoseStatus.mapped.yaw.toFixed(1)} pitch ${latestPoseStatus.mapped.pitch.toFixed(1)} roll ${latestPoseStatus.mapped.roll.toFixed(1)}`,
          `udp:    ${latestPoseStatus.udpPayload}`,
          `sent:   ${latestPoseStatus.sentAt}`,
        ]
      : [
          `source: ${latestPoseStatus.source}`,
          `control: ${latestPoseStatus.udpPayload}`,
          `sent:    ${latestPoseStatus.sentAt}`,
        ]
    : ['pose: no packets sent yet'];

  setStatus(status, [
    `signaling: ${ws?.readyState === WebSocket.OPEN ? 'connected' : 'offline'}`,
    `clients: ${clients.map((client) => `${client.role}:${client.id.slice(0, 8)}`).join(', ') || 'none'}`,
    ...poseLines,
    extra,
  ].filter(Boolean));
}

async function loadConfig() {
  const response = await fetch('/api/config', { cache: 'no-store' });
  config = await response.json();
  setStatus(configElement, [
    `game: ${config.game?.name ?? 'unknown'}`,
    `pose target: ${config.server?.poseHost ?? '127.0.0.1'}:${config.server?.posePort ?? 7791}`,
    `quest serial: ${config.quest?.serial || '(first authorized device)'}`,
    `viewer mode: ${config.viewer?.mode ?? 'fullscreen'}`,
    `stereo: ${config.viewer?.stereoLayout ?? 'mono'} swapEyes=${config.viewer?.swapEyes === true}`,
    `framed quad: ${JSON.stringify(config.viewer?.quad ?? {})}`,
    `axis map: ${JSON.stringify(config.pose?.axisMap ?? {})}`,
  ]);
}

connectButton.addEventListener('click', () => {
  ws = connectSignaling('harness', (message) => {
    if (message.type === 'hello' || message.type === 'status') {
      clients = message.clients ?? clients;
      if (message.config) {
        config = message.config;
      }
      updateStatus();
    } else if (message.type === 'poseStatus') {
      latestPoseStatus = message;
      updateStatus();
    }
  });
});

for (const button of document.querySelectorAll('button[data-axis]')) {
  button.addEventListener('click', () => {
    const axis = button.dataset.axis;
    const kind = button.dataset.kind;
    signal({ type: kind, ...pose(axis, 20) });
    updateStatus(`${kind} ${axis} +20 sent.`);
  });
}

zeroButton.addEventListener('click', () => {
  signal({ type: 'mappedPose', yaw: 0, pitch: 0, roll: 0 });
  updateStatus('Mapped zero pose sent.');
});

loadConfig().finally(updateStatus);
