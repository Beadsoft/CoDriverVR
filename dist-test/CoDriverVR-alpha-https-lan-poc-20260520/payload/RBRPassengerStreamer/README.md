# RBR Passenger Streamer

This is the runnable LAN MVP for the second passenger headset.

It now includes a reusable harness so the same pose/video link can be tested against RBR first and other games later.

## Run

```powershell
cd "D:\LocalSyncApp\RBR Passenger\RBRPassengerStreamer"
npm start
```

Open the printed streamer URL on the RBR PC. Click `Share RBR window` and select the RBR/openRBRVR companion window that shows the passenger view.

Open the printed Quest URL in Quest Browser. Click `Connect`, then `Enter VR`.

For the headset-first driver flow, install dependencies once and let the in-game `CoDriverVR Passenger Room` menu start the embedded Electron host:

```powershell
npm install
npm start
```

The native openRBRVR menu calls the local `/api/driver-room/*` endpoints. The Electron host auto-captures the RBR/openRBRVR companion window, creates the passenger room, and keeps the WebRTC driver peer alive without a browser window picker.

## Harness

The PC setup page is:

```text
http://127.0.0.1:7790/setup.html
```

It lists ADB devices, enables WiFi ADB from a USB-connected Quest, selects the passenger device, and launches the Quest viewer.

Open:

```text
http://127.0.0.1:7790/harness.html
```

Use `Output yaw +20`, `Output pitch +20`, and `Output roll +20` to test the openRBRVR/game camera directly. These packets bypass headset axis mapping.

Use `Raw yaw +20`, `Raw pitch +20`, and `Raw roll +20` to test the configured headset axis mapping. The current default mapping matches the observed Quest/WebXR issue:

```json
{
  "yaw": { "source": "pitch", "scale": 1, "offset": 0 },
  "pitch": { "source": "roll", "scale": 1, "offset": 0 },
  "roll": { "source": "yaw", "scale": 1, "offset": 0 }
}
```

On the PC streamer page, `Use test pattern` sends a known 16:9 grid to the Quest. Use it before RBR capture to check crop, flip, stretch, and headset framing.

## Config

Edit `config.json` to choose the game target, pose mapping, viewer mode, and Quest device:

```json
{
  "quest": {
    "adbPath": "C:\\Users\\mintl\\Downloads\\platform-tools_r34.0.5-windows\\platform-tools\\adb.exe",
    "serial": "2G0YC5ZH1400XK",
    "useAdbReverse": true,
    "launchPath": "/quest.html"
  }
}
```

The Quest viewer defaults to `viewer.mode: "fullscreen"` so the streamed
passenger view fills each XR eye buffer. The meter-based `viewer.quad` settings
are only used when `viewer.mode` is explicitly set to `"framed"` for diagnostics.

To find the configured game window:

```powershell
.\find-game-window.ps1
```

To push the Quest browser to the configured URL:

```powershell
.\launch-quest-viewer.ps1
```

## WiFi ADB

For easier passenger setup, use ADB over WiFi while keeping `useAdbReverse` enabled. This means the Quest still opens:

```text
http://127.0.0.1:7790/quest.html
```

That is intentional: Quest Browser WebXR is much more reliable from `127.0.0.1` than from a plain LAN `http://192.168.x.x` address.

One-time setup with USB connected:

```powershell
.\enable-quest-adb-wifi.ps1 -UpdateConfig
```

Then unplug USB and launch over WiFi:

```powershell
.\launch-quest-viewer.ps1
```

Or use the setup page:

```powershell
.\open-passenger-setup.ps1
```

The script stores the target in `config.json`:

```json
{
  "quest": {
    "connectionMode": "wifi",
    "wifiSerial": "192.168.1.50:5555",
    "useAdbReverse": true
  }
}
```

The browser driver page remains as a diagnostic fallback. The ship workflow is the embedded Electron host because it can select the RBR companion window without the browser `getDisplayMedia` picker.

## Internet Mode

Internet passenger mode defaults to the Cloudflare Worker signalling service in `D:\LocalSyncApp\RBR Passenger\CloudflareRoomWorker`. The older Node room server in `D:\LocalSyncApp\RBR Passenger\RBRInternetRoomServer` remains useful for local development and EC2 fallback tests.

Open the local driver page:

```text
http://127.0.0.1:7790/internet-driver.html
```

The driver creates a room on the configured room server, shares the invite link, and keeps the local harness open so passenger pose, hand tracking, and roadbook commands can still be forwarded to openRBRVR.

The internet mode does not expose this local ADB/setup API. Its transport route is:

- authenticated signaling server for pairing the driver PC and passenger headset
- WebRTC STUN for direct NAT traversal
- video over WebRTC as now
- pose/recenter/hand/roadbook messages over a WebRTC data channel instead of raw UDP
- optional invite code so the driver can choose exactly which remote passenger joins

The setup page can enable internet mode, set the room server URL, and test it. The driver page blocks localhost room servers when public internet mode is required, copies the invite link, uses the browser share sheet when available, or copies the invite and opens Messenger. The free build does not configure TURN; if direct WebRTC fails, the UI reports that relay would be required.

For same-WiFi LAN tests, the app exposes a LAN room server URL based on the PC's detected IPv4 address, such as `http://192.168.1.50:7790`. Use `Use LAN room server` on the driver page. Windows Firewall must allow inbound TCP `7790`; the installer/start scripts try to create a Private-network rule automatically.

## Driver Room Control

The in-game `CoDriverVR Passenger Room` menu provides:

- `Start room`, `Stop room`, and `Recreate invite`
- LAN/Public mode switching
- driver mic toggle
- `Open share page on Quest`
- status for invite, passenger join, signalling, direct WebRTC, capture source, and mic state

The share page opens at `http://127.0.0.1:7790/driver-share.html?...` on the driver Quest through ADB reverse when available. It uses the Quest/browser share sheet via `navigator.share()`, then falls back to copying the invite and opening Messenger/Facebook.

## Quest Browser WebXR Note

WebXR normally requires a secure context. For the quickest local test, connect the Quest to the PC over USB with developer mode enabled and run:

```powershell
.\enable-quest-adb-reverse.ps1
```

Then open this in Quest Browser:

```text
http://127.0.0.1:7790/quest.html
```

The PC streamer page can still be opened at:

```text
http://127.0.0.1:7790/streamer.html
```

## Data Flow

- The embedded Electron host captures the RBR passenger monitor view automatically. The PC browser path remains available for diagnostics.
- WebRTC sends the video stream over LAN to the Quest Browser page.
- Quest Browser WebXR supplies headset orientation.
- The Node server forwards Quest orientation to openRBRVR as UDP `yaw pitch roll` on `127.0.0.1:7791`.

## Notes

- This uses side-by-side passenger stereo from openRBRVR and a fullscreen WebXR viewer in Quest Browser.
- No internet relay is used.
- The server uses a small built-in WebSocket implementation to avoid external npm dependencies.
