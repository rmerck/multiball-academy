#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/PngHeader.hpp"

namespace {

std::array<unsigned char, 24> header(std::uint32_t w, std::uint32_t h) {
  std::array<unsigned char, 24> d{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n',
                                  0,    0,   0,   13,  'I',  'H',  'D',  'R'};
  for (std::size_t i = 0; i < 4; ++i) {
    d[16 + i] = static_cast<unsigned char>((w >> (24U - 8U * i)) & 0xFFU);
    d[20 + i] = static_cast<unsigned char>((h >> (24U - 8U * i)) & 0xFFU);
  }
  return d;
}

}  // namespace

void testPngHeader() {
  using pinball::parsePngHeader;

  {
    const auto d = header(96, 64);
    const auto size = parsePngHeader(d);
    CHECK(size.has_value());
    CHECK(size && size->width == 96 && size->height == 64);
  }
  {  // Large but legal values are reported, not truncated.
    const auto d = header(0x7FFFFFFFU, 70000);
    const auto size = parsePngHeader(d.data(), d.size());
    CHECK(size && size->width == 0x7FFFFFFFU && size->height == 70000);
  }
  {  // Zero and over-range dimensions.
    CHECK(!parsePngHeader(header(0, 10).data(), 24));
    CHECK(!parsePngHeader(header(10, 0).data(), 24));
    CHECK(!parsePngHeader(header(0x80000000U, 10).data(), 24));
    CHECK(!parsePngHeader(header(10, 0xFFFFFFFFU).data(), 24));
  }
  {  // Short, null and corrupted input.
    const auto d = header(96, 96);
    CHECK(!parsePngHeader(nullptr, 24));
    CHECK(!parsePngHeader(d.data(), 23));
    CHECK(!parsePngHeader(d.data(), 0));
    CHECK(!parsePngHeader(std::span(d).first(23)));
    CHECK(!parsePngHeader(std::span<const std::uint8_t>{}));
    // Bytes after the header are not looked at.
    std::array<unsigned char, 64> longer{};
    std::ranges::copy(d, longer.begin());
    CHECK(parsePngHeader(longer).has_value());
    for (std::size_t i = 0; i < 16; ++i) {
      auto bad = d;
      bad[i] = static_cast<unsigned char>(bad[i] ^ 0x01U);
      CHECK(!parsePngHeader(bad.data(), bad.size()));
    }
  }
}
