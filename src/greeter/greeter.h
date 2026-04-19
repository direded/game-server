#pragma once

#include <string>
#include <string_view>

namespace game::greeter {

std::string get_greeting(std::string_view configured_message);

} // namespace game::greeter
