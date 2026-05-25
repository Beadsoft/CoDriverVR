#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sstream>
#include <string>
#include <unordered_map>

#include <d3d9.h>

#include "Util.hpp"

#include <vec2.hpp>
#include <vec3.hpp>

#define TOML_HEADER_ONLY 1
#include <inicpp.h>
#include <toml.hpp>

#include "VR.hpp"
#include "RoadbookVR.hpp"

static auto int_or_default(const std::string& value, int def) -> int
{
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        return def;
    }
}

static std::string vec3_as_space_separated_string(const glm::vec3& vec)
{
    auto v = glm::value_ptr(vec);
    return std::format("{:.4f} {:.4f} {:.4f}", v[0], v[1], v[2]);
}

static std::optional<glm::vec3> vec3_from_space_separated_string(const std::string& s)
{
    std::string val;
    std::stringstream ss(s);

    int i = 0;
    auto ret = glm::vec3 {};
    auto v = glm::value_ptr(ret);
    while (std::getline(ss, val, ' ')) {
        auto idx = i;
        i++;

        if (val.size() == 0) {
            continue;
        }

        if (idx >= 3) {
            return ret;
        }

        float f {};
        auto [p, ec] = std::from_chars(val.data(), val.data() + val.size(), f);
        if (ec != std::errc()) {
            return std::nullopt;
        }

        v[idx] = f;
    }

    if (i < 2) {
        return std::nullopt;
    }

    return ret;
}

struct RenderContextConfig {
    float supersampling = 1.0;
    std::optional<D3DMULTISAMPLE_TYPE> msaa = std::nullopt;
    std::vector<int> stage_ids = {};
    bool quad_view_rendering = false;
    bool multiview_rendering = false;
};

struct PassengerVRConfig {
    bool enabled = false;
    glm::vec3 camera_offset = { -0.55f, 0.02f, 0.05f };
    float camera_yaw_degrees = 0.0f;
    std::string render_mode = "mono";
    std::string stream_host = "0.0.0.0";
    int stream_port = 7790;
    int pose_port = 7791;
    std::string recenter_key = "QuestMenu";
};

struct Config {
    float menu_size = 1.0;
    bool menu_scene = true;
    float overlay_size = 1.0;
    glm::vec3 overlay_translation = { 0, 0, 0 };
    float supersampling = 1.0;
    HorizonLock lock_to_horizon = HorizonLock::LOCK_NONE;
    double horizon_lock_multiplier = 1.0;
    float lowpass_roll_filter = 0.1;
    float lowpass_pitch_filter = 0.1;
    float lowpass_yaw_filter = 0.1;
    bool horizon_lock_flip = false;
    CompanionMode companion_mode;
    bool draw_loading_screen = true;
    bool debug = false;
    int debug_mode = 0;
    bool render_pausemenu_3d = true;
    bool render_prestage_3d = false;
    bool render_replays_3d = false;
    int anisotropy = 0;
    VRRuntime runtime = VRRuntime::OPENVR;
    std::unordered_map<std::string, RenderContextConfig> gfx = { { "default", RenderContextConfig {} } };
    glm::ivec2 companion_offset;
    int companion_size = 100;
    RenderTarget companion_eye = LeftEye;
    int world_scale = 1000;
    bool quad_view_rendering = false;
    bool wanted_quad_view_rendering = false;
    D3DMULTISAMPLE_TYPE peripheral_msaa = D3DMULTISAMPLE_NONE;
    bool openxr_motion_compensation = false; // OpenXR-MotionCompensation support https://github.com/BuzzteeBear/OpenXR-MotionCompensation
    bool render_particles = true;
    bool always_render_particles_in_replay = false;
    int64_t prediction_dampening = 0;
    bool enable_xr_api_path_modification = true;
    bool enable_obsmirror_support = true;
    bool multiview = false;
    bool recenter_at_session_start = false;
    bool recenter_at_stage_start = false;
    bool threedof = false;
    PassengerVRConfig passenger_vr;
    RoadbookVRConfig roadbook_vr;
    struct {
        bool disable_multiview = false;
        int64_t adjust_displaytime_ms = 0;
    } experimental;

    Config& operator=(const Config& rhs)
    {
        menu_size = rhs.menu_size;
        menu_scene = rhs.menu_scene;
        overlay_size = rhs.overlay_size;
        overlay_translation = rhs.overlay_translation;
        supersampling = rhs.supersampling;
        lock_to_horizon = rhs.lock_to_horizon;
        horizon_lock_multiplier = rhs.horizon_lock_multiplier;
        lowpass_roll_filter = rhs.lowpass_roll_filter;
        lowpass_pitch_filter = rhs.lowpass_pitch_filter;
        lowpass_yaw_filter = rhs.lowpass_yaw_filter;
        horizon_lock_flip = rhs.horizon_lock_flip;
        companion_mode = rhs.companion_mode;
        draw_loading_screen = rhs.draw_loading_screen;
        debug = rhs.debug;
        render_pausemenu_3d = rhs.render_pausemenu_3d;
        render_prestage_3d = rhs.render_prestage_3d;
        render_replays_3d = rhs.render_replays_3d;
        runtime = rhs.runtime;
        anisotropy = rhs.anisotropy;
        gfx = rhs.gfx;
        companion_offset = rhs.companion_offset;
        companion_size = rhs.companion_size;
        companion_eye = rhs.companion_eye;
        companion_mode = rhs.companion_mode;
        debug_mode = rhs.debug_mode;
        world_scale = rhs.world_scale;
        quad_view_rendering = rhs.quad_view_rendering;
        wanted_quad_view_rendering = rhs.wanted_quad_view_rendering;
        peripheral_msaa = rhs.peripheral_msaa;
        openxr_motion_compensation = rhs.openxr_motion_compensation;
        render_particles = rhs.render_particles;
        always_render_particles_in_replay = rhs.always_render_particles_in_replay;
        prediction_dampening = rhs.prediction_dampening;
        enable_xr_api_path_modification = rhs.enable_xr_api_path_modification;
        enable_obsmirror_support = rhs.enable_obsmirror_support;
        multiview = rhs.multiview;
        recenter_at_session_start = rhs.recenter_at_session_start;
        recenter_at_stage_start = rhs.recenter_at_stage_start;
        threedof = rhs.threedof;
        passenger_vr = rhs.passenger_vr;
        roadbook_vr = rhs.roadbook_vr;
        experimental = rhs.experimental;
        return *this;
    }

    bool operator==(const Config& rhs) const
    {
        return menu_size == rhs.menu_size
            && menu_scene == rhs.menu_scene
            && overlay_size == rhs.overlay_size
            && overlay_translation == rhs.overlay_translation
            && supersampling == rhs.supersampling
            && lock_to_horizon == rhs.lock_to_horizon
            && horizon_lock_multiplier == rhs.horizon_lock_multiplier
            && lowpass_roll_filter == rhs.lowpass_roll_filter
            && lowpass_pitch_filter == rhs.lowpass_pitch_filter
            && lowpass_yaw_filter == rhs.lowpass_yaw_filter
            && horizon_lock_flip == rhs.horizon_lock_flip
            && companion_mode == rhs.companion_mode
            && draw_loading_screen == rhs.draw_loading_screen
            && debug == rhs.debug
            && debug_mode == rhs.debug_mode
            && render_pausemenu_3d == rhs.render_pausemenu_3d
            && render_prestage_3d == rhs.render_prestage_3d
            && render_replays_3d == rhs.render_replays_3d
            && runtime == rhs.runtime
            && companion_offset == rhs.companion_offset
            && companion_size == rhs.companion_size
            && companion_eye == rhs.companion_eye
            && world_scale == rhs.world_scale
            && quad_view_rendering == rhs.quad_view_rendering
            && wanted_quad_view_rendering == rhs.wanted_quad_view_rendering
            && peripheral_msaa == rhs.peripheral_msaa
            && openxr_motion_compensation == rhs.openxr_motion_compensation
            && render_particles == rhs.render_particles
            && always_render_particles_in_replay == rhs.always_render_particles_in_replay
            && prediction_dampening == rhs.prediction_dampening
            && enable_xr_api_path_modification == rhs.enable_xr_api_path_modification
            && enable_obsmirror_support == rhs.enable_obsmirror_support
            && multiview == rhs.multiview
            && recenter_at_session_start == rhs.recenter_at_session_start
            && recenter_at_stage_start == rhs.recenter_at_stage_start
            && threedof == rhs.threedof
            && passenger_vr.enabled == rhs.passenger_vr.enabled
            && passenger_vr.camera_offset == rhs.passenger_vr.camera_offset
            && passenger_vr.camera_yaw_degrees == rhs.passenger_vr.camera_yaw_degrees
            && passenger_vr.render_mode == rhs.passenger_vr.render_mode
            && passenger_vr.stream_host == rhs.passenger_vr.stream_host
            && passenger_vr.stream_port == rhs.passenger_vr.stream_port
            && passenger_vr.pose_port == rhs.passenger_vr.pose_port
            && passenger_vr.recenter_key == rhs.passenger_vr.recenter_key
            && roadbook_vr.enabled == rhs.roadbook_vr.enabled
            && roadbook_vr.rbr_root == rhs.roadbook_vr.rbr_root
            && roadbook_vr.source == rhs.roadbook_vr.source
            && roadbook_vr.lock_hand == rhs.roadbook_vr.lock_hand
            && roadbook_vr.page_hand == rhs.roadbook_vr.page_hand
            && roadbook_vr.driver_visible == rhs.roadbook_vr.driver_visible
            && roadbook_vr.passenger_visible == rhs.roadbook_vr.passenger_visible
            && roadbook_vr.avatar_enabled == rhs.roadbook_vr.avatar_enabled
            && roadbook_vr.avatar_driver_visible == rhs.roadbook_vr.avatar_driver_visible
            && roadbook_vr.avatar_passenger_visible == rhs.roadbook_vr.avatar_passenger_visible
            && roadbook_vr.avatar_hands_visible == rhs.roadbook_vr.avatar_hands_visible
            && roadbook_vr.player_hands_enabled == rhs.roadbook_vr.player_hands_enabled
            && roadbook_vr.player_hands_input_mode == rhs.roadbook_vr.player_hands_input_mode
            && roadbook_vr.player_hands_render_driver_self == rhs.roadbook_vr.player_hands_render_driver_self
            && roadbook_vr.player_hands_render_driver_to_passenger == rhs.roadbook_vr.player_hands_render_driver_to_passenger
            && roadbook_vr.player_hands_render_codriver == rhs.roadbook_vr.player_hands_render_codriver
            && roadbook_vr.player_hands_quality == rhs.roadbook_vr.player_hands_quality
            && roadbook_vr.player_hands_max_age_ms == rhs.roadbook_vr.player_hands_max_age_ms
            && roadbook_vr.player_hands_debug == rhs.roadbook_vr.player_hands_debug
            && roadbook_vr.asset_probe_enabled == rhs.roadbook_vr.asset_probe_enabled
            && roadbook_vr.avatar_reuse_mode == rhs.roadbook_vr.avatar_reuse_mode
            && roadbook_vr.avatar_replay_enabled == rhs.roadbook_vr.avatar_replay_enabled
            && roadbook_vr.avatar_fallback_enabled == rhs.roadbook_vr.avatar_fallback_enabled
            && roadbook_vr.roadbook_use_game_assets == rhs.roadbook_vr.roadbook_use_game_assets
            && roadbook_vr.avatar_dump_enabled == rhs.roadbook_vr.avatar_dump_enabled
            && roadbook_vr.avatar_dump_target == rhs.roadbook_vr.avatar_dump_target
            && roadbook_vr.avatar_dump_directory == rhs.roadbook_vr.avatar_dump_directory
            && roadbook_vr.avatar_dump_apply_world_transform == rhs.roadbook_vr.avatar_dump_apply_world_transform
            && roadbook_vr.avatar_mesh_source == rhs.roadbook_vr.avatar_mesh_source
            && roadbook_vr.avatar_mesh_enabled == rhs.roadbook_vr.avatar_mesh_enabled
            && roadbook_vr.avatar_mesh_path == rhs.roadbook_vr.avatar_mesh_path
            && roadbook_vr.avatar_mesh_texture_mode == rhs.roadbook_vr.avatar_mesh_texture_mode
            && roadbook_vr.avatar_mesh_scale == rhs.roadbook_vr.avatar_mesh_scale
            && roadbook_vr.avatar_rig_enabled == rhs.roadbook_vr.avatar_rig_enabled
            && roadbook_vr.avatar_rig_profile == rhs.roadbook_vr.avatar_rig_profile
            && roadbook_vr.avatar_rig_debug_regions == rhs.roadbook_vr.avatar_rig_debug_regions
            && roadbook_vr.avatar_rig_pose == rhs.roadbook_vr.avatar_rig_pose
            && roadbook_vr.avatar_rig_live_passenger_enabled == rhs.roadbook_vr.avatar_rig_live_passenger_enabled
            && roadbook_vr.avatar_rig_live_pose_max_age_ms == rhs.roadbook_vr.avatar_rig_live_pose_max_age_ms
            && roadbook_vr.avatar_rig_hand_scale == rhs.roadbook_vr.avatar_rig_hand_scale
            && roadbook_vr.avatar_rig_head_rotation_scale == rhs.roadbook_vr.avatar_rig_head_rotation_scale
            && roadbook_vr.avatar_rig_hand_rotation_scale == rhs.roadbook_vr.avatar_rig_hand_rotation_scale
            && roadbook_vr.avatar_rig_live_hand_fallback == rhs.roadbook_vr.avatar_rig_live_hand_fallback
            && roadbook_vr.avatar_controller_profile == rhs.roadbook_vr.avatar_controller_profile
            && roadbook_vr.avatar_live_pose_transform_mode == rhs.roadbook_vr.avatar_live_pose_transform_mode
            && roadbook_vr.avatar_live_pose_swap_hands == rhs.roadbook_vr.avatar_live_pose_swap_hands
            && roadbook_vr.avatar_diagnostic_enabled == rhs.roadbook_vr.avatar_diagnostic_enabled
            && roadbook_vr.avatar_diagnostic_directory == rhs.roadbook_vr.avatar_diagnostic_directory
            && roadbook_vr.avatar_diagnostic_target == rhs.roadbook_vr.avatar_diagnostic_target
            && roadbook_vr.avatar_diagnostic_capture_frames == rhs.roadbook_vr.avatar_diagnostic_capture_frames
            && roadbook_vr.avatar_diagnostic_pose_sweep == rhs.roadbook_vr.avatar_diagnostic_pose_sweep
            && roadbook_vr.avatar_diagnostic_overlay == rhs.roadbook_vr.avatar_diagnostic_overlay
            && roadbook_vr.avatar_auto_seat_placement == rhs.roadbook_vr.avatar_auto_seat_placement
            && roadbook_vr.avatar_driver_seat_correction == rhs.roadbook_vr.avatar_driver_seat_correction
            && roadbook_vr.avatar_passenger_seat_correction == rhs.roadbook_vr.avatar_passenger_seat_correction
            && roadbook_vr.avatar_driver_offset == rhs.roadbook_vr.avatar_driver_offset
            && roadbook_vr.avatar_passenger_offset == rhs.roadbook_vr.avatar_passenger_offset
            && roadbook_vr.avatar_driver_yaw_degrees == rhs.roadbook_vr.avatar_driver_yaw_degrees
            && roadbook_vr.avatar_passenger_yaw_degrees == rhs.roadbook_vr.avatar_passenger_yaw_degrees
            && roadbook_vr.asset_reuse_mode == rhs.roadbook_vr.asset_reuse_mode
            && roadbook_vr.panel_width_meters == rhs.roadbook_vr.panel_width_meters
            && roadbook_vr.panel_height_meters == rhs.roadbook_vr.panel_height_meters
            && roadbook_vr.panel_offset == rhs.roadbook_vr.panel_offset
            && roadbook_vr.panel_tilt_degrees == rhs.roadbook_vr.panel_tilt_degrees
            && roadbook_vr.notes_per_page == rhs.roadbook_vr.notes_per_page
            && roadbook_vr.page_turn_seconds == rhs.roadbook_vr.page_turn_seconds
            && roadbook_vr.fallback_pose == rhs.roadbook_vr.fallback_pose
            && experimental.disable_multiview == rhs.experimental.disable_multiview
            && experimental.adjust_displaytime_ms == rhs.experimental.adjust_displaytime_ms;
    }

    bool write(const std::filesystem::path& path) const
    {
        constexpr auto round = [](double v) -> double { return std::round(v * 1000.0) / 1000.0; };

        std::ofstream f(path);
        if (!f.good()) {
            return false;
        }
        toml::table out {
            { "menuSize", round(menu_size) },
            { "menuScene", menu_scene },
            { "overlaySize", round(overlay_size) },
            { "overlayTranslateX", round(overlay_translation.x) },
            { "overlayTranslateY", round(overlay_translation.y) },
            { "overlayTranslateZ", round(overlay_translation.z) },
            { "lockToHorizon", static_cast<int>(lock_to_horizon) },
            { "horizonLockMultiplier", horizon_lock_multiplier },
            { "lowpassRollFilter", round(lowpass_roll_filter) },
            { "lowpassPitchFilter", round(lowpass_pitch_filter) },
            { "lowpassYawFilter", round(lowpass_yaw_filter) },
            { "horizonLockFlip", horizon_lock_flip },
            { "desktopWindowMode", companion_mode_str(companion_mode) },
            { "drawLoadingScreen", draw_loading_screen },
            { "debug", debug },
            { "debugMode", debug_mode },
            { "renderPauseMenu3d", render_pausemenu_3d },
            { "renderPreStage3d", render_prestage_3d },
            { "renderReplays3d", render_replays_3d },
            { "runtime", runtime == OPENVR ? "steamvr" : "openxr" },
            { "desktopWindowOffsetX", companion_offset.x },
            { "desktopWindowOffsetY", companion_offset.y },
            { "desktopWindowSize", companion_size },
            { "desktopEye", static_cast<int>(companion_eye) },
            { "renderParticles", render_particles },
            { "alwaysRenderParticlesInReplay", always_render_particles_in_replay },
            { "multiViewRendering", multiview },
            { "recenterAtSessionStart", recenter_at_session_start },
            { "recenterAtStageStart", recenter_at_stage_start },
            { "3dof", threedof },
        };

        toml::table gfxTbl;
        for (const auto& v : gfx) {
            toml::table t;
            toml::array a;

            const auto& ctx = v.second;
            for (const auto& stage : ctx.stage_ids) {
                a.push_back(stage);
            }

            t.insert("superSampling", round(ctx.supersampling));
            if (ctx.msaa) {
                t.insert("antiAliasing", static_cast<int>(ctx.msaa.value()));
            }
            if (std::string(v.first) != "default") {
                t.insert("stages", a);
            }
            gfxTbl.insert(v.first, t);
        }
        out.insert("gfx", gfxTbl);

        toml::table openxr;
        openxr.insert("worldScale", world_scale);
        openxr.insert("quadViewRendering", wanted_quad_view_rendering);
        openxr.insert("peripheralAntiAliasing", peripheral_msaa);
        openxr.insert("motionCompensation", openxr_motion_compensation);
        openxr.insert("predictionDampening", prediction_dampening);
        if (!enable_xr_api_path_modification) {
            openxr.insert("xrApiPathModification", false);
        }
        if (!enable_obsmirror_support) {
            openxr.insert("xrApiLayerObsmirror", false);
        }
        out.insert("OpenXR", openxr);

        toml::table passenger_vr_node;
        passenger_vr_node.insert("enabled", passenger_vr.enabled);
        passenger_vr_node.insert("cameraOffset", toml::array {
                                                   round(passenger_vr.camera_offset.x),
                                                   round(passenger_vr.camera_offset.y),
                                                   round(passenger_vr.camera_offset.z),
                                               });
        passenger_vr_node.insert("cameraYawDegrees", round(passenger_vr.camera_yaw_degrees));
        passenger_vr_node.insert("renderMode", passenger_vr.render_mode);
        passenger_vr_node.insert("streamHost", passenger_vr.stream_host);
        passenger_vr_node.insert("streamPort", passenger_vr.stream_port);
        passenger_vr_node.insert("posePort", passenger_vr.pose_port);
        passenger_vr_node.insert("recenterKey", passenger_vr.recenter_key);
        out.insert("PassengerVR", passenger_vr_node);

        toml::table roadbook_vr_node;
        roadbook_vr_node.insert("enabled", roadbook_vr.enabled);
        roadbook_vr_node.insert("rbrRoot", roadbook_vr.rbr_root);
        roadbook_vr_node.insert("source", roadbook_vr.source);
        roadbook_vr_node.insert("lockHand", roadbook_vr.lock_hand);
        roadbook_vr_node.insert("pageHand", roadbook_vr.page_hand);
        roadbook_vr_node.insert("driverVisible", roadbook_vr.driver_visible);
        roadbook_vr_node.insert("passengerVisible", roadbook_vr.passenger_visible);
        roadbook_vr_node.insert("avatarEnabled", roadbook_vr.avatar_enabled);
        roadbook_vr_node.insert("avatarDriverVisible", roadbook_vr.avatar_driver_visible);
        roadbook_vr_node.insert("avatarPassengerVisible", roadbook_vr.avatar_passenger_visible);
        roadbook_vr_node.insert("avatarHandsVisible", roadbook_vr.avatar_hands_visible);
        roadbook_vr_node.insert("playerHandsEnabled", roadbook_vr.player_hands_enabled);
        roadbook_vr_node.insert("playerHandsInputMode", roadbook_vr.player_hands_input_mode);
        roadbook_vr_node.insert("playerHandsRenderDriverSelf", roadbook_vr.player_hands_render_driver_self);
        roadbook_vr_node.insert("playerHandsRenderDriverToPassenger", roadbook_vr.player_hands_render_driver_to_passenger);
        roadbook_vr_node.insert("playerHandsRenderCoDriver", roadbook_vr.player_hands_render_codriver);
        roadbook_vr_node.insert("playerHandsQuality", roadbook_vr.player_hands_quality);
        roadbook_vr_node.insert("playerHandsMaxAgeMs", roadbook_vr.player_hands_max_age_ms);
        roadbook_vr_node.insert("playerHandsDebug", roadbook_vr.player_hands_debug);
        roadbook_vr_node.insert("assetProbeEnabled", roadbook_vr.asset_probe_enabled);
        roadbook_vr_node.insert("avatarReuseMode", roadbook_vr.avatar_reuse_mode);
        roadbook_vr_node.insert("avatarReplayEnabled", roadbook_vr.avatar_replay_enabled);
        roadbook_vr_node.insert("avatarFallbackEnabled", roadbook_vr.avatar_fallback_enabled);
        roadbook_vr_node.insert("roadbookUseGameAssets", roadbook_vr.roadbook_use_game_assets);
        roadbook_vr_node.insert("avatarDumpEnabled", roadbook_vr.avatar_dump_enabled);
        roadbook_vr_node.insert("avatarDumpTarget", roadbook_vr.avatar_dump_target);
        roadbook_vr_node.insert("avatarDumpDirectory", roadbook_vr.avatar_dump_directory);
        roadbook_vr_node.insert("avatarDumpApplyWorldTransform", roadbook_vr.avatar_dump_apply_world_transform);
        roadbook_vr_node.insert("avatarMeshSource", roadbook_vr.avatar_mesh_source);
        roadbook_vr_node.insert("avatarMeshEnabled", roadbook_vr.avatar_mesh_enabled);
        roadbook_vr_node.insert("avatarMeshPath", roadbook_vr.avatar_mesh_path);
        roadbook_vr_node.insert("avatarMeshTextureMode", roadbook_vr.avatar_mesh_texture_mode);
        roadbook_vr_node.insert("avatarMeshScale", round(roadbook_vr.avatar_mesh_scale));
        roadbook_vr_node.insert("avatarRigEnabled", roadbook_vr.avatar_rig_enabled);
        roadbook_vr_node.insert("avatarRigProfile", roadbook_vr.avatar_rig_profile);
        roadbook_vr_node.insert("avatarRigDebugRegions", roadbook_vr.avatar_rig_debug_regions);
        roadbook_vr_node.insert("avatarRigPose", roadbook_vr.avatar_rig_pose);
        roadbook_vr_node.insert("avatarRigLivePassengerEnabled", roadbook_vr.avatar_rig_live_passenger_enabled);
        roadbook_vr_node.insert("avatarRigLivePoseMaxAgeMs", roadbook_vr.avatar_rig_live_pose_max_age_ms);
        roadbook_vr_node.insert("avatarRigHandScale", round(roadbook_vr.avatar_rig_hand_scale));
        roadbook_vr_node.insert("avatarRigHeadRotationScale", toml::array {
                                                              round(roadbook_vr.avatar_rig_head_rotation_scale.x),
                                                              round(roadbook_vr.avatar_rig_head_rotation_scale.y),
                                                              round(roadbook_vr.avatar_rig_head_rotation_scale.z),
                                                          });
        roadbook_vr_node.insert("avatarRigHandRotationScale", toml::array {
                                                              round(roadbook_vr.avatar_rig_hand_rotation_scale.x),
                                                              round(roadbook_vr.avatar_rig_hand_rotation_scale.y),
                                                              round(roadbook_vr.avatar_rig_hand_rotation_scale.z),
                                                          });
        roadbook_vr_node.insert("avatarRigLiveHandFallback", roadbook_vr.avatar_rig_live_hand_fallback);
        roadbook_vr_node.insert("avatarControllerProfile", roadbook_vr.avatar_controller_profile);
        roadbook_vr_node.insert("avatarLivePoseTransformMode", roadbook_vr.avatar_live_pose_transform_mode);
        roadbook_vr_node.insert("avatarLivePoseSwapHands", roadbook_vr.avatar_live_pose_swap_hands);
        roadbook_vr_node.insert("avatarDiagnosticEnabled", roadbook_vr.avatar_diagnostic_enabled);
        roadbook_vr_node.insert("avatarDiagnosticDirectory", roadbook_vr.avatar_diagnostic_directory);
        roadbook_vr_node.insert("avatarDiagnosticTarget", roadbook_vr.avatar_diagnostic_target);
        roadbook_vr_node.insert("avatarDiagnosticCaptureFrames", roadbook_vr.avatar_diagnostic_capture_frames);
        roadbook_vr_node.insert("avatarDiagnosticPoseSweep", roadbook_vr.avatar_diagnostic_pose_sweep);
        roadbook_vr_node.insert("avatarDiagnosticOverlay", roadbook_vr.avatar_diagnostic_overlay);
        roadbook_vr_node.insert("avatarAutoSeatPlacement", roadbook_vr.avatar_auto_seat_placement);
        roadbook_vr_node.insert("avatarDriverSeatCorrection", toml::array {
                                                           round(roadbook_vr.avatar_driver_seat_correction.x),
                                                           round(roadbook_vr.avatar_driver_seat_correction.y),
                                                           round(roadbook_vr.avatar_driver_seat_correction.z),
                                                       });
        roadbook_vr_node.insert("avatarPassengerSeatCorrection", toml::array {
                                                              round(roadbook_vr.avatar_passenger_seat_correction.x),
                                                              round(roadbook_vr.avatar_passenger_seat_correction.y),
                                                              round(roadbook_vr.avatar_passenger_seat_correction.z),
                                                          });
        roadbook_vr_node.insert("avatarDriverOffset", toml::array {
                                                   round(roadbook_vr.avatar_driver_offset.x),
                                                   round(roadbook_vr.avatar_driver_offset.y),
                                                   round(roadbook_vr.avatar_driver_offset.z),
                                               });
        roadbook_vr_node.insert("avatarPassengerOffset", toml::array {
                                                      round(roadbook_vr.avatar_passenger_offset.x),
                                                      round(roadbook_vr.avatar_passenger_offset.y),
                                                      round(roadbook_vr.avatar_passenger_offset.z),
                                                  });
        roadbook_vr_node.insert("avatarDriverYawDegrees", round(roadbook_vr.avatar_driver_yaw_degrees));
        roadbook_vr_node.insert("avatarPassengerYawDegrees", round(roadbook_vr.avatar_passenger_yaw_degrees));
        roadbook_vr_node.insert("assetReuseMode", roadbook_vr.asset_reuse_mode);
        roadbook_vr_node.insert("panelWidthMeters", round(roadbook_vr.panel_width_meters));
        roadbook_vr_node.insert("panelHeightMeters", round(roadbook_vr.panel_height_meters));
        roadbook_vr_node.insert("panelOffset", toml::array {
                                                 round(roadbook_vr.panel_offset.x),
                                                 round(roadbook_vr.panel_offset.y),
                                                 round(roadbook_vr.panel_offset.z),
                                             });
        roadbook_vr_node.insert("panelTiltDegrees", toml::array {
                                                     round(roadbook_vr.panel_tilt_degrees.x),
                                                     round(roadbook_vr.panel_tilt_degrees.y),
                                                     round(roadbook_vr.panel_tilt_degrees.z),
                                                 });
        roadbook_vr_node.insert("notesPerPage", roadbook_vr.notes_per_page);
        roadbook_vr_node.insert("pageTurnSeconds", round(roadbook_vr.page_turn_seconds));
        roadbook_vr_node.insert("fallbackPose", roadbook_vr.fallback_pose);
        out.insert("RoadbookVR", roadbook_vr_node);

        toml::table experimental_node;
        experimental_node.insert("disableMultiView", experimental.disable_multiview);
        experimental_node.insert("adjustDisplayTimeMs", experimental.adjust_displaytime_ms);
        out.insert("experimental", experimental_node);

        f << out;
        f.close();
        return f.good();
    }

    static Config from_toml(const std::filesystem::path& path)
    {
        toml::table parsed;
        auto cfg = Config {};

        if (!std::filesystem::exists(path)) {
            if (!cfg.write(path)) {
                MessageBoxA(nullptr, "Could not write openRBRVR.toml", "Error", MB_OK);
            }
            return cfg;
        } else {
            try {
                parsed = toml::parse_file(path.c_str());
            } catch (const toml::parse_error& e) {
                MessageBoxA(nullptr, std::format("Failed to parse openRBRVR.toml: {}. Please check the syntax.", e.what()).c_str(), "Parse error", MB_OK);
                return cfg;
            }
        }
        if (parsed.size() == 0) {
            MessageBoxA(nullptr, "openRBRVR.toml is empty, continuing with default config.", "Parse error", MB_OK);
            return cfg;
        }
        cfg.menu_size = parsed["menuSize"].value_or(1.0f);
        cfg.menu_scene = parsed["menuScene"].value_or(true);
        cfg.overlay_size = parsed["overlaySize"].value_or(1.0f);
        cfg.overlay_translation.x = parsed["overlayTranslateX"].value_or(0.0f);
        cfg.overlay_translation.y = parsed["overlayTranslateY"].value_or(0.0f);
        cfg.overlay_translation.z = parsed["overlayTranslateZ"].value_or(0.0f);
        cfg.lock_to_horizon = static_cast<HorizonLock>(parsed["lockToHorizon"].value_or(0));
        cfg.horizon_lock_multiplier = parsed["horizonLockMultiplier"].value_or(1.0);
        cfg.lowpass_roll_filter = parsed["lowpassRollFilter"].value_or(1.0f);
        cfg.lowpass_pitch_filter = parsed["lowpassPitchFilter"].value_or(1.0f);
        cfg.lowpass_yaw_filter = parsed["lowpassYawFilter"].value_or(1.0f);
        cfg.horizon_lock_flip = parsed["horizonLockFlip"].value_or(false);
        cfg.companion_mode = companion_mode_from_str(parsed["desktopWindowMode"].value_or("vreye"));
        cfg.draw_loading_screen = parsed["drawLoadingScreen"].value_or(true);
        cfg.debug = parsed["debug"].value_or(false);
        cfg.debug_mode = parsed["debugMode"].value_or(0);
        cfg.render_pausemenu_3d = parsed["renderPauseMenu3d"].value_or(true);
        cfg.render_prestage_3d = parsed["renderPreStage3d"].value_or(false);
        cfg.render_replays_3d = parsed["renderReplays3d"].value_or(false);
        cfg.companion_offset = { parsed["desktopWindowOffsetX"].value_or(0), parsed["desktopWindowOffsetY"].value_or(0) };
        cfg.companion_size = parsed["desktopWindowSize"].value_or(100);
        cfg.companion_eye = static_cast<RenderTarget>(std::clamp(parsed["desktopEye"].value_or(0), 0, 1));
        cfg.render_particles = parsed["renderParticles"].value_or(true);
        cfg.always_render_particles_in_replay = parsed["alwaysRenderParticlesInReplay"].value_or(false);
        cfg.multiview = parsed["multiViewRendering"].value_or(false);
        cfg.recenter_at_session_start = parsed["recenterAtSessionStart"].value_or(false);
        cfg.recenter_at_stage_start = parsed["recenterAtStageStart"].value_or(false);
        cfg.threedof = parsed["3dof"].value_or(false);

        const std::string& runtime = parsed["runtime"].value_or("steamvr");
        if (runtime == "openxr" || runtime == "openxr-wmr") {
            cfg.runtime = OPENXR;
        } else {
            cfg.runtime = OPENVR;
        }

        auto oxrnode = parsed["OpenXR"];
        if (oxrnode.is_table()) {
            cfg.world_scale = std::clamp(oxrnode["worldScale"].value_or(1000), 500, 1500);
            cfg.quad_view_rendering = cfg.wanted_quad_view_rendering = oxrnode["quadViewRendering"].value_or(false);
            cfg.peripheral_msaa = static_cast<D3DMULTISAMPLE_TYPE>(oxrnode["peripheralAntiAliasing"].value_or(0));
            cfg.openxr_motion_compensation = oxrnode["motionCompensation"].value_or(false);
            cfg.prediction_dampening = oxrnode["predictionDampening"].value_or(0);
            cfg.prediction_dampening = std::clamp(cfg.prediction_dampening, 0LL, 100LL);
            cfg.enable_xr_api_path_modification = oxrnode["xrApiPathModification"].value_or(true);
            cfg.enable_obsmirror_support = oxrnode["xrApiLayerObsmirror"].value_or(true);
        }

        auto passenger_vr_node = parsed["PassengerVR"];
        if (passenger_vr_node.is_table()) {
            cfg.passenger_vr.enabled = passenger_vr_node["enabled"].value_or(false);
            cfg.passenger_vr.camera_yaw_degrees = passenger_vr_node["cameraYawDegrees"].value_or(0.0f);
            cfg.passenger_vr.render_mode = passenger_vr_node["renderMode"].value_or("mono");
            cfg.passenger_vr.stream_host = passenger_vr_node["streamHost"].value_or("0.0.0.0");
            cfg.passenger_vr.stream_port = passenger_vr_node["streamPort"].value_or(7790);
            cfg.passenger_vr.pose_port = passenger_vr_node["posePort"].value_or(7791);
            cfg.passenger_vr.recenter_key = passenger_vr_node["recenterKey"].value_or("QuestMenu");

            if (auto offset = passenger_vr_node["cameraOffset"].as_array(); offset && offset->size() >= 3) {
                const auto read_float = [](toml::array* a, size_t idx, float fallback) {
                    const auto node = a->get(idx);
                    if (!node) {
                        return fallback;
                    }
                    if (auto f = node->value<double>()) {
                        return static_cast<float>(*f);
                    }
                    if (auto i = node->value<int64_t>()) {
                        return static_cast<float>(*i);
                    }
                    return fallback;
                };
                cfg.passenger_vr.camera_offset.x = read_float(offset, 0, cfg.passenger_vr.camera_offset.x);
                cfg.passenger_vr.camera_offset.y = read_float(offset, 1, cfg.passenger_vr.camera_offset.y);
                cfg.passenger_vr.camera_offset.z = read_float(offset, 2, cfg.passenger_vr.camera_offset.z);
            }
        }

        auto roadbook_vr_node = parsed["RoadbookVR"];
        if (roadbook_vr_node.is_table()) {
            cfg.roadbook_vr.enabled = roadbook_vr_node["enabled"].value_or(false);
            cfg.roadbook_vr.rbr_root = roadbook_vr_node["rbrRoot"].value_or("C:\\richard burns rally");
            cfg.roadbook_vr.source = roadbook_vr_node["source"].value_or("ngpMyPacenotes");
            cfg.roadbook_vr.lock_hand = roadbook_vr_node["lockHand"].value_or("left");
            cfg.roadbook_vr.page_hand = roadbook_vr_node["pageHand"].value_or("right");
            cfg.roadbook_vr.driver_visible = roadbook_vr_node["driverVisible"].value_or(true);
            cfg.roadbook_vr.passenger_visible = roadbook_vr_node["passengerVisible"].value_or(true);
            cfg.roadbook_vr.avatar_enabled = roadbook_vr_node["avatarEnabled"].value_or(false);
            cfg.roadbook_vr.avatar_driver_visible = roadbook_vr_node["avatarDriverVisible"].value_or(true);
            cfg.roadbook_vr.avatar_passenger_visible = roadbook_vr_node["avatarPassengerVisible"].value_or(true);
            cfg.roadbook_vr.avatar_hands_visible = roadbook_vr_node["avatarHandsVisible"].value_or(true);
            cfg.roadbook_vr.player_hands_enabled = roadbook_vr_node["playerHandsEnabled"].value_or(true);
            cfg.roadbook_vr.player_hands_input_mode = roadbook_vr_node["playerHandsInputMode"].value_or("auto");
            cfg.roadbook_vr.player_hands_render_driver_self = roadbook_vr_node["playerHandsRenderDriverSelf"].value_or(true);
            cfg.roadbook_vr.player_hands_render_driver_to_passenger = roadbook_vr_node["playerHandsRenderDriverToPassenger"].value_or(true);
            cfg.roadbook_vr.player_hands_render_codriver = roadbook_vr_node["playerHandsRenderCoDriver"].value_or(false);
            cfg.roadbook_vr.player_hands_quality = roadbook_vr_node["playerHandsQuality"].value_or("low");
            cfg.roadbook_vr.player_hands_max_age_ms = std::clamp(roadbook_vr_node["playerHandsMaxAgeMs"].value_or(250), 50, 5000);
            cfg.roadbook_vr.player_hands_debug = roadbook_vr_node["playerHandsDebug"].value_or(false);
            cfg.roadbook_vr.asset_probe_enabled = roadbook_vr_node["assetProbeEnabled"].value_or(false);
            cfg.roadbook_vr.avatar_reuse_mode = roadbook_vr_node["avatarReuseMode"].value_or("rbrCaptureThenFallback");
            cfg.roadbook_vr.avatar_replay_enabled = roadbook_vr_node["avatarReplayEnabled"].value_or(true);
            cfg.roadbook_vr.avatar_fallback_enabled = roadbook_vr_node["avatarFallbackEnabled"].value_or(true);
            cfg.roadbook_vr.roadbook_use_game_assets = roadbook_vr_node["roadbookUseGameAssets"].value_or(true);
            cfg.roadbook_vr.avatar_dump_enabled = roadbook_vr_node["avatarDumpEnabled"].value_or(false);
            cfg.roadbook_vr.avatar_dump_target = roadbook_vr_node["avatarDumpTarget"].value_or("both");
            cfg.roadbook_vr.avatar_dump_directory = roadbook_vr_node["avatarDumpDirectory"].value_or("Plugins\\openRBRVR\\Captures");
            cfg.roadbook_vr.avatar_dump_apply_world_transform = roadbook_vr_node["avatarDumpApplyWorldTransform"].value_or(false);
            cfg.roadbook_vr.avatar_mesh_source = roadbook_vr_node["avatarMeshSource"].value_or("rbrDump");
            cfg.roadbook_vr.avatar_mesh_enabled = roadbook_vr_node["avatarMeshEnabled"].value_or(true);
            cfg.roadbook_vr.avatar_mesh_path = roadbook_vr_node["avatarMeshPath"].value_or("Plugins\\openRBRVR\\Assets\\RaceCarDriverLowPoly\\model.obj");
            cfg.roadbook_vr.avatar_mesh_texture_mode = roadbook_vr_node["avatarMeshTextureMode"].value_or("rbr");
            cfg.roadbook_vr.avatar_mesh_scale = std::clamp(roadbook_vr_node["avatarMeshScale"].value_or(0.42f), 0.01f, 5.0f);
            cfg.roadbook_vr.avatar_rig_enabled = roadbook_vr_node["avatarRigEnabled"].value_or(true);
            cfg.roadbook_vr.avatar_rig_profile = roadbook_vr_node["avatarRigProfile"].value_or("Plugins\\openRBRVR\\Captures\\codriver_rig.toml");
            cfg.roadbook_vr.avatar_rig_debug_regions = roadbook_vr_node["avatarRigDebugRegions"].value_or(false);
            cfg.roadbook_vr.avatar_rig_pose = roadbook_vr_node["avatarRigPose"].value_or("codriverReadingBook");
            cfg.roadbook_vr.avatar_rig_live_passenger_enabled = roadbook_vr_node["avatarRigLivePassengerEnabled"].value_or(true);
            cfg.roadbook_vr.avatar_rig_live_pose_max_age_ms = std::clamp(roadbook_vr_node["avatarRigLivePoseMaxAgeMs"].value_or(500), 50, 5000);
            cfg.roadbook_vr.avatar_rig_hand_scale = std::clamp(roadbook_vr_node["avatarRigHandScale"].value_or(1.0f), 0.1f, 3.0f);
            cfg.roadbook_vr.avatar_rig_live_hand_fallback = roadbook_vr_node["avatarRigLiveHandFallback"].value_or("defaultPose");
            cfg.roadbook_vr.avatar_controller_profile = roadbook_vr_node["avatarControllerProfile"].value_or("questTouch");
            cfg.roadbook_vr.avatar_live_pose_transform_mode = roadbook_vr_node["avatarLivePoseTransformMode"].value_or("identity");
            cfg.roadbook_vr.avatar_live_pose_swap_hands = roadbook_vr_node["avatarLivePoseSwapHands"].value_or(false);
            cfg.roadbook_vr.avatar_diagnostic_enabled = roadbook_vr_node["avatarDiagnosticEnabled"].value_or(false);
            cfg.roadbook_vr.avatar_diagnostic_directory = roadbook_vr_node["avatarDiagnosticDirectory"].value_or("Plugins\\openRBRVR\\Diagnostics");
            cfg.roadbook_vr.avatar_diagnostic_target = roadbook_vr_node["avatarDiagnosticTarget"].value_or("LeftEye");
            cfg.roadbook_vr.avatar_diagnostic_capture_frames = std::clamp(roadbook_vr_node["avatarDiagnosticCaptureFrames"].value_or(1), 1, 60);
            cfg.roadbook_vr.avatar_diagnostic_pose_sweep = roadbook_vr_node["avatarDiagnosticPoseSweep"].value_or(false);
            cfg.roadbook_vr.avatar_diagnostic_overlay = roadbook_vr_node["avatarDiagnosticOverlay"].value_or(false);
            cfg.roadbook_vr.avatar_auto_seat_placement = roadbook_vr_node["avatarAutoSeatPlacement"].value_or(true);
            cfg.roadbook_vr.avatar_driver_yaw_degrees = roadbook_vr_node["avatarDriverYawDegrees"].value_or(-5.0f);
            cfg.roadbook_vr.avatar_passenger_yaw_degrees = roadbook_vr_node["avatarPassengerYawDegrees"].value_or(5.0f);
            cfg.roadbook_vr.asset_reuse_mode = roadbook_vr_node["assetReuseMode"].value_or("probeThenFallback");
            cfg.roadbook_vr.panel_width_meters = roadbook_vr_node["panelWidthMeters"].value_or(0.55f);
            cfg.roadbook_vr.panel_height_meters = roadbook_vr_node["panelHeightMeters"].value_or(0.38f);
            cfg.roadbook_vr.notes_per_page = std::clamp(roadbook_vr_node["notesPerPage"].value_or(12), 4, 24);
            cfg.roadbook_vr.page_turn_seconds = std::clamp(roadbook_vr_node["pageTurnSeconds"].value_or(0.36f), 0.05f, 2.0f);
            cfg.roadbook_vr.fallback_pose = roadbook_vr_node["fallbackPose"].value_or("head");

            const auto read_float = [](toml::array* a, size_t idx, float fallback) {
                const auto node = a->get(idx);
                if (!node) {
                    return fallback;
                }
                if (auto f = node->value<double>()) {
                    return static_cast<float>(*f);
                }
                if (auto i = node->value<int64_t>()) {
                    return static_cast<float>(*i);
                }
                return fallback;
            };
            if (auto offset = roadbook_vr_node["panelOffset"].as_array(); offset && offset->size() >= 3) {
                cfg.roadbook_vr.panel_offset.x = read_float(offset, 0, cfg.roadbook_vr.panel_offset.x);
                cfg.roadbook_vr.panel_offset.y = read_float(offset, 1, cfg.roadbook_vr.panel_offset.y);
                cfg.roadbook_vr.panel_offset.z = read_float(offset, 2, cfg.roadbook_vr.panel_offset.z);
            }
            if (auto tilt = roadbook_vr_node["panelTiltDegrees"].as_array(); tilt && tilt->size() >= 3) {
                cfg.roadbook_vr.panel_tilt_degrees.x = read_float(tilt, 0, cfg.roadbook_vr.panel_tilt_degrees.x);
                cfg.roadbook_vr.panel_tilt_degrees.y = read_float(tilt, 1, cfg.roadbook_vr.panel_tilt_degrees.y);
                cfg.roadbook_vr.panel_tilt_degrees.z = read_float(tilt, 2, cfg.roadbook_vr.panel_tilt_degrees.z);
            }
            if (auto scale = roadbook_vr_node["avatarRigHeadRotationScale"].as_array(); scale && scale->size() >= 3) {
                cfg.roadbook_vr.avatar_rig_head_rotation_scale.x = read_float(scale, 0, cfg.roadbook_vr.avatar_rig_head_rotation_scale.x);
                cfg.roadbook_vr.avatar_rig_head_rotation_scale.y = read_float(scale, 1, cfg.roadbook_vr.avatar_rig_head_rotation_scale.y);
                cfg.roadbook_vr.avatar_rig_head_rotation_scale.z = read_float(scale, 2, cfg.roadbook_vr.avatar_rig_head_rotation_scale.z);
            }
            if (auto scale = roadbook_vr_node["avatarRigHandRotationScale"].as_array(); scale && scale->size() >= 3) {
                cfg.roadbook_vr.avatar_rig_hand_rotation_scale.x = read_float(scale, 0, cfg.roadbook_vr.avatar_rig_hand_rotation_scale.x);
                cfg.roadbook_vr.avatar_rig_hand_rotation_scale.y = read_float(scale, 1, cfg.roadbook_vr.avatar_rig_hand_rotation_scale.y);
                cfg.roadbook_vr.avatar_rig_hand_rotation_scale.z = read_float(scale, 2, cfg.roadbook_vr.avatar_rig_hand_rotation_scale.z);
            }
            if (auto offset = roadbook_vr_node["avatarDriverOffset"].as_array(); offset && offset->size() >= 3) {
                cfg.roadbook_vr.avatar_driver_offset.x = read_float(offset, 0, cfg.roadbook_vr.avatar_driver_offset.x);
                cfg.roadbook_vr.avatar_driver_offset.y = read_float(offset, 1, cfg.roadbook_vr.avatar_driver_offset.y);
                cfg.roadbook_vr.avatar_driver_offset.z = read_float(offset, 2, cfg.roadbook_vr.avatar_driver_offset.z);
            }
            if (auto offset = roadbook_vr_node["avatarPassengerOffset"].as_array(); offset && offset->size() >= 3) {
                cfg.roadbook_vr.avatar_passenger_offset.x = read_float(offset, 0, cfg.roadbook_vr.avatar_passenger_offset.x);
                cfg.roadbook_vr.avatar_passenger_offset.y = read_float(offset, 1, cfg.roadbook_vr.avatar_passenger_offset.y);
                cfg.roadbook_vr.avatar_passenger_offset.z = read_float(offset, 2, cfg.roadbook_vr.avatar_passenger_offset.z);
            }
            if (auto correction = roadbook_vr_node["avatarDriverSeatCorrection"].as_array(); correction && correction->size() >= 3) {
                cfg.roadbook_vr.avatar_driver_seat_correction.x = read_float(correction, 0, cfg.roadbook_vr.avatar_driver_seat_correction.x);
                cfg.roadbook_vr.avatar_driver_seat_correction.y = read_float(correction, 1, cfg.roadbook_vr.avatar_driver_seat_correction.y);
                cfg.roadbook_vr.avatar_driver_seat_correction.z = read_float(correction, 2, cfg.roadbook_vr.avatar_driver_seat_correction.z);
            }
            if (auto correction = roadbook_vr_node["avatarPassengerSeatCorrection"].as_array(); correction && correction->size() >= 3) {
                cfg.roadbook_vr.avatar_passenger_seat_correction.x = read_float(correction, 0, cfg.roadbook_vr.avatar_passenger_seat_correction.x);
                cfg.roadbook_vr.avatar_passenger_seat_correction.y = read_float(correction, 1, cfg.roadbook_vr.avatar_passenger_seat_correction.y);
                cfg.roadbook_vr.avatar_passenger_seat_correction.z = read_float(correction, 2, cfg.roadbook_vr.avatar_passenger_seat_correction.z);
            }
        }

        auto gfxnode = parsed["gfx"];
        if (gfxnode.is_table()) {
            toml::table* gfx = gfxnode.as_table();
            gfx->for_each([&cfg](const toml::key& key, toml::table& val) {
                auto k = std::string(key.data());
                auto ss = val["superSampling"].value_or(1.0f);
                auto msaa_int = val["antiAliasing"];
                std::optional<D3DMULTISAMPLE_TYPE> msaa;
                if (msaa_int) {
                    msaa = static_cast<D3DMULTISAMPLE_TYPE>(msaa_int.value_or(0));
                }
                const auto stagesNode = val["stages"];
                std::vector<int> stages;
                if (stagesNode.is_array()) {
                    stagesNode.as_array()->for_each([&stages](toml::value<int64_t>& v) {
                        stages.push_back(static_cast<int>(*v));
                    });
                }
                bool quad_view_stage_rendering = val["quadViewRendering"].value_or(cfg.wanted_quad_view_rendering);
                bool multiview_stage_rendering = val["multiViewRendering"].value_or(cfg.multiview);
                cfg.gfx[k] = RenderContextConfig { ss, msaa, stages, quad_view_stage_rendering, multiview_stage_rendering };
            });
        }

        auto experimental_node = parsed["experimental"];
        if (experimental_node.is_table()) {
            cfg.experimental.adjust_displaytime_ms = experimental_node["adjustDisplayTimeMs"].value_or(0);
            cfg.experimental.disable_multiview = experimental_node["disableMultiView"].value_or(false);
        }

        return cfg;
    }

    void apply_dxvk_conf()
    {
        std::ifstream dxvk_config("dxvk.conf");
        std::string line;
        if (dxvk_config.good()) {
            while (std::getline(dxvk_config, line)) {
                auto end = std::remove_if(line.begin(), line.end(), isspace);
                auto s = std::string(line.begin(), end);
                if (s.starts_with("d3d9.forceSwapchainMSAA")) {
                    std::stringstream ss { s };
                    std::string key, value;
                    std::getline(ss, key, '=');
                    std::getline(ss, value);
                    gfx["default"].msaa = static_cast<D3DMULTISAMPLE_TYPE>(std::max<int>(0, int_or_default(value, 0)));
                }
                if (s.starts_with("d3d9.samplerAnisotropy")) {
                    std::stringstream ss { s };
                    std::string key, value;
                    std::getline(ss, key, '=');
                    std::getline(ss, value);
                    anisotropy = std::max<int>(int_or_default(value, 0), 0);
                }
            }
        } else {
            gfx["default"].msaa = D3DMULTISAMPLE_NONE;
        }
    }

    static std::optional<std::string> to_string(const std::filesystem::path& p)
    {
        return p.generic_string();
    }

    static std::optional<std::filesystem::path> resolve_car_ini_path(uint32_t car_id)
    {
        auto cars_ini_path = "Cars\\cars.ini";
        if (!std::filesystem::exists(cars_ini_path)) {
            dbg("Could not resolve car ini path");
            return std::nullopt;
        }

        try {
            ini::IniFile cars_ini(cars_ini_path);
            auto car_key = std::format("Car0{}", car_id);
            return std::filesystem::path(cars_ini[car_key]["IniFile"].as<std::string>()
                | std::ranges::views::filter([](char c) { return c != '"'; })
                | std::ranges::to<std::string>());
        } catch (...) {
            dbg("Could not resolve car ini path");
            return std::nullopt;
        }
    }

    static std::optional<std::filesystem::path> resolve_personal_car_ini_path(uint32_t car_id)
    {
        auto ini_file_path = resolve_car_ini_path(car_id);
        if (!ini_file_path) {
            return std::nullopt;
        }

        auto personal_filename = ini_file_path.value().filename();
        personal_filename.replace_extension("");
        personal_filename += "_personal";
        personal_filename.replace_extension(".ini");
        ini_file_path.value().replace_filename(personal_filename);

        return ini_file_path;
    }

    static bool insert_or_update_seat_translation(uint32_t car_id, const glm::vec3& seat_translation)
    {
        auto ini_path = resolve_personal_car_ini_path(car_id).and_then(to_string);
        if (!ini_path) {
            return false;
        }

        try {
            ini::IniFile personal_ini(ini_path.value());
            personal_ini["openRBRVR"]["seatPosition"] = vec3_as_space_separated_string(seat_translation);
            personal_ini.save(ini_path.value());
        } catch (...) {
            dbg("Updating seat translation failed");
            return false;
        }

        return true;
    }

    std::tuple<glm::vec3, bool> load_seat_translation(uint32_t car_id)
    {
        const auto default_translation = std::make_tuple(glm::vec3 { 0.33, 1.0, -1.0 }, true);
        auto ini_path = resolve_personal_car_ini_path(car_id).and_then(to_string);
        if (!ini_path) {
            return default_translation;
        }

        bool is_openrbrvr_translation = false;
        std::optional<glm::vec3> seat_translation = std::nullopt;

        try {
            if (std::filesystem::exists(ini_path.value())) {
                ini::IniFile personal_ini(ini_path.value());

                seat_translation = vec3_from_space_separated_string(personal_ini["openRBRVR"]["seatPosition"].as<std::string>());

                if (!seat_translation) {
                    seat_translation = vec3_from_space_separated_string(personal_ini["Cam_internal"]["Pos"].as<std::string>());
                } else {
                    is_openrbrvr_translation = true;
                }
            }

            if (!seat_translation) {
                ini_path = resolve_car_ini_path(car_id).and_then(to_string);
                if (!ini_path) {
                    return default_translation;
                }
                ini::IniFile ini(ini_path.value());
                seat_translation = vec3_from_space_separated_string(ini["Cam_internal"]["Pos"].as<std::string>());
            }

            return seat_translation
                .and_then([&](const glm::vec3& t) { return std::optional(std::make_tuple(t, is_openrbrvr_translation)); })
                .value_or(default_translation);

        } catch (...) {
            dbg("Loading seat translation failed");
            return default_translation;
        }
    }

    static Config from_path(const std::filesystem::path& path)
    {
        auto toml_cfg = from_toml(path / "openRBRVR.toml");
        toml_cfg.apply_dxvk_conf();

        return toml_cfg;
    }
};
