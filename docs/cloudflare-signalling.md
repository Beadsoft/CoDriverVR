# Cloudflare Signalling Deployment

CoDriverVR internet mode uses a Cloudflare Worker + Durable Objects as the default free signalling service. The Worker only relays room setup, WebRTC offers/answers, ICE candidates, and passenger config. It does not relay video.

## Deploy

```powershell
cd "D:\LocalSyncApp\RBR Passenger\CloudflareRoomWorker"
npm install
npx wrangler login
npx wrangler deploy
```

Wrangler prints a URL like:

```text
https://codrivervr-rooms.<account>.workers.dev
```

Use that URL in CoDriverVR:

1. Start CoDriverVR.
2. Open `http://127.0.0.1:7790/setup.html`.
3. Enable internet passenger rooms.
4. Paste the Worker URL into Room server URL.
5. Keep “Require public HTTPS invite URLs” enabled.
6. Click Test server, then Save internet settings.

## Runtime Model

- Driver PC opens local `internet-driver.html`.
- Driver page creates a room on the Worker.
- The invite URL points to `workers.dev`, not `127.0.0.1`.
- Driver and passenger exchange WebRTC signalling through the Worker.
- Video/audio/pose/hand/roadbook traffic goes peer-to-peer after WebRTC connects.

## Free Build Policy

Default ICE is STUN-only:

```text
stun:stun.l.google.com:19302
```

No TURN relay is configured by default. If direct WebRTC fails, the app reports that a relay would be required instead of using paid relay bandwidth.

## AWS Note

AWS SAM is not used for the Cloudflare deployment. The old Node room server can still run on EC2 as a fallback, but the free default path is Wrangler deploying the Worker to Cloudflare.
