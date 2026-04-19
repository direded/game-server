#include "greeter/greeter.h"

#include <gtest/gtest.h>

TEST(GreeterTest, ReturnsConfiguredMessage) {
    EXPECT_EQ(game::greeter::get_greeting("Hello, world!"), "Hello, world!");
}

TEST(GreeterTest, EmptyMessageYieldsEmptyString) {
    EXPECT_EQ(game::greeter::get_greeting(""), "");
}

TEST(GreeterTest, PreservesWhitespaceAndPunctuation) {
    const std::string expected = "  <b>hi!</b>\n\t{json:1}";
    EXPECT_EQ(game::greeter::get_greeting(expected), expected);
}

TEST(GreeterTest, ReflectsMessageExactly) {
    const std::string custom = "custom-value-42";
    EXPECT_EQ(game::greeter::get_greeting(custom), custom);
}
