#include "RoadbookVR.hpp"

#include "Config.hpp"
#include "Dx.hpp"
#include "Globals.hpp"
#include "RBR.hpp"
#include "VR.hpp"
#include "Vertex.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <gtx/quaternion.hpp>

namespace {
    constexpr uint32_t TextureWidth = 1024;
    constexpr uint32_t TextureHeight = 768;
    constexpr auto InputDebounce = std::chrono::milliseconds(250);

    enum class Hand {
        Left,
        Right,
        Head,
    };

    struct Note {
        int type = 0;
        float distance = 0.0f;
        std::string label;
    };

    struct ControllerState {
        bool valid = false;
        M4 device_to_absolute = glm::identity<M4>();
        uint64_t pressed = 0;
        float axis_x = 0.0f;
        uint32_t packet = 0;
    };

    IDirect3DTexture9* panel_texture = nullptr;
    IDirect3DVertexBuffer9* panel_quad = nullptr;
    uint32_t panel_quad_w = 0;
    uint32_t panel_quad_h = 0;
    std::vector<Note> notes;
    std::filesystem::path loaded_file;
    std::filesystem::file_time_type loaded_write_time {};
    std::unordered_map<int, std::string> labels_by_id;
    ControllerState left_controller;
    ControllerState right_controller;
    ControllerState passenger_left_hand;
    ControllerState passenger_right_hand;
    int pending_next_pages = 0;
    int pending_previous_pages = 0;
    int pending_visibility_toggles = 0;
    int pending_page_resets = 0;
    bool visible = true;
    int current_page = 0;
    bool texture_dirty = true;
    std::chrono::steady_clock::time_point last_input;
    std::string last_status = "not loaded";

    std::string read_text(const std::filesystem::path& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f.good()) {
            return {};
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    std::string trim(std::string s)
    {
        const auto not_space = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
        return s;
    }

    std::string humanize(std::string token)
    {
        std::replace(token.begin(), token.end(), '_', ' ');
        std::string out;
        bool upper = true;
        for (auto c : token) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!out.empty() && out.back() != ' ') {
                    out.push_back(' ');
                }
                upper = true;
                continue;
            }
            out.push_back(static_cast<char>(upper ? std::toupper(static_cast<unsigned char>(c)) : std::tolower(static_cast<unsigned char>(c))));
            upper = false;
        }
        return out.empty() ? token : out;
    }

    Hand hand_from_string(const std::string& s, Hand fallback)
    {
        auto lower = s;
        std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower == "left") {
            return Hand::Left;
        }
        if (lower == "right") {
            return Hand::Right;
        }
        if (lower == "head") {
            return Hand::Head;
        }
        return fallback;
    }

    ControllerState& state_for_hand(Hand hand)
    {
        return hand == Hand::Right ? right_controller : left_controller;
    }

    ControllerState& passenger_state_for_hand(Hand hand)
    {
        return hand == Hand::Right ? passenger_right_hand : passenger_left_hand;
    }

    void load_labels(const std::filesystem::path& rbr_root)
    {
        labels_by_id.clear();
        std::unordered_map<std::string, std::string> custom_labels;

        const auto custom_path = rbr_root / "Plugins" / "NGPCarMenu" / "MyPacenotes" / "CustomLabels" / "pacenoteLabels.sym";
        std::istringstream custom_stream(read_text(custom_path));
        for (std::string raw; std::getline(custom_stream, raw);) {
            const auto line = trim(raw);
            if (line.empty() || line.starts_with(';') || line.starts_with('#')) {
                continue;
            }
            const auto split = line.find('=');
            if (split == std::string::npos) {
                continue;
            }
            custom_labels[trim(line.substr(0, split))] = trim(line.substr(split + 1));
        }

        const auto json = read_text(rbr_root / "Plugins" / "Pacenote" / "config" / "pacenotes.json");
        const std::regex note_re(R"json(\{\s*"id"\s*:\s*"?(\d+)"?[\s\S]*?"name"\s*:\s*"([^"]+)")json");
        for (auto it = std::sregex_iterator(json.begin(), json.end(), note_re); it != std::sregex_iterator(); ++it) {
            const auto id = std::stoi((*it)[1].str());
            const auto raw_name = (*it)[2].str();
            const auto custom = custom_labels.find(raw_name);
            labels_by_id[id] = custom == custom_labels.end() ? humanize(raw_name) : custom->second;
        }
    }

    std::optional<std::filesystem::path> find_latest_note_file(const std::filesystem::path& rbr_root)
    {
        const auto root = rbr_root / "Plugins" / "NGPCarMenu" / "MyPacenotes";
        if (!std::filesystem::exists(root)) {
            return std::nullopt;
        }

        std::optional<std::filesystem::path> best_path;
        std::filesystem::file_time_type best_time {};

        for (const auto& dir : std::filesystem::directory_iterator(root)) {
            if (!dir.is_directory() || dir.path().filename() == "CustomLabels") {
                continue;
            }

            std::optional<std::filesystem::path> stage_best;
            std::filesystem::file_time_type stage_time {};
            for (const auto& file : std::filesystem::directory_iterator(dir.path())) {
                if (!file.is_regular_file() || file.path().extension() != ".ini") {
                    continue;
                }
                const auto t = file.last_write_time();
                if (!stage_best || t > stage_time) {
                    stage_best = file.path();
                    stage_time = t;
                }
            }

            if (stage_best && (!best_path || stage_time > best_time)) {
                best_path = stage_best;
                best_time = stage_time;
            }
        }

        return best_path;
    }

    void load_notes_if_needed()
    {
        const auto rbr_root = std::filesystem::path(g::cfg.roadbook_vr.rbr_root);
        const auto selected = find_latest_note_file(rbr_root);
        if (!selected) {
            last_status = "no MyPacenotes file found";
            notes.clear();
            texture_dirty = true;
            return;
        }

        const auto write_time = std::filesystem::last_write_time(*selected);
        if (*selected == loaded_file && write_time == loaded_write_time && !labels_by_id.empty()) {
            return;
        }

        load_labels(rbr_root);
        loaded_file = *selected;
        loaded_write_time = write_time;
        notes.clear();
        current_page = 0;

        const auto content = read_text(*selected);
        std::istringstream stream(content);
        std::string section;
        std::unordered_map<std::string, std::string> values;
        const auto flush = [&] {
            if (!section.starts_with('P') || section == "PACENOTES") {
                return;
            }
            const auto type_it = values.find("type");
            const auto distance_it = values.find("distance");
            if (type_it == values.end()) {
                return;
            }
            Note note;
            note.type = std::stoi(type_it->second);
            note.distance = distance_it == values.end() ? 0.0f : std::stof(distance_it->second);
            const auto label = labels_by_id.find(note.type);
            note.label = label == labels_by_id.end() ? std::format("Type {}", note.type) : label->second;
            notes.push_back(note);
        };

        for (std::string raw; std::getline(stream, raw);) {
            auto line = trim(raw);
            if (line.empty() || line.starts_with(';') || line.starts_with('#')) {
                continue;
            }
            if (line.starts_with('[') && line.ends_with(']')) {
                flush();
                section = line.substr(1, line.size() - 2);
                values.clear();
                continue;
            }
            const auto split = line.find('=');
            if (split != std::string::npos) {
                values[trim(line.substr(0, split))] = trim(line.substr(split + 1));
            }
        }
        flush();

        std::ranges::sort(notes, {}, &Note::distance);
        last_status = std::format("{} notes: {}", notes.size(), loaded_file.filename().string());
        texture_dirty = true;
    }

    bool create_or_update_quad(IDirect3DDevice9* dev)
    {
        const auto width = static_cast<uint32_t>(std::max(1.0f, g::cfg.roadbook_vr.panel_width_meters * 1000.0f));
        const auto height = static_cast<uint32_t>(std::max(1.0f, g::cfg.roadbook_vr.panel_height_meters * 1000.0f));
        if (panel_quad && panel_quad_w == width && panel_quad_h == height) {
            return true;
        }
        if (panel_quad) {
            panel_quad->Release();
            panel_quad = nullptr;
        }

        const float half_w = g::cfg.roadbook_vr.panel_width_meters * 0.5f;
        const float half_h = g::cfg.roadbook_vr.panel_height_meters * 0.5f;
        Vertex quad[] = {
            { -half_w, half_h, 0.0f, 0.0f, 0.0f },
            { half_w, half_h, 0.0f, 1.0f, 0.0f },
            { -half_w, -half_h, 0.0f, 0.0f, 1.0f },
            { half_w, -half_h, 0.0f, 1.0f, 1.0f },
        };

        if (!create_vertex_buffer(dev, quad, 4, &panel_quad)) {
            dbg("RoadbookVR: failed to create panel quad");
            return false;
        }
        panel_quad_w = width;
        panel_quad_h = height;
        return true;
    }

    bool ensure_texture(IDirect3DDevice9* dev)
    {
        if (panel_texture) {
            return true;
        }
        if (dev->CreateTexture(TextureWidth, TextureHeight, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &panel_texture, nullptr) != D3D_OK) {
            dbg("RoadbookVR: failed to create panel texture");
            return false;
        }
        texture_dirty = true;
        return true;
    }

    void draw_text(HDC dc, int x, int y, int w, int h, const std::string& text, UINT format)
    {
        RECT rect { x, y, x + w, y + h };
        DrawTextA(dc, text.c_str(), -1, &rect, format);
    }

    void redraw_texture(IDirect3DDevice9* dev)
    {
        if (!texture_dirty || !ensure_texture(dev)) {
            return;
        }

        BITMAPINFO bmi {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = TextureWidth;
        bmi.bmiHeader.biHeight = -static_cast<LONG>(TextureHeight);
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pixels = nullptr;
        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (!bitmap || !pixels) {
            if (bitmap) {
                DeleteObject(bitmap);
            }
            DeleteDC(dc);
            return;
        }

        const auto old_bitmap = SelectObject(dc, bitmap);
        HBRUSH bg = CreateSolidBrush(RGB(16, 18, 20));
        RECT full { 0, 0, static_cast<LONG>(TextureWidth), static_cast<LONG>(TextureHeight) };
        FillRect(dc, &full, bg);
        DeleteObject(bg);

        HPEN border_pen = CreatePen(PS_SOLID, 3, RGB(218, 178, 95));
        HBRUSH panel_brush = CreateSolidBrush(RGB(28, 31, 34));
        const auto old_pen = SelectObject(dc, border_pen);
        const auto old_brush = SelectObject(dc, panel_brush);
        RoundRect(dc, 18, 18, TextureWidth - 18, TextureHeight - 18, 22, 22);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(border_pen);
        DeleteObject(panel_brush);

        HFONT title_font = CreateFontA(42, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
        HFONT note_font = CreateFontA(34, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
        HFONT small_font = CreateFontA(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(245, 238, 220));
        SelectObject(dc, title_font);
        const auto title = loaded_file.empty() ? "RBR Roadbook" : loaded_file.parent_path().filename().string();
        draw_text(dc, 48, 34, TextureWidth - 96, 52, title, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        const auto notes_per_page = std::clamp(g::cfg.roadbook_vr.notes_per_page, 4, 24);
        const auto page_count = std::max(1, static_cast<int>((notes.size() + notes_per_page - 1) / notes_per_page));
        current_page = std::clamp(current_page, 0, page_count - 1);
        SelectObject(dc, small_font);
        SetTextColor(dc, RGB(177, 188, 191));
        draw_text(dc, 48, 88, TextureWidth - 96, 34, std::format("Page {} / {}   {}", current_page + 1, page_count, loaded_file.filename().string()), DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        const int start = current_page * notes_per_page;
        const int end = std::min<int>(static_cast<int>(notes.size()), start + notes_per_page);
        int y = 140;
        SelectObject(dc, note_font);
        for (int i = start; i < end; ++i) {
            const auto& note = notes[i];
            SetTextColor(dc, RGB(218, 178, 95));
            draw_text(dc, 54, y, 140, 38, std::format("{:>5.0f}m", note.distance), DT_RIGHT | DT_SINGLELINE);
            SetTextColor(dc, RGB(247, 247, 242));
            draw_text(dc, 220, y, TextureWidth - 280, 38, note.label, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += 46;
        }

        if (notes.empty()) {
            SetTextColor(dc, RGB(247, 247, 242));
            draw_text(dc, 54, 150, TextureWidth - 108, 80, "No pacenotes loaded", DT_LEFT | DT_SINGLELINE);
            SelectObject(dc, small_font);
            SetTextColor(dc, RGB(177, 188, 191));
            draw_text(dc, 54, 210, TextureWidth - 108, 80, last_status, DT_LEFT | DT_WORDBREAK);
        }

        auto* px = static_cast<uint8_t*>(pixels);
        for (uint32_t i = 0; i < TextureWidth * TextureHeight; ++i) {
            px[i * 4 + 3] = 235;
        }

        D3DLOCKED_RECT locked {};
        if (panel_texture->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD) == D3D_OK) {
            for (uint32_t row = 0; row < TextureHeight; ++row) {
                memcpy(static_cast<uint8_t*>(locked.pBits) + row * locked.Pitch, px + row * TextureWidth * 4, TextureWidth * 4);
            }
            panel_texture->UnlockRect(0);
            texture_dirty = false;
        }

        SelectObject(dc, old_bitmap);
        DeleteObject(title_font);
        DeleteObject(note_font);
        DeleteObject(small_font);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }

    void process_input()
    {
        const auto now = std::chrono::steady_clock::now();
        const auto notes_per_page = std::clamp(g::cfg.roadbook_vr.notes_per_page, 4, 24);
        const auto page_count = std::max(1, static_cast<int>((notes.size() + notes_per_page - 1) / notes_per_page));

        if (pending_next_pages > 0) {
            current_page = std::min(current_page + pending_next_pages, page_count - 1);
            pending_next_pages = 0;
            texture_dirty = true;
        }
        if (pending_previous_pages > 0) {
            current_page = std::max(current_page - pending_previous_pages, 0);
            pending_previous_pages = 0;
            texture_dirty = true;
        }
        if (pending_page_resets > 0) {
            current_page = 0;
            pending_page_resets = 0;
            texture_dirty = true;
        }
        if (pending_visibility_toggles > 0) {
            visible = !visible;
            pending_visibility_toggles = 0;
        }

        if (now - last_input < InputDebounce) {
            return;
        }

        const auto page_hand = hand_from_string(g::cfg.roadbook_vr.page_hand, Hand::Right);
        const auto& state = state_for_hand(page_hand);
        if (!state.valid) {
            return;
        }

        const bool axis_next = state.axis_x > 0.65f;
        const bool axis_prev = state.axis_x < -0.65f;
        const bool grip = (state.pressed & vr::ButtonMaskFromId(vr::k_EButton_Grip)) != 0;
        const bool trigger = (state.pressed & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)) != 0;

        if (axis_next) {
            current_page = std::min(current_page + 1, page_count - 1);
            texture_dirty = true;
            last_input = now;
        } else if (axis_prev) {
            current_page = std::max(current_page - 1, 0);
            texture_dirty = true;
            last_input = now;
        } else if (grip) {
            visible = !visible;
            last_input = now;
        } else if (trigger) {
            current_page = 0;
            texture_dirty = true;
            last_input = now;
        }
    }

    M4 panel_model_matrix(RenderTarget target)
    {
        const auto lock_hand = hand_from_string(g::cfg.roadbook_vr.lock_hand, Hand::Left);
        const auto& cfg = g::cfg.roadbook_vr;
        const auto offset = glm::translate(glm::identity<M4>(), cfg.panel_offset);
        const auto tilt = glm::rotate(glm::identity<M4>(), glm::radians(cfg.panel_tilt_degrees.x), glm::vec3(1, 0, 0))
            * glm::rotate(glm::identity<M4>(), glm::radians(cfg.panel_tilt_degrees.y), glm::vec3(0, 1, 0))
            * glm::rotate(glm::identity<M4>(), glm::radians(cfg.panel_tilt_degrees.z), glm::vec3(0, 0, 1));

        if (lock_hand != Hand::Head) {
            const auto passenger_target = target == PassengerLeft || target == PassengerRight;
            const auto& controller = passenger_target ? passenger_state_for_hand(lock_hand) : state_for_hand(lock_hand);
            if (controller.valid) {
                return controller.device_to_absolute * offset * tilt;
            }
        }

        return glm::translate(glm::identity<M4>(), glm::vec3 { 0.0f, -0.18f, 0.85f }) * tilt;
    }

    void render_target(IDirect3DDevice9* dev, VRInterface* vr, RenderTarget target)
    {
        if (!vr->prepare_vr_rendering(dev, target, false)) {
            return;
        }
        render_textured_quad(dev, vr, panel_texture, target, panel_quad, panel_model_matrix(target));
        vr->finish_vr_rendering(dev, target);
    }
}

namespace roadbook_vr {
    void update_openvr_controller_state(vr::IVRSystem* hmd, const vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount])
    {
        left_controller.valid = false;
        right_controller.valid = false;

        for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
            const auto role = hmd->GetControllerRoleForTrackedDeviceIndex(i);
            if (role != vr::TrackedControllerRole_LeftHand && role != vr::TrackedControllerRole_RightHand) {
                continue;
            }

            auto& target = role == vr::TrackedControllerRole_RightHand ? right_controller : left_controller;
            target.valid = poses[i].bPoseIsValid;
            if (target.valid) {
                target.device_to_absolute = M4(
                    { poses[i].mDeviceToAbsoluteTracking.m[0][0], poses[i].mDeviceToAbsoluteTracking.m[1][0], poses[i].mDeviceToAbsoluteTracking.m[2][0], 0.0f },
                    { poses[i].mDeviceToAbsoluteTracking.m[0][1], poses[i].mDeviceToAbsoluteTracking.m[1][1], poses[i].mDeviceToAbsoluteTracking.m[2][1], 0.0f },
                    { poses[i].mDeviceToAbsoluteTracking.m[0][2], poses[i].mDeviceToAbsoluteTracking.m[1][2], poses[i].mDeviceToAbsoluteTracking.m[2][2], 0.0f },
                    { poses[i].mDeviceToAbsoluteTracking.m[0][3], poses[i].mDeviceToAbsoluteTracking.m[1][3], poses[i].mDeviceToAbsoluteTracking.m[2][3], 1.0f });
            }

            vr::VRControllerState_t state {};
            if (hmd->GetControllerState(i, &state, sizeof(state))) {
                target.pressed = state.ulButtonPressed;
                target.packet = state.unPacketNum;
                target.axis_x = std::abs(state.rAxis[3].x) > std::abs(state.rAxis[0].x) ? state.rAxis[3].x : state.rAxis[0].x;
            }
        }
    }

    void update_passenger_hand_state(const std::string& side, bool valid, const glm::vec3& position, const glm::quat& orientation, bool pinch)
    {
        auto& target = side == "right" ? passenger_right_hand : passenger_left_hand;
        target.valid = valid;
        if (!valid) {
            target.pressed = 0;
            target.axis_x = 0.0f;
            return;
        }

        const auto mapped_position = glm::vec3 { position.x, position.y, -position.z };
        target.device_to_absolute = glm::translate(glm::identity<M4>(), mapped_position) * glm::mat4_cast(orientation);
        target.pressed = pinch ? vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger) : 0;
        target.axis_x = 0.0f;
        target.packet++;
    }

    void queue_passenger_command(const std::string& command)
    {
        if (command == "nextPage") {
            pending_next_pages++;
        } else if (command == "previousPage") {
            pending_previous_pages++;
        } else if (command == "toggleVisible") {
            pending_visibility_toggles++;
        } else if (command == "resetPage") {
            pending_page_resets++;
        }
    }

    void render(IDirect3DDevice9* dev, VRInterface* vr)
    {
        if (!g::cfg.roadbook_vr.enabled || !rbr::is_rendering_3d()) {
            return;
        }

        load_notes_if_needed();
        process_input();
        if (!visible || !ensure_texture(dev) || !create_or_update_quad(dev)) {
            return;
        }
        redraw_texture(dev);

        if (g::cfg.roadbook_vr.driver_visible) {
            render_target(dev, vr, LeftEye);
            if (!dx::multiview_rendering_enabled()) {
                render_target(dev, vr, RightEye);
            }
        }

        if (g::cfg.roadbook_vr.passenger_visible && g::cfg.passenger_vr.enabled) {
            render_target(dev, vr, PassengerLeft);
            if (g::cfg.passenger_vr.render_mode == "stereo") {
                render_target(dev, vr, PassengerRight);
            }
        }
    }

    std::string debug_status()
    {
        return std::format("RoadbookVR: {}, page {}, visible {}", last_status, current_page + 1, visible ? "yes" : "no");
    }
}
