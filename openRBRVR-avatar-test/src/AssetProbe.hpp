#pragma once

#include <d3d9.h>
#include <string>

namespace asset_probe {
    void log_texture_load(const std::string& name, IDirect3DTexture9* texture);
    void log_draw_primitive(IDirect3DDevice9* dev, D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT primitive_count);
    void log_draw_indexed_primitive(IDirect3DDevice9* dev, D3DPRIMITIVETYPE primitive_type, INT base_vertex_index, UINT min_vertex_index, UINT num_vertices, UINT start_index, UINT primitive_count);
    IDirect3DBaseTexture9* find_loaded_texture_by_suffix(const std::string& suffix);
    bool render_driver_avatar(IDirect3DDevice9* dev);
    bool render_passenger_avatar(IDirect3DDevice9* dev);
    bool has_driver_avatar();
    bool has_passenger_avatar();
}
