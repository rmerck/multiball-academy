#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The general flash test of WCAG 2.3.1, per pixel, over a run of frames at a
// fixed rate: for each pixel the relative luminance is followed, and a
// change is counted each time it moves the other way from its last turning
// point by at least kStep (10 percent of the maximum) while the darker of
// the two is under kDarker (0.80). A pixel fails at a frame when it has made
// more than kChangesAllowed changes (three flashes, each a pair of opposite
// changes) within the last second. The measure is the share of the watched
// pixels failing at once, worst over the run: what the 25-percent-of-the-
// visual-field area rule is judged against.
//
// No SDL: frames come in as straight RGBA bytes.
namespace fxreel {

class FlashMeter {
 public:
  static constexpr float kStep = 0.10F;
  static constexpr float kDarker = 0.80F;
  static constexpr int kChangesAllowed = 6;
  static constexpr int kKept = kChangesAllowed + 1;

  // width x height pixels; mask (same size, may be empty) says which pixels
  // count; fps is the frame rate the frames are taken at.
  FlashMeter(int width, int height, std::vector<std::uint8_t> mask, int fps)
      : width_(width), height_(height), fps_(fps), mask_(std::move(mask)),
        extreme_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), -1.0F),
        direction_(extreme_.size(), 0),
        changes_(extreme_.size() * kKept, -1'000'000), now_(extreme_.size(), 0) {
    for (std::size_t i = 0; i < extreme_.size(); ++i) {
      watched_ += mask_.empty() || mask_[i] != 0 ? 1 : 0;
    }
  }

  // The relative luminance of an sRGB pixel.
  static float luminance(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    static const std::array<float, 256> kLinear = [] {
      std::array<float, 256> table{};
      for (std::size_t c = 0; c < table.size(); ++c) {
        const float v = static_cast<float>(c) / 255.0F;
        table[c] = v <= 0.04045F ? v / 12.92F : std::pow((v + 0.055F) / 1.055F, 2.4F);
      }
      return table;
    }();
    return 0.2126F * kLinear[r] + 0.7152F * kLinear[g] + 0.0722F * kLinear[b];
  }

  // One frame, `stride` bytes a row, RGBA. Returns the share of watched
  // pixels failing at this frame: more than kChangesAllowed changes within
  // the last second.
  float add(const std::uint8_t* rgba, int stride) {
    int failing = 0;
    for (int y = 0; y < height_; ++y) {
      const std::uint8_t* row = rgba + static_cast<std::ptrdiff_t>(y) * stride;
      for (int x = 0; x < width_; ++x) {
        const auto i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                       static_cast<std::size_t>(x);
        if (!mask_.empty() && mask_[i] == 0) {
          continue;
        }
        const std::uint8_t* p = row + static_cast<std::ptrdiff_t>(x) * 4;
        const float l = luminance(p[0], p[1], p[2]);
        float& ext = extreme_[i];
        std::int8_t& dir = direction_[i];
        if (ext < 0.0F) {
          ext = l;
        } else {
          const float d = l - ext;
          const std::int8_t way = d > 0.0F ? 1 : -1;
          if (dir == way) {
            // Still going the same way: the turning point moves with it.
            if ((way > 0 && l > ext) || (way < 0 && l < ext)) {
              ext = l;
            }
          } else if (std::fabs(d) >= kStep && std::min(l, ext) < kDarker) {
            // A change the other way (or the first one) large enough.
            std::int32_t* slots = &changes_[i * kKept];
            *std::min_element(slots, slots + kKept) = frame_;
            dir = way;
            ext = l;
          }
        }
        const std::int32_t* slots = &changes_[i * kKept];
        int recent = 0;
        for (int k = 0; k < kKept; ++k) {
          recent += frame_ - slots[k] < fps_ ? 1 : 0;
        }
        failing += recent == kKept ? 1 : 0;
        now_[i] = recent == kKept ? 1 : 0;
      }
    }
    ++frame_;
    const float share =
        watched_ > 0 ? static_cast<float>(failing) / static_cast<float>(watched_) : 0.0F;
    if (share > worst_) {
      worst_ = share;
      worstMap_ = now_;
      worstFrame_ = frame_ - 1;
    }
    return share;
  }

  [[nodiscard]] float worst() const noexcept { return worst_; }
  [[nodiscard]] int frames() const noexcept { return frame_; }
  // Which pixels were failing at the worst frame (1), and which frame.
  [[nodiscard]] const std::vector<std::uint8_t>& worstMap() const noexcept { return worstMap_; }
  [[nodiscard]] int worstFrame() const noexcept { return worstFrame_; }

  // The meter on made-up frames: the whole picture flashing black and white
  // at `hz` for two seconds. Returns the worst share.
  static float synthetic(float hz, int fps) {
    constexpr int kSide = 8;
    FlashMeter meter(kSide, kSide, {}, fps);
    std::vector<std::uint8_t> frame(kSide * kSide * 4);
    for (int f = 0; f < fps * 2; ++f) {
      const bool on = std::fmod(static_cast<float>(f) * hz / static_cast<float>(fps), 1.0F) < 0.5F;
      std::fill(frame.begin(), frame.end(), static_cast<std::uint8_t>(on ? 255 : 0));
      meter.add(frame.data(), kSide * 4);
    }
    return meter.worst();
  }

 private:
  int width_;
  int height_;
  int fps_;
  std::vector<std::uint8_t> mask_;
  std::vector<float> extreme_;
  std::vector<std::int8_t> direction_;
  std::vector<std::int32_t> changes_;
  std::vector<std::uint8_t> now_;
  std::vector<std::uint8_t> worstMap_;
  int worstFrame_ = -1;
  int watched_ = 0;
  std::int32_t frame_ = 0;
  float worst_ = 0.0F;
};

}  // namespace fxreel
