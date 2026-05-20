#include "DriverRoomControl.hpp"

#include <windows.h>
#include <wininet.h>

#include <cctype>
#include <cstring>
#include <format>
#include <string>

namespace driver_room {
namespace {
    Status cached_status;

    std::string request(const char* method, const char* path, const std::string& body = "")
    {
        HINTERNET internet = InternetOpenA("CoDriverVR/openRBRVR", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        if (!internet) {
            return {};
        }
        HINTERNET connect = InternetConnectA(internet, "127.0.0.1", 7790, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
        if (!connect) {
            InternetCloseHandle(internet);
            return {};
        }
        HINTERNET http = HttpOpenRequestA(connect, method, path, nullptr, nullptr, nullptr, INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD, 0);
        if (!http) {
            InternetCloseHandle(connect);
            InternetCloseHandle(internet);
            return {};
        }

        DWORD timeout_ms = 1200;
        InternetSetOptionA(http, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout_ms, sizeof(timeout_ms));
        InternetSetOptionA(http, INTERNET_OPTION_SEND_TIMEOUT, &timeout_ms, sizeof(timeout_ms));
        InternetSetOptionA(http, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout_ms, sizeof(timeout_ms));

        const char* headers = "content-type: application/json\r\n";
        const auto ok = HttpSendRequestA(
            http,
            headers,
            static_cast<DWORD>(strlen(headers)),
            body.empty() ? nullptr : const_cast<char*>(body.data()),
            static_cast<DWORD>(body.size()));

        std::string output;
        if (ok) {
            char buffer[4096];
            DWORD read = 0;
            while (InternetReadFile(http, buffer, sizeof(buffer), &read) && read > 0) {
                output.append(buffer, buffer + read);
            }
        }

        InternetCloseHandle(http);
        InternetCloseHandle(connect);
        InternetCloseHandle(internet);
        return output;
    }

    std::string json_string(const std::string& json, const std::string& key, const std::string& fallback = "")
    {
        const auto marker = std::format("\"{}\"", key);
        auto pos = json.find(marker);
        if (pos == std::string::npos) {
            return fallback;
        }
        pos = json.find(':', pos + marker.size());
        if (pos == std::string::npos) {
            return fallback;
        }
        pos = json.find('"', pos + 1);
        if (pos == std::string::npos) {
            return fallback;
        }
        std::string result;
        bool escaped = false;
        for (size_t i = pos + 1; i < json.size(); ++i) {
            const char c = json[i];
            if (escaped) {
                if (c == 'n') {
                    result.push_back('\n');
                } else {
                    result.push_back(c);
                }
                escaped = false;
                continue;
            }
            if (c == '\\') {
                escaped = true;
                continue;
            }
            if (c == '"') {
                return result;
            }
            result.push_back(c);
        }
        return fallback;
    }

    bool json_bool(const std::string& json, const std::string& key, bool fallback = false)
    {
        const auto marker = std::format("\"{}\"", key);
        auto pos = json.find(marker);
        if (pos == std::string::npos) {
            return fallback;
        }
        pos = json.find(':', pos + marker.size());
        if (pos == std::string::npos) {
            return fallback;
        }
        while (++pos < json.size() && isspace(static_cast<unsigned char>(json[pos]))) {
        }
        if (json.compare(pos, 4, "true") == 0) {
            return true;
        }
        if (json.compare(pos, 5, "false") == 0) {
            return false;
        }
        return fallback;
    }

    void parse_status(const std::string& json)
    {
        if (json.empty()) {
            cached_status = {};
            return;
        }
        cached_status.reachable = true;
        cached_status.host_connected = json_bool(json, "hostConnected");
        cached_status.running = json_bool(json, "running");
        cached_status.invite_ready = json_bool(json, "inviteReady");
        cached_status.passenger_joined = json_bool(json, "passengerJoined");
        cached_status.signaling_connected = json_bool(json, "signalingConnected");
        cached_status.direct_connected = json_bool(json, "directConnected");
        cached_status.direct_failed = json_bool(json, "directFailed");
        cached_status.mic_enabled = json_bool(json, "micEnabled");
        cached_status.mode = json_string(json, "mode", cached_status.mode);
        cached_status.room_id = json_string(json, "roomId");
        cached_status.capture_source = json_string(json, "captureSource", "none");
        cached_status.peer_state = json_string(json, "peerState", "not created");
        cached_status.summary = json_string(json, "summary", cached_status.running ? "Running" : "Stopped");
    }

    void post_command(const char* path, const std::string& body = "{}")
    {
        request("POST", path, body);
        refresh(true);
    }

    std::string command_mode()
    {
        const auto& current = status().mode;
        if (current == "secure-lan") {
            return "secure-lan";
        }
        if (current == "internet") {
            return "internet";
        }
        return "lan";
    }
}

const Status& status()
{
    return cached_status;
}

void refresh(bool force)
{
    (void)force;
    parse_status(request("GET", "/api/driver-room/status"));
}

void start()
{
    const auto body = std::format("{{\"mode\":\"{}\"}}", command_mode());
    post_command("/api/driver-room/start", body);
}

void stop()
{
    post_command("/api/driver-room/stop");
}

void recreate()
{
    const auto body = std::format("{{\"mode\":\"{}\"}}", command_mode());
    post_command("/api/driver-room/recreate", body);
}

void toggle_mode()
{
    const auto next = status().mode == "lan" ? "secure-lan" : "lan";
    const auto body = std::format("{{\"mode\":\"{}\"}}", next);
    post_command(cached_status.running ? "/api/driver-room/recreate" : "/api/driver-room/start", body);
}

void toggle_mic()
{
    post_command("/api/driver-room/mic");
}

void open_share_on_quest()
{
    post_command("/api/driver-room/share-on-quest");
}
}
