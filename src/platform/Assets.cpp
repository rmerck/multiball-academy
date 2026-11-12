#include "platform/Assets.hpp"

#include <cstddef>
#include <utility>

#include "core/AssetName.hpp"
#include "core/PngHeader.hpp"
#include "platform/EmbeddedManifest.hpp"
#include "platform/SafeFile.hpp"

namespace pinball {

namespace {

struct SurfaceDeleter {
  void operator()(SDL_Surface* s) const noexcept { SDL_DestroySurface(s); }
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

bool AssetLoader::readVerified(const AssetEntry& entry, std::vector<std::uint8_t>& bytes) const {
  bytes.clear();
  if (root_.empty()) {
    return false;
  }
  // Manifest names are generated at build time, but validate anyway.
  if (!isSafeAssetName(entry.name) || entry.size == 0 || entry.size > kMaxAssetBytes) {
    logAssetError(entry.name, "invalid manifest entry");
    return false;
  }

  const std::string path = root_ + std::string(entry.name);
  // Only a regular file of exactly the built size. Never more than one byte
  // beyond that is read, so nothing is allocated for an attacker-chosen size.
  const auto expected = static_cast<std::size_t>(entry.size);
  const ReadResult result = readRegularFile(path, expected, bytes);
  if (result != ReadResult::Ok || bytes.size() != expected) {
    logAssetError(entry.name, result == ReadResult::Missing
                                  ? "missing"
                                  : "integrity check failed: not a regular file of the built size");
    bytes.clear();
    return false;
  }

  if (checkAsset(entry, bytes.data(), bytes.size()) != AssetCheck::Ok) {
    logAssetError(entry.name, "integrity check failed: content differs from the build");
    bytes.clear();
    return false;
  }
  return true;
}

bool AssetLoader::verifyAll() const {
  const AssetEntry* const entries = embeddedAssets();
  const std::size_t count = embeddedAssetCount();
  bool ok = count > 0;
  std::vector<std::uint8_t> bytes;
  for (std::size_t i = 0; i < count; ++i) {
    ok = readVerified(entries[i], bytes) && ok;
  }
  return ok;
}

TexturePtr AssetLoader::loadTexture(SDL_Renderer* renderer, std::string_view name) const {
  if (renderer == nullptr) {
    return nullptr;
  }
  // The manifest is the allowlist: nothing else can be opened.
  const AssetEntry* const entry = findAsset(embeddedAssets(), embeddedAssetCount(), name);
  if (entry == nullptr) {
    logAssetError(name, "not in the asset manifest");
    return nullptr;
  }

  std::vector<std::uint8_t> bytes;
  if (!readVerified(*entry, bytes)) {
    return nullptr;
  }

  // Check the declared image size before decoding.
  const auto declared = parsePngHeader(bytes.data(), bytes.size());
  if (!declared || declared->width > static_cast<std::uint32_t>(kMaxTextureEdge) ||
      declared->height > static_cast<std::uint32_t>(kMaxTextureEdge)) {
    logAssetError(name, "not a PNG or image too large");
    return nullptr;
  }

  // Decode the verified bytes themselves, not the file again, so the content
  // cannot change between the check and the use.
  SDL_IOStream* const memory = SDL_IOFromConstMem(bytes.data(), bytes.size());
  if (memory == nullptr) {
    logAssetError(name, SDL_GetError());
    return nullptr;
  }
  const std::unique_ptr<SDL_Surface, SurfaceDeleter> surface(SDL_LoadPNG_IO(memory, true));
  if (!surface) {
    logAssetError(name, SDL_GetError());
    return nullptr;
  }
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
