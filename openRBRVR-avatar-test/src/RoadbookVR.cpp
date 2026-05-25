#include "RoadbookVR.hpp"

#include "AssetProbe.hpp"
#include "AvatarHands.hpp"
#include "AvatarMesh.hpp"
#include "AvatarRig.hpp"
#include "Config.hpp"
#include "DriverAvatarTracking/DriverAvatarTracker.hpp"
#include "Dx.hpp"
#include "Globals.hpp"
#include "PassengerVR.hpp"
#include "RBR.hpp"
#include "VR.hpp"
#include "Vertex.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <vector>

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
    IDirect3DTexture9* turning_page_texture = nullptr;
    IDirect3DTexture9* cover_texture = nullptr;
    IDirect3DTexture9* paper_shadow_texture = nullptr;
    IDirect3DTexture9* hand_texture = nullptr;
    IDirect3DTexture9* cuff_texture = nullptr;
    IDirect3DTexture9* driver_suit_texture = nullptr;
    IDirect3DTexture9* passenger_suit_texture = nullptr;
    IDirect3DTexture9* suit_accent_texture = nullptr;
    IDirect3DTexture9* face_texture = nullptr;
    IDirect3DTexture9* helmet_texture = nullptr;
    IDirect3DTexture9* visor_texture = nullptr;
    IDirect3DVertexBuffer9* panel_quad = nullptr;
    IDirect3DVertexBuffer9* cover_quad = nullptr;
    IDirect3DVertexBuffer9* paper_shadow_quad = nullptr;
    IDirect3DVertexBuffer9* turning_page_quad = nullptr;
    IDirect3DVertexBuffer9* palm_quad = nullptr;
    IDirect3DVertexBuffer9* finger_quad = nullptr;
    IDirect3DVertexBuffer9* cuff_quad = nullptr;
    IDirect3DVertexBuffer9* torso_quad = nullptr;
    IDirect3DVertexBuffer9* chest_quad = nullptr;
    IDirect3DVertexBuffer9* shoulder_quad = nullptr;
    IDirect3DVertexBuffer9* neck_quad = nullptr;
    IDirect3DVertexBuffer9* head_quad = nullptr;
    IDirect3DVertexBuffer9* face_quad = nullptr;
    IDirect3DVertexBuffer9* limb_quad = nullptr;
    IDirect3DVertexBuffer9* leg_quad = nullptr;
    IDirect3DVertexBuffer9* visor_quad = nullptr;
    uint32_t panel_quad_w = 0;
    uint32_t panel_quad_h = 0;
    std::vector<Note> notes;
    std::filesystem::path loaded_file;
    std::filesystem::file_time_type loaded_write_time {};
    std::unordered_map<int, std::string> labels_by_id;
    ControllerState left_controller;
    ControllerState right_controller;
    driver_avatar_tracking::DriverAvatarTracker driver_tracker;
    bool visible = true;
    int current_page = 0;
    bool texture_dirty = true;
    bool turning_texture_dirty = true;
    int panel_texture_page = -1;
    int turning_texture_page = -1;
    bool page_turn_active = false;
    int page_turn_from = 0;
    int page_turn_to = 0;
    int page_turn_direction = 1;
    std::chrono::steady_clock::time_point page_turn_start;
    std::chrono::steady_clock::time_point last_input;
    std::string last_status = "not loaded";
    std::string last_avatar_seat_status = "manual avatar placement";

    struct AvatarSeatPlacement {
        glm::vec3 driver_offset {};
        glm::vec3 passenger_offset {};
        std::filesystem::path source_ini;
    };

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

    std::string strip_quotes(std::string s)
    {
        s = trim(std::move(s));
        s.erase(std::remove(s.begin(), s.end(), '"'), s.end());
        return s;
    }

    std::optional<glm::vec3> read_ini_vec3(ini::IniFile& ini, const std::string& section, const std::string& key)
    {
        try {
            return vec3_from_space_separated_string(ini[section][key].as<std::string>());
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<std::filesystem::path> current_car_ini_path()
    {
        const auto car_id = rbr::get_current_car_id();
        if (!car_id) {
            return std::nullopt;
        }

        const auto root = std::filesystem::path(g::cfg.roadbook_vr.rbr_root);
        const auto cars_ini_path = root / "Cars\\cars.ini";
        if (!std::filesystem::exists(cars_ini_path)) {
            return std::nullopt;
        }

        try {
            ini::IniFile cars_ini(cars_ini_path.string());
            const auto car_key = std::format("Car{:02}", *car_id);
            auto ini_file = strip_quotes(cars_ini[car_key]["IniFile"].as<std::string>());
            auto path = std::filesystem::path(ini_file);
            if (path.is_relative()) {
                path = root / path;
            }
            if (std::filesystem::exists(path)) {
                return path;
            }
        } catch (...) {
        }

        return std::nullopt;
    }

    std::optional<AvatarSeatPlacement> load_avatar_seat_placement()
    {
        static std::optional<uint32_t> cached_car_id;
        static std::optional<AvatarSeatPlacement> cached_placement;

        const auto car_id = rbr::get_current_car_id();
        if (!car_id) {
            cached_car_id.reset();
            cached_placement.reset();
            last_avatar_seat_status = "manual avatar placement: no car id";
            return std::nullopt;
        }

        if (cached_car_id == car_id) {
            return cached_placement;
        }

        cached_car_id = car_id;
        cached_placement.reset();

        const auto ini_path = current_car_ini_path();
        if (!ini_path) {
            last_avatar_seat_status = std::format("manual avatar placement: car {} ini not found", *car_id);
            return std::nullopt;
        }

        try {
            ini::IniFile car_ini(ini_path->string());
            const auto cam = read_ini_vec3(car_ini, "Cam_internal", "Pos");
            const auto driver_base = read_ini_vec3(car_ini, "Point_drivercambase", "Pos");
            if (!cam || !driver_base || std::abs(driver_base->x) < 0.05f) {
                last_avatar_seat_status = std::format("manual avatar placement: no seat base in {}", ini_path->filename().string());
                return std::nullopt;
            }

            const auto codriver_base = glm::vec3 { -driver_base->x, driver_base->y, driver_base->z };
            const auto codriver_delta = codriver_base - *cam;

            AvatarSeatPlacement placement {};
            // RBR car coordinates have lateral X opposite to the local VR/avatar root convention used here.
            placement.passenger_offset = { -codriver_delta.x, codriver_delta.y, codriver_delta.z };
            placement.driver_offset = { -placement.passenger_offset.x, placement.passenger_offset.y, placement.passenger_offset.z };
            placement.source_ini = *ini_path;
            cached_placement = placement;
            last_avatar_seat_status = std::format(
                "auto avatar seats: {} passenger [{:.2f}, {:.2f}, {:.2f}]",
                ini_path->filename().string(),
                placement.passenger_offset.x,
                placement.passenger_offset.y,
                placement.passenger_offset.z);
            return cached_placement;
        } catch (...) {
            last_avatar_seat_status = std::format("manual avatar placement: failed to parse {}", ini_path->filename().string());
            return std::nullopt;
        }
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

    std::string lower_copy(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
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

    int notes_per_page()
    {
        return std::clamp(g::cfg.roadbook_vr.notes_per_page, 4, 24);
    }

    int page_count()
    {
        const auto npp = notes_per_page();
        return std::max(1, static_cast<int>((notes.size() + npp - 1) / npp));
    }

    float smoothstep(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    float page_turn_progress()
    {
        if (!page_turn_active) {
            return 1.0f;
        }

        const auto elapsed = std::chrono::steady_clock::now() - page_turn_start;
        const auto duration_ms = std::max(1.0f, g::cfg.roadbook_vr.page_turn_seconds * 1000.0f);
        return std::clamp(static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()) / duration_ms, 0.0f, 1.0f);
    }

    void update_page_turn_state()
    {
        if (page_turn_active && page_turn_progress() >= 1.0f) {
            page_turn_active = false;
        }
    }

    bool create_or_update_sized_quad(IDirect3DDevice9* dev, float width, float height, IDirect3DVertexBuffer9** quad)
    {
        if (*quad) {
            (*quad)->Release();
            *quad = nullptr;
        }

        const float half_w = width * 0.5f;
        const float half_h = height * 0.5f;
        Vertex v[] = {
            { -half_w, half_h, 0.0f, 0.0f, 0.0f },
            { half_w, half_h, 0.0f, 1.0f, 0.0f },
            { -half_w, -half_h, 0.0f, 0.0f, 1.0f },
            { half_w, -half_h, 0.0f, 1.0f, 1.0f },
        };

        return create_vertex_buffer(dev, v, 4, quad);
    }

    bool create_or_update_quads(IDirect3DDevice9* dev)
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

        const auto page_w = g::cfg.roadbook_vr.panel_width_meters;
        const auto page_h = g::cfg.roadbook_vr.panel_height_meters;
        if (!create_or_update_sized_quad(dev, page_w, page_h, &panel_quad)) {
            dbg("RoadbookVR: failed to create panel quad");
            return false;
        }
        if (!create_or_update_sized_quad(dev, page_w + 0.065f, page_h + 0.055f, &cover_quad)
            || !create_or_update_sized_quad(dev, page_w + 0.025f, page_h + 0.025f, &paper_shadow_quad)
            || !create_or_update_sized_quad(dev, 0.088f, 0.062f, &palm_quad)
            || !create_or_update_sized_quad(dev, 0.018f, 0.050f, &finger_quad)
            || !create_or_update_sized_quad(dev, 0.082f, 0.040f, &cuff_quad)
            || !create_or_update_sized_quad(dev, 0.26f, 0.36f, &torso_quad)
            || !create_or_update_sized_quad(dev, 0.15f, 0.24f, &chest_quad)
            || !create_or_update_sized_quad(dev, 0.13f, 0.055f, &shoulder_quad)
            || !create_or_update_sized_quad(dev, 0.070f, 0.060f, &neck_quad)
            || !create_or_update_sized_quad(dev, 0.145f, 0.165f, &head_quad)
            || !create_or_update_sized_quad(dev, 0.090f, 0.055f, &face_quad)
            || !create_or_update_sized_quad(dev, 0.050f, 0.255f, &limb_quad)
            || !create_or_update_sized_quad(dev, 0.065f, 0.300f, &leg_quad)
            || !create_or_update_sized_quad(dev, 0.115f, 0.040f, &visor_quad)) {
            dbg("RoadbookVR: failed to create book/hand quads");
            return false;
        }
        panel_quad_w = width;
        panel_quad_h = height;
        return true;
    }

    bool update_turning_page_quad(IDirect3DDevice9* dev, float progress)
    {
        const float half_w = g::cfg.roadbook_vr.panel_width_meters * 0.5f;
        const float half_h = g::cfg.roadbook_vr.panel_height_meters * 0.5f;
        const float width = g::cfg.roadbook_vr.panel_width_meters;
        const float hinge_x = page_turn_direction >= 0 ? -half_w : half_w;
        const float edge_sign = page_turn_direction >= 0 ? 1.0f : -1.0f;
        const float angle = smoothstep(progress) * 3.1415926535f;
        const float edge_x = hinge_x + edge_sign * width * std::cos(angle);
        const float edge_z = std::abs(width * std::sin(angle)) * 0.32f;

        Vertex quad[] = {
            { hinge_x, half_h, 0.0f, page_turn_direction >= 0 ? 0.0f : 1.0f, 0.0f },
            { edge_x, half_h, edge_z, page_turn_direction >= 0 ? 1.0f : 0.0f, 0.0f },
            { hinge_x, -half_h, 0.0f, page_turn_direction >= 0 ? 0.0f : 1.0f, 1.0f },
            { edge_x, -half_h, edge_z, page_turn_direction >= 0 ? 1.0f : 0.0f, 1.0f },
        };

        if (!turning_page_quad) {
            return create_vertex_buffer(dev, quad, 4, &turning_page_quad);
        }

        void* bufmem = nullptr;
        const auto size = sizeof(quad);
        if (turning_page_quad->Lock(0, size, &bufmem, 0) != D3D_OK) {
            return false;
        }
        memcpy(bufmem, quad, size);
        turning_page_quad->Unlock();
        return true;
    }

    bool create_solid_texture(IDirect3DDevice9* dev, D3DCOLOR color, IDirect3DTexture9** texture)
    {
        if (*texture) {
            return true;
        }

        if (dev->CreateTexture(4, 4, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, texture, nullptr) != D3D_OK) {
            return false;
        }

        D3DLOCKED_RECT locked {};
        if ((*texture)->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD) != D3D_OK) {
            return false;
        }
        for (int y = 0; y < 4; ++y) {
            auto* row = reinterpret_cast<D3DCOLOR*>(static_cast<uint8_t*>(locked.pBits) + y * locked.Pitch);
            for (int x = 0; x < 4; ++x) {
                row[x] = color;
            }
        }
        (*texture)->UnlockRect(0);
        return true;
    }

    bool ensure_texture(IDirect3DDevice9* dev)
    {
        bool created_page_texture = false;
        if (!panel_texture) {
            if (dev->CreateTexture(TextureWidth, TextureHeight, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &panel_texture, nullptr) != D3D_OK) {
                dbg("RoadbookVR: failed to create panel texture");
                return false;
            }
            created_page_texture = true;
        }
        if (!turning_page_texture) {
            if (dev->CreateTexture(TextureWidth, TextureHeight, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &turning_page_texture, nullptr) != D3D_OK) {
                dbg("RoadbookVR: failed to create page-turn texture");
                return false;
            }
            created_page_texture = true;
        }
        if (!create_solid_texture(dev, D3DCOLOR_ARGB(255, 45, 38, 30), &cover_texture)
            || !create_solid_texture(dev, D3DCOLOR_ARGB(95, 0, 0, 0), &paper_shadow_texture)
            || !create_solid_texture(dev, D3DCOLOR_ARGB(248, 218, 174, 132), &hand_texture)
            || !create_solid_texture(dev, D3DCOLOR_ARGB(255, 24, 27, 29), &cuff_texture)
            || !create_solid_texture(dev, D3DCOLOR_ARGB(245, 18, 22, 27), &driver_suit_texture)
            || !create_solid_texture(dev, D3DCOLOR_ARGB(245, 24, 29, 35), &passenger_suit_texture)
            || !create_solid_texture(dev, D3DCOLOR_ARGB(245, 190, 38, 34), &suit_accent_texture)
            || !create_solid_texture(dev, D3DCOLOR_ARGB(245, 214, 166, 128), &face_texture)
            || !create_solid_texture(dev, D3DCOLOR_ARGB(250, 232, 234, 228), &helmet_texture)
            || !create_solid_texture(dev, D3DCOLOR_ARGB(210, 18, 26, 34), &visor_texture)) {
            dbg("RoadbookVR: failed to create solid asset textures");
            return false;
        }
        if (created_page_texture) {
            texture_dirty = true;
            turning_texture_dirty = true;
        }
        return true;
    }

    void draw_text(HDC dc, int x, int y, int w, int h, const std::string& text, UINT format)
    {
        RECT rect { x, y, x + w, y + h };
        DrawTextA(dc, text.c_str(), -1, &rect, format);
    }

    void redraw_page_texture(IDirect3DDevice9* dev, IDirect3DTexture9* texture, int page_index, bool& dirty, int& texture_page)
    {
        if ((!dirty && texture_page == page_index) || !texture) {
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
        HBRUSH bg = CreateSolidBrush(RGB(236, 226, 199));
        RECT full { 0, 0, static_cast<LONG>(TextureWidth), static_cast<LONG>(TextureHeight) };
        FillRect(dc, &full, bg);
        DeleteObject(bg);

        HPEN border_pen = CreatePen(PS_SOLID, 4, RGB(92, 70, 42));
        HBRUSH panel_brush = CreateSolidBrush(RGB(248, 242, 222));
        const auto old_pen = SelectObject(dc, border_pen);
        const auto old_brush = SelectObject(dc, panel_brush);
        RoundRect(dc, 22, 18, TextureWidth - 22, TextureHeight - 18, 18, 18);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(border_pen);
        DeleteObject(panel_brush);

        HFONT title_font = CreateFontA(42, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
        HFONT note_font = CreateFontA(34, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
        HFONT small_font = CreateFontA(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(39, 37, 33));
        SelectObject(dc, title_font);
        const auto title = loaded_file.empty() ? "RBR Roadbook" : loaded_file.parent_path().filename().string();
        draw_text(dc, 48, 34, TextureWidth - 96, 52, title, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        const auto npp = notes_per_page();
        const auto pages = page_count();
        page_index = std::clamp(page_index, 0, pages - 1);
        SelectObject(dc, small_font);
        SetTextColor(dc, RGB(91, 83, 67));
        draw_text(dc, 48, 88, TextureWidth - 96, 34, std::format("Page {} / {}   {}", page_index + 1, pages, loaded_file.filename().string()), DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        const int start = page_index * npp;
        const int end = std::min<int>(static_cast<int>(notes.size()), start + npp);
        int y = 140;
        SelectObject(dc, note_font);
        for (int i = start; i < end; ++i) {
            const auto& note = notes[i];
            SetTextColor(dc, RGB(136, 87, 26));
            draw_text(dc, 54, y, 140, 38, std::format("{:>5.0f}m", note.distance), DT_RIGHT | DT_SINGLELINE);
            SetTextColor(dc, RGB(24, 26, 27));
            draw_text(dc, 220, y, TextureWidth - 280, 38, note.label, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += 46;
        }

        if (notes.empty()) {
            SetTextColor(dc, RGB(24, 26, 27));
            draw_text(dc, 54, 150, TextureWidth - 108, 80, "No pacenotes loaded", DT_LEFT | DT_SINGLELINE);
            SelectObject(dc, small_font);
            SetTextColor(dc, RGB(91, 83, 67));
            draw_text(dc, 54, 210, TextureWidth - 108, 80, last_status, DT_LEFT | DT_WORDBREAK);
        }

        auto* px = static_cast<uint8_t*>(pixels);
        for (uint32_t i = 0; i < TextureWidth * TextureHeight; ++i) {
            px[i * 4 + 3] = 235;
        }

        D3DLOCKED_RECT locked {};
        if (texture->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD) == D3D_OK) {
            for (uint32_t row = 0; row < TextureHeight; ++row) {
                memcpy(static_cast<uint8_t*>(locked.pBits) + row * locked.Pitch, px + row * TextureWidth * 4, TextureWidth * 4);
            }
            texture->UnlockRect(0);
            dirty = false;
            texture_page = page_index;
        }

        SelectObject(dc, old_bitmap);
        DeleteObject(title_font);
        DeleteObject(note_font);
        DeleteObject(small_font);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }

    void begin_page_turn(int target_page)
    {
        target_page = std::clamp(target_page, 0, page_count() - 1);
        if (target_page == current_page || page_turn_active) {
            return;
        }

        page_turn_from = current_page;
        page_turn_to = target_page;
        page_turn_direction = target_page > current_page ? 1 : -1;
        page_turn_start = std::chrono::steady_clock::now();
        page_turn_active = true;
        current_page = target_page;
        texture_dirty = true;
        turning_texture_dirty = true;
    }

    void process_input()
    {
        const auto now = std::chrono::steady_clock::now();
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
            begin_page_turn(current_page + 1);
            last_input = now;
        } else if (axis_prev) {
            begin_page_turn(current_page - 1);
            last_input = now;
        } else if (grip) {
            visible = !visible;
            last_input = now;
        } else if (trigger) {
            page_turn_active = false;
            current_page = 0;
            texture_dirty = true;
            last_input = now;
        }
    }

    M4 panel_model_matrix()
    {
        const auto lock_hand = hand_from_string(g::cfg.roadbook_vr.lock_hand, Hand::Left);
        const auto& cfg = g::cfg.roadbook_vr;
        const auto offset = glm::translate(glm::identity<M4>(), cfg.panel_offset);
        const auto tilt = glm::rotate(glm::identity<M4>(), glm::radians(cfg.panel_tilt_degrees.x), glm::vec3(1, 0, 0))
            * glm::rotate(glm::identity<M4>(), glm::radians(cfg.panel_tilt_degrees.y), glm::vec3(0, 1, 0))
            * glm::rotate(glm::identity<M4>(), glm::radians(cfg.panel_tilt_degrees.z), glm::vec3(0, 0, 1));

        if (lock_hand != Hand::Head) {
            const auto& controller = state_for_hand(lock_hand);
            if (controller.valid) {
                return controller.device_to_absolute * offset * tilt;
            }
        }

        return glm::translate(glm::identity<M4>(), glm::vec3 { 0.0f, -0.18f, 0.85f }) * tilt;
    }

    void render_hand(IDirect3DDevice9* dev, VRInterface* vr, RenderTarget target, const M4& root, bool mirror)
    {
        render_textured_quad(dev, vr, cuff_texture, target, cuff_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { 0.0f, -0.052f, 0.012f }));
        render_textured_quad(dev, vr, hand_texture, target, palm_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { 0.0f, 0.0f, 0.018f }));

        const float side = mirror ? -1.0f : 1.0f;
        for (int i = 0; i < 4; ++i) {
            const float x = (-0.030f + i * 0.020f) * side;
            const float y = 0.048f + (i == 1 || i == 2 ? 0.006f : 0.0f);
            render_textured_quad(dev, vr, hand_texture, target, finger_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { x, y, 0.022f }));
        }
    }

    void render_passenger_hands(IDirect3DDevice9* dev, VRInterface* vr, RenderTarget target, const M4& book_model)
    {
        if (!g::cfg.roadbook_vr.avatar_hands_visible) {
            return;
        }

        if (g::cfg.passenger_vr.enabled) {
            const auto state = passenger_vr::latest_pose_state();
            const bool fresh = state.age_ms >= 0 && state.age_ms <= g::cfg.roadbook_vr.avatar_rig_live_pose_max_age_ms;
            const auto render_live_hand = [&](const passenger_vr::TrackedPartPose& hand, bool mirror) {
                if (!fresh || !hand.valid) {
                    return false;
                }
                const auto root = glm::translate(glm::identity<M4>(), g::cfg.passenger_vr.camera_offset + hand.position)
                    * glm::rotate(glm::identity<M4>(), glm::radians(hand.rotation_degrees.x), glm::vec3(0, 1, 0))
                    * glm::rotate(glm::identity<M4>(), glm::radians(hand.rotation_degrees.y), glm::vec3(1, 0, 0))
                    * glm::rotate(glm::identity<M4>(), glm::radians(hand.rotation_degrees.z), glm::vec3(0, 0, 1));
                render_hand(dev, vr, target, root, mirror);
                return true;
            };

            const bool rendered_left = render_live_hand(state.left_hand, false);
            const bool rendered_right = render_live_hand(state.right_hand, true);
            if (rendered_left || rendered_right) {
                return;
            }
        }

        const float half_w = g::cfg.roadbook_vr.panel_width_meters * 0.5f;
        const float half_h = g::cfg.roadbook_vr.panel_height_meters * 0.5f;
        const float progress = page_turn_active ? smoothstep(page_turn_progress()) : 0.0f;
        const float swipe = page_turn_active ? page_turn_direction * progress * g::cfg.roadbook_vr.panel_width_meters * 0.46f : 0.0f;
        const float lift = page_turn_active ? std::sin(progress * 3.1415926535f) * 0.045f : 0.0f;

        const auto left_root = book_model
            * glm::translate(glm::identity<M4>(), glm::vec3 { -half_w * 0.62f, -half_h - 0.015f, 0.030f })
            * glm::rotate(glm::identity<M4>(), glm::radians(-12.0f), glm::vec3(0, 0, 1));
        const auto right_root = book_model
            * glm::translate(glm::identity<M4>(), glm::vec3 { half_w * 0.62f - swipe, -half_h - 0.010f + lift, 0.034f })
            * glm::rotate(glm::identity<M4>(), glm::radians(14.0f - page_turn_direction * progress * 18.0f), glm::vec3(0, 0, 1));

        render_hand(dev, vr, target, left_root, false);
        render_hand(dev, vr, target, right_root, true);
    }

    void render_driver_self_hands(IDirect3DDevice9* dev, VRInterface* vr, RenderTarget target)
    {
        if (!g::cfg.roadbook_vr.avatar_hands_visible) {
            return;
        }

        if (left_controller.valid) {
            const auto root = left_controller.device_to_absolute
                * glm::translate(glm::identity<M4>(), glm::vec3 { 0.015f, 0.000f, -0.020f })
                * glm::rotate(glm::identity<M4>(), glm::radians(-18.0f), glm::vec3(1, 0, 0));
            render_hand(dev, vr, target, root, false);
        }

        if (right_controller.valid) {
            const auto root = right_controller.device_to_absolute
                * glm::translate(glm::identity<M4>(), glm::vec3 { -0.015f, 0.000f, -0.020f })
                * glm::rotate(glm::identity<M4>(), glm::radians(-18.0f), glm::vec3(1, 0, 0));
            render_hand(dev, vr, target, root, true);
        }
    }

    M4 avatar_root(bool passenger_avatar)
    {
        auto seat_offset = passenger_avatar ? g::cfg.roadbook_vr.avatar_passenger_offset : g::cfg.roadbook_vr.avatar_driver_offset;
        if (g::cfg.roadbook_vr.avatar_auto_seat_placement) {
            if (const auto placement = load_avatar_seat_placement()) {
                seat_offset = passenger_avatar ? placement->passenger_offset : placement->driver_offset;
                seat_offset += passenger_avatar ? g::cfg.roadbook_vr.avatar_passenger_seat_correction : g::cfg.roadbook_vr.avatar_driver_seat_correction;
            }
        } else {
            last_avatar_seat_status = "manual avatar placement: disabled";
        }
        const auto yaw_degrees = passenger_avatar ? g::cfg.roadbook_vr.avatar_passenger_yaw_degrees : g::cfg.roadbook_vr.avatar_driver_yaw_degrees;

        return glm::translate(glm::identity<M4>(), seat_offset)
            * glm::rotate(glm::identity<M4>(), glm::radians(yaw_degrees), glm::vec3(0, 1, 0));
    }

    float rbr_dump_mesh_scale(const std::filesystem::path& path)
    {
        struct CachedScale {
            std::filesystem::file_time_type write_time {};
            float scale = 0.001f;
        };
        static std::unordered_map<std::string, CachedScale> cache;

        if (!std::filesystem::exists(path)) {
            return 0.001f;
        }

        const auto key = path.string();
        const auto write_time = std::filesystem::last_write_time(path);
        if (auto it = cache.find(key); it != cache.end() && it->second.write_time == write_time) {
            return it->second.scale;
        }

        glm::vec3 min_v { std::numeric_limits<float>::max() };
        glm::vec3 max_v { std::numeric_limits<float>::lowest() };
        size_t vertex_count = 0;
        std::ifstream file(path);
        for (std::string line; std::getline(file, line);) {
            if (!line.starts_with("v ")) {
                continue;
            }
            std::stringstream ss(line.substr(2));
            glm::vec3 p {};
            if (!(ss >> p.x >> p.y >> p.z)) {
                continue;
            }
            min_v = glm::min(min_v, p);
            max_v = glm::max(max_v, p);
            ++vertex_count;
            if (vertex_count >= 4096) {
                break;
            }
        }

        float scale = 0.001f;
        if (vertex_count > 0) {
            const auto size = max_v - min_v;
            const auto largest_axis = std::max({ size.x, size.y, size.z });
            scale = largest_axis > 10.0f ? 0.001f : 1.0f;
        }

        cache[key] = { write_time, scale };
        dbg(std::format("RoadbookVR: RBR dump mesh scale {} for {}", scale, path.string()));
        return scale;
    }

    std::optional<avatar_rig::LivePose> live_passenger_pose()
    {
        if (!g::cfg.roadbook_vr.avatar_rig_live_passenger_enabled || !g::cfg.passenger_vr.enabled) {
            return std::nullopt;
        }

        const auto state = passenger_vr::latest_pose_state();
        if (state.age_ms < 0 || state.age_ms > g::cfg.roadbook_vr.avatar_rig_live_pose_max_age_ms || !state.head.valid) {
            return std::nullopt;
        }

        avatar_rig::LivePose pose;
        pose.active = true;
        pose.hand_scale = g::cfg.roadbook_vr.avatar_rig_hand_scale;
        pose.head_rotation_scale = g::cfg.roadbook_vr.avatar_rig_head_rotation_scale;
        pose.head = { state.head.valid, state.head.position, state.head.rotation_degrees };
        pose.left_hand = { state.left_hand.valid, state.left_hand.position, state.left_hand.rotation_degrees };
        pose.right_hand = { state.right_hand.valid, state.right_hand.position, state.right_hand.rotation_degrees };
        return pose;
    }

    std::optional<avatar_rig::LivePose> live_driver_pose(VRInterface* vr, const M4& mesh_model, const M4& orientation_model)
    {
        driver_avatar_tracking::Settings settings;
        settings.controller_profile = g::cfg.roadbook_vr.avatar_controller_profile;
        settings.max_pose_age_ms = g::cfg.roadbook_vr.avatar_rig_live_pose_max_age_ms;
        settings.head_rotation_scale = g::cfg.roadbook_vr.avatar_rig_head_rotation_scale;
        settings.hand_rotation_scale = g::cfg.roadbook_vr.avatar_rig_hand_rotation_scale;
        driver_tracker.set_settings(settings);

        driver_avatar_tracking::FrameInput input;
        input.cockpit_camera_ready = rbr::is_using_internal_camera();
        input.hmd = {
            true,
            glm::inverse(vr->get_pose(LeftEye)),
            0,
            driver_avatar_tracking::PoseSource::None,
        };
        input.left_controller = {
            left_controller.valid,
            left_controller.device_to_absolute,
            left_controller.valid ? 0 : -1,
            driver_avatar_tracking::PoseSource::OpenVrController,
        };
        input.right_controller = {
            right_controller.valid,
            right_controller.device_to_absolute,
            right_controller.valid ? 0 : -1,
            driver_avatar_tracking::PoseSource::OpenVrController,
        };

        if (g::cfg.roadbook_vr.avatar_live_pose_swap_hands) {
            std::swap(input.left_controller, input.right_controller);
        }

        const auto result = driver_tracker.update(input, mesh_model, orientation_model);
        if (!result.active) {
            return std::nullopt;
        }
        return result.live_pose;
    }

    void render_avatar(IDirect3DDevice9* dev, VRInterface* vr, RenderTarget target, bool passenger_avatar, bool self_view = false)
    {
        const auto root = avatar_root(passenger_avatar);
        auto* suit = passenger_avatar ? passenger_suit_texture : driver_suit_texture;

        if (g::cfg.roadbook_vr.avatar_mesh_enabled) {
            const auto root_path = std::filesystem::path(g::cfg.roadbook_vr.rbr_root);
            const auto source = lower_copy(g::cfg.roadbook_vr.avatar_mesh_source);
            std::filesystem::path mesh_path;
            bool using_rbr_dump_mesh = false;
            if (source == "rbrdump") {
                const auto dump_dir = root_path / g::cfg.roadbook_vr.avatar_dump_directory;
                mesh_path = dump_dir / (passenger_avatar ? "codriver_local.obj" : "driver_local.obj");
                using_rbr_dump_mesh = std::filesystem::exists(mesh_path);
                if (!using_rbr_dump_mesh) {
                    mesh_path = root_path / g::cfg.roadbook_vr.avatar_mesh_path;
                }
            } else {
                mesh_path = root_path / g::cfg.roadbook_vr.avatar_mesh_path;
            }
            const auto mesh_scale = using_rbr_dump_mesh ? rbr_dump_mesh_scale(mesh_path) : g::cfg.roadbook_vr.avatar_mesh_scale;
            const auto rbr_dump_forward = using_rbr_dump_mesh
                ? glm::rotate(glm::identity<M4>(), glm::radians(180.0f), glm::vec3(0, 1, 0))
                : glm::identity<M4>();
            const auto mesh_model = root
                * rbr_dump_forward
                * glm::scale(glm::identity<M4>(), glm::vec3 { mesh_scale });
            if (using_rbr_dump_mesh && g::cfg.roadbook_vr.avatar_rig_enabled) {
                const auto rig_profile = root_path / g::cfg.roadbook_vr.avatar_rig_profile;
                const auto passenger_pose = passenger_avatar ? live_passenger_pose() : std::optional<avatar_rig::LivePose> {};
                const auto orientation_model = root * rbr_dump_forward;
                const auto driver_pose = !passenger_avatar ? live_driver_pose(vr, mesh_model, orientation_model) : std::optional<avatar_rig::LivePose> {};
                const auto* live_pose = passenger_pose ? &*passenger_pose : (driver_pose ? &*driver_pose : nullptr);
                const auto filter = self_view ? avatar_rig::RenderFilter::LimbsOnly : avatar_rig::RenderFilter::Full;
                if (avatar_rig::render(dev, vr, target, mesh_path, rig_profile, mesh_model, suit, helmet_texture, face_texture, hand_texture, suit, live_pose, filter)) {
                    return;
                }
            }
            if (avatar_mesh::render(dev, vr, target, mesh_path, mesh_model, suit, helmet_texture, face_texture, hand_texture, suit)) {
                return;
            }
        }

        render_textured_quad(dev, vr, suit, target, leg_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { -0.060f, -0.350f, -0.010f }) * glm::rotate(glm::identity<M4>(), glm::radians(-7.0f), glm::vec3(0, 0, 1)));
        render_textured_quad(dev, vr, suit, target, leg_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { 0.060f, -0.350f, -0.010f }) * glm::rotate(glm::identity<M4>(), glm::radians(7.0f), glm::vec3(0, 0, 1)));
        render_textured_quad(dev, vr, suit, target, torso_quad, root);
        render_textured_quad(dev, vr, suit_accent_texture, target, chest_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { 0.0f, 0.025f, 0.014f }));
        render_textured_quad(dev, vr, suit_accent_texture, target, shoulder_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { -0.090f, 0.145f, 0.016f }) * glm::rotate(glm::identity<M4>(), glm::radians(-12.0f), glm::vec3(0, 0, 1)));
        render_textured_quad(dev, vr, suit_accent_texture, target, shoulder_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { 0.090f, 0.145f, 0.016f }) * glm::rotate(glm::identity<M4>(), glm::radians(12.0f), glm::vec3(0, 0, 1)));
        render_textured_quad(dev, vr, face_texture, target, neck_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { 0.0f, 0.205f, 0.010f }));

        render_textured_quad(dev, vr, suit, target, limb_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { -0.165f, -0.020f, 0.012f }) * glm::rotate(glm::identity<M4>(), glm::radians(-16.0f), glm::vec3(0, 0, 1)));
        render_textured_quad(dev, vr, suit, target, limb_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { 0.165f, -0.020f, 0.012f }) * glm::rotate(glm::identity<M4>(), glm::radians(16.0f), glm::vec3(0, 0, 1)));

        render_textured_quad(dev, vr, helmet_texture, target, head_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { 0.0f, 0.315f, 0.022f }));
        render_textured_quad(dev, vr, face_texture, target, face_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { 0.0f, 0.292f, 0.030f }));
        render_textured_quad(dev, vr, visor_texture, target, visor_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { 0.0f, 0.340f, 0.035f }));

        if (g::cfg.roadbook_vr.avatar_hands_visible) {
            render_textured_quad(dev, vr, hand_texture, target, palm_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { -0.190f, -0.155f, 0.034f }) * glm::rotate(glm::identity<M4>(), glm::radians(-8.0f), glm::vec3(0, 0, 1)));
            render_textured_quad(dev, vr, hand_texture, target, palm_quad, root * glm::translate(glm::identity<M4>(), glm::vec3 { 0.190f, -0.155f, 0.034f }) * glm::rotate(glm::identity<M4>(), glm::radians(8.0f), glm::vec3(0, 0, 1)));
        }
    }

    void render_avatar_target(IDirect3DDevice9* dev, VRInterface* vr, RenderTarget target, bool passenger_avatar, bool self_view = false)
    {
        if (!vr->prepare_vr_rendering(dev, target, false)) {
            return;
        }
        if (self_view) {
            if (g::cfg.roadbook_vr.avatar_enabled && g::cfg.roadbook_vr.avatar_mesh_enabled && g::cfg.roadbook_vr.avatar_rig_enabled) {
                render_avatar(dev, vr, target, passenger_avatar, self_view);
            } else {
                avatar_hands::render_driver_self(dev, vr, target);
            }
            vr->finish_vr_rendering(dev, target);
            return;
        }
        render_avatar(dev, vr, target, passenger_avatar, self_view);
        if (!passenger_avatar && target == PassengerMono && !(g::cfg.roadbook_vr.avatar_mesh_enabled && g::cfg.roadbook_vr.avatar_rig_enabled)) {
            avatar_hands::render_driver_to_passenger(dev, vr, target);
        }
        vr->finish_vr_rendering(dev, target);
    }

    bool render_reused_avatar_target(IDirect3DDevice9* dev, VRInterface* vr, RenderTarget target, bool passenger_avatar)
    {
        if (!g::cfg.roadbook_vr.avatar_replay_enabled) {
            return false;
        }
        const auto mode = [&] {
            auto value = g::cfg.roadbook_vr.avatar_reuse_mode;
            std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }();
        if (mode != "rbrcapturethenfallback" && mode != "rbrcapture" && mode != "probeandcapture") {
            return false;
        }
        if (!vr->prepare_vr_rendering(dev, target, false)) {
            return false;
        }

        const auto previous_target = g::vr_render_target;
        const auto previous_multiview = g::cfg.multiview;
        g::vr_render_target = target;
        if (target == PassengerMono) {
            g::cfg.multiview = false;
        }

        const bool rendered = passenger_avatar ? asset_probe::render_passenger_avatar(dev) : asset_probe::render_driver_avatar(dev);

        g::cfg.multiview = previous_multiview;
        g::vr_render_target = previous_target;
        vr->finish_vr_rendering(dev, target);
        return rendered;
    }

    void render_avatar_with_fallback(IDirect3DDevice9* dev, VRInterface* vr, RenderTarget target, bool passenger_avatar)
    {
        if (render_reused_avatar_target(dev, vr, target, passenger_avatar)) {
            return;
        }
        if (g::cfg.roadbook_vr.avatar_fallback_enabled) {
            render_avatar_target(dev, vr, target, passenger_avatar);
        }
    }

    void render_avatars(IDirect3DDevice9* dev, VRInterface* vr)
    {
        if (!g::cfg.roadbook_vr.avatar_enabled && !g::cfg.roadbook_vr.player_hands_enabled) {
            return;
        }
        const bool avatar_assets_ready = !g::cfg.roadbook_vr.avatar_enabled || (ensure_texture(dev) && create_or_update_quads(dev));
        const auto mode = rbr::get_game_mode();
        if ((mode != rbr::GameMode::Driving && mode != rbr::GameMode::Replay) || !rbr::is_using_cockpit_camera()) {
            return;
        }

        if (g::cfg.roadbook_vr.avatar_enabled && avatar_assets_ready && g::cfg.roadbook_vr.avatar_passenger_visible) {
            render_avatar_with_fallback(dev, vr, LeftEye, true);
            if (!dx::multiview_rendering_enabled()) {
                render_avatar_with_fallback(dev, vr, RightEye, true);
            }
        }

        if (g::cfg.roadbook_vr.avatar_driver_visible && g::cfg.roadbook_vr.player_hands_enabled) {
            render_avatar_target(dev, vr, LeftEye, false, true);
            if (!dx::multiview_rendering_enabled()) {
                render_avatar_target(dev, vr, RightEye, false, true);
            }
        }

        if (g::cfg.roadbook_vr.avatar_enabled && avatar_assets_ready && g::cfg.roadbook_vr.avatar_driver_visible && g::cfg.passenger_vr.enabled) {
            render_avatar_with_fallback(dev, vr, PassengerMono, false);
        }
    }

    void render_target(IDirect3DDevice9* dev, VRInterface* vr, RenderTarget target)
    {
        if (!vr->prepare_vr_rendering(dev, target, false)) {
            return;
        }
        const auto model = panel_model_matrix();
        render_textured_quad(dev, vr, paper_shadow_texture, target, paper_shadow_quad, model * glm::translate(glm::identity<M4>(), glm::vec3 { 0.012f, -0.012f, -0.018f }));
        render_textured_quad(dev, vr, cover_texture, target, cover_quad, model * glm::translate(glm::identity<M4>(), glm::vec3 { 0.0f, 0.0f, -0.014f }));
        render_textured_quad(dev, vr, panel_texture, target, panel_quad, model * glm::translate(glm::identity<M4>(), glm::vec3 { 0.0f, 0.0f, 0.0f }));

        if (page_turn_active && update_turning_page_quad(dev, page_turn_progress())) {
            render_textured_quad(dev, vr, turning_page_texture, target, turning_page_quad, model * glm::translate(glm::identity<M4>(), glm::vec3 { 0.0f, 0.0f, 0.010f }));
        }

        if (target == PassengerMono) {
            render_passenger_hands(dev, vr, target, model);
        }
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

        avatar_hands::update_from_controller_pose(
            left_controller.device_to_absolute,
            left_controller.valid,
            left_controller.packet,
            right_controller.device_to_absolute,
            right_controller.valid,
            right_controller.packet);
    }

    void render(IDirect3DDevice9* dev, VRInterface* vr)
    {
        if (!rbr::is_rendering_3d()) {
            return;
        }

        render_avatars(dev, vr);

        if (!g::cfg.roadbook_vr.enabled) {
            return;
        }

        load_notes_if_needed();
        process_input();
        update_page_turn_state();
        if (visible && ensure_texture(dev) && create_or_update_quads(dev)) {
            redraw_page_texture(dev, panel_texture, current_page, texture_dirty, panel_texture_page);
            if (page_turn_active) {
                redraw_page_texture(dev, turning_page_texture, page_turn_from, turning_texture_dirty, turning_texture_page);
            }

            if (g::cfg.roadbook_vr.driver_visible) {
                render_target(dev, vr, LeftEye);
                if (!dx::multiview_rendering_enabled()) {
                    render_target(dev, vr, RightEye);
                }
            }

            if (g::cfg.roadbook_vr.passenger_visible && g::cfg.passenger_vr.enabled) {
                render_target(dev, vr, PassengerMono);
            }
        }
    }

    std::string debug_status()
    {
        auto status = std::format("RoadbookVR: {}, page {}, visible {}, {}", last_status, current_page + 1, visible ? "yes" : "no", last_avatar_seat_status);
        if (g::cfg.roadbook_vr.player_hands_debug) {
            const auto left_age = avatar_hands::age_ms(avatar_hands::Side::Left);
            const auto right_age = avatar_hands::age_ms(avatar_hands::Side::Right);
            status += std::format(
                ", hands L {}, R {}",
                left_age ? std::format("{}ms", *left_age) : "invalid",
                right_age ? std::format("{}ms", *right_age) : "invalid");
        }
        return status;
    }
}
