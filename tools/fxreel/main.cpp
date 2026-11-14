// Development tool: plays a scripted scene of the light show headless and
// writes the playfield as a numbered run of PNGs, for review GIFs and
// before/after comparisons. No window is opened. Not part of the game.
//
// Usage: pinball_fxreel <scene> <out_prefix> [options]
//
// scene    play       the scripted flipping of pinball_snapshot, from the plunge
//          hits       a ball sent into the bumper nest, then a slingshot and targets
//          multiball  the Hangar staged with two locked, the third ball shot in
//          jackpot    multiball running, a lit jackpot shot made, then the super
//          drain      a ball let go down the middle between still flippers
//          attract    the attract sequence over the scripted game
//          tilt       the table shoved until it tilts
//          storm      the worst case for photosensitivity: multiball, a jackpot
//                     every 30 steps, a ball sent into the bumper nest every 20
// options  --plain           draw without the light show (the look before it)
//          --reduce-motion   the light show with reduce motion on
//          --every N         a frame every N steps (default 4: 30 a second)
//          --frames N        how many frames (default 90)
//          --skip N          steps before the first frame (default 0)
//          --time            print the mean and worst time to draw a frame
//          --gpu             draw with the platform's own renderer (Metal,
//                            Direct3D, OpenGL) in a hidden window instead of
//                            the software one, and write no frames: each
//                            frame is timed to a one-pixel read back, so the
//                            time includes the GPU's work
//
//          --wcag LIMIT      measure the run with the general flash test of
//                            WCAG 2.3.1 (FlashMeter.hpp) at 60 frames a second
//                            (--every 2), print the worst share of the
//                            playfield flashing more than three times a
//                            second, and fail if it is over LIMIT (0: report)
//
// Writes <out_prefix>_000.png and on; a prefix of - writes nothing. Each frame is the playfield alone at
// one pixel to the unit (768 x 960).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "FlashMeter.hpp"
#include "GameTestAccess.hpp"
#include "core/Game.hpp"
#include "core/LightShow.hpp"
#include "core/Scenes.hpp"
#include "core/ScreenLayout.hpp"
#include "platform/Assets.hpp"
#include "platform/TableRenderer.hpp"

namespace {

using pinball::Game;
using pinball::InputState;
using pinball::Vec2;
using Seam = pinball::GameTestAccess;
namespace table = pinball::table;

struct SurfaceDeleter {
  void operator()(SDL_Surface* s) const noexcept { SDL_DestroySurface(s); }
};
struct RendererDeleter {
  void operator()(SDL_Renderer* r) const noexcept { SDL_DestroyRenderer(r); }
};
struct WindowDeleter {
  void operator()(SDL_Window* w) const noexcept { SDL_DestroyWindow(w); }
};

InputState flipping(std::uint32_t step) noexcept {
  InputState input;
  input.plunger = (step % 1200U) >= 20U && (step % 1200U) < 140U;
  input.leftFlipper = (step / 29U) % 2U == 0U;
  input.rightFlipper = (step / 41U) % 2U == 0U;
  return input;
}

struct Options {
  std::string scene;
  std::string prefix;
  bool plain = false;
  bool reduceMotion = false;
  bool time = false;
  bool gpu = false;
  float wcag = -1.0F;  // Negative: not measured.
  std::string mapPath;  // --wcag-map: where to write the worst frame's failing pixels.
  std::uint32_t every = 4;
  std::uint32_t frames = 90;
  std::uint32_t skip = 0;
};

// The scene: what happens at each step. Returns the input for the step and
// may set the table up by hand before it.
class Script {
 public:
  explicit Script(const Options& options) : options_(options) {}

  bool setUp(Game& game) {
    const std::string& s = options_.scene;
    if (s == "multiball") {
      return pinball::stageScene(game, pinball::Scene::Hangar);
    }
    if (s == "jackpot" || s == "storm") {
      return pinball::stageScene(game, pinball::Scene::Multiball);
    }
    if (s == "drain") {
      // No ball saver: the ball is lost and the next one served.
      Seam::rules(game).setBallSaveEnabled(false);
    }
    if (s == "hits" || s == "drain" || s == "tilt") {
      // A ball in play first.
      for (std::uint32_t i = 0; i < 400 && !game.rules().ballInPlay(); ++i) {
        game.step(flipping(i));
      }
      return game.rules().ballInPlay();
    }
    return s == "play" || s == "attract";
  }

  // A shot the rules have lit for a jackpot, made through their own events
  // as Scenes does. False if none is lit.
  static bool jackpot(Game& game, std::size_t slot) {
    pinball::Rules& rules = Seam::rules(game);
    for (const pinball::Shot shot : {pinball::Shot::Runway, pinball::Shot::Skyway,
                                     pinball::Shot::Briefing, pinball::Shot::Hangar}) {
      if (!rules.jackpotLit(shot) && !(shot == pinball::Shot::Hangar && rules.superJackpotLit())) {
        continue;
      }
      switch (shot) {
        case pinball::Shot::Runway:
          rules.onHole(table::kRunwayHole, slot);
          break;
        case pinball::Shot::Skyway:
          rules.onHole(table::kSkywayHole, slot);
          break;
        case pinball::Shot::Briefing:
          rules.onHole(table::kScoopHole, slot);
          rules.onEject(table::kScoopHole);
          break;
        default:
          rules.onHole(table::kHangarHole, slot);
          rules.onEject(table::kHangarHole);
          break;
      }
      return true;
    }
    return false;
  }

  bool jackpotWanted = false;

  InputState before(Game& game, std::uint32_t t) {
    const std::string& s = options_.scene;
    const std::size_t slot = game.ball().id;
    if (s == "play" || s == "attract") {
      return flipping(t);
    }
    if (s == "hits") {
      // Down onto a bumper, onto the middle one, across into a turbo
      // target, then onto the left slingshot's face.
      if (t == 10) {
        Seam::placeBall(game, slot, {262.0F, 165.0F}, {20.0F, 700.0F});
      } else if (t == 110) {
        Seam::placeBall(game, slot, {352.0F, 185.0F}, {0.0F, 700.0F});
      } else if (t == 200) {
        Seam::placeBall(game, slot, {235.0F, 226.0F}, {-800.0F, 0.0F});
      } else if (t == 270) {
        const auto& sling = table::kSlingshots[0].points;
        const Vec2 face = (sling[0] + sling[2]) * 0.5F;
        Seam::placeBall(game, slot, face + Vec2{45.0F, -25.0F}, {-600.0F, 330.0F});
      }
      return InputState{};
    }
    if (s == "multiball" && t == 20) {
      Seam::placeBall(game, slot, {279.0F, 440.0F}, {0.0F, -900.0F});
      Seam::putInPlay(game, slot);
    }
    if (s == "storm") {
      if (t % 20 == 0) {
        // Down onto the first bumper, into the nest.
        Seam::placeBall(game, slot, {262.0F, 165.0F}, {20.0F, 700.0F});
      }
      if (t % 30 == 0 && !jackpot(game, slot)) {
        jackpotWanted = true;  // None lit: the caller starts the show itself.
      }
      return InputState{};
    }
    if (s == "jackpot" && (t == 20 || t == 200)) {
      jackpot(game, slot);
    }
    if (s == "drain" && t == 10) {
      Seam::placeBall(game, slot, {table::kWidth * 0.5F - 8.0F, 780.0F}, {0.0F, 400.0F});
    }
    if (s == "tilt") {
      InputState input;
      input.nudgeLeft = t % 24 == 0 && t < 200;
      return input;
    }
    return InputState{};
  }

 private:
  const Options& options_;
};

bool parse(int argc, char* argv[], Options& o) {
  if (argc < 3) {
    return false;
  }
  o.scene = argv[1];
  o.prefix = argv[2];
  for (int i = 3; i < argc; ++i) {
    const std::string_view a = argv[i];
    const auto number = [&](std::uint32_t& out) {
      if (i + 1 >= argc) {
        return false;
      }
      out = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
      return true;
    };
    if (a == "--plain") {
      o.plain = true;
    } else if (a == "--reduce-motion") {
      o.reduceMotion = true;
    } else if (a == "--gpu") {
      o.gpu = true;
      o.time = true;
    } else if (a == "--wcag") {
      if (i + 1 >= argc) return false;
      o.wcag = std::strtof(argv[++i], nullptr);
      o.every = 2;
    } else if (a == "--wcag-map") {
      if (i + 1 >= argc) return false;
      o.mapPath = argv[++i];
    } else if (a == "--time") {
      o.time = true;
    } else if (a == "--every") {
      if (!number(o.every)) return false;
    } else if (a == "--frames") {
      if (!number(o.frames)) return false;
    } else if (a == "--skip") {
      if (!number(o.skip)) return false;
    } else {
      return false;
    }
  }
  o.every = std::max<std::uint32_t>(o.every, 1);
  return true;
}

int run(const Options& o) {
  namespace screen = pinball::screen;
  const screen::Layout layout =
      screen::computeLayout({1280.0F, 960.0F, 0.0F, 0.0F, 0.0F, 0.0F}, screen::Idiom::Desktop);
  pinball::AssetLoader assets = pinball::AssetLoader::fromExecutableLocation();
  if (!assets.verifyAll()) {
    std::fputs("asset integrity check failed\n", stderr);
    return EXIT_FAILURE;
  }
  const std::unique_ptr<SDL_Surface, SurfaceDeleter> canvas(SDL_CreateSurface(
      static_cast<int>(layout.canvasWidth), static_cast<int>(layout.canvasHeight),
      SDL_PIXELFORMAT_RGBA32));
  const std::unique_ptr<SDL_Surface, SurfaceDeleter> frame(SDL_CreateSurface(
      static_cast<int>(table::kWidth), static_cast<int>(table::kHeight), SDL_PIXELFORMAT_RGBA32));
  if (!canvas || !frame) {
    return EXIT_FAILURE;
  }
  int status = EXIT_SUCCESS;
  {
    std::unique_ptr<SDL_Window, WindowDeleter> window;
    std::unique_ptr<SDL_Renderer, RendererDeleter> renderer;
    if (o.gpu) {
      if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return EXIT_FAILURE;
      }
      window.reset(SDL_CreateWindow("fxreel", static_cast<int>(layout.canvasWidth),
                                    static_cast<int>(layout.canvasHeight), SDL_WINDOW_HIDDEN));
      if (window) {
        renderer.reset(SDL_CreateRenderer(window.get(), nullptr));
      }
      if (renderer) {
        SDL_SetRenderLogicalPresentation(renderer.get(), static_cast<int>(layout.canvasWidth),
                                         static_cast<int>(layout.canvasHeight),
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
        std::printf("renderer: %s\n", SDL_GetRendererName(renderer.get()));
      }
    } else {
      renderer.reset(SDL_CreateSoftwareRenderer(canvas.get()));
    }
    if (!renderer) {
      std::fprintf(stderr, "no renderer: %s\n", SDL_GetError());
      return EXIT_FAILURE;
    }
    SDL_Renderer* const r = renderer.get();
    const auto drawer =
        pinball::TableRenderer::create(r, assets, pinball::TextureSet::Desktop);
    if (!drawer) {
      return EXIT_FAILURE;
    }
    // Everything the light show draws, whatever the renderer.
    drawer->setEffectsEconomy(false);
    Game game;
    pinball::LightShow show;
    show.setReduceMotion(o.reduceMotion);
    Script script(o);
    if (!script.setUp(game)) {
      std::fputs("the scene could not be set up\n", stderr);
      return EXIT_FAILURE;
    }
    show.observe(game);
    show.setAttract(o.scene == "attract");
    double total = 0.0;
    double worst = 0.0;
    double cpu = 0.0;  // Processor time: steadier than the clock on a busy machine.
    std::uint32_t t = 0;
    const std::uint32_t steps = o.skip + o.frames * o.every;
    // The flash meter: the playfield at half a pixel to the unit, the pixels
    // inside the shell watched.
    constexpr int kMeterScale = 2;
    const int meterW = static_cast<int>(table::kWidth) / kMeterScale;
    const int meterH = static_cast<int>(table::kHeight) / kMeterScale;
    std::unique_ptr<fxreel::FlashMeter> meter;
    const std::unique_ptr<SDL_Surface, SurfaceDeleter> small(
        SDL_CreateSurface(meterW, meterH, SDL_PIXELFORMAT_RGBA32));
    if (o.wcag >= 0.0F) {
      if (fxreel::FlashMeter::synthetic(10.0F, 60) < 0.99F ||
          fxreel::FlashMeter::synthetic(2.0F, 60) > 0.0F) {
        std::fputs("the flash meter fails its own check\n", stderr);
        return EXIT_FAILURE;
      }
      std::vector<std::uint8_t> mask(static_cast<std::size_t>(meterW * meterH));
      for (int y = 0; y < meterH; ++y) {
        for (int x = 0; x < meterW; ++x) {
          const float px = (static_cast<float>(x) + 0.5F) * kMeterScale;
          const float py = (static_cast<float>(y) + 0.5F) * kMeterScale;
          bool in = false;
          for (std::size_t a = 0, b = table::kShell.size() - 1; a < table::kShell.size(); b = a++) {
            const Vec2 p = table::kShell[a];
            const Vec2 q = table::kShell[b];
            if ((p.y > py) != (q.y > py) && px < (q.x - p.x) * (py - p.y) / (q.y - p.y) + p.x) {
              in = !in;
            }
          }
          mask[static_cast<std::size_t>(y * meterW + x)] = in ? 1 : 0;
        }
      }
      meter = std::make_unique<fxreel::FlashMeter>(meterW, meterH, std::move(mask), 60);
    }
    for (std::uint32_t frameIndex = 0; t < steps; ++t) {
      game.step(script.before(game, t));
      if (script.jackpotWanted) {
        script.jackpotWanted = false;
        show.startShow(pinball::fx::Show::Jackpot, game.ball().position);
      }
      show.observe(game);
      if (t < o.skip || (t - o.skip) % o.every != 0) {
        continue;
      }
      const auto start = std::chrono::steady_clock::now();
      const std::clock_t cpuStart = std::clock();
      SDL_SetRenderDrawColor(r, 18, 18, 58, 255);
      SDL_RenderClear(r);
      pinball::DrawOptions options;
      options.lightShow = o.plain ? nullptr : &show;
      drawer->draw(r, game, 1.0F, layout, options);
      SDL_FlushRenderer(r);
      if (o.gpu) {
        // Wait for the GPU: read one pixel back.
        const SDL_Rect one{0, 0, 1, 1};
        SDL_DestroySurface(SDL_RenderReadPixels(r, &one));
      }
      const double ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
              .count();
      total += ms;
      cpu += 1000.0 * static_cast<double>(std::clock() - cpuStart) / CLOCKS_PER_SEC;
      worst = std::max(worst, ms);
      if (o.gpu) {
        SDL_RenderPresent(r);
        continue;
      }
      const SDL_Rect field{static_cast<int>(std::lround(layout.playfield.x)),
                           static_cast<int>(std::lround(layout.playfield.y)),
                           static_cast<int>(table::kWidth), static_cast<int>(table::kHeight)};
      SDL_BlitSurface(canvas.get(), &field, frame.get(), nullptr);
      if (meter) {
        SDL_BlitSurfaceScaled(frame.get(), nullptr, small.get(), nullptr, SDL_SCALEMODE_LINEAR);
        meter->add(static_cast<const std::uint8_t*>(small->pixels), small->pitch);
      }
      if (o.prefix == "-") {
        ++frameIndex;
        continue;
      }
      char name[32];
      std::snprintf(name, sizeof name, "_%03u.png", frameIndex++);
      if (!SDL_SavePNG(frame.get(), (o.prefix + name).c_str())) {
        std::fprintf(stderr, "cannot write a frame: %s\n", SDL_GetError());
        status = EXIT_FAILURE;
        break;
      }
    }
    if (o.time) {
      std::printf("%s%s: %u frames, draw mean %.2f ms, worst %.2f ms, processor %.2f ms a frame (%s renderer)\n",
                  o.scene.c_str(), o.plain ? " (plain)" : "", o.frames,
                  total / std::max<double>(o.frames, 1.0), worst,
                  cpu / std::max<double>(o.frames, 1.0), SDL_GetRendererName(r));
    }
    if (meter) {
      std::printf("%s%s%s: WCAG 2.3.1 general flash, worst share of the playfield over 3 flashes a "
                  "second: %.2f%% (%d frames at 60 a second)\n",
                  o.scene.c_str(), o.plain ? " (plain)" : "", o.reduceMotion ? " (reduce motion)" : "",
                  static_cast<double>(meter->worst()) * 100.0, meter->frames());
      if (!o.mapPath.empty() && !meter->worstMap().empty()) {
        // The worst frame's failing pixels, white on black, at the meter's size.
        const std::unique_ptr<SDL_Surface, SurfaceDeleter> map(
            SDL_CreateSurface(meterW, meterH, SDL_PIXELFORMAT_RGBA32));
        auto* px = static_cast<std::uint8_t*>(map->pixels);
        for (int y = 0; y < meterH; ++y) {
          for (int x = 0; x < meterW; ++x) {
            const std::uint8_t v = meter->worstMap()[static_cast<std::size_t>(y * meterW + x)] ? 255 : 0;
            std::uint8_t* q = px + y * map->pitch + x * 4;
            q[0] = q[1] = q[2] = v;
            q[3] = 255;
          }
        }
        SDL_SavePNG(map.get(), o.mapPath.c_str());
        std::printf("worst frame %d\n", meter->worstFrame());
      }
      if (o.wcag > 0.0F && meter->worst() > o.wcag) {
        std::fprintf(stderr, "over the limit of %.2f%%\n", static_cast<double>(o.wcag) * 100.0);
        status = EXIT_FAILURE;
      }
    }
    std::printf("%s: score %llu, balls %zu, show bursts %u, large flashes %u (held %u)\n",
                o.scene.c_str(), static_cast<unsigned long long>(game.rules().score()),
                game.ballCount(), show.burstsStarted(), show.largeFlashes(),
                show.largeFlashesHeld());
  }
  return status;
}

}  // namespace

int main(int argc, char* argv[]) {
  Options options;
  if (!parse(argc, argv, options)) {
    std::fputs("usage: pinball_fxreel <play|hits|multiball|jackpot|drain|attract|tilt|storm> <out_prefix>"
               " [--plain] [--reduce-motion] [--every N] [--frames N] [--skip N] [--time] [--gpu]\n",
               stderr);
    return EXIT_FAILURE;
  }
  const int status = run(options);
  SDL_Quit();
  return status;
}
