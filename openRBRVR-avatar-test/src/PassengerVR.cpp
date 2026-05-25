#include "PassengerVR.hpp"
#include "Config.hpp"
#include "Util.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <format>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <gtc/matrix_transform.hpp>
#include <gtx/euler_angles.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

namespace passenger_vr {
    namespace {
        SOCKET pose_socket = INVALID_SOCKET;
        bool winsock_started = false;
        int active_pose_port = 0;
        std::mutex pose_mutex;
        glm::vec3 latest_euler_degrees = { 0.0f, 0.0f, 0.0f };
        PoseState latest_state {};
        std::chrono::steady_clock::time_point latest_pose_time {};

        void close_pose_socket()
        {
            if (pose_socket != INVALID_SOCKET) {
                closesocket(pose_socket);
                pose_socket = INVALID_SOCKET;
            }
            active_pose_port = 0;
        }

        std::vector<float> parse_floats(std::string payload)
        {
            for (auto& c : payload) {
                if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')) {
                    c = ' ';
                }
            }

            std::vector<float> values;
            std::stringstream ss(payload);
            std::string token;
            while (ss >> token) {
                float value = 0.0f;
                const auto* begin = token.data();
                const auto* end = token.data() + token.size();
                if (auto [ptr, ec] = std::from_chars(begin, end, value); ec == std::errc() && ptr == end) {
                    values.push_back(value);
                }
            }
            return values;
        }
    }

    void start_pose_receiver(const PassengerVRConfig& cfg)
    {
        if (!cfg.enabled) {
            stop_pose_receiver();
            return;
        }

        if (pose_socket != INVALID_SOCKET && active_pose_port == cfg.pose_port) {
            return;
        }

        close_pose_socket();

        if (!winsock_started) {
            WSADATA data;
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
                dbg("PassengerVR: WSAStartup failed");
                return;
            }
            winsock_started = true;
        }

        pose_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (pose_socket == INVALID_SOCKET) {
            dbg("PassengerVR: pose socket creation failed");
            return;
        }

        u_long nonblocking = 1;
        if (ioctlsocket(pose_socket, FIONBIO, &nonblocking) != 0) {
            dbg("PassengerVR: could not set pose socket nonblocking");
            close_pose_socket();
            return;
        }

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(static_cast<u_short>(std::clamp(cfg.pose_port, 1, 65535)));

        if (bind(pose_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            dbg(std::format("PassengerVR: could not bind pose socket on UDP port {}", cfg.pose_port));
            close_pose_socket();
            return;
        }

        active_pose_port = cfg.pose_port;
        dbg(std::format("PassengerVR: listening for pose packets on UDP port {}", cfg.pose_port));
    }

    void stop_pose_receiver()
    {
        close_pose_socket();
    }

    void poll_pose_receiver()
    {
        if (pose_socket == INVALID_SOCKET) {
            return;
        }

        std::array<char, 512> buffer {};
        for (;;) {
            sockaddr_in from {};
            int from_len = sizeof(from);
            const int received = recvfrom(pose_socket, buffer.data(), static_cast<int>(buffer.size() - 1), 0, reinterpret_cast<sockaddr*>(&from), &from_len);
            if (received == SOCKET_ERROR) {
                const auto err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK) {
                    dbg(std::format("PassengerVR: pose recvfrom failed: {}", err));
                }
                return;
            }

            buffer[received] = '\0';
            const auto values = parse_floats(std::string(buffer.data(), static_cast<size_t>(received)));
            if (values.size() >= 3) {
                std::scoped_lock lock(pose_mutex);
                latest_euler_degrees = { values[0], values[1], values[2] };
                latest_state.head.valid = true;
                latest_state.head.rotation_degrees = latest_euler_degrees;
                latest_state.head.position = {};
                latest_state.left_hand = {};
                latest_state.right_hand = {};
                if (values.size() >= 17) {
                    latest_state.left_hand.valid = values[3] > 0.5f;
                    latest_state.left_hand.position = { values[4], values[5], values[6] };
                    latest_state.left_hand.rotation_degrees = { values[7], values[8], values[9] };
                    latest_state.right_hand.valid = values[10] > 0.5f;
                    latest_state.right_hand.position = { values[11], values[12], values[13] };
                    latest_state.right_hand.rotation_degrees = { values[14], values[15], values[16] };
                }
                latest_pose_time = std::chrono::steady_clock::now();
            }
        }
    }

    glm::vec3 latest_pose_degrees()
    {
        std::scoped_lock lock(pose_mutex);
        return latest_euler_degrees;
    }

    int64_t latest_pose_age_ms()
    {
        std::scoped_lock lock(pose_mutex);
        if (latest_pose_time == std::chrono::steady_clock::time_point {}) {
            return -1;
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - latest_pose_time).count();
    }

    PoseState latest_pose_state()
    {
        std::scoped_lock lock(pose_mutex);
        auto state = latest_state;
        if (latest_pose_time == std::chrono::steady_clock::time_point {}) {
            state.age_ms = -1;
        } else {
            state.age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - latest_pose_time).count();
        }
        return state;
    }

    M4 view_matrix(const PassengerVRConfig& cfg)
    {
        const auto euler = latest_pose_degrees();

        const auto yaw = glm::radians(euler.x + cfg.camera_yaw_degrees);
        const auto pitch = glm::radians(euler.y);
        const auto roll = glm::radians(euler.z);
        const auto head_rotation = glm::yawPitchRoll(yaw, pitch, roll);
        const auto seat_translation = glm::translate(glm::identity<M4>(), -cfg.camera_offset);

        return glm::inverse(head_rotation) * seat_translation;
    }
}
