export function setStatus(element, lines) {
  element.textContent = Array.isArray(lines) ? lines.filter(Boolean).join('\n') : String(lines);
}

export function signalUrl() {
  const url = new URL('/signal', location.href);
  const roomId = location.pathname.split('/').filter(Boolean).pop();
  if (roomId) {
    url.searchParams.set('roomId', roomId);
  }
  url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
  return url.toString();
}

export function parseToken() {
  const params = new URLSearchParams(location.hash.replace(/^#/, ''));
  return params.get('token') ?? '';
}
