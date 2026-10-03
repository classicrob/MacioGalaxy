"""Reuse an ImGui texture for editor camera readbacks without allocating per frame."""
def patch(text: str) -> str:
    anchor = 'std::vector<wgpu::Texture> g_wgpuTextures;'
    if text.count(anchor) != 1:
        raise ValueError('Aurora ImGui texture storage anchor mismatch')
    text = text.replace(anchor, anchor + '\nstd::map<ImTextureID, wgpu::Texture> g_petariTextures;')
    text = text.replace('  g_wgpuTextures.clear();', '  g_petariTextures.clear();\n  g_wgpuTextures.clear();')
    anchor = '  return reinterpret_cast<ImTextureID>(textureView.MoveToCHandle());'
    if text.count(anchor) != 1:
        raise ValueError('Aurora ImGui texture view anchor mismatch')
    text = text.replace(anchor, '''  const auto id = reinterpret_cast<ImTextureID>(textureView.MoveToCHandle());
  g_petariTextures.emplace(id, texture);
  return id;''')
    helper = '''
bool update_texture(ImTextureID id, uint32_t width, uint32_t height, const uint8_t* data) noexcept {
  if (window::get_sdl_renderer()) {
    auto* texture = reinterpret_cast<SDL_Texture*>(id);
    if (std::find(g_sdlTextures.begin(), g_sdlTextures.end(), texture) == g_sdlTextures.end()) return false;
    if (texture->w != static_cast<int>(width) || texture->h != static_cast<int>(height)) return false;
    return SDL_UpdateTexture(texture, nullptr, data, width * 4);
  }
  const auto it = g_petariTextures.find(id);
  if (it == g_petariTextures.end() || it->second.GetWidth() != width || it->second.GetHeight() != height) return false;
  const wgpu::TexelCopyTextureInfo dst{.texture = it->second};
  const uint32_t stride = AURORA_ALIGN(width * 4, 256);
  const wgpu::TexelCopyBufferLayout layout{.bytesPerRow = stride, .rowsPerImage = height};
  const wgpu::Extent3D size{.width = width, .height = height, .depthOrArrayLayers = 1};
  enqueue_texture_upload(create_texture_upload_buffer(width, height, data, stride), dst, layout, size);
  return true;
}
'''
    text = text.replace('} // namespace aurora::imgui', helper + '} // namespace aurora::imgui')
    text += '''
extern "C" bool aurora_imgui_update_texture(unsigned long long id, unsigned width, unsigned height, const void* rgba) {
  return aurora::imgui::update_texture(static_cast<ImTextureID>(id), width, height, static_cast<const uint8_t*>(rgba));
}
'''
    return '#include <map>\n#include <algorithm>\n' + text
