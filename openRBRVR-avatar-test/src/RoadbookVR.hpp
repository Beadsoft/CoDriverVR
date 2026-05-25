#pragma once

#include "RenderTarget.hpp"
#include "Util.hpp"

#include <d3d9.h>
#include <openvr.h>
#include <string>

class VRInterface;

struct RoadbookVRConfig {
    bool enabled = false;
    std::string rbr_root = "C:\\richard burns rally";
    std::string source = "ngpMyPacenotes";
    std::string lock_hand = "left";
    std::string page_hand = "right";
    bool driver_visible = true;
    bool passenger_visible = true;
    bool avatar_enabled = false;
    bool avatar_driver_visible = true;
    bool avatar_passenger_visible = true;
    bool avatar_hands_visible = true;
    bool player_hands_enabled = true;
    std::string player_hands_input_mode = "auto";
    bool player_hands_render_driver_self = true;
    bool player_hands_render_driver_to_passenger = true;
    bool player_hands_render_codriver = false;
    std::string player_hands_quality = "low";
    int player_hands_max_age_ms = 250;
    bool player_hands_debug = false;
    bool asset_probe_enabled = false;
    std::string avatar_reuse_mode = "rbrCaptureThenFallback";
    bool avatar_replay_enabled = true;
    bool avatar_fallback_enabled = true;
    bool roadbook_use_game_assets = true;
    bool avatar_dump_enabled = false;
    std::string avatar_dump_target = "both";
    std::string avatar_dump_directory = "Plugins\\openRBRVR\\Captures";
    bool avatar_dump_apply_world_transform = false;
    std::string avatar_mesh_source = "rbrDump";
    bool avatar_mesh_enabled = true;
    std::string avatar_mesh_path = "Plugins\\openRBRVR\\Assets\\RaceCarDriverLowPoly\\model.obj";
    std::string avatar_mesh_texture_mode = "rbr";
    float avatar_mesh_scale = 0.42f;
    bool avatar_rig_enabled = true;
    std::string avatar_rig_profile = "Plugins\\openRBRVR\\Captures\\codriver_rig.toml";
    bool avatar_rig_debug_regions = false;
    std::string avatar_rig_pose = "codriverReadingBook";
    bool avatar_rig_live_passenger_enabled = true;
    std::string avatar_rig_live_input_source = "passenger";
    int avatar_rig_live_pose_max_age_ms = 500;
    float avatar_rig_hand_scale = 1.0f;
    glm::vec3 avatar_rig_head_rotation_scale = { 1.0f, 1.0f, 1.0f };
    glm::vec3 avatar_rig_hand_rotation_scale = { 1.0f, 1.0f, 1.0f };
    std::string avatar_rig_live_hand_fallback = "defaultPose";
    std::string avatar_controller_profile = "questTouch";
    std::string avatar_live_pose_transform_mode = "identity";
    bool avatar_live_pose_swap_hands = false;
    bool avatar_diagnostic_enabled = false;
    std::string avatar_diagnostic_directory = "Plugins\\openRBRVR\\Diagnostics";
    std::string avatar_diagnostic_target = "LeftEye";
    int avatar_diagnostic_capture_frames = 1;
    bool avatar_diagnostic_pose_sweep = false;
    bool avatar_diagnostic_overlay = false;
    bool avatar_auto_seat_placement = true;
    glm::vec3 avatar_driver_seat_correction = { 0.0f, -0.45f, 0.0f };
    glm::vec3 avatar_passenger_seat_correction = { 0.0f, -0.45f, 0.0f };
    glm::vec3 avatar_driver_offset = { -0.38f, -0.72f, 0.62f };
    glm::vec3 avatar_passenger_offset = { 0.38f, -0.72f, 0.62f };
    float avatar_driver_yaw_degrees = -5.0f;
    float avatar_passenger_yaw_degrees = 5.0f;
    std::string asset_reuse_mode = "probeThenFallback";
    float panel_width_meters = 0.55f;
    float panel_height_meters = 0.38f;
    glm::vec3 panel_offset = { 0.04f, 0.04f, 0.12f };
    glm::vec3 panel_tilt_degrees = { -18.0f, 0.0f, 0.0f };
    int notes_per_page = 12;
    float page_turn_seconds = 0.36f;
    std::string fallback_pose = "head";
};

namespace roadbook_vr {
    void update_openvr_controller_state(vr::IVRSystem* hmd, const vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]);
    void render(IDirect3DDevice9* dev, VRInterface* vr);
    std::string debug_status();
}
