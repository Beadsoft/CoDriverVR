# CoDriverVR

CoDriverVR is the combined RBR VR passenger and roadbook solution.

## Components

- `openRBRVR`: the combined in-game VR plugin. It contains:
  - `PassengerVR`, which renders the passenger camera into left/right passenger eyes.
  - `RoadbookVR`, which renders the native pacenote panel into the driver eyes and passenger eyes.
- `RBRPassengerStreamer`: the Electron/WebRTC/WebXR passenger stream app that captures the companion/passenger window and sends Quest pose back to openRBRVR.
- `RBRPassengerQuestUnity`: the Unity Quest client scaffold for a later native passenger client.

## Build openRBRVR

From `openRBRVR`:

```powershell
$env:ZIG_GLOBAL_CACHE_DIR="$PWD\.zig-global-cache"
.\zig-x86_64-windows-0.15.2\zig.exe build --release=fast
```

The build output is written to:

```text
openRBRVR\zig-out\bin\openRBRVR.dll
```

## Runtime Config

Enable both features in `C:\richard burns rally\Plugins\openRBRVR.toml`:

```toml
swapEyes = false

[PassengerVR]
enabled = true
renderMode = 'stereo'
posePort = 7791

[RoadbookVR]
enabled = true
rbrRoot = 'C:\richard burns rally'
source = 'ngpMyPacenotes'
lockHand = 'left'
pageHand = 'right'
driverVisible = true
passengerVisible = true
panelWidthMeters = 0.55
panelHeightMeters = 0.38
panelOffset = [0.04, 0.04, 0.12]
panelTiltDegrees = [-18.0, 0.0, 0.0]
notesPerPage = 12
fallbackPose = 'head'
```

## Controller Controls

- Left controller: default panel anchor.
- Right joystick/touchpad right: next page.
- Right joystick/touchpad left: previous page.
- Right grip: show/hide roadbook.
- Right trigger: reset to page 1.

## Passenger Hand Roadbook

Quest Browser hand tracking can now drive the passenger roadbook. `lockHand` chooses the hand that holds the pacenote book, and `pageHand` chooses the hand that turns pages. The passenger pinches with the page hand and swipes left/right to change page. If hands are unavailable, the roadbook falls back to the configured head/fallback pose and existing controller controls are unchanged.

## Internet Passenger

The normal installer asks whether to enable internet rooms. The intended driver flow is now in-headset: open the RBR VR plugin menu, choose `CoDriverVR Passenger Room`, start the room, and open the Quest share page. The embedded Electron host captures the passenger companion window automatically and uses the configured Cloudflare Worker or LAN room server for WebRTC signaling.

The local browser page at `http://127.0.0.1:7790/internet-driver.html` remains available as a diagnostic fallback.

Deploy the default free signalling service from `D:\LocalSyncApp\RBR Passenger\CloudflareRoomWorker`, then paste the resulting `workers.dev` URL into setup. See `docs/cloudflare-signalling.md`.
