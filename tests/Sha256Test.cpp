#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/AssetManifest.hpp"
#include "core/Sha256.hpp"

namespace {

std::string hex(const pinball::Sha256Digest& digest) {
  static const char* const kDigits = "0123456789abcdef";
  std::string out;
  for (const std::uint8_t byte : digest) {
    out.push_back(kDigits[byte >> 4U]);
    out.push_back(kDigits[byte & 0x0FU]);
  }
  return out;
}

std::string hashOf(const std::string& text) {
  return hex(pinball::sha256(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

}  // namespace

void testSha256() {
  // Published test vectors (FIPS 180-4 examples and NIST CAVS).
  CHECK(hashOf("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(hashOf("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(hashOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK(hashOf("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklm"
               "nopqrlmnopqrsmnopqrstnopqrstu") ==
        "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
  CHECK(hashOf(std::string(1000000, 'a')) ==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

  {  // Every length around the block and padding boundaries, hashed in one
     // call and in uneven pieces, must agree.
    for (std::size_t length = 0; length <= 200; ++length) {
      std::vector<std::uint8_t> data(length);
      for (std::size_t i = 0; i < length; ++i) {
        data[i] = static_cast<std::uint8_t>((i * 131U + 7U) & 0xFFU);
      }
      const pinball::Sha256Digest whole = pinball::sha256(data.data(), data.size());
      pinball::Sha256 pieces;
      std::size_t offset = 0;
      std::size_t chunk = 1;
      while (offset < length) {
        const std::size_t take = chunk < length - offset ? chunk : length - offset;
        pieces.update(data.data() + offset, take);
        offset += take;
        chunk = chunk * 2 + 1;
      }
      CHECK(pieces.finish() == whole);
    }
  }

  {  // Null data with zero size is the empty message.
    pinball::Sha256 hasher;
    hasher.update(nullptr, 0);
    CHECK(hex(hasher.finish()) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  }

  {  // Manifest checks.
    const std::string content = "pinball asset bytes";
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(content.data());
    const pinball::AssetEntry entry{"sprites/x.png", content.size(),
                                    pinball::sha256(bytes, content.size())};
    CHECK(pinball::checkAsset(entry, bytes, content.size()) == pinball::AssetCheck::Ok);
    CHECK(pinball::checkAsset(entry, bytes, content.size() - 1) ==
          pinball::AssetCheck::WrongSize);

    std::string flipped = content;
    flipped[5] = static_cast<char>(flipped[5] ^ 0x01);
    CHECK(pinball::checkAsset(entry, reinterpret_cast<const std::uint8_t*>(flipped.data()),
                              flipped.size()) == pinball::AssetCheck::WrongDigest);

    const pinball::AssetEntry table[] = {entry};
    CHECK(pinball::findAsset(table, 1, "sprites/x.png") == &table[0]);
    CHECK(pinball::findAsset(table, 1, "sprites/y.png") == nullptr);
    CHECK(pinball::findAsset(table, 1, "") == nullptr);
    CHECK(pinball::findAsset(table, 0, "sprites/x.png") == nullptr);
    CHECK(pinball::findAsset(nullptr, 1, "sprites/x.png") == nullptr);
  }
}
