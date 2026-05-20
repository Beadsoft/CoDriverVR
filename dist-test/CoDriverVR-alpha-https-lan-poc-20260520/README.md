# CoDriverVR Alpha Test Guide

CoDriverVR adds an experimental remote passenger VR view to RBR/openRBRVR.

This alpha is a proof of concept. The passenger video/link path is the focus. Passenger menu loading, pacenote tracking, and hand/roadbook polish are known rough areas.

## Requirements

- RallySimFans RBR with openRBRVR working.
- SteamVR/OpenVR working on the driver PC.
- Meta Quest Browser on the passenger headset.
- Node.js 20 LTS or newer on the driver PC.
- Both PC and passenger Quest should be on the same Wi-Fi/LAN for lowest latency.

## Install

1. Close RBR and the RSF launcher.
2. Extract the zip.
3. Open PowerShell in the extracted folder.
4. Run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\Install-CoDriverVR.ps1 -RbrRoot "C:\richard burns rally"
```

If the tester does not already have the openRBRVR DXVK `d3d9.dll`, run:

```powershell
.\Install-CoDriverVR.ps1 -RbrRoot "C:\richard burns rally" -InstallDxvk
```

The installer backs up existing plugin files before replacing them.

## Start

1. Start the streamer:

```powershell
%LOCALAPPDATA%\CoDriverVR\Start-CoDriverVR.ps1
```

2. Open the driver control page on the PC:

```text
http://127.0.0.1:7790/internet-driver.html
```

3. Confirm `https lan tunnel` shows a `https://...trycloudflare.com` URL.
   If it does not, click `Start HTTPS LAN tunnel`.

4. Launch RBR normally.

5. In the RBR VR main menu, open:

```text
Options -> Plugins -> CoDriverVR Passenger Room
```

6. Select `Start room`.

7. Select `Open share page on Quest`, or copy the HTTPS invite from the PC control page.

## Passenger Quest

1. Open the `https://...trycloudflare.com/join/...` invite in Quest Browser.
2. Press `Join room`.
3. Wait for the flat video preview.
4. Press `Enter VR`.

Use the HTTPS `trycloudflare.com` invite for Quest testing. Do not use the `http://192.168...` LAN invite for VR mode; LAN HTTP can show flat video but Quest Browser may block immersive VR from that origin.

## Expected Status

On the driver menu/control page:

```text
mode: secure-lan
passenger: joined
signaling: connected
direct WebRTC: connected
capture: Richard Burns Rally - DirectX9
```

The Cloudflare tunnel provides HTTPS page/signalling access. The actual WebRTC video still tries to connect directly between the PC and Quest, so LAN latency should remain low when the network allows direct peer connection.

## Known Alpha Issues

- Passenger RBR menus may not load/render correctly yet.
- Passenger pacenote/roadbook tracking is not polished.
- Direct WebRTC can still fail on guest Wi-Fi, isolated Wi-Fi, VPNs, or restrictive firewalls.
- The temporary `trycloudflare.com` URL changes when the tunnel restarts.

## Troubleshooting

- If the invite is `http://192.168...`, recreate the room after the HTTPS LAN tunnel is running.
- If `Enter VR` is unavailable, confirm the passenger URL starts with `https://`.
- If video appears flat but `Enter VR` fails, refresh the Quest page and press `Join room` again.
- If `Direct WebRTC: failed`, make sure the Quest is on the same non-guest Wi-Fi as the PC.
- If the room looks stale, select `Stop room`, then `Start room`.

## Rollback

The installer creates timestamped backups in the RBR folder. Restore the newest matching backup if needed:

```powershell
Copy-Item "C:\richard burns rally\Plugins\OpenRBRVR.dll.bak-YYYYMMDD-HHMMSS" "C:\richard burns rally\Plugins\OpenRBRVR.dll" -Force
Copy-Item "C:\richard burns rally\Plugins\CoDriverVR.dll.bak-YYYYMMDD-HHMMSS" "C:\richard burns rally\Plugins\CoDriverVR.dll" -Force
Copy-Item "C:\richard burns rally\Plugins\openRBRVR.toml.bak-YYYYMMDD-HHMMSS" "C:\richard burns rally\Plugins\openRBRVR.toml" -Force
```
