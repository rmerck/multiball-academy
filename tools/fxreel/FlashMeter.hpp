#pragma once

#include <algorithm>
#include <initializer_list>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

// The general flash test of WCAG 2.3.1, per pixel, over a run of frames at a
// fixed rate: for each pixel the relative luminance is followed, and a
// change is counted each time it moves the other way from its last turning
// point by at least kStep (10 percent of the maximum) while the darker of
// the two is under kDarker (0.80). A pixel fails at a frame when it has made
// more than kChangesAllowed changes (three flashes, each a pair of opposite
// changes) within the last second.
//
// WCAG judges the area against a 10 degree field of view: a general flash
// fails when the failing pixels cover more than 25 percent of any such
// field. So the meter is given square windows, each the size of a 10 degree
// field at one device's scale and viewing distance (in meter pixels), and
// reports for each the worst share of a window failing at once anywhere on
// the table, worst over the run. The share of the whole table failing at
// once is kept as a second number.
//
// Red flashes are counted the same way on their own (WCAG's saturated red:
// R / (R + G + B) at least 0.8 in either state, and (R - G - B) x 320 moving
// by more than 20, in sRGB from 0 to 1).
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
  FlashMeter(int width, int height, std::vector<std::uint8_t> mask, int fps,
             std::vector<int> windows = {})
      : width_(width), height_(height), fps_(fps), mask_(std::move(mask)),
        extreme_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), -1.0F),
        direction_(extreme_.size(), 0),
        changes_(extreme_.size() * kKept, -1'000'000), now_(extreme_.size(), 0),
        redExtreme_(extreme_.size(), -1.0F), redDirection_(extreme_.size(), 0),
        redChanges_(extreme_.size() * kKept, -1'000'000), redWasSaturated_(extreme_.size(), 0),
        windows_(std::move(windows)), worstWindow_(windows_.size(), 0.0F),
        integral_(static_cast<std::size_t>(width + 1) * static_cast<std::size_t>(height + 1), 0) {
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
    int redFailing = 0;
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
        // The red flash, followed apart.
        const float sum = static_cast<float>(p[0]) + static_cast<float>(p[1]) +
                          static_cast<float>(p[2]);
        const float ratio = sum > 0.0F ? static_cast<float>(p[0]) / sum : 0.0F;
        const float red =
            std::max(0.0F, (static_cast<float>(p[0]) - static_cast<float>(p[1]) -
                            static_cast<float>(p[2])) / 255.0F * 320.0F);
        float& rext = redExtreme_[i];
        std::int8_t& rdir = redDirection_[i];
        if (rext < 0.0F) {
          rext = red;
          redWasSaturated_[i] = ratio >= 0.8F ? 1 : 0;
        } else {
          const float d = red - rext;
          const std::int8_t way = d > 0.0F ? 1 : -1;
          if (rdir == way) {
            if ((way > 0 && red > rext) || (way < 0 && red < rext)) {
              rext = red;
              redWasSaturated_[i] = std::max<std::uint8_t>(redWasSaturated_[i], ratio >= 0.8F ? 1 : 0);
            }
          } else if (std::fabs(d) > 20.0F && (ratio >= 0.8F || redWasSaturated_[i] != 0)) {
            std::int32_t* rs = &redChanges_[i * kKept];
            *std::min_element(rs, rs + kKept) = frame_;
            rdir = way;
            rext = red;
            redWasSaturated_[i] = ratio >= 0.8F ? 1 : 0;
          }
        }
        const std::int32_t* rs = &redChanges_[i * kKept];
        int redRecent = 0;
        for (int k = 0; k < kKept; ++k) {
          redRecent += frame_ - rs[k] < fps_ ? 1 : 0;
        }
        redFailing += redRecent == kKept ? 1 : 0;
      }
    }
    ++frame_;
    const float share =
        watched_ > 0 ? static_cast<float>(failing) / static_cast<float>(watched_) : 0.0F;
    worstRed_ = std::max(worstRed_, watched_ > 0 ? static_cast<float>(redFailing) /
                                                       static_cast<float>(watched_)
                                                 : 0.0F);
    measureWindows();
    if (share > worst_) {
      worst_ = share;
      if (windows_.empty()) {
        worstMap_ = now_;
        worstFrame_ = frame_ - 1;
      }
    }
    return share;
  }

  [[nodiscard]] float worst() const noexcept { return worst_; }
  // The worst share of each window (in the order given) failing at once.
  [[nodiscard]] float worstWindow(std::size_t k) const noexcept {
    return k < worstWindow_.size() ? worstWindow_[k] : 0.0F;
  }
  // The worst share of the table in a red flash.
  [[nodiscard]] float worstRed() const noexcept { return worstRed_; }
  [[nodiscard]] int frames() const noexcept { return frame_; }
  // Where the first window was worst (its top left), in meter pixels.
  [[nodiscard]] std::pair<int, int> worstAt() const noexcept { return worstAt_; }
  // Which pixels were failing at the worst frame (1), and which frame: the
  // first window's worst when windows are given, else the whole table's.
  [[nodiscard]] const std::vector<std::uint8_t>& worstMap() const noexcept { return worstMap_; }
  [[nodiscard]] int worstFrame() const noexcept { return worstFrame_; }

  // A window of `side` pixels at every place on the frame (four pixels
  // apart): the most failing pixels in one, over its area.
  void measureWindows() {
    if (windows_.empty()) {
      return;
    }
    const auto w1 = static_cast<std::size_t>(width_) + 1;
    for (int y = 0; y < height_; ++y) {
      std::int32_t row = 0;
      for (int x = 0; x < width_; ++x) {
        row += now_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                    static_cast<std::size_t>(x)];
        integral_[(static_cast<std::size_t>(y) + 1) * w1 + static_cast<std::size_t>(x) + 1] =
            integral_[static_cast<std::size_t>(y) * w1 + static_cast<std::size_t>(x) + 1] + row;
      }
    }
    for (std::size_t k = 0; k < windows_.size(); ++k) {
      const int side = std::min({windows_[k], width_, height_});
      std::int32_t most = 0;
      int mx = 0;
      int my = 0;
      for (int y = 0; y + side <= height_; y += 4) {
        for (int x = 0; x + side <= width_; x += 4) {
          const auto at = [&](int xx, int yy) {
            return integral_[static_cast<std::size_t>(yy) * w1 + static_cast<std::size_t>(xx)];
          };
          const std::int32_t n = at(x + side, y + side) - at(x, y + side) - at(x + side, y) + at(x, y);
          if (n > most) {
            most = n;
            mx = x;
            my = y;
          }
        }
      }
      // Over the field's own area, even where the table is smaller.
      const float field = static_cast<float>(windows_[k]) * static_cast<float>(windows_[k]);
      const float share = static_cast<float>(most) / field;
      if (share > worstWindow_[k]) {
        worstWindow_[k] = share;
        if (k == 0) {
          worstMap_ = now_;
          worstFrame_ = frame_ - 1;
          worstAt_ = {mx, my};
        }
      }
    }
  }

  // The meter on made-up frames: the whole picture flashing black and white
  // at `hz` for two seconds. Returns the worst share.
  // red: the flash is saturated red on black instead. Returns the worst
  // share (of the red flash, when red).
  static float synthetic(float hz, int fps, bool red = false) {
    constexpr int kSide = 8;
    FlashMeter meter(kSide, kSide, {}, fps);
    std::vector<std::uint8_t> frame(kSide * kSide * 4);
    for (int f = 0; f < fps * 2; ++f) {
      const bool on = std::fmod(static_cast<float>(f) * hz / static_cast<float>(fps), 1.0F) < 0.5F;
      for (std::size_t i = 0; i < frame.size(); i += 4) {
        frame[i] = on ? 255 : 0;
        frame[i + 1] = on && !red ? 255 : 0;
        frame[i + 2] = on && !red ? 255 : 0;
        frame[i + 3] = 255;
      }
      meter.add(frame.data(), kSide * 4);
    }
    return red ? meter.worstRed() : meter.worst();
  }
  // A flash over a quarter of a 32-pixel frame: a 16-pixel window measures
  // it whole, a 32-pixel one at a quarter.
  static float syntheticWindow(int window) {
    constexpr int kSide = 32;
    FlashMeter meter(kSide, kSide, {}, 60, {window});
    std::vector<std::uint8_t> frame(kSide * kSide * 4, 0);
    for (int f = 0; f < 120; ++f) {
      const bool on = (f / 6) % 2 == 0;  // Five a second.
      for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
          const bool lit = on && x < kSide / 2 && y < kSide / 2;
          std::uint8_t* p = &frame[static_cast<std::size_t>((y * kSide + x) * 4)];
          p[0] = p[1] = p[2] = lit ? 255 : 0;
          p[3] = 255;
        }
      }
      meter.add(frame.data(), kSide * 4);
    }
    return meter.worstWindow(0);
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
  std::pair<int, int> worstAt_{};
  std::vector<float> redExtreme_;
  std::vector<std::int8_t> redDirection_;
  std::vector<std::int32_t> redChanges_;
  std::vector<std::uint8_t> redWasSaturated_;
  std::vector<int> windows_;
  std::vector<float> worstWindow_;
  std::vector<std::int32_t> integral_;
  float worstRed_ = 0.0F;
  int watched_ = 0;
  std::int32_t frame_ = 0;
  float worst_ = 0.0F;
};

}  // namespace fxreel
