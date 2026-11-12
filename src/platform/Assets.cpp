#include "platform/Assets.hpp"

#include <array>
#include <utility>

#include "core/AssetName.hpp"
#include "core/PngHeader.hpp"

namespace pinball {

namespace {

// Largest accepted file. Real assets are far smaller.
constexpr Sint64 kMaxAssetBytes = 16 * 1024 * 1024;

struct SurfaceDeleter {
  void operator()(SDL_Surface* s) const noexcept { SDL_DestroySurface(s); }
};
struct IoDeleter {
  void operator()(SDL_IOStream* io) const noexcept { SDL_CloseIO(io); }
};

void logAssetError(std::string_view name, const char* what) {
  SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Asset %.*s: %s", static_cast<int>(name.size()),
               name.data(), what);
}

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
  std::unique_ptr<SDL_IOStream, IoDeleter> io(SDL_IOFromFile(path.c_str(), "rb"));
  if (!io) {
    logAssetError(name, SDL_GetError());
    return nullptr;
  }

  // Check the file size and the declared image size before decoding, so an
  // oversized or hostile file is refused without allocating for it.
  const Sint64 fileSize = SDL_GetIOSize(io.get());
  if (fileSize < static_cast<Sint64>(kPngHeaderBytes) || fileSize > kMaxAssetBytes) {
    logAssetError(name, "unsupported file size");
    return nullptr;
  }
  std::array<unsigned char, kPngHeaderBytes> head{};
  if (SDL_ReadIO(io.get(), head.data(), head.size()) != head.size()) {
    logAssetError(name, "could not read header");
    return nullptr;
  }
  const auto declared = parsePngHeader(head.data(), head.size());
  if (!declared || declared->width > static_cast<std::uint32_t>(kMaxTextureEdge) ||
      declared->height > static_cast<std::uint32_t>(kMaxTextureEdge)) {
    logAssetError(name, "not a PNG or image too large");
    return nullptr;
  }
  if (SDL_SeekIO(io.get(), 0, SDL_IO_SEEK_SET) != 0) {
    logAssetError(name, "could not rewind");
    return nullptr;
  }

  const std::unique_ptr<SDL_Surface, SurfaceDeleter> surface(SDL_LoadPNG_IO(io.get(), false));
  if (!surface) {
    logAssetError(name, SDL_GetError());
    return nullptr;
  }
  // The decoder's result must agree with what the header declared.
  if (surface->w <= 0 || surface->h <= 0 || surface->w > kMaxTextureEdge ||
      surface->h > kMaxTextureEdge) {
    logAssetError(name, "decoded size out of range");
    return nullptr;
  }

  TexturePtr texture(SDL_CreateTextureFromSurface(renderer, surface.get()));
  if (!texture) {
    logAssetError(name, SDL_GetError());
    return nullptr;
  }
  // Best effort: sprites are authored at 4x and scaled down.
  SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR);
  SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
  return texture;
}

}  // namespace pinball
