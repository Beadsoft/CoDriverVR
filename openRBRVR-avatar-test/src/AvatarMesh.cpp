#include "AvatarMesh.hpp"

#include "AssetProbe.hpp"
#include "Dx.hpp"
#include "Globals.hpp"
#include "Vertex.hpp"
#include "VR.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <fstream>
#include <gdiplus.h>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
    struct ObjVertex {
        glm::vec3 position {};
        glm::vec2 uv {};
    };

    struct MeshPart {
        std::string material;
        std::vector<Vertex> vertices;
        IDirect3DVertexBuffer9* vertex_buffer = nullptr;
    };

    struct MaterialTexture {
        std::filesystem::path file;
        IDirect3DTexture9* texture = nullptr;
    };

    std::filesystem::path loaded_path;
    std::filesystem::file_time_type loaded_write_time {};
    std::vector<MeshPart> parts;
    std::unordered_map<std::string, MaterialTexture> material_textures;
    bool load_attempted = false;
    constexpr D3DMATRIX identity_matrix = d3d_from_m4(glm::identity<glm::mat4x4>());
    ULONG_PTR gdiplus_token = 0;

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

    void release_parts()
    {
        for (auto& part : parts) {
            if (part.vertex_buffer) {
                part.vertex_buffer->Release();
                part.vertex_buffer = nullptr;
            }
        }
        parts.clear();
        for (auto& [_, material] : material_textures) {
            if (material.texture) {
                material.texture->Release();
                material.texture = nullptr;
            }
        }
        material_textures.clear();
    }

    std::wstring widen(const std::filesystem::path& path)
    {
        return path.wstring();
    }

    bool ensure_gdiplus()
    {
        if (gdiplus_token != 0) {
            return true;
        }
        Gdiplus::GdiplusStartupInput input;
        return Gdiplus::GdiplusStartup(&gdiplus_token, &input, nullptr) == Gdiplus::Ok;
    }

    bool load_image_texture(IDirect3DDevice9* dev, const std::filesystem::path& path, IDirect3DTexture9** texture)
    {
        if (!ensure_gdiplus() || !std::filesystem::exists(path)) {
            return false;
        }

        Gdiplus::Bitmap source(widen(path).c_str());
        if (source.GetLastStatus() != Gdiplus::Ok) {
            return false;
        }

        const auto width = source.GetWidth();
        const auto height = source.GetHeight();
        Gdiplus::Bitmap argb(width, height, PixelFormat32bppARGB);
        Gdiplus::Graphics graphics(&argb);
        graphics.DrawImage(&source, 0, 0, width, height);

        Gdiplus::BitmapData bitmap_data {};
        Gdiplus::Rect rect(0, 0, width, height);
        if (argb.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmap_data) != Gdiplus::Ok) {
            return false;
        }

        IDirect3DTexture9* created = nullptr;
        if (dev->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &created, nullptr) != D3D_OK) {
            argb.UnlockBits(&bitmap_data);
            return false;
        }

        D3DLOCKED_RECT locked {};
        if (created->LockRect(0, &locked, nullptr, 0) != D3D_OK) {
            created->Release();
            argb.UnlockBits(&bitmap_data);
            return false;
        }

        for (UINT y = 0; y < height; ++y) {
            memcpy(
                static_cast<uint8_t*>(locked.pBits) + y * locked.Pitch,
                static_cast<uint8_t*>(bitmap_data.Scan0) + y * bitmap_data.Stride,
                width * 4);
        }

        created->UnlockRect(0);
        argb.UnlockBits(&bitmap_data);
        *texture = created;
        dbg(std::format("AvatarMesh: loaded material texture {}", path.string()));
        return true;
    }

    std::filesystem::path resolve_material_texture(const std::filesystem::path& obj_dir, const std::filesystem::path& raw_path)
    {
        const auto filename = raw_path.filename();
        const auto stem = filename.stem().string();
        const auto candidates = std::array {
            obj_dir / filename,
            obj_dir / "textures" / filename,
            obj_dir / (stem + ".png"),
            obj_dir / "textures" / (stem + ".png"),
            obj_dir / (stem + ".jpg"),
            obj_dir / "textures" / (stem + ".jpg"),
        };
        for (const auto& candidate : candidates) {
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
        }
        return {};
    }

    void load_mtl(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if (!file.good()) {
            return;
        }

        std::string current_material;
        for (std::string raw; std::getline(file, raw);) {
            const auto line = trim(raw);
            if (line.empty() || line.starts_with('#')) {
                continue;
            }
            std::stringstream ss(line);
            std::string op;
            ss >> op;
            if (op == "newmtl") {
                ss >> current_material;
            } else if (op == "map_Kd" && !current_material.empty()) {
                std::string texture_name;
                ss >> texture_name;
                const auto resolved = resolve_material_texture(path.parent_path(), std::filesystem::path(texture_name));
                if (!resolved.empty()) {
                    material_textures[current_material].file = resolved;
                }
            }
        }
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

    bool load_obj(IDirect3DDevice9* dev, const std::filesystem::path& path)
    {
        release_parts();
        load_attempted = true;
        const auto filename = lower_copy(path.filename().string());
        const bool rbr_dump_mesh = filename == "driver_local.obj" || filename == "codriver_local.obj";

        std::ifstream file(path);
        if (!file.good()) {
            dbg(std::format("AvatarMesh: OBJ not found: {}", path.string()));
            return false;
        }

        std::vector<glm::vec3> positions;
        std::vector<glm::vec2> texcoords;
        std::unordered_map<std::string, size_t> material_to_part;
        std::string current_material = "body";

        const auto get_part = [&]() -> MeshPart& {
            if (auto it = material_to_part.find(current_material); it != material_to_part.end()) {
                return parts[it->second];
            }
            material_to_part[current_material] = parts.size();
            auto& part = parts.emplace_back();
            part.material = current_material;
            return part;
        };

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
            } else if (op == "mtllib") {
                std::string mtl_name;
                ss >> mtl_name;
                if (!mtl_name.empty()) {
                    load_mtl(path.parent_path() / mtl_name);
                }
            } else if (op == "usemtl" || op == "g" || op == "o") {
                std::string name;
                ss >> name;
                if (!name.empty()) {
                    current_material = name;
                }
            } else if (op == "f") {
                std::vector<std::pair<int, int>> refs;
                for (std::string ref; ss >> ref;) {
                    if (auto parsed = parse_face_ref(ref)) {
                        refs.push_back(*parsed);
                    }
                }
                if (refs.size() < 3) {
                    continue;
                }

                auto& part = get_part();
                for (size_t i = 1; i + 1 < refs.size(); ++i) {
                    const std::array tri { refs[0], refs[i], refs[i + 1] };
                    for (const auto& [vi, ti] : tri) {
                        if (vi < 0 || static_cast<size_t>(vi) >= positions.size()) {
                            continue;
                        }
                        const auto p = positions[vi];
                        const auto uv = ti >= 0 && static_cast<size_t>(ti) < texcoords.size() ? texcoords[ti] : glm::vec2 { 0.0f, 0.0f };
                        if (rbr_dump_mesh) {
                            part.vertices.push_back(Vertex { p.x, p.y, p.z, uv.x, 1.0f - uv.y });
                        } else {
                            part.vertices.push_back(Vertex { p.x, p.z, p.y, uv.x, 1.0f - uv.y });
                        }
                    }
                }
            }
        }

        std::erase_if(parts, [](const MeshPart& part) { return part.vertices.empty(); });
        if (parts.empty()) {
            dbg(std::format("AvatarMesh: OBJ had no usable triangles: {}", path.string()));
            return false;
        }

        for (auto& part : parts) {
            if (!create_vertex_buffer(dev, part.vertices.data(), part.vertices.size(), &part.vertex_buffer)) {
                release_parts();
                return false;
            }
        }

        loaded_path = path;
        loaded_write_time = std::filesystem::last_write_time(path);
        dbg(std::format("AvatarMesh: loaded {} mesh parts from {}", parts.size(), path.string()));
        return true;
    }

    bool ensure_loaded(IDirect3DDevice9* dev, const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path)) {
            if (!load_attempted || loaded_path != path) {
                load_attempted = true;
                loaded_path = path;
                dbg(std::format("AvatarMesh: waiting for OBJ at {}", path.string()));
            }
            return false;
        }

        const auto write_time = std::filesystem::last_write_time(path);
        if (!parts.empty() && loaded_path == path && loaded_write_time == write_time) {
            return true;
        }

        return load_obj(dev, path);
    }

    IDirect3DBaseTexture9* rbr_texture_for_material(
        IDirect3DDevice9* dev,
        const std::string& material,
        IDirect3DBaseTexture9* fallback_body,
        IDirect3DBaseTexture9* fallback_helmet,
        IDirect3DBaseTexture9* fallback_face,
        IDirect3DBaseTexture9* fallback_hands,
        IDirect3DBaseTexture9* fallback_shoes)
    {
        const auto m = lower_copy(material);
        const auto texture_mode = lower_copy(g::cfg.roadbook_vr.avatar_mesh_texture_mode);
        if (texture_mode == "asset" || texture_mode == "model") {
            if (auto it = material_textures.find(material); it != material_textures.end()) {
                if (!it->second.texture && !it->second.file.empty()) {
                    load_image_texture(dev, it->second.file, &it->second.texture);
                }
                if (it->second.texture) {
                    it->second.texture->AddRef();
                    return it->second.texture;
                }
            }
        }

        const auto find = [](std::initializer_list<const char*> names) -> IDirect3DBaseTexture9* {
            for (const auto* name : names) {
                if (auto* texture = asset_probe::find_loaded_texture_by_suffix(name)) {
                    return texture;
                }
            }
            return nullptr;
        };

        IDirect3DBaseTexture9* selected = nullptr;
        if (m.contains("helmet") || m.contains("helemt") || m.contains("visor")) {
            selected = find({ "incar_helmet03.dds", "helmet_outside_01.dds" });
            if (!selected) {
                selected = fallback_helmet;
                if (selected) {
                    selected->AddRef();
                }
            }
        } else if (m.contains("face") || m.contains("skin") || m.contains("head")) {
            selected = find({ "reid_face_04.dds", "eeros_face01.dds", "face_robert_reid_nohelmet_02.dds", "face_hd_richard_01.dds" });
            if (!selected) {
                selected = fallback_face;
                if (selected) {
                    selected->AddRef();
                }
            }
        } else if (m.contains("hand") || m.contains("glove")) {
            selected = find({ "gloves_01left.dds", "hands01.dds" });
            if (!selected) {
                selected = fallback_hands;
                if (selected) {
                    selected->AddRef();
                }
            }
        } else if (m.contains("shoe") || m.contains("boot") || m.contains("foot")) {
            selected = find({ "shoes01.dds", "ofdshoes01.dds" });
            if (!selected) {
                selected = fallback_shoes;
                if (selected) {
                    selected->AddRef();
                }
            }
        } else {
            selected = find({ "incar_drv_1_01.dds", "body_rallydriver_01.dds", "body-leftarm_rallydriver_01.dds" });
            if (!selected) {
                selected = fallback_body;
                if (selected) {
                    selected->AddRef();
                }
            }
        }
        return selected;
    }

    void render_part(
        IDirect3DDevice9* dev,
        VRInterface* vr,
        RenderTarget target,
        const MeshPart& part,
        const M4& model,
        IDirect3DBaseTexture9* texture)
    {
        if (!part.vertex_buffer || part.vertices.empty()) {
            return;
        }

        const auto& projection_l = vr->get_projection(target);
        const auto& eye_pos_l = vr->get_eye_pos(target);
        const auto& pose_l = vr->get_pose(target);
        const D3DMATRIX mvp_l = d3d_from_m4(projection_l * eye_pos_l * pose_l * g::flip_z_matrix * model);
        D3DMATRIX mvp_r = {};
        const bool use_multiview = dx::multiview_rendering_enabled() && (target == LeftEye || target == FocusLeft);
        if (use_multiview) {
            const auto right = render_target_counterpart(target);
            mvp_r = d3d_from_m4(vr->get_projection(right) * vr->get_eye_pos(right) * vr->get_pose(right) * g::flip_z_matrix * model);
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

        if (old_vs) {
            old_vs->Release();
        }
        if (old_ps) {
            old_ps->Release();
        }
        if (old_texture) {
            old_texture->Release();
        }
        if (old_stream) {
            old_stream->Release();
        }
    }
}

namespace avatar_mesh {
    bool render(
        IDirect3DDevice9* dev,
        VRInterface* vr,
        RenderTarget target,
        const std::filesystem::path& obj_path,
        const M4& model,
        IDirect3DBaseTexture9* fallback_body,
        IDirect3DBaseTexture9* fallback_helmet,
        IDirect3DBaseTexture9* fallback_face,
        IDirect3DBaseTexture9* fallback_hands,
        IDirect3DBaseTexture9* fallback_shoes)
    {
        if (!ensure_loaded(dev, obj_path)) {
            return false;
        }

        for (const auto& part : parts) {
            auto* texture = rbr_texture_for_material(dev, part.material, fallback_body, fallback_helmet, fallback_face, fallback_hands, fallback_shoes);
            render_part(dev, vr, target, part, model, texture);
            if (texture) {
                texture->Release();
            }
        }
        return true;
    }

    void reset()
    {
        release_parts();
        loaded_path.clear();
        loaded_write_time = {};
        load_attempted = false;
    }
}
