#include "DriverAvatarTracker.hpp"

#include <cmath>
#include <geometric.hpp>
#include <gtx/euler_angles.hpp>
#include <utility>

namespace driver_avatar_tracking {
namespace {
    bool finite_vec3(glm::vec3 value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool fresh_pose(const TrackedPose& pose, int64_t max_age_ms)
    {
        return pose.valid && pose.age_ms >= 0 && pose.age_ms <= max_age_ms && finite_vec3(translation_from_matrix(pose.absolute));
    }

    HandResult solve_hand(const TrackedPose& controller, const M4& world_to_model, const M4& world_to_orientation, glm::vec3 head_model, const Settings& settings)
    {
        HandResult result;
        if (!fresh_pose(controller, settings.max_pose_age_ms)) {
            result.reject_reason = controller.valid ? "stale_controller" : "invalid_controller";
            return result;
        }

        const auto controller_model = world_to_model * controller.absolute;
        result.model_position = translation_from_matrix(controller_model);
        if (!finite_vec3(result.model_position)) {
            result.reject_reason = "nonfinite_controller";
            return result;
        }

        const auto distance = glm::length(result.model_position - head_model);
        if (!std::isfinite(distance) || distance > settings.max_hand_distance_model) {
            result.reject_reason = "controller_out_of_range";
            return result;
        }

        const auto controller_orientation = world_to_orientation * controller.absolute;
        result.rotation_degrees = yaw_pitch_roll_from_matrix(controller_orientation);
        result.valid = true;
        return result;
    }
}

glm::vec3 translation_from_matrix(const M4& matrix)
{
    return { matrix[3].x, matrix[3].y, matrix[3].z };
}

glm::vec3 yaw_pitch_roll_from_matrix(const M4& matrix)
{
    const auto euler = glm::degrees(glm::eulerAngles(glm::quat_cast(matrix)));
    return { euler.y, euler.x, euler.z };
}

void DriverAvatarTracker::set_settings(Settings settings)
{
    settings_ = std::move(settings);
}

const Settings& DriverAvatarTracker::settings() const
{
    return settings_;
}

FrameResult DriverAvatarTracker::update(const FrameInput& input, const M4& model_to_world, const M4& orientation_to_world)
{
    FrameResult result;
    result.hmd_valid = fresh_pose(input.hmd, settings_.max_pose_age_ms);
    if (!input.cockpit_camera_ready || !result.hmd_valid) {
        return result;
    }

    const auto world_to_model = glm::inverse(model_to_world);
    const auto world_to_orientation = glm::inverse(orientation_to_world);
    const auto hmd_model = world_to_model * input.hmd.absolute;
    const auto hmd_orientation = world_to_orientation * input.hmd.absolute;
    const auto head_model = translation_from_matrix(hmd_model);

    result.left_hand = solve_hand(input.left_controller, world_to_model, world_to_orientation, head_model, settings_);
    result.right_hand = solve_hand(input.right_controller, world_to_model, world_to_orientation, head_model, settings_);

    result.active = true;
    result.live_pose.active = true;
    result.live_pose.hand_positions_model_local = true;
    result.live_pose.head_rotation_scale = settings_.head_rotation_scale;
    result.live_pose.hand_rotation_scale = settings_.hand_rotation_scale;
    result.live_pose.head = { true, head_model, yaw_pitch_roll_from_matrix(hmd_orientation) };
    result.live_pose.left_hand = { result.left_hand.valid, result.left_hand.model_position, result.left_hand.rotation_degrees };
    result.live_pose.right_hand = { result.right_hand.valid, result.right_hand.model_position, result.right_hand.rotation_degrees };
    return result;
}
}
