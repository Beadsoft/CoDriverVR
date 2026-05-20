# CoDriverVR Cloudflare Room Worker

Cloudflare Workers + Durable Objects signalling service for CoDriverVR internet passenger rooms.

This service handles invite rooms, WebSocket signalling, and STUN configuration only. It does not relay video. WebRTC direct peer-to-peer is used by default so the free release avoids TURN bandwidth costs.

## Deploy

```powershell
cd "D:\LocalSyncApp\RBR Passenger\CloudflareRoomWorker"
npm install
npx wrangler login
npx wrangler deploy
```

Wrangler prints a public URL like:

```text
https://codrivervr-rooms.<account>.workers.dev
```

Put that URL into CoDriverVR setup as the internet room server URL.

## Local Dev

```powershell
npx wrangler dev
```

The Worker serves passenger assets from its local `public` folder. If the fallback Node passenger page changes, copy the updated passenger assets into this folder before deployment.

## Cost Policy

Default ICE config is STUN-only:

```text
stun:stun.l.google.com:19302
```

No TURN relay is configured by default. If direct WebRTC fails for a driver/passenger network pair, the app should report that relay would be required rather than silently routing paid video traffic through infrastructure.
