# Passenger VR Phase 1

This branch adds the first local passenger-camera slice for openRBRVR. It does not submit to a second HMD yet and it does not stream video to Quest yet.

## What is implemented

- New `[PassengerVR]` config section.
- A gated mono passenger render target.
- Passenger view matrix using a co-driver seat offset plus remote yaw/pitch/roll.
- UDP pose input on `posePort`.
- Desktop companion-window output from the passenger render target while passenger mode is enabled.

## Config

```toml
[PassengerVR]
enabled = false
cameraOffset = [-0.55, 0.02, 0.05]
cameraYawDegrees = 0.0
renderMode = "mono"
streamHost = "0.0.0.0"
streamPort = 7790
posePort = 7791
recenterKey = "QuestMenu"
```

Leave `enabled = false` for normal driving. Set it to `true` only when testing the passenger camera.

## Pose Input

Send UDP packets to the RBR PC on `posePort`, default `7791`.

The parser accepts simple text containing at least three float values:

```text
yaw pitch roll
```

Example:

```text
15.0 -2.5 0.0
```

JSON-ish payloads such as `[15.0,-2.5,0.0]` also work because non-number punctuation is ignored.

For a local sweep test:

```powershell
cd "D:\LocalSyncApp\RBR Passenger\tools\pose-sender"
npm run sweep
```

When `debug = true` in `openRBRVR.toml`, the debug text includes the latest PassengerVR yaw/pitch/roll and packet age.

## Quest Browser MVP

The runnable second-headset MVP is in:

```powershell
D:\LocalSyncApp\RBR Passenger\RBRPassengerStreamer
```

Start it with:

```powershell
.\start-passenger-streamer.ps1
```

Open `/streamer.html` on the RBR PC and share the RBR companion window. For Quest Browser WebXR, use USB ADB reverse and open `http://127.0.0.1:7790/quest.html` on the Quest so the page is treated as a secure localhost context. The server forwards Quest headset pose to this plugin over UDP.

## Current Limitations

- The passenger view is mono only.
- The Quest Browser client renders the stream as a headset-fixed mono panel; native Unity packaging is scaffolded but not yet wired to a scene.
- Passenger recenter and seat-adjust events are not implemented yet.
- Passenger rendering temporarily disables multiview during the passenger render pass, then restores the driver's multiview setting.
- Performance has not been profiled in game yet.

## Safe Test Order

1. Build the unmodified fork and confirm it matches the installed openRBRVR behavior.
2. Back up the installed RBR VR binaries and config.
3. Install the rebuilt DLLs only after the baseline is verified.
4. Launch with `PassengerVR.enabled = false` and confirm normal driver VR.
5. Enable passenger mode and confirm the companion window shows the offset co-driver view.
6. Send UDP yaw/pitch/roll packets and confirm only the passenger view moves.
