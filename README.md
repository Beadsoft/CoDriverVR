# CoDriverVR

CoDriverVR is an experimental Richard Burns Rally/openRBRVR passenger mode for Quest headsets.

The current alpha focuses on proving the passenger video/link path: the driver starts RBR on the PC, opens a passenger room from the in-game plugin menu, shares an HTTPS invite link, and a second Quest joins as the remote co-driver/passenger.

## Alpha Status

This is a proof-of-concept alpha, not a polished release.

Working in this build:

- CoDriverVR appears as its own RBR plugin menu.
- The driver can start a passenger room from VR.
- The streamer starts a temporary Cloudflare `trycloudflare.com` HTTPS tunnel automatically.
- The passenger Quest can open the invite link in Quest Browser.
- WebRTC video and pose transport work when the network allows direct peer-to-peer traffic.
- The release package bundles the streamer dependencies and `cloudflared.exe`.

Known rough areas:

- Passenger menus do not load correctly yet.
- Pacenote/roadbook tracking is currently unstable.
- Hand tracking is experimental.
- There is no TURN relay in this free alpha, so some networks may connect to signalling but fail direct video.
- Cloudflare quick tunnel URLs change when the streamer restarts.

## Download

Use the latest alpha release:

<https://github.com/Beadsoft/CoDriverVR/releases/tag/alpha-https-lan-poc-20260520>

If the release asset has not been attached yet, download the alpha zip from the project maintainer rather than cloning the repository.

## Install

1. Close Richard Burns Rally.
2. Extract `CoDriverVR-alpha-https-lan-poc-20260520.zip`.
3. Open PowerShell in the extracted folder.
4. Run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\Install-CoDriverVR.ps1 -RbrRoot "C:\richard burns rally"
```

Use your actual RBR install path if it is different.

The installer copies the plugin DLLs into `Plugins`, installs the streamer under `%LOCALAPPDATA%\CoDriverVR`, creates/updates the config, and keeps backups of replaced plugin files.

## Start

Run:

```powershell
%LOCALAPPDATA%\CoDriverVR\Start-CoDriverVR.ps1
```

Then start RBR.

In RBR VR:

1. Open `Options`.
2. Open `Plugins`.
3. Open `CoDriverVR Passenger Room`.
4. Select `Start room`.
5. Share the generated `https://...trycloudflare.com/...` invite link.

Ignore any `http://127.0.0.1` or `http://192.168...` link for Quest VR testing. Quest Browser needs HTTPS for WebXR VR mode.

## Passenger Quest

On the passenger Quest:

1. Open the `https://...trycloudflare.com/...` invite link in Quest Browser.
2. Join the room.
3. Wait for the video to appear.
4. Press the VR button when available.

If the page connects but video fails, the network is probably blocking direct WebRTC peer-to-peer traffic. This alpha does not include a paid relay/TURN fallback.

## Developer Notes

Main components:

- `openRBRVR`: the RBR VR plugin code, including `PassengerVR`, `RoadbookVR`, and the CoDriverVR in-game menu.
- `RBRPassengerStreamer`: the local Electron/WebRTC/WebXR streamer.
- `RBRPassengerQuestUnity`: an early scaffold for a later native Quest client.
- `CloudflareRoomWorker`: optional Cloudflare Worker signalling implementation for future hosted signalling.

Build openRBRVR from `openRBRVR`:

```powershell
$env:ZIG_GLOBAL_CACHE_DIR="$PWD\.zig-global-cache"
.\zig-x86_64-windows-0.15.2\zig.exe build --release=fast
```

Run streamer checks from `RBRPassengerStreamer`:

```powershell
npm run check
```
