#pragma once

#include "Util.hpp"

struct PassengerVRConfig;

namespace passenger_vr {
    struct TrackedPartPose {
        bool valid = false;
        glm::vec3 position {};
        glm::vec3 rotation_degrees {};
    };

    struct PoseState {
        TrackedPartPose head;
        TrackedPartPose left_hand;
        TrackedPartPose right_hand;
        int64_t age_ms = -1;
    };

    void start_pose_receiver(const PassengerVRConfig& cfg);
    void stop_pose_receiver();
    void poll_pose_receiver();
    glm::vec3 latest_pose_degrees();
    int64_t latest_pose_age_ms();
    PoseState latest_pose_state();
    M4 view_matrix(const PassengerVRConfig& cfg);
}
