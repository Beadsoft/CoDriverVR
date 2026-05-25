#include "AvatarRig.hpp"

#include "AssetProbe.hpp"
#include "Dx.hpp"
#include "Globals.hpp"
#include "Vertex.hpp"
#include "VR.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtc/matrix_transform.hpp>
#include <gtx/norm.hpp>

#define TOML_HEADER_ONLY 1
#include <toml.hpp>

namespace {
    enum class RegionId : size_t {
        Hidden,
        TorsoFixed,
        Head,
        Helmet,
        Face,
        LeftUpperArm,
        LeftForearm,
        LeftHand,
        RightUpperArm,
        RightForearm,
        RightHand,
        Count,
    };

    struct ObjFaceVertex {
        int position = -1;
        int texcoord = -1;
    };

    struct Bounds {
        glm::vec3 min { -100000.0f, -100000.0f, -100000.0f };
        glm::vec3 max { 100000.0f, 100000.0f, 100000.0f };
    };

    struct RegionRule {
        RegionId id = RegionId::TorsoFixed;
        std::string name;
        std::vector<std::string> material_contains;
        Bounds bounds;
        glm::vec3 pivot {};
        glm::vec3 rotation_degrees {};
        bool enabled = true;
    };

    struct RigPart {
        RegionId id = RegionId::TorsoFixed;
        std::string region_name;
        std::string material;
        std::vector<Vertex> vertices;
        IDirect3DVertexBuffer9* vertex_buffer = nullptr;
    };

    struct RigMesh {
        std::filesystem::path obj_path;
        std::filesystem::path profile_path;
        std::filesystem::file_time_type obj_write_time {};
        std::filesystem::file_time_type profile_write_time {};
        std::vector<RigPart> parts;
        std::array<glm::vec3, static_cast<size_t>(RegionId::Count)> pivots {};
        std::array<glm::vec3, static_cast<size_t>(RegionId::Count)> rotations {};
        bool single_texture_crew = false;
        bool driver_side_mesh = false;
        bool debug_regions = false;
        bool config_debug_regions = false;
        bool load_attempted = false;
    };

    struct TextureBinding {
        IDirect3DBaseTexture9* texture = nullptr;
        std::string name = "null";
        bool fallback = false;
    };

    RigMesh rig;
    std::vector<avatar_rig::DiagnosticPart> last_diagnostic_parts;
    std::array<IDirect3DTexture9*, static_cast<size_t>(RegionId::Count)> mask_textures {};
    constexpr D3DMATRIX identity_matrix = d3d_from_m4(glm::identity<glm::mat4x4>());

    std::string trim(std::string s)
    {
        const auto not_space = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
        return s;
    }

    std::string lower_copy(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    std::string region_name(RegionId id)
    {
        switch (id) {
            case RegionId::Hidden: return "hidden";
            case RegionId::TorsoFixed: return "torso_fixed";
            case RegionId::Head: return "head";
            case RegionId::Helmet: return "helmet";
            case RegionId::Face: return "face";
            case RegionId::LeftUpperArm: return "left_upper_arm";
            case RegionId::LeftForearm: return "left_forearm";
            case RegionId::LeftHand: return "left_hand";
            case RegionId::RightUpperArm: return "right_upper_arm";
            case RegionId::RightForearm: return "right_forearm";
            case RegionId::RightHand: return "right_hand";
            case RegionId::Count: break;
        }
        return "torso_fixed";
    }

    RegionId region_id_from_name(const std::string& name)
    {
        const auto n = lower_copy(name);
        if (n == "hidden") return RegionId::Hidden;
        if (n == "torso_fixed" || n == "torso") return RegionId::TorsoFixed;
        if (n == "head") return RegionId::Head;
        if (n == "helmet") return RegionId::Helmet;
        if (n == "face") return RegionId::Face;
        if (n == "left_upper_arm") return RegionId::LeftUpperArm;
        if (n == "left_forearm") return RegionId::LeftForearm;
        if (n == "left_hand") return RegionId::LeftHand;
        if (n == "right_upper_arm") return RegionId::RightUpperArm;
        if (n == "right_forearm") return RegionId::RightForearm;
        if (n == "right_hand") return RegionId::RightHand;
        return RegionId::TorsoFixed;
    }

    std::string material_name_for_obj(const std::string& name)
    {
        auto value = name;
        for (auto& c : value) {
            if (!std::isalnum(static_cast<unsigned char>(c))) {
                c = '_';
            }
        }
        return value.empty() ? "material" : value;
    }

    bool contains_material(const RegionRule& rule, const std::string& material)
    {
        if (rule.material_contains.empty()) {
            return true;
        }
        const auto m = lower_copy(material);
        return std::ranges::any_of(rule.material_contains, [&](const auto& wanted) {
            return m.contains(lower_copy(wanted));
        });
    }

    bool in_bounds(const Bounds& bounds, glm::vec3 p)
    {
        return p.x >= bounds.min.x && p.x <= bounds.max.x
            && p.y >= bounds.min.y && p.y <= bounds.max.y
            && p.z >= bounds.min.z && p.z <= bounds.max.z;
    }

    bool is_single_texture_crew_material(const std::string& material)
    {
        const auto m = lower_copy(material);
        return m.contains("drivers_dds")
            || m.contains("_drivers")
            || m.contains("drivers.dds");
    }

    void set_pose(RegionId id, glm::vec3 pivot, glm::vec3 rotation_degrees)
    {
        const auto idx = static_cast<size_t>(id);
        rig.pivots[idx] = pivot;
        rig.rotations[idx] = rotation_degrees;
    }

    void apply_single_texture_crew_pose()
    {
        // Fiesta Rally2-style crew dumps are already metre-scale and use one
        // atlas, so the editable pose is spatial rather than material-based.
        const float side = rig.driver_side_mesh ? 1.0f : -1.0f;
        set_pose(RegionId::Head, { 0.34f * side, 0.86f, 0.16f }, { -8.0f, -8.0f * side, 0.0f });
        set_pose(RegionId::Helmet, { 0.34f * side, 0.86f, 0.16f }, { -8.0f, -8.0f * side, 0.0f });
        set_pose(RegionId::Face, { 0.34f * side, 0.86f, 0.16f }, { -8.0f, -8.0f * side, 0.0f });
        set_pose(RegionId::LeftUpperArm, { 0.20f * side, 0.72f, 0.10f }, { -8.0f, -18.0f * side, -8.0f * side });
        set_pose(RegionId::LeftForearm, { 0.16f * side, 0.58f, 0.02f }, { -24.0f, -8.0f * side, -14.0f * side });
        set_pose(RegionId::LeftHand, { 0.12f * side, 0.44f, -0.05f }, { -28.0f, -4.0f * side, -18.0f * side });
        set_pose(RegionId::RightUpperArm, { 0.50f * side, 0.72f, 0.10f }, { -8.0f, 18.0f * side, 8.0f * side });
        set_pose(RegionId::RightForearm, { 0.50f * side, 0.58f, 0.02f }, { -28.0f, 8.0f * side, 14.0f * side });
        set_pose(RegionId::RightHand, { 0.50f * side, 0.44f, -0.05f }, { -32.0f, 4.0f * side, 18.0f * side });
    }

    std::optional<RegionRule> classify_single_texture_crew_region(const std::string& material, glm::vec3 centroid)
    {
        if (!is_single_texture_crew_material(material)) {
            return std::nullopt;
        }

        rig.single_texture_crew = true;
        RegionRule rule;
        rule.enabled = true;

        // The current combined Fiesta crew draw contains both occupants. Keep
        // the side matching the OBJ being rendered and hide the duplicate.
        if ((!rig.driver_side_mesh && centroid.x > 0.05f) || (rig.driver_side_mesh && centroid.x < -0.05f)) {
            rule.id = RegionId::Hidden;
            rule.name = region_name(rule.id);
            return rule;
        }

        const float center_x = rig.driver_side_mesh ? 0.33f : -0.33f;
        if (centroid.y > 0.84f) {
            rule.id = RegionId::Head;
        } else if (centroid.y > 0.24f && centroid.y < 0.80f && centroid.z < 0.30f && std::abs(centroid.x - center_x) > 0.08f) {
            const bool outer_side = centroid.x < center_x;
            if (centroid.y < 0.48f || centroid.z < -0.18f) {
                rule.id = outer_side ? RegionId::LeftHand : RegionId::RightHand;
            } else if (centroid.y < 0.65f) {
                rule.id = outer_side ? RegionId::LeftForearm : RegionId::RightForearm;
            } else {
                rule.id = outer_side ? RegionId::LeftUpperArm : RegionId::RightUpperArm;
            }
        } else {
            rule.id = RegionId::TorsoFixed;
        }

        rule.name = region_name(rule.id);
        return rule;
    }

    std::optional<std::pair<int, int>> parse_face_ref(const std::string& ref)
    {
        std::array<std::string, 3> fields {};
        std::stringstream ss(ref);
        for (size_t i = 0; i < fields.size() && std::getline(ss, fields[i], '/'); ++i) {
        }
        if (fields[0].empty()) {
            return std::nullopt;
        }

        try {
            const int vi = std::stoi(fields[0]) - 1;
            const int ti = fields[1].empty() ? -1 : std::stoi(fields[1]) - 1;
            return std::pair { vi, ti };
        } catch (...) {
            return std::nullopt;
        }
    }

    float read_float(toml::array* array, size_t idx, float fallback)
    {
        if (!array || idx >= array->size()) {
            return fallback;
        }
        const auto node = array->get(idx);
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
    }

    glm::vec3 read_vec3(toml::node_view<toml::node> node, glm::vec3 fallback)
    {
        auto* array = node.as_array();
        if (!array || array->size() < 3) {
            return fallback;
        }
        return {
            read_float(array, 0, fallback.x),
            read_float(array, 1, fallback.y),
            read_float(array, 2, fallback.z),
        };
    }

    std::vector<std::string> read_string_array(toml::node_view<toml::node> node)
    {
        std::vector<std::string> values;
        if (auto* array = node.as_array()) {
            array->for_each([&](toml::node& item) {
                if (auto value = item.value<std::string>()) {
                    values.push_back(*value);
                }
            });
        }
        return values;
    }

    const char* default_profile_text()
    {
        return R"(# Generated by openRBRVR. Values are in codriver_local.obj dump-local coordinates.
pose = "codriverReadingBook"
debugRegions = false

[regions.hidden]
enabled = false
materialContains = []
min = [0, 0, 0]
max = [0, 0, 0]

[regions.face]
materialContains = ["reid_face", "face"]
min = [-100, 780, -160]
max = [100, 1040, 150]
pivot = [0, 920, 55]
rotationDegrees = [-8, 8, 0]

[regions.helmet]
materialContains = ["incar_helmet", "helmet"]
min = [-140, 820, -180]
max = [140, 1180, 260]
pivot = [0, 920, 55]
rotationDegrees = [-8, 8, 0]

[regions.left_hand]
materialContains = ["gloves", "hand"]
min = [-520, 250, -820]
max = [-120, 680, -90]
pivot = [-260, 500, -410]
rotationDegrees = [-28, -4, -18]

[regions.left_forearm]
materialContains = ["incar_drv", "gloves"]
min = [-520, 260, -860]
max = [-120, 720, -90]
pivot = [-255, 600, -355]
rotationDegrees = [-24, -8, -14]

[regions.left_upper_arm]
materialContains = ["incar_drv"]
min = [-400, 520, -760]
max = [-95, 930, 80]
pivot = [-155, 800, -95]
rotationDegrees = [-8, -18, -8]

[regions.right_hand]
materialContains = ["gloves", "hand"]
min = [-80, 250, -820]
max = [260, 680, -90]
pivot = [130, 505, -390]
rotationDegrees = [-32, 4, 18]

[regions.right_forearm]
materialContains = ["incar_drv", "gloves"]
min = [-80, 260, -860]
max = [300, 720, -90]
pivot = [135, 600, -350]
rotationDegrees = [-28, 8, 14]

[regions.right_upper_arm]
materialContains = ["incar_drv"]
min = [85, 520, -760]
max = [330, 930, 100]
pivot = [145, 800, -90]
rotationDegrees = [-8, 18, 8]

[regions.head]
materialContains = ["head"]
min = [-120, 790, -180]
max = [120, 1120, 250]
pivot = [0, 920, 55]
rotationDegrees = [-8, 8, 0]

[regions.torso_fixed]
materialContains = []
min = [-10000, -10000, -10000]
max = [10000, 10000, 10000]
pivot = [0, 0, 0]
rotationDegrees = [0, 0, 0]
)";
    }

    bool ensure_profile_exists(const std::filesystem::path& profile_path)
    {
        if (std::filesystem::exists(profile_path)) {
            return true;
        }
        try {
            std::filesystem::create_directories(profile_path.parent_path());
            std::ofstream file(profile_path);
            file << default_profile_text();
            dbg(std::format("AvatarRig: wrote default profile {}", profile_path.string()));
            return file.good();
        } catch (...) {
            dbg(std::format("AvatarRig: failed to write default profile {}", profile_path.string()));
            return false;
        }
    }

    std::optional<std::vector<RegionRule>> load_profile(const std::filesystem::path& profile_path)
    {
        if (!ensure_profile_exists(profile_path)) {
            return std::nullopt;
        }

        toml::table parsed;
        try {
            parsed = toml::parse_file(profile_path.c_str());
        } catch (const toml::parse_error& e) {
            dbg(std::format("AvatarRig: failed to parse {}: {}", profile_path.string(), e.what()));
            return std::nullopt;
        }

        rig.config_debug_regions = g::cfg.roadbook_vr.avatar_rig_debug_regions;
        rig.debug_regions = parsed["debugRegions"].value_or(false) || rig.config_debug_regions;
        std::vector<RegionRule> rules;
        auto regions = parsed["regions"];
        if (!regions.is_table()) {
            dbg(std::format("AvatarRig: profile has no [regions]: {}", profile_path.string()));
            return std::nullopt;
        }

        regions.as_table()->for_each([&](const toml::key& key, toml::table& value) {
            RegionRule rule;
            rule.name = std::string(key.data(), key.length());
            rule.id = region_id_from_name(rule.name);
            rule.enabled = value["enabled"].value_or(true);
            rule.material_contains = read_string_array(value["materialContains"]);
            rule.bounds.min = read_vec3(value["min"], rule.bounds.min);
            rule.bounds.max = read_vec3(value["max"], rule.bounds.max);
            rule.pivot = read_vec3(value["pivot"], rule.pivot);
            rule.rotation_degrees = read_vec3(value["rotationDegrees"], rule.rotation_degrees);
            rules.push_back(rule);
            rig.pivots[static_cast<size_t>(rule.id)] = rule.pivot;
            rig.rotations[static_cast<size_t>(rule.id)] = rule.rotation_degrees;
        });

        const auto sort_key = [](const RegionRule& rule) {
            if (rule.id == RegionId::Hidden) return 0;
            if (rule.id == RegionId::Face || rule.id == RegionId::Helmet || rule.id == RegionId::Head) return 1;
            if (rule.id == RegionId::LeftHand || rule.id == RegionId::RightHand) return 2;
            if (rule.id == RegionId::LeftForearm || rule.id == RegionId::RightForearm) return 3;
            if (rule.id == RegionId::LeftUpperArm || rule.id == RegionId::RightUpperArm) return 4;
            return 9;
        };
        std::ranges::stable_sort(rules, [&](const auto& lhs, const auto& rhs) {
            return sort_key(lhs) < sort_key(rhs);
        });
        return rules;
    }

    RegionRule classify_region(const std::vector<RegionRule>& rules, const std::string& material, glm::vec3 centroid)
    {
        if (rig.driver_side_mesh && !is_single_texture_crew_material(material)) {
            RegionRule hidden;
            hidden.id = RegionId::Hidden;
            hidden.name = region_name(hidden.id);
            return hidden;
        }

        if (auto single_texture_rule = classify_single_texture_crew_region(material, centroid)) {
            return *single_texture_rule;
        }

        for (const auto& rule : rules) {
            if (!rule.enabled) {
                continue;
            }
            if (contains_material(rule, material) && in_bounds(rule.bounds, centroid)) {
                return rule;
            }
        }
        RegionRule fallback;
        fallback.id = RegionId::TorsoFixed;
        fallback.name = region_name(fallback.id);
        return fallback;
    }

    RigPart& get_part(std::vector<RigPart>& parts, RegionId id, const std::string& material)
    {
        const auto name = region_name(id);
        for (auto& part : parts) {
            if (part.id == id && part.material == material) {
                return part;
            }
        }
        auto& part = parts.emplace_back();
        part.id = id;
        part.region_name = name;
        part.material = material;
        return part;
    }

    void release_parts()
    {
        for (auto& part : rig.parts) {
            if (part.vertex_buffer) {
                part.vertex_buffer->Release();
                part.vertex_buffer = nullptr;
            }
        }
        rig.parts.clear();
    }

    void release_mask_textures()
    {
        for (auto*& texture : mask_textures) {
            if (texture) {
                texture->Release();
                texture = nullptr;
            }
        }
    }

    void write_debug_obj(const std::filesystem::path& obj_path)
    {
        const auto path = obj_path.parent_path() / "codriver_parts_debug.obj";
        try {
            std::ofstream file(path);
            if (!file.good()) {
                return;
            }
            file << "# openRBRVR codriver partial rig debug export\n";
            file << "o codriver_parts_debug\n";
            size_t index = 1;
            for (const auto& part : rig.parts) {
                if (part.id == RegionId::Hidden || part.vertices.empty()) {
                    continue;
                }
                const auto material = material_name_for_obj(part.region_name + "_" + part.material);
                file << "usemtl " << material << "\n";
                file << "g " << material << "\n";
                const auto first = index;
                for (const auto& v : part.vertices) {
                    file << std::format("v {:.8f} {:.8f} {:.8f}\n", v.x, v.y, v.z);
                    file << std::format("vt {:.8f} {:.8f}\n", v.u, v.v);
                    ++index;
                }
                for (size_t i = 0; i + 2 < part.vertices.size(); i += 3) {
                    const auto a = first + i;
                    file << std::format("f {}/{} {}/{} {}/{}\n", a, a, a + 1, a + 1, a + 2, a + 2);
                }
            }
            dbg(std::format("AvatarRig: wrote debug region OBJ {}", path.string()));
        } catch (...) {
            dbg("AvatarRig: failed to write debug region OBJ");
        }
    }

    bool load_rig(IDirect3DDevice9* dev, const std::filesystem::path& obj_path, const std::filesystem::path& profile_path)
    {
        release_parts();
        rig.load_attempted = true;
        rig.single_texture_crew = false;
        rig.driver_side_mesh = lower_copy(obj_path.filename().string()) == "driver_local.obj";

        const auto rules = load_profile(profile_path);
        if (!rules) {
            return false;
        }

        std::ifstream file(obj_path);
        if (!file.good()) {
            dbg(std::format("AvatarRig: OBJ not found: {}", obj_path.string()));
            return false;
        }

        std::vector<glm::vec3> positions;
        std::vector<glm::vec2> texcoords;
        std::string current_material = "body";

        for (std::string raw; std::getline(file, raw);) {
            const auto line = trim(raw);
            if (line.empty() || line.starts_with('#')) {
                continue;
            }

            std::stringstream ss(line);
            std::string op;
            ss >> op;

            if (op == "v") {
                glm::vec3 p {};
                ss >> p.x >> p.y >> p.z;
                positions.push_back(p);
            } else if (op == "vt") {
                glm::vec2 uv {};
                ss >> uv.x >> uv.y;
                texcoords.push_back(uv);
            } else if (op == "usemtl") {
                ss >> current_material;
            } else if (op == "f") {
                std::vector<ObjFaceVertex> refs;
                for (std::string ref; ss >> ref;) {
                    if (auto parsed = parse_face_ref(ref)) {
                        refs.push_back({ parsed->first, parsed->second });
                    }
                }
                if (refs.size() < 3) {
                    continue;
                }

                for (size_t i = 1; i + 1 < refs.size(); ++i) {
                    const std::array tri { refs[0], refs[i], refs[i + 1] };
                    glm::vec3 centroid {};
                    bool valid = true;
                    for (const auto& ref : tri) {
                        if (ref.position < 0 || static_cast<size_t>(ref.position) >= positions.size()) {
                            valid = false;
                            break;
                        }
                        centroid += positions[ref.position];
                    }
                    if (!valid) {
                        continue;
                    }
                    centroid /= 3.0f;

                    const auto rule = classify_region(*rules, current_material, centroid);
                    if (rule.id == RegionId::Hidden) {
                        continue;
                    }

                    auto& part = get_part(rig.parts, rule.id, current_material);
                    for (const auto& ref : tri) {
                        const auto p = positions[ref.position];
                        const auto uv = ref.texcoord >= 0 && static_cast<size_t>(ref.texcoord) < texcoords.size() ? texcoords[ref.texcoord] : glm::vec2 {};
                        part.vertices.push_back(Vertex { p.x, p.y, p.z, uv.x, 1.0f - uv.y });
                    }
                }
            }
        }

        std::erase_if(rig.parts, [](const RigPart& part) { return part.vertices.empty(); });
        if (rig.parts.empty()) {
            dbg(std::format("AvatarRig: no usable regions from {}", obj_path.string()));
            return false;
        }

        if (rig.single_texture_crew) {
            apply_single_texture_crew_pose();
            dbg("AvatarRig: using single-texture crew spatial split");
        }

        for (auto& part : rig.parts) {
            if (!create_vertex_buffer(dev, part.vertices.data(), part.vertices.size(), &part.vertex_buffer)) {
                release_parts();
                return false;
            }
        }

        for (RegionId id : { RegionId::Head, RegionId::Helmet, RegionId::Face, RegionId::LeftUpperArm, RegionId::LeftForearm, RegionId::LeftHand, RegionId::RightUpperArm, RegionId::RightForearm, RegionId::RightHand }) {
            const auto count = std::ranges::count_if(rig.parts, [&](const RigPart& part) { return part.id == id; });
            if (count == 0) {
                dbg(std::format("AvatarRig: region '{}' is empty", region_name(id)));
            }
        }

        rig.obj_path = obj_path;
        rig.profile_path = profile_path;
        rig.obj_write_time = std::filesystem::last_write_time(obj_path);
        rig.profile_write_time = std::filesystem::last_write_time(profile_path);
        if (rig.debug_regions) {
            write_debug_obj(obj_path);
        }

        dbg(std::format("AvatarRig: loaded {} rig parts from {}", rig.parts.size(), obj_path.string()));
        return true;
    }

    bool ensure_loaded(IDirect3DDevice9* dev, const std::filesystem::path& obj_path, const std::filesystem::path& profile_path)
    {
        if (!std::filesystem::exists(obj_path)) {
            if (!rig.load_attempted || rig.obj_path != obj_path) {
                rig.load_attempted = true;
                rig.obj_path = obj_path;
                dbg(std::format("AvatarRig: waiting for OBJ at {}", obj_path.string()));
            }
            return false;
        }
        ensure_profile_exists(profile_path);
        if (!std::filesystem::exists(profile_path)) {
            return false;
        }

        const auto obj_write_time = std::filesystem::last_write_time(obj_path);
        const auto profile_write_time = std::filesystem::last_write_time(profile_path);
        if (!rig.parts.empty()
            && rig.obj_path == obj_path
            && rig.profile_path == profile_path
            && rig.obj_write_time == obj_write_time
            && rig.profile_write_time == profile_write_time
            && rig.config_debug_regions == g::cfg.roadbook_vr.avatar_rig_debug_regions) {
            return true;
        }

        return load_rig(dev, obj_path, profile_path);
    }

    TextureBinding rbr_texture_for_material(
        const std::string& material,
        IDirect3DBaseTexture9* fallback_body,
        IDirect3DBaseTexture9* fallback_helmet,
        IDirect3DBaseTexture9* fallback_face,
        IDirect3DBaseTexture9* fallback_hands,
        IDirect3DBaseTexture9* fallback_shoes)
    {
        const auto m = lower_copy(material);
        const auto find = [](std::initializer_list<const char*> names) -> TextureBinding {
            for (const auto* name : names) {
                if (auto* texture = asset_probe::find_loaded_texture_by_suffix(name)) {
                    return { texture, name, false };
                }
            }
            return {};
        };

        TextureBinding selected {};
        if (is_single_texture_crew_material(material)) {
            selected = find({ "_drivers.dds", "-drivers.dds", "fiesta_rally2_drivers.dds" });
            if (!selected.texture && fallback_body) {
                selected = { fallback_body, "fallback_body", true };
                selected.texture->AddRef();
            }
        } else if (m.contains("helmet")) {
            selected = find({ "incar_helmet03.dds", "helmet_outside_01.dds" });
            if (!selected.texture && fallback_helmet) {
                selected = { fallback_helmet, "fallback_helmet", true };
                selected.texture->AddRef();
            }
        } else if (m.contains("face") || m.contains("head")) {
            selected = find({ "reid_face_04.dds", "eeros_face01.dds", "face_robert_reid_nohelmet_02.dds", "face_hd_richard_01.dds" });
            if (!selected.texture && fallback_face) {
                selected = { fallback_face, "fallback_face", true };
                selected.texture->AddRef();
            }
        } else if (m.contains("glove") || m.contains("hand")) {
            selected = find({ "gloves_01left.dds", "hands01.dds" });
            if (!selected.texture && fallback_hands) {
                selected = { fallback_hands, "fallback_hands", true };
                selected.texture->AddRef();
            }
        } else if (m.contains("shoe")) {
            selected = find({ "shoes01.dds", "ofdshoes01.dds" });
            if (!selected.texture && fallback_shoes) {
                selected = { fallback_shoes, "fallback_shoes", true };
                selected.texture->AddRef();
            }
        } else {
            selected = find({ "incar_drv_1_01.dds", "body_rallydriver_01.dds", "body-leftarm_rallydriver_01.dds" });
            if (!selected.texture && fallback_body) {
                selected = { fallback_body, "fallback_body", true };
                selected.texture->AddRef();
            }
        }
        return selected;
    }

    D3DCOLOR mask_color(RegionId id)
    {
        switch (id) {
            case RegionId::TorsoFixed: return D3DCOLOR_XRGB(40, 200, 80);
            case RegionId::Head: return D3DCOLOR_XRGB(255, 255, 80);
            case RegionId::Helmet: return D3DCOLOR_XRGB(80, 160, 255);
            case RegionId::Face: return D3DCOLOR_XRGB(255, 180, 120);
            case RegionId::LeftUpperArm: return D3DCOLOR_XRGB(255, 80, 80);
            case RegionId::LeftForearm: return D3DCOLOR_XRGB(255, 120, 80);
            case RegionId::LeftHand: return D3DCOLOR_XRGB(255, 80, 180);
            case RegionId::RightUpperArm: return D3DCOLOR_XRGB(160, 80, 255);
            case RegionId::RightForearm: return D3DCOLOR_XRGB(120, 80, 255);
            case RegionId::RightHand: return D3DCOLOR_XRGB(80, 255, 255);
            case RegionId::Hidden:
            case RegionId::Count: return D3DCOLOR_XRGB(255, 255, 255);
        }
        return D3DCOLOR_XRGB(255, 255, 255);
    }

    IDirect3DBaseTexture9* mask_texture_for_region(IDirect3DDevice9* dev, RegionId id)
    {
        const auto idx = static_cast<size_t>(id);
        if (!mask_textures[idx]) {
            IDirect3DTexture9* created = nullptr;
            if (dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &created, nullptr) != D3D_OK) {
                return nullptr;
            }
            D3DLOCKED_RECT locked {};
            if (created->LockRect(0, &locked, nullptr, 0) != D3D_OK) {
                created->Release();
                return nullptr;
            }
            *static_cast<uint32_t*>(locked.pBits) = mask_color(id);
            created->UnlockRect(0);
            mask_textures[idx] = created;
        }
        mask_textures[idx]->AddRef();
        return mask_textures[idx];
    }

    glm::vec3 clamped_delta(glm::vec3 delta, float max_length)
    {
        const auto length2 = glm::length2(delta);
        if (length2 > max_length * max_length) {
            return glm::normalize(delta) * max_length;
        }
        return delta;
    }

    glm::vec3 scaled_clamped_rotation(glm::vec3 degrees, glm::vec3 scale, glm::vec3 limits)
    {
        if (!std::isfinite(degrees.x)) degrees.x = 0.0f;
        if (!std::isfinite(degrees.y)) degrees.y = 0.0f;
        if (!std::isfinite(degrees.z)) degrees.z = 0.0f;
        return glm::clamp(glm::vec3 {
                              degrees.x * scale.x,
                              degrees.y * scale.y,
                              degrees.z * scale.z,
                          },
            -limits,
            limits);
    }

    const avatar_rig::LiveTrackedPart* live_hand_for_region(RegionId id, const avatar_rig::LivePose* live_pose)
    {
        if (!live_pose || !live_pose->active) {
            return nullptr;
        }
        switch (id) {
            case RegionId::LeftUpperArm:
            case RegionId::LeftForearm:
            case RegionId::LeftHand:
                return live_pose->left_hand.valid ? &live_pose->left_hand : nullptr;
            case RegionId::RightUpperArm:
            case RegionId::RightForearm:
            case RegionId::RightHand:
                return live_pose->right_hand.valid ? &live_pose->right_hand : nullptr;
            default:
                return nullptr;
        }
    }

    float hand_follow_weight(RegionId id)
    {
        switch (id) {
            case RegionId::LeftHand:
            case RegionId::RightHand:
                return 1.0f;
            case RegionId::LeftForearm:
            case RegionId::RightForearm:
                return 0.58f;
            case RegionId::LeftUpperArm:
            case RegionId::RightUpperArm:
                return 0.24f;
            default:
                return 0.0f;
        }
    }

    M4 region_transform(RegionId id, const avatar_rig::LivePose* live_pose)
    {
        if (id == RegionId::TorsoFixed) {
            return glm::identity<M4>();
        }

        const auto idx = static_cast<size_t>(id);
        const auto pivot = rig.pivots[idx];
        auto rotation = rig.rotations[idx];
        glm::vec3 translation {};

        if (live_pose && live_pose->active && live_pose->head.valid && (id == RegionId::Head || id == RegionId::Helmet || id == RegionId::Face)) {
            rotation.x += live_pose->head.rotation_degrees.y * live_pose->head_rotation_scale.y;
            rotation.y += live_pose->head.rotation_degrees.x * live_pose->head_rotation_scale.x;
            rotation.z += live_pose->head.rotation_degrees.z * live_pose->head_rotation_scale.z;
        }

        if (const auto* hand = live_hand_for_region(id, live_pose)) {
            const auto head_pivot = rig.pivots[static_cast<size_t>(RegionId::Head)];
            const auto target = live_pose->hand_positions_model_local
                ? hand->position
                : head_pivot + hand->position * live_pose->hand_scale;
            const auto hand_id = (id == RegionId::LeftUpperArm || id == RegionId::LeftForearm || id == RegionId::LeftHand)
                ? RegionId::LeftHand
                : RegionId::RightHand;
            const auto default_hand_pivot = rig.pivots[static_cast<size_t>(hand_id)];
            translation = clamped_delta(target - default_hand_pivot, 0.65f) * hand_follow_weight(id);
            if (id == RegionId::LeftHand || id == RegionId::RightHand) {
                const auto hand_rotation = scaled_clamped_rotation(
                    hand->rotation_degrees,
                    live_pose->hand_rotation_scale,
                    glm::vec3 { 90.0f, 90.0f, 90.0f });
                rotation.x += hand_rotation.y;
                rotation.y += hand_rotation.x;
                rotation.z += hand_rotation.z;
            }
        }

        return glm::translate(glm::identity<M4>(), translation)
            * glm::translate(glm::identity<M4>(), pivot)
            * glm::rotate(glm::identity<M4>(), glm::radians(rotation.x), glm::vec3(1, 0, 0))
            * glm::rotate(glm::identity<M4>(), glm::radians(rotation.y), glm::vec3(0, 1, 0))
            * glm::rotate(glm::identity<M4>(), glm::radians(rotation.z), glm::vec3(0, 0, 1))
            * glm::translate(glm::identity<M4>(), -pivot);
    }

    void render_part(
        IDirect3DDevice9* dev,
        VRInterface* vr,
        RenderTarget target,
        const RigPart& part,
        const M4& model,
        IDirect3DBaseTexture9* texture,
        const avatar_rig::LivePose* live_pose)
    {
        if (!part.vertex_buffer || part.vertices.empty()) {
            return;
        }

        const auto part_model = model * region_transform(part.id, live_pose);
        const auto& projection_l = vr->get_projection(target);
        const auto& eye_pos_l = vr->get_eye_pos(target);
        const auto& pose_l = vr->get_pose(target);
        const D3DMATRIX mvp_l = d3d_from_m4(projection_l * eye_pos_l * pose_l * g::flip_z_matrix * part_model);
        D3DMATRIX mvp_r = {};
        const bool use_multiview = dx::multiview_rendering_enabled() && (target == LeftEye || target == FocusLeft);
        if (use_multiview) {
            const auto right = render_target_counterpart(target);
            mvp_r = d3d_from_m4(vr->get_projection(right) * vr->get_eye_pos(right) * vr->get_pose(right) * g::flip_z_matrix * part_model);
        }

        IDirect3DVertexShader9* old_vs = nullptr;
        IDirect3DPixelShader9* old_ps = nullptr;
        IDirect3DBaseTexture9* old_texture = nullptr;
        IDirect3DVertexBuffer9* old_stream = nullptr;
        UINT old_stream_offset = 0;
        UINT old_stream_stride = 0;
        DWORD old_fvf = 0;
        D3DMATRIX old_proj_l {};
        D3DMATRIX old_view_l {};
        D3DMATRIX old_proj_r {};
        D3DMATRIX old_view_r {};
        D3DMATRIX old_world {};
        DWORD old_alpha_blend = 0;
        DWORD old_z_enable = 0;
        DWORD old_cull_mode = 0;

        dev->GetVertexShader(&old_vs);
        dev->GetPixelShader(&old_ps);
        dev->GetTexture(0, &old_texture);
        dev->GetStreamSource(0, &old_stream, &old_stream_offset, &old_stream_stride);
        dev->GetFVF(&old_fvf);
        dev->GetTransform(D3DTS_PROJECTION_LEFT, &old_proj_l);
        dev->GetTransform(D3DTS_VIEW_LEFT, &old_view_l);
        dev->GetTransform(D3DTS_WORLD, &old_world);
        dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &old_alpha_blend);
        dev->GetRenderState(D3DRS_ZENABLE, &old_z_enable);
        dev->GetRenderState(D3DRS_CULLMODE, &old_cull_mode);
        if (use_multiview) {
            dev->GetTransform(D3DTS_PROJECTION_RIGHT, &old_proj_r);
            dev->GetTransform(D3DTS_VIEW_RIGHT, &old_view_r);
        }

        dev->SetVertexShader(nullptr);
        dev->SetPixelShader(nullptr);
        dev->SetTransform(D3DTS_PROJECTION_LEFT, &mvp_l);
        dev->SetTransform(D3DTS_VIEW_LEFT, &identity_matrix);
        if (use_multiview) {
            dev->SetTransform(D3DTS_PROJECTION_RIGHT, &mvp_r);
            dev->SetTransform(D3DTS_VIEW_RIGHT, &identity_matrix);
        }
        dev->SetTransform(D3DTS_WORLD, &identity_matrix);
        dev->SetRenderState(D3DRS_ZENABLE, TRUE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        dev->SetTexture(0, texture);
        dev->SetStreamSource(0, part.vertex_buffer, 0, sizeof(Vertex));
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);

        dev->BeginScene();
        dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, static_cast<UINT>(part.vertices.size() / 3));
        dev->EndScene();

        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, old_alpha_blend);
        dev->SetRenderState(D3DRS_ZENABLE, old_z_enable);
        dev->SetRenderState(D3DRS_CULLMODE, old_cull_mode);
        dev->SetTexture(0, old_texture);
        dev->SetStreamSource(0, old_stream, old_stream_offset, old_stream_stride);
        if (old_fvf != 0) {
            dev->SetFVF(old_fvf);
        }
        dev->SetVertexShader(old_vs);
        dev->SetPixelShader(old_ps);
        dev->SetTransform(D3DTS_PROJECTION_LEFT, &old_proj_l);
        dev->SetTransform(D3DTS_VIEW_LEFT, &old_view_l);
        if (use_multiview) {
            dev->SetTransform(D3DTS_PROJECTION_RIGHT, &old_proj_r);
            dev->SetTransform(D3DTS_VIEW_RIGHT, &old_view_r);
        }
        dev->SetTransform(D3DTS_WORLD, &old_world);

        if (old_vs) old_vs->Release();
        if (old_ps) old_ps->Release();
        if (old_texture) old_texture->Release();
        if (old_stream) old_stream->Release();
    }

    bool render_part_allowed(RegionId id, avatar_rig::RenderFilter filter)
    {
        if (filter == avatar_rig::RenderFilter::Full) {
            return true;
        }
        switch (id) {
            case RegionId::LeftUpperArm:
            case RegionId::LeftForearm:
            case RegionId::LeftHand:
            case RegionId::RightUpperArm:
            case RegionId::RightForearm:
            case RegionId::RightHand:
                return true;
            default:
                return false;
        }
    }
}

namespace avatar_rig {
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
        const LivePose* live_pose,
        RenderFilter filter)
    {
        if (!ensure_loaded(dev, obj_path, profile_path)) {
            return false;
        }

        last_diagnostic_parts.clear();
        for (const auto& part : rig.parts) {
            if (!render_part_allowed(part.id, filter)) {
                continue;
            }
            auto binding = rbr_texture_for_material(part.material, fallback_body, fallback_helmet, fallback_face, fallback_hands, fallback_shoes);
            last_diagnostic_parts.push_back({ part.region_name, part.material, binding.name, binding.fallback });
            render_part(dev, vr, target, part, model, binding.texture, live_pose);
            if (binding.texture) {
                binding.texture->Release();
            }
        }
        return true;
    }

    bool render_mask(
        IDirect3DDevice9* dev,
        VRInterface* vr,
        RenderTarget target,
        const std::filesystem::path& obj_path,
        const std::filesystem::path& profile_path,
        const M4& model)
    {
        if (!ensure_loaded(dev, obj_path, profile_path)) {
            return false;
        }

        for (const auto& part : rig.parts) {
            auto* texture = mask_texture_for_region(dev, part.id);
            render_part(dev, vr, target, part, model, texture, nullptr);
            if (texture) {
                texture->Release();
            }
        }
        return true;
    }

    std::vector<DiagnosticPart> diagnostic_parts()
    {
        return last_diagnostic_parts;
    }

    void reset()
    {
        release_parts();
        release_mask_textures();
        last_diagnostic_parts.clear();
        rig = RigMesh {};
    }
}
