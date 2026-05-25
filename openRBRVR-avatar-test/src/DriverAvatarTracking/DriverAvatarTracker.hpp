#pragma once

#include "../AvatarRig.hpp"
#include "../Util.hpp"

#include <cstdint>
#include <string>

namespace driver_avatar_tracking {
    enum class PoseSource {
        None,
        OpenVrController,
    };

    struct TrackedPose {
        bool valid = false;
        M4 absolute = glm::identity<M4>();
        int64_t age_ms = -1;
        PoseSource source = PoseSource::None;
    };

    struct Settings {
        std::string controller_profile = "questTouch";
        int64_t max_pose_age_ms = 250;
        float max_hand_distance_model = 4.0f;
        glm::vec3 head_rotation_scale = { 1.0f, 1.0f, 1.0f };
        glm::vec3 hand_rotation_scale = { 1.0f, 1.0f, 1.0f };
    };

    struct FrameInput {
        bool cockpit_camera_ready = false;
        TrackedPose hmd;
        TrackedPose left_controller;
        TrackedPose right_controller;
    };

    struct HandResult {
        bool valid = false;
        glm::vec3 model_position {};
        glm::vec3 rotation_degrees {};
        std::string reject_reason;
    };

    struct FrameResult {
        bool active = false;
        bool hmd_valid = false;
        HandResult left_hand;
        HandResult right_hand;
        avatar_rig::LivePose live_pose;
    };

    class DriverAvatarTracker {
    public:
        void set_settings(Settings settings);
        const Settings& settings() const;
        FrameResult update(const FrameInput& input, const M4& model_to_world, const M4& orientation_to_world);

    private:
        Settings settings_ {};
    };

    glm::vec3 translation_from_matrix(const M4& matrix);
    glm::vec3 yaw_pitch_roll_from_matrix(const M4& matrix);
}
