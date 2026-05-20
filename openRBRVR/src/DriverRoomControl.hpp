#pragma once

#include <string>

namespace driver_room {
struct Status {
    bool reachable = false;
    bool host_connected = false;
    bool running = false;
    bool invite_ready = false;
    bool passenger_joined = false;
    bool signaling_connected = false;
    bool direct_connected = false;
    bool direct_failed = false;
    bool mic_enabled = false;
    std::string mode = "internet";
    std::string room_id;
    std::string capture_source = "none";
    std::string peer_state = "not created";
    std::string summary = "Streamer offline";
};

const Status& status();
void refresh(bool force = false);
void start();
void stop();
void recreate();
void toggle_mode();
void toggle_mic();
void open_share_on_quest();
}
