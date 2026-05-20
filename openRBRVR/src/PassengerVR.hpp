#pragma once

#include "Util.hpp"

struct PassengerVRConfig;

namespace passenger_vr {
    void start_pose_receiver(const PassengerVRConfig& cfg);
    void stop_pose_receiver();
    void poll_pose_receiver();
    glm::vec3 latest_pose_degrees();
    int64_t latest_pose_age_ms();
    M4 view_matrix(const PassengerVRConfig& cfg);
}
