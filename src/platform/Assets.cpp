#include "platform/Assets.hpp"

#include <utility>

#include "core/AssetName.hpp"

namespace pinball {

namespace {

struct SurfaceDeleter {
  void operator()(SDL_Surface* s) const noexcept { SDL_DestroySurface(s); }
};

}  // namespace

AssetLoader AssetLoader::fromExecutableLocation() {
  // Owned by SDL; ends with a path separator.
  const char* base = SDL_GetBasePath();
  if (base == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_GetBasePath failed: %s", SDL_GetError());
    return AssetLoader(std::string());
  }
  return AssetLoader(std::string(base) + "assets/");
}

TexturePtr AssetLoader::loadTexture(SDL_Renderer* renderer, std::string_view name) const {
  if (renderer == nullptr || root_.empty()) {
    return nullptr;
  }
  if (!isSafeAssetName(name)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rejected asset name");
    return nullptr;
  }

  const std::string path = root_ + std::string(name);
  const std::unique_ptr<SDL_Surface, SurfaceDeleter> surface(SDL_LoadPNG(path.c_str()));
  if (!surface) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load %.*s: %s",
                 static_cast<int>(name.size()), name.data(), SDL_GetError());
    return nullptr;
  }
  if (surface->w <= 0 || surface->h <= 0 || surface->w > kMaxTextureEdge ||
      surface->h > kMaxTextureEdge) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Asset %.*s has unsupported size %dx%d",
                 static_cast<int>(name.size()), name.data(), surface->w, surface->h);
    return nullptr;
  }

  TexturePtr texture(SDL_CreateTextureFromSurface(renderer, surface.get()));
  if (!texture) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create texture for %.*s: %s",
                 static_cast<int>(name.size()), name.data(), SDL_GetError());
    return nullptr;
  }
  // Best effort: sprites are authored at 4x and scaled down.
  SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR);
  SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
  return texture;
}

}  // namespace pinball
