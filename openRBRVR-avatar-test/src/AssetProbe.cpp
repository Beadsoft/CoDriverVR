#include "AssetProbe.hpp"

#include "Globals.hpp"
#include "RBR.hpp"
#include "Util.hpp"

#include <chrono>
#include <array>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <ranges>
#include <set>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace {
    constexpr size_t MaxTextureLogs = 512;
    constexpr size_t MaxDrawLogs = 256;
    constexpr size_t MaxReplayDrawsPerGroup = 48;

    std::unordered_set<std::string> logged_textures;
    std::unordered_map<uintptr_t, std::string> texture_names;
    std::unordered_map<std::string, IDirect3DBaseTexture9*> loaded_textures;
    size_t draw_log_count = 0;
    bool session_logged = false;
    bool replaying = false;

    struct CapturedDraw {
        std::string texture_name;
        IDirect3DBaseTexture9* texture = nullptr;
        IDirect3DVertexBuffer9* stream = nullptr;
        IDirect3DIndexBuffer9* indices = nullptr;
        IDirect3DVertexShader9* vertex_shader = nullptr;
        IDirect3DPixelShader9* pixel_shader = nullptr;
        IDirect3DVertexDeclaration9* vertex_declaration = nullptr;
        D3DPRIMITIVETYPE primitive_type = D3DPT_TRIANGLELIST;
        INT base_vertex_index = 0;
        UINT min_vertex_index = 0;
        UINT num_vertices = 0;
        UINT start_index = 0;
        UINT primitive_count = 0;
        UINT stream_offset = 0;
        UINT stream_stride = 0;
        DWORD fvf = 0;
        D3DMATRIX world {};
        D3DMATRIX view {};
        D3DMATRIX projection {};
    };

    struct CaptureGroup {
        std::vector<CapturedDraw> draws;
    };

    CaptureGroup driver_group;
    CaptureGroup passenger_group;
    CaptureGroup crew_seat_group;
    CaptureGroup menu_human_group;
    size_t dumped_driver_draw_count = 0;
    size_t dumped_passenger_draw_count = 0;
    size_t dumped_crew_seat_draw_count = 0;
    size_t dumped_menu_human_draw_count = 0;

    void log_line(const std::string& line);

    void safe_release(IDirect3DBaseTexture9*& ptr)
    {
        if (ptr) {
            ptr->Release();
            ptr = nullptr;
        }
    }

    template <typename T>
    void safe_release(T*& ptr)
    {
        if (ptr) {
            ptr->Release();
            ptr = nullptr;
        }
    }

    void release_draw(CapturedDraw& draw)
    {
        safe_release(draw.texture);
        safe_release(draw.stream);
        safe_release(draw.indices);
        safe_release(draw.vertex_shader);
        safe_release(draw.pixel_shader);
        safe_release(draw.vertex_declaration);
    }

    void reset_draw(CapturedDraw& dst, CapturedDraw src)
    {
        release_draw(dst);
        dst = src;
    }

    void add_ref_draw(CapturedDraw& draw)
    {
        if (draw.texture) {
            draw.texture->AddRef();
        }
        if (draw.stream) {
            draw.stream->AddRef();
        }
        if (draw.indices) {
            draw.indices->AddRef();
        }
        if (draw.vertex_shader) {
            draw.vertex_shader->AddRef();
        }
        if (draw.pixel_shader) {
            draw.pixel_shader->AddRef();
        }
        if (draw.vertex_declaration) {
            draw.vertex_declaration->AddRef();
        }
    }

    bool enabled()
    {
        return g::cfg.roadbook_vr.asset_probe_enabled;
    }

    std::string render_target_name()
    {
        if (!g::vr_render_target) {
            return "screen";
        }

        switch (*g::vr_render_target) {
            case LeftEye: return "LeftEye";
            case RightEye: return "RightEye";
            case FocusLeft: return "FocusLeft";
            case FocusRight: return "FocusRight";
            case PassengerMono: return "PassengerMono";
            case GameMenu: return "GameMenu";
            case Overlay: return "Overlay";
            default: return std::format("RenderTarget{}", static_cast<int>(*g::vr_render_target));
        }
    }

    std::string known_texture_name(IDirect3DBaseTexture9* texture)
    {
        if (!texture) {
            return "none";
        }
        for (const auto& entry : g::car_textures) {
            if (entry.second == texture) {
                return entry.first;
            }
        }
        if (auto it = texture_names.find(reinterpret_cast<uintptr_t>(texture)); it != texture_names.end()) {
            return it->second;
        }
        return std::format("unknown@0x{:x}", reinterpret_cast<uintptr_t>(texture));
    }

    std::string lower_copy(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    bool is_driver_texture(const std::string& texture)
    {
        const auto t = lower_copy(texture);
        return t.ends_with("body_rallydriver_01.dds")
            || t.ends_with("_drivers.dds")
            || t.ends_with("-drivers.dds")
            || t.contains("body_rallydriver-team")
            || t.contains("body_rallydriver_la-team")
            || t.ends_with("body-leftarm_rallydriver_01.dds")
            || t.ends_with("hands01.dds")
            || t.ends_with("helmet_outside_01.dds")
            || t.ends_with("ofdshoes01.dds")
            || t.contains("28g_face_")
            || t.ends_with("eeros_face01.dds")
            || t.ends_with("face_robert_reid_nohelmet_02.dds")
            || t.ends_with("face_hd_richard_01.dds")
            || t.ends_with("teeth_richard_burns_01.dds");
    }

    bool is_passenger_texture(const std::string& texture)
    {
        const auto t = lower_copy(texture);
        const bool passenger_body = t.ends_with("incar_drv_1_01.dds")
            || t.ends_with("_drivers.dds")
            || t.ends_with("-drivers.dds")
            || t.ends_with("incar_helmet03.dds")
            || t.ends_with("gloves_01left.dds")
            || t.ends_with("shoes01.dds")
            || t.ends_with("reid_face_04.dds");
        const bool roadbook = t.ends_with("routbook.dds")
            || t.ends_with("routbookspiral.dds");
        return passenger_body || (g::cfg.roadbook_vr.roadbook_use_game_assets && roadbook);
    }

    bool is_car_specific_crew_texture(const std::string& texture)
    {
        const auto t = lower_copy(texture);
        return t.ends_with("_drivers.dds") || t.ends_with("-drivers.dds");
    }

    bool is_roadbook_texture(const std::string& texture)
    {
        const auto t = lower_copy(texture);
        return t.ends_with("routbook.dds") || t.ends_with("routbookspiral.dds");
    }

    bool is_crew_seat_texture(const std::string& texture)
    {
        const auto t = lower_copy(texture);
        return t.ends_with("incar_chair_01.dds")
            || t.ends_with("incar_belt01.dds")
            || t.contains("seat")
            || t.contains("chair")
            || t.contains("belt");
    }

    bool replay_enabled()
    {
        const auto mode = lower_copy(g::cfg.roadbook_vr.avatar_reuse_mode);
        return g::cfg.roadbook_vr.avatar_replay_enabled
            && (mode == "rbrcapturethenfallback" || mode == "rbrcapture" || mode == "probeandcapture");
    }

    bool dump_enabled()
    {
        return g::cfg.roadbook_vr.avatar_dump_enabled;
    }

    bool capture_enabled()
    {
        return replay_enabled() || dump_enabled();
    }

    bool should_dump_group(const std::string& group_name)
    {
        const auto target = lower_copy(g::cfg.roadbook_vr.avatar_dump_target);
        if (target == "both") {
            return true;
        }
        if (group_name == "menu_humans") {
            return target == "menuhumans" || target == "menu_humans" || target == "menu";
        }
        if (group_name == "crew_seats") {
            return target == "seats" || target == "crew_seats" || target == "crewseats";
        }
        return target == group_name;
    }

    std::string material_name_for_texture(const std::string& texture)
    {
        auto value = texture;
        for (auto& c : value) {
            if (!std::isalnum(static_cast<unsigned char>(c))) {
                c = '_';
            }
        }
        return value.empty() ? "material_none" : value;
    }

    std::filesystem::path dump_directory()
    {
        return std::filesystem::path(g::cfg.roadbook_vr.rbr_root) / g::cfg.roadbook_vr.avatar_dump_directory;
    }

    struct VertexLayout {
        size_t position_offset = 0;
        size_t uv_offset = 0;
        bool has_position = false;
        bool has_uv = false;
    };

    void apply_stride_uv_heuristic(VertexLayout& layout, const CapturedDraw& draw)
    {
        if (!layout.has_position || layout.has_uv) {
            return;
        }

        // Several RBR character/menu meshes are fixed-function streams where the FVF
        // returned by the device does not expose UVs. These are the common layouts seen
        // in captures: XYZ+UV, XYZ+diffuse+UV, and wider menu-character variants.
        size_t offset = 0;
        switch (draw.stream_stride) {
            case 20:
                offset = 12;
                break;
            case 24:
                offset = 16;
                break;
            case 32:
                offset = 24;
                break;
            case 36:
                offset = 28;
                break;
            case 44:
                offset = 36;
                break;
            default:
                return;
        }

        if (offset + 8 <= draw.stream_stride) {
            layout.uv_offset = offset;
            layout.has_uv = true;
        }
    }

    std::optional<VertexLayout> layout_from_declaration(const CapturedDraw& draw)
    {
        if (!draw.vertex_declaration) {
            return std::nullopt;
        }

        D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH] {};
        UINT count = MAXD3DDECLLENGTH;
        if (draw.vertex_declaration->GetDeclaration(elements, &count) != D3D_OK) {
            return std::nullopt;
        }

        VertexLayout layout {};
        for (UINT i = 0; i < count; ++i) {
            const auto& e = elements[i];
            if (e.Stream == 0xFF) {
                break;
            }
            if (e.Stream != 0) {
                continue;
            }
            if (e.Usage == D3DDECLUSAGE_POSITION && e.UsageIndex == 0 && (e.Type == D3DDECLTYPE_FLOAT3 || e.Type == D3DDECLTYPE_FLOAT4)) {
                layout.position_offset = e.Offset;
                layout.has_position = true;
            } else if (e.Usage == D3DDECLUSAGE_TEXCOORD && e.UsageIndex == 0 && (e.Type == D3DDECLTYPE_FLOAT2 || e.Type == D3DDECLTYPE_FLOAT3 || e.Type == D3DDECLTYPE_FLOAT4)) {
                layout.uv_offset = e.Offset;
                layout.has_uv = true;
            }
        }

        if (!layout.has_position) {
            return std::nullopt;
        }
        return layout;
    }

    std::optional<VertexLayout> layout_from_fvf(const CapturedDraw& draw)
    {
        const auto position = draw.fvf & D3DFVF_POSITION_MASK;
        if (position != D3DFVF_XYZ) {
            return std::nullopt;
        }

        VertexLayout layout {};
        layout.position_offset = 0;
        layout.has_position = true;

        size_t offset = 12;
        if (draw.fvf & D3DFVF_NORMAL) {
            offset += 12;
        }
        if (draw.fvf & D3DFVF_PSIZE) {
            offset += 4;
        }
        if (draw.fvf & D3DFVF_DIFFUSE) {
            offset += 4;
        }
        if (draw.fvf & D3DFVF_SPECULAR) {
            offset += 4;
        }

        const auto tex_count = (draw.fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
        if (tex_count > 0 && offset + 8 <= draw.stream_stride) {
            layout.uv_offset = offset;
            layout.has_uv = true;
        }

        return layout;
    }

    std::optional<VertexLayout> vertex_layout(const CapturedDraw& draw)
    {
        if (auto layout = layout_from_declaration(draw)) {
            apply_stride_uv_heuristic(*layout, draw);
            return layout;
        }
        auto layout = layout_from_fvf(draw);
        if (layout) {
            apply_stride_uv_heuristic(*layout, draw);
        }
        return layout;
    }

    glm::vec3 transform_position(const D3DMATRIX& matrix, glm::vec3 p)
    {
        const auto transformed = m4_from_d3d(matrix) * glm::vec4(p, 1.0f);
        return glm::vec3(transformed.x, transformed.y, transformed.z);
    }

    bool write_group_obj(const CaptureGroup& group, const std::filesystem::path& path, const std::string& object_name, bool apply_world)
    {
        std::filesystem::create_directories(path.parent_path());

        auto mtl_path = path;
        mtl_path.replace_extension(".mtl");
        std::ofstream obj(path);
        std::ofstream mtl(mtl_path);
        if (!obj.good() || !mtl.good()) {
            log_line(std::format("AssetProbe dump failed to open output for {}", object_name));
            return false;
        }

        const auto mtl_name = mtl_path.filename().string();
        obj << "# openRBRVR captured RBR avatar mesh\n";
        obj << "mtllib " << mtl_name << "\n";
        obj << "o " << object_name << "\n";

        std::set<std::string> written_materials;
        size_t vertex_index = 1;
        size_t exported_draws = 0;
        size_t exported_triangles = 0;

        for (const auto& draw : group.draws) {
            if (draw.primitive_type != D3DPT_TRIANGLELIST || is_roadbook_texture(draw.texture_name)) {
                continue;
            }

            const auto layout = vertex_layout(draw);
            if (!layout) {
                log_line(std::format("AssetProbe dump skipped {}: unsupported vertex layout texture={} stride={} fvf=0x{:x}", object_name, draw.texture_name, draw.stream_stride, draw.fvf));
                continue;
            }

            D3DVERTEXBUFFER_DESC vb_desc {};
            D3DINDEXBUFFER_DESC ib_desc {};
            if (draw.stream->GetDesc(&vb_desc) != D3D_OK || draw.indices->GetDesc(&ib_desc) != D3D_OK) {
                log_line(std::format("AssetProbe dump skipped {}: failed buffer desc texture={}", object_name, draw.texture_name));
                continue;
            }

            void* vertex_data = nullptr;
            void* index_data = nullptr;
            if (draw.stream->Lock(0, 0, &vertex_data, D3DLOCK_READONLY) != D3D_OK) {
                log_line(std::format("AssetProbe dump skipped {}: vertex buffer lock failed texture={}", object_name, draw.texture_name));
                continue;
            }
            if (draw.indices->Lock(0, 0, &index_data, D3DLOCK_READONLY) != D3D_OK) {
                draw.stream->Unlock();
                log_line(std::format("AssetProbe dump skipped {}: index buffer lock failed texture={}", object_name, draw.texture_name));
                continue;
            }

            const auto material = material_name_for_texture(draw.texture_name);
            if (written_materials.insert(material).second) {
                mtl << "newmtl " << material << "\n";
                mtl << "Ka 1 1 1\n";
                mtl << "Kd 1 1 1\n";
                mtl << "Ks 0 0 0\n";
                if (draw.texture_name != "none") {
                    mtl << "map_Kd " << draw.texture_name << "\n";
                }
                mtl << "\n";
            }
            obj << "usemtl " << material << "\n";
            obj << "g " << material << "\n";

            const auto* vb = static_cast<const uint8_t*>(vertex_data);
            const auto* ib = static_cast<const uint8_t*>(index_data);
            const auto index_size = ib_desc.Format == D3DFMT_INDEX32 ? 4u : 2u;
            const auto index_count = draw.primitive_count * 3;
            const auto first_obj_vertex = vertex_index;
            bool draw_ok = true;

            for (UINT i = 0; i < index_count; ++i) {
                const auto index_offset = (draw.start_index + i) * index_size;
                if (index_offset + index_size > ib_desc.Size) {
                    draw_ok = false;
                    break;
                }

                const uint32_t raw_index = index_size == 4
                    ? *reinterpret_cast<const uint32_t*>(ib + index_offset)
                    : *reinterpret_cast<const uint16_t*>(ib + index_offset);
                const auto vertex_number = static_cast<int64_t>(draw.base_vertex_index) + raw_index;
                const auto vertex_offset = draw.stream_offset + static_cast<size_t>(vertex_number) * draw.stream_stride;
                if (vertex_number < 0 || vertex_offset + layout->position_offset + 12 > vb_desc.Size) {
                    draw_ok = false;
                    break;
                }

                const auto* vertex = vb + vertex_offset;
                auto position = *reinterpret_cast<const glm::vec3*>(vertex + layout->position_offset);
                if (apply_world) {
                    position = transform_position(draw.world, position);
                }

                glm::vec2 uv { 0.0f, 0.0f };
                if (layout->has_uv && vertex_offset + layout->uv_offset + 8 <= vb_desc.Size) {
                    uv = *reinterpret_cast<const glm::vec2*>(vertex + layout->uv_offset);
                }

                obj << std::format("v {:.8f} {:.8f} {:.8f}\n", position.x, position.y, position.z);
                obj << std::format("vt {:.8f} {:.8f}\n", uv.x, 1.0f - uv.y);
                ++vertex_index;
            }

            if (draw_ok) {
                for (UINT tri = 0; tri < draw.primitive_count; ++tri) {
                    const auto a = first_obj_vertex + tri * 3;
                    obj << std::format("f {}/{} {}/{} {}/{}\n", a, a, a + 1, a + 1, a + 2, a + 2);
                    ++exported_triangles;
                }
                ++exported_draws;
            } else {
                log_line(std::format("AssetProbe dump had out-of-range indices for {} texture={}", object_name, draw.texture_name));
            }

            draw.indices->Unlock();
            draw.stream->Unlock();
        }

        log_line(std::format("AssetProbe dumped {} draws, {} triangles to {}", exported_draws, exported_triangles, path.string()));
        return exported_draws > 0;
    }

    void dump_group_if_needed(const CaptureGroup& group, const std::string& group_name, size_t& dumped_count)
    {
        if (!dump_enabled() || !should_dump_group(group_name) || group.draws.empty() || dumped_count == group.draws.size()) {
            return;
        }

        const auto dir = dump_directory();
        const auto base_name = group_name == "driver"
            ? "driver"
            : (group_name == "codriver" ? "codriver" : (group_name == "crew_seats" ? "crew_seats" : "menu_humans"));
        write_group_obj(group, dir / std::format("{}_local.obj", base_name), base_name, false);
        if (g::cfg.roadbook_vr.avatar_dump_apply_world_transform) {
            write_group_obj(group, dir / std::format("{}_world.obj", base_name), base_name, true);
        }
        dumped_count = group.draws.size();
    }

    bool equivalent_draw(const CapturedDraw& lhs, const CapturedDraw& rhs)
    {
        return lhs.texture_name == rhs.texture_name
            && lhs.base_vertex_index == rhs.base_vertex_index
            && lhs.min_vertex_index == rhs.min_vertex_index
            && lhs.num_vertices == rhs.num_vertices
            && lhs.start_index == rhs.start_index
            && lhs.primitive_count == rhs.primitive_count;
    }

    void store_capture(CaptureGroup& group, CapturedDraw draw)
    {
        for (auto& existing : group.draws) {
            if (equivalent_draw(existing, draw)) {
                reset_draw(existing, draw);
                return;
            }
        }

        if (group.draws.size() >= MaxReplayDrawsPerGroup) {
            release_draw(group.draws.front());
            group.draws.erase(group.draws.begin());
        }
        group.draws.push_back(draw);
    }

    bool is_menu_human_candidate(
        const std::string& texture_name,
        D3DPRIMITIVETYPE primitive_type,
        UINT num_vertices,
        UINT primitive_count,
        UINT stream_stride)
    {
        if (!dump_enabled() || rbr::get_game_mode() != rbr::GameMode::MainMenu) {
            return false;
        }
        if (!lower_copy(texture_name).starts_with("unknown@") || primitive_type != D3DPT_TRIANGLELIST) {
            return false;
        }
        if (stream_stride != 24 && stream_stride != 32 && stream_stride != 36 && stream_stride != 44) {
            return false;
        }
        return num_vertices >= 3
            && num_vertices <= 12000
            && primitive_count >= 1
            && primitive_count <= 7000;
    }

    void capture_draw(
        IDirect3DDevice9* dev,
        D3DPRIMITIVETYPE primitive_type,
        INT base_vertex_index,
        UINT min_vertex_index,
        UINT num_vertices,
        UINT start_index,
        UINT primitive_count,
        IDirect3DBaseTexture9* current_texture,
        const std::string& texture_name)
    {
        if (replaying || !capture_enabled() || !rbr::is_rendering_3d() || !g::vr_render_target) {
            return;
        }
        CapturedDraw draw {};
        draw.texture_name = texture_name;
        draw.texture = current_texture;
        draw.primitive_type = primitive_type;
        draw.base_vertex_index = base_vertex_index;
        draw.min_vertex_index = min_vertex_index;
        draw.num_vertices = num_vertices;
        draw.start_index = start_index;
        draw.primitive_count = primitive_count;
        if (draw.texture) {
            draw.texture->AddRef();
        }

        dev->GetStreamSource(0, &draw.stream, &draw.stream_offset, &draw.stream_stride);
        dev->GetIndices(&draw.indices);
        dev->GetVertexShader(&draw.vertex_shader);
        dev->GetPixelShader(&draw.pixel_shader);
        dev->GetVertexDeclaration(&draw.vertex_declaration);
        dev->GetFVF(&draw.fvf);
        dev->GetTransform(D3DTS_WORLD, &draw.world);
        dev->GetTransform(D3DTS_VIEW, &draw.view);
        dev->GetTransform(D3DTS_PROJECTION, &draw.projection);

        const auto is_driver = is_driver_texture(texture_name);
        const auto is_passenger = is_passenger_texture(texture_name);
        const auto is_crew_seat = is_crew_seat_texture(texture_name);
        const auto is_menu_human = is_menu_human_candidate(texture_name, primitive_type, num_vertices, primitive_count, draw.stream_stride);
        if (!is_driver && !is_passenger && !is_crew_seat && !is_menu_human) {
            release_draw(draw);
            return;
        }

        if (!draw.stream || !draw.indices || (!draw.vertex_declaration && draw.fvf == 0)) {
            release_draw(draw);
            return;
        }

        if (is_car_specific_crew_texture(texture_name)) {
            CapturedDraw passenger_draw = draw;
            add_ref_draw(passenger_draw);
            store_capture(driver_group, draw);
            dump_group_if_needed(driver_group, "driver", dumped_driver_draw_count);
            store_capture(passenger_group, passenger_draw);
            dump_group_if_needed(passenger_group, "codriver", dumped_passenger_draw_count);
        } else if (is_driver) {
            store_capture(driver_group, draw);
            dump_group_if_needed(driver_group, "driver", dumped_driver_draw_count);
        } else if (is_passenger) {
            store_capture(passenger_group, draw);
            dump_group_if_needed(passenger_group, "codriver", dumped_passenger_draw_count);
        } else if (is_crew_seat) {
            store_capture(crew_seat_group, draw);
            dump_group_if_needed(crew_seat_group, "crew_seats", dumped_crew_seat_draw_count);
        } else {
            store_capture(menu_human_group, draw);
            dump_group_if_needed(menu_human_group, "menu_humans", dumped_menu_human_draw_count);
        }
    }

    bool render_group(IDirect3DDevice9* dev, const CaptureGroup& group)
    {
        if (group.draws.empty()) {
            return false;
        }

        IDirect3DBaseTexture9* old_texture = nullptr;
        IDirect3DVertexBuffer9* old_stream = nullptr;
        IDirect3DIndexBuffer9* old_indices = nullptr;
        IDirect3DVertexShader9* old_vertex_shader = nullptr;
        IDirect3DPixelShader9* old_pixel_shader = nullptr;
        IDirect3DVertexDeclaration9* old_vertex_declaration = nullptr;
        UINT old_stream_offset = 0;
        UINT old_stream_stride = 0;
        DWORD old_fvf = 0;
        D3DMATRIX old_world {};
        D3DMATRIX old_view {};
        D3DMATRIX old_projection {};
        DWORD old_z_enable = 0;
        DWORD old_alpha_blend = 0;

        dev->GetTexture(0, &old_texture);
        dev->GetStreamSource(0, &old_stream, &old_stream_offset, &old_stream_stride);
        dev->GetIndices(&old_indices);
        dev->GetVertexShader(&old_vertex_shader);
        dev->GetPixelShader(&old_pixel_shader);
        dev->GetVertexDeclaration(&old_vertex_declaration);
        dev->GetFVF(&old_fvf);
        dev->GetTransform(D3DTS_WORLD, &old_world);
        dev->GetTransform(D3DTS_VIEW, &old_view);
        dev->GetTransform(D3DTS_PROJECTION, &old_projection);
        dev->GetRenderState(D3DRS_ZENABLE, &old_z_enable);
        dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &old_alpha_blend);

        replaying = true;
        dev->SetRenderState(D3DRS_ZENABLE, TRUE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->BeginScene();
        for (const auto& draw : group.draws) {
            dev->SetTexture(0, draw.texture);
            dev->SetStreamSource(0, draw.stream, draw.stream_offset, draw.stream_stride);
            dev->SetIndices(draw.indices);
            dev->SetVertexShader(draw.vertex_shader);
            dev->SetPixelShader(draw.pixel_shader);
            if (draw.vertex_declaration) {
                dev->SetVertexDeclaration(draw.vertex_declaration);
            } else {
                dev->SetFVF(draw.fvf);
            }
            dev->SetTransform(D3DTS_PROJECTION, &draw.projection);
            dev->SetTransform(D3DTS_VIEW, &draw.view);
            dev->SetTransform(D3DTS_WORLD, &draw.world);
            g::hooks::draw_indexed_primitive.call(dev, draw.primitive_type, draw.base_vertex_index, draw.min_vertex_index, draw.num_vertices, draw.start_index, draw.primitive_count);
        }
        dev->EndScene();
        replaying = false;

        dev->SetRenderState(D3DRS_ZENABLE, old_z_enable);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, old_alpha_blend);
        dev->SetTexture(0, old_texture);
        dev->SetStreamSource(0, old_stream, old_stream_offset, old_stream_stride);
        dev->SetIndices(old_indices);
        dev->SetVertexShader(old_vertex_shader);
        dev->SetPixelShader(old_pixel_shader);
        if (old_vertex_declaration) {
            dev->SetVertexDeclaration(old_vertex_declaration);
        } else if (old_fvf != 0) {
            dev->SetFVF(old_fvf);
        }
        dev->SetTransform(D3DTS_PROJECTION, &old_projection);
        dev->SetTransform(D3DTS_VIEW, &old_view);
        dev->SetTransform(D3DTS_WORLD, &old_world);

        safe_release(old_texture);
        safe_release(old_stream);
        safe_release(old_indices);
        safe_release(old_vertex_shader);
        safe_release(old_pixel_shader);
        safe_release(old_vertex_declaration);
        return true;
    }

    void write_probe_log(const std::string& line)
    {
        std::ofstream log("Plugins\\openRBRVR-asset-probe.log", std::ios::app);
        if (!log.good()) {
            log.open("openRBRVR-asset-probe.log", std::ios::app);
        }
        if (!log.good()) {
            return;
        }

        if (!session_logged) {
            const auto now = std::chrono::system_clock::now();
            log << "AssetProbe session " << std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() << "\n";
            session_logged = true;
        }

        log << line << "\n";
    }

    void log_line(const std::string& line)
    {
        dbg(line);
        write_probe_log(line);
    }
}

namespace asset_probe {
    void log_texture_load(const std::string& name, IDirect3DTexture9* texture)
    {
        if (!texture) {
            return;
        }

        texture_names[reinterpret_cast<uintptr_t>(texture)] = name;
        const auto lowered_name = lower_copy(name);
        if (auto it = loaded_textures.find(lowered_name); it != loaded_textures.end()) {
            if (it->second != texture) {
                it->second->Release();
                loaded_textures.erase(it);
            }
        }
        if (!loaded_textures.contains(lowered_name)) {
            texture->AddRef();
            loaded_textures[lowered_name] = texture;
        }
        if (!enabled()) {
            return;
        }
        if (logged_textures.size() >= MaxTextureLogs) {
            return;
        }
        if (logged_textures.insert(name).second) {
            log_line(std::format("AssetProbe texture '{}' -> 0x{:x}", name, reinterpret_cast<uintptr_t>(texture)));
        }
    }

    void log_draw_primitive(IDirect3DDevice9* dev, D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT primitive_count)
    {
        if (!enabled() || !rbr::is_rendering_3d() || draw_log_count >= MaxDrawLogs) {
            return;
        }

        IDirect3DBaseTexture9* texture = nullptr;
        IDirect3DVertexBuffer9* stream = nullptr;
        UINT offset = 0;
        UINT stride = 0;
        dev->GetTexture(0, &texture);
        dev->GetStreamSource(0, &stream, &offset, &stride);

        log_line(std::format(
            "AssetProbe draw target={} primitive={} startVertex={} primitiveCount={} texture={} stream=0x{:x} stride={}",
            render_target_name(),
            static_cast<int>(primitive_type),
            start_vertex,
            primitive_count,
            known_texture_name(texture),
            reinterpret_cast<uintptr_t>(stream),
            stride));

        if (texture) {
            texture->Release();
        }
        if (stream) {
            stream->Release();
        }
        ++draw_log_count;
    }

    void log_draw_indexed_primitive(IDirect3DDevice9* dev, D3DPRIMITIVETYPE primitive_type, INT base_vertex_index, UINT min_vertex_index, UINT num_vertices, UINT start_index, UINT primitive_count)
    {
        IDirect3DBaseTexture9* texture = nullptr;
        IDirect3DVertexBuffer9* stream = nullptr;
        IDirect3DIndexBuffer9* indices = nullptr;
        UINT offset = 0;
        UINT stride = 0;
        dev->GetTexture(0, &texture);
        const auto texture_name = known_texture_name(texture);
        capture_draw(dev, primitive_type, base_vertex_index, min_vertex_index, num_vertices, start_index, primitive_count, texture, texture_name);

        if (replaying) {
            if (texture) {
                texture->Release();
            }
            return;
        }

        if (!enabled() || !rbr::is_rendering_3d() || draw_log_count >= MaxDrawLogs) {
            if (texture) {
                texture->Release();
            }
            return;
        }

        dev->GetStreamSource(0, &stream, &offset, &stride);
        dev->GetIndices(&indices);

        log_line(std::format(
            "AssetProbe drawIndexed target={} primitive={} baseVertex={} minVertex={} numVertices={} startIndex={} primitiveCount={} texture={} stream=0x{:x} indices=0x{:x} stride={}",
            render_target_name(),
            static_cast<int>(primitive_type),
            base_vertex_index,
            min_vertex_index,
            num_vertices,
            start_index,
            primitive_count,
            texture_name,
            reinterpret_cast<uintptr_t>(stream),
            reinterpret_cast<uintptr_t>(indices),
            stride));

        if (texture) {
            texture->Release();
        }
        if (stream) {
            stream->Release();
        }
        if (indices) {
            indices->Release();
        }
        ++draw_log_count;
    }

    IDirect3DBaseTexture9* find_loaded_texture_by_suffix(const std::string& suffix)
    {
        const auto wanted = lower_copy(suffix);
        for (const auto& [name, texture] : loaded_textures) {
            if (name.ends_with(wanted) && texture) {
                texture->AddRef();
                return texture;
            }
        }
        return nullptr;
    }

    bool render_driver_avatar(IDirect3DDevice9* dev)
    {
        return render_group(dev, driver_group);
    }

    bool render_passenger_avatar(IDirect3DDevice9* dev)
    {
        return render_group(dev, passenger_group);
    }

    bool has_driver_avatar()
    {
        return !driver_group.draws.empty();
    }

    bool has_passenger_avatar()
    {
        return !passenger_group.draws.empty();
    }
}
