const encoder = new TextEncoder();

function json(body, status = 200) {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      'content-type': 'application/json; charset=utf-8',
      'access-control-allow-origin': '*',
      'access-control-allow-methods': 'GET,POST,OPTIONS',
      'access-control-allow-headers': 'content-type',
    },
  });
}

function token(bytes = 18) {
  const data = new Uint8Array(bytes);
  crypto.getRandomValues(data);
  let binary = '';
  for (const byte of data) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

async function sha256Base64Url(value) {
  const digest = await crypto.subtle.digest('SHA-256', encoder.encode(value));
  let binary = '';
  for (const byte of new Uint8Array(digest)) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

async function hashPassword(password, salt = token(12)) {
  if (!password) return null;
  return { salt, digest: await sha256Base64Url(`${salt}:${password}`) };
}

async function verifyPassword(password, stored) {
  if (!stored) return true;
  return (await sha256Base64Url(`${stored.salt}:${password}`)) === stored.digest;
}

function publicOrigin(request) {
  const url = new URL(request.url);
  const forwardedHost = request.headers.get('x-forwarded-host');
  const forwardedProto = request.headers.get('x-forwarded-proto');
  if (forwardedHost) {
    return `${forwardedProto ?? 'https'}://${forwardedHost}`.replace(/\/+$/, '');
  }
  return url.origin.replace(/\/+$/, '');
}

function iceServers(env) {
  return [{ urls: [env.DEFAULT_STUN_URL || 'stun:stun.l.google.com:19302'] }];
}

function publicRoom(room, driverConnected = false, passengerConnected = false) {
  return {
    roomId: room.roomId,
    visibility: room.visibility,
    createdAt: room.createdAt,
    hasPassword: Boolean(room.passwordHash),
    driverConnected,
    passengerConnected,
  };
}

function socketSend(socket, message) {
  try {
    socket?.send(JSON.stringify(message));
  } catch {
    try {
      socket?.close();
    } catch {
      // Ignore close failures.
    }
  }
}

async function readJson(request) {
  try {
    return await request.json();
  } catch {
    return {};
  }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (request.method === 'OPTIONS') {
      return json({}, 204);
    }

    if (url.pathname === '/api/rooms' && request.method === 'POST') {
      const body = await readJson(request);
      const roomId = token(8);
      const driverToken = token();
      const passengerToken = token();
      const room = {
        roomId,
        visibility: body.visibility === 'open' ? 'open' : 'private',
        driverToken,
        passengerToken,
        passwordHash: await hashPassword(body.password ?? ''),
        passengerConfig: body.passengerConfig && typeof body.passengerConfig === 'object' ? body.passengerConfig : {},
        createdAt: new Date().toISOString(),
      };

      const stub = env.ROOMS.get(env.ROOMS.idFromName(roomId));
      await stub.fetch('https://room/create', {
        method: 'POST',
        body: JSON.stringify(room),
      });

      if (room.visibility === 'open') {
        const index = env.ROOM_INDEX.get(env.ROOM_INDEX.idFromName('global'));
        await index.fetch('https://index/update', {
          method: 'POST',
          body: JSON.stringify({ ...publicRoom(room), driverConnected: false, passengerConnected: false }),
        });
      }

      return json({
        roomId,
        driverToken,
        passengerInviteUrl: `${publicOrigin(request)}/join/${roomId}#token=${passengerToken}`,
        iceServers: iceServers(env),
      });
    }

    if (url.pathname === '/api/rooms/open' && request.method === 'GET') {
      const index = env.ROOM_INDEX.get(env.ROOM_INDEX.idFromName('global'));
      return index.fetch('https://index/open');
    }

    if (url.pathname === '/signal') {
      const roomId = url.searchParams.get('roomId');
      if (!roomId) return json({ error: 'Missing roomId.' }, 400);
      const stub = env.ROOMS.get(env.ROOMS.idFromName(roomId));
      return stub.fetch(request);
    }

    if (url.pathname.startsWith('/join/')) {
      return env.ASSETS.fetch(new Request(new URL('/passenger.html', request.url), request));
    }

    return env.ASSETS.fetch(request);
  },
};

export class RoomIndex {
  constructor(state) {
    this.state = state;
  }

  async fetch(request) {
    const url = new URL(request.url);
    if (url.pathname === '/update') {
      const room = await request.json();
      await this.state.storage.put(room.roomId, room);
      return json({ ok: true });
    }
    if (url.pathname === '/delete') {
      const { roomId } = await request.json();
      await this.state.storage.delete(roomId);
      return json({ ok: true });
    }
    const rooms = await this.state.storage.list();
    return json([...rooms.values()]);
  }
}

export class RoomObject {
  constructor(state, env) {
    this.state = state;
    this.env = env;
    this.room = null;
    this.driver = null;
    this.passenger = null;
    this.expiryTimer = null;
  }

  async loadRoom() {
    if (!this.room) {
      this.room = (await this.state.storage.get('room')) ?? null;
    }
    return this.room;
  }

  roomStatus() {
    return {
      type: 'room:status',
      ...publicRoom(this.room, Boolean(this.driver?.socket), Boolean(this.passenger?.socket)),
    };
  }

  async updateIndex() {
    if (this.room?.visibility !== 'open') return;
    const index = this.env.ROOM_INDEX.get(this.env.ROOM_INDEX.idFromName('global'));
    await index.fetch('https://index/update', {
      method: 'POST',
      body: JSON.stringify(this.roomStatus()),
    });
  }

  broadcast() {
    const status = this.roomStatus();
    socketSend(this.driver?.socket, status);
    socketSend(this.passenger?.socket, status);
    this.updateIndex();
  }

  async expireIfDriverGone() {
    const graceMs = Number(this.env.ROOM_RECONNECT_GRACE_SECONDS ?? 20) * 1000;
    clearTimeout(this.expiryTimer);
    this.expiryTimer = setTimeout(async () => {
      if (this.driver?.socket) return;
      socketSend(this.passenger?.socket, { type: 'room:error', error: 'Driver disconnected. Room expired.' });
      this.passenger?.socket?.close();
      if (this.room?.visibility === 'open') {
        const index = this.env.ROOM_INDEX.get(this.env.ROOM_INDEX.idFromName('global'));
        await index.fetch('https://index/delete', {
          method: 'POST',
          body: JSON.stringify({ roomId: this.room.roomId }),
        });
      }
      await this.state.storage.deleteAll();
      this.room = null;
    }, graceMs);
  }

  async authenticate(message) {
    if (message.role === 'driver') return message.token === this.room.driverToken;
    if (message.role === 'passenger') {
      const tokenOk = this.room.visibility === 'open' || message.token === this.room.passengerToken;
      return tokenOk && (await verifyPassword(message.password ?? '', this.room.passwordHash));
    }
    return false;
  }

  async join(socket, message) {
    await this.loadRoom();
    if (!this.room) {
      socketSend(socket, { type: 'room:error', error: 'Room not found.' });
      return;
    }
    if (!(await this.authenticate(message))) {
      socketSend(socket, { type: 'room:error', error: 'Room token or password was rejected.' });
      return;
    }
    if (message.role === 'passenger' && this.passenger?.socket) {
      socketSend(socket, { type: 'room:error', error: 'Passenger seat is already occupied.' });
      return;
    }

    socket.role = message.role;
    if (message.role === 'driver') {
      this.driver = { socket };
      clearTimeout(this.expiryTimer);
    } else {
      this.passenger = { socket };
    }

    socketSend(socket, {
      type: 'room:joined',
      room: publicRoom(this.room, Boolean(this.driver?.socket), Boolean(this.passenger?.socket)),
      role: message.role,
      iceServers: iceServers(this.env),
    });
    if (message.role === 'passenger') {
      socketSend(socket, { type: 'passenger:config', config: this.room.passengerConfig ?? {} });
    }
    this.broadcast();
  }

  relay(socket, message) {
    const target = socket.role === 'driver' ? this.passenger : this.driver;
    if (!target?.socket) {
      socketSend(socket, { type: 'room:error', error: 'The other peer is not connected yet.' });
      return;
    }
    socketSend(target.socket, { ...message, from: socket.role });
  }

  async fetch(request) {
    const url = new URL(request.url);
    if (url.pathname === '/create') {
      this.room = await request.json();
      await this.state.storage.put('room', this.room);
      return json({ ok: true });
    }

    if (request.headers.get('upgrade') !== 'websocket') {
      return json({ error: 'WebSocket required.' }, 426);
    }

    const pair = new WebSocketPair();
    const client = pair[0];
    const server = pair[1];
    server.accept();

    server.addEventListener('message', async (event) => {
      let message;
      try {
        message = JSON.parse(event.data);
      } catch {
        socketSend(server, { type: 'room:error', error: 'Invalid JSON.' });
        return;
      }

      if (message.type === 'room:join') {
        await this.join(server, message);
      } else if (message.type === 'room:leave') {
        server.close();
      } else if (message.type === 'signal:offer' || message.type === 'signal:answer' || message.type === 'signal:candidate') {
        this.relay(server, message);
      }
    });

    server.addEventListener('close', async () => {
      if (this.driver?.socket === server) {
        this.driver = null;
        await this.expireIfDriverGone();
      } else if (this.passenger?.socket === server) {
        this.passenger = null;
      }
      if (this.room) this.broadcast();
    });

    return new Response(null, { status: 101, webSocket: client });
  }
}
