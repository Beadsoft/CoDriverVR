export function connectSignaling(role, onMessage, onOpen) {
  const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const url = new URL(`${protocol}//${location.host}/signal`);
  const roomId = location.pathname.split('/').filter(Boolean).pop();
  if (location.pathname.startsWith('/join/') && roomId) {
    url.searchParams.set('roomId', roomId);
  }
  const ws = new WebSocket(url.toString());

  ws.addEventListener('open', () => {
    ws.send(JSON.stringify({ type: 'hello', role }));
    onOpen?.(ws);
  });

  ws.addEventListener('message', (event) => {
    onMessage(JSON.parse(event.data), ws);
  });

  return ws;
}

export function connectLocalSignaling(role, onMessage, onOpen) {
  const ws = new WebSocket(`ws://${location.host}/signal`);

  ws.addEventListener('open', () => {
    ws.send(JSON.stringify({ type: 'hello', role }));
    onOpen?.(ws);
  });

  ws.addEventListener('message', (event) => {
    onMessage(JSON.parse(event.data), ws);
  });

  return ws;
}

export function makePeerConnection(sendSignal) {
  const pc = new RTCPeerConnection({
    iceServers: [],
  });

  pc.addEventListener('icecandidate', (event) => {
    if (event.candidate) {
      sendSignal({ type: 'candidate', candidate: event.candidate });
    }
  });

  return pc;
}

export function signalUrl() {
  const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const url = new URL(`${protocol}//${location.host}/signal`);
  const roomId = location.pathname.split('/').filter(Boolean).pop();
  if (roomId) {
    url.searchParams.set('roomId', roomId);
  }
  return url.toString();
}

export function parseToken() {
  const params = new URLSearchParams(location.hash.replace(/^#/, ''));
  return params.get('token') ?? '';
}

export function setStatus(element, lines) {
  element.textContent = Array.isArray(lines) ? lines.join('\n') : String(lines);
}
