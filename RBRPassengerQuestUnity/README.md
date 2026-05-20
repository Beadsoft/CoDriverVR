# RBR Passenger Quest Unity Viewer

This is the native Quest app scaffold for the passenger viewer. The runnable MVP is currently the browser/WebXR viewer in `../RBRPassengerStreamer`, because it can be tested immediately from Quest Browser without sideloading.

## Intended Native Path

- Unity 6000.x Android build.
- XR Plug-in Management with OpenXR enabled.
- Meta Quest OpenXR feature group enabled.
- Unity WebRTC package for receiving the PC stream.
- Send headset yaw/pitch/roll to the PC signaling server, which forwards pose to openRBRVR UDP `posePort`.

## Open In Unity

Open this folder as a Unity project after installing Android Build Support for the selected Unity editor.

The scripts in `Assets/Scripts` define the native viewer behavior but are not yet wired to a scene. The next native-app task is to create an XR Origin scene, a video surface, and a small status UI.
