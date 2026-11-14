#include "core/FrontLayout.hpp"

#include <algorithm>

#include "core/OverlayLayout.hpp"

namespace pinball::screen {

namespace {

constexpr float kButton = kMinHitUnits;
constexpr float kMargin = 32.0F;
constexpr float kNone = 0.0F;

constexpr Rect centredAt(float cx, float y, float w, float h) noexcept {
  return {cx - w * 0.5F, y, w, h};
}

constexpr float bottomOf(const Rect& r) noexcept { return r.y + r.h; }

// Keeps r inside outer, moving it, never resizing it.
Rect keptIn(Rect r, const Rect& outer) noexcept {
  r.x = std::clamp(r.x, outer.x, std::max(outer.x, outer.x + outer.w - r.w));
  r.y = std::clamp(r.y, outer.y, std::max(outer.y, outer.y + outer.h - r.h));
  return r;
}

bool classic(const Layout& layout) noexcept {
  return layout.composition == Composition::Classic;
}

}  // namespace

Rect compositionArea(const Layout& layout) noexcept {
  if (classic(layout)) {
    return {layout.playfield.x, layout.playfield.y, kClassicWidth, kClassicHeight};
  }
  return {layout.hud.x, layout.hud.y, kStackWidth,
          layout.hud.h + layout.playfield.h + layout.deck.h};
}

TitleLayout titleLayout(const Layout& layout, const FrontConfig& config) noexcept {
  TitleLayout title{};
  title.area = compositionArea(layout);
  const Rect& area = title.area;
  const float cx = area.x + area.w * 0.5F;
  const bool wide = classic(layout);
  const std::size_t shown = config.continueShown ? kTitleItemCount : kTitleItemCount - 1;
  const float gap = wide ? (config.continueShown ? 12.0F : 16.0F) : 18.0F;
  const float logoW = wide ? 528.0F : 672.0F;
  const float logoH = logoW * 0.25F;
  const float itemsH =
      static_cast<float>(shown) * kButton + static_cast<float>(shown - 1) * gap;
  const float content = logoH + 20.0F + 64.0F + 28.0F + itemsH + 16.0F + 30.0F;
  // The spare height goes a third above and two thirds below.
  const float top = area.y + std::max((area.h - content) / 3.0F, wide ? 20.0F : 40.0F);
  title.logo = centredAt(cx, top, logoW, logoH);
  title.marquee = centredAt(cx, bottomOf(title.logo) + 20.0F, 640.0F, 64.0F);
  float y = bottomOf(title.marquee) + 28.0F;
  for (std::size_t i = 0; i < kTitleItemCount; ++i) {
    if (i == static_cast<std::size_t>(TitleItem::Continue) && !config.continueShown) {
      title.items[i] = {cx, y, kNone, kNone};
      continue;
    }
    title.items[i] = centredAt(cx, y, 520.0F, kButton);
    y += kButton + gap;
  }
  title.footer = centredAt(cx, y - gap + 16.0F, area.w - 2.0F * kMargin, 30.0F);
  return title;
}

PageLayout pageLayout(const Layout& layout, FrontPage page, bool guideButton) noexcept {
  PageLayout p{};
  const Rect area = compositionArea(layout);
  if (classic(layout)) {
    p.box = {area.x + 140.0F, area.y + 24.0F, 1000.0F, area.h - 48.0F};
  } else {
    const float h = std::min(area.h - 48.0F, 1200.0F);
    p.box = {area.x + 24.0F, area.y + (area.h - h) * 0.5F, area.w - 48.0F, h};
  }
  const Rect& box = p.box;
  const float cx = box.x + box.w * 0.5F;
  p.heading = {box.x + kMargin, box.y + kMargin, box.w - 2.0F * kMargin, 64.0F};
  const float rowY = bottomOf(box) - kMargin - kButton;
  p.back = centredAt(cx, rowY, 280.0F, kButton);
  float bodyBottom = rowY - 24.0F;
  if (page == FrontPage::HowTo) {
    p.previous = {box.x + kMargin, rowY, 120.0F, kButton};
    p.next = {box.x + box.w - kMargin - 120.0F, rowY, 120.0F, kButton};
    if (guideButton) {
      p.guide = {box.x + kMargin, rowY - 16.0F - kButton, box.w - 2.0F * kMargin, kButton};
      bodyBottom = p.guide.y - 16.0F;
    }
  }
  const float bodyTop = bottomOf(p.heading) + 24.0F;
  p.body = {box.x + 40.0F, bodyTop, box.w - 80.0F, std::max(bodyBottom - bodyTop, 0.0F)};
  return p;
}

SettingsLayout settingsLayout(const Layout& layout, const FrontConfig& config) noexcept {
  SettingsLayout s{};
  s.page = pageLayout(layout, FrontPage::Settings);
  const Rect& body = s.page.body;
  const float gap = 16.0F;
  float y = body.y;
  for (std::size_t i = 0; i < kSettingsRowCount; ++i) {
    const auto row = static_cast<SettingsRow>(i);
    if (row == SettingsRow::Back) {
      s.rows[i] = s.page.back;
      continue;
    }
    if (row == SettingsRow::Haptics && !config.hapticsShown) {
      s.rows[i] = {body.x, y, kNone, kNone};
      continue;
    }
    s.rows[i] = {body.x, y, body.w, kButton};
    y += kButton + gap;
  }
  const Rect& volume = s.rows[static_cast<std::size_t>(SettingsRow::Volume)];
  s.volumeDown = {volume.x, volume.y, 120.0F, kButton};
  s.volumeUp = {volume.x + volume.w - 120.0F, volume.y, 120.0F, kButton};
  return s;
}

GuideLayout guideLayout(const Layout& layout) noexcept {
  GuideLayout g{};
  const Rect& field = layout.playfield;
  const Rect& safe = layout.safe;
  // In the lower half of the table, one label inside each flipper zone.
  const float labelW = 330.0F;
  const float labelH = 150.0F;
  const float y = field.y + 560.0F;
  const Rect& left = layout.flipperLeftZone;
  const Rect& right = layout.flipperRightZone;
  const float leftEdge = std::max(left.x, field.x) + 24.0F;
  g.leftLabel = keptIn({leftEdge, y, labelW, labelH}, safe);
  const float rightEdge = std::min(right.x + right.w, field.x + field.w) - 24.0F - labelW;
  g.rightLabel = keptIn({std::max(rightEdge, right.x + 16.0F), y, labelW, labelH}, safe);
  // Above the centre button and each nudge button.
  const Rect& centre = layout.centreButton;
  g.centreLabel = keptIn(
      centredAt(centre.x + centre.w * 0.5F, centre.y - 12.0F - 110.0F, 300.0F, 110.0F), safe);
  const auto above = [&](const Rect& button) {
    return keptIn(centredAt(button.x + button.w * 0.5F, g.centreLabel.y, 150.0F, 110.0F), safe);
  };
  g.nudgeLeftLabel = above(layout.nudgeLeftButton);
  g.nudgeRightLabel = above(layout.nudgeRightButton);
  // The nudge labels sit outside the centre's, side by side with it.
  g.nudgeLeftLabel.x = std::min(g.nudgeLeftLabel.x, g.centreLabel.x - 8.0F - g.nudgeLeftLabel.w);
  g.nudgeRightLabel.x = std::max(g.nudgeRightLabel.x, g.centreLabel.x + g.centreLabel.w + 8.0F);
  g.nudgeLeftLabel = keptIn(g.nudgeLeftLabel, safe);
  g.nudgeRightLabel = keptIn(g.nudgeRightLabel, safe);
  // Beside the pause button, away from the screen's edge.
  const Rect& pause = layout.pauseButton;
  g.pauseLabel = keptIn({pause.x + pause.w + 8.0F, pause.y + (pause.h - 60.0F) * 0.5F, 260.0F,
                         60.0F},
                        safe);
  g.card = keptIn(centredAt(field.x + field.w * 0.5F, field.y + 190.0F, 620.0F, 230.0F), safe);
  return g;
}

FrontTarget frontTarget(const Layout& layout, const FrontConfig& config, const FrontState& state,
                        float x, float y) noexcept {
  using Kind = FrontTarget::Kind;
  switch (state.page) {
    case FrontPage::Title: {
      const TitleLayout title = titleLayout(layout, config);
      for (std::size_t i = 0; i < title.items.size(); ++i) {
        if (contains(title.items[i], x, y)) {
          return {Kind::Item, static_cast<std::uint8_t>(i)};
        }
      }
      return {};
    }
    case FrontPage::Scores:
    case FrontPage::HowTo: {
      const bool guide = state.page == FrontPage::HowTo && config.guideShown &&
                         state.howToPage == 0;
      const PageLayout page = pageLayout(layout, state.page, guide);
      if (contains(page.back, x, y)) {
        return {Kind::Back, 0};
      }
      if (contains(page.previous, x, y)) {
        return {Kind::Previous, 0};
      }
      if (contains(page.next, x, y)) {
        return {Kind::Next, 0};
      }
      if (contains(page.guide, x, y)) {
        return {Kind::GuideButton, 0};
      }
      return {};
    }
    case FrontPage::Settings: {
      const SettingsLayout s = settingsLayout(layout, config);
      if (contains(s.volumeDown, x, y)) {
        return {Kind::VolumeDown, 0};
      }
      if (contains(s.volumeUp, x, y)) {
        return {Kind::VolumeUp, 0};
      }
      if (contains(s.page.back, x, y)) {
        return {Kind::Back, 0};
      }
      for (std::size_t i = 0; i < s.rows.size(); ++i) {
        if (static_cast<SettingsRow>(i) != SettingsRow::Volume && contains(s.rows[i], x, y)) {
          return {Kind::Row, static_cast<std::uint8_t>(i)};
        }
      }
      return {};
    }
    case FrontPage::Guide:
      return {Kind::Anywhere, 0};
  }
  return {};
}

}  // namespace pinball::screen
