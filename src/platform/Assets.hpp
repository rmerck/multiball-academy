#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/AssetManifest.hpp"

namespace pinball {

struct TextureDeleter {
  void operator()(SDL_Texture* t) const noexcept { SDL_DestroyTexture(t); }
};
using TexturePtr = std::unique_ptr<SDL_Texture, TextureDeleter>;

// Loads read-only assets from the "assets" directory next to the executable.
//
// Every asset is listed in a manifest compiled into the program, with the
// size and SHA-256 it had at build time. A file is read fully into memory,
// checked against the manifest, and only then decoded from that same buffer.
// Anything missing, altered or unlisted is refused.
class AssetLoader {
 public:
  // Largest accepted image edge in pixels.
  static constexpr int kMaxTextureEdge = 4096;
  // Largest accepted file in bytes.
  static constexpr std::uint64_t kMaxAssetBytes = 16ULL * 1024ULL * 1024ULL;

  // Returns an unusable loader (every load fails) if the base path is unknown.
  static AssetLoader fromExecutableLocation();

  // Checks every asset in the manifest. Returns false, after logging each
  // problem, if any is missing or does not match. Call before starting.
  // On success the verified bytes are kept for loadTexture, so no file is
  // read or hashed a second time; they are freed with the loader.
  bool verifyAll();

  // Loads a verified PNG as a texture with linear filtering. Returns nullptr
  // and logs on any failure. Works without verifyAll: the file is then read
  // and verified here.
  TexturePtr loadTexture(SDL_Renderer* renderer, std::string_view name) const;

 private:
  explicit AssetLoader(std::string root) : root_(std::move(root)) {}

  // Reads and verifies one asset. On success bytes holds exactly the
  // verified content.
  bool readVerified(const AssetEntry& entry, std::vector<std::uint8_t>& bytes) const;

  std::string root_;  // Ends with a path separator, or empty if unusable.
  // Verified content of each manifest entry, in manifest order. Empty until
  // verifyAll has succeeded.
  std::vector<std::vector<std::uint8_t>> verified_;
};

}  // namespace pinball
