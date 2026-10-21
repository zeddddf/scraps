#include "core/paste/TextTransformEngine.h"

#include "tests/TestSupport.h"

namespace clipd2d::tests {

CLIPD2D_TEST(TextTransformEnginePowerShellHereStringWrapsNormalizedMultilineText) {
  clipd2d::core::paste::TextTransformEngine engine;
  std::string output;
  const std::string input = "line1\r\nline2\rline3\xE2\x80\xA8line4\xE2\x80\xA9line5";

  const auto status = engine.Transform("powershell-here-string", input, &output);

  CLIPD2D_CHECK(status.ok());
  CLIPD2D_CHECK_EQ(output, std::string("@'\nline1\nline2\nline3\nline4\nline5\n'@"));
}

}  // namespace clipd2d::tests
