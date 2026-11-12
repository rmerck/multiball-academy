#include <cmath>
#include <limits>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/FixedTimestep.hpp"
#include "core/LoopPolicy.hpp"

void testFixedTimestep() {
  using pinball::FixedTimestep;
  const double step = 1.0 / 120.0;

  {  // Real cadences: 60 Hz frames give exactly 2 steps, 120 Hz frames exactly 1.
    FixedTimestep at60(step, 8);
    FixedTimestep at120(step, 8);
    long total60 = 0;
    long total120 = 0;
    for (int i = 0; i < 36000; ++i) {
      total60 += at60.advance(1.0 / 60.0);
      total120 += at120.advance(1.0 / 120.0);
    }
    CHECK(std::labs(total60 - 72000) <= 1);
    CHECK(std::labs(total120 - 36000) <= 1);
  }
  {  // A step of one second or more is rejected.
    FixedTimestep ts(1.0, 4);
    CHECK(ts.step() < 1.0);
  }
  {  // Slightly more than two steps produces two.
    FixedTimestep ts(step, 8);
    CHECK(ts.advance(step * 2.0 + 1e-9) == 2);
    CHECK(ts.alpha() >= 0.0 && ts.alpha() < 1.0);
  }
  {  // Sub-step frames accumulate.
    FixedTimestep ts(step, 8);
    CHECK(ts.advance(step * 0.6) == 0);
    CHECK(ts.advance(step * 0.6) == 1);
    CHECK(std::fabs(ts.alpha() - 0.2) < 1e-6);
  }
  {  // Long frames are capped and the backlog is dropped.
    FixedTimestep ts(step, 8);
    CHECK(ts.advance(10.0) == 8);
    CHECK(ts.alpha() < 1.0);
    CHECK(ts.advance(0.0) == 0);
  }
  {  // Hostile input never produces steps or corrupts state.
    FixedTimestep ts(step, 8);
    CHECK(ts.advance(-1.0) == 0);
    CHECK(ts.advance(std::numeric_limits<double>::quiet_NaN()) == 0);
    CHECK(ts.advance(std::numeric_limits<double>::infinity()) == 0);
    CHECK(ts.alpha() == 0.0);
    CHECK(ts.advance(step + 1e-9) == 1);
  }
  {  // Invalid construction falls back to safe defaults.
    FixedTimestep ts(0.0, 0);
    CHECK(ts.step() > 0.0);
    CHECK(ts.maxSteps() == 1);
    FixedTimestep nan(std::numeric_limits<double>::quiet_NaN(), -3);
    CHECK(nan.step() > 0.0);
    CHECK(nan.maxSteps() == 1);
  }

  {  // The cap is exact for every rate and cap, not just the shipped pair.
    const double rates[] = {30.0, 50.0, 60.0, 100.0, 120.0, 144.0, 240.0, 1000.0};
    for (const double hz : rates) {
      for (int cap = 1; cap <= 64; ++cap) {
        FixedTimestep ts(1.0 / hz, cap);
        CHECK(ts.advance(10.0) == cap);
        CHECK(ts.alpha() == 0.0);
        CHECK(ts.advance(0.0) == 0);
      }
    }
  }
  {  // Steps are conserved over many irregular frames, and alpha stays in range.
    FixedTimestep ts(step, 8);
    long total = 0;
    double elapsed = 0.0;
    for (int i = 0; i < 100000; ++i) {
      const double frame = 0.004 + 0.00001 * static_cast<double>(i % 977);
      elapsed += frame;
      total += ts.advance(frame);
      if (ts.alpha() < 0.0 || ts.alpha() >= 1.0) {
        CHECK(false && "alpha out of range");
        break;
      }
    }
    const double expected = elapsed / step;
    CHECK(std::fabs(static_cast<double>(total) - expected) <= 2.0);
  }

  // The main loop's pause clock and idle decision (core/LoopPolicy.hpp).
  {  // Running frames pass through unchanged, the very first one included.
    pinball::PauseClock clock;
    CHECK(clock.simulated(0.016, false) == 0.016);
    CHECK(clock.simulated(0.008, false) == 0.008);
  }
  {  // A halt gives nothing, however long, and neither does the frame that ends it.
    pinball::PauseClock clock;
    CHECK(clock.simulated(0.016, false) == 0.016);
    CHECK(clock.simulated(0.016, true) == 0.0);
    CHECK(clock.simulated(30.0, true) == 0.0);
    CHECK(clock.simulated(30.0, false) == 0.0);  // Its interval is the wait.
    CHECK(clock.simulated(0.016, false) == 0.016);
  }
  {  // Starting halted, and halting again on the frame after a resume.
    pinball::PauseClock clock;
    CHECK(clock.simulated(5.0, true) == 0.0);
    CHECK(clock.simulated(5.0, false) == 0.0);
    CHECK(clock.simulated(0.1, true) == 0.0);
    CHECK(clock.simulated(0.1, false) == 0.0);
    CHECK(clock.simulated(0.1, false) == 0.1);
  }
  {  // A long pause never turns into simulation steps.
    pinball::PauseClock clock;
    FixedTimestep ts(step, 8);
    CHECK(ts.advance(clock.simulated(1.0 / 60.0, false)) == 2);
    for (int i = 0; i < 100; ++i) {
      CHECK(clock.simulated(0.1, true) == 0.0);
    }
    CHECK(ts.advance(clock.simulated(10.0, false)) == 0);
    CHECK(ts.advance(clock.simulated(1.0 / 60.0, false)) == 2);
  }
  {  // The halted loop sleeps only when nothing needs it awake.
    using pinball::kMaxHaltedBusyFrames;
    using pinball::sleepsWhileHalted;
    CHECK(sleepsWhileHalted(true, false, false, 1));
    // Never while the game runs, whatever else holds.
    CHECK(!sleepsWhileHalted(false, false, false, 0));
    CHECK(!sleepsWhileHalted(false, false, true, kMaxHaltedBusyFrames + 1));
    // Never in a timed run: its frames must keep counting.
    CHECK(!sleepsWhileHalted(true, true, false, 1));
    CHECK(!sleepsWhileHalted(true, true, false, kMaxHaltedBusyFrames + 1));
    // A sound's tail keeps it awake, but not for ever.
    CHECK(!sleepsWhileHalted(true, false, true, 1));
    CHECK(!sleepsWhileHalted(true, false, true, kMaxHaltedBusyFrames));
    CHECK(sleepsWhileHalted(true, false, true, kMaxHaltedBusyFrames + 1));
  }
}
