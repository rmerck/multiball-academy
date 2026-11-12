#pragma once

#include <algorithm>
#include <cmath>

namespace pinball {

// Converts variable frame times into a whole number of fixed simulation steps.
// Physics must advance in constant increments to stay deterministic and stable.
class FixedTimestep {
 public:
  // stepSeconds must be in (0, 1); maxStepsPerFrame must be >= 1.
  // Invalid values fall back to 120 Hz and 1 step so the object is always usable.
  constexpr FixedTimestep(double stepSeconds, int maxStepsPerFrame) noexcept
      : step_(stepSeconds > 0.0 && stepSeconds < 1.0 ? stepSeconds : 1.0 / 120.0),
        maxSteps_(maxStepsPerFrame >= 1 ? maxStepsPerFrame : 1) {}

  // Adds elapsed wall time and returns how many fixed steps to run now.
  // Non-finite or negative input counts as zero. If the frame took at least
  // maxSteps_ steps, exactly maxSteps_ are returned and the backlog is dropped.
  int advance(double frameSeconds) noexcept {
    if (!std::isfinite(frameSeconds) || frameSeconds < 0.0) {
      frameSeconds = 0.0;
    }
    accumulator_ += frameSeconds;

    // At or past the cap: run exactly maxSteps_ and drop the backlog. Decided
    // against the budget directly so rounding in the division cannot lose a step.
    const double budget = step_ * static_cast<double>(maxSteps_);
    if (accumulator_ >= budget) {
      accumulator_ = 0.0;
      return maxSteps_;
    }

    // One division instead of repeated subtraction, which drifts.
    const double whole =
        std::min(std::floor(accumulator_ / step_), static_cast<double>(maxSteps_ - 1));
    accumulator_ -= whole * step_;
    // Keep alpha() inside [0, 1) whatever the rounding did.
    accumulator_ = std::clamp(accumulator_, 0.0, std::nextafter(step_, 0.0));
    return static_cast<int>(whole);
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
