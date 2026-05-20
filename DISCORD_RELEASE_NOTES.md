# CoDriverVR Preview

Experimental RBR/openRBRVR passenger + roadbook test build.

What it does:
- Adds a second passenger camera stream for Quest Browser.
- Adds a VR roadbook/pacenote panel.
- Streams the RBR companion window over local WebRTC.
- Sends Quest head pose back to openRBRVR over UDP.
- Includes a setup page for camera position, panel size/distance, bitrate, FPS, pose prediction, and smoothing.

Requirements:
- RSF RBR.
- SteamVR/OpenVR already working.
- Node.js 20 LTS or newer.
- Quest Browser. ADB is optional but useful for one-click launch.

Known limitations:
- This is local-network/browser streaming, not native Quest yet.
- Browser window capture still requires one manual `Share RBR window` click.
- Camera offset changes require restarting RBR.
- Latency is tunable but not eliminated; native capture/client work is the next upgrade.

Install:
1. Close RBR.
2. Extract the zip.
3. Run PowerShell:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\Install-CoDriverVR.ps1 -RbrRoot "C:\richard burns rally"
```

Run:

```powershell
%LOCALAPPDATA%\CoDriverVR\Start-CoDriverVR.ps1
```

Then open:

```text
http://127.0.0.1:7790/setup.html
```
