#pragma once

#include "RenderTarget.hpp"
#include "Util.hpp"

#include <d3d9.h>
#include <filesystem>

class VRInterface;

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
        IDirect3DBaseTexture9* fallback_shoes);
    void reset();
}
