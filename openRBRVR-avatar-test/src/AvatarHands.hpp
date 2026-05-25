#pragma once

#include "RenderTarget.hpp"
#include "Util.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <d3d9.h>
#include <optional>

class VRInterface;

namespace avatar_hands {
    enum class Side {
        Left,
        Right,
    };

    enum class SourceTier {
        None,
        SteamVrSkeleton,
        OpenXrGripPose,
        ControllerPose,
        RemotePassenger,
    };

    struct HandState {
        bool valid = false;
        SourceTier source = SourceTier::None;
        M4 wrist = glm::identity<M4>();
        std::array<float, 5> finger_curl { 0.15f, 0.15f, 0.15f, 0.15f, 0.15f };
        std::chrono::steady_clock::time_point updated_at {};
        uint32_t packet = 0;
    };

    void reset();
    void update_from_controller_pose(const M4& left_wrist, bool left_valid, uint32_t left_packet, const M4& right_wrist, bool right_valid, uint32_t right_packet);
    void update_from_openxr_grip_pose(const M4& left_wrist, bool left_valid, const M4& right_wrist, bool right_valid);
    const HandState& state(Side side);
    std::optional<int64_t> age_ms(Side side);
    bool fresh(Side side);

    bool render_driver_self(IDirect3DDevice9* dev, VRInterface* vr, RenderTarget target);
    bool render_driver_to_passenger(IDirect3DDevice9* dev, VRInterface* vr, RenderTarget target);
}
