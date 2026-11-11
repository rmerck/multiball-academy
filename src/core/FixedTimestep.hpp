#pragma once

#include <algorithm>
#include <cmath>

namespace pinball {

// Converts variable frame times into a whole number of fixed simulation steps.
// Physics must advance in constant increments to stay deterministic and stable.
class FixedTimestep {
 public:
  // stepSeconds must be finite and > 0; maxStepsPerFrame must be >= 1.
  // Invalid values fall back to 120 Hz and 1 step so the object is always usable.
  constexpr FixedTimestep(double stepSeconds, int maxStepsPerFrame) noexcept
      : step_(stepSeconds > 0.0 && stepSeconds < 1.0 ? stepSeconds : 1.0 / 120.0),
        maxSteps_(maxStepsPerFrame >= 1 ? maxStepsPerFrame : 1) {}

  // Adds elapsed wall time and returns how many fixed steps to run now.
  // Non-finite or negative input counts as zero. If the frame took longer than
  // maxSteps_ steps, the backlog is dropped instead of spiralling.
  int advance(double frameSeconds) noexcept {
    if (!std::isfinite(frameSeconds) || frameSeconds < 0.0) {
      frameSeconds = 0.0;
    }
    const double budget = step_ * static_cast<double>(maxSteps_);
    accumulator_ = std::min(accumulator_ + frameSeconds, budget);

    int steps = 0;
    while (accumulator_ >= step_ && steps < maxSteps_) {
      accumulator_ -= step_;
      ++steps;
    }
    return steps;
  }

  // Fraction of a step left over, in [0, 1). Used to interpolate rendering.
  double alpha() const noexcept { return accumulator_ / step_; }

  double step() const noexcept { return step_; }
  int maxSteps() const noexcept { return maxSteps_; }

 private:
  double step_;
  int maxSteps_;
  double accumulator_ = 0.0;
};

}  // namespace pinball
