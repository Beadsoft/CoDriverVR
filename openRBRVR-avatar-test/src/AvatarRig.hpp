#pragma once

#include "RenderTarget.hpp"
#include "Util.hpp"

#include <d3d9.h>
#include <filesystem>
#include <string>
#include <vector>

class VRInterface;

namespace avatar_rig {
    struct DiagnosticPart {
        std::string region;
        std::string material;
        std::string texture;
        bool fallback = false;
    };

    struct LiveTrackedPart {
        bool valid = false;
        glm::vec3 position {};
        glm::vec3 rotation_degrees {};
    };

    struct LivePose {
        bool active = false;
        LiveTrackedPart head;
        LiveTrackedPart left_hand;
        LiveTrackedPart right_hand;
        float hand_scale = 1.0f;
        glm::vec3 head_rotation_scale = { 1.0f, 1.0f, 1.0f };
        glm::vec3 hand_rotation_scale = { 0.35f, 0.35f, 0.35f };
        bool hand_positions_model_local = false;
    };

    enum class RenderFilter {
        Full,
        LimbsOnly,
    };

    bool render(
        IDirect3DDevice9* dev,
        VRInterface* vr,
        RenderTarget target,
        const std::filesystem::path& obj_path,
        const std::filesystem::path& profile_path,
        const M4& model,
        IDirect3DBaseTexture9* fallback_body,
        IDirect3DBaseTexture9* fallback_helmet,
        IDirect3DBaseTexture9* fallback_face,
        IDirect3DBaseTexture9* fallback_hands,
        IDirect3DBaseTexture9* fallback_shoes,
        const LivePose* live_pose = nullptr,
        RenderFilter filter = RenderFilter::Full);
    bool render_mask(
        IDirect3DDevice9* dev,
        VRInterface* vr,
        RenderTarget target,
        const std::filesystem::path& obj_path,
        const std::filesystem::path& profile_path,
        const M4& model);
    std::vector<DiagnosticPart> diagnostic_parts();
    void reset();
}
