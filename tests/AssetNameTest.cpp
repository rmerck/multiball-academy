#include <string>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/AssetName.hpp"

void testAssetName() {
  using pinball::isSafeAssetName;

  CHECK(isSafeAssetName("sprites/ball.png"));
  CHECK(isSafeAssetName("table/background_2x.png"));
  CHECK(isSafeAssetName("a.png"));

  CHECK(!isSafeAssetName(""));
  CHECK(!isSafeAssetName(".png"));
  CHECK(!isSafeAssetName("ball"));
  CHECK(!isSafeAssetName("ball.bmp"));
  CHECK(!isSafeAssetName("/etc/passwd.png"));
  CHECK(!isSafeAssetName("../secret.png"));
  CHECK(!isSafeAssetName("sprites/../../secret.png"));
  CHECK(!isSafeAssetName("sprites//ball.png"));
  CHECK(!isSafeAssetName("sprites/./ball.png"));
  CHECK(!isSafeAssetName("sprites/.hidden.png"));
  CHECK(!isSafeAssetName("sprites/"));
  CHECK(!isSafeAssetName("sprites\\ball.png"));
  CHECK(!isSafeAssetName("Sprites/Ball.png"));
  CHECK(!isSafeAssetName("sprites/ball .png"));
  CHECK(!isSafeAssetName("c:/ball.png"));
  CHECK(!isSafeAssetName("~/ball.png"));
  CHECK(!isSafeAssetName(std::string("sprites/ball.png\0.png", 20)));
  CHECK(!isSafeAssetName("nul.png"));
  CHECK(!isSafeAssetName("con.png"));
  CHECK(!isSafeAssetName("aux/ball.png"));
  CHECK(!isSafeAssetName("sprites/com1.png"));
  CHECK(!isSafeAssetName("sprites/lpt9.extra.png"));
  CHECK(!isSafeAssetName("sprites./ball.png"));
  CHECK(isSafeAssetName("sprites/console.png"));
  CHECK(isSafeAssetName("sprites/com10.png"));
  CHECK(!isSafeAssetName(std::string(200, 'a') + ".png"));
}
