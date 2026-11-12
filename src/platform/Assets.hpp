#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <string_view>

namespace pinball {

struct TextureDeleter {
  void operator()(SDL_Texture* t) const noexcept { SDL_DestroyTexture(t); }
};
using TexturePtr = std::unique_ptr<SDL_Texture, TextureDeleter>;

// Loads read-only assets from the "assets" directory next to the executable.
// Nothing outside that directory can be opened through this class.
class AssetLoader {
 public:
  // Largest accepted image edge in pixels.
  static constexpr int kMaxTextureEdge = 4096;

  // Returns an unusable loader (every load fails) if the base path is unknown.
  static AssetLoader fromExecutableLocation();

  // Loads a PNG as a texture with linear filtering. name must satisfy
  // isSafeAssetName. Returns nullptr and logs on any failure.
  TexturePtr loadTexture(SDL_Renderer* renderer, std::string_view name) const;

 private:
  explicit AssetLoader(std::string root) : root_(std::move(root)) {}

  std::string root_;  // Ends with a path separator, or empty if unusable.
};

}  // namespace pinball
