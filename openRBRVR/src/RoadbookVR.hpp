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
    float panel_width_meters = 0.55f;
    float panel_height_meters = 0.38f;
    glm::vec3 panel_offset = { 0.04f, 0.04f, 0.12f };
    glm::vec3 panel_tilt_degrees = { -18.0f, 0.0f, 0.0f };
    int notes_per_page = 12;
    std::string fallback_pose = "head";
};

namespace roadbook_vr {
    void update_openvr_controller_state(vr::IVRSystem* hmd, const vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]);
    void update_passenger_hand_state(const std::string& side, bool valid, const glm::vec3& position, const glm::quat& orientation, bool pinch);
    void queue_passenger_command(const std::string& command);
    void render(IDirect3DDevice9* dev, VRInterface* vr);
    std::string debug_status();
}
