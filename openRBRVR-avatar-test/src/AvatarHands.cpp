#include "AvatarHands.hpp"

#include "Config.hpp"
#include "Globals.hpp"
#include "VR.hpp"
#include "Vertex.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <gtc/matrix_transform.hpp>
#include <ranges>
#include <string>

namespace avatar_hands {
namespace {
    struct CachedRenderAssets {
        IDirect3DTexture9* skin_texture = nullptr;
        IDirect3DTexture9* cuff_texture = nullptr;
        IDirect3DVertexBuffer9* palm_quad = nullptr;
        IDirect3DVertexBuffer9* finger_quad = nullptr;
        IDirect3DVertexBuffer9* cuff_quad = nullptr;
    };

    HandState left_state;
    HandState right_state;
    CachedRenderAssets assets;

    std::string lower_copy(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    bool create_solid_texture(IDirect3DDevice9* dev, uint32_t argb, IDirect3DTexture9** out)
    {
        if (*out) {
            return true;
        }
        if (FAILED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, out, nullptr))) {
            dbg("AvatarHands: failed to create solid texture");
            return false;
        }

        D3DLOCKED_RECT rect {};
        if (FAILED((*out)->LockRect(0, &rect, nullptr, 0))) {
            (*out)->Release();
            *out = nullptr;
            dbg("AvatarHands: failed to lock solid texture");
            return false;
        }
        *static_cast<uint32_t*>(rect.pBits) = argb;
        (*out)->UnlockRect(0);
        return true;
    }

    bool create_sized_quad(IDirect3DDevice9* dev, float width, float height, IDirect3DVertexBuffer9** out)
    {
        if (*out) {
            return true;
        }
        const float half_w = width * 0.5f;
        const float half_h = height * 0.5f;
        Vertex vertices[] = {
            { -half_w, half_h, 0.0f, 0.0f, 0.0f },
            { half_w, half_h, 0.0f, 1.0f, 0.0f },
            { -half_w, -half_h, 0.0f, 0.0f, 1.0f },
            { half_w, -half_h, 0.0f, 1.0f, 1.0f },
        };
        return create_vertex_buffer(dev, vertices, 4, out);
    }

    bool ensure_assets(IDirect3DDevice9* dev)
    {
        return create_solid_texture(dev, D3DCOLOR_ARGB(255, 236, 185, 142), &assets.skin_texture)
            && create_solid_texture(dev, D3DCOLOR_ARGB(255, 30, 50, 68), &assets.cuff_texture)
            && create_sized_quad(dev, 0.088f, 0.062f, &assets.palm_quad)
            && create_sized_quad(dev, 0.018f, 0.050f, &assets.finger_quad)
            && create_sized_quad(dev, 0.082f, 0.040f, &assets.cuff_quad);
    }

    HandState& mutable_state(Side side)
    {
        return side == Side::Right ? right_state : left_state;
    }

    bool is_fresh(const HandState& hand)
    {
        if (!hand.valid) {
            return false;
        }
        const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - hand.updated_at).count();
        return age >= 0 && age <= std::clamp(g::cfg.roadbook_vr.player_hands_max_age_ms, 50, 5000);
    }

    M4 wrist_render_root(const HandState& hand, Side side)
    {
        const float side_sign = side == Side::Right ? -1.0f : 1.0f;
        return hand.wrist
            * glm::translate(glm::identity<M4>(), glm::vec3 { 0.015f * side_sign, 0.0f, -0.020f })
            * glm::rotate(glm::identity<M4>(), glm::radians(-18.0f), glm::vec3(1, 0, 0));
    }

    void render_one_hand(IDirect3DDevice9* dev, VRInterface* vr, RenderTarget target, const HandState& hand, Side side)
    {
        const auto root = wrist_render_root(hand, side);
        const float side_sign = side == Side::Right ? -1.0f : 1.0f;

        render_textured_quad(dev, vr, assets.cuff_texture, target, assets.cuff_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { 0.0f, -0.052f, 0.012f }));
        render_textured_quad(dev, vr, assets.skin_texture, target, assets.palm_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { 0.0f, 0.0f, 0.018f }));

        for (int i = 0; i < 4; ++i) {
            const float finger_spread = static_cast<float>(i) - 1.5f;
            const float x = side_sign * finger_spread * 0.017f;
            const float y = 0.045f + std::abs(finger_spread) * 0.003f;
            const float curl = std::clamp(hand.finger_curl[static_cast<size_t>(i)], 0.0f, 1.0f);
            const auto finger_model = root
                * glm::translate(glm::identity<M4>(), glm::vec3 { x, y, 0.022f })
                * glm::rotate(glm::identity<M4>(), glm::radians(-10.0f - curl * 35.0f), glm::vec3(1, 0, 0));
            render_textured_quad(dev, vr, assets.skin_texture, target, assets.finger_quad, finger_model);
        }

        const float thumb_x = side_sign * 0.044f;
        const auto thumb_model = root
            * glm::translate(glm::identity<M4>(), glm::vec3 { thumb_x, 0.013f, 0.023f })
            * glm::rotate(glm::identity<M4>(), glm::radians(side == Side::Right ? -35.0f : 35.0f), glm::vec3(0, 0, 1));
        render_textured_quad(dev, vr, assets.skin_texture, target, assets.finger_quad, thumb_model);
    }

    bool render_driver_hands(IDirect3DDevice9* dev, VRInterface* vr, RenderTarget target, bool enabled_for_target)
    {
        if (!g::cfg.roadbook_vr.player_hands_enabled || !enabled_for_target) {
            return false;
        }
        if (!g::cfg.roadbook_vr.avatar_hands_visible) {
            return false;
        }
        const auto quality = lower_copy(g::cfg.roadbook_vr.player_hands_quality);
        if (quality != "low" && quality != "auto") {
            return false;
        }
        if (!ensure_assets(dev)) {
            return false;
        }

        bool rendered = false;
        if (is_fresh(left_state)) {
            render_one_hand(dev, vr, target, left_state, Side::Left);
            rendered = true;
        }
        if (is_fresh(right_state)) {
            render_one_hand(dev, vr, target, right_state, Side::Right);
            rendered = true;
        }
        return rendered;
    }
}

void reset()
{
    left_state = {};
    right_state = {};
}

void update_from_pose_pair(
    const M4& left_wrist,
    bool left_valid,
    uint32_t left_packet,
    const M4& right_wrist,
    bool right_valid,
    uint32_t right_packet,
    SourceTier source)
{
    const auto now = std::chrono::steady_clock::now();

    auto update = [&](HandState& hand, const M4& wrist, bool valid, uint32_t packet) {
        hand.valid = valid;
        hand.packet = packet;
        if (!valid) {
            hand.source = SourceTier::None;
            return;
        }
        hand.source = source;
        hand.wrist = wrist;
        hand.updated_at = now;
        hand.finger_curl = { 0.08f, 0.10f, 0.12f, 0.14f, 0.18f };
    };

    update(left_state, left_wrist, left_valid, left_packet);
    update(right_state, right_wrist, right_valid, right_packet);
}

void update_from_controller_pose(const M4& left_wrist, bool left_valid, uint32_t left_packet, const M4& right_wrist, bool right_valid, uint32_t right_packet)
{
    update_from_pose_pair(left_wrist, left_valid, left_packet, right_wrist, right_valid, right_packet, SourceTier::ControllerPose);
}

void update_from_openxr_grip_pose(const M4& left_wrist, bool left_valid, const M4& right_wrist, bool right_valid)
{
    update_from_pose_pair(left_wrist, left_valid, 0, right_wrist, right_valid, 0, SourceTier::OpenXrGripPose);
}

const HandState& state(Side side)
{
    return side == Side::Right ? right_state : left_state;
}

std::optional<int64_t> age_ms(Side side)
{
    const auto& hand = state(side);
    if (!hand.valid) {
        return std::nullopt;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - hand.updated_at).count();
}

bool fresh(Side side)
{
    return is_fresh(state(side));
}

bool render_driver_self(IDirect3DDevice9* dev, VRInterface* vr, RenderTarget target)
{
    return render_driver_hands(dev, vr, target, g::cfg.roadbook_vr.player_hands_render_driver_self);
}

bool render_driver_to_passenger(IDirect3DDevice9* dev, VRInterface* vr, RenderTarget target)
{
    return render_driver_hands(dev, vr, target, g::cfg.roadbook_vr.player_hands_render_driver_to_passenger);
}
}
