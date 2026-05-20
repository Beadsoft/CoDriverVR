using System;
using System.Net.Sockets;
using System.Text;
using UnityEngine;
using UnityEngine.XR;

public sealed class PassengerPoseSender : MonoBehaviour
{
    [SerializeField] private string host = "127.0.0.1";
    [SerializeField] private int port = 7791;
    [SerializeField] private float sendHz = 30f;

    private UdpClient udp;
    private float nextSendTime;
    private Quaternion recenter = Quaternion.identity;

    private void OnEnable()
    {
        udp = new UdpClient();
    }

    private void OnDisable()
    {
        udp?.Dispose();
        udp = null;
    }

    private void Update()
    {
        if (Time.unscaledTime < nextSendTime || udp == null)
        {
            return;
        }

        nextSendTime = Time.unscaledTime + (1f / Mathf.Max(1f, sendHz));

        var device = InputDevices.GetDeviceAtXRNode(XRNode.CenterEye);
        if (!device.TryGetFeatureValue(CommonUsages.centerEyeRotation, out var rotation))
        {
            rotation = Camera.main != null ? Camera.main.transform.rotation : transform.rotation;
        }

        var euler = (Quaternion.Inverse(recenter) * rotation).eulerAngles;
        var yaw = NormalizeDegrees(euler.y);
        var pitch = NormalizeDegrees(euler.x);
        var roll = NormalizeDegrees(euler.z);
        var payload = $"{yaw:F3} {pitch:F3} {roll:F3}";
        var bytes = Encoding.ASCII.GetBytes(payload);
        udp.Send(bytes, bytes.Length, host, port);
    }

    public void Recenter()
    {
        var device = InputDevices.GetDeviceAtXRNode(XRNode.CenterEye);
        if (device.TryGetFeatureValue(CommonUsages.centerEyeRotation, out var rotation))
        {
            recenter = rotation;
        }
    }

    private static float NormalizeDegrees(float value)
    {
        value = Mathf.Repeat(value + 180f, 360f) - 180f;
        return Math.Abs(value) < 0.001f ? 0f : value;
    }
}
