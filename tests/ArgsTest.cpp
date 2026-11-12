#include <string>

#include "Check.hpp"
#include "core/Args.hpp"

void testArgs() {
  using pinball::parseFrameLimit;

  CHECK(parseFrameLimit("1") == 1U);
  CHECK(parseFrameLimit("120") == 120U);
  CHECK(parseFrameLimit("1000000") == pinball::kMaxFrameLimit);

  CHECK(!parseFrameLimit(""));
  CHECK(!parseFrameLimit("0"));
  CHECK(!parseFrameLimit("1000001"));
  CHECK(!parseFrameLimit("-5"));
  CHECK(!parseFrameLimit("+5"));
  CHECK(!parseFrameLimit(" 5"));
  CHECK(!parseFrameLimit("5 "));
  CHECK(!parseFrameLimit("5x"));
  CHECK(!parseFrameLimit("0x10"));
  CHECK(!parseFrameLimit("1e3"));
  CHECK(!parseFrameLimit("99999999999999999999"));

  // Embedded NUL is trailing junk, not a terminator.
  CHECK(!parseFrameLimit(std::string("5\0", 2)));
}
